#include "ai_model_storage.h"

#include <stdlib.h>
#include <string.h>

#include "../config/fat32_config.h"
#include "../core/hk_binary.h"
#include "fat32_file.h"
#include "fat32_volume.h"
#include "file_mount.h"
#include "file_path.h"

#define AI_MANIFEST_BYTES 256U
#define AI_MANIFEST_VERSION 1U
#define AI_MANIFEST_CRC_OFFSET 12U
#define AI_LABELS_MAX_BYTES 16384U

static uint8_t mounted(void)
{
    return hk_sd_present() && file_mount_if_needed() && hk_fat_mounted();
}

static void copy_field(char *dst, uint32_t dst_size, const uint8_t *src, uint32_t src_size)
{
    uint32_t count = 0U;

    while(count + 1U < dst_size && count < src_size && src[count])
    {
        dst[count] = (char)src[count];
        count++;
    }
    dst[count] = '\0';
}

ai_model_storage_result_t ai_model_storage_load(const char *directory_path,
                                                const char *model_path,
                                                uint32_t max_bytes,
                                                uint32_t alignment,
                                                uint8_t **buffer,
                                                uint32_t *size,
                                                uint32_t *crc32)
{
    fat_file_entry_t entry;
    uint8_t *raw;
    uint8_t *data;

    if(buffer) *buffer = NULL;
    if(size) *size = 0U;
    if(crc32) *crc32 = 0U;
    if(!mounted())
        return AI_MODEL_STORAGE_NO_SD;
    if(directory_path && directory_path[0] &&
       (file_path_find(directory_path, &entry) != FILE_PATH_OK || !(entry.attr & FAT_ATTR_DIR)))
        return AI_MODEL_STORAGE_DIRECTORY;
    if(!model_path || file_path_find(model_path, &entry) != FILE_PATH_OK ||
       (entry.attr & FAT_ATTR_DIR))
        return AI_MODEL_STORAGE_FILE;
    if(entry.size < 28U || !max_bytes || entry.size > max_bytes)
        return AI_MODEL_STORAGE_SIZE;
    if(alignment < sizeof(void *) || (alignment & (alignment - 1U)) != 0U)
        return AI_MODEL_STORAGE_ALLOC;

    raw = (uint8_t *)malloc(entry.size + alignment + sizeof(void *));
    if(!raw)
        return AI_MODEL_STORAGE_ALLOC;
    data = (uint8_t *)(((uintptr_t)(raw + sizeof(void *) + alignment - 1U)) &
                       ~(uintptr_t)(alignment - 1U));
    ((void **)data)[-1] = raw;
    if(!fat_file_read_at(&entry, 0U, data, entry.size))
    {
        free(raw);
        return AI_MODEL_STORAGE_READ;
    }
    if(buffer) *buffer = data;
    if(size) *size = entry.size;
    if(crc32) *crc32 = crc32_update(0U, data, entry.size);
    return AI_MODEL_STORAGE_OK;
}

void ai_model_storage_free(uint8_t *buffer)
{
    if(buffer)
        free(((void **)buffer)[-1]);
}

ai_model_storage_result_t ai_model_storage_load_manifest(const char *manifest_path,
                                                         ai_model_manifest_t *manifest)
{
    fat_file_entry_t entry;
    uint8_t raw[AI_MANIFEST_BYTES];
    uint32_t expected_crc;
    uint32_t actual_crc;

    if(!manifest_path || !manifest || !mounted())
        return AI_MODEL_STORAGE_NO_SD;
    if(file_path_find(manifest_path, &entry) != FILE_PATH_OK || (entry.attr & FAT_ATTR_DIR))
        return AI_MODEL_STORAGE_FILE;
    if(entry.size != sizeof(raw) || !fat_file_read_at(&entry, 0U, raw, sizeof(raw)))
        return AI_MODEL_STORAGE_MANIFEST;
    if(memcmp(raw, "HKAI", 4U) != 0 || rd16(raw + 4U) != AI_MANIFEST_VERSION ||
       rd16(raw + 6U) != sizeof(raw))
        return AI_MODEL_STORAGE_MANIFEST;
    expected_crc = rd32(raw + 8U);
    actual_crc = crc32_update(0U, raw + AI_MANIFEST_CRC_OFFSET,
                              sizeof(raw) - AI_MANIFEST_CRC_OFFSET);
    if(expected_crc != actual_crc)
        return AI_MODEL_STORAGE_MANIFEST;

    memset(manifest, 0, sizeof(*manifest));
    manifest->model_size = rd32(raw + 12U);
    manifest->model_crc32 = rd32(raw + 16U);
    manifest->kmodel.version = rd32(raw + 20U);
    manifest->kmodel.flags = rd32(raw + 24U);
    manifest->kmodel.arch = rd32(raw + 28U);
    manifest->kmodel.layers_length = rd32(raw + 32U);
    manifest->kmodel.max_start_address = rd32(raw + 36U);
    manifest->kmodel.main_mem_usage = rd32(raw + 40U);
    manifest->kmodel.output_count = rd32(raw + 44U);
    manifest->kmodel.input_bytes = rd32(raw + 48U);
    for(uint8_t i = 0U; i < AI_MODEL_MAX_TENSOR_DIMS; i++)
        manifest->input_shape[i] = rd16(raw + 52U + i * 2U);
    manifest->input_rank = raw[60U];
    manifest->input_element = (ai_model_element_t)raw[61U];
    manifest->input_layout = (ai_model_layout_t)raw[62U];
    manifest->normalization = (ai_model_normalization_t)raw[63U];
    manifest->postprocess = (ai_model_postprocess_t)raw[64U];
    manifest->output_count = raw[65U];
    manifest->label_count = rd16(raw + 66U);
    for(uint8_t i = 0U; i < AI_MODEL_MAX_OUTPUTS; i++)
        manifest->output_bytes[i] = rd32(raw + 68U + i * 4U);
    copy_field(manifest->id, sizeof(manifest->id), raw + 84U, 32U);
    copy_field(manifest->model_file, sizeof(manifest->model_file), raw + 116U, 64U);
    copy_field(manifest->labels_file, sizeof(manifest->labels_file), raw + 180U, 64U);
    return AI_MODEL_STORAGE_OK;
}

ai_model_storage_result_t ai_model_storage_validate_labels(
    const char *labels_path, uint16_t expected_count)
{
    fat_file_entry_t entry;
    uint8_t buffer[128];
    uint32_t offset = 0U;
    uint16_t count = 0U;
    uint8_t have_text = 0U;

    if(!labels_path || !labels_path[0] || !expected_count)
        return AI_MODEL_STORAGE_MANIFEST;
    if(!mounted())
        return AI_MODEL_STORAGE_NO_SD;
    if(file_path_find(labels_path, &entry) != FILE_PATH_OK ||
       (entry.attr & FAT_ATTR_DIR))
        return AI_MODEL_STORAGE_FILE;
    if(!entry.size || entry.size > AI_LABELS_MAX_BYTES)
        return AI_MODEL_STORAGE_MANIFEST;

    while(offset < entry.size)
    {
        uint32_t remaining = entry.size - offset;
        uint32_t chunk = remaining < sizeof(buffer) ? remaining : sizeof(buffer);

        if(!fat_file_read_at(&entry, offset, buffer, chunk))
            return AI_MODEL_STORAGE_READ;
        for(uint32_t index = 0U; index < chunk; index++)
        {
            uint8_t value = buffer[index];

            if(value == '\n')
            {
                if(!have_text || count == UINT16_MAX)
                    return AI_MODEL_STORAGE_MANIFEST;
                count++;
                have_text = 0U;
            }
            else if(value != '\r')
            {
                if(value < 0x20U)
                    return AI_MODEL_STORAGE_MANIFEST;
                have_text = 1U;
            }
        }
        offset += chunk;
    }
    if(have_text)
        count++;
    return count == expected_count ?
           AI_MODEL_STORAGE_OK : AI_MODEL_STORAGE_MANIFEST;
}

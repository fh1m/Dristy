#include "file_path.h"

#include <stdio.h>
#include <string.h>

#include "../config/fat32_config.h"
#include "../config/sd_config.h"
#include "../core/file_name.h"
#include "../core/hk_binary.h"
#include "../core/hk_string.h"
#include "sd_card.h"
#include "fat32_allocation.h"
#include "fat32_volume.h"
#include "internal/fat32_state_private.h"

static void file_path_short_name(const uint8_t *raw, char *name, size_t name_size)
{
    char base[9] = {0};
    char ext[4] = {0};
    uint8_t base_len = 0;
    uint8_t ext_len = 0;

    for(uint8_t i = 0; i < 8 && raw[i] != ' '; i++)
        base[base_len++] = (char)raw[i];
    for(uint8_t i = 8; i < 11 && raw[i] != ' '; i++)
        ext[ext_len++] = (char)raw[i];
    if(ext[0])
        snprintf(name, name_size, "%s.%s", base, ext);
    else
        snprintf(name, name_size, "%s", base);
}

static void file_path_lfn_store(const uint8_t *raw, uint16_t *lfn, size_t lfn_units)
{
    static const uint8_t positions[13] = {1, 3, 5, 7, 9, 14, 16, 18, 20, 22, 24, 28, 30};
    uint8_t sequence = (uint8_t)(raw[0] & 0x1FU);
    uint16_t offset;

    if(raw[0] & 0x40U)
        memset(lfn, 0, lfn_units * sizeof(*lfn));
    if(sequence == 0)
        return;
    offset = (uint16_t)(sequence - 1U) * 13U;
    for(uint8_t i = 0; i < 13 && offset + i + 1U < lfn_units; i++)
    {
        uint16_t character = rd16(&raw[positions[i]]);
        if(character == 0x0000U || character == 0xFFFFU)
        {
            lfn[offset + i] = 0U;
            break;
        }
        lfn[offset + i] = character;
    }
}

static file_path_result_t file_path_find_in_directory(uint32_t directory_cluster, const char *wanted,
                                                      fat_file_entry_t *found)
{
    uint16_t lfn[FILE_LFN_MAX_UNITS] = {0};
    uint32_t current = directory_cluster;
    uint32_t ordinal = 0;
    uint8_t lfn_count = 0;
    uint8_t sectors_per_cluster = fat32_sectors_per_cluster();
    uint8_t *sector = fat32_sector_scratch();

    while(current >= 2U && current < FAT32_EOC)
    {
        uint32_t base = fat_cluster_lba(current);
        for(uint8_t sector_index = 0; sector_index < sectors_per_cluster; sector_index++)
        {
            if(!sd_card_read_block(base + sector_index, sector))
                return FILE_PATH_IO;
            for(uint16_t offset = 0; offset < SD_BLOCK_SIZE; offset += 32U)
            {
                const uint8_t *raw = &sector[offset];
                uint8_t attr = raw[11];
                char name[FILE_NAME_MAX];
                uint32_t entry_ordinal = ordinal++;

                if(raw[0] == 0x00U)
                    return FILE_PATH_NOT_FOUND;
                if(raw[0] == 0xE5U)
                {
                    lfn[0] = 0U;
                    lfn_count = 0;
                    continue;
                }
                if(attr == 0x0FU)
                {
                    file_path_lfn_store(raw, lfn, FILE_LFN_MAX_UNITS);
                    if(lfn_count < 20U)
                        lfn_count++;
                    continue;
                }
                if((attr & 0x08U) || raw[0] == '.')
                {
                    lfn[0] = 0U;
                    lfn_count = 0;
                    continue;
                }
                if(lfn[0])
                    utf16_to_utf8(lfn, FILE_LFN_MAX_UNITS, name, sizeof(name));
                else
                    file_path_short_name(raw, name, sizeof(name));
                lfn[0] = 0U;
                if(str_eq_ci(name, wanted))
                {
                    snprintf(found->name, sizeof(found->name), "%s", name);
                    found->attr = attr;
                    found->cluster = ((uint32_t)rd16(&raw[20]) << 16) | rd16(&raw[26]);
                    found->size = rd32(&raw[28]);
                    found->modified = ((uint32_t)rd16(&raw[24]) << 16) | rd16(&raw[22]);
                    found->dir_ordinal = entry_ordinal;
                    found->lfn_count = lfn_count;
                    return FILE_PATH_OK;
                }
                lfn_count = 0;
            }
        }
        current = fat_next_cluster(current);
    }
    return FILE_PATH_NOT_FOUND;
}

file_path_result_t file_path_find(const char *path, fat_file_entry_t *entry)
{
    fat_file_entry_t current_entry;
    uint32_t directory_cluster;
    const char *cursor;

    if(!path || !entry || !hk_fat_mounted())
        return FILE_PATH_INVALID;
    cursor = path;
    while(*cursor == '/')
        cursor++;
    if(!*cursor)
        return FILE_PATH_INVALID;
    directory_cluster = hk_fat_root_cluster();
    while(*cursor)
    {
        const char *separator = strchr(cursor, '/');
        size_t length = separator ? (size_t)(separator - cursor) : strlen(cursor);
        char component[FILE_NAME_MAX];
        file_path_result_t result;

        if(length == 0U || length >= sizeof(component))
            return FILE_PATH_INVALID;
        memcpy(component, cursor, length);
        component[length] = '\0';
        result = file_path_find_in_directory(directory_cluster, component, &current_entry);
        if(result != FILE_PATH_OK)
            return result;
        if(!separator)
        {
            *entry = current_entry;
            return FILE_PATH_OK;
        }
        if(!(current_entry.attr & FAT_ATTR_DIR))
            return FILE_PATH_NOT_DIRECTORY;
        directory_cluster = current_entry.cluster;
        cursor = separator + 1;
        while(*cursor == '/')
            cursor++;
        if(!*cursor)
        {
            *entry = current_entry;
            return FILE_PATH_OK;
        }
    }
    return FILE_PATH_INVALID;
}

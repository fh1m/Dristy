#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "hk_binary.h"
#include "internal_flash.h"
#include "userfs.h"

#define FLASH_BYTES 0x00390000U
#define ERASE_BYTES 4096U

static uint8_t g_flash[FLASH_BYTES];
static int32_t g_fail_after = -1;
static uint32_t g_mutations;

static int fail_now(void)
{
    uint32_t mutation = g_mutations++;
    return g_fail_after >= 0 && mutation == (uint32_t)g_fail_after;
}

internal_flash_result_t internal_flash_init(uint32_t hz)
{
    return hz ? INTERNAL_FLASH_OK : INTERNAL_FLASH_ERROR_INVALID_ARGUMENT;
}

void internal_flash_get_info(internal_flash_info_t *info)
{
    memset(info, 0, sizeof(*info));
    info->jedec_id[0] = 0xEF;
    info->jedec_id[1] = 0x40;
    info->jedec_id[2] = 0x18;
    info->capacity = 0x01000000U;
    info->initialized = 1U;
}

internal_flash_result_t internal_flash_partition_info(
    internal_flash_partition_id_t partition,
    internal_flash_partition_info_t *info)
{
    if(partition != INTERNAL_FLASH_PARTITION_USERFS || !info)
        return INTERNAL_FLASH_ERROR_INVALID_ARGUMENT;
    memset(info, 0, sizeof(*info));
    info->name = "userfs";
    info->offset = 0x00C70000U;
    info->size = FLASH_BYTES;
    info->required_capacity = 0x01000000U;
    info->erase_size = ERASE_BYTES;
    info->program_size = 256U;
    info->runtime_writable = 1U;
    info->available = 1U;
    return INTERNAL_FLASH_OK;
}

internal_flash_result_t internal_flash_read(
    internal_flash_partition_id_t partition, uint32_t offset,
    uint8_t *data, size_t length)
{
    if(partition != INTERNAL_FLASH_PARTITION_USERFS ||
       offset > FLASH_BYTES || length > FLASH_BYTES - offset ||
       (length && !data))
        return INTERNAL_FLASH_ERROR_OUT_OF_BOUNDS;
    memcpy(data, g_flash + offset, length);
    return INTERNAL_FLASH_OK;
}

internal_flash_result_t internal_flash_program(
    internal_flash_partition_id_t partition, uint32_t offset,
    const uint8_t *data, size_t length)
{
    size_t apply = length;

    if(partition != INTERNAL_FLASH_PARTITION_USERFS ||
       offset > FLASH_BYTES || length > FLASH_BYTES - offset ||
       (length && !data))
        return INTERNAL_FLASH_ERROR_OUT_OF_BOUNDS;
    if(fail_now())
        apply /= 2U;
    for(size_t i = 0; i < apply; i++)
    {
        if((g_flash[offset + i] & data[i]) != data[i])
            return INTERNAL_FLASH_ERROR_FAULTED;
        g_flash[offset + i] &= data[i];
    }
    return apply == length ? INTERNAL_FLASH_OK : INTERNAL_FLASH_ERROR_FAULTED;
}

internal_flash_result_t internal_flash_erase(
    internal_flash_partition_id_t partition, uint32_t offset, size_t length)
{
    size_t apply = length;

    if(partition != INTERNAL_FLASH_PARTITION_USERFS ||
       offset % ERASE_BYTES || length % ERASE_BYTES ||
       offset > FLASH_BYTES || length > FLASH_BYTES - offset)
        return INTERNAL_FLASH_ERROR_ALIGNMENT;
    if(fail_now())
        apply /= 2U;
    memset(g_flash + offset, 0xFF, apply);
    return apply == length ? INTERNAL_FLASH_OK : INTERNAL_FLASH_ERROR_FAULTED;
}

uint8_t internal_flash_range_valid(uint32_t partition_size,
                                   uint32_t offset, size_t length)
{
    return offset <= partition_size && length <= partition_size - offset;
}

const char *internal_flash_result_name(internal_flash_result_t result)
{
    (void)result;
    return "fake";
}

static void require(int condition, const char *message)
{
    if(!condition)
    {
        fprintf(stderr, "FAIL: %s\n", message);
        exit(1);
    }
}

static void upload(const char *name, const uint8_t *data, size_t size)
{
    uint32_t id = 0U;
    uint32_t crc = crc32_update(0U, data, size);
    size_t offset = 0U;

    require(userfs_upload_begin(name, (uint32_t)size, crc, &id) == USERFS_OK,
            "upload begin");
    while(offset < size)
    {
        size_t chunk = size - offset > 173U ? 173U : size - offset;
        require(userfs_upload_write(id, (uint32_t)offset,
                                    data + offset, chunk) == USERFS_OK,
                "upload chunk");
        offset += chunk;
    }
    require(userfs_upload_commit(id) == USERFS_OK, "upload commit");
}

static size_t read_file(const char *name, uint8_t *data, size_t capacity)
{
    size_t total = 0U;

    while(total < capacity)
    {
        size_t count = 0U;
        userfs_result_t result = userfs_read(name, (uint32_t)total,
                                             data + total,
                                             capacity - total, &count);
        if(result != USERFS_OK)
            return 0U;
        total += count;
        if(!count)
            break;
    }
    return total;
}

static uint8_t list_count(const char *name, uint32_t size, void *context)
{
    uint32_t *count = context;
    require(name[0] != '.', "hidden temporary listed");
    require(size > 0U, "listed empty script");
    (*count)++;
    return 1U;
}

static userfs_result_t try_replacement(const uint8_t *data, size_t size)
{
    uint32_t id = 0U;
    uint32_t crc = crc32_update(0U, data, size);
    size_t offset = 0U;
    userfs_result_t result = userfs_upload_begin("main.py", (uint32_t)size,
                                                 crc, &id);

    while(result == USERFS_OK && offset < size)
    {
        size_t chunk = size - offset > 211U ? 211U : size - offset;
        result = userfs_upload_write(id, (uint32_t)offset,
                                     data + offset, chunk);
        offset += chunk;
    }
    if(result == USERFS_OK)
        result = userfs_upload_commit(id);
    return result;
}

int main(void)
{
    const size_t old_size = 7000U;
    const size_t new_size = 12000U;
    uint8_t *old_script;
    uint8_t *new_script;
    uint8_t *readback;
    uint8_t *snapshot;
    char startup[64];
    uint32_t listed = 0U;
    uint32_t mutation_count;

    old_script = malloc(old_size);
    new_script = malloc(new_size);
    readback = malloc(new_size + 1U);
    require(old_script && new_script && readback, "allocate scripts");
    for(size_t i = 0; i < old_size; i++)
        old_script[i] = (uint8_t)('a' + (i % 23U));
    for(size_t i = 0; i < new_size; i++)
        new_script[i] = (uint8_t)('A' + (i % 19U));
    memcpy(old_script, "# old script\n", 13U);
    memcpy(new_script, "# new script\n", 13U);

    memset(g_flash, 0xFF, sizeof(g_flash));
    require(userfs_mount() == USERFS_ERROR_UNFORMATTED,
            "blank flash must not auto-format");
    require(userfs_format_explicit() == USERFS_OK, "explicit format");
    upload("main.py", old_script, old_size);
    require(userfs_set_startup("main.py") == USERFS_OK, "set startup");
    require(userfs_get_startup(startup, sizeof(startup)) == USERFS_OK &&
            strcmp(startup, "main.py") == 0, "get startup");
    require(userfs_list(list_count, &listed) == USERFS_OK && listed == 1U,
            "list scripts");
    require(read_file("main.py", readback, new_size + 1U) == old_size &&
            memcmp(readback, old_script, old_size) == 0,
            "read old script");

    require(userfs_unmount() == USERFS_OK, "unmount baseline");
    snapshot = malloc(sizeof(g_flash));
    require(snapshot != NULL, "allocate snapshot");
    memcpy(snapshot, g_flash, sizeof(g_flash));

    memcpy(g_flash, snapshot, sizeof(g_flash));
    userfs_test_power_cycle();
    require(userfs_mount() == USERFS_OK, "mount mutation counter");
    g_fail_after = -1;
    g_mutations = 0U;
    require(try_replacement(new_script, new_size) == USERFS_OK,
            "uninterrupted replacement");
    mutation_count = g_mutations;
    require(mutation_count > 0U, "replacement must mutate flash");

    for(uint32_t cut = 0U; cut < mutation_count; cut++)
    {
        size_t size;
        int is_old;
        int is_new;

        memcpy(g_flash, snapshot, sizeof(g_flash));
        userfs_test_power_cycle();
        require(userfs_mount() == USERFS_OK, "mount before power cut");
        g_mutations = 0U;
        g_fail_after = (int32_t)cut;
        (void)try_replacement(new_script, new_size);

        g_fail_after = -1;
        userfs_test_power_cycle();
        require(userfs_mount() == USERFS_OK, "remount after power cut");
        memset(readback, 0, new_size + 1U);
        size = read_file("main.py", readback, new_size + 1U);
        is_old = size == old_size &&
                 memcmp(readback, old_script, size) == 0;
        is_new = size == new_size &&
                 memcmp(readback, new_script, size) == 0;
        require(is_old || is_new, "atomic replace exposed partial script");
    }

    /* A rejected CRC must never publish a target and must remain abortable. */
    {
        static const uint8_t invalid[] = "print('bad crc')\n";
        uint32_t id = 0U;
        uint32_t size = 0U;

        require(userfs_upload_begin("bad.py", sizeof(invalid) - 1U,
                                    0x12345678U, &id) == USERFS_OK,
                "begin bad CRC upload");
        require(userfs_upload_write(id, 0U, invalid,
                                    sizeof(invalid) - 1U) == USERFS_OK,
                "write bad CRC upload");
        require(userfs_upload_commit(id) == USERFS_ERROR_CRC,
                "reject bad CRC upload");
        require(userfs_stat("bad.py", &size) == USERFS_ERROR_NOT_FOUND,
                "pending failed upload remains isolated");
        require(userfs_upload_abort(id) == USERFS_OK,
                "abort failed CRC upload");
        require(userfs_stat("bad.py", &size) == USERFS_ERROR_NOT_FOUND,
                "bad CRC target absent");
    }

    /* Deleting the selected file clears startup metadata before the file. */
    require(userfs_remove("main.py") == USERFS_OK,
            "delete startup program");
    require(userfs_get_startup(startup, sizeof(startup)) ==
                USERFS_ERROR_NOT_FOUND,
            "delete clears startup selection");
    listed = 0U;
    require(userfs_list(list_count, &listed) == USERFS_OK && listed == 0U,
            "delete removes program");

    g_fail_after = -1;
    free(snapshot);
    free(readback);
    free(new_script);
    free(old_script);
    printf("USERFS_OK mutations=%u power_cuts=%u\n",
           mutation_count, mutation_count);
    return 0;
}

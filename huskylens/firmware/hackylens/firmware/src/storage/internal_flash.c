#include "internal_flash.h"

#include <stddef.h>
#include <string.h>

#include "flash_layout.h"
#include "../drivers/boot_flash.h"

typedef struct
{
    const char *name;
    uint32_t offset;
    uint32_t size;
    uint32_t required_capacity;
    uint8_t runtime_writable;
} internal_flash_partition_t;

static const internal_flash_partition_t g_partitions[INTERNAL_FLASH_PARTITION_COUNT] = {
    [INTERNAL_FLASH_PARTITION_FIRMWARE] = {
        "firmware",
        HK_FLASH_PARTITION_FIRMWARE_OFFSET,
        HK_FLASH_PARTITION_FIRMWARE_SIZE,
        HK_FLASH_PARTITION_FIRMWARE_REQUIRED_CAPACITY,
        HK_FLASH_PARTITION_FIRMWARE_RUNTIME_WRITABLE,
    },
    [INTERNAL_FLASH_PARTITION_SETTINGS_0] = {
        "settings_0",
        HK_FLASH_PARTITION_SETTINGS_0_OFFSET,
        HK_FLASH_PARTITION_SETTINGS_0_SIZE,
        HK_FLASH_PARTITION_SETTINGS_0_REQUIRED_CAPACITY,
        HK_FLASH_PARTITION_SETTINGS_0_RUNTIME_WRITABLE,
    },
    [INTERNAL_FLASH_PARTITION_SETTINGS_1] = {
        "settings_1",
        HK_FLASH_PARTITION_SETTINGS_1_OFFSET,
        HK_FLASH_PARTITION_SETTINGS_1_SIZE,
        HK_FLASH_PARTITION_SETTINGS_1_REQUIRED_CAPACITY,
        HK_FLASH_PARTITION_SETTINGS_1_RUNTIME_WRITABLE,
    },
    [INTERNAL_FLASH_PARTITION_LEGACY_RESERVED] = {
        "legacy_reserved",
        HK_FLASH_PARTITION_LEGACY_RESERVED_OFFSET,
        HK_FLASH_PARTITION_LEGACY_RESERVED_SIZE,
        HK_FLASH_PARTITION_LEGACY_RESERVED_REQUIRED_CAPACITY,
        HK_FLASH_PARTITION_LEGACY_RESERVED_RUNTIME_WRITABLE,
    },
    [INTERNAL_FLASH_PARTITION_USERFS] = {
        "userfs",
        HK_FLASH_PARTITION_USERFS_OFFSET,
        HK_FLASH_PARTITION_USERFS_SIZE,
        HK_FLASH_PARTITION_USERFS_REQUIRED_CAPACITY,
        HK_FLASH_PARTITION_USERFS_RUNTIME_WRITABLE,
    },
};

static internal_flash_info_t g_internal_flash_info;

_Static_assert((unsigned)INTERNAL_FLASH_PARTITION_COUNT ==
               (unsigned)HK_FLASH_PARTITION_COUNT,
               "partition enums disagree with generated flash layout");
_Static_assert(HK_FLASH_PARTITION_FIRMWARE_OFFSET +
               HK_FLASH_PARTITION_FIRMWARE_SIZE ==
               HK_FLASH_PARTITION_SETTINGS_0_OFFSET,
               "firmware/settings boundary changed");
_Static_assert(HK_FLASH_PARTITION_SETTINGS_0_OFFSET +
               HK_FLASH_PARTITION_SETTINGS_0_SIZE ==
               HK_FLASH_PARTITION_SETTINGS_1_OFFSET,
               "settings slots are not adjacent");
_Static_assert(HK_FLASH_PARTITION_SETTINGS_1_OFFSET +
               HK_FLASH_PARTITION_SETTINGS_1_SIZE ==
               HK_FLASH_PARTITION_LEGACY_RESERVED_OFFSET,
               "settings/reserved boundary changed");
_Static_assert(HK_FLASH_PARTITION_LEGACY_RESERVED_OFFSET +
               HK_FLASH_PARTITION_LEGACY_RESERVED_SIZE ==
               HK_FLASH_PARTITION_USERFS_OFFSET,
               "reserved/userfs boundary changed");
_Static_assert(HK_FLASH_PARTITION_USERFS_OFFSET +
               HK_FLASH_PARTITION_USERFS_SIZE == HK_FLASH_MAX_CAPACITY,
               "userfs must end at flash capacity");

static const internal_flash_partition_t *internal_flash_partition(
    internal_flash_partition_id_t id)
{
    if((unsigned)id >= INTERNAL_FLASH_PARTITION_COUNT)
        return NULL;
    return &g_partitions[id];
}

static internal_flash_result_t internal_flash_from_boot(
    boot_flash_result_t result)
{
    switch(result)
    {
    case BOOT_FLASH_OK:
        return INTERNAL_FLASH_OK;
    case BOOT_FLASH_ERROR_INVALID_ARGUMENT:
        return INTERNAL_FLASH_ERROR_INVALID_ARGUMENT;
    case BOOT_FLASH_ERROR_NOT_INITIALIZED:
        return INTERNAL_FLASH_ERROR_NOT_INITIALIZED;
    case BOOT_FLASH_ERROR_WRONG_CORE:
        return INTERNAL_FLASH_ERROR_WRONG_CORE;
    case BOOT_FLASH_ERROR_UNSUPPORTED_DEVICE:
        return INTERNAL_FLASH_ERROR_UNSUPPORTED_DEVICE;
    case BOOT_FLASH_ERROR_OUT_OF_BOUNDS:
        return INTERNAL_FLASH_ERROR_OUT_OF_BOUNDS;
    case BOOT_FLASH_ERROR_ALIGNMENT:
        return INTERNAL_FLASH_ERROR_ALIGNMENT;
    case BOOT_FLASH_ERROR_TIMEOUT:
        return INTERNAL_FLASH_ERROR_TIMEOUT;
    case BOOT_FLASH_ERROR_WRITE_ENABLE:
        return INTERNAL_FLASH_ERROR_WRITE_ENABLE;
    case BOOT_FLASH_ERROR_FAULTED:
    default:
        return INTERNAL_FLASH_ERROR_FAULTED;
    }
}

static void internal_flash_refresh_info(void)
{
    boot_flash_info_t info;
    boot_flash_get_info(&info);
    memcpy(g_internal_flash_info.jedec_id, info.jedec_id,
           sizeof(g_internal_flash_info.jedec_id));
    g_internal_flash_info.capacity = info.capacity;
    g_internal_flash_info.initialized = info.initialized;
    g_internal_flash_info.faulted = info.faulted;
}

static internal_flash_result_t internal_flash_validate(
    internal_flash_partition_id_t id, uint32_t offset, size_t len,
    uint8_t require_writable, const internal_flash_partition_t **partition)
{
    const internal_flash_partition_t *selected = internal_flash_partition(id);

    internal_flash_refresh_info();
    if(!g_internal_flash_info.initialized)
        return INTERNAL_FLASH_ERROR_NOT_INITIALIZED;
    if(g_internal_flash_info.faulted)
        return INTERNAL_FLASH_ERROR_FAULTED;
    if(selected == NULL)
        return INTERNAL_FLASH_ERROR_INVALID_ARGUMENT;
    if(selected->required_capacity > g_internal_flash_info.capacity)
        return INTERNAL_FLASH_ERROR_PARTITION_UNAVAILABLE;
    if(!internal_flash_range_valid(selected->size, offset, len))
        return INTERNAL_FLASH_ERROR_OUT_OF_BOUNDS;
    if(require_writable && !selected->runtime_writable)
        return INTERNAL_FLASH_ERROR_READ_ONLY;
    if(partition != NULL)
        *partition = selected;
    return INTERNAL_FLASH_OK;
}

internal_flash_result_t internal_flash_init(uint32_t hz)
{
    internal_flash_result_t result = internal_flash_from_boot(boot_flash_init(hz));
    internal_flash_refresh_info();
    return result;
}

void internal_flash_get_info(internal_flash_info_t *info)
{
    if(info == NULL)
        return;
    internal_flash_refresh_info();
    *info = g_internal_flash_info;
}

internal_flash_result_t internal_flash_partition_info(
    internal_flash_partition_id_t id, internal_flash_partition_info_t *info)
{
    const internal_flash_partition_t *partition = internal_flash_partition(id);

    if(partition == NULL || info == NULL)
        return INTERNAL_FLASH_ERROR_INVALID_ARGUMENT;
    internal_flash_refresh_info();
    info->name = partition->name;
    info->offset = partition->offset;
    info->size = partition->size;
    info->required_capacity = partition->required_capacity;
    info->erase_size = HK_FLASH_ERASE_SIZE;
    info->program_size = HK_FLASH_PROGRAM_SIZE;
    info->runtime_writable = partition->runtime_writable;
    info->available = g_internal_flash_info.initialized &&
                      !g_internal_flash_info.faulted &&
                      partition->required_capacity <= g_internal_flash_info.capacity;
    return g_internal_flash_info.initialized ? INTERNAL_FLASH_OK :
           INTERNAL_FLASH_ERROR_NOT_INITIALIZED;
}

internal_flash_result_t internal_flash_read(
    internal_flash_partition_id_t id, uint32_t offset,
    uint8_t *data, size_t len)
{
    const internal_flash_partition_t *partition;
    internal_flash_result_t result = internal_flash_validate(
        id, offset, len, 0U, &partition);

    if(result != INTERNAL_FLASH_OK)
        return result;
    if(len != 0U && data == NULL)
        return INTERNAL_FLASH_ERROR_INVALID_ARGUMENT;
    return internal_flash_from_boot(
        boot_flash_read(partition->offset + offset, data, len));
}

internal_flash_result_t internal_flash_erase(
    internal_flash_partition_id_t id, uint32_t offset, size_t len)
{
    const internal_flash_partition_t *partition;
    internal_flash_result_t result = internal_flash_validate(
        id, offset, len, 1U, &partition);

    if(result != INTERNAL_FLASH_OK)
        return result;
    if(offset % HK_FLASH_ERASE_SIZE || len % HK_FLASH_ERASE_SIZE)
        return INTERNAL_FLASH_ERROR_ALIGNMENT;
    while(len != 0U)
    {
        result = internal_flash_from_boot(
            boot_flash_sector_erase(partition->offset + offset));
        if(result != INTERNAL_FLASH_OK)
            return result;
        offset += HK_FLASH_ERASE_SIZE;
        len -= HK_FLASH_ERASE_SIZE;
    }
    return INTERNAL_FLASH_OK;
}

internal_flash_result_t internal_flash_program(
    internal_flash_partition_id_t id, uint32_t offset,
    const uint8_t *data, size_t len)
{
    const internal_flash_partition_t *partition;
    internal_flash_result_t result = internal_flash_validate(
        id, offset, len, 1U, &partition);

    if(result != INTERNAL_FLASH_OK)
        return result;
    if(len != 0U && data == NULL)
        return INTERNAL_FLASH_ERROR_INVALID_ARGUMENT;
    return internal_flash_from_boot(
        boot_flash_program(partition->offset + offset, data, len));
}

uint8_t internal_flash_range_valid(uint32_t partition_size,
                                   uint32_t offset, size_t len)
{
    return offset <= partition_size &&
           len <= (size_t)(partition_size - offset);
}

const char *internal_flash_result_name(internal_flash_result_t result)
{
    switch(result)
    {
    case INTERNAL_FLASH_OK:
        return "ok";
    case INTERNAL_FLASH_ERROR_INVALID_ARGUMENT:
        return "invalid-argument";
    case INTERNAL_FLASH_ERROR_NOT_INITIALIZED:
        return "not-initialized";
    case INTERNAL_FLASH_ERROR_WRONG_CORE:
        return "wrong-core";
    case INTERNAL_FLASH_ERROR_UNSUPPORTED_DEVICE:
        return "unsupported-device";
    case INTERNAL_FLASH_ERROR_PARTITION_UNAVAILABLE:
        return "partition-unavailable";
    case INTERNAL_FLASH_ERROR_OUT_OF_BOUNDS:
        return "out-of-bounds";
    case INTERNAL_FLASH_ERROR_ALIGNMENT:
        return "alignment";
    case INTERNAL_FLASH_ERROR_READ_ONLY:
        return "read-only";
    case INTERNAL_FLASH_ERROR_TIMEOUT:
        return "timeout";
    case INTERNAL_FLASH_ERROR_WRITE_ENABLE:
        return "write-enable";
    case INTERNAL_FLASH_ERROR_FAULTED:
        return "faulted";
    default:
        return "unknown";
    }
}

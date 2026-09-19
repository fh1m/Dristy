#ifndef HK_INTERNAL_FLASH_H
#define HK_INTERNAL_FLASH_H

#include <stddef.h>
#include <stdint.h>

typedef enum
{
    INTERNAL_FLASH_PARTITION_FIRMWARE = 0,
    INTERNAL_FLASH_PARTITION_SETTINGS_0,
    INTERNAL_FLASH_PARTITION_SETTINGS_1,
    INTERNAL_FLASH_PARTITION_LEGACY_RESERVED,
    INTERNAL_FLASH_PARTITION_USERFS,
    INTERNAL_FLASH_PARTITION_COUNT
} internal_flash_partition_id_t;

typedef enum
{
    INTERNAL_FLASH_OK = 0,
    INTERNAL_FLASH_ERROR_INVALID_ARGUMENT,
    INTERNAL_FLASH_ERROR_NOT_INITIALIZED,
    INTERNAL_FLASH_ERROR_WRONG_CORE,
    INTERNAL_FLASH_ERROR_UNSUPPORTED_DEVICE,
    INTERNAL_FLASH_ERROR_PARTITION_UNAVAILABLE,
    INTERNAL_FLASH_ERROR_OUT_OF_BOUNDS,
    INTERNAL_FLASH_ERROR_ALIGNMENT,
    INTERNAL_FLASH_ERROR_READ_ONLY,
    INTERNAL_FLASH_ERROR_TIMEOUT,
    INTERNAL_FLASH_ERROR_WRITE_ENABLE,
    INTERNAL_FLASH_ERROR_FAULTED
} internal_flash_result_t;

typedef struct
{
    const char *name;
    uint32_t offset;
    uint32_t size;
    uint32_t required_capacity;
    uint32_t erase_size;
    uint32_t program_size;
    uint8_t runtime_writable;
    uint8_t available;
} internal_flash_partition_info_t;

typedef struct
{
    uint8_t jedec_id[3];
    uint32_t capacity;
    uint8_t initialized;
    uint8_t faulted;
} internal_flash_info_t;

internal_flash_result_t internal_flash_init(uint32_t hz);
void internal_flash_get_info(internal_flash_info_t *info);
internal_flash_result_t internal_flash_partition_info(
    internal_flash_partition_id_t partition,
    internal_flash_partition_info_t *info);

internal_flash_result_t internal_flash_read(
    internal_flash_partition_id_t partition, uint32_t offset,
    uint8_t *data, size_t len);
internal_flash_result_t internal_flash_erase(
    internal_flash_partition_id_t partition, uint32_t offset, size_t len);
internal_flash_result_t internal_flash_program(
    internal_flash_partition_id_t partition, uint32_t offset,
    const uint8_t *data, size_t len);

/* Overflow-safe range predicate kept independent of hardware for host tests. */
uint8_t internal_flash_range_valid(uint32_t partition_size,
                                   uint32_t offset, size_t len);
const char *internal_flash_result_name(internal_flash_result_t result);

#endif

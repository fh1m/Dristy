#ifndef BOOT_FLASH_H
#define BOOT_FLASH_H

#include <stddef.h>
#include <stdint.h>

typedef enum
{
    BOOT_FLASH_OK = 0,
    BOOT_FLASH_ERROR_INVALID_ARGUMENT,
    BOOT_FLASH_ERROR_NOT_INITIALIZED,
    BOOT_FLASH_ERROR_WRONG_CORE,
    BOOT_FLASH_ERROR_UNSUPPORTED_DEVICE,
    BOOT_FLASH_ERROR_OUT_OF_BOUNDS,
    BOOT_FLASH_ERROR_ALIGNMENT,
    BOOT_FLASH_ERROR_TIMEOUT,
    BOOT_FLASH_ERROR_WRITE_ENABLE,
    BOOT_FLASH_ERROR_FAULTED
} boot_flash_result_t;

typedef struct
{
    uint8_t jedec_id[3];
    uint32_t capacity;
    uint8_t initialized;
    uint8_t faulted;
} boot_flash_info_t;

/* The boot flash shares board SPI resources and is deliberately owned by core 0. */
boot_flash_result_t boot_flash_init(uint32_t hz);
boot_flash_result_t boot_flash_read_id(uint8_t id[3]);
boot_flash_result_t boot_flash_read(uint32_t addr, uint8_t *data, size_t len);
boot_flash_result_t boot_flash_sector_erase(uint32_t addr);

/* Programs at most one 256-byte page and rejects a page-boundary crossing. */
boot_flash_result_t boot_flash_page_program(uint32_t addr, const uint8_t *data,
                                            size_t len);

/* Splits an arbitrary in-range write at every flash page boundary. */
boot_flash_result_t boot_flash_program(uint32_t addr, const uint8_t *data,
                                      size_t len);

void boot_flash_get_info(boot_flash_info_t *info);
const char *boot_flash_result_name(boot_flash_result_t result);

#endif

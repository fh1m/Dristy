#ifndef HK_STORAGE_SD_CARD_H
#define HK_STORAGE_SD_CARD_H

#include <stdint.h>

/* Permanent private storage boundary for bounded SD block operations. */
uint8_t sd_card_init(void);
uint8_t sd_card_read_block(uint32_t lba, uint8_t *dst);
uint8_t sd_card_write_block(uint32_t lba, const uint8_t *src);
uint8_t sd_card_write_block_fast(uint32_t lba, const uint8_t *src);

#endif

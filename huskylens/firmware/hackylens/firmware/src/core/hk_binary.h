#ifndef HK_BINARY_H
#define HK_BINARY_H

#include <stddef.h>
#include <stdint.h>

uint8_t clamp_u8(uint8_t value, uint8_t min_value, uint8_t max_value);
uint16_t rd16(const uint8_t *p);
uint32_t rd32(const uint8_t *p);
uint32_t rd32be(const uint8_t *p);
void wr16(uint8_t *p, uint16_t value);
void wr32(uint8_t *p, uint32_t value);
uint32_t crc32_update(uint32_t crc, const uint8_t *data, size_t len);

#endif

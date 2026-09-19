#include "hk_binary.h"

uint8_t clamp_u8(uint8_t value, uint8_t min_value, uint8_t max_value)
{
    if(value < min_value)
        return min_value;
    if(value > max_value)
        return max_value;
    return value;
}

uint16_t rd16(const uint8_t *p)
{
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

uint32_t rd32(const uint8_t *p)
{
    return (uint32_t)p[0] |
           ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

uint32_t rd32be(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) |
           ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) |
           (uint32_t)p[3];
}

void wr16(uint8_t *p, uint16_t value)
{
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8);
}

void wr32(uint8_t *p, uint32_t value)
{
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8);
    p[2] = (uint8_t)(value >> 16);
    p[3] = (uint8_t)(value >> 24);
}

uint32_t crc32_update(uint32_t crc, const uint8_t *data, size_t len)
{
    crc = ~crc;
    while(len--)
    {
        crc ^= *data++;
        for(uint8_t i = 0; i < 8; i++)
            crc = (crc >> 1) ^ (0xEDB88320UL & (0UL - (crc & 1U)));
    }
    return ~crc;
}

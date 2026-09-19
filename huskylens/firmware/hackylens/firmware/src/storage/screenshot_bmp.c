#include "screenshot_bmp.h"

#include <string.h>

#include "../config/display_config.h"
#include "../config/screenshot_config.h"

static uint32_t screenshot_bmp_row_bytes(uint16_t width)
{
    return ((uint32_t)width * 3U + 3U) & ~3U;
}

static void screenshot_write_u16(uint8_t *dst, uint16_t value)
{
    dst[0] = (uint8_t)value;
    dst[1] = (uint8_t)(value >> 8);
}

static void screenshot_write_u32(uint8_t *dst, uint32_t value)
{
    dst[0] = (uint8_t)value;
    dst[1] = (uint8_t)(value >> 8);
    dst[2] = (uint8_t)(value >> 16);
    dst[3] = (uint8_t)(value >> 24);
}

static void screenshot_make_bmp_header(uint8_t header[SCREENSHOT_BMP_HEADER_SIZE])
{
    uint32_t data_size = screenshot_bmp_row_bytes(HK_DISPLAY_REQUIRED_WIDTH) * HK_DISPLAY_REQUIRED_HEIGHT;

    memset(header, 0, SCREENSHOT_BMP_HEADER_SIZE);
    header[0] = 'B';
    header[1] = 'M';
    screenshot_write_u32(&header[2], SCREENSHOT_BMP_HEADER_SIZE + data_size);
    screenshot_write_u32(&header[10], SCREENSHOT_BMP_HEADER_SIZE);
    screenshot_write_u32(&header[14], 40U);
    screenshot_write_u32(&header[18], HK_DISPLAY_REQUIRED_WIDTH);
    screenshot_write_u32(&header[22], HK_DISPLAY_REQUIRED_HEIGHT);
    screenshot_write_u16(&header[26], 1U);
    screenshot_write_u16(&header[28], 24U);
    screenshot_write_u32(&header[34], data_size);
}

uint32_t screenshot_bmp_file_size(void)
{
    return SCREENSHOT_BMP_HEADER_SIZE + screenshot_bmp_row_bytes(HK_DISPLAY_REQUIRED_WIDTH) * HK_DISPLAY_REQUIRED_HEIGHT;
}

void screenshot_bmp_fill_bytes(const screenshot_pixel_source_t *source, uint32_t offset, uint8_t *dst, uint16_t len)
{
    uint8_t header[SCREENSHOT_BMP_HEADER_SIZE];
    uint32_t row_bytes = screenshot_bmp_row_bytes(HK_DISPLAY_REQUIRED_WIDTH);
    uint32_t data_size = row_bytes * HK_DISPLAY_REQUIRED_HEIGHT;

    screenshot_make_bmp_header(header);

    for(uint16_t i = 0; i < len; i++)
    {
        uint32_t pos = offset + i;
        if(pos < SCREENSHOT_BMP_HEADER_SIZE)
        {
            dst[i] = header[pos];
            continue;
        }

        uint32_t data_pos = pos - SCREENSHOT_BMP_HEADER_SIZE;
        if(data_pos >= data_size)
        {
            dst[i] = 0;
            continue;
        }

        uint32_t row = data_pos / row_bytes;
        uint32_t col_byte = data_pos % row_bytes;
        uint32_t x = col_byte / 3U;
        uint32_t y = HK_DISPLAY_REQUIRED_HEIGHT - 1U - row;
        if(x >= HK_DISPLAY_REQUIRED_WIDTH)
        {
            dst[i] = 0;
            continue;
        }

        uint16_t color = source && source->pixel_at ? source->pixel_at(source->context, (uint16_t)x, (uint16_t)y) : 0;
        uint8_t component = (uint8_t)(col_byte % 3U);
        if(component == 0)
            dst[i] = (uint8_t)((color & 0x001FU) * 255U / 31U);
        else if(component == 1)
            dst[i] = (uint8_t)(((color >> 5) & 0x003FU) * 255U / 63U);
        else
            dst[i] = (uint8_t)(((color >> 11) & 0x001FU) * 255U / 31U);
    }
}

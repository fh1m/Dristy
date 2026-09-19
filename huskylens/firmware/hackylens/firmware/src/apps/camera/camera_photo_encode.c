#include "camera_photo_encode.h"

#include <stddef.h>

#include "../../core/photo_types.h"

#include "camera_config.h"

static const photo_source_t *g_photo_encode_source;

void photo_encode_begin(const photo_source_t *source)
{
    g_photo_encode_source = source;
}

void photo_encode_end(void)
{
    g_photo_encode_source = NULL;
}

static uint16_t photo_frame_pixel(uint32_t x, uint32_t y)
{
    if(!g_photo_encode_source || !g_photo_encode_source->read_pixel)
        return 0;
    return g_photo_encode_source->read_pixel(g_photo_encode_source->ctx, x, y);
}

void photo_encode_fill_file_bytes(photo_format_t format,
                                  uint32_t offset,
                                  uint8_t *dst,
                                  uint16_t len,
                                  uint16_t width,
                                  uint16_t height)
{
    uint8_t header24[PHOTO_BMP_HEADER_SIZE];
    uint8_t header565[PHOTO_BMP565_HEADER_SIZE];
    uint8_t ppm_header[PHOTO_PPM_HEADER_MAX];
    uint16_t ppm_header_size = 0;
    uint32_t bmp24_row_bytes = photo_bmp24_row_bytes(width);
    uint32_t ppm_row_bytes = photo_rgb888_row_bytes(width);
    uint32_t bmp565_row_bytes = photo_bmp565_row_bytes(width);
    uint32_t raw_size = (uint32_t)width * height * 2U;

    if(format == PHOTO_FORMAT_BMP24)
        photo_make_bmp_header(header24, width, height);
    else if(format == PHOTO_FORMAT_BMP565)
        photo_make_bmp565_header(header565, width, height);
    else if(format == PHOTO_FORMAT_PPM)
        ppm_header_size = photo_make_ppm_header(ppm_header, width, height);

    for(uint16_t i = 0; i < len; i++)
    {
        uint32_t pos = offset + i;
        uint32_t data_pos;
        uint16_t color;
        uint8_t component;

        if(format == PHOTO_FORMAT_BMP24)
        {
            if(pos < PHOTO_BMP_HEADER_SIZE)
            {
                dst[i] = header24[pos];
                continue;
            }

            data_pos = pos - PHOTO_BMP_HEADER_SIZE;
            if(data_pos >= ppm_row_bytes * height)
            {
                dst[i] = 0;
                continue;
            }

            uint32_t row = data_pos / ppm_row_bytes;
            uint32_t col_byte = data_pos % ppm_row_bytes;
            uint32_t x = col_byte / 3U;
            uint32_t y = height - 1U - row;
            if(x >= width)
            {
                dst[i] = 0;
                continue;
            }
            component = (uint8_t)(col_byte % 3U);
            color = photo_frame_pixel(x, y);

            if(component == 0)
                dst[i] = (uint8_t)((color & 0x001FU) * 255U / 31U);
            else if(component == 1)
                dst[i] = (uint8_t)(((color >> 5) & 0x003FU) * 255U / 63U);
            else
                dst[i] = (uint8_t)(((color >> 11) & 0x001FU) * 255U / 31U);
            continue;
        }

        if(format == PHOTO_FORMAT_BMP565)
        {
            if(pos < PHOTO_BMP565_HEADER_SIZE)
            {
                dst[i] = header565[pos];
                continue;
            }

            data_pos = pos - PHOTO_BMP565_HEADER_SIZE;
            if(data_pos >= bmp565_row_bytes * height)
            {
                dst[i] = 0;
                continue;
            }

            uint32_t row = data_pos / bmp565_row_bytes;
            uint32_t col_byte = data_pos % bmp565_row_bytes;
            uint32_t x = col_byte / 2U;
            uint32_t y = height - 1U - row;
            if(x >= width)
            {
                dst[i] = 0;
                continue;
            }
            color = photo_frame_pixel(x, y);
            dst[i] = (col_byte & 1U) ? (uint8_t)(color >> 8) : (uint8_t)color;
            continue;
        }

        if(format == PHOTO_FORMAT_PPM)
        {
            if(pos < ppm_header_size)
            {
                dst[i] = ppm_header[pos];
                continue;
            }

            data_pos = pos - ppm_header_size;
            if(data_pos >= bmp24_row_bytes * height)
            {
                dst[i] = 0;
                continue;
            }

            uint32_t row = data_pos / bmp24_row_bytes;
            uint32_t col_byte = data_pos % bmp24_row_bytes;
            uint32_t x = col_byte / 3U;
            if(x >= width)
            {
                dst[i] = 0;
                continue;
            }
            component = (uint8_t)(col_byte % 3U);
            color = photo_frame_pixel(x, row);
            if(component == 0)
                dst[i] = (uint8_t)(((color >> 11) & 0x001FU) * 255U / 31U);
            else if(component == 1)
                dst[i] = (uint8_t)(((color >> 5) & 0x003FU) * 255U / 63U);
            else
                dst[i] = (uint8_t)((color & 0x001FU) * 255U / 31U);
            continue;
        }

        data_pos = pos;
        if(data_pos >= raw_size)
        {
            dst[i] = 0;
            continue;
        }
        uint32_t pixel = data_pos / 2U;
        color = photo_frame_pixel(pixel % width, pixel / width);
        dst[i] = (data_pos & 1U) ? (uint8_t)(color >> 8) : (uint8_t)color;
    }
}

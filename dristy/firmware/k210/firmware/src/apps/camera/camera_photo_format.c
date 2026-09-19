#include "camera_photo_format.h"

#include <stdio.h>
#include <string.h>

#include "../../core/photo_types.h"

#include "camera_config.h"

#include "../../core/hk_binary.h"

const char *photo_format_label(photo_format_t format)
{
    if(format == PHOTO_FORMAT_RAW565)
        return "RAW565";
    if(format == PHOTO_FORMAT_BMP565)
        return "BMP565";
    if(format == PHOTO_FORMAT_PPM)
        return "PPM";
    return "BMP24";
}

uint32_t photo_bmp24_row_bytes(uint16_t width)
{
    return (((uint32_t)width * 3U) + 3U) & ~3UL;
}

uint32_t photo_rgb888_row_bytes(uint16_t width)
{
    return (uint32_t)width * 3U;
}

uint32_t photo_bmp565_row_bytes(uint16_t width)
{
    return (((uint32_t)width * 2U) + 3U) & ~3UL;
}

const char *photo_format_ext(photo_format_t format)
{
    if(format == PHOTO_FORMAT_RAW565)
        return "RAW";
    if(format == PHOTO_FORMAT_PPM)
        return "PPM";
    return "BMP";
}

uint16_t photo_make_ppm_header(uint8_t header[PHOTO_PPM_HEADER_MAX], uint16_t width, uint16_t height)
{
    int len = snprintf((char *)header, PHOTO_PPM_HEADER_MAX, "P6\n%u %u\n255\n", width, height);
    if(len < 0)
        return 0;
    if((uint32_t)len >= PHOTO_PPM_HEADER_MAX)
        return PHOTO_PPM_HEADER_MAX - 1U;
    return (uint16_t)len;
}

uint32_t photo_format_size(photo_format_t format, uint16_t width, uint16_t height)
{
    if(format == PHOTO_FORMAT_RAW565)
        return (uint32_t)width * height * 2U;
    if(format == PHOTO_FORMAT_BMP565)
        return PHOTO_BMP565_HEADER_SIZE + photo_bmp565_row_bytes(width) * height;
    if(format == PHOTO_FORMAT_PPM)
    {
        uint8_t header[PHOTO_PPM_HEADER_MAX];
        return photo_make_ppm_header(header, width, height) + photo_rgb888_row_bytes(width) * height;
    }
    return PHOTO_BMP_HEADER_SIZE + photo_bmp24_row_bytes(width) * height;
}

void photo_make_bmp_header(uint8_t header[PHOTO_BMP_HEADER_SIZE], uint16_t width, uint16_t height)
{
    uint32_t row_bytes = photo_bmp24_row_bytes(width);

    memset(header, 0, PHOTO_BMP_HEADER_SIZE);
    header[0] = 'B';
    header[1] = 'M';
    wr32(&header[2], PHOTO_BMP_HEADER_SIZE + row_bytes * height);
    wr32(&header[10], PHOTO_BMP_HEADER_SIZE);
    wr32(&header[14], 40);
    wr32(&header[18], width);
    wr32(&header[22], height);
    wr16(&header[26], 1);
    wr16(&header[28], PHOTO_BMP_BPP);
    wr32(&header[34], row_bytes * height);
}

void photo_make_bmp565_header(uint8_t header[PHOTO_BMP565_HEADER_SIZE], uint16_t width, uint16_t height)
{
    uint32_t row_bytes = photo_bmp565_row_bytes(width);

    memset(header, 0, PHOTO_BMP565_HEADER_SIZE);
    header[0] = 'B';
    header[1] = 'M';
    wr32(&header[2], PHOTO_BMP565_HEADER_SIZE + row_bytes * height);
    wr32(&header[10], PHOTO_BMP565_HEADER_SIZE);
    wr32(&header[14], 40);
    wr32(&header[18], width);
    wr32(&header[22], height);
    wr16(&header[26], 1);
    wr16(&header[28], 16);
    wr32(&header[30], 3);
    wr32(&header[34], row_bytes * height);
    wr32(&header[54], 0x0000F800UL);
    wr32(&header[58], 0x000007E0UL);
    wr32(&header[62], 0x0000001FUL);
}

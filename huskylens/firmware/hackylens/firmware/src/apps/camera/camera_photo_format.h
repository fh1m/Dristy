#ifndef PHOTO_FORMAT_H
#define PHOTO_FORMAT_H

#include <stdint.h>

#include "../../core/photo_types.h"

#include "camera_config.h"


const char *photo_format_label(photo_format_t format);
const char *photo_format_ext(photo_format_t format);
uint32_t photo_bmp24_row_bytes(uint16_t width);
uint32_t photo_bmp565_row_bytes(uint16_t width);
uint32_t photo_rgb888_row_bytes(uint16_t width);
uint32_t photo_format_size(photo_format_t format, uint16_t width, uint16_t height);
void photo_make_bmp_header(uint8_t header[PHOTO_BMP_HEADER_SIZE], uint16_t width, uint16_t height);
void photo_make_bmp565_header(uint8_t header[PHOTO_BMP565_HEADER_SIZE], uint16_t width, uint16_t height);
uint16_t photo_make_ppm_header(uint8_t header[PHOTO_PPM_HEADER_MAX], uint16_t width, uint16_t height);

#endif

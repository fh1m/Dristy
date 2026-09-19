#ifndef IMAGE_DECODE_PNG_INFLATE_H
#define IMAGE_DECODE_PNG_INFLATE_H

#include <stdint.h>

uint8_t png_zlib_inflate_to_buffer(const uint8_t *src, uint32_t src_size, uint8_t *out, uint32_t out_size);

#endif

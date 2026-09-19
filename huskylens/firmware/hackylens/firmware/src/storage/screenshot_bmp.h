#ifndef SCREENSHOT_BMP_H
#define SCREENSHOT_BMP_H

#include <stdint.h>

#include "../core/pixel_source.h"

uint32_t screenshot_bmp_file_size(void);
void screenshot_bmp_fill_bytes(const screenshot_pixel_source_t *source, uint32_t offset, uint8_t *dst, uint16_t len);

#endif

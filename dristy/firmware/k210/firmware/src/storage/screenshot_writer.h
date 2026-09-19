#ifndef SCREENSHOT_WRITER_H
#define SCREENSHOT_WRITER_H

#include <stddef.h>
#include <stdint.h>

#include "screenshot_bmp.h"

uint8_t screenshot_save_current_screen(const screenshot_pixel_source_t *source, char *saved_name, size_t saved_name_size);

#endif

#ifndef PIXEL_SOURCE_H
#define PIXEL_SOURCE_H

#include <stdint.h>

typedef uint16_t (*screenshot_pixel_reader_t)(void *context, uint16_t x, uint16_t y);

typedef struct
{
    screenshot_pixel_reader_t pixel_at;
    void *context;
} screenshot_pixel_source_t;

#endif

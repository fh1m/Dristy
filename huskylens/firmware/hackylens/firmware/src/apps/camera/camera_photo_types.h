#ifndef PHOTO_SOURCE_H
#define PHOTO_SOURCE_H

#include "../../core/photo_types.h"

#include <stdint.h>
#include "camera_photo_format.h"

typedef uint16_t (*photo_pixel_reader_t)(void *ctx, uint32_t x, uint32_t y);

typedef struct
{
    photo_format_t format;
    uint16_t width;
    uint16_t height;
    photo_pixel_reader_t read_pixel;
    void *ctx;
} photo_source_t;

#endif

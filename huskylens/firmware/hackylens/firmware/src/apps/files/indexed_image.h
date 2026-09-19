#ifndef HK_INDEXED_IMAGE_H
#define HK_INDEXED_IMAGE_H

#include <stdint.h>

typedef struct
{
    uint16_t canvas_w;
    uint16_t canvas_h;
    uint16_t frame_x;
    uint16_t frame_y;
    uint16_t frame_w;
    uint16_t frame_h;
    uint16_t background_rgb565;
    uint32_t delay_ms;
    int16_t transparent_index;
    uint8_t disposal;
} hk_indexed_frame_t;

#endif

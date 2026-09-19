#ifndef CAMERA_VIEW_H
#define CAMERA_VIEW_H

#include <stdint.h>

typedef struct
{
    volatile uint16_t *pixels;
    uint16_t width;
    uint16_t height;
    uint8_t rotate;
    uint8_t fps_overlay;
    uint8_t light_overlay;
    char fps_text[16];
    char light_text[12];
} camera_view_frame_t;

typedef struct
{
    int16_t x;
    int16_t y;
    int16_t w;
    int16_t h;
} camera_view_rect_t;

typedef struct
{
    uint32_t lease_id;
    uint8_t *rgb565_be;
    uint16_t width;
    uint16_t height;
    uint16_t stride_bytes;
} camera_view_present_t;

uint8_t camera_view_compose_frame(const camera_view_frame_t *frame, camera_view_present_t *present);
void camera_view_compose_rects(camera_view_present_t *present,
                               const camera_view_frame_t *frame,
                               const camera_view_rect_t *rects,
                               uint8_t count,
                               uint16_t color);
void camera_view_compose_text_at(camera_view_present_t *present,
                                 uint16_t x,
                                 uint16_t y,
                                 const char *text,
                                 uint16_t fg,
                                 uint16_t bg);
void camera_view_compose_crosshair(camera_view_present_t *present,
                                   uint16_t x,
                                   uint16_t y,
                                   uint16_t color);
uint8_t camera_view_present(camera_view_present_t *present);
void camera_view_discard(camera_view_present_t *present);
void camera_view_draw_rects(const camera_view_frame_t *frame, const camera_view_rect_t *rects, uint8_t count, uint16_t color);
void camera_view_clear(void);

#endif

#ifndef CAMERA_FRAME_H
#define CAMERA_FRAME_H

#include <stdint.h>

typedef struct
{
    volatile uint16_t *pixels;
    uint16_t width;
    uint16_t height;
    uint8_t rotate;
    uint32_t lease_id;
    uint32_t sequence;
} camera_preview_frame_t;

uint16_t camera_service_frame_pixel(uint32_t x, uint32_t y);
void camera_service_frame_info(uint16_t *width, uint16_t *height);
uint8_t camera_service_preview_acquire(camera_preview_frame_t *frame);
void camera_service_preview_release(camera_preview_frame_t *frame);
uint8_t camera_service_frame_snapshot(uint32_t wait_ms, const uint8_t **data, uint32_t *bytes, uint16_t *width, uint16_t *height);
void camera_service_frame_snapshot_done(uint8_t resume_preview);
uint8_t camera_service_have_frame(void);
uint8_t camera_service_capture_frame_tick(void);
uint8_t camera_service_consume_frame_timeout(void);
#endif

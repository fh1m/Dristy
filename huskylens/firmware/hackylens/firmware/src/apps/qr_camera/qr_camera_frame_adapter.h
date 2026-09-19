#ifndef HK_QR_CAMERA_FRAME_ADAPTER_H
#define HK_QR_CAMERA_FRAME_ADAPTER_H

#include <stdint.h>

typedef struct
{
    const uint16_t *pixels;
    uint16_t width;
    uint16_t height;
    uint8_t using_snapshot;
} qr_camera_decode_frame_t;

uint8_t qr_camera_frame_acquire(uint8_t force, qr_camera_decode_frame_t *frame);
void qr_camera_frame_resume_if_needed(uint8_t using_snapshot);
void qr_camera_frame_result_close(uint8_t ok_is_down);

#endif

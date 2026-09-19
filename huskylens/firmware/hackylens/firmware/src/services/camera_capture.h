#ifndef CAMERA_CAPTURE_H
#define CAMERA_CAPTURE_H

#include <stdint.h>

typedef struct
{
    uint8_t have_frame;
    uint8_t capture_state;
    uint16_t frame_wait_ms;
    uint8_t frame_timeout_shown;
    uint32_t frame_start_count;
    uint32_t convert_count;
    uint32_t frame_finish_count;
    uint32_t captured_count;
    uint32_t timeout_count;
    uint32_t ready_drop_count;
    uint32_t busy_drop_count;
    uint32_t last_sts;
} camera_capture_status_t;

typedef struct
{
    const volatile uint16_t *pixels;
    uint32_t lease_id;
    uint32_t sequence;
} camera_capture_frame_t;

uint8_t camera_capture_start(uint16_t width, uint16_t height, uint8_t burst);
void camera_capture_reset_session(void);
void camera_capture_reset_counters(void);
void camera_capture_reset_flow(void);
void camera_capture_clear_timeout(void);
void camera_capture_status(camera_capture_status_t *status);
uint8_t camera_capture_tick(const char *log_prefix);
uint8_t camera_capture_have_frame(void);
uint8_t camera_capture_consume_timeout(void);
uint8_t camera_capture_acquire(camera_capture_frame_t *frame);
void camera_capture_release(uint32_t lease_id);
uint8_t camera_capture_snapshot(uint16_t width, uint16_t height, uint32_t wait_ms, const char *log_prefix);
void camera_capture_pause(void);
void camera_capture_snapshot_done(uint8_t resume_preview);
void camera_capture_resume_preview(void);
const uint16_t *camera_capture_snapshot_pixels(void);

#endif

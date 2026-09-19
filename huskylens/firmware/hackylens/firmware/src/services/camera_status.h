#ifndef CAMERA_STATUS_H
#define CAMERA_STATUS_H

#include <stddef.h>
#include <stdint.h>

typedef struct
{
    uint8_t initialized;
    uint8_t ready;
    uint8_t have_frame;
    uint8_t colorbar_enabled;
    uint8_t capture_state;
    uint16_t frame_wait_ms;
    uint8_t frame_timeout_shown;
    uint16_t width;
    uint16_t height;
    uint16_t mid;
    uint16_t pid;
    uint32_t last_sts;
    uint32_t frame_start_count;
    uint32_t convert_count;
    uint32_t frame_finish_count;
    uint32_t captured_count;
    uint32_t timeout_count;
    uint32_t ready_drop_count;
    uint32_t busy_drop_count;
    const char *fail_reason;
} camera_service_status_t;

const char *camera_log_prefix(void);
void camera_service_status(camera_service_status_t *status);
void camera_service_note_probe_result(uint16_t mid, uint16_t pid);
uint8_t camera_service_fps_overlay_enabled(void);
void camera_service_format_fps_overlay(char *line, size_t line_size);
void camera_service_format_fps(char *line, size_t line_size);
void camera_service_fps_reset(void);
void camera_service_fps_note_present(uint32_t compose_us, uint32_t present_us);
void camera_service_fps_on_frame(void);
uint8_t camera_service_toggle_colorbar(void);

#endif

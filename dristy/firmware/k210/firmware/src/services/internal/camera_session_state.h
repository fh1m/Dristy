#ifndef CAMERA_SESSION_STATE_H
#define CAMERA_SESSION_STATE_H

#include <stdint.h>

uint8_t camera_session_ready(void);
uint8_t camera_session_qr_mode(void);
uint8_t camera_session_qvga_mode(void);
uint16_t camera_session_width(void);
uint16_t camera_session_height(void);
uint8_t camera_session_preview_rotate(void);
void camera_session_set_frozen(uint8_t frozen);
uint8_t camera_session_snapshot_capture(uint32_t wait_ms);
void camera_session_snapshot_resume(uint8_t resume_preview);

uint8_t camera_session_initialized(void);
uint8_t camera_session_ever_initialized(void);
uint8_t camera_session_colorbar_enabled(void);
void camera_session_set_qr_mode(uint8_t qr_mode);
void camera_session_set_qvga_mode(uint8_t enabled);
void camera_session_set_ready(uint8_t ready);
void camera_session_set_preview_rotate(uint8_t rotate);
void camera_session_set_fail_reason(const char *reason);
void camera_session_mark_started(void);
void camera_session_mark_stopped(void);
void camera_session_sensor_id(uint16_t *mid, uint16_t *pid);

#endif

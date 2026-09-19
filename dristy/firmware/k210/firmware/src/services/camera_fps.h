#ifndef CAMERA_FPS_H
#define CAMERA_FPS_H

#include <stdint.h>

uint8_t camera_fps_enabled_state(void);
void camera_fps_apply_persisted(uint8_t enabled);
uint32_t camera_fps_current_x10(void);
uint32_t camera_fps_average_x10(void);
void camera_fps_mark_stopped(void);
void camera_fps_log_summary(const char *prefix, uint16_t width, uint16_t height);

#endif

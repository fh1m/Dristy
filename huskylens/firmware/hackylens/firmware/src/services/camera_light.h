#ifndef CAMERA_LIGHT_H
#define CAMERA_LIGHT_H


#include <stddef.h>
#include <stdint.h>

#include "../core/camera_types.h"

typedef enum
{
    CAMERA_RGB_RED = 0,
    CAMERA_RGB_GREEN,
    CAMERA_RGB_BLUE,
} camera_rgb_channel_t;

uint8_t camera_service_light_overlay_enabled(void);
void camera_service_format_light_overlay(char *line, size_t line_size);
camera_light_mode_t camera_service_light_mode(void);
uint8_t camera_service_rgb_channel(camera_rgb_channel_t channel);
uint8_t camera_service_light_level(void);
void camera_service_set_light_level(uint8_t level);
uint8_t camera_service_light_active(void);
void camera_service_set_light_active(uint8_t active);
camera_light_mode_t camera_service_toggle_light_mode(void);
uint8_t camera_service_cycle_rgb_channel(camera_rgb_channel_t channel);
const char *camera_light_mode_label(camera_light_mode_t mode);
void camera_light_outputs_off(void);
void camera_light_apply(void);
void camera_light_restore_global(void);
void camera_light_adjust(int8_t delta);

#endif

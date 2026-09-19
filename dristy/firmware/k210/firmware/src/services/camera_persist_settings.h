#ifndef HK_CAMERA_PERSIST_SETTINGS_H
#define HK_CAMERA_PERSIST_SETTINGS_H

#include <stdint.h>

#include "../core/camera_types.h"
#include "../core/photo_types.h"

typedef struct
{
    uint8_t review_after_shot;
    uint8_t fps_enabled;
    camera_light_mode_t light_mode;
    uint8_t rgb_red;
    uint8_t rgb_green;
    uint8_t rgb_blue;
    photo_format_t photo_format;
    camera_size_t size;
} camera_persist_settings_t;

uint8_t camera_service_review_after_shot(void);
uint8_t camera_service_fps_enabled(void);
photo_format_t camera_service_photo_format(void);
camera_size_t camera_service_size(void);
void camera_service_persist_defaults(void);
void camera_service_persist_get(camera_persist_settings_t *settings);
void camera_service_persist_apply(const camera_persist_settings_t *settings);
void camera_service_set_fps_enabled(uint8_t enabled);
uint8_t camera_service_toggle_review_after_shot(void);
uint8_t camera_service_toggle_fps(void);
photo_format_t camera_service_cycle_photo_format(void);
camera_size_t camera_service_cycle_size(void);

#endif

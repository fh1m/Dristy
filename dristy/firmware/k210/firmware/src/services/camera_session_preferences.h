#ifndef HK_CAMERA_SESSION_PREFERENCES_H
#define HK_CAMERA_SESSION_PREFERENCES_H

#include <stdint.h>

#include "../core/camera_types.h"

typedef struct
{
    uint8_t fps_enabled;
    camera_light_mode_t light_mode;
    uint8_t rgb_red;
    uint8_t rgb_green;
    uint8_t rgb_blue;
} camera_session_preferences_t;

void camera_session_preferences_override(const camera_session_preferences_t *preferences);
void camera_session_preferences_clear(void);
uint8_t camera_session_preferences_fps_enabled(void);
camera_light_mode_t camera_session_preferences_light_mode(void);
uint8_t camera_session_preferences_rgb_red(void);
uint8_t camera_session_preferences_rgb_green(void);
uint8_t camera_session_preferences_rgb_blue(void);

#endif

#ifndef HK_OBJECT_DETECT_SETTINGS_H
#define HK_OBJECT_DETECT_SETTINGS_H

#include <stdint.h>

#include "object_detect_types.h"

void object_detect_settings_load(void);
void object_detect_settings_apply_session(void);
const object_detect_preferences_t *object_detect_settings_preferences(void);
uint8_t object_detect_settings_set_confidence(uint8_t value);
uint8_t object_detect_settings_set_nms(uint8_t value);
uint8_t object_detect_settings_set_fps(uint8_t enabled);
uint8_t object_detect_settings_set_light(uint8_t mode);
uint8_t object_detect_settings_set_rgb(uint8_t channel, uint8_t value);

#endif

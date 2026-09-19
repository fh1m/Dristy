#ifndef HK_APRILTAG_SETTINGS_H
#define HK_APRILTAG_SETTINGS_H

#include <stdint.h>

#include "apriltag_types.h"

void apriltag_settings_load(void);
void apriltag_settings_apply_session(void);
const apriltag_preferences_t *apriltag_settings_preferences(void);
uint8_t apriltag_settings_selected(uint16_t id);
uint8_t apriltag_settings_toggle_selected(uint16_t id);
uint8_t apriltag_settings_clear_selected(void);
uint16_t apriltag_settings_selected_count(void);
uint8_t apriltag_settings_set_refine(uint8_t enabled);
uint8_t apriltag_settings_set_output(apriltag_output_mode_t mode);
uint8_t apriltag_settings_set_fps(uint8_t enabled);
uint8_t apriltag_settings_set_light(uint8_t mode);
uint8_t apriltag_settings_set_rgb(uint8_t channel, uint8_t value);
const char *apriltag_settings_output_label(void);

#endif

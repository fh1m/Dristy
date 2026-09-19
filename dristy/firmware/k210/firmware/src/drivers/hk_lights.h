#ifndef HK_LIGHTS_H
#define HK_LIGHTS_H

#include <stdint.h>

void lights_driver_prepare(void);
void lights_screen_backlight_set(uint8_t percent);
void lights_screen_backlight_off(void);
void lights_illum_set(uint8_t enabled, uint8_t brightness);
void lights_rgb_set(uint8_t enabled, uint8_t red, uint8_t green, uint8_t blue);

#endif

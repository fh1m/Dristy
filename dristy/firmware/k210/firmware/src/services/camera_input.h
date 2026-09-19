#ifndef HK_CAMERA_INPUT_H
#define HK_CAMERA_INPUT_H

#include <stdint.h>

void camera_input_reset(uint8_t ok_is_down);
void camera_input_cancel(void);
void camera_input_press(uint64_t now_us);
void camera_input_ignore_until_release(uint8_t ok_is_down);
uint8_t camera_input_ignore_update(uint8_t ok_is_down);
uint8_t camera_input_hold_triggered(uint64_t now_us);
uint8_t camera_input_release_was_short(void);
void camera_input_return_from_settings(uint8_t ok_is_down);

#endif

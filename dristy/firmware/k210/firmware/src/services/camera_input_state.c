#include "camera_input.h"

#include "../config/camera_layout.h"


static uint64_t g_camera_ok_press_us;
static uint8_t g_camera_ok_hold_fired;
static uint8_t g_camera_ignore_ok_until_release;

void camera_input_reset(uint8_t ok_is_down)
{
    g_camera_ok_press_us = 0;
    g_camera_ok_hold_fired = 0;
    g_camera_ignore_ok_until_release = ok_is_down ? 1 : 0;
}

void camera_input_cancel(void)
{
    g_camera_ok_press_us = 0;
    g_camera_ok_hold_fired = 0;
    g_camera_ignore_ok_until_release = 0;
}

void camera_input_press(uint64_t now_us)
{
    g_camera_ok_press_us = now_us;
    g_camera_ok_hold_fired = 0;
}

void camera_input_ignore_until_release(uint8_t ok_is_down)
{
    g_camera_ok_press_us = 0;
    g_camera_ok_hold_fired = 0;
    g_camera_ignore_ok_until_release = ok_is_down ? 1 : 0;
}

uint8_t camera_input_ignore_update(uint8_t ok_is_down)
{
    if(!g_camera_ignore_ok_until_release)
        return 0;

    if(!ok_is_down)
        camera_input_cancel();

    return 1;
}

uint8_t camera_input_hold_triggered(uint64_t now_us)
{
    if(g_camera_ok_press_us == 0 || g_camera_ok_hold_fired)
        return 0;
    if(now_us - g_camera_ok_press_us < CAMERA_OK_HOLD_US)
        return 0;

    g_camera_ok_hold_fired = 1;
    return 1;
}

uint8_t camera_input_release_was_short(void)
{
    uint8_t was_short = g_camera_ok_hold_fired ? 0 : 1;

    g_camera_ok_press_us = 0;
    g_camera_ok_hold_fired = 0;
    return was_short;
}

void camera_input_return_from_settings(uint8_t ok_is_down)
{
    g_camera_ok_press_us = 0;
    g_camera_ok_hold_fired = 1;
    g_camera_ignore_ok_until_release = ok_is_down ? 1 : 0;
}

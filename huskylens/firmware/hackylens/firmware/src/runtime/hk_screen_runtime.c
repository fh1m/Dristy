#include "../core/hk_screen.h"

#include "../core/hk_app.h"
#include "hal_time.h"

static screen_t s_screen = SCREEN_MENU;
static uint64_t s_last_activity_us;
static hk_screen_wake_handler_t s_wake_handler;

screen_t hk_screen_get(void)
{
    return s_screen;
}

void hk_screen_set(screen_t screen)
{
    s_screen = screen;
}

void hk_screen_set_wake_handler(hk_screen_wake_handler_t handler)
{
    s_wake_handler = handler;
}

void hk_screen_request_wake(void)
{
    if(s_wake_handler)
        s_wake_handler();
}

const char *screen_label(screen_t screen)
{
    if(screen == SCREEN_CAMERA)
        return "CAMERA";
    if(screen == SCREEN_QR_CAMERA)
        return "QR-CAMERA";
    if(screen == SCREEN_FACE_DETECT)
        return "FACE-DETECT";
    if(screen == SCREEN_APRILTAG)
        return "APRILTAG";
    if(screen == SCREEN_OBJECT_DETECT)
        return "OBJECT-DETECT";
    if(screen == SCREEN_VISION_MODE)
        return "VISION-MODE";
    if(screen == SCREEN_CAMERA_SETTINGS)
        return "CAM-SETTINGS";
    if(screen == SCREEN_FILES)
        return "FILES";
    if(screen == SCREEN_BUTTONS)
        return "BUTTONS";
    if(screen == SCREEN_SETTINGS)
        return "SETTINGS";
    if(screen == SCREEN_SLEEP)
        return "SLEEP";
    return "MENU";
}

uint64_t hk_last_activity_us(void)
{
    return s_last_activity_us;
}

void activity_note(void)
{
    s_last_activity_us = hal_time_us();
}

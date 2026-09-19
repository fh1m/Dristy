#ifndef HK_APP_H
#define HK_APP_H

#include <stdint.h>

#include "hk_events.h"

typedef enum
{
    SCREEN_MENU = 0,
    SCREEN_CAMERA,
    SCREEN_QR_CAMERA,
    SCREEN_FACE_DETECT,
    SCREEN_APRILTAG,
    SCREEN_CAMERA_SETTINGS,
    SCREEN_FILES,
    SCREEN_BUTTONS,
    SCREEN_APP_SLOT_0,
    SCREEN_APP_SLOT_1,
    SCREEN_APP_SLOT_2,
    SCREEN_APP_SLOT_3,
    SCREEN_SETTINGS,
    SCREEN_SLEEP,
    SCREEN_OBJECT_DETECT,
    SCREEN_VISION_MODE,
} screen_t;

typedef enum
{
    HK_AUTOSTART_OFF = 0,
    HK_AUTOSTART_TERMINAL,
    HK_AUTOSTART_CAMERA,
    HK_AUTOSTART_QR_CAMERA,
    HK_AUTOSTART_FACE_DETECT,
    HK_AUTOSTART_APRILTAG,
    HK_AUTOSTART_FILES,
    HK_AUTOSTART_BUTTONS,
    HK_AUTOSTART_PONG,
    HK_AUTOSTART_OBJECT_DETECT,
    HK_AUTOSTART_MICROPYTHON,
    HK_AUTOSTART_COUNT,
} hk_autostart_id_t;

typedef struct
{
    uint32_t state;
    uint32_t pressed;
    uint32_t changed;
} hk_input_snapshot_t;

typedef struct hk_app
{
    const char *id;
    const char *title;
    screen_t screen;
    hk_autostart_id_t autostart_id;
    void (*enter)(const hk_input_snapshot_t *input);
    void (*exit)(void);
    void (*tick)(const hk_input_snapshot_t *input);
    void (*handle_input)(const hk_input_snapshot_t *input);
    uint8_t (*owns_screen)(screen_t screen);
    void (*draw_icon)(uint16_t x, uint16_t y, uint16_t color, uint16_t bg);
    void (*background_tick)(const hk_input_snapshot_t *input);
    void (*handle_sd_event)(hk_sd_event_t event);
    uint8_t blocks_sd_poll;
    uint8_t tick_interval_ms;
    uint8_t (*handle_debug_command)(const char *cmd);
    const char *debug_help;
} hk_app_t;

#endif

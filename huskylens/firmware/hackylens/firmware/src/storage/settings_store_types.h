#ifndef SETTINGS_STORE_TYPES_H
#define SETTINGS_STORE_TYPES_H

#include <stdint.h>

#include "../config/settings_config.h"

typedef struct
{
    uint8_t led_enabled;
    uint8_t led_brightness;
    uint8_t rgb_enabled;
    uint8_t rgb_brightness;
    uint8_t screen_brightness;
    uint8_t camera_review_after_shot;
    uint8_t auto_sleep_minutes;
    uint8_t camera_format_rgb_red;
    uint8_t camera_size_rgb_green;
    uint8_t camera_schema_mark;
    uint8_t rgb_red_light_mode;
    uint8_t rgb_green;
    uint8_t rgb_blue;
    uint8_t rgb_schema_mark;
    uint8_t fps_rgb_blue;
    uint8_t qr_rate_fps_mark;
    uint8_t app_data[SETTINGS_APP_DATA_SIZE];
    uint8_t autostart_id;
} settings_payload_t;

typedef struct
{
    uint32_t magic;
    uint16_t version;
    uint16_t payload_size;
    uint32_t sequence;
    settings_payload_t payload;
    uint32_t crc32;
} settings_record_t;

_Static_assert(sizeof(settings_payload_t) == 105U, "settings v4 payload layout changed");
_Static_assert(sizeof(settings_record_t) == 124U, "settings v4 record layout changed");

#endif

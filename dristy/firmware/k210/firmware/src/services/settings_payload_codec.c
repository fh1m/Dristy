#include "settings_payload_codec.h"

#include <string.h>

#include "../core/photo_types.h"
#include "../core/hk_app.h"

#include "../config/camera_config.h"
#include "../config/settings_config.h"

#include "../core/hk_binary.h"
#include "../core/hk_camera_sizes.h"
#include "hk_config.h"
#include "external_link_types.h"

settings_payload_t settings_payload_encode(const settings_snapshot_t *snapshot)
{
    settings_payload_t payload;
    memset(&payload, 0, sizeof(payload));
    if(!snapshot)
        return payload;

    payload.led_enabled = snapshot->led_enabled ? 1 : 0;
    payload.led_brightness = clamp_u8(snapshot->led_brightness, 0, 100);
    payload.rgb_enabled = snapshot->rgb_enabled ? 1 : 0;
    payload.rgb_brightness = clamp_u8((uint8_t)(((uint16_t)snapshot->rgb_red + snapshot->rgb_green + snapshot->rgb_blue) / 3U), 0, 100);
    payload.screen_brightness = clamp_u8(snapshot->screen_brightness, 10, 100);
#if HK_ENABLE_CAMERA_FEATURE
    payload.camera_review_after_shot = snapshot->camera.review_after_shot ? 1 : 0;
#endif
    payload.auto_sleep_minutes = (uint8_t)(clamp_u8(snapshot->auto_sleep_minutes, 1, 30) |
                                           ((snapshot->feature_flags & SETTINGS_FEATURE_FLAGS_MASK) <<
                                            SETTINGS_PAYLOAD_FEATURE_SHIFT));
#if HK_ENABLE_CAMERA_FEATURE
    payload.camera_format_rgb_red = (uint8_t)((snapshot->camera.photo_format < PHOTO_FORMAT_COUNT ? (uint8_t)snapshot->camera.photo_format : PHOTO_FORMAT_RAW565) |
                                             ((clamp_u8(snapshot->camera.rgb_red, 0, 100) / 10U) << 4));
    payload.camera_size_rgb_green = (uint8_t)((camera_size_is_safe(snapshot->camera.size) ? (uint8_t)snapshot->camera.size : CAMERA_SIZE_320X240) |
                                             ((clamp_u8(snapshot->camera.rgb_green, 0, 100) / 10U) << 4));
    payload.camera_schema_mark = SETTINGS_CAMERA_MARK;
    payload.rgb_red_light_mode = (uint8_t)(clamp_u8(snapshot->rgb_red, 0, 100) |
                                           (snapshot->camera.light_mode == CAMERA_LIGHT_RGB ? 0x80U : 0U));
#else
    payload.rgb_red_light_mode = clamp_u8(snapshot->rgb_red, 0, 100);
#endif
    payload.rgb_green = clamp_u8(snapshot->rgb_green, 0, 100);
    payload.rgb_blue = clamp_u8(snapshot->rgb_blue, 0, 100);
    payload.rgb_schema_mark = SETTINGS_RGB_MARK;
    payload.fps_rgb_blue = (uint8_t)(SETTINGS_UART_SPEED_MARK |
                                    (((uint8_t)snapshot->external_link_uart_speed << SETTINGS_UART_SPEED_SHIFT) &
                                     SETTINGS_UART_SPEED_MASK));
#if HK_ENABLE_CAMERA_FEATURE
    payload.fps_rgb_blue |= (uint8_t)((snapshot->camera.fps_enabled ? 1U : 0U) |
                                     ((clamp_u8(snapshot->camera.rgb_blue, 0, 100) / 10U) << 4));
#endif
    payload.qr_rate_fps_mark = (uint8_t)((clamp_u8(snapshot->qr_decode_rate,
                                                  SETTINGS_QR_DECODE_RATE_MIN,
                                                  SETTINGS_QR_DECODE_RATE_MAX) << 4) |
                                         SETTINGS_FPS_MARK_LOW);
    memcpy(payload.app_data, snapshot->app_data, sizeof(payload.app_data));
    payload.autostart_id = snapshot->autostart_id < HK_AUTOSTART_COUNT ?
                           snapshot->autostart_id : HK_AUTOSTART_OFF;
    return payload;
}

void settings_payload_decode(const settings_payload_t *payload, settings_snapshot_t *snapshot)
{
#if HK_ENABLE_CAMERA_FEATURE
    camera_persist_settings_t camera = {
        .review_after_shot = 0,
        .fps_enabled = 1,
        .light_mode = CAMERA_LIGHT_LED,
        .rgb_red = 100,
        .rgb_green = 100,
        .rgb_blue = 100,
        .photo_format = PHOTO_FORMAT_RAW565,
        .size = CAMERA_SIZE_320X240,
    };
#endif
    uint8_t rgb_brightness;

    if(!payload || !snapshot)
        return;

    memset(snapshot, 0, sizeof(*snapshot));
    memcpy(snapshot->app_data, payload->app_data, sizeof(snapshot->app_data));
    snapshot->autostart_id = payload->autostart_id < HK_AUTOSTART_COUNT ?
                             payload->autostart_id : HK_AUTOSTART_OFF;

    snapshot->led_enabled = payload->led_enabled ? 1 : 0;
    snapshot->led_brightness = clamp_u8(payload->led_brightness, 0, 100);
    snapshot->rgb_enabled = payload->rgb_enabled ? 1 : 0;
    if(payload->rgb_schema_mark == SETTINGS_RGB_MARK)
    {
        snapshot->rgb_red = clamp_u8((uint8_t)(payload->rgb_red_light_mode & 0x7FU), 0, 100);
        snapshot->rgb_green = clamp_u8(payload->rgb_green, 0, 100);
        snapshot->rgb_blue = clamp_u8(payload->rgb_blue, 0, 100);
    }
    else
    {
        rgb_brightness = clamp_u8(payload->rgb_brightness, 0, 100);
        snapshot->rgb_red = rgb_brightness;
        snapshot->rgb_green = rgb_brightness;
        snapshot->rgb_blue = rgb_brightness;
    }
    snapshot->screen_brightness = clamp_u8(payload->screen_brightness, 10, 100);
#if HK_ENABLE_CAMERA_FEATURE
    camera.review_after_shot = payload->camera_review_after_shot ? 1 : 0;
#endif
    snapshot->auto_sleep_minutes = (payload->auto_sleep_minutes & SETTINGS_PAYLOAD_AUTO_SLEEP_MASK) >= 1 &&
                                   (payload->auto_sleep_minutes & SETTINGS_PAYLOAD_AUTO_SLEEP_MASK) <= 30 ?
                                   (uint8_t)(payload->auto_sleep_minutes & SETTINGS_PAYLOAD_AUTO_SLEEP_MASK) : 1;
    snapshot->feature_flags = (uint8_t)((payload->auto_sleep_minutes >> SETTINGS_PAYLOAD_FEATURE_SHIFT) &
                                        SETTINGS_FEATURE_FLAGS_MASK);
    if(payload->fps_rgb_blue & SETTINGS_UART_SPEED_MARK)
    {
        uint8_t speed = (uint8_t)((payload->fps_rgb_blue & SETTINGS_UART_SPEED_MASK) >>
                                  SETTINGS_UART_SPEED_SHIFT);
        snapshot->external_link_uart_speed = speed < EXTERNAL_LINK_UART_SPEED_COUNT ?
                                             speed : EXTERNAL_LINK_UART_SPEED_115200;
    }
    else
        snapshot->external_link_uart_speed = EXTERNAL_LINK_UART_SPEED_115200;
#if HK_ENABLE_CAMERA_FEATURE
    if(payload->camera_schema_mark == SETTINGS_CAMERA_MARK || payload->camera_schema_mark == SETTINGS_CAMERA_MARK_V1)
    {
        uint8_t format = payload->camera_format_rgb_red & 0x0FU;
        uint8_t size = payload->camera_size_rgb_green & 0x0FU;
        camera.photo_format = format < PHOTO_FORMAT_COUNT ? (photo_format_t)format : PHOTO_FORMAT_RAW565;
        camera.size = size < CAMERA_SIZE_COUNT ? (camera_size_t)size : CAMERA_SIZE_320X240;
        if(!camera_size_is_safe(camera.size))
            camera.size = CAMERA_SIZE_320X240;
        if(payload->camera_schema_mark == SETTINGS_CAMERA_MARK)
        {
            camera.light_mode = (payload->rgb_red_light_mode & 0x80U) ? CAMERA_LIGHT_RGB : CAMERA_LIGHT_LED;
            camera.rgb_red = clamp_u8((uint8_t)(((payload->camera_format_rgb_red >> 4) & 0x0FU) * 10U), 0, 100);
            camera.rgb_green = clamp_u8((uint8_t)(((payload->camera_size_rgb_green >> 4) & 0x0FU) * 10U), 0, 100);
            camera.rgb_blue = clamp_u8((uint8_t)(((payload->fps_rgb_blue >> 4) & 0x0FU) * 10U), 0, 100);
        }
        else
        {
            camera.light_mode = CAMERA_LIGHT_LED;
            camera.rgb_red = 100;
            camera.rgb_green = 100;
            camera.rgb_blue = 100;
        }
    }
    else
    {
        camera.photo_format = PHOTO_FORMAT_RAW565;
        camera.size = CAMERA_SIZE_320X240;
        camera.light_mode = CAMERA_LIGHT_LED;
        camera.rgb_red = 100;
        camera.rgb_green = 100;
        camera.rgb_blue = 100;
    }
    if((payload->qr_rate_fps_mark & 0x0FU) == SETTINGS_FPS_MARK_LOW)
    {
        camera.fps_enabled = (payload->fps_rgb_blue & 0x01U) ? 1 : 0;
        snapshot->qr_decode_rate =
            (payload->qr_rate_fps_mark >> 4) >= SETTINGS_QR_DECODE_RATE_MIN &&
            (payload->qr_rate_fps_mark >> 4) <= SETTINGS_QR_DECODE_RATE_MAX ?
            (uint8_t)(payload->qr_rate_fps_mark >> 4) : SETTINGS_QR_DECODE_RATE_DEFAULT;
    }
    else
    {
        camera.fps_enabled = 1;
        snapshot->qr_decode_rate = SETTINGS_QR_DECODE_RATE_DEFAULT;
    }
    snapshot->camera = camera;
#else
    if((payload->qr_rate_fps_mark & 0x0FU) == SETTINGS_FPS_MARK_LOW)
    {
        snapshot->qr_decode_rate =
            (payload->qr_rate_fps_mark >> 4) >= SETTINGS_QR_DECODE_RATE_MIN &&
            (payload->qr_rate_fps_mark >> 4) <= SETTINGS_QR_DECODE_RATE_MAX ?
            (uint8_t)(payload->qr_rate_fps_mark >> 4) : SETTINGS_QR_DECODE_RATE_DEFAULT;
    }
    else
    {
        snapshot->qr_decode_rate = SETTINGS_QR_DECODE_RATE_DEFAULT;
    }
#endif
}

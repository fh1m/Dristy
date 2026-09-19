#include "internal/camera_settings_state.h"

#include "camera_light.h"
#include "camera_session.h"
#include "camera_persist_settings.h"

#include "../core/photo_types.h"
#include "../core/camera_types.h"

#include "camera_fps.h"
#include "../core/hk_binary.h"
#include "../core/hk_camera_sizes.h"
#include "settings_lights.h"
#include "settings_persistence.h"

static uint8_t g_camera_review_after_shot;
static camera_light_mode_t g_camera_light_mode = CAMERA_LIGHT_LED;
static uint8_t g_camera_rgb_red = 100;
static uint8_t g_camera_rgb_green = 100;
static uint8_t g_camera_rgb_blue = 100;
static photo_format_t g_camera_photo_format = PHOTO_FORMAT_RAW565;
static camera_size_t g_camera_size = CAMERA_SIZE_320X240;
static uint8_t g_camera_size_pending_reinit;

void camera_service_persist_defaults(void)
{
    camera_persist_settings_t settings = {
        .review_after_shot = 0,
        .fps_enabled = 1,
        .light_mode = CAMERA_LIGHT_LED,
        .rgb_red = 100,
        .rgb_green = 100,
        .rgb_blue = 100,
        .photo_format = PHOTO_FORMAT_RAW565,
        .size = CAMERA_SIZE_320X240,
    };

    camera_service_persist_apply(&settings);
}

void camera_service_persist_get(camera_persist_settings_t *settings)
{
    if(!settings)
        return;

    settings->review_after_shot = g_camera_review_after_shot ? 1 : 0;
    settings->fps_enabled = camera_service_fps_enabled() ? 1 : 0;
    settings->light_mode = g_camera_light_mode;
    settings->rgb_red = clamp_u8(g_camera_rgb_red, 0, 100);
    settings->rgb_green = clamp_u8(g_camera_rgb_green, 0, 100);
    settings->rgb_blue = clamp_u8(g_camera_rgb_blue, 0, 100);
    settings->photo_format = g_camera_photo_format < PHOTO_FORMAT_COUNT ? g_camera_photo_format : PHOTO_FORMAT_RAW565;
    settings->size = camera_size_is_safe(g_camera_size) ? g_camera_size : CAMERA_SIZE_320X240;
}

void camera_service_persist_apply(const camera_persist_settings_t *settings)
{
    if(!settings)
    {
        camera_service_persist_defaults();
        return;
    }

    g_camera_review_after_shot = settings->review_after_shot ? 1 : 0;
    camera_fps_apply_persisted(settings->fps_enabled);
    g_camera_light_mode = settings->light_mode == CAMERA_LIGHT_RGB ? CAMERA_LIGHT_RGB : CAMERA_LIGHT_LED;
    g_camera_rgb_red = clamp_u8(settings->rgb_red, 0, 100);
    g_camera_rgb_green = clamp_u8(settings->rgb_green, 0, 100);
    g_camera_rgb_blue = clamp_u8(settings->rgb_blue, 0, 100);
    g_camera_photo_format = settings->photo_format < PHOTO_FORMAT_COUNT ? settings->photo_format : PHOTO_FORMAT_RAW565;
    camera_settings_force_size(settings->size);
    camera_active_size_set(g_camera_size);
}

uint8_t camera_service_review_after_shot(void)
{
    return g_camera_review_after_shot;
}

camera_light_mode_t camera_service_light_mode(void)
{
    return g_camera_light_mode;
}

uint8_t camera_service_rgb_channel(camera_rgb_channel_t channel)
{
    if(channel == CAMERA_RGB_RED)
        return g_camera_rgb_red;
    if(channel == CAMERA_RGB_GREEN)
        return g_camera_rgb_green;
    return g_camera_rgb_blue;
}

photo_format_t camera_service_photo_format(void)
{
    return g_camera_photo_format;
}

camera_size_t camera_service_size(void)
{
    return g_camera_size;
}

uint8_t camera_service_toggle_review_after_shot(void)
{
    g_camera_review_after_shot = g_camera_review_after_shot ? 0 : 1;
    settings_mark_dirty(1);
    return g_camera_review_after_shot;
}

camera_light_mode_t camera_service_toggle_light_mode(void)
{
    g_camera_light_mode = g_camera_light_mode == CAMERA_LIGHT_RGB ? CAMERA_LIGHT_LED : CAMERA_LIGHT_RGB;
    camera_light_apply();
    settings_mark_dirty(1);
    return g_camera_light_mode;
}

uint8_t camera_service_cycle_rgb_channel(camera_rgb_channel_t channel)
{
    uint8_t *value = &g_camera_rgb_blue;

    if(channel == CAMERA_RGB_RED)
        value = &g_camera_rgb_red;
    else if(channel == CAMERA_RGB_GREEN)
        value = &g_camera_rgb_green;

    *value = *value >= 100U ? 0U : (uint8_t)(*value + 10U);
    camera_light_apply();
    settings_mark_dirty(1);
    return *value;
}

photo_format_t camera_service_cycle_photo_format(void)
{
    g_camera_photo_format = (photo_format_t)((g_camera_photo_format + 1U) % PHOTO_FORMAT_COUNT);
    settings_mark_dirty(1);
    return g_camera_photo_format;
}

camera_size_t camera_service_cycle_size(void)
{
    g_camera_size = camera_size_next(g_camera_size);
    g_camera_size_pending_reinit = 1;
    settings_mark_dirty(1);
    return g_camera_size;
}

void camera_settings_force_size(camera_size_t size)
{
    g_camera_size = camera_size_is_safe(size) ? size : CAMERA_SIZE_320X240;
}

uint8_t camera_settings_consume_size_pending(void)
{
    uint8_t pending = g_camera_size_pending_reinit;
    g_camera_size_pending_reinit = 0;
    return pending;
}

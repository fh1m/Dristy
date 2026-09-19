#include "camera_session_preferences.h"

#include "camera_light.h"
#include "camera_persist_settings.h"
#include "../core/hk_binary.h"

static camera_session_preferences_t g_override;
static uint8_t g_override_active;

void camera_session_preferences_override(const camera_session_preferences_t *preferences)
{
    if(!preferences)
        return;
    g_override = *preferences;
    g_override.fps_enabled = g_override.fps_enabled ? 1U : 0U;
    g_override.light_mode = g_override.light_mode == CAMERA_LIGHT_RGB ? CAMERA_LIGHT_RGB : CAMERA_LIGHT_LED;
    g_override.rgb_red = clamp_u8(g_override.rgb_red, 0U, 100U);
    g_override.rgb_green = clamp_u8(g_override.rgb_green, 0U, 100U);
    g_override.rgb_blue = clamp_u8(g_override.rgb_blue, 0U, 100U);
    g_override_active = 1U;
}

void camera_session_preferences_clear(void)
{
    g_override_active = 0U;
}

uint8_t camera_session_preferences_fps_enabled(void)
{
    return g_override_active ? g_override.fps_enabled : camera_service_fps_enabled();
}

camera_light_mode_t camera_session_preferences_light_mode(void)
{
    return g_override_active ? g_override.light_mode : camera_service_light_mode();
}

uint8_t camera_session_preferences_rgb_red(void)
{
    return g_override_active ? g_override.rgb_red : camera_service_rgb_channel(CAMERA_RGB_RED);
}

uint8_t camera_session_preferences_rgb_green(void)
{
    return g_override_active ? g_override.rgb_green : camera_service_rgb_channel(CAMERA_RGB_GREEN);
}

uint8_t camera_session_preferences_rgb_blue(void)
{
    return g_override_active ? g_override.rgb_blue : camera_service_rgb_channel(CAMERA_RGB_BLUE);
}

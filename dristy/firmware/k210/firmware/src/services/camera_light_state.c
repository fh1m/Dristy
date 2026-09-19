#include "camera_light.h"

#include <stdio.h>

#include "../core/hk_binary.h"
#include "settings_lights.h"
#include "camera_session_preferences.h"

static uint8_t g_camera_light_level;
static uint8_t g_camera_light_active;

uint8_t camera_service_light_overlay_enabled(void)
{
    return g_camera_light_level != 0;
}

void camera_service_format_light_overlay(char *line, size_t line_size)
{
    snprintf(line, line_size, "%s %u", camera_light_mode_label(camera_session_preferences_light_mode()), g_camera_light_level);
}

uint8_t camera_service_light_level(void)
{
    return g_camera_light_level;
}

void camera_service_set_light_level(uint8_t level)
{
    g_camera_light_level = clamp_u8(level, 0, 100);
}

uint8_t camera_service_light_active(void)
{
    return g_camera_light_active;
}

void camera_service_set_light_active(uint8_t active)
{
    g_camera_light_active = active ? 1 : 0;
}

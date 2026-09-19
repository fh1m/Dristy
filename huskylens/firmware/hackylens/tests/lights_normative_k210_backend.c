#include "lights_normative_backend.h"

#include <hackylens/capability/lights.h>

extern const hk_capability_provider_t hk_k210_lights_provider;

static uint64_t s_now_us;
static uint32_t s_effect_count;
static uint32_t s_active_mask;
static uint32_t s_safe_off_mask;

uint64_t hal_time_us(void)
{
    return s_now_us;
}

void lights_driver_prepare(void)
{
}

void lights_screen_backlight_set(uint8_t percent)
{
    s_effect_count++;
    if(percent != 0U)
        s_active_mask |= HK_LIGHTS_CHANNEL_BACKLIGHT;
    else
        s_active_mask &= ~HK_LIGHTS_CHANNEL_BACKLIGHT;
}

void lights_screen_backlight_off(void)
{
    s_effect_count++;
    s_active_mask &= ~HK_LIGHTS_CHANNEL_BACKLIGHT;
    s_safe_off_mask |= HK_LIGHTS_CHANNEL_BACKLIGHT;
}

void lights_illum_set(uint8_t enabled, uint8_t brightness)
{
    (void)brightness;
    s_effect_count++;
    if(enabled)
        s_active_mask |= HK_LIGHTS_CHANNEL_ILLUMINATION;
    else
    {
        s_active_mask &= ~HK_LIGHTS_CHANNEL_ILLUMINATION;
        s_safe_off_mask |= HK_LIGHTS_CHANNEL_ILLUMINATION;
    }
}

void lights_rgb_set(
    uint8_t enabled, uint8_t red, uint8_t green, uint8_t blue)
{
    (void)red;
    (void)green;
    (void)blue;
    s_effect_count++;
    if(enabled)
        s_active_mask |= HK_LIGHTS_CHANNEL_RGB;
    else
    {
        s_active_mask &= ~HK_LIGHTS_CHANNEL_RGB;
        s_safe_off_mask |= HK_LIGHTS_CHANNEL_RGB;
    }
}

const hk_capability_provider_t *lights_normative_backend_provider(void)
{
    return &hk_k210_lights_provider;
}

const char *lights_normative_backend_name(void)
{
    return "k210";
}

void lights_normative_backend_reset(uint64_t now_us)
{
    s_now_us = now_us;
    s_effect_count = 0U;
    s_active_mask = 0U;
    s_safe_off_mask = 0U;
}

void lights_normative_backend_set_now(uint64_t now_us)
{
    s_now_us = now_us;
}

uint32_t lights_normative_backend_effect_count(void)
{
    return s_effect_count;
}

uint32_t lights_normative_backend_active_mask(void)
{
    return s_active_mask;
}

uint32_t lights_normative_backend_safe_off_mask(void)
{
    return s_safe_off_mask;
}

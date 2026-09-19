#include "settings_lights.h"

#include <stddef.h>

#include <hackylens/capability/lights.h>

#include "settings_service.h"
#include "../core/hk_capability_client.h"

typedef struct
{
    const char *consumer_id;
    uint32_t channel;
    uint64_t feature;
    hk_owner_t owner;
    hk_lights_t handle;
} settings_light_lease_t;

static settings_light_lease_t s_backlight = {
    "consumer:settings-lights", HK_LIGHTS_CHANNEL_BACKLIGHT,
    HK_LIGHTS_FEATURE_BACKLIGHT, HK_OWNER_NONE, {HK_LEASE_NONE},
};
static settings_light_lease_t s_illumination = {
    "consumer:settings-lights", HK_LIGHTS_CHANNEL_ILLUMINATION,
    HK_LIGHTS_FEATURE_ILLUMINATION, HK_OWNER_NONE, {HK_LEASE_NONE},
};
static settings_light_lease_t s_rgb = {
    "consumer:settings-lights", HK_LIGHTS_CHANNEL_RGB,
    HK_LIGHTS_FEATURE_RGB, HK_OWNER_NONE, {HK_LEASE_NONE},
};

static uint8_t owner_equal(hk_owner_t left, hk_owner_t right)
{
    return (uint8_t)(left.slot == right.slot &&
                     left.generation == right.generation);
}

static hk_result_t settings_light_acquire(settings_light_lease_t *light)
{
    hk_capability_request_t request = HK_LIGHTS_REQUEST_0_1_INIT;
    hk_owner_t owner;

    if(!light)
        return HK_ERR_INVALID_ARGUMENT;
    owner = capability_client_consumer_owner(light->consumer_id);
    if(hk_owner_is_zero(owner))
        return HK_ERR_CAPABILITY_ABSENT;
    if(!hk_lease_is_zero(&light->handle.lease) &&
       owner_equal(owner, light->owner))
        return HK_OK;
    light->handle.lease = HK_LEASE_NONE;
    light->owner = owner;
    request.required_features = light->feature;
    return hk_lights_acquire(
        owner, &request, light->channel, &light->handle);
}

static void settings_light_release(settings_light_lease_t *light)
{
    if(!light || hk_lease_is_zero(&light->handle.lease))
        return;
    (void)hk_lights_release(
        light->owner, HK_DEADLINE_IMMEDIATE, &light->handle);
}

void screen_brightness_apply(void)
{
    settings_set_screen_brightness(settings_screen_brightness());
    if(settings_light_acquire(&s_backlight) == HK_OK)
        (void)hk_lights_set_level(
            s_backlight.owner, &s_backlight.handle,
            HK_LIGHTS_CHANNEL_BACKLIGHT,
            (uint16_t)settings_screen_brightness() * 10U,
            HK_DEADLINE_IMMEDIATE, NULL);
}

void screen_brightness_off(void)
{
    if(settings_light_acquire(&s_backlight) == HK_OK)
        (void)hk_lights_set_level(
            s_backlight.owner, &s_backlight.handle,
            HK_LIGHTS_CHANNEL_BACKLIGHT, 0U,
            HK_DEADLINE_IMMEDIATE, NULL);
}

void illum_led_apply(void)
{
    uint16_t level;

    settings_set_led_brightness(settings_led_brightness());
    level = settings_led_enabled() ?
            (uint16_t)settings_led_brightness() * 10U : 0U;
    if(settings_light_acquire(&s_illumination) == HK_OK)
        (void)hk_lights_set_level(
            s_illumination.owner, &s_illumination.handle,
            HK_LIGHTS_CHANNEL_ILLUMINATION, level,
            HK_DEADLINE_IMMEDIATE, NULL);
}

void rgb_led_apply(void)
{
    uint16_t red;
    uint16_t green;
    uint16_t blue;

    settings_set_rgb_red(settings_rgb_red());
    settings_set_rgb_green(settings_rgb_green());
    settings_set_rgb_blue(settings_rgb_blue());
    red = settings_rgb_enabled() ? (uint16_t)settings_rgb_red() * 10U : 0U;
    green = settings_rgb_enabled() ?
            (uint16_t)settings_rgb_green() * 10U : 0U;
    blue = settings_rgb_enabled() ? (uint16_t)settings_rgb_blue() * 10U : 0U;
    if(settings_light_acquire(&s_rgb) == HK_OK)
        (void)hk_lights_set_rgb(
            s_rgb.owner, &s_rgb.handle, red, green, blue,
            HK_DEADLINE_IMMEDIATE, NULL);
}

void settings_lights_suspend(uint32_t channels)
{
    if(channels & HK_LIGHTS_CHANNEL_BACKLIGHT)
        settings_light_release(&s_backlight);
    if(channels & HK_LIGHTS_CHANNEL_ILLUMINATION)
        settings_light_release(&s_illumination);
    if(channels & HK_LIGHTS_CHANNEL_RGB)
        settings_light_release(&s_rgb);
}

void settings_lights_restore(uint32_t channels)
{
    if(channels & HK_LIGHTS_CHANNEL_BACKLIGHT)
        screen_brightness_apply();
    if(channels & HK_LIGHTS_CHANNEL_ILLUMINATION)
        illum_led_apply();
    if(channels & HK_LIGHTS_CHANNEL_RGB)
        rgb_led_apply();
}

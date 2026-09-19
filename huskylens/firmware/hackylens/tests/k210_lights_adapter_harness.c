#include <hackylens/capability/lights.h>

#include <stdio.h>

#include "../firmware/src/capabilities/capability_provider.h"
#include "../firmware/src/capabilities/lights_provider.h"

#define CHECK(condition)                                                     \
    do                                                                       \
    {                                                                        \
        if(!(condition))                                                     \
        {                                                                    \
            printf("K210_LIGHTS_FAIL line=%d\n", __LINE__);                \
            return 1;                                                        \
        }                                                                    \
    } while(0)

extern const hk_capability_provider_t hk_k210_lights_provider;

static uint64_t s_now_us = 100U;
static uint32_t s_writes;
static uint32_t s_backlight_off;
static uint8_t s_illum_enabled;
static uint8_t s_illum_percent;
static uint8_t s_red;
static uint8_t s_green;
static uint8_t s_blue;
static uint32_t s_prepare_calls;

uint64_t hal_time_us(void)
{
    return s_now_us;
}

void lights_driver_prepare(void)
{
    s_prepare_calls++;
}

void lights_screen_backlight_set(uint8_t percent)
{
    (void)percent;
    s_writes++;
}

void lights_screen_backlight_off(void)
{
    s_backlight_off++;
    s_writes++;
}

void lights_illum_set(uint8_t enabled, uint8_t brightness)
{
    s_illum_enabled = enabled;
    s_illum_percent = brightness;
    s_writes++;
}

void lights_rgb_set(
    uint8_t enabled, uint8_t red, uint8_t green, uint8_t blue)
{
    s_red = enabled ? red : 0U;
    s_green = enabled ? green : 0U;
    s_blue = enabled ? blue : 0U;
    s_writes++;
}

static uint8_t cancelled(const void *context)
{
    return *(const uint8_t *)context;
}

static hk_lease_t lease(uint32_t slot, hk_owner_t owner)
{
    hk_lease_t value = {
        slot, 1U, owner, HK_CAPABILITY_ID_LIGHTS,
    };
    return value;
}

int main(void)
{
    hk_lights_provider_t *provider =
        (hk_lights_provider_t *)hk_k210_lights_provider.context;
    hk_owner_t owner_a = {1U, 1U};
    hk_owner_t owner_b = {2U, 1U};
    hk_owner_t owner_c = {3U, 1U};
    hk_lease_t illumination = lease(1U, owner_a);
    hk_lease_t rgb = lease(2U, owner_b);
    hk_lease_t conflicting = lease(3U, owner_c);
    hk_lights_info_t info;
    uint8_t cancel_flag = 1U;
    hk_cancel_t cancel = {cancelled, &cancel_flag};
    uint32_t writes_before_cleanup;

    CHECK(provider && hk_k210_lights_provider.max_leases == 16U);
    CHECK(provider->get_info(provider->context, &info) == HK_OK);
    CHECK(info.supported_channels == HK_LIGHTS_CHANNEL_ALL &&
          info.maximum_level == 1000U);
    CHECK(provider->open_channels(
        provider->context, &illumination,
        HK_LIGHTS_CHANNEL_ILLUMINATION) == HK_OK);
    CHECK(provider->open_channels(
        provider->context, &rgb, HK_LIGHTS_CHANNEL_RGB) == HK_OK);
    CHECK(s_prepare_calls == 1U);
    CHECK(provider->open_channels(
        provider->context, &conflicting,
        HK_LIGHTS_CHANNEL_BACKLIGHT | HK_LIGHTS_CHANNEL_ILLUMINATION) ==
          HK_ERR_BUSY);

    CHECK(provider->set_level(
        provider->context, &illumination,
        HK_LIGHTS_CHANNEL_ILLUMINATION, 505U,
        HK_DEADLINE_IMMEDIATE, &cancel) == HK_ERR_CANCELLED);
    CHECK(provider->set_level(
        provider->context, &illumination,
        HK_LIGHTS_CHANNEL_ILLUMINATION, 505U,
        (hk_deadline_t){100U}, NULL) == HK_ERR_DEADLINE_EXCEEDED);
    CHECK(s_writes == 0U);
    cancel_flag = 0U;
    CHECK(provider->set_level(
        provider->context, &illumination,
        HK_LIGHTS_CHANNEL_ILLUMINATION, 505U,
        HK_DEADLINE_IMMEDIATE, &cancel) == HK_OK);
    CHECK(s_illum_enabled == 1U && s_illum_percent == 51U);
    CHECK(provider->set_rgb(
        provider->context, &rgb, 1000U, 500U, 1U,
        HK_DEADLINE_IMMEDIATE, NULL) == HK_OK);
    CHECK(s_red == 100U && s_green == 50U && s_blue == 0U);
    CHECK(provider->set_level(
        provider->context, &illumination,
        HK_LIGHTS_CHANNEL_BACKLIGHT, 500U,
        HK_DEADLINE_IMMEDIATE, NULL) == HK_ERR_WRONG_OWNER);

    writes_before_cleanup = s_writes;
    CHECK(provider->close_channels(
        provider->context, &illumination, (hk_deadline_t){100U}) ==
          HK_ERR_DEADLINE_EXCEEDED);
    CHECK(s_writes == writes_before_cleanup && s_illum_enabled == 1U);
    CHECK(provider->close_channels(
        provider->context, &illumination, HK_DEADLINE_IMMEDIATE) == HK_OK);
    CHECK(s_illum_enabled == 0U && s_illum_percent == 0U);
    writes_before_cleanup = s_writes;
    CHECK(hk_k210_lights_provider.cleanup(
        hk_k210_lights_provider.context, owner_b,
        (hk_deadline_t){100U}) == HK_ERR_DEADLINE_EXCEEDED);
    CHECK(s_writes == writes_before_cleanup && s_red == 100U &&
          s_green == 50U && s_blue == 0U);
    CHECK(hk_k210_lights_provider.cleanup(
        hk_k210_lights_provider.context, owner_b,
        HK_DEADLINE_IMMEDIATE) == HK_OK);
    CHECK(s_red == 0U && s_green == 0U && s_blue == 0U);
    CHECK(s_backlight_off == 0U);

    printf("K210_LIGHTS_OK writes=%u illum_percent=51 rgb=100/50/0\n",
           (unsigned)s_writes);
    return 0;
}

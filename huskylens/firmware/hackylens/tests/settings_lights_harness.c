#include <hackylens/capability/lights.h>

#include <stdio.h>

#include "../firmware/src/services/settings_lights.h"

#define CHECK(condition)                                                     \
    do                                                                       \
    {                                                                        \
        if(!(condition))                                                     \
        {                                                                    \
            printf("SETTINGS_LIGHTS_FAIL line=%d\n", __LINE__);            \
            return 1;                                                        \
        }                                                                    \
    } while(0)

static uint8_t s_led_enabled = 1U;
static uint8_t s_led_brightness = 40U;
static uint8_t s_rgb_enabled = 1U;
static uint8_t s_rgb_red = 20U;
static uint8_t s_rgb_green = 30U;
static uint8_t s_rgb_blue = 40U;
static uint8_t s_backlight = 90U;
static uint32_t s_acquired_mask;
static uint32_t s_released_mask;
static uint32_t s_acquire_count;
static uint32_t s_write_count;
static uint16_t s_last_illumination;
static uint16_t s_last_red;
static uint16_t s_last_green;
static uint16_t s_last_blue;

uint8_t settings_led_enabled(void) { return s_led_enabled; }
uint8_t settings_led_brightness(void) { return s_led_brightness; }
uint8_t settings_rgb_enabled(void) { return s_rgb_enabled; }
uint8_t settings_rgb_red(void) { return s_rgb_red; }
uint8_t settings_rgb_green(void) { return s_rgb_green; }
uint8_t settings_rgb_blue(void) { return s_rgb_blue; }
uint8_t settings_screen_brightness(void) { return s_backlight; }
void settings_set_led_brightness(uint8_t value) { s_led_brightness = value; }
void settings_set_rgb_red(uint8_t value) { s_rgb_red = value; }
void settings_set_rgb_green(uint8_t value) { s_rgb_green = value; }
void settings_set_rgb_blue(uint8_t value) { s_rgb_blue = value; }
void settings_set_screen_brightness(uint8_t value) { s_backlight = value; }

hk_owner_t capability_client_consumer_owner(const char *consumer_id)
{
    hk_owner_t owner = {1U, 1U};

    if(consumer_id && consumer_id[18] == 'i')
        owner.slot = 2U;
    else if(consumer_id && consumer_id[18] == 'r')
        owner.slot = 3U;
    return owner;
}

hk_result_t hk_lights_acquire(
    hk_owner_t owner, const hk_capability_request_t *request,
    uint32_t channels, hk_lights_t *handle)
{
    (void)request;
    if(!handle || channels == 0U)
        return HK_ERR_INVALID_ARGUMENT;
    handle->lease = (hk_lease_t){
        channels, s_acquire_count + 1U, owner, HK_CAPABILITY_ID_LIGHTS,
    };
    s_acquired_mask |= channels;
    s_acquire_count++;
    return HK_OK;
}

hk_result_t hk_lights_release(
    hk_owner_t owner, hk_deadline_t deadline, hk_lights_t *handle)
{
    (void)owner;
    (void)deadline;
    if(!handle)
        return HK_ERR_INVALID_ARGUMENT;
    s_released_mask |= handle->lease.slot;
    handle->lease = HK_LEASE_NONE;
    return HK_OK;
}

hk_result_t hk_lights_set_level(
    hk_owner_t owner, const hk_lights_t *handle, uint32_t channel,
    uint16_t level, hk_deadline_t deadline, const hk_cancel_t *cancel)
{
    (void)owner;
    (void)handle;
    (void)deadline;
    (void)cancel;
    if(channel == HK_LIGHTS_CHANNEL_ILLUMINATION)
        s_last_illumination = level;
    s_write_count++;
    return HK_OK;
}

hk_result_t hk_lights_set_rgb(
    hk_owner_t owner, const hk_lights_t *handle, uint16_t red,
    uint16_t green, uint16_t blue, hk_deadline_t deadline,
    const hk_cancel_t *cancel)
{
    (void)owner;
    (void)handle;
    (void)deadline;
    (void)cancel;
    s_last_red = red;
    s_last_green = green;
    s_last_blue = blue;
    s_write_count++;
    return HK_OK;
}

int main(void)
{
    screen_brightness_apply();
    illum_led_apply();
    rgb_led_apply();
    CHECK(s_acquire_count == 3U && s_write_count == 3U);
    CHECK(s_acquired_mask == HK_LIGHTS_CHANNEL_ALL);
    CHECK(s_last_illumination == 400U && s_last_red == 200U &&
          s_last_green == 300U && s_last_blue == 400U);

    settings_lights_suspend(
        HK_LIGHTS_CHANNEL_ILLUMINATION | HK_LIGHTS_CHANNEL_RGB);
    CHECK(s_released_mask ==
          (HK_LIGHTS_CHANNEL_ILLUMINATION | HK_LIGHTS_CHANNEL_RGB));
    s_led_enabled = 1U;
    s_led_brightness = 70U;
    s_rgb_enabled = 1U;
    s_rgb_red = 11U;
    s_rgb_green = 22U;
    s_rgb_blue = 33U;
    settings_lights_restore(
        HK_LIGHTS_CHANNEL_ILLUMINATION | HK_LIGHTS_CHANNEL_RGB);
    CHECK(s_acquire_count == 5U && s_write_count == 5U);
    CHECK(s_last_illumination == 700U && s_last_red == 110U &&
          s_last_green == 220U && s_last_blue == 330U);

    printf("SETTINGS_LIGHTS_OK reacquired=2 latest=700/110/220/330\n");
    return 0;
}

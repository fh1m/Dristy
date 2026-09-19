#include <stdint.h>
#include <stdio.h>

#include <hackylens/capability/time.h>

#include "../firmware/src/apps/sleep/sleep_controller.h"
#include "../firmware/src/core/hk_screen.h"

static screen_t g_screen;
static uint64_t g_last_activity;
static uint64_t g_now;
static hk_result_t g_acquire_result;
static hk_result_t g_now_result;
static uint64_t g_requested_features;
static uint32_t g_acquire_count;
static uint32_t g_sleep_count;
static uint32_t g_wake_count;

static int check(uint8_t condition, const char *message)
{
    if(condition)
        return 0;
    fprintf(stderr, "sleep controller check failed: %s\n", message);
    return 1;
}

static void reset_fixture(void)
{
    g_screen = SCREEN_MENU;
    g_last_activity = 1000000U;
    g_now = g_last_activity;
    g_acquire_result = HK_OK;
    g_now_result = HK_OK;
    g_requested_features = 0U;
    g_acquire_count = 0U;
    g_sleep_count = 0U;
    g_wake_count = 0U;
}

hk_owner_t capability_client_consumer_owner(const char *consumer_id)
{
    (void)consumer_id;
    return (hk_owner_t){1U, 1U};
}

hk_result_t hk_time_acquire(
    hk_owner_t owner,
    const hk_capability_request_t *request,
    hk_time_t *handle)
{
    g_acquire_count++;
    g_requested_features = request ? request->required_features : 0U;
    if(g_acquire_result == HK_OK)
    {
        handle->lease = (hk_lease_t){1U, 1U, owner, HK_CAPABILITY_ID_TIME};
    }
    return g_acquire_result;
}

hk_result_t hk_time_now_us(
    hk_owner_t owner,
    const hk_time_t *handle,
    uint64_t *value)
{
    (void)owner;
    (void)handle;
    if(g_now_result == HK_OK)
        *value = g_now;
    return g_now_result;
}

screen_t hk_screen_get(void)
{
    return g_screen;
}

void hk_screen_set(screen_t screen)
{
    g_screen = screen;
    if(screen == SCREEN_SLEEP)
        g_sleep_count++;
}

void hk_screen_request_wake(void)
{
    g_wake_count++;
}

uint64_t hk_last_activity_us(void)
{
    return g_last_activity;
}

uint8_t hk_auto_sleep_minutes(void)
{
    return 1U;
}

void hk_back_exit_set_armed(uint8_t armed)
{
    (void)armed;
}

void sleep_view_enter(void)
{
}

void screen_brightness_off(void)
{
}

int main(void)
{
    hk_input_snapshot_t idle = {0U, 0U, 0U};
    hk_input_snapshot_t pressed = {1U, 1U, 1U};
    int failed = 0;

    reset_fixture();
    g_acquire_result = HK_ERR_FEATURE_UNAVAILABLE;
    auto_sleep_controller_tick(&idle);
    failed |= check(g_acquire_count == 1U, "TIME must be acquired once");
    failed |= check(
        g_requested_features == HK_TIME_FEATURE_MONOTONIC_US,
        "sleep controller must request monotonic-us only");
    failed |= check(
        g_sleep_count == 0U,
        "TIME acquire failure must not force sleep");

    reset_fixture();
    g_now_result = HK_ERR_INTERNAL;
    auto_sleep_controller_tick(&idle);
    failed |= check(
        g_sleep_count == 0U,
        "TIME read failure must not force sleep");

    reset_fixture();
    auto_sleep_controller_tick(&idle);
    failed |= check(g_sleep_count == 0U, "fresh activity must stay awake");

    reset_fixture();
    g_now = g_last_activity - 1U;
    auto_sleep_controller_tick(&idle);
    failed |= check(
        g_sleep_count == 0U,
        "backward time must not underflow into sleep");

    reset_fixture();
    g_now = g_last_activity + 60000000U;
    auto_sleep_controller_tick(&idle);
    failed |= check(
        g_sleep_count == 1U && g_screen == SCREEN_SLEEP,
        "exact inactivity deadline must enter sleep");

    reset_fixture();
    g_now = g_last_activity + 60000000U;
    auto_sleep_controller_tick(&pressed);
    failed |= check(
        g_sleep_count == 0U,
        "held input must suppress auto sleep");

    reset_fixture();
    sleep_controller_handle_buttons(&pressed);
    failed |= check(g_wake_count == 1U, "button press must request wake");

    if(failed)
        return 1;
    puts("SLEEP_CONTROLLER_OK monotonic_only=1 failure_safe=1 wrap_safe=1");
    return 0;
}

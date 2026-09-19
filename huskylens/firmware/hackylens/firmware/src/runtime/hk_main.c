#include "hk_main.h"

#include <stdio.h>
#include <string.h>

#include <hackylens/capability/input.h>

#include "../core/hk_app_registry.h"
#include "../core/hk_dispatch.h"
#include "../core/hk_menu.h"
#include "../core/hk_screen.h"
#include "../core/hk_capability_client.h"
#include "hal_time.h"

static hk_main_hooks_t s_hooks;

static hk_input_t s_runtime_input;
static hk_owner_t s_runtime_input_owner;

static hk_result_t runtime_input_prepare(void)
{
    static const hk_capability_request_t request = HK_INPUT_REQUEST_0_1_INIT;

    s_runtime_input_owner = capability_client_consumer_owner(
        "consumer:firmware-runtime");
    if(hk_owner_is_zero(s_runtime_input_owner))
        return HK_ERR_STALE_HANDLE;
    return hk_input_acquire(
        s_runtime_input_owner, &request, &s_runtime_input);
}

static hk_result_t runtime_input_sample(uint32_t *state)
{
    return hk_input_get_state(
        s_runtime_input_owner, &s_runtime_input, state);
}

static hk_result_t runtime_input_dispatch(hk_input_snapshot_t *snapshot)
{
    hk_input_event_t event;
    hk_result_t result;

    if(!snapshot)
        return HK_ERR_INVALID_ARGUMENT;
    *snapshot = (hk_input_snapshot_t){0U, 0U, 0U};
    result = runtime_input_sample(&snapshot->state);
    if(result != HK_OK)
        return result;
    while((result = hk_input_next_event(
               s_runtime_input_owner, &s_runtime_input, &event)) == HK_OK)
    {
        snapshot->state = event.state;
        snapshot->pressed = event.pressed;
        snapshot->changed = event.changed;
        activity_note();
        shell_handle_buttons(snapshot);
    }
    if(result == HK_ERR_OVERFLOW)
    {
        snapshot->state = event.state;
        snapshot->pressed = 0U;
        snapshot->changed = 0U;
        return HK_OK;
    }
    return result == HK_PENDING ? HK_OK : result;
}

void hk_main_set_hooks(const hk_main_hooks_t *hooks)
{
    if(hooks)
        s_hooks = *hooks;
    else
        memset(&s_hooks, 0, sizeof(s_hooks));
}

int hk_main(void)
{
    uint64_t next_dispatch_us = 0U;

    if(s_hooks.startup)
        s_hooks.startup();
    if(runtime_input_prepare() != HK_OK)
    {
        printf("[INPUT] capability unavailable\r\n");
        return -1;
    }
    while(1)
    {
        const hk_app_t *app;
        hk_input_snapshot_t input;
        uint64_t now_us;
        uint64_t sleep_us;
        uint8_t tick_interval_ms;

        input = (hk_input_snapshot_t){0U, 0U, 0U};
        if(runtime_input_sample(&input.state) != HK_OK)
        {
            printf("[INPUT] provider failure\r\n");
            return -1;
        }
        now_us = hal_time_us();
        if(next_dispatch_us != 0U && now_us < next_dispatch_us)
        {
            sleep_us = next_dispatch_us - now_us;
            if(sleep_us > HK_INPUT_SAMPLE_INTERVAL_US)
                sleep_us = HK_INPUT_SAMPLE_INTERVAL_US;
            hal_sleep_ms((uint32_t)((sleep_us + 999U) / 1000U));
            continue;
        }
        if(runtime_input_dispatch(&input) != HK_OK)
        {
            printf("[INPUT] event dispatch failure\r\n");
            return -1;
        }
        if(s_hooks.debug_tick)
            s_hooks.debug_tick();
        if(hk_screen_get() == SCREEN_MENU)
            menu_tick(&input);
        app = hk_app_for_screen(hk_screen_get());
        if(app && app->tick)
            app->tick(&input);
        if(s_hooks.system_tick)
            s_hooks.system_tick(&input);
        app = hk_app_for_screen(hk_screen_get());
        tick_interval_ms = app && app->screen == hk_screen_get() && app->tick_interval_ms ?
                           app->tick_interval_ms : 20U;
        next_dispatch_us = now_us + (uint64_t)tick_interval_ms * 1000U;
        sleep_us = (uint64_t)tick_interval_ms * 1000U;
        if(sleep_us > HK_INPUT_SAMPLE_INTERVAL_US)
            sleep_us = HK_INPUT_SAMPLE_INTERVAL_US;
        hal_sleep_ms((uint32_t)((sleep_us + 999U) / 1000U));
    }

    return 0;
}

#include "files_controller.h"

#include <stdio.h>

#include <hackylens/capability/time.h>

#include "../../core/hk_app.h"

#include "../../config/input_config.h"
#include "files_config.h"

#include "../../core/hk_back_exit.h"
#include "../../core/hk_menu.h"
#include "../../core/hk_screen.h"
#include "../../core/hk_capability_client.h"
#include "file_browser_mode.h"
#include "files_actions.h"
#include "files_presenter.h"

static uint32_t g_files_repeat_button;
static uint8_t g_files_repeat_ticks;
static uint64_t g_files_ok_press_us;
static uint8_t g_files_ok_active;
static uint8_t g_files_ok_hold_fired;
static hk_time_t s_files_time;
static hk_owner_t s_files_time_owner;

static uint64_t files_time_now_us(void)
{
    hk_capability_request_t request = HK_TIME_REQUEST_0_1_INIT;
    hk_owner_t owner = capability_client_current_owner();
    uint64_t value = 0U;

    request.required_features = HK_TIME_FEATURE_MONOTONIC_US;
    if(hk_owner_is_zero(owner))
        return 0U;
    if(owner.slot != s_files_time_owner.slot ||
       owner.generation != s_files_time_owner.generation ||
       hk_lease_is_zero(&s_files_time.lease))
    {
        s_files_time.lease = HK_LEASE_NONE;
        s_files_time_owner = owner;
        if(hk_time_acquire(owner, &request, &s_files_time) != HK_OK)
            return 0U;
    }
    if(hk_time_now_us(owner, &s_files_time, &value) != HK_OK)
        return 0U;
    return value;
}

void files_controller_reset_input(void)
{
    g_files_repeat_button = 0;
    g_files_repeat_ticks = 0;
    g_files_ok_press_us = 0;
    g_files_ok_active = 0;
    g_files_ok_hold_fired = 0;
}

static void files_repeat_reset(void)
{
    g_files_repeat_button = 0;
    g_files_repeat_ticks = 0;
}

static void files_repeat_start(uint32_t button)
{
    g_files_repeat_button = button;
    g_files_repeat_ticks = FILES_REPEAT_INITIAL_TICKS;
}

static uint8_t files_repeat_delay_active(void)
{
    if(g_files_repeat_ticks == 0)
        return 0;
    g_files_repeat_ticks--;
    return 1;
}

static void files_repeat_set_next_delay(void)
{
    g_files_repeat_ticks = FILES_REPEAT_NEXT_TICKS;
}

static void files_ok_reset(void)
{
    g_files_ok_press_us = 0;
    g_files_ok_active = 0;
    g_files_ok_hold_fired = 0;
}

static void files_ok_press(uint64_t now_us)
{
    g_files_ok_press_us = now_us;
    g_files_ok_active = 1U;
    g_files_ok_hold_fired = 0;
}

static uint8_t files_ok_release_should_open(void)
{
    return g_files_ok_active && !g_files_ok_hold_fired;
}

static uint8_t files_ok_hold_ready(uint32_t input_state, uint64_t now_us)
{
    if(!g_files_ok_active ||
       g_files_ok_press_us == 0U ||
       now_us == 0U ||
       now_us < g_files_ok_press_us ||
       g_files_ok_hold_fired ||
       !(input_state & BUTTON_OK) ||
       now_us - g_files_ok_press_us < FILES_OK_HOLD_US)
        return 0;

    g_files_ok_hold_fired = 1;
    g_files_ok_active = 0U;
    g_files_ok_press_us = 0;
    return 1;
}

static void files_controller_handle_nav_repeat(uint32_t input_state)
{
    if(g_files_repeat_button == 0 || !(input_state & g_files_repeat_button))
    {
        if(input_state & BUTTON_LEFT)
            files_repeat_start(BUTTON_LEFT);
        else if(input_state & BUTTON_RIGHT)
            files_repeat_start(BUTTON_RIGHT);
        else
            files_repeat_reset();
        return;
    }

    if(files_repeat_delay_active())
        return;

    if(g_files_repeat_button == BUTTON_LEFT)
        files_nav_delta(-1);
    else if(g_files_repeat_button == BUTTON_RIGHT)
        files_nav_delta(1);

    files_repeat_set_next_delay();
}

static void files_controller_tick_delete(uint32_t input_state, uint64_t now_us)
{
    file_browser_mode_t mode = files_mode();

    if(mode == FILES_MODE_DELETE_CONFIRM)
    {
        files_repeat_reset();
        return;
    }

    if((mode == FILES_MODE_LIST ||
        mode == FILES_MODE_TEXT ||
        mode == FILES_MODE_IMAGE) &&
       files_ok_hold_ready(input_state, now_us))
    {
        files_repeat_reset();
        files_delete_confirm_enter();
        return;
    }

    files_controller_handle_nav_repeat(input_state);
}

static void files_controller_handle_delete_confirm(uint32_t pressed)
{
    if(pressed & BUTTON_BACK)
    {
        files_repeat_reset();
        files_ok_reset();
        files_delete_cancel();
        return;
    }
    if(pressed & BUTTON_OK)
    {
        files_repeat_reset();
        files_ok_reset();
        files_delete_confirmed();
    }
}

static void files_controller_handle_preview_buttons(uint32_t pressed, uint32_t changed, uint32_t input_state, uint64_t now_us)
{
    if(pressed & BUTTON_BACK)
    {
        files_repeat_reset();
        files_presenter_close_image();
        files_set_mode(FILES_MODE_LIST);
        files_ok_reset();
        files_presenter_render_list();
        return;
    }
    if(pressed & BUTTON_LEFT)
    {
        files_nav_delta(-1);
        files_repeat_start(BUTTON_LEFT);
    }
    if(pressed & BUTTON_RIGHT)
    {
        files_nav_delta(1);
        files_repeat_start(BUTTON_RIGHT);
    }
    if(pressed & BUTTON_OK)
    {
        files_repeat_reset();
        files_ok_press(now_us);
        return;
    }
    if((changed & BUTTON_OK) && !(input_state & BUTTON_OK))
    {
        if(files_ok_release_should_open() && files_mode() == FILES_MODE_IMAGE)
            (void)files_presenter_toggle_image_pause(now_us);
        files_ok_reset();
    }
}

static uint8_t files_controller_handle_list_back(void)
{
    files_repeat_reset();
    files_ok_reset();
    return files_back_from_list();
}

static void files_controller_handle_list_buttons(uint32_t pressed, uint32_t changed, uint32_t input_state, uint64_t now_us)
{
    if(pressed & BUTTON_LEFT)
    {
        files_nav_delta(-1);
        files_repeat_start(BUTTON_LEFT);
    }
    if(pressed & BUTTON_RIGHT)
    {
        files_nav_delta(1);
        files_repeat_start(BUTTON_RIGHT);
    }
    if(pressed & BUTTON_OK)
    {
        files_repeat_reset();
        files_ok_press(now_us);
        return;
    }
    if((changed & BUTTON_OK) && !(input_state & BUTTON_OK))
    {
        if(files_ok_release_should_open())
            files_open_selected();
        files_ok_reset();
    }
}

void files_controller_enter(const hk_input_snapshot_t *input)
{
    hk_screen_set(SCREEN_FILES);
    hk_back_exit_set_armed(0);
    printf("[SHELL] screen FILES\r\n");
    files_controller_reset_input();
    files_backend_enter();
}

void files_controller_exit(void)
{
    files_presenter_close_image();
    files_controller_reset_input();
}

void files_controller_tick(const hk_input_snapshot_t *input)
{
    uint64_t now_us;

    if(hk_screen_get() != SCREEN_FILES)
    {
        files_repeat_reset();
        return;
    }

    now_us = files_time_now_us();
    files_controller_tick_delete(input->state, now_us);
    if(files_mode() == FILES_MODE_IMAGE)
        files_presenter_tick_image(now_us);
}

void files_controller_handle_buttons(const hk_input_snapshot_t *input)
{
    if(files_mode())
    {
        if(files_mode() == FILES_MODE_DELETE_CONFIRM)
        {
            files_controller_handle_delete_confirm(input->pressed);
            return;
        }

        files_controller_handle_preview_buttons(
            input->pressed, input->changed, input->state,
            files_time_now_us());
        return;
    }

    if(input->pressed & BUTTON_BACK)
    {
        if(files_controller_handle_list_back())
            shell_show_menu();
        return;
    }

    files_controller_handle_list_buttons(
        input->pressed, input->changed, input->state,
        files_time_now_us());
}

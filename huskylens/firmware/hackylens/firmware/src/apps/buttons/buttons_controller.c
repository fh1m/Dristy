#include "buttons_controller.h"

#include <limits.h>
#include <stdio.h>
#include <string.h>

#include "../../config/input_config.h"
#include "../../core/hk_app.h"
#include "../../core/hk_menu.h"
#include "../../core/hk_screen.h"
#include "buttons_view.h"

#define BUTTON_HOLD_TICKS 50U

static const uint32_t s_button_masks[4] = {
    BUTTON_LEFT, BUTTON_OK, BUTTON_RIGHT, BUTTON_BACK,
};
static buttons_view_state_t s_test;
static uint8_t s_hold_ticks[4];
static uint8_t s_exit_ticks;
static uint8_t s_exit_armed;
static uint32_t s_ignore_until_released;

static void buttons_controller_refresh_passed(uint32_t mask, uint8_t index)
{
    if((s_test.hold_passed & mask) && !(s_test.repeat_error & mask) &&
       s_test.pressed_count[index] != 0U &&
       s_test.pressed_count[index] == s_test.released_count[index])
        s_test.passed |= mask;
    else
        s_test.passed &= ~mask;
}

void buttons_controller_enter(const hk_input_snapshot_t *input)
{
    uint32_t initial_state = input ? input->state & BUTTON_ALL : 0U;

    memset(&s_test, 0, sizeof(s_test));
    memset(s_hold_ticks, 0, sizeof(s_hold_ticks));
    s_test.state = initial_state;
    s_ignore_until_released = initial_state;
    s_exit_ticks = 0U;
    s_exit_armed = 0U;
    hk_screen_set(SCREEN_BUTTONS);
    buttons_view_render(&s_test);
    printf("[SHELL] screen BUTTONS\r\n");
}

void buttons_controller_handle_buttons(const hk_input_snapshot_t *input)
{
    uint32_t changed;
    uint8_t footer_changed = 0U;

    if(!input)
        return;
    changed = input->changed & BUTTON_ALL;
    for(uint8_t index = 0U; index < 4U; index++)
    {
        uint32_t mask = s_button_masks[index];

        if(!(changed & mask))
            continue;
        if(s_ignore_until_released & mask)
        {
            if(!(input->state & mask))
                s_ignore_until_released &= ~mask;
            s_hold_ticks[index] = 0U;
            continue;
        }
        if(input->state & mask)
        {
            if((s_test.state & mask) || !(input->pressed & mask))
                s_test.repeat_error |= mask;
            if(s_test.pressed_count[index] != UINT16_MAX)
                s_test.pressed_count[index]++;
            s_hold_ticks[index] = 0U;
            s_test.passed &= ~mask;
        }
        else
        {
            if(s_test.released_count[index] != UINT16_MAX)
                s_test.released_count[index]++;
            buttons_controller_refresh_passed(mask, index);
            footer_changed = 1U;
        }
    }
    s_test.state = input->state & BUTTON_ALL;
    if(changed)
        buttons_view_update(&s_test, changed, footer_changed);
    if(s_exit_armed && !(s_test.state & (BUTTON_OK | BUTTON_BACK)))
        shell_show_menu();
}

void buttons_controller_tick(const hk_input_snapshot_t *input)
{
    uint32_t changed = 0U;

    if(!input)
        return;
    s_test.state = input->state & BUTTON_ALL;
    for(uint8_t index = 0U; index < 4U; index++)
    {
        uint32_t mask = s_button_masks[index];

        if(!(s_test.state & mask) || (s_ignore_until_released & mask))
        {
            s_hold_ticks[index] = 0U;
            continue;
        }
        if(s_hold_ticks[index] < BUTTON_HOLD_TICKS)
            s_hold_ticks[index]++;
        if(s_hold_ticks[index] == BUTTON_HOLD_TICKS &&
           !(s_test.hold_passed & mask))
        {
            s_test.hold_passed |= mask;
            changed |= mask;
        }
    }
    if((s_test.state & (BUTTON_OK | BUTTON_BACK)) ==
       (BUTTON_OK | BUTTON_BACK))
    {
        if(s_exit_ticks < BUTTON_HOLD_TICKS)
            s_exit_ticks++;
        if(s_exit_ticks == BUTTON_HOLD_TICKS)
            s_exit_armed = 1U;
    }
    else if(!s_exit_armed)
        s_exit_ticks = 0U;
    if(changed)
        buttons_view_update(&s_test, changed, 0U);
}

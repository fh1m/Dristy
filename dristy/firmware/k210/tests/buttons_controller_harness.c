#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../firmware/src/apps/buttons/buttons_controller.h"
#include "../firmware/src/apps/buttons/buttons_view.h"
#include "../firmware/src/config/input_config.h"
#include "../firmware/src/core/hk_app.h"

static buttons_view_state_t g_view;
static unsigned g_menu_shown;
static unsigned g_failures;

static void check(int condition, const char *message)
{
    if(!condition)
    {
        fprintf(stderr, "FAIL: %s\n", message);
        g_failures++;
    }
}

void hk_screen_set(screen_t screen)
{
    check(screen == SCREEN_BUTTONS, "controller must select BUTTONS screen");
}

void shell_show_menu(void)
{
    g_menu_shown++;
}

void buttons_view_render(const buttons_view_state_t *state)
{
    g_view = *state;
}

void buttons_view_update(
    const buttons_view_state_t *state, uint32_t changed, uint8_t footer_changed)
{
    (void)changed;
    (void)footer_changed;
    g_view = *state;
}

static hk_input_snapshot_t snapshot(
    uint32_t state, uint32_t pressed, uint32_t changed)
{
    hk_input_snapshot_t input = {state, pressed, changed};
    return input;
}

static void complete_button(uint32_t mask, uint8_t index)
{
    hk_input_snapshot_t input = snapshot(mask, mask, mask);

    buttons_controller_handle_buttons(&input);
    for(unsigned tick = 0U; tick < 50U; tick++)
        buttons_controller_tick(&input);
    check((g_view.hold_passed & mask) != 0U, "hold must be observed");
    input = snapshot(0U, 0U, mask);
    buttons_controller_handle_buttons(&input);
    check(g_view.pressed_count[index] == 1U, "press count must be one");
    check(g_view.released_count[index] == 1U, "release count must be one");
    check((g_view.passed & mask) != 0U, "completed button must pass");
}

int main(void)
{
    hk_input_snapshot_t input = snapshot(BUTTON_OK, BUTTON_OK, BUTTON_OK);

    memset(&g_view, 0, sizeof(g_view));
    buttons_controller_enter(&input);
    input = snapshot(0U, 0U, BUTTON_OK);
    buttons_controller_handle_buttons(&input);
    check(g_view.pressed_count[1] == 0U, "menu OK press must be ignored");
    check(g_view.released_count[1] == 0U, "menu OK release must be ignored");

    complete_button(BUTTON_LEFT, 0U);
    complete_button(BUTTON_OK, 1U);
    complete_button(BUTTON_RIGHT, 2U);
    complete_button(BUTTON_BACK, 3U);
    check(g_view.passed == BUTTON_ALL, "all four buttons must pass");
    check(g_menu_shown == 0U, "plain BACK must not exit the test");

    input = snapshot(BUTTON_OK, BUTTON_OK, BUTTON_OK);
    buttons_controller_handle_buttons(&input);
    input = snapshot(BUTTON_OK | BUTTON_BACK, BUTTON_BACK, BUTTON_BACK);
    buttons_controller_handle_buttons(&input);
    for(unsigned tick = 0U; tick < 50U; tick++)
        buttons_controller_tick(&input);
    input = snapshot(BUTTON_BACK, 0U, BUTTON_OK);
    buttons_controller_handle_buttons(&input);
    check(g_menu_shown == 0U, "exit waits for both chord buttons to release");
    input = snapshot(0U, 0U, BUTTON_BACK);
    buttons_controller_handle_buttons(&input);
    check(g_menu_shown == 1U, "held OK+BACK chord must exit on release");

    input = snapshot(0U, 0U, 0U);
    buttons_controller_enter(&input);
    input = snapshot(BUTTON_LEFT, BUTTON_LEFT, BUTTON_LEFT);
    buttons_controller_handle_buttons(&input);
    buttons_controller_handle_buttons(&input);
    check(
        (g_view.repeat_error & BUTTON_LEFT) != 0U,
        "duplicate press while down must be reported");

    if(g_failures)
        return 1;
    puts("BUTTONS_CONTROLLER_OK press_release=1 hold=1 repeat=1 back=1");
    return 0;
}

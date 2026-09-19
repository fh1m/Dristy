#include "hk_dispatch.h"

#include "hk_app.h"
#include "hk_events.h"

#include "../config/input_config.h"
#include "../config/menu_layout.h"

#include "hk_app_registry.h"
#include "hk_menu.h"
#include "hk_screen.h"

static hk_sd_event_handler_t s_sd_event_handler;

void shell_set_sd_event_handler(hk_sd_event_handler_t handler)
{
    s_sd_event_handler = handler;
}

void shell_handle_buttons(const hk_input_snapshot_t *input)
{
    const hk_app_t *app;
    uint32_t pressed = input->pressed;
    screen_t screen = hk_screen_get();

    if(screen == SCREEN_MENU)
    {
        if(pressed & BUTTON_LEFT)
        {
            menu_select_delta(-1);
            menu_repeat_start(BUTTON_LEFT);
        }
        if(pressed & BUTTON_RIGHT)
        {
            menu_select_delta(1);
            menu_repeat_start(BUTTON_RIGHT);
        }
        if((pressed & BUTTON_OK) && (input->state & BUTTON_RIGHT))
        {
            menu_page_delta(1);
            menu_repeat_start(BUTTON_OK | BUTTON_RIGHT);
        }
        else if(pressed & BUTTON_OK)
            shell_open_selected(input);
        if(pressed & BUTTON_BACK)
        {
            menu_page_delta(-1);
            menu_repeat_start(BUTTON_BACK);
        }
        return;
    }

    app = hk_app_for_screen(screen);
    if(app && app->handle_input)
        app->handle_input(input);
}

void shell_handle_sd_event(hk_sd_event_t event)
{
    if(s_sd_event_handler)
        s_sd_event_handler(event);
}

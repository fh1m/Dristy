#include "settings_controller.h"

#include <stdio.h>

#include "settings_menu.h"
#include "../../controllers/settings_menu_controller.h"
#include "../../core/hk_back_exit.h"
#include "../../core/hk_menu.h"
#include "../../core/hk_screen.h"

static settings_menu_session_t g_settings_menu;

void settings_controller_enter(const hk_input_snapshot_t *input)
{
    (void)input;
    hk_screen_set(SCREEN_SETTINGS);
    hk_back_exit_set_armed(0U);
    (void)settings_menu_open(&g_settings_menu, settings_app_menu_definition());
    printf("[SHELL] screen SETTINGS\r\n");
}

void settings_controller_exit(void)
{
    settings_menu_close(&g_settings_menu);
}

void settings_controller_handle_buttons(const hk_input_snapshot_t *input)
{
    if(settings_menu_handle_input(&g_settings_menu, input) ==
       SETTINGS_MENU_EVENT_CLOSE_REQUESTED)
        shell_show_menu();
}

void settings_controller_tick(const hk_input_snapshot_t *input)
{
    settings_menu_tick(&g_settings_menu, input);
}

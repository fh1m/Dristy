#include "camera_settings.h"

#include <stdio.h>

#include "../../config/input_config.h"
#include "../../controllers/settings_menu_controller.h"
#include "../../core/hk_back_exit.h"
#include "../../core/hk_screen.h"
#include "../../services/camera_session.h"
#include "camera_settings_menu.h"

static settings_menu_session_t g_camera_settings;

void camera_settings_open(void)
{
    hk_screen_set(SCREEN_CAMERA_SETTINGS);
    hk_back_exit_set_armed(0U);
    (void)settings_menu_open(&g_camera_settings, camera_settings_menu_definition());
    printf("[SHELL] screen CAMERA SETTINGS\r\n");
}

void camera_settings_close(void)
{
    settings_menu_close(&g_camera_settings);
}

void camera_settings_tick(const hk_input_snapshot_t *input)
{
    settings_menu_tick(&g_camera_settings, input);
}

uint8_t camera_settings_active(void)
{
    return settings_menu_active(&g_camera_settings);
}

camera_settings_exit_t camera_settings_handle_input(const hk_input_snapshot_t *input)
{
    camera_settings_return_t action;

    if(!input || settings_menu_handle_input(&g_camera_settings, input) !=
       SETTINGS_MENU_EVENT_CLOSE_REQUESTED)
        return CAMERA_SETTINGS_EXIT_NONE;

    settings_menu_close(&g_camera_settings);
    action = camera_service_prepare_settings_return(0U,
                                                    (input->state & BUTTON_OK) ? 1U : 0U);
    return action == CAMERA_SETTINGS_RETURN_REINIT ?
           CAMERA_SETTINGS_EXIT_REINIT : CAMERA_SETTINGS_EXIT_RESUME;
}

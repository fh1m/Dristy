#include "qr_settings.h"

#include <stdio.h>

#include "../../config/input_config.h"
#include "../../controllers/settings_menu_controller.h"
#include "../../core/hk_back_exit.h"
#include "../../core/hk_screen.h"
#include "../../services/camera_session.h"
#include "qr_settings_menu.h"

static settings_menu_session_t g_qr_settings;

void qr_settings_open(void)
{
    camera_service_freeze(1U);
    hk_screen_set(SCREEN_CAMERA_SETTINGS);
    hk_back_exit_set_armed(0U);
    (void)settings_menu_open(&g_qr_settings, qr_settings_menu_definition());
    printf("[SHELL] screen QR SETTINGS\r\n");
}

void qr_settings_close(void)
{
    settings_menu_close(&g_qr_settings);
}

void qr_settings_tick(const hk_input_snapshot_t *input)
{
    settings_menu_tick(&g_qr_settings, input);
}

uint8_t qr_settings_active(void)
{
    return settings_menu_active(&g_qr_settings);
}

uint8_t qr_settings_handle_input(const hk_input_snapshot_t *input)
{
    if(!input || settings_menu_handle_input(&g_qr_settings, input) !=
       SETTINGS_MENU_EVENT_CLOSE_REQUESTED)
        return 0U;
    settings_menu_close(&g_qr_settings);
    (void)camera_service_prepare_settings_return(1U,
                                                 (input->state & BUTTON_OK) ? 1U : 0U);
    return 1U;
}

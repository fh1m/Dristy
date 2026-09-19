#include "qr_camera_app.h"

#include <stdio.h>

#include "../../core/hk_menu.h"
#include "../../core/hk_screen.h"
#include "../../core/hk_string.h"
#include "../../services/debug_console_service.h"
#include "qr_camera_controller.h"
#include "qr_service.h"
#include "qr_camera_view.h"

const char g_qr_camera_debug_help[] = "HKQRINFO HKQR/HKQRCAM HKQRDECODE";

void qr_camera_enter(const hk_input_snapshot_t *input) { qr_camera_controller_enter(input); }
void qr_camera_exit(void) { qr_camera_controller_exit(); }
void qr_camera_tick(const hk_input_snapshot_t *input) { qr_camera_controller_tick(input); }
void qr_camera_handle_buttons(const hk_input_snapshot_t *input) { qr_camera_controller_handle_input(input); }

uint8_t qr_camera_owns_screen(screen_t screen)
{
    return screen == SCREEN_CAMERA_SETTINGS && qr_camera_controller_settings_active();
}

uint8_t qr_camera_handle_debug_command(const char *cmd)
{
    char line[640];

    if(str_eq_ci(cmd, "HKQRINFO"))
    {
        qr_service_format_info(line, sizeof(line),
                               qr_camera_controller_settings_active() ?
                               "QR-SETTINGS" : screen_label(hk_screen_get()));
        debug_console_write_text(line);
        return 1U;
    }
    if(str_eq_ci(cmd, "HKQRCAM") || str_eq_ci(cmd, "HKQR"))
    {
        activity_note();
        if(hk_screen_get() != SCREEN_MENU)
            shell_show_menu();
        qr_camera_controller_enter(NULL);
        return 1U;
    }
    if(str_eq_ci(cmd, "HKQRDECODE"))
    {
        activity_note();
        if(hk_screen_get() != SCREEN_QR_CAMERA)
            debug_console_write_text("HKQRDECODE ERR NOTQR\n");
        else
        {
            qr_service_decode_force();
            qr_service_format_info(line, sizeof(line), screen_label(hk_screen_get()));
            debug_console_write_text(line);
        }
        return 1U;
    }
    return 0U;
}

void qr_camera_draw_icon(uint16_t x, uint16_t y, uint16_t color, uint16_t bg)
{
    qr_camera_view_draw_icon(x, y, color, bg);
}

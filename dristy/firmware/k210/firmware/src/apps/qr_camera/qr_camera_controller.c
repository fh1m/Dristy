#include "qr_camera_controller.h"

#include <stdio.h>

#include "../../config/input_config.h"
#include "../../controllers/camera_runtime_controller.h"
#include "../../core/hk_screen.h"
#include "../../services/camera_session.h"
#include "../../ui/camera_view.h"
#include "qr_camera_frame_adapter.h"
#include "qr_result.h"
#include "qr_result_controller.h"
#include "qr_result_view.h"
#include "qr_service.h"
#include "qr_settings.h"

void qr_camera_controller_enter(const hk_input_snapshot_t *input)
{
    qr_service_enter();
    camera_runtime_enter(CAMERA_RUNTIME_QR, input);
}

void qr_camera_controller_exit(void)
{
    qr_settings_close();
    qr_result_close_window();
    qr_result_reset();
    camera_stop();
    camera_service_clear_mode();
}

void qr_camera_controller_tick(const hk_input_snapshot_t *input)
{
    if(qr_settings_active())
    {
        qr_settings_tick(input);
        return;
    }
    if(!qr_result_open() && camera_runtime_ok_hold_triggered(input))
    {
        camera_service_freeze(1U);
        qr_settings_open();
        return;
    }
    if(camera_runtime_tick(input) && !qr_result_open())
    {
        if(qr_service_decode_maybe(0U) == QR_DECODE_FOUND)
        {
            qr_result_show();
            camera_service_freeze(1U);
            qr_result_controller_render();
        }
    }
}

void qr_camera_controller_handle_input(const hk_input_snapshot_t *input)
{
    qr_result_input_result_t result_input;

    if(!input)
        return;
    if(qr_settings_active())
    {
        if(qr_settings_handle_input(input))
        {
            hk_screen_set(SCREEN_QR_CAMERA);
            camera_view_clear();
            printf("[SHELL] screen QR-CAMERA\r\n");
        }
        return;
    }

    result_input = qr_result_controller_handle_input(input->pressed);
    if(result_input == QR_RESULT_INPUT_CLOSE_REQUEST)
    {
        qr_result_close_window();
        qr_result_view_clear();
        qr_camera_frame_result_close((input->state & BUTTON_OK) ? 1U : 0U);
        printf("[QR] result close\r\n");
        return;
    }
    if(result_input == QR_RESULT_INPUT_HANDLED)
        return;
    (void)camera_runtime_handle_input(input);
}

uint8_t qr_camera_controller_settings_active(void)
{
    return qr_settings_active();
}

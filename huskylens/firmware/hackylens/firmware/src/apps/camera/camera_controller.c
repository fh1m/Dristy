#include "camera_controller.h"

#include <stdio.h>

#include "../../controllers/camera_runtime_controller.h"
#include "../../core/hk_screen.h"
#include "../../services/camera_session.h"
#include "../../ui/camera_view.h"
#include "camera_photo_controller.h"
#include "camera_settings.h"

void camera_controller_enter(const hk_input_snapshot_t *input)
{
    camera_runtime_enter(CAMERA_RUNTIME_PHOTO, input);
}

void camera_controller_exit(void)
{
    camera_settings_close();
    camera_stop();
    camera_service_clear_mode();
}

void camera_controller_tick(const hk_input_snapshot_t *input)
{
    if(camera_settings_active())
    {
        camera_settings_tick(input);
        return;
    }
    if(camera_runtime_ok_hold_triggered(input))
    {
        camera_service_freeze(1U);
        camera_settings_open();
        return;
    }
    camera_runtime_tick(input);
}

void camera_controller_handle_input(const hk_input_snapshot_t *input)
{
    camera_settings_exit_t settings_exit;

    if(camera_settings_active())
    {
        settings_exit = camera_settings_handle_input(input);
        if(settings_exit == CAMERA_SETTINGS_EXIT_REINIT)
        {
            hk_input_snapshot_t restart = {input ? input->state : 0U, 0U, 0U};

            camera_stop();
            camera_runtime_enter(CAMERA_RUNTIME_PHOTO, &restart);
        }
        else if(settings_exit == CAMERA_SETTINGS_EXIT_RESUME)
        {
            hk_screen_set(SCREEN_CAMERA);
            camera_view_clear();
            printf("[SHELL] screen CAMERA\r\n");
        }
        return;
    }

    if(camera_runtime_handle_input(input) == CAMERA_RUNTIME_INPUT_OK_RELEASE)
        camera_photo_controller_take(camera_runtime_redraw_preview);
}

uint8_t camera_controller_settings_active(void)
{
    return camera_settings_active();
}

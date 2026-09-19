#include "camera_runtime_controller.h"

#include <stdio.h>

#include "../config/camera_layout.h"
#include "../config/input_config.h"

#include "../core/hk_back_exit.h"
#include "../core/hk_menu.h"
#include "../core/hk_screen.h"
#include "hal_time.h"
#include "../services/camera_frame.h"
#include "../services/camera_input.h"
#include "../services/camera_light.h"
#include "../services/camera_session.h"
#include "../services/camera_session_preferences.h"
#include "../services/camera_status.h"
#include "../services/settings_lights.h"
#include "../ui/camera_status_view.h"
#include "../ui/camera_view.h"
#include "../ui/hk_ui.h"
#include "../apps/vision_mode/vision_mode_shell.h"

static uint32_t g_camera_light_repeat_button;
static uint64_t g_camera_light_repeat_deadline_us;

static void camera_light_repeat_reset(void)
{
    g_camera_light_repeat_button = 0;
    g_camera_light_repeat_deadline_us = 0;
}

static void camera_light_repeat_start(uint32_t button, int8_t delta)
{
    g_camera_light_repeat_button = button;
    g_camera_light_repeat_deadline_us = hal_time_us() + CAMERA_LIGHT_REPEAT_INITIAL_US;
    camera_light_adjust(delta);
}

static void camera_light_repeat_tick(uint32_t button_state)
{
    uint64_t now;
    int8_t delta;

    if(g_camera_light_repeat_button == 0)
        return;
    if(!(button_state & g_camera_light_repeat_button))
    {
        camera_light_repeat_reset();
        return;
    }

    now = hal_time_us();
    if(now < g_camera_light_repeat_deadline_us)
        return;

    delta = g_camera_light_repeat_button == BUTTON_RIGHT ? 1 : -1;
    camera_light_adjust(delta);
    g_camera_light_repeat_deadline_us = now + CAMERA_LIGHT_REPEAT_NEXT_US;
}

static uint8_t camera_runtime_present_preview(camera_runtime_frame_consumer_t consumer,
                                              void *consumer_context,
                                              camera_runtime_frame_overlay_t overlay,
                                              void *overlay_context)
{
    camera_preview_frame_t service_frame;
    camera_view_frame_t view_frame;
    camera_view_present_t present = {0};
    uint64_t compose_started_us;
    uint64_t compose_finished_us;
    uint64_t present_finished_us;

    if(!camera_service_preview_acquire(&service_frame))
        return 0;
    if(consumer)
        consumer(service_frame.pixels, service_frame.width, service_frame.height,
                 service_frame.sequence, consumer_context);
    view_frame.pixels = service_frame.pixels;
    view_frame.width = service_frame.width;
    view_frame.height = service_frame.height;
    view_frame.rotate = service_frame.rotate;
    view_frame.fps_overlay = camera_session_preferences_fps_enabled();
    view_frame.light_overlay = camera_service_light_overlay_enabled();
    camera_service_format_fps_overlay(view_frame.fps_text, sizeof(view_frame.fps_text));
    camera_service_format_light_overlay(view_frame.light_text, sizeof(view_frame.light_text));
    compose_started_us = hal_time_us();
    if(!camera_view_compose_frame(&view_frame, &present))
    {
        camera_service_preview_release(&service_frame);
        camera_view_discard(&present);
        return 0;
    }
    compose_finished_us = hal_time_us();
    if(overlay)
        overlay(&present, service_frame.width, service_frame.height,
                service_frame.sequence, overlay_context);
    camera_service_preview_release(&service_frame);
    if(!camera_view_present(&present))
        return 0;
    present_finished_us = hal_time_us();
    camera_service_fps_note_present((uint32_t)(compose_finished_us - compose_started_us),
                                    (uint32_t)(present_finished_us - compose_finished_us));
    return 1;
}

void camera_runtime_redraw_preview(void)
{
    (void)camera_runtime_present_preview(NULL, NULL, NULL, NULL);
}

void camera_runtime_enter(camera_runtime_mode_t mode, const hk_input_snapshot_t *input)
{
    uint32_t button_state = input ? input->state : 0;
    uint8_t qr_mode = mode == CAMERA_RUNTIME_QR ? 1 : 0;
    uint8_t face_mode = mode == CAMERA_RUNTIME_FACE_DETECT ? 1 : 0;
    uint8_t apriltag_mode = mode == CAMERA_RUNTIME_APRILTAG ? 1 : 0;
    uint8_t object_mode = mode == CAMERA_RUNTIME_OBJECT_DETECT ? 1 : 0;
    uint8_t classical_mode = mode == CAMERA_RUNTIME_CLASSICAL ? 1 : 0;
    const char *init_title = qr_mode ? "QR INIT" :
        (face_mode ? "FACE INIT" : (apriltag_mode ? "TAG INIT" :
         (object_mode ? "OBJECT INIT" :
          (classical_mode ? "VISION INIT" : "CAMERA INIT"))));
    const char *fail_title = qr_mode ? "QR FAIL" :
        (face_mode ? "FACE FAIL" : (apriltag_mode ? "TAG FAIL" :
         (object_mode ? "OBJECT FAIL" :
          (classical_mode ? "VISION FAIL" : "CAMERA FAIL"))));

    hk_screen_set(qr_mode ? SCREEN_QR_CAMERA :
                  (face_mode ? SCREEN_FACE_DETECT :
                   (apriltag_mode ? SCREEN_APRILTAG :
                    (object_mode ? SCREEN_OBJECT_DETECT : SCREEN_CAMERA))));
    if(vision_mode_shell_active())
        hk_screen_set(SCREEN_VISION_MODE);
    camera_service_set_qvga_mode(
        face_mode || apriltag_mode || object_mode || classical_mode);
    hk_back_exit_set_armed(0);
    camera_light_repeat_reset();
    camera_service_enter_begin(qr_mode, (button_state & BUTTON_OK) ? 1 : 0);
    camera_status_view_draw(init_title, "OV2640 PROBE");
    printf("[SHELL] screen %s\r\n", screen_label(hk_screen_get()));

    if(!camera_service_start())
    {
        camera_status_view_draw(fail_title, camera_service_fail_reason());
        return;
    }

    camera_view_clear();
}

uint8_t camera_runtime_tick(const hk_input_snapshot_t *input)
{
    return camera_runtime_tick_with_pipeline(input, NULL, NULL, NULL, NULL);
}

uint8_t camera_runtime_tick_with_consumer(const hk_input_snapshot_t *input,
                                          camera_runtime_frame_consumer_t consumer,
                                          void *context)
{
    return camera_runtime_tick_with_pipeline(input, consumer, context, NULL, NULL);
}

uint8_t camera_runtime_tick_with_pipeline(const hk_input_snapshot_t *input,
                                          camera_runtime_frame_consumer_t consumer,
                                          void *consumer_context,
                                          camera_runtime_frame_overlay_t overlay,
                                          void *overlay_context)
{
    if(!input)
        return 0;

    camera_light_repeat_tick(input->state);
    if(!camera_service_capture_ready())
        return 0;

    if(camera_service_capture_frame_tick())
    {
        if(camera_runtime_present_preview(consumer, consumer_context, overlay, overlay_context))
        {
            camera_service_fps_on_frame();
            return 1;
        }
    }

    if(camera_service_consume_frame_timeout())
        camera_status_view_draw("CAMERA FAIL", "NO FRAME");
    return 0;
}

uint8_t camera_runtime_ok_hold_triggered(const hk_input_snapshot_t *input)
{
    return input && (input->state & BUTTON_OK) && camera_input_hold_triggered(hal_time_us());
}

camera_runtime_input_event_t camera_runtime_handle_input(const hk_input_snapshot_t *input)
{
    if(!input)
        return CAMERA_RUNTIME_INPUT_NONE;

    if(input->pressed & BUTTON_BACK)
    {
        camera_light_repeat_reset();
        camera_input_cancel();
        shell_show_menu();
        return CAMERA_RUNTIME_INPUT_EXIT;
    }

    if(camera_input_ignore_update((input->state & BUTTON_OK) ? 1 : 0))
        return CAMERA_RUNTIME_INPUT_NONE;

    if(input->pressed & BUTTON_OK)
    {
        camera_input_press(hal_time_us());
        return CAMERA_RUNTIME_INPUT_NONE;
    }

    if(input->pressed & BUTTON_LEFT)
    {
        camera_light_repeat_start(BUTTON_LEFT, -1);
        return CAMERA_RUNTIME_INPUT_NONE;
    }

    if(input->pressed & BUTTON_RIGHT)
    {
        camera_light_repeat_start(BUTTON_RIGHT, 1);
        return CAMERA_RUNTIME_INPUT_NONE;
    }

    if((input->changed & BUTTON_OK) && !(input->state & BUTTON_OK))
    {
        if(camera_input_release_was_short())
            return CAMERA_RUNTIME_INPUT_OK_RELEASE;
    }

    return CAMERA_RUNTIME_INPUT_NONE;
}

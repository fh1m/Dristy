#include "vision_mode_controller.h"

#include <stddef.h>

#include "../../config/input_config.h"
#include "../../controllers/camera_runtime_controller.h"
#include "../../services/camera_session.h"
#include "../../ui/camera_view.h"
#include "../../core/hk_menu.h"
#include "../../core/hk_screen.h"
#include "../../dristy/dristy_mode_readiness.h"
#include "../../dristy/dristy_modes.h"
#include "../../dristy/dristy_pipeline.h"
#include "../../dristy/dristy_result_bus.h"
#include "vision_mode_bridge.h"
#include "vision_mode_shell.h"
#include "vision_mode_view.h"

static dristy_mode_t s_pending_mode = DRISTY_MODE_DETECT_TRACK;
static dristy_mode_t s_active_mode = DRISTY_MODE_INVALID;
static uint8_t s_stub;
static uint8_t s_preview_only;

static void classical_consume_frame(const volatile uint16_t *pixels,
                                    uint16_t width,
                                    uint16_t height,
                                    uint32_t sequence,
                                    void *context)
{
    (void)sequence;
    (void)context;
    (void)dristy_pipeline_ingest_rgb565(pixels, width, height);
}

static void classical_compose_overlay(camera_view_present_t *present,
                                      uint16_t width,
                                      uint16_t height,
                                      uint32_t sequence,
                                      void *context)
{
    const dristy_result_snapshot_t *snap;

    (void)sequence;
    (void)context;
    snap = dristy_result_bus_read();
    if(!snap || snap->mode != s_active_mode)
        return;
    vision_mode_view_compose_overlays(present, width, height, snap);
}

static uint8_t mode_uses_classical_lcd(dristy_mode_t mode)
{
    const dristy_mode_info_t *info = dristy_mode_info(mode);

    if(!info || !(info->capabilities & DRISTY_CAP_CLASSICAL))
        return 0U;
    if(info->capabilities & DRISTY_CAP_KPU)
        return 0U;
    if(mode == DRISTY_MODE_APRILTAG || mode == DRISTY_MODE_QR_CODE ||
       mode == DRISTY_MODE_LANDING_TARGET)
        return 0U;
    return 1U;
}

void vision_mode_controller_set_pending_mode(dristy_mode_t mode)
{
    s_pending_mode = mode;
}

uint8_t vision_mode_controller_open(const hk_input_snapshot_t *input)
{
    vision_mode_controller_enter(input);
    return 1U;
}

void vision_mode_controller_enter(const hk_input_snapshot_t *input)
{
    s_active_mode = s_pending_mode;
    s_stub = 0U;
    s_preview_only = mode_uses_classical_lcd(s_active_mode);

    (void)dristy_pipeline_set_mode(s_active_mode);
    hk_screen_set(SCREEN_VISION_MODE);

    if(s_stub)
    {
        vision_mode_view_draw_stub(s_active_mode);
        return;
    }

    if(s_preview_only)
    {
        vision_mode_shell_set_active(1U);
        camera_runtime_enter(CAMERA_RUNTIME_CLASSICAL, input);
        hk_screen_set(SCREEN_VISION_MODE);
        return;
    }

    vision_mode_bridge_start(s_active_mode);
    hk_screen_set(SCREEN_VISION_MODE);
}

void vision_mode_controller_exit(void)
{
    if(!s_stub)
    {
        if(s_preview_only)
        {
            vision_mode_shell_set_active(0U);
            camera_stop();
            camera_service_clear_mode();
        }
        else
            vision_mode_bridge_stop();
    }
    s_active_mode = DRISTY_MODE_INVALID;
    s_stub = 0U;
    s_preview_only = 0U;
}

void vision_mode_controller_tick(const hk_input_snapshot_t *input)
{
    if(s_stub)
        return;

    if(s_preview_only)
        (void)camera_runtime_tick_with_pipeline(input, classical_consume_frame, NULL,
                                                classical_compose_overlay, NULL);
    else
        vision_mode_bridge_tick(input);
}

void vision_mode_controller_handle_buttons(const hk_input_snapshot_t *input)
{
    if(s_stub)
    {
        if(input && (input->pressed & BUTTON_BACK))
            shell_show_menu();
        return;
    }

    if(s_preview_only)
    {
        if(camera_runtime_handle_input(input) == CAMERA_RUNTIME_INPUT_EXIT)
            shell_show_menu();
        return;
    }

    vision_mode_bridge_handle_buttons(input);
    if(input && (input->pressed & BUTTON_BACK))
        shell_show_menu();
}

dristy_mode_t vision_mode_controller_active_mode(void)
{
    return s_active_mode;
}

uint8_t vision_mode_controller_apply_host_mode(dristy_mode_t mode)
{
    dristy_mode_t active = s_active_mode;

    if(active == mode)
        return 1U;
    if(hk_screen_get() != SCREEN_VISION_MODE)
        return 0U;
    if(vision_mode_bridge_same_route(active, mode))
    {
        s_pending_mode = mode;
        s_active_mode = mode;
        (void)dristy_pipeline_set_mode(mode);
        return 1U;
    }
    if(s_preview_only && mode_uses_classical_lcd(mode))
    {
        s_pending_mode = mode;
        s_active_mode = mode;
        (void)dristy_pipeline_set_mode(mode);
        return 1U;
    }
    return 0U;
}

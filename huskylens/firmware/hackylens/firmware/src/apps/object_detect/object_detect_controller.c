#include "object_detect_controller.h"

#include <string.h>

#include "../../config/input_config.h"
#include "../../controllers/camera_runtime_controller.h"
#include "../../controllers/settings_menu_controller.h"
#include "../../core/hk_menu.h"
#include "../../core/hk_screen.h"
#include "../vision_mode/vision_mode_shell.h"
#include "../../dristy/dristy_pipeline.h"
#include "../../dristy/dristy_modes.h"
#include "../vision_mode/vision_mode_controller.h"
#if HK_ENABLE_APP_APRILTAG
#include "../apriltag/apriltag_detector.h"
#endif
#include "../../services/camera_frame.h"
#include "../../services/camera_session.h"
#include "../../services/camera_session_preferences.h"
#include "../../services/vision_result_service.h"
#include "../../ui/camera_status_view.h"
#include "../../ui/camera_view.h"
#include "object_detect_detector.h"
#include "object_detect_settings.h"
#include "object_detect_settings_menu.h"
#include "object_detect_view.h"

static uint8_t g_error;
static uint8_t g_loading;
static object_detect_result_t g_display_results[OBJECT_DETECT_RESULT_MAX];
static uint8_t g_display_count;
static uint8_t g_display_misses;
static uint32_t g_display_sequence;
static settings_menu_session_t g_settings_menu;

static void object_detect_start(const hk_input_snapshot_t *input)
{
    object_detect_load_result_t result = object_detect_detector_load();

    if(result == OBJECT_DETECT_LOAD_BUSY)
    {
        if(!g_loading)
            camera_status_view_draw("OBJECT WAIT", "AI BUSY");
        g_loading = 1U;
        g_error = 0U;
        return;
    }
    g_loading = 0U;
    g_error = result != OBJECT_DETECT_LOAD_OK;
    if(g_error)
    {
        vision_result_clear(VISION_SOURCE_OBJECT_DETECT);
        camera_status_view_draw("OBJECT ERROR",
                                object_detect_detector_error_label(result));
        return;
    }
    camera_runtime_enter(CAMERA_RUNTIME_OBJECT_DETECT, input);
    object_detect_detector_attach_camera();
}

static void object_display_reset(void)
{
    g_display_count = 0U;
    g_display_misses = 0U;
    g_display_sequence = 0U;
    memset(g_display_results, 0, sizeof(g_display_results));
}

static void object_display_refresh(void)
{
    uint32_t sequence = object_detect_detector_result_sequence();
    const object_detect_result_t *results;
    uint8_t count;

    if(sequence == 0U || sequence == g_display_sequence)
        return;
    results = object_detect_detector_results(&count);
    g_display_sequence = sequence;
    if(count)
    {
        if(count > OBJECT_DETECT_RESULT_MAX)
            count = OBJECT_DETECT_RESULT_MAX;
        memcpy(g_display_results, results,
               (size_t)count * sizeof(g_display_results[0]));
        g_display_count = count;
        g_display_misses = 0U;
        return;
    }
    if(g_display_count &&
       g_display_misses < OBJECT_DETECT_DISPLAY_HOLD_MISSES)
    {
        g_display_misses++;
        return;
    }
    g_display_count = 0U;
    g_display_misses = 0U;
}

static volatile uint32_t g_pending_camera_sequence;

static void object_consume_frame(const volatile uint16_t *pixels,
                                 uint16_t width,
                                 uint16_t height,
                                 uint32_t sequence,
                                 void *context)
{
    dristy_mode_t mode;

    (void)context;
    g_pending_camera_sequence = sequence;
    mode = vision_mode_controller_active_mode();
    if(mode == DRISTY_MODE_DETECT_MOTION || mode == DRISTY_MODE_DETECT_ARUCO ||
       mode == DRISTY_MODE_DETECT_TAG)
        (void)dristy_pipeline_ingest_rgb565(pixels, width, height);
#if HK_ENABLE_APP_APRILTAG
    if(mode == DRISTY_MODE_DETECT_TAG)
    {
        (void)apriltag_detector_submit(pixels, width, height);
        apriltag_detector_service_tick();
    }
#endif
}

static void object_compose_overlay(camera_view_present_t *present,
                                   uint16_t width,
                                   uint16_t height,
                                   uint32_t sequence,
                                   void *context)
{
    (void)context;
    object_display_refresh();
    object_detect_view_compose_results(present, width, height,
                                       g_display_results, g_display_count);
    object_detect_detector_note_present(sequence);
}

void object_detect_controller_background_tick(void)
{
    uint32_t sequence;

    object_detect_detector_service_tick();
    sequence = g_pending_camera_sequence;
    if(sequence != 0U)
    {
        g_pending_camera_sequence = 0U;
        object_detect_detector_process_frame(sequence);
    }
}

void object_detect_controller_enter(const hk_input_snapshot_t *input)
{
    object_display_reset();
    settings_menu_close(&g_settings_menu);
    object_detect_settings_load();
    object_detect_settings_apply_session();
    g_error = 0U;
    g_loading = 0U;
    if(!vision_mode_shell_active())
        hk_screen_set(SCREEN_OBJECT_DETECT);
    camera_status_view_draw("OBJECT LOAD", "VOC20 MODEL");
    object_detect_start(input);
}

void object_detect_controller_exit(void)
{
    settings_menu_close(&g_settings_menu);
    object_display_reset();
    vision_result_clear(VISION_SOURCE_OBJECT_DETECT);
    camera_stop();
    camera_service_clear_mode();
    object_detect_detector_unload();
    camera_session_preferences_clear();
    g_loading = 0U;
}

void object_detect_controller_tick(const hk_input_snapshot_t *input)
{
    const object_detect_result_t *results;
    vision_result_item_t items[OBJECT_DETECT_RESULT_MAX];
    uint16_t width;
    uint16_t height;
    uint8_t count;

    if(g_loading)
    {
        object_detect_start(input);
        return;
    }
    if(settings_menu_active(&g_settings_menu))
    {
        settings_menu_tick(&g_settings_menu, input);
        return;
    }
    if(camera_runtime_ok_hold_triggered(input))
    {
        object_display_reset();
        object_detect_detector_invalidate_results();
        vision_result_clear(VISION_SOURCE_OBJECT_DETECT);
        camera_service_freeze(1U);
        object_detect_detector_pause_capture();
        (void)settings_menu_open(
            &g_settings_menu, object_detect_settings_menu_definition());
        return;
    }
    if(g_error)
        return;
    if(!camera_runtime_tick_with_pipeline(input,
                                          object_consume_frame, NULL,
                                          object_compose_overlay, NULL))
        return;
    if(!object_detect_detector_ready())
    {
        g_error = 1U;
        object_display_reset();
        vision_result_clear(VISION_SOURCE_OBJECT_DETECT);
        camera_stop();
        camera_service_clear_mode();
        object_detect_detector_unload();
        camera_status_view_draw(
            "OBJECT ERROR",
            object_detect_detector_error_label(
                object_detect_detector_result()));
        return;
    }

    results = object_detect_detector_results(&count);
    camera_service_frame_info(&width, &height);
    for(uint8_t index = 0U; index < count; index++)
    {
        items[index].kind = VISION_ITEM_BLOCK;
        items[index].flags = 0U;
        items[index].id = results[index].class_id;
        items[index].x0 = (uint16_t)results[index].x;
        items[index].y0 = (uint16_t)results[index].y;
        items[index].x1 = (uint16_t)(results[index].x + results[index].w);
        items[index].y1 = (uint16_t)(results[index].y + results[index].h);
        items[index].confidence = results[index].confidence;
        items[index].reserved = 0U;
    }
    vision_result_publish(VISION_SOURCE_OBJECT_DETECT,
                          width, height, items, count);
}

void object_detect_controller_handle_buttons(
    const hk_input_snapshot_t *input)
{
    if(settings_menu_active(&g_settings_menu))
    {
        if(settings_menu_handle_input(&g_settings_menu, input) ==
           SETTINGS_MENU_EVENT_CLOSE_REQUESTED)
        {
            settings_menu_close(&g_settings_menu);
            object_display_reset();
            object_detect_detector_invalidate_results();
            camera_service_resume_from_settings(
                input && (input->state & BUTTON_OK));
            object_detect_detector_resume_capture();
            camera_view_clear();
        }
        return;
    }
    if(g_error || g_loading)
    {
        if(input && (input->pressed & BUTTON_BACK))
            shell_show_menu();
        return;
    }
    (void)camera_runtime_handle_input(input);
}

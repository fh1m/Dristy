#include "apriltag_controller.h"

#include <string.h>

#include "../../config/input_config.h"
#include "../../controllers/camera_runtime_controller.h"
#include "../../controllers/settings_menu_controller.h"
#include "../../core/hk_menu.h"
#include "../../core/hk_screen.h"
#include "../vision_mode/vision_mode_shell.h"
#include "../../services/camera_frame.h"
#include "../../services/camera_session.h"
#include "../../services/camera_session_preferences.h"
#include "../../services/vision_result_service.h"
#include "../../ui/camera_status_view.h"
#include "../../ui/camera_view.h"
#include "apriltag_detector.h"
#include "apriltag_config.h"
#include "apriltag_settings.h"
#include "apriltag_settings_menu.h"
#include "apriltag_view.h"

static uint8_t g_error;
static apriltag_result_t g_display_results[APRILTAG_RESULT_MAX];
static uint8_t g_display_count;
static uint8_t g_display_misses;
static uint32_t g_display_sequence;
static int16_t g_target_id = -1;
static settings_menu_session_t g_settings_menu;

static void apriltag_display_reset(void)
{
    g_display_count = 0U;
    g_display_misses = 0U;
    g_display_sequence = 0U;
    g_target_id = -1;
}

static void apriltag_update_target(uint16_t width, uint16_t height)
{
    int32_t center_x = width / 2U;
    int32_t center_y = height / 2U;
    uint32_t best_distance = 0xFFFFFFFFUL;

    g_target_id = -1;
    for(uint8_t i = 0U; i < g_display_count; i++)
    {
        const apriltag_result_t *result = &g_display_results[i];
        int32_t dx;
        int32_t dy;
        uint32_t distance;

        if(center_x < result->x || center_y < result->y ||
           center_x > result->x + result->w || center_y > result->y + result->h)
            continue;
        dx = result->center_x - center_x;
        dy = result->center_y - center_y;
        distance = (uint32_t)(dx * dx + dy * dy);
        if(distance < best_distance)
        {
            best_distance = distance;
            g_target_id = (int16_t)result->id;
        }
    }
}

static void apriltag_display_refresh(void)
{
    uint32_t sequence = apriltag_detector_result_sequence();
    const apriltag_result_t *results;
    uint8_t count;

    if(sequence == 0U || sequence == g_display_sequence)
        return;
    results = apriltag_detector_results(&count);
    g_display_sequence = sequence;
    if(count)
    {
        if(count > APRILTAG_RESULT_MAX)
            count = APRILTAG_RESULT_MAX;
        memcpy(g_display_results, results, (size_t)count * sizeof(g_display_results[0]));
        g_display_count = count;
        g_display_misses = 0U;
        return;
    }
    if(g_display_count && g_display_misses < APRILTAG_DISPLAY_HOLD_MISSES)
    {
        g_display_misses++;
        return;
    }
    g_display_count = 0U;
    g_display_misses = 0U;
}

static void apriltag_consume_frame(const volatile uint16_t *pixels,
                                   uint16_t width,
                                   uint16_t height,
                                   uint32_t sequence,
                                   void *context)
{
    (void)context;
    (void)sequence;
    (void)apriltag_detector_submit(pixels, width, height);
}

static void apriltag_compose_overlay(camera_view_present_t *present,
                                     uint16_t width,
                                     uint16_t height,
                                     uint32_t sequence,
                                     void *context)
{
    uint8_t selected[APRILTAG_RESULT_MAX] = {0};

    (void)context;
    (void)sequence;
    apriltag_display_refresh();
    apriltag_update_target(width, height);
    for(uint8_t i = 0U; i < g_display_count; i++)
        selected[i] = apriltag_settings_selected(g_display_results[i].id);
    apriltag_view_compose_results(present, width, height,
                                  g_display_results, selected, g_display_count);
}

void apriltag_controller_enter(const hk_input_snapshot_t *input)
{
    apriltag_display_reset();
    settings_menu_close(&g_settings_menu);
    apriltag_settings_load();
    apriltag_settings_apply_session();
    g_error = !apriltag_detector_init();
    if(g_error)
    {
        vision_result_clear(VISION_SOURCE_APRILTAG);
        if(!vision_mode_shell_active())
            hk_screen_set(SCREEN_APRILTAG);
        camera_status_view_draw("TAG ERROR", "NO MEMORY");
        return;
    }
    camera_runtime_enter(CAMERA_RUNTIME_APRILTAG, input);
}

void apriltag_controller_exit(void)
{
    settings_menu_close(&g_settings_menu);
    apriltag_display_reset();
    vision_result_clear(VISION_SOURCE_APRILTAG);
    camera_stop();
    apriltag_detector_deinit();
    camera_session_preferences_clear();
}

void apriltag_controller_tick(const hk_input_snapshot_t *input)
{
    const apriltag_result_t *results;
    vision_result_item_t items[APRILTAG_RESULT_MAX];
    uint16_t width;
    uint16_t height;
    uint8_t count;
    uint8_t published_count = 0U;

    if(settings_menu_active(&g_settings_menu))
    {
        settings_menu_tick(&g_settings_menu, input);
        return;
    }
    if(camera_runtime_ok_hold_triggered(input))
    {
        apriltag_display_reset();
        vision_result_clear(VISION_SOURCE_APRILTAG);
        camera_service_freeze(1U);
        (void)settings_menu_open(&g_settings_menu, apriltag_settings_menu_definition());
        return;
    }

    if(g_error || !camera_runtime_tick_with_pipeline(input,
                                                     apriltag_consume_frame, NULL,
                                                     apriltag_compose_overlay, NULL))
        return;
    apriltag_display_refresh();
    results = apriltag_detector_results(&count);
    camera_service_frame_info(&width, &height);
    for(uint8_t i = 0; i < count; i++)
    {
        if(apriltag_settings_preferences()->output_mode == APRILTAG_OUTPUT_SELECTED &&
           !apriltag_settings_selected(results[i].id))
            continue;
        items[published_count].kind = VISION_ITEM_BLOCK;
        items[published_count].flags = results[i].hamming;
        items[published_count].id = results[i].id;
        items[published_count].x0 = (uint16_t)results[i].x;
        items[published_count].y0 = (uint16_t)results[i].y;
        items[published_count].x1 = (uint16_t)(results[i].x + results[i].w);
        items[published_count].y1 = (uint16_t)(results[i].y + results[i].h);
        items[published_count].confidence = results[i].confidence;
        items[published_count].reserved = 0U;
        published_count++;
    }
    vision_result_publish(VISION_SOURCE_APRILTAG, width, height, items, published_count);
}

void apriltag_controller_handle_buttons(const hk_input_snapshot_t *input)
{
    camera_runtime_input_event_t event;

    if(settings_menu_active(&g_settings_menu))
    {
        if(settings_menu_handle_input(&g_settings_menu, input) == SETTINGS_MENU_EVENT_CLOSE_REQUESTED)
        {
            settings_menu_close(&g_settings_menu);
            g_display_sequence = apriltag_detector_result_sequence();
            g_display_count = 0U;
            g_display_misses = 0U;
            g_target_id = -1;
            camera_service_resume_from_settings(input && (input->state & BUTTON_OK));
            camera_view_clear();
        }
        return;
    }
    if(g_error)
    {
        if(input && (input->pressed & BUTTON_BACK))
            shell_show_menu();
        return;
    }
    event = camera_runtime_handle_input(input);
    if(event == CAMERA_RUNTIME_INPUT_OK_RELEASE && g_target_id >= 0)
        (void)apriltag_settings_toggle_selected((uint16_t)g_target_id);
}

int16_t apriltag_controller_target_id(void)
{
    return g_target_id;
}

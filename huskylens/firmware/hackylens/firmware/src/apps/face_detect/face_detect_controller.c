#include "face_detect_controller.h"

#include <stdio.h>
#include <string.h>

#include "../../config/input_config.h"
#include "../../controllers/camera_runtime_controller.h"
#include "../../core/hk_menu.h"
#include "../../core/hk_screen.h"
#include "../vision_mode/vision_mode_shell.h"
#include "../../services/camera_frame.h"
#include "../../services/camera_session.h"
#include "../../services/vision_result_service.h"
#include "../../ui/camera_status_view.h"
#include "face_detect_detector.h"
#include "face_detect_view.h"

static uint8_t g_error;
static uint8_t g_loading;
static face_detect_box_t g_overlay_boxes[FACE_DETECT_BOX_MAX];
static uint8_t g_overlay_count;

static void face_compose_overlay(camera_view_present_t *present,
                                 uint16_t width,
                                 uint16_t height,
                                 uint32_t sequence,
                                 void *context)
{
    (void)sequence;
    (void)context;
    face_detect_view_compose_boxes(present, width, height,
                                   g_overlay_boxes, g_overlay_count);
}

static void face_detect_start(const hk_input_snapshot_t *input)
{
    face_detect_load_result_t result = face_detect_detector_load();

    if(result == FACE_DETECT_LOAD_BUSY)
    {
        if(!g_loading)
            camera_status_view_draw("FACE WAIT", "AI BUSY");
        g_loading = 1U;
        g_error = 0U;
        return;
    }
    g_loading = 0U;
    g_error = result != FACE_DETECT_LOAD_OK;
    if(g_error)
    {
        vision_result_clear(VISION_SOURCE_FACE);
        if(!vision_mode_shell_active())
            hk_screen_set(SCREEN_FACE_DETECT);
        camera_status_view_draw("FACE ERROR",
                                face_detect_detector_error_label(result));
        printf("[FACE] load %s\r\n",
               face_detect_detector_error_label(result));
        return;
    }
    camera_runtime_enter(CAMERA_RUNTIME_FACE_DETECT, input);
    face_detect_detector_attach_camera();
}

void face_detect_controller_enter(const hk_input_snapshot_t *input)
{
    g_error = 0U;
    g_loading = 0U;
    if(!vision_mode_shell_active())
        hk_screen_set(SCREEN_FACE_DETECT);
    camera_status_view_draw("FACE LOAD", "DETECT MODEL");
    face_detect_start(input);
}

void face_detect_controller_exit(void)
{
    vision_result_clear(VISION_SOURCE_FACE);
    camera_stop();
    camera_service_clear_mode();
    face_detect_detector_unload();
    g_loading = 0U;
}

void face_detect_controller_tick(const hk_input_snapshot_t *input)
{
    const face_detect_box_t *boxes;
    uint16_t width;
    uint16_t height;
    uint8_t count;
    vision_result_item_t items[FACE_DETECT_BOX_MAX];

    if(g_loading)
    {
        face_detect_start(input);
        return;
    }
    if(g_error || !camera_runtime_tick_with_pipeline(input, NULL, NULL,
                                                     face_compose_overlay, NULL))
        return;
    if(!face_detect_detector_ready())
    {
        g_error = 1U;
        vision_result_clear(VISION_SOURCE_FACE);
        camera_stop();
        camera_service_clear_mode();
        face_detect_detector_unload();
        camera_status_view_draw(
            "FACE ERROR",
            face_detect_detector_error_label(face_detect_detector_result()));
        return;
    }
    boxes = face_detect_detector_boxes(&count);
    if(count > FACE_DETECT_BOX_MAX)
        count = FACE_DETECT_BOX_MAX;
    memcpy(g_overlay_boxes, boxes, (size_t)count * sizeof(g_overlay_boxes[0]));
    g_overlay_count = count;
    camera_service_frame_info(&width, &height);
    for(uint8_t i = 0U; i < count; i++)
    {
        items[i].kind = VISION_ITEM_BLOCK;
        items[i].flags = 0U;
        items[i].id = i;
        items[i].x0 = (uint16_t)boxes[i].x;
        items[i].y0 = (uint16_t)boxes[i].y;
        items[i].x1 = (uint16_t)(boxes[i].x + boxes[i].w);
        items[i].y1 = (uint16_t)(boxes[i].y + boxes[i].h);
        items[i].confidence = 1000U;
        items[i].reserved = 0U;
    }
    vision_result_publish(VISION_SOURCE_FACE, width, height, items, count);
}

void face_detect_controller_handle_buttons(const hk_input_snapshot_t *input)
{
    if(g_error || g_loading)
    {
        if(input && (input->pressed & BUTTON_BACK))
            shell_show_menu();
        return;
    }
    (void)camera_runtime_handle_input(input);
}

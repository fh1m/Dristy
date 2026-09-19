#include "apriltag_view.h"

#include <stdio.h>
#include <string.h>

#include "../../config/display_config.h"
#include "../../ui/display_binding.h"
#include "../../ui/camera_view.h"

#define APRILTAG_SELECTED_COLOR 0xFFE0U

void apriltag_view_compose_results(camera_view_present_t *present,
                                   uint16_t width,
                                   uint16_t height,
                                   const apriltag_result_t *results,
                                   const uint8_t *selected,
                                   uint8_t count)
{
    camera_view_frame_t view = {0};
    camera_view_rect_t rects[APRILTAG_RESULT_MAX];

    if(!present || !results)
        return;
    if(count > APRILTAG_RESULT_MAX)
        count = APRILTAG_RESULT_MAX;
    for(uint8_t i = 0; i < count; i++)
    {
        rects[i].x = results[i].x;
        rects[i].y = results[i].y;
        rects[i].w = results[i].w;
        rects[i].h = results[i].h;
    }
    view.width = width;
    view.height = height;
    for(uint8_t i = 0U; i < count; i++)
        camera_view_compose_rects(present, &view, &rects[i], 1U,
                                  selected && selected[i] ? APRILTAG_SELECTED_COLOR : COLOR_TERM_GREEN);

    for(uint8_t i = 0; i < count; i++)
    {
        char label[16];
        uint16_t label_x;
        uint16_t label_y;
        uint16_t label_width;

        snprintf(label, sizeof(label), "%u", (unsigned)results[i].id);
        label_width = (uint16_t)(strlen(label) * HACKYLENS_FONT_W);
        label_x = results[i].center_x > (int16_t)(label_width / 2U) ?
            (uint16_t)(results[i].center_x - label_width / 2U) : 0U;
        label_y = results[i].center_y > (int16_t)(HACKYLENS_FONT_H / 2U) ?
            (uint16_t)(results[i].center_y - HACKYLENS_FONT_H / 2U) : 0U;
        if(label_x + label_width > width)
            label_x = width > label_width ? width - label_width : 0U;
        if(label_y + HACKYLENS_FONT_H > height)
            label_y = height > HACKYLENS_FONT_H ? height - HACKYLENS_FONT_H : 0U;
        camera_view_compose_text_at(present, label_x, label_y, label,
                                    selected && selected[i] ? APRILTAG_SELECTED_COLOR : COLOR_TERM_GREEN,
                                    COLOR_BLACK);
    }
    camera_view_compose_crosshair(present, width / 2U, height / 2U, COLOR_WHITE);
}

void apriltag_view_draw_icon(uint16_t x, uint16_t y, uint16_t color, uint16_t bg)
{
    (void)bg;
    hk_ui_display_draw_rect(x + 13, y + 11, 34, 34, 3, color);
    hk_ui_display_draw_rect(x + 19, y + 17, 22, 22, 2, color);
    hk_ui_display_fill_rect(x + 22, y + 20, 5, 5, color);
    hk_ui_display_fill_rect(x + 33, y + 20, 5, 5, color);
    hk_ui_display_fill_rect(x + 22, y + 31, 5, 5, color);
    hk_ui_display_fill_rect(x + 31, y + 29, 7, 7, color);
}

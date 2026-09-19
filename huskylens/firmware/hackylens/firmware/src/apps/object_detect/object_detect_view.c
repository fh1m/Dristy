#include "object_detect_view.h"

#include <stdio.h>
#include <string.h>

#include "../../config/display_config.h"
#include "../../ui/display_binding.h"
#include "object_detect_labels.h"

static const uint16_t g_class_colors[] = {
    0x5DFFU, 0x07FFU, 0xFFE0U, 0xF81FU, 0xFD20U, 0xF800U,
};

void object_detect_view_compose_results(
    camera_view_present_t *present,
    uint16_t width,
    uint16_t height,
    const object_detect_result_t *results,
    uint8_t count)
{
    camera_view_frame_t frame = {0};

    if(!present || !results)
        return;
    if(count > OBJECT_DETECT_RESULT_MAX)
        count = OBJECT_DETECT_RESULT_MAX;
    frame.width = width;
    frame.height = height;

    for(uint8_t index = 0U; index < count; index++)
    {
        const object_detect_result_t *result = &results[index];
        camera_view_rect_t rect = {
            .x = result->x,
            .y = result->y,
            .w = result->w,
            .h = result->h,
        };
        char label[24];
        uint16_t label_width;
        uint16_t label_x;
        uint16_t label_y;
        uint16_t color =
            g_class_colors[result->class_id %
                           (sizeof(g_class_colors) / sizeof(g_class_colors[0]))];

        camera_view_compose_rects(present, &frame, &rect, 1U, color);
        snprintf(label, sizeof(label), "%s %u%%",
                 object_detect_label(result->class_id),
                 (unsigned)((result->confidence + 5U) / 10U));
        label_width = (uint16_t)(strlen(label) * HACKYLENS_FONT_W);
        label_x = result->x > 0 ? (uint16_t)result->x : 0U;
        label_y = result->y > 0 ? (uint16_t)result->y : 0U;
        if(label_width > width)
            label_width = width;
        if(label_x + label_width > width)
            label_x = width > label_width ? width - label_width : 0U;
        if(label_y + HACKYLENS_FONT_H > height)
            label_y = height > HACKYLENS_FONT_H ?
                      height - HACKYLENS_FONT_H : 0U;
        camera_view_compose_text_at(present, label_x, label_y, label,
                                    color, COLOR_BLACK);
    }
}

void object_detect_view_draw_icon(uint16_t x, uint16_t y,
                                  uint16_t color, uint16_t bg)
{
    (void)bg;
    hk_ui_display_draw_rect(x + 10U, y + 12U, 40U, 32U, 2U, color);
    hk_ui_display_draw_rect(x + 17U, y + 18U, 14U, 12U, 2U, color);
    hk_ui_display_draw_rect(x + 32U, y + 28U, 12U, 10U, 2U, color);
    hk_ui_display_fill_rect(x + 13U, y + 9U, 8U, 3U, color);
}

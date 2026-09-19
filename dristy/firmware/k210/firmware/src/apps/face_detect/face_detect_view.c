#include "face_detect_view.h"

#include <stdio.h>

#include "../../config/display_config.h"
#include "../../ui/display_binding.h"
#include "../../ui/dristy_theme.h"
#include "../../ui/camera_view.h"

void face_detect_view_draw_boxes(uint16_t width, uint16_t height,
                                 const face_detect_box_t *boxes, uint8_t count)
{
    camera_view_frame_t view = {0};
    camera_view_rect_t rects[FACE_DETECT_BOX_MAX];

    if(count > FACE_DETECT_BOX_MAX)
        count = FACE_DETECT_BOX_MAX;
    for(uint8_t i = 0; i < count; i++)
    {
        rects[i].x = boxes[i].x;
        rects[i].y = boxes[i].y;
        rects[i].w = boxes[i].w;
        rects[i].h = boxes[i].h;
    }
    view.width = width;
    view.height = height;
    camera_view_draw_rects(&view, rects, count, DRISTY_COLOR_ACCENT);
}

void face_detect_view_compose_boxes(camera_view_present_t *present,
                                    uint16_t width,
                                    uint16_t height,
                                    const face_detect_box_t *boxes,
                                    uint8_t count)
{
    camera_view_frame_t view = {0};
    camera_view_rect_t rects[FACE_DETECT_BOX_MAX];
    char label[12];
    uint8_t i;

    if(!present || !boxes)
        return;
    if(count > FACE_DETECT_BOX_MAX)
        count = FACE_DETECT_BOX_MAX;
    view.width = width;
    view.height = height;
    for(i = 0U; i < count; i++)
    {
        rects[i].x = boxes[i].x;
        rects[i].y = boxes[i].y;
        rects[i].w = boxes[i].w;
        rects[i].h = boxes[i].h;
    }
    camera_view_compose_rects(present, &view, rects, count, DRISTY_COLOR_ACCENT);
    for(i = 0U; i < count; i++)
    {
        snprintf(label, sizeof(label), "F%u", (unsigned)(i + 1U));
        camera_view_compose_text_at(present, (uint16_t)boxes[i].x,
                                    boxes[i].y > 12U ? (uint16_t)(boxes[i].y - 12U) :
                                                       (uint16_t)boxes[i].y,
                                    label, DRISTY_COLOR_ACCENT, COLOR_BLACK);
    }
}

void face_detect_view_draw_icon(uint16_t x, uint16_t y, uint16_t color, uint16_t bg)
{
    (void)bg;
    hk_ui_display_draw_rect(x + 15, y + 10, 30, 34, 2, color);
    hk_ui_display_fill_rect(x + 23, y + 22, 4, 4, color);
    hk_ui_display_fill_rect(x + 34, y + 22, 4, 4, color);
    hk_ui_display_fill_rect(x + 25, y + 34, 12, 2, color);
}

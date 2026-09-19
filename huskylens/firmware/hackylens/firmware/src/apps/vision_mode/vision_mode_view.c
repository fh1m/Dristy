#include "vision_mode_view.h"

#include "../../config/display_config.h"
#include "../../ui/dristy_theme.h"
#include "../../ui/dristy_ui.h"
#include "../../dristy/dristy_result_bus.h"
#include "../../ui/display_binding.h"
#include "../../ui/camera_view.h"
#include "../object_detect/object_detect_labels.h"

#include <stdio.h>

void vision_mode_view_draw_stub(dristy_mode_t mode)
{
    const char *name = dristy_mode_name(mode);

    dristy_ui_draw_shell_bg();
    dristy_ui_draw_app_header(name, "Not wired yet");
    hk_ui_display_draw_dristy_text_at(16, 100, "Not wired yet", DRISTY_COLOR_TEXT,
                                    DRISTY_COLOR_BG);
    hk_ui_display_draw_dristy_small_text_at(16, 130, "Host mode OK",
                                           DRISTY_COLOR_TEXT_DIM, DRISTY_COLOR_BG);
    dristy_ui_draw_app_footer(" ", " ", " ", "Back");
}

static void draw_box(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color)
{
    if(w <= 0 || h <= 0)
        return;
    hk_ui_display_draw_rect((uint16_t)x, (uint16_t)y, (uint16_t)w, (uint16_t)h,
                            1U, color);
}

static void draw_label(int16_t x, int16_t y, const char *text, uint16_t color)
{
    if(!text || !text[0])
        return;
    if(x < 0)
        x = 0;
    if(y < 0)
        y = 0;
    hk_ui_display_draw_dristy_small_text_at((uint16_t)x, (uint16_t)y, text, color,
                                           DRISTY_COLOR_BG);
}

void vision_mode_view_draw_overlays(const dristy_result_snapshot_t *snap)
{
    uint8_t i;

    if(!snap)
        return;

    for(i = 0U; i < snap->detection_count; i++)
    {
        const dristy_result_detection_t *d = &snap->detections[i];
        char label[32];

        draw_box(d->x, d->y, d->w, d->h, DRISTY_COLOR_ACCENT);
        snprintf(label, sizeof(label), "%s %u",
                 (snap->mode == DRISTY_MODE_MOTION_DETECT) ? "MOTION" :
                 object_detect_label(d->cls),
                 (unsigned)(d->confidence / 10U));
        draw_label(d->x, d->y > 12 ? d->y - 12 : d->y, label, DRISTY_COLOR_ACCENT);
    }
    for(i = 0U; i < snap->track_count; i++)
    {
        const dristy_result_track_t *t = &snap->tracks[i];
        char label[16];
        int16_t x0 = (int16_t)(t->cx - t->w / 2);
        int16_t y0 = (int16_t)(t->cy - t->h / 2);

        draw_box(x0, y0, t->w, t->h, DRISTY_COLOR_ACCENT);
        snprintf(label, sizeof(label), "ID%u", (unsigned)t->id);
        draw_label(x0, y0 > 12 ? y0 - 12 : y0, label, DRISTY_COLOR_ACCENT);
    }
    for(i = 0U; i < snap->tag_count; i++)
    {
        const dristy_result_tag_t *tag = &snap->tags[i];
        char label[16];

        draw_box((int16_t)(tag->cx - 8), (int16_t)(tag->cy - 8), 16, 16,
                 DRISTY_COLOR_ACCENT);
        snprintf(label, sizeof(label), "T%u", (unsigned)tag->tag_id);
        draw_label(tag->cx - 8, tag->cy - 20, label, DRISTY_COLOR_ACCENT);
    }
    for(i = 0U; i < snap->blob_count; i++)
    {
        const dristy_result_blob_t *b = &snap->blobs[i];
        draw_box((int16_t)(b->cx - b->w / 2), (int16_t)(b->cy - b->h / 2),
                 b->w, b->h, DRISTY_COLOR_ACCENT);
    }
    if(snap->line_valid)
    {
        int16_t x1 = dristy_norm_to_pixel_x(snap->line.x1, 320);
        int16_t y1 = dristy_norm_to_pixel_y(snap->line.y1, 240);
        int16_t x2 = dristy_norm_to_pixel_x(snap->line.x2, 320);
        int16_t y2 = dristy_norm_to_pixel_y(snap->line.y2, 240);
        draw_box(x1, y1, 4, 4, DRISTY_COLOR_ACCENT);
        draw_box(x2, y2, 4, 4, DRISTY_COLOR_ACCENT);
    }
    for(i = 0U; i < snap->flow_count; i++)
    {
        const dristy_result_flow_t *f = &snap->flow[i];
        int16_t cx = dristy_norm_to_pixel_x(f->roi_cx, 320);
        int16_t cy = dristy_norm_to_pixel_y(f->roi_cy, 240);
        int16_t ex = cx + (int16_t)(f->dx_x100 / 25);
        int16_t ey = cy + (int16_t)(f->dy_x100 / 25);

        draw_box(cx - 2, cy - 2, 4, 4, DRISTY_COLOR_ACCENT);
        draw_box(ex - 2, ey - 2, 4, 4, DRISTY_COLOR_ACCENT);
        draw_label(cx, cy + 6, "FLOW", DRISTY_COLOR_ACCENT);
    }
}

void vision_mode_view_compose_overlays(camera_view_present_t *present,
                                       uint16_t width,
                                       uint16_t height,
                                       const dristy_result_snapshot_t *snap)
{
    camera_view_frame_t frame = {0};
    uint8_t i;

    if(!present || !snap)
        return;
    frame.width = width;
    frame.height = height;

    for(i = 0U; i < snap->detection_count; i++)
    {
        const dristy_result_detection_t *d = &snap->detections[i];
        camera_view_rect_t rect = {.x = d->x, .y = d->y, .w = d->w, .h = d->h};
        char label[32];

        camera_view_compose_rects(present, &frame, &rect, 1U, DRISTY_COLOR_ACCENT);
        snprintf(label, sizeof(label), "%s %u",
                 (snap->mode == DRISTY_MODE_MOTION_DETECT) ? "MOTION" :
                 object_detect_label(d->cls),
                 (unsigned)(d->confidence / 10U));
        camera_view_compose_text_at(present, (uint16_t)d->x,
                                    d->y > 12 ? (uint16_t)(d->y - 12U) : (uint16_t)d->y,
                                    label, DRISTY_COLOR_ACCENT, COLOR_BLACK);
    }
    for(i = 0U; i < snap->track_count; i++)
    {
        const dristy_result_track_t *t = &snap->tracks[i];
        camera_view_rect_t rect = {
            .x = (int16_t)(t->cx - t->w / 2),
            .y = (int16_t)(t->cy - t->h / 2),
            .w = t->w,
            .h = t->h,
        };
        char label[16];

        camera_view_compose_rects(present, &frame, &rect, 1U, DRISTY_COLOR_ACCENT);
        snprintf(label, sizeof(label), "ID%u", (unsigned)t->id);
        camera_view_compose_text_at(present, (uint16_t)rect.x,
                                    rect.y > 12 ? (uint16_t)(rect.y - 12U) : (uint16_t)rect.y,
                                    label, DRISTY_COLOR_ACCENT, COLOR_BLACK);
    }
    for(i = 0U; i < snap->tag_count; i++)
    {
        const dristy_result_tag_t *tag = &snap->tags[i];
        camera_view_rect_t rect = {
            .x = (int16_t)(tag->cx - 8),
            .y = (int16_t)(tag->cy - 8),
            .w = 16,
            .h = 16,
        };
        char label[16];

        camera_view_compose_rects(present, &frame, &rect, 1U, DRISTY_COLOR_ACCENT);
        snprintf(label, sizeof(label), "T%u", (unsigned)tag->tag_id);
        camera_view_compose_text_at(present, (uint16_t)rect.x,
                                    tag->cy > 20 ? (uint16_t)(tag->cy - 20U) : 0U,
                                    label, DRISTY_COLOR_ACCENT, COLOR_BLACK);
    }
    for(i = 0U; i < snap->blob_count; i++)
    {
        const dristy_result_blob_t *b = &snap->blobs[i];
        camera_view_rect_t rect = {
            .x = (int16_t)(b->cx - b->w / 2),
            .y = (int16_t)(b->cy - b->h / 2),
            .w = b->w,
            .h = b->h,
        };

        camera_view_compose_rects(present, &frame, &rect, 1U, DRISTY_COLOR_ACCENT);
        camera_view_compose_text_at(present, (uint16_t)rect.x,
                                    rect.y > 12 ? (uint16_t)(rect.y - 12U) : (uint16_t)rect.y,
                                    "BLOB", DRISTY_COLOR_ACCENT, COLOR_BLACK);
    }
    if(snap->line_valid)
    {
        int16_t x1 = dristy_norm_to_pixel_x(snap->line.x1, width);
        int16_t y1 = dristy_norm_to_pixel_y(snap->line.y1, height);
        int16_t x2 = dristy_norm_to_pixel_x(snap->line.x2, width);
        int16_t y2 = dristy_norm_to_pixel_y(snap->line.y2, height);
        camera_view_rect_t dots[2] = {
            {.x = x1, .y = y1, .w = 4, .h = 4},
            {.x = x2, .y = y2, .w = 4, .h = 4},
        };

        camera_view_compose_rects(present, &frame, dots, 2U, DRISTY_COLOR_ACCENT);
    }
    for(i = 0U; i < snap->flow_count; i++)
    {
        const dristy_result_flow_t *f = &snap->flow[i];
        int16_t cx = dristy_norm_to_pixel_x(f->roi_cx, width);
        int16_t cy = dristy_norm_to_pixel_y(f->roi_cy, height);
        int16_t ex = cx + (int16_t)(f->dx_x100 / 25);
        int16_t ey = cy + (int16_t)(f->dy_x100 / 25);
        camera_view_rect_t marks[2] = {
            {.x = (int16_t)(cx - 2), .y = (int16_t)(cy - 2), .w = 4, .h = 4},
            {.x = (int16_t)(ex - 2), .y = (int16_t)(ey - 2), .w = 4, .h = 4},
        };

        camera_view_compose_rects(present, &frame, marks, 2U, DRISTY_COLOR_ACCENT);
        camera_view_compose_text_at(present, (uint16_t)cx, (uint16_t)(cy + 6U), "FLOW",
                                    DRISTY_COLOR_ACCENT, COLOR_BLACK);
    }
}

void vision_mode_view_draw_icon(uint16_t x, uint16_t y, uint16_t color, uint16_t bg)
{
    hk_ui_display_fill_rect(x, y, 40, 40, bg);
    hk_ui_display_draw_rect(x + 4, y + 4, 32, 32, 1U, color);
}

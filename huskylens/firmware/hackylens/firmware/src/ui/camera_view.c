#include "camera_view.h"

#include <string.h>

#include "../config/display_config.h"

#include "dristy_theme.h"
#include "hk_ui.h"
#include "../ui/display_binding.h"

static uint16_t g_camera_map_x[HK_DISPLAY_REQUIRED_WIDTH];
static uint16_t g_camera_map_y[HK_DISPLAY_REQUIRED_HEIGHT];
static uint16_t g_camera_map_logical_w;
static uint16_t g_camera_map_logical_h;
static uint16_t g_camera_map_dst_w;
static uint16_t g_camera_map_dst_h;
static uint8_t g_camera_map_valid;

static inline void camera_view_write_pixel(uint8_t *dst, uint16_t color)
{
    dst[0] = color >> 8;
    dst[1] = color & 0xFF;
}

static uint8_t camera_view_compose_direct(const camera_view_frame_t *frame,
                                          hk_ui_display_surface_t *surface,
                                          uint16_t dst_x,
                                          uint16_t dst_y,
                                          uint16_t dst_w,
                                          uint16_t dst_h)
{
    uint16_t x_step;
    uint16_t y_step;

    if(frame->rotate || dst_x != 0 || dst_y != 0 || dst_w != HK_DISPLAY_REQUIRED_WIDTH || dst_h != HK_DISPLAY_REQUIRED_HEIGHT)
        return 0;
    if(frame->width == 640U && frame->height == 480U)
    {
        x_step = 2;
        y_step = 2;
    }
    else if(frame->width == 320U && frame->height == 240U)
    {
        x_step = 1;
        y_step = 1;
    }
    else if(frame->width == 160U && frame->height == 120U)
    {
        x_step = 0;
        y_step = 0;
    }
    else
    {
        return 0;
    }

    for(uint16_t y = 0; y < HK_DISPLAY_REQUIRED_HEIGHT; y++)
    {
        uint32_t src_y = y_step ? (uint32_t)y * y_step : ((uint32_t)y >> 1U);
        const volatile uint16_t *src = frame->pixels + src_y * frame->width;
        uint8_t *dst = surface->rgb565_be + (uint32_t)y * surface->stride_bytes;

        for(uint16_t x = 0; x < HK_DISPLAY_REQUIRED_WIDTH; x++)
        {
            uint32_t src_x = x_step ? (uint32_t)x * x_step : ((uint32_t)x >> 1U);
            camera_view_write_pixel(dst + x * 2U, src[src_x]);
        }
    }
    return 1;
}

static void camera_view_prepare_maps(uint16_t logical_w, uint16_t logical_h, uint16_t dst_w, uint16_t dst_h)
{
    if(g_camera_map_valid &&
       g_camera_map_logical_w == logical_w && g_camera_map_logical_h == logical_h &&
       g_camera_map_dst_w == dst_w && g_camera_map_dst_h == dst_h)
        return;

    for(uint16_t x = 0; x < dst_w; x++)
    {
        if(logical_w == 640U && dst_w == HK_DISPLAY_REQUIRED_WIDTH)
            g_camera_map_x[x] = x * 2U;
        else if(logical_w == 320U && dst_w == HK_DISPLAY_REQUIRED_WIDTH)
            g_camera_map_x[x] = x;
        else if(logical_w == 160U && dst_w == HK_DISPLAY_REQUIRED_WIDTH)
            g_camera_map_x[x] = x / 2U;
        else
            g_camera_map_x[x] = (uint16_t)((uint32_t)x * logical_w / dst_w);
    }
    for(uint16_t y = 0; y < dst_h; y++)
    {
        if(logical_h == 480U && dst_h == HK_DISPLAY_REQUIRED_HEIGHT)
            g_camera_map_y[y] = y * 2U;
        else if(logical_h == 240U && dst_h == HK_DISPLAY_REQUIRED_HEIGHT)
            g_camera_map_y[y] = y;
        else if(logical_h == 120U && dst_h == HK_DISPLAY_REQUIRED_HEIGHT)
            g_camera_map_y[y] = y / 2U;
        else
            g_camera_map_y[y] = (uint16_t)((uint32_t)y * logical_h / dst_h);
    }
    g_camera_map_logical_w = logical_w;
    g_camera_map_logical_h = logical_h;
    g_camera_map_dst_w = dst_w;
    g_camera_map_dst_h = dst_h;
    g_camera_map_valid = 1;
}

static void camera_view_compose_overlay(hk_ui_display_surface_t *surface,
                                        const char *text,
                                        uint8_t enabled,
                                        uint16_t box_x,
                                        uint16_t box_y,
                                        uint16_t box_w)
{
    const uint16_t box_h = HACKYLENS_FONT_H + 4U;
    const uint16_t text_x = box_x + 4U;
    const uint16_t text_y = box_y + 2U;
    uint16_t clipped_w;
    uint16_t clipped_h;

    if(!enabled || !text || box_x >= surface->width || box_y >= surface->height)
        return;
    clipped_w = box_x + box_w > surface->width ? surface->width - box_x : box_w;
    clipped_h = box_y + box_h > surface->height ? surface->height - box_y : box_h;
    for(uint16_t y = 0; y < clipped_h; y++)
        memset(surface->rgb565_be + (uint32_t)(box_y + y) * surface->stride_bytes + box_x * 2U,
               0,
               clipped_w * 2U);

    for(uint16_t index = 0; text[index] && text_x + (index + 1U) * HACKYLENS_FONT_W <= box_x + clipped_w; index++)
    {
        const uint8_t *glyph = hk_font_glyph(text[index]);
        uint16_t glyph_x0 = text_x + index * HACKYLENS_FONT_W;

        for(uint16_t y = 0; y < HACKYLENS_FONT_H && text_y + y < surface->height; y++)
        {
            for(uint16_t x = 0; x < HACKYLENS_FONT_W; x++)
            {
                uint8_t bit = glyph[y * HACKYLENS_FONT_ROW_BYTES + x / 8U] & (0x80U >> (x & 7U));
                if(bit)
                {
                    uint8_t *dst = surface->rgb565_be +
                        (uint32_t)(text_y + y) * surface->stride_bytes + (glyph_x0 + x) * 2U;
                    camera_view_write_pixel(dst, DRISTY_COLOR_ACCENT);
                }
            }
        }
    }
}

static uint16_t camera_view_overlay_width(const char *text)
{
    const uint16_t max_width = HK_DISPLAY_REQUIRED_WIDTH / 2U - 4U;
    uint32_t width;

    if(!text)
        return 0;
    width = (uint32_t)strlen(text) * HACKYLENS_FONT_W + 8U;
    if(width > max_width)
        width = max_width;
    return (uint16_t)width;
}

uint8_t camera_view_compose_frame(const camera_view_frame_t *frame, camera_view_present_t *present)
{
    hk_ui_display_surface_t surface;
    uint16_t logical_w;
    uint16_t logical_h;
    uint16_t dst_x;
    uint16_t dst_y;
    uint16_t dst_w;
    uint16_t dst_h;

    if(present)
        memset(present, 0, sizeof(*present));
    if(!frame || !present || !frame->pixels || frame->width == 0 || frame->height == 0)
        return 0;
    if(!hk_ui_display_frame_acquire(&surface))
        return 0;

    logical_w = frame->rotate ? frame->height : frame->width;
    logical_h = frame->rotate ? frame->width : frame->height;
    image_fit_viewport(logical_w, logical_h, &dst_x, &dst_y, &dst_w, &dst_h);
    if(dst_w == 0 || dst_h == 0 || dst_x + dst_w > surface.width || dst_y + dst_h > surface.height)
    {
        hk_ui_display_frame_cancel(surface.lease_id);
        return 0;
    }

    memset(surface.rgb565_be, 0, (uint32_t)surface.stride_bytes * surface.height);
    if(!camera_view_compose_direct(frame, &surface, dst_x, dst_y, dst_w, dst_h))
    {
        camera_view_prepare_maps(logical_w, logical_h, dst_w, dst_h);
        for(uint16_t y = 0; y < dst_h; y++)
        {
            uint16_t logical_y = g_camera_map_y[y];
            uint8_t *dst = surface.rgb565_be +
                (uint32_t)(dst_y + y) * surface.stride_bytes + dst_x * 2U;

            for(uint16_t x = 0; x < dst_w; x++)
            {
                uint16_t logical_x = g_camera_map_x[x];
                uint32_t src_x;
                uint32_t src_y;
                uint16_t color;

                if(frame->rotate)
                {
                    src_x = logical_y;
                    src_y = frame->height - 1U - logical_x;
                }
                else
                {
                    src_x = logical_x;
                    src_y = logical_y;
                }

                color = frame->pixels[src_y * frame->width + src_x];
                camera_view_write_pixel(dst + x * 2U, color);
            }
        }
    }
    {
        uint16_t fps_width = camera_view_overlay_width(frame->fps_text);
        uint16_t light_width = camera_view_overlay_width(frame->light_text);

        camera_view_compose_overlay(&surface, frame->fps_text, frame->fps_overlay, 2, 2, fps_width);
        camera_view_compose_overlay(&surface,
                                    frame->light_text,
                                    frame->light_overlay,
                                    HK_DISPLAY_REQUIRED_WIDTH - light_width - 2U,
                                    2,
                                    light_width);
    }
    present->lease_id = surface.lease_id;
    present->rgb565_be = surface.rgb565_be;
    present->width = surface.width;
    present->height = surface.height;
    present->stride_bytes = surface.stride_bytes;
    return 1;
}

static void camera_view_compose_fill_rect(camera_view_present_t *present,
                                          uint16_t x, uint16_t y,
                                          uint16_t w, uint16_t h,
                                          uint16_t color)
{
    if(!present || !present->lease_id || !present->rgb565_be || !w || !h ||
       x >= present->width || y >= present->height)
        return;
    if(x + w > present->width)
        w = present->width - x;
    if(y + h > present->height)
        h = present->height - y;

    for(uint16_t row = 0; row < h; row++)
    {
        uint8_t *dst = present->rgb565_be +
            (uint32_t)(y + row) * present->stride_bytes + (uint32_t)x * 2U;
        for(uint16_t column = 0; column < w; column++)
            camera_view_write_pixel(dst + (uint32_t)column * 2U, color);
    }
}

void camera_view_compose_crosshair(camera_view_present_t *present,
                                   uint16_t x,
                                   uint16_t y,
                                   uint16_t color)
{
    const uint16_t arm = 12U;
    const uint16_t gap = 3U;

    if(!present || x >= present->width || y >= present->height)
        return;
    if(x > arm)
        camera_view_compose_fill_rect(present, x - arm, y, arm - gap, 1U, color);
    camera_view_compose_fill_rect(present, x + gap + 1U, y, arm - gap, 1U, color);
    if(y > arm)
        camera_view_compose_fill_rect(present, x, y - arm, 1U, arm - gap, color);
    camera_view_compose_fill_rect(present, x, y + gap + 1U, 1U, arm - gap, color);
}

static void camera_view_compose_rect(camera_view_present_t *present,
                                     uint16_t x, uint16_t y,
                                     uint16_t w, uint16_t h,
                                     uint16_t thickness, uint16_t color)
{
    for(uint16_t i = 0; i < thickness; i++)
    {
        if(w <= i * 2U || h <= i * 2U)
            break;
        camera_view_compose_fill_rect(present, x + i, y + i, w - i * 2U, 1U, color);
        camera_view_compose_fill_rect(present, x + i, y + h - 1U - i, w - i * 2U, 1U, color);
        camera_view_compose_fill_rect(present, x + i, y + i, 1U, h - i * 2U, color);
        camera_view_compose_fill_rect(present, x + w - 1U - i, y + i, 1U, h - i * 2U, color);
    }
}

void camera_view_compose_rects(camera_view_present_t *present,
                               const camera_view_frame_t *frame,
                               const camera_view_rect_t *rects,
                               uint8_t count,
                               uint16_t color)
{
    uint16_t logical_w;
    uint16_t logical_h;
    uint16_t dst_x;
    uint16_t dst_y;
    uint16_t dst_w;
    uint16_t dst_h;

    if(!present || !frame || !rects || !frame->width || !frame->height)
        return;
    logical_w = frame->rotate ? frame->height : frame->width;
    logical_h = frame->rotate ? frame->width : frame->height;
    image_fit_viewport(logical_w, logical_h, &dst_x, &dst_y, &dst_w, &dst_h);
    for(uint8_t i = 0; i < count; i++)
    {
        int32_t x0 = rects[i].x;
        int32_t y0 = rects[i].y;
        int32_t x1 = x0 + rects[i].w;
        int32_t y1 = y0 + rects[i].h;
        uint16_t draw_x;
        uint16_t draw_y;
        uint16_t draw_w;
        uint16_t draw_h;

        if(frame->rotate || rects[i].w <= 0 || rects[i].h <= 0)
            continue;
        if(x0 < 0) x0 = 0;
        if(y0 < 0) y0 = 0;
        if(x1 > frame->width) x1 = frame->width;
        if(y1 > frame->height) y1 = frame->height;
        if(x1 <= x0 || y1 <= y0)
            continue;
        draw_x = (uint16_t)(dst_x + (uint32_t)x0 * dst_w / frame->width);
        draw_y = (uint16_t)(dst_y + (uint32_t)y0 * dst_h / frame->height);
        draw_w = (uint16_t)(((uint32_t)(x1 - x0) * dst_w + frame->width - 1U) / frame->width);
        draw_h = (uint16_t)(((uint32_t)(y1 - y0) * dst_h + frame->height - 1U) / frame->height);
        camera_view_compose_rect(present, draw_x, draw_y,
                                 draw_w ? draw_w : 1U, draw_h ? draw_h : 1U,
                                 2U, color);
    }
}

void camera_view_compose_text_at(camera_view_present_t *present,
                                 uint16_t x,
                                 uint16_t y,
                                 const char *text,
                                 uint16_t fg,
                                 uint16_t bg)
{
    if(!present || !text)
        return;
    while(*text && x + HACKYLENS_FONT_W <= present->width &&
          y + HACKYLENS_FONT_H <= present->height)
    {
        const uint8_t *glyph = hk_font_glyph((uint8_t)*text++);

        for(uint16_t glyph_y = 0; glyph_y < HACKYLENS_FONT_H; glyph_y++)
        {
            uint8_t *dst = present->rgb565_be +
                (uint32_t)(y + glyph_y) * present->stride_bytes + (uint32_t)x * 2U;
            for(uint16_t glyph_x = 0; glyph_x < HACKYLENS_FONT_W; glyph_x++)
            {
                uint8_t bit = glyph[glyph_y * HACKYLENS_FONT_ROW_BYTES + glyph_x / 8U] &
                    (uint8_t)(0x80U >> (glyph_x & 7U));
                camera_view_write_pixel(dst + (uint32_t)glyph_x * 2U, bit ? fg : bg);
            }
        }
        x += HACKYLENS_FONT_W;
    }
}

uint8_t camera_view_present(camera_view_present_t *present)
{
    uint32_t lease_id;
    uint8_t result;

    if(!present || present->lease_id == 0)
        return 0;
    lease_id = present->lease_id;
    memset(present, 0, sizeof(*present));
    result = hk_ui_display_frame_present(lease_id);
    if(!result)
        hk_ui_display_frame_cancel(lease_id);
    return result;
}

void camera_view_discard(camera_view_present_t *present)
{
    if(!present || present->lease_id == 0)
        return;
    hk_ui_display_frame_cancel(present->lease_id);
    memset(present, 0, sizeof(*present));
}

void camera_view_draw_rects(const camera_view_frame_t *frame, const camera_view_rect_t *rects, uint8_t count, uint16_t color)
{
    uint16_t logical_w;
    uint16_t logical_h;
    uint16_t dst_x;
    uint16_t dst_y;
    uint16_t dst_w;
    uint16_t dst_h;

    if(!frame || !rects || !frame->width || !frame->height)
        return;
    logical_w = frame->rotate ? frame->height : frame->width;
    logical_h = frame->rotate ? frame->width : frame->height;
    image_fit_viewport(logical_w, logical_h, &dst_x, &dst_y, &dst_w, &dst_h);
    for(uint8_t i = 0; i < count; i++)
    {
        int32_t x0 = rects[i].x;
        int32_t y0 = rects[i].y;
        int32_t x1 = x0 + rects[i].w;
        int32_t y1 = y0 + rects[i].h;
        uint16_t draw_x;
        uint16_t draw_y;
        uint16_t draw_w;
        uint16_t draw_h;

        if(frame->rotate || rects[i].w <= 0 || rects[i].h <= 0)
            continue;
        if(x0 < 0) x0 = 0;
        if(y0 < 0) y0 = 0;
        if(x1 > frame->width) x1 = frame->width;
        if(y1 > frame->height) y1 = frame->height;
        if(x1 <= x0 || y1 <= y0)
            continue;
        draw_x = (uint16_t)(dst_x + (uint32_t)x0 * dst_w / frame->width);
        draw_y = (uint16_t)(dst_y + (uint32_t)y0 * dst_h / frame->height);
        draw_w = (uint16_t)(((uint32_t)(x1 - x0) * dst_w + frame->width - 1U) / frame->width);
        draw_h = (uint16_t)(((uint32_t)(y1 - y0) * dst_h + frame->height - 1U) / frame->height);
        hk_ui_display_draw_rect(draw_x, draw_y, draw_w ? draw_w : 1, draw_h ? draw_h : 1, 2, color);
    }
}

void camera_view_clear(void)
{
    hk_ui_display_fill_rect(0, 0, HK_DISPLAY_REQUIRED_WIDTH, HK_DISPLAY_REQUIRED_HEIGHT, COLOR_BLACK);
}

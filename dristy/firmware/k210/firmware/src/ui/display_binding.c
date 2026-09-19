#include "display_binding.h"

#include <dristy/capability/display.h>

#include <limits.h>
#include <string.h>

#include "dristy_font_1bpp.h"
#include "hk_font.h"
#include "../config/display_config.h"
#include "../core/hk_capability_client.h"
#include "dristy_boot_view.h"
#include "dristy_font.h"
#include "hal_time.h"

#define UI_DISPLAY_PRESENT_TIMEOUT_US 500000ULL

static hk_display_t s_display;
static hk_owner_t s_owner;
static uint8_t s_ready;
static uint8_t s_frame_active;
static uint32_t s_frame_lease_id;
static uint32_t s_next_frame_lease_id;
static uint8_t *s_frame_pixels;
static uint32_t s_frame_stride;
static uint8_t s_row[HK_UI_DISPLAY_WIDTH * 2U];

static hk_deadline_t present_deadline(void)
{
    uint64_t now = hal_time_us();
    hk_deadline_t deadline = {
        UINT64_MAX - now <= UI_DISPLAY_PRESENT_TIMEOUT_US ?
            UINT64_MAX - 1U : now + UI_DISPLAY_PRESENT_TIMEOUT_US,
    };
    return deadline;
}

hk_result_t hk_ui_display_prepare(void)
{
    hk_capability_request_t request = HK_DISPLAY_REQUEST_0_1_INIT;
    hk_display_info_t info;
    hk_result_t result;

    if(s_ready)
        return HK_OK;
    s_owner = capability_client_consumer_owner("consumer:firmware-runtime");
    if(hk_owner_is_zero(s_owner))
        return HK_ERR_STALE_HANDLE;
    request.required_features =
        HK_DISPLAY_FEATURE_BASE_PLANE |
        HK_DISPLAY_FEATURE_DIRTY_REGIONS |
        HK_DISPLAY_FEATURE_RGB565 |
        HK_DISPLAY_FEATURE_BORROWED_SURFACE;
    result = hk_display_acquire(
        s_owner, &request, HK_DISPLAY_PLANE_BASE, &s_display);
    if(result != HK_OK)
        return result;
    result = hk_display_get_info(s_owner, &s_display, &info);
    if(result != HK_OK || info.width < HK_UI_DISPLAY_WIDTH ||
       info.height < HK_UI_DISPLAY_HEIGHT ||
       (info.pixel_formats & HK_DISPLAY_FORMAT_RGB565_BE) == 0U ||
       info.maximum_dirty_rects < HK_UI_DISPLAY_FRAME_MAX_DIRTY_RECTS)
    {
        (void)hk_display_release(s_owner, HK_DEADLINE_IMMEDIATE, &s_display);
        return result == HK_OK ? HK_ERR_FEATURE_UNAVAILABLE : result;
    }
    s_ready = 1U;
    return HK_OK;
}

static hk_result_t surface_acquire(hk_display_surface_t *surface)
{
    hk_result_t result;

    if(s_frame_active)
    {
        *surface = (hk_display_surface_t){
            sizeof(hk_display_surface_t), HK_DISPLAY_SURFACE_VERSION,
            {s_frame_pixels,
             s_frame_stride * HK_UI_DISPLAY_HEIGHT,
             s_frame_stride,
             HK_BUFFER_ACCESS_READABLE | HK_BUFFER_ACCESS_WRITABLE},
            HK_UI_DISPLAY_WIDTH, HK_UI_DISPLAY_HEIGHT,
            HK_DISPLAY_FORMAT_RGB565_BE, 0U,
        };
        return HK_OK;
    }
    if(!s_ready)
    {
        result = hk_ui_display_prepare();
        if(result != HK_OK)
            return result;
    }
    return hk_display_surface_acquire(s_owner, &s_display, surface);
}

static hk_result_t surface_present_now(const hk_display_rect_t *dirty)
{
    hk_result_t result = hk_display_mark_dirty(s_owner, &s_display, dirty);

    if(result == HK_OK)
        result = hk_display_present(
            s_owner, &s_display, present_deadline(), NULL);
    if(result != HK_OK)
        (void)hk_display_abort(s_owner, &s_display);
    return result;
}

static hk_result_t surface_present(const hk_display_rect_t *dirty)
{
    if(s_frame_active)
        return HK_OK;
    return surface_present_now(dirty);
}

static hk_display_rect_t clipped_rect(
    uint16_t x, uint16_t y, uint16_t width, uint16_t height)
{
    hk_display_rect_t result = {x, y, width, height};

    if(x >= HK_UI_DISPLAY_WIDTH || y >= HK_UI_DISPLAY_HEIGHT)
        return (hk_display_rect_t){0, 0, 0U, 0U};
    if(width > HK_UI_DISPLAY_WIDTH - x)
        result.width = HK_UI_DISPLAY_WIDTH - x;
    if(height > HK_UI_DISPLAY_HEIGHT - y)
        result.height = HK_UI_DISPLAY_HEIGHT - y;
    return result;
}

static void surface_fill(
    const hk_display_surface_t *surface, const hk_display_rect_t *rect,
    uint16_t color)
{
    uint8_t high = (uint8_t)(color >> 8);
    uint8_t low = (uint8_t)color;

    for(uint32_t y = 0U; y < rect->height; y++)
    {
        uint8_t *row = (uint8_t *)surface->pixels.data +
            ((uint32_t)rect->y + y) * surface->pixels.stride_bytes +
            (uint32_t)rect->x * 2U;
        for(uint32_t x = 0U; x < rect->width; x++)
        {
            row[x * 2U] = high;
            row[x * 2U + 1U] = low;
        }
    }
}

void hk_ui_display_fill_rect(
    uint16_t x, uint16_t y, uint16_t width, uint16_t height,
    uint16_t color)
{
    hk_display_surface_t surface;
    hk_display_rect_t dirty = clipped_rect(x, y, width, height);

    if(dirty.width == 0U || dirty.height == 0U ||
       surface_acquire(&surface) != HK_OK)
        return;
    surface_fill(&surface, &dirty, color);
    (void)surface_present(&dirty);
}

void hk_ui_display_draw_rect(
    uint16_t x, uint16_t y, uint16_t width, uint16_t height,
    uint16_t thickness, uint16_t color)
{
    hk_display_surface_t surface;
    hk_display_rect_t dirty = clipped_rect(x, y, width, height);

    if(dirty.width == 0U || dirty.height == 0U || thickness == 0U ||
       surface_acquire(&surface) != HK_OK)
        return;
    for(uint16_t inset = 0U; inset < thickness; inset++)
    {
        hk_display_rect_t edge;
        if(width <= (uint32_t)inset * 2U || height <= (uint32_t)inset * 2U)
            break;
        edge = clipped_rect((uint16_t)(x + inset), (uint16_t)(y + inset),
                            (uint16_t)(width - inset * 2U), 1U);
        surface_fill(&surface, &edge, color);
        edge = clipped_rect((uint16_t)(x + inset),
                            (uint16_t)(y + height - 1U - inset),
                            (uint16_t)(width - inset * 2U), 1U);
        surface_fill(&surface, &edge, color);
        edge = clipped_rect((uint16_t)(x + inset), (uint16_t)(y + inset),
                            1U, (uint16_t)(height - inset * 2U));
        surface_fill(&surface, &edge, color);
        edge = clipped_rect((uint16_t)(x + width - 1U - inset),
                            (uint16_t)(y + inset), 1U,
                            (uint16_t)(height - inset * 2U));
        surface_fill(&surface, &edge, color);
    }
    (void)surface_present(&dirty);
}

static void surface_glyph_bytes(
    const hk_display_surface_t *surface, uint16_t x0, uint16_t y0,
    const uint8_t *glyph, uint16_t foreground, uint16_t background,
    uint16_t font_w, uint16_t font_h, uint16_t row_bytes)
{
    uint16_t y;
    uint16_t x;

    if(!glyph || x0 + font_w > HK_UI_DISPLAY_WIDTH ||
       y0 + font_h > HK_UI_DISPLAY_HEIGHT)
        return;
    for(y = 0U; y < font_h; y++)
    {
        uint8_t *row = (uint8_t *)surface->pixels.data +
            (uint32_t)(y0 + y) * surface->pixels.stride_bytes +
            (uint32_t)x0 * 2U;
        for(x = 0U; x < font_w; x++)
        {
            uint8_t bit = glyph[y * row_bytes + x / 8U] &
                          (uint8_t)(0x80U >> (x & 7U));
            uint16_t color = bit ? foreground : background;
            row[x * 2U] = (uint8_t)(color >> 8);
            row[x * 2U + 1U] = (uint8_t)color;
        }
    }
}

static void surface_glyph(
    const hk_display_surface_t *surface, uint16_t x0, uint16_t y0,
    uint32_t codepoint, uint16_t foreground, uint16_t background)
{
    surface_glyph_bytes(surface, x0, y0, hk_font_glyph(codepoint),
                        foreground, background,
                        DRISTY_FONT_W, DRISTY_FONT_H, DRISTY_FONT_ROW_BYTES);
}

static void surface_dristy_glyph(
    const hk_display_surface_t *surface, uint16_t x0, uint16_t y0,
    uint32_t codepoint, uint16_t foreground, uint16_t background)
{
    surface_glyph_bytes(surface, x0, y0, dristy_font_glyph(codepoint),
                        foreground, background, DRISTY_FONT_W, DRISTY_FONT_H,
                        DRISTY_FONT_ROW_BYTES);
}

void hk_ui_display_draw_glyph_at(
    uint16_t x, uint16_t y, uint32_t codepoint,
    uint16_t foreground, uint16_t background)
{
    hk_display_surface_t surface;
    hk_display_rect_t dirty = clipped_rect(
        x, y, DRISTY_FONT_W, DRISTY_FONT_H);

    if(dirty.width != DRISTY_FONT_W ||
       dirty.height != DRISTY_FONT_H ||
       surface_acquire(&surface) != HK_OK)
        return;
    surface_glyph(&surface, x, y, codepoint, foreground, background);
    (void)surface_present(&dirty);
}

static uint32_t utf8_next(const char **text)
{
    const uint8_t *cursor = (const uint8_t *)*text;
    uint8_t first = *cursor++;
    uint32_t codepoint;
    uint8_t required;

    if(first < 0x80U)
    {
        *text = (const char *)cursor;
        return first;
    }
    if((first & 0xE0U) == 0xC0U)
    {
        codepoint = first & 0x1FU;
        required = 1U;
    }
    else if((first & 0xF0U) == 0xE0U)
    {
        codepoint = first & 0x0FU;
        required = 2U;
    }
    else
    {
        codepoint = first & 0x07U;
        required = 3U;
    }
    while(required-- && (*cursor & 0xC0U) == 0x80U)
        codepoint = (codepoint << 6) | (*cursor++ & 0x3FU);
    *text = (const char *)cursor;
    return codepoint;
}

static void surface_glyph_scaled(
    const hk_display_surface_t *surface, uint16_t x0, uint16_t y0,
    uint32_t codepoint, uint16_t foreground, uint16_t background,
    uint16_t scale)
{
    const uint8_t *glyph = hk_font_glyph(codepoint);
    uint16_t out_w;
    uint16_t out_h;
    uint16_t ox;
    uint16_t oy;

    if(scale <= 1U)
    {
        surface_glyph(surface, x0, y0, codepoint, foreground, background);
        return;
    }
    out_w = (uint16_t)(DRISTY_FONT_W / scale);
    out_h = (uint16_t)(DRISTY_FONT_H / scale);
    if(x0 + DRISTY_FONT_W > HK_UI_DISPLAY_WIDTH ||
       y0 + DRISTY_FONT_H > HK_UI_DISPLAY_HEIGHT)
        return;
    for(oy = 0U; oy < out_h; oy++)
    {
        for(ox = 0U; ox < out_w; ox++)
        {
            uint16_t sx = (uint16_t)(ox * scale);
            uint16_t sy = (uint16_t)(oy * scale);
            uint8_t bit = glyph[sy * DRISTY_FONT_ROW_BYTES + sx / 8U] &
                          (uint8_t)(0x80U >> (sx & 7U));
            uint16_t color = bit ? foreground : background;
            uint8_t *base = (uint8_t *)surface->pixels.data +
                (uint32_t)(y0 + oy * scale) * surface->pixels.stride_bytes +
                (uint32_t)(x0 + ox * scale) * 2U;
            uint16_t dy;
            uint16_t dx;

            for(dy = 0U; dy < scale; dy++)
            {
                uint8_t *row = base + (uint32_t)dy * surface->pixels.stride_bytes;
                for(dx = 0U; dx < scale; dx++)
                {
                    row[dx * 2U] = (uint8_t)(color >> 8);
                    row[dx * 2U + 1U] = (uint8_t)color;
                }
            }
        }
    }
}

static void surface_dristy_glyph_scaled(
    const hk_display_surface_t *surface, uint16_t x0, uint16_t y0,
    uint32_t codepoint, uint16_t foreground, uint16_t background,
    uint16_t scale)
{
    const uint8_t *glyph = dristy_font_glyph(codepoint);
    uint16_t out_w;
    uint16_t out_h;
    uint16_t ox;
    uint16_t oy;

    if(scale <= 1U)
    {
        surface_dristy_glyph(surface, x0, y0, codepoint, foreground, background);
        return;
    }
    out_w = (uint16_t)(DRISTY_FONT_W / scale);
    out_h = (uint16_t)(DRISTY_FONT_H / scale);
    if(x0 + DRISTY_FONT_W > HK_UI_DISPLAY_WIDTH ||
       y0 + DRISTY_FONT_H > HK_UI_DISPLAY_HEIGHT)
        return;
    for(oy = 0U; oy < out_h; oy++)
    {
        for(ox = 0U; ox < out_w; ox++)
        {
            uint16_t sx = (uint16_t)(ox * scale);
            uint16_t sy = (uint16_t)(oy * scale);
            uint8_t bit = glyph[sy * DRISTY_FONT_ROW_BYTES + sx / 8U] &
                          (uint8_t)(0x80U >> (sx & 7U));
            uint16_t color = bit ? foreground : background;
            uint8_t *base = (uint8_t *)surface->pixels.data +
                (uint32_t)(y0 + oy * scale) * surface->pixels.stride_bytes +
                (uint32_t)(x0 + ox * scale) * 2U;
            uint16_t dy;
            uint16_t dx;

            for(dy = 0U; dy < scale; dy++)
            {
                uint8_t *row = base + (uint32_t)dy * surface->pixels.stride_bytes;
                for(dx = 0U; dx < scale; dx++)
                {
                    row[dx * 2U] = (uint8_t)(color >> 8);
                    row[dx * 2U + 1U] = (uint8_t)color;
                }
            }
        }
    }
}

static void surface_dristy_small_glyph(
    const hk_display_surface_t *surface, uint16_t x0, uint16_t y0,
    uint32_t codepoint, uint16_t foreground, uint16_t background)
{
    surface_glyph_bytes(surface, x0, y0, dristy_font_small_glyph(codepoint),
                        foreground, background, DRISTY_FONT_SMALL_W,
                        DRISTY_FONT_SMALL_H, DRISTY_FONT_SMALL_ROW_BYTES);
}

void hk_ui_display_draw_dristy_small_text_at(
    uint16_t x, uint16_t y, const char *text,
    uint16_t foreground, uint16_t background)
{
    hk_display_surface_t surface;
    uint16_t start = x;
    hk_display_rect_t dirty;

    if(!text || y + DRISTY_FONT_SMALL_H > HK_UI_DISPLAY_HEIGHT ||
       surface_acquire(&surface) != HK_OK)
        return;
    while(*text && x + DRISTY_FONT_SMALL_W <= HK_UI_DISPLAY_WIDTH)
    {
        surface_dristy_small_glyph(&surface, x, y, utf8_next(&text),
                                   foreground, background);
        x = (uint16_t)(x + DRISTY_FONT_SMALL_W);
    }
    dirty = clipped_rect(start, y, (uint16_t)(x - start), DRISTY_FONT_SMALL_H);
    if(dirty.width == 0U)
        (void)hk_display_abort(s_owner, &s_display);
    else
        (void)surface_present(&dirty);
}

void hk_ui_display_draw_dristy_text_at(
    uint16_t x, uint16_t y, const char *text,
    uint16_t foreground, uint16_t background)
{
    hk_display_surface_t surface;
    uint16_t start = x;
    hk_display_rect_t dirty;

    if(!text || y + DRISTY_FONT_H > HK_UI_DISPLAY_HEIGHT ||
       surface_acquire(&surface) != HK_OK)
        return;
    while(*text && x + DRISTY_FONT_W <= HK_UI_DISPLAY_WIDTH)
    {
        surface_dristy_glyph(&surface, x, y, utf8_next(&text),
                             foreground, background);
        x = (uint16_t)(x + DRISTY_FONT_W);
    }
    dirty = clipped_rect(start, y, (uint16_t)(x - start), DRISTY_FONT_H);
    if(dirty.width == 0U)
        (void)hk_display_abort(s_owner, &s_display);
    else
        (void)surface_present(&dirty);
}

void hk_ui_display_draw_dristy_text_scaled_at(
    uint16_t x, uint16_t y, const char *text,
    uint16_t foreground, uint16_t background, uint16_t scale)
{
    hk_display_surface_t surface;
    uint16_t start = x;
    hk_display_rect_t dirty;

    if(!text || scale == 0U)
        return;
    if(scale <= 1U)
    {
        hk_ui_display_draw_dristy_text_at(x, y, text, foreground, background);
        return;
    }
    if(y + DRISTY_FONT_H > HK_UI_DISPLAY_HEIGHT ||
       surface_acquire(&surface) != HK_OK)
        return;
    while(*text && x + DRISTY_FONT_W <= HK_UI_DISPLAY_WIDTH)
    {
        surface_dristy_glyph_scaled(&surface, x, y, utf8_next(&text),
                                    foreground, background, scale);
        x = (uint16_t)(x + DRISTY_FONT_W);
    }
    dirty = clipped_rect(start, y, (uint16_t)(x - start), DRISTY_FONT_H);
    if(dirty.width == 0U)
        (void)hk_display_abort(s_owner, &s_display);
    else
        (void)surface_present(&dirty);
}

void hk_ui_display_draw_text_scaled_at(
    uint16_t x, uint16_t y, const char *text,
    uint16_t foreground, uint16_t background, uint16_t scale)
{
    hk_display_surface_t surface;
    uint16_t start = x;
    hk_display_rect_t dirty;

    if(!text || scale == 0U)
        return;
    if(scale <= 1U)
    {
        hk_ui_display_draw_text_at(x, y, text, foreground, background);
        return;
    }
    if(y + DRISTY_FONT_H > HK_UI_DISPLAY_HEIGHT ||
       surface_acquire(&surface) != HK_OK)
        return;
    while(*text && x + DRISTY_FONT_W <= HK_UI_DISPLAY_WIDTH)
    {
        surface_glyph_scaled(&surface, x, y, utf8_next(&text),
                             foreground, background, scale);
        x = (uint16_t)(x + DRISTY_FONT_W);
    }
    dirty = clipped_rect(start, y, (uint16_t)(x - start), DRISTY_FONT_H);
    if(dirty.width == 0U)
        (void)hk_display_abort(s_owner, &s_display);
    else
        (void)surface_present(&dirty);
}

void hk_ui_display_draw_text_at(
    uint16_t x, uint16_t y, const char *text,
    uint16_t foreground, uint16_t background)
{
    hk_display_surface_t surface;
    uint16_t start = x;
    hk_display_rect_t dirty;

    if(!text || y + DRISTY_FONT_H > HK_UI_DISPLAY_HEIGHT ||
       surface_acquire(&surface) != HK_OK)
        return;
    while(*text && x + DRISTY_FONT_W <= HK_UI_DISPLAY_WIDTH)
    {
        surface_glyph(&surface, x, y, utf8_next(&text),
                      foreground, background);
        x = (uint16_t)(x + DRISTY_FONT_W);
    }
    dirty = clipped_rect(start, y, (uint16_t)(x - start), DRISTY_FONT_H);
    if(dirty.width == 0U)
        (void)hk_display_abort(s_owner, &s_display);
    else
        (void)surface_present(&dirty);
}

static uint16_t glyph_count(const char *text)
{
    uint16_t count = 0U;
    while(text && *text)
    {
        (void)utf8_next(&text);
        count++;
    }
    return count;
}

void hk_ui_display_draw_text_centered(
    uint16_t y, const char *text,
    uint16_t foreground, uint16_t background)
{
    uint32_t width = (uint32_t)glyph_count(text) * DRISTY_FONT_W;
    uint16_t x = width < HK_UI_DISPLAY_WIDTH ?
        (uint16_t)((HK_UI_DISPLAY_WIDTH - width) / 2U) : 0U;
    hk_ui_display_draw_text_at(x, y, text, foreground, background);
}

void hk_ui_display_draw_boot_logo(void)
{
    dristy_boot_view_show();
}

uint8_t *hk_ui_display_row_buffer(void)
{
    return s_row;
}

void hk_ui_display_write_row(
    uint16_t x, uint16_t y, uint16_t width, const uint8_t *pixels)
{
    hk_display_surface_t surface;
    hk_display_rect_t dirty = clipped_rect(x, y, width, 1U);

    if(!pixels || dirty.width == 0U || surface_acquire(&surface) != HK_OK)
        return;
    memcpy((uint8_t *)surface.pixels.data +
               (uint32_t)y * surface.pixels.stride_bytes + (uint32_t)x * 2U,
           pixels, dirty.width * 2U);
    (void)surface_present(&dirty);
}

uint16_t hk_ui_display_shadow_pixel(uint16_t x, uint16_t y)
{
    hk_display_surface_t surface;
    uint8_t *pixel;
    uint16_t result;

    if(x >= HK_UI_DISPLAY_WIDTH || y >= HK_UI_DISPLAY_HEIGHT ||
       surface_acquire(&surface) != HK_OK)
        return COLOR_BLACK;
    pixel = (uint8_t *)surface.pixels.data +
        (uint32_t)y * surface.pixels.stride_bytes + (uint32_t)x * 2U;
    result = (uint16_t)((uint16_t)pixel[0] << 8) | pixel[1];
    (void)hk_display_abort(s_owner, &s_display);
    return result;
}

uint8_t hk_ui_display_frame_acquire(hk_ui_display_surface_t *surface)
{
    hk_display_surface_t capability_surface;
    uint32_t lease_id;

    if(!surface || s_frame_active ||
       surface_acquire(&capability_surface) != HK_OK)
        return 0U;
    lease_id = ++s_next_frame_lease_id;
    if(lease_id == 0U)
        lease_id = ++s_next_frame_lease_id;
    s_frame_active = 1U;
    s_frame_lease_id = lease_id;
    s_frame_pixels = (uint8_t *)capability_surface.pixels.data;
    s_frame_stride = capability_surface.pixels.stride_bytes;
    *surface = (hk_ui_display_surface_t){
        (uint8_t *)capability_surface.pixels.data,
        (uint16_t)capability_surface.width,
        (uint16_t)capability_surface.height,
        (uint16_t)capability_surface.pixels.stride_bytes,
        lease_id,
    };
    return 1U;
}

uint8_t hk_ui_display_frame_present(uint32_t lease_id)
{
    hk_display_rect_t screen = {
        0, 0, HK_UI_DISPLAY_WIDTH, HK_UI_DISPLAY_HEIGHT,
    };
    hk_result_t result;

    if(!s_frame_active || lease_id == 0U || lease_id != s_frame_lease_id)
        return 0U;
    result = surface_present_now(&screen);
    s_frame_active = 0U;
    s_frame_lease_id = 0U;
    s_frame_pixels = NULL;
    s_frame_stride = 0U;
    return result == HK_OK;
}

uint8_t hk_ui_display_frame_present_regions(
    uint32_t lease_id, const hk_ui_display_rect_t *regions,
    uint16_t region_count)
{
    hk_result_t result = HK_OK;

    if(!s_frame_active || lease_id == 0U || lease_id != s_frame_lease_id ||
       !regions || region_count == 0U ||
       region_count > HK_UI_DISPLAY_FRAME_MAX_DIRTY_RECTS)
        return 0U;
    for(uint16_t index = 0U; index < region_count; index++)
    {
        hk_display_rect_t dirty = clipped_rect(
            regions[index].x, regions[index].y,
            regions[index].width, regions[index].height);

        if(dirty.width == 0U || dirty.height == 0U)
            continue;
        result = hk_display_mark_dirty(s_owner, &s_display, &dirty);
        if(result != HK_OK)
            break;
    }
    if(result == HK_OK)
        result = hk_display_present(
            s_owner, &s_display, present_deadline(), NULL);
    if(result != HK_OK)
        (void)hk_display_abort(s_owner, &s_display);
    s_frame_active = 0U;
    s_frame_lease_id = 0U;
    s_frame_pixels = NULL;
    s_frame_stride = 0U;
    return result == HK_OK;
}

void hk_ui_display_frame_cancel(uint32_t lease_id)
{
    if(!s_frame_active || lease_id == 0U || lease_id != s_frame_lease_id)
        return;
    (void)hk_display_abort(s_owner, &s_display);
    s_frame_active = 0U;
    s_frame_lease_id = 0U;
    s_frame_pixels = NULL;
    s_frame_stride = 0U;
}

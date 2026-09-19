#ifndef HK_UI_DISPLAY_BINDING_H
#define HK_UI_DISPLAY_BINDING_H

#include <stddef.h>
#include <stdint.h>

#include <hackylens/capability/common.h>

#include "hk_font.h"
#include "../config/display_config.h"

#define HK_UI_DISPLAY_WIDTH HK_DISPLAY_REQUIRED_WIDTH
#define HK_UI_DISPLAY_HEIGHT HK_DISPLAY_REQUIRED_HEIGHT
#define HK_UI_DISPLAY_FRAME_MAX_DIRTY_RECTS 8U

typedef struct
{
    uint8_t *rgb565_be;
    uint16_t width;
    uint16_t height;
    uint16_t stride_bytes;
    uint32_t lease_id;
} hk_ui_display_surface_t;

typedef struct
{
    uint16_t x;
    uint16_t y;
    uint16_t width;
    uint16_t height;
} hk_ui_display_rect_t;

hk_result_t hk_ui_display_prepare(void);
void hk_ui_display_draw_boot_logo(void);
void hk_ui_display_fill_rect(
    uint16_t x, uint16_t y, uint16_t width, uint16_t height,
    uint16_t color);
void hk_ui_display_draw_rect(
    uint16_t x, uint16_t y, uint16_t width, uint16_t height,
    uint16_t thickness, uint16_t color);
void hk_ui_display_draw_glyph_at(
    uint16_t x, uint16_t y, uint32_t codepoint,
    uint16_t foreground, uint16_t background);
void hk_ui_display_draw_text_at(
    uint16_t x, uint16_t y, const char *text,
    uint16_t foreground, uint16_t background);
void hk_ui_display_draw_text_scaled_at(
    uint16_t x, uint16_t y, const char *text,
    uint16_t foreground, uint16_t background, uint16_t scale);
void hk_ui_display_draw_dristy_text_at(
    uint16_t x, uint16_t y, const char *text,
    uint16_t foreground, uint16_t background);
void hk_ui_display_draw_dristy_small_text_at(
    uint16_t x, uint16_t y, const char *text,
    uint16_t foreground, uint16_t background);
void hk_ui_display_draw_dristy_text_scaled_at(
    uint16_t x, uint16_t y, const char *text,
    uint16_t foreground, uint16_t background, uint16_t scale);
void hk_ui_display_draw_text_centered(
    uint16_t y, const char *text,
    uint16_t foreground, uint16_t background);
uint8_t *hk_ui_display_row_buffer(void);
void hk_ui_display_write_row(
    uint16_t x, uint16_t y, uint16_t width, const uint8_t *pixels);
uint16_t hk_ui_display_shadow_pixel(uint16_t x, uint16_t y);
uint8_t hk_ui_display_frame_acquire(hk_ui_display_surface_t *surface);
uint8_t hk_ui_display_frame_present(uint32_t lease_id);
uint8_t hk_ui_display_frame_present_regions(
    uint32_t lease_id, const hk_ui_display_rect_t *regions,
    uint16_t region_count);
void hk_ui_display_frame_cancel(uint32_t lease_id);

#endif

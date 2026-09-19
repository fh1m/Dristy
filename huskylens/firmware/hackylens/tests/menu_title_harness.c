#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#include "display_binding.h"
#include "display_config.h"
#include "dristy_theme.h"
#include "hk_app.h"
#include "hk_ui.h"

static uint16_t g_fill_x;
static uint16_t g_fill_y;
static uint16_t g_fill_w;
static uint16_t g_fill_h;
static uint16_t g_fill_color;
static uint8_t g_fill_count;
static uint8_t g_centered_count;

void hk_ui_display_fill_rect(uint16_t x, uint16_t y,
                             uint16_t w, uint16_t h, uint16_t color)
{
    g_fill_x = x;
    g_fill_y = y;
    g_fill_w = w;
    g_fill_h = h;
    g_fill_color = color;
    g_fill_count++;
}

void hk_ui_display_draw_rect(uint16_t x, uint16_t y,
                             uint16_t w, uint16_t h,
                             uint16_t thickness, uint16_t color)
{
    (void)x;
    (void)y;
    (void)w;
    (void)h;
    (void)thickness;
    (void)color;
}

void hk_ui_display_draw_text_at(uint16_t x, uint16_t y, const char *text,
                                uint16_t foreground, uint16_t background)
{
    (void)x;
    (void)y;
    (void)text;
    (void)foreground;
    (void)background;
}

void hk_ui_display_draw_text_centered(uint16_t y, const char *text,
                                      uint16_t foreground, uint16_t background)
{
    (void)y;
    (void)text;
    (void)foreground;
    (void)background;
    g_centered_count++;
}

int main(void)
{
    const uint16_t object_detect_right =
        (HK_DISPLAY_REQUIRED_WIDTH - 13U * HACKYLENS_FONT_W) / 2U +
        13U * HACKYLENS_FONT_W;
    const uint16_t sd_status_x = HK_DISPLAY_REQUIRED_WIDTH - 23U;

    menu_draw_title("PONG");

    assert(g_fill_count == 1U);
    assert(g_fill_x == 52U);
    assert(g_fill_y == 2U);
    assert(g_fill_x + g_fill_w >= object_detect_right);
    assert(g_fill_x + g_fill_w == sd_status_x);
    assert(g_fill_h >= HACKYLENS_FONT_H);
    assert(g_fill_color == DRISTY_COLOR_PANEL);
    assert(g_centered_count == 1U);
    puts("MENU_TITLE_HOST_OK old_object_detect_glyphs_cleared=1");
    return 0;
}

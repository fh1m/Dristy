#include "dristy_ui.h"

#include <string.h>

#include "../config/display_config.h"
#include "../config/menu_layout.h"

#include "dristy_icons.h"
#include "dristy_theme.h"
#include "display_binding.h"

static uint8_t s_sd_mounted;

static uint16_t dristy_small_text_width(const char *text)
{
    return (uint16_t)((text ? strlen(text) : 0U) * DRISTY_UI_FONT_SMALL_W);
}

static uint16_t dristy_body_text_width(const char *text)
{
    return (uint16_t)((text ? strlen(text) : 0U) * DRISTY_FONT_W);
}

static void draw_sd_icon(uint16_t x, uint16_t y)
{
    if(!s_sd_mounted)
        return;
    hk_ui_display_fill_rect(x + 2, y, 10, 2, DRISTY_COLOR_OK);
    hk_ui_display_fill_rect(x + 12, y + 2, 2, 10, DRISTY_COLOR_OK);
    hk_ui_display_fill_rect(x, y + 3, 2, 11, DRISTY_COLOR_OK);
    hk_ui_display_fill_rect(x + 2, y + 12, 12, 2, DRISTY_COLOR_OK);
}

static void draw_footer_pill(uint16_t x, uint16_t y, uint16_t w,
                             const char *label, uint8_t highlight)
{
    const uint16_t ph = 26U;
    uint16_t bg = highlight ? DRISTY_COLOR_ACCENT : DRISTY_COLOR_PANEL_HI;
    uint16_t fg = highlight ? DRISTY_COLOR_TEXT : DRISTY_COLOR_TEXT_DIM;
    uint16_t border = highlight ? DRISTY_COLOR_ACCENT : DRISTY_COLOR_BORDER;
    uint16_t tx;
    uint16_t ty;

    hk_ui_display_fill_rect(x, y, w, ph, bg);
    hk_ui_display_draw_rect(x, y, w, ph, 1U, border);
    tx = (uint16_t)(x + (w > dristy_small_text_width(label) ?
                              (w - dristy_small_text_width(label)) / 2U :
                              6U));
    ty = (uint16_t)(y + (ph - DRISTY_UI_FONT_SMALL_H) / 2U);
    hk_ui_display_draw_dristy_small_text_at(tx, ty, label, fg, bg);
}

void dristy_ui_set_sd_mounted(uint8_t mounted)
{
    s_sd_mounted = mounted ? 1U : 0U;
}

void dristy_ui_draw_shell_bg(void)
{
    hk_ui_display_fill_rect(0, 0, HK_DISPLAY_REQUIRED_WIDTH, HK_DISPLAY_REQUIRED_HEIGHT,
                            DRISTY_COLOR_BG);
}

void dristy_ui_draw_menu_header(const char *title, const char *subtitle)
{
    hk_ui_display_fill_rect(0, 0, HK_DISPLAY_REQUIRED_WIDTH, DRISTY_MENU_BAR_H,
                            DRISTY_COLOR_PANEL);
    hk_ui_display_fill_rect(0, DRISTY_MENU_BAR_H - 2U, HK_DISPLAY_REQUIRED_WIDTH, 2U,
                            DRISTY_COLOR_ACCENT);

    hk_ui_display_draw_dristy_small_text_at(10U, 6U, "DRISTY", DRISTY_COLOR_ACCENT,
                                           DRISTY_COLOR_PANEL);
    if(title && title[0])
    {
        hk_ui_display_draw_dristy_text_at(10U, 22U, title, DRISTY_COLOR_TEXT,
                                          DRISTY_COLOR_PANEL);
    }
    if(subtitle && subtitle[0])
    {
        hk_ui_display_draw_dristy_small_text_at(10U, 44U, subtitle, DRISTY_COLOR_TEXT_DIM,
                                               DRISTY_COLOR_PANEL);
    }
    draw_sd_icon((uint16_t)(HK_DISPLAY_REQUIRED_WIDTH - 24U), 10U);
}

void dristy_ui_draw_menu_footer(uint8_t highlight_btn)
{
    const uint16_t fy = (uint16_t)(HK_DISPLAY_REQUIRED_HEIGHT - DRISTY_MENU_FOOTER_H);
    const uint16_t gap = 8U;
    const uint16_t margin = 8U;
    const uint16_t pw =
        (uint16_t)((HK_DISPLAY_REQUIRED_WIDTH - margin * 2U - gap * 3U) / 4U);
    uint16_t x = margin;
    uint16_t py = (uint16_t)(fy + 5U);

    hk_ui_display_fill_rect(0, fy, HK_DISPLAY_REQUIRED_WIDTH, DRISTY_MENU_FOOTER_H,
                            DRISTY_COLOR_PANEL);
    hk_ui_display_fill_rect(0, fy, HK_DISPLAY_REQUIRED_WIDTH, 1U, DRISTY_COLOR_BORDER);

    draw_footer_pill(x, py, pw, "Prev", highlight_btn == DRISTY_FOOTER_BTN_LEFT);
    x = (uint16_t)(x + pw + gap);
    draw_footer_pill(x, py, pw, "Open", highlight_btn == DRISTY_FOOTER_BTN_OK);
    x = (uint16_t)(x + pw + gap);
    draw_footer_pill(x, py, pw, "Next", highlight_btn == DRISTY_FOOTER_BTN_RIGHT);
    x = (uint16_t)(x + pw + gap);
    draw_footer_pill(x, py, pw, "Page", highlight_btn == DRISTY_FOOTER_BTN_BACK);
}

void dristy_ui_draw_list_scrollbar(uint8_t selected, uint8_t total,
                                   uint8_t scroll_offset, uint8_t visible_rows)
{
    uint16_t x = (uint16_t)(HK_DISPLAY_REQUIRED_WIDTH - 10U);
    uint16_t y0 = DRISTY_LIST_Y0;
    uint16_t slot_h = (uint16_t)(DRISTY_LIST_ROW_H + DRISTY_LIST_ROW_GAP);
    uint16_t h = (uint16_t)(visible_rows * slot_h - DRISTY_LIST_ROW_GAP);
    uint16_t thumb_h;
    uint16_t thumb_y;

    (void)selected;
    if(total <= visible_rows)
        return;

    hk_ui_display_fill_rect(x, y0, 4U, h, DRISTY_COLOR_PANEL);
    thumb_h = (uint16_t)((uint32_t)h * visible_rows / total);
    if(thumb_h < 12U)
        thumb_h = 12U;
    thumb_y = (uint16_t)(y0 +
        (uint32_t)(h - thumb_h) * scroll_offset / (total - visible_rows));
    hk_ui_display_fill_rect(x, thumb_y, 4U, thumb_h, DRISTY_COLOR_ACCENT);
}

void dristy_ui_draw_section_label(uint16_t y, const char *label)
{
    hk_ui_display_fill_rect(8U, y, HK_DISPLAY_REQUIRED_WIDTH - 16U, 18U, DRISTY_COLOR_BG);
    hk_ui_display_draw_dristy_small_text_at(12U, (uint16_t)(y + 2U), label,
                                            DRISTY_COLOR_ACCENT_DIM, DRISTY_COLOR_BG);
}

void dristy_ui_draw_list_row(uint16_t y, const char *app_id,
                             const char *title, const char *subtitle,
                             uint8_t selected)
{
    const uint16_t panel_h = DRISTY_LIST_ROW_H;
    uint16_t fg = DRISTY_COLOR_TEXT;
    uint16_t bg = selected ? DRISTY_COLOR_PANEL_HI : DRISTY_COLOR_PANEL;
    uint16_t ring = selected ? DRISTY_COLOR_ACCENT : DRISTY_COLOR_BORDER;
    uint16_t ix = 12U;
    uint16_t iy = (uint16_t)(y + (panel_h - DRISTY_LIST_ICON) / 2U);
    uint16_t tx = (uint16_t)(ix + DRISTY_LIST_ICON + 14U);
    uint16_t ty = (uint16_t)(y + (panel_h - DRISTY_UI_FONT_H) / 2U);

    (void)subtitle;

    hk_ui_display_fill_rect(8U, y, HK_DISPLAY_REQUIRED_WIDTH - 18U, panel_h, bg);
    hk_ui_display_draw_rect(8U, y, HK_DISPLAY_REQUIRED_WIDTH - 18U, panel_h, 1U, ring);
    if(selected)
    {
        hk_ui_display_fill_rect(8U, y, 4U, panel_h, DRISTY_COLOR_ACCENT);
        hk_ui_display_draw_dristy_text_at((uint16_t)(HK_DISPLAY_REQUIRED_WIDTH - 24U), ty,
                                          ">", DRISTY_COLOR_ACCENT, bg);
    }

    hk_ui_display_fill_rect(ix, iy, DRISTY_LIST_ICON, DRISTY_LIST_ICON, bg);
    hk_ui_display_draw_rect(ix, iy, DRISTY_LIST_ICON, DRISTY_LIST_ICON, 1U,
                            DRISTY_COLOR_BORDER);
    dristy_icon_draw(app_id, ix, iy, DRISTY_LIST_ICON, DRISTY_COLOR_ACCENT, bg);

    if(title)
        hk_ui_display_draw_dristy_text_at(tx, ty, title, fg, bg);
}

void dristy_ui_draw_app_header(const char *title, const char *subtitle)
{
    hk_ui_display_fill_rect(0, 0, HK_DISPLAY_REQUIRED_WIDTH, DRISTY_APP_BAR_H,
                            DRISTY_COLOR_PANEL);
    hk_ui_display_fill_rect(0, DRISTY_APP_BAR_H - 2U, HK_DISPLAY_REQUIRED_WIDTH, 2U,
                            DRISTY_COLOR_ACCENT_DIM);
    hk_ui_display_draw_dristy_text_at(10U, 8U, title ? title : "DRISTY",
                                      DRISTY_COLOR_TEXT, DRISTY_COLOR_PANEL);
    if(subtitle && subtitle[0])
    {
        hk_ui_display_draw_dristy_small_text_at(10U, 34U, subtitle, DRISTY_COLOR_TEXT_DIM,
                                               DRISTY_COLOR_PANEL);
    }
    hk_ui_display_draw_dristy_small_text_at((uint16_t)(HK_DISPLAY_REQUIRED_WIDTH - 52U), 10U,
                                            "BACK", DRISTY_COLOR_ACCENT_DIM,
                                            DRISTY_COLOR_PANEL);
    draw_sd_icon((uint16_t)(HK_DISPLAY_REQUIRED_WIDTH - 24U), 8U);
}

void dristy_ui_draw_app_footer(const char *left, const char *ok, const char *right,
                               const char *back)
{
    const uint16_t fy = (uint16_t)(HK_DISPLAY_REQUIRED_HEIGHT - DRISTY_APP_FOOTER_H);
    const uint16_t ty = (uint16_t)(fy + 6U);

    hk_ui_display_fill_rect(0, fy, HK_DISPLAY_REQUIRED_WIDTH, DRISTY_APP_FOOTER_H,
                            DRISTY_COLOR_PANEL);
    hk_ui_display_fill_rect(0, fy, HK_DISPLAY_REQUIRED_WIDTH, 1U, DRISTY_COLOR_BORDER);
    if(left)
        hk_ui_display_draw_dristy_small_text_at(10U, ty, left, DRISTY_COLOR_TEXT_DIM,
                                               DRISTY_COLOR_PANEL);
    if(ok)
        hk_ui_display_draw_dristy_small_text_at(92U, ty, ok, DRISTY_COLOR_TEXT_DIM,
                                               DRISTY_COLOR_PANEL);
    if(right)
        hk_ui_display_draw_dristy_small_text_at(174U, ty, right, DRISTY_COLOR_TEXT_DIM,
                                               DRISTY_COLOR_PANEL);
    if(back)
        hk_ui_display_draw_dristy_small_text_at(252U, ty, back, DRISTY_COLOR_ACCENT_DIM,
                                               DRISTY_COLOR_PANEL);
}

uint16_t dristy_ui_content_top(void)
{
    return DRISTY_CONTENT_Y0;
}

uint16_t dristy_ui_content_bottom(void)
{
    return (uint16_t)(HK_DISPLAY_REQUIRED_HEIGHT - DRISTY_APP_FOOTER_H);
}

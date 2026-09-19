#include "settings_menu_view.h"

#include <string.h>

#include "../config/display_config.h"
#include "../config/settings_menu_layout.h"
#include "../ui/display_binding.h"
#include "../ui/dristy_theme.h"
#include "../ui/dristy_ui.h"
#include "hk_ui.h"

static void settings_menu_view_clear_slot(uint8_t slot)
{
    uint16_t y;

    if(slot >= SETTINGS_MENU_VISIBLE_ROWS)
        return;
    y = SETTINGS_MENU_ROW_Y0 + slot * SETTINGS_MENU_ROW_H;
    hk_ui_display_fill_rect(6, y, HK_DISPLAY_REQUIRED_WIDTH - 12, SETTINGS_MENU_ROW_H - 2,
                            DRISTY_COLOR_BG);
}

void settings_menu_view_open(const char *title)
{
    dristy_ui_draw_shell_bg();
    dristy_ui_draw_app_header(title ? title : "Settings", "L/R move  OK edit");
    dristy_ui_draw_app_footer("Prev", "Edit", "Next", "Back");
}

void settings_menu_view_clear_rows(void)
{
    for(uint8_t slot = 0U; slot < SETTINGS_MENU_VISIBLE_ROWS; slot++)
        settings_menu_view_clear_slot(slot);
}

void settings_menu_view_draw_row(uint8_t slot,
                                 const char *title,
                                 const char *value,
                                 uint8_t selected,
                                 uint8_t editing)
{
    uint16_t y;
    uint16_t fg;
    uint16_t bg;
    uint16_t value_x;
    size_t value_length;

    if(slot >= SETTINGS_MENU_VISIBLE_ROWS)
        return;
    title = title ? title : "";
    value = value ? value : "";
    selected = selected ? 1U : 0U;
    fg = selected ? DRISTY_COLOR_BG : DRISTY_COLOR_TEXT;
    bg = selected ? DRISTY_COLOR_ACCENT : DRISTY_COLOR_PANEL;
    y = SETTINGS_MENU_ROW_Y0 + slot * SETTINGS_MENU_ROW_H;
    value_length = strlen(value);
    value_x = HK_DISPLAY_REQUIRED_WIDTH - 12U - (uint16_t)(value_length * HACKYLENS_FONT_W);

    hk_ui_display_fill_rect(8, y, HK_DISPLAY_REQUIRED_WIDTH - 16, SETTINGS_MENU_ROW_H - 4, bg);
    hk_ui_display_draw_text_at(12, (uint16_t)(y + (SETTINGS_MENU_ROW_H - HACKYLENS_FONT_H) / 2U),
                               title, fg, bg);
    if(editing)
        hk_ui_display_draw_text_at(value_x - HACKYLENS_FONT_W,
                                   (uint16_t)(y + (SETTINGS_MENU_ROW_H - HACKYLENS_FONT_H) / 2U),
                                   "*", fg, bg);
    hk_ui_display_draw_text_at(value_x,
                               (uint16_t)(y + (SETTINGS_MENU_ROW_H - HACKYLENS_FONT_H) / 2U),
                               value, fg, bg);
}

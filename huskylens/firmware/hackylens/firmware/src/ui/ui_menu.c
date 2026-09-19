#include "hk_ui.h"

#include "../core/hk_app.h"
#include "../core/hk_app_registry.h"

#include "../config/display_config.h"
#include "../config/menu_layout.h"
#include "dristy_app_catalog.h"
#include "dristy_mode_menu.h"
#include "dristy_theme.h"
#include "dristy_ui.h"
#include "display_binding.h"

static uint8_t s_menu_footer_highlight = 0xFFU;

void topbar_set_sd_mounted(uint8_t mounted)
{
    dristy_ui_set_sd_mounted(mounted);
}

void topbar_draw_sd_status(void)
{
    /* SD drawn with menu header */
}

void menu_footer_highlight_button(uint8_t btn_id)
{
    s_menu_footer_highlight = btn_id;
}

void menu_draw_chrome_ex(const char *title, const char *subtitle)
{
    dristy_ui_draw_shell_bg();
    dristy_ui_draw_menu_header(title, subtitle);
    dristy_ui_draw_menu_footer(s_menu_footer_highlight);
    s_menu_footer_highlight = 0xFFU;
}

void menu_draw_chrome(const char *title)
{
    menu_draw_chrome_ex(title, NULL);
}

void menu_draw_title(const char *title)
{
    dristy_ui_draw_menu_header(title, NULL);
}

static uint16_t row_y_for_slot(uint8_t slot)
{
    return (uint16_t)(DRISTY_LIST_Y0 +
                      slot * (DRISTY_LIST_ROW_H + DRISTY_LIST_ROW_GAP));
}

void menu_draw_list_viewport(uint8_t scroll_offset, uint8_t selected_display)
{
    uint8_t total = dristy_menu_sorted_count();
    uint8_t slot;

    for(slot = 0U; slot < MENU_LIST_VISIBLE_ROWS; slot++)
    {
        uint8_t di = (uint8_t)(scroll_offset + slot);
        uint16_t y = row_y_for_slot(slot);
        dristy_app_category_t cat;
        dristy_app_category_t prev_cat;
        dristy_menu_row_t row;

        hk_ui_display_fill_rect(8U, y, HK_DISPLAY_REQUIRED_WIDTH - 16U,
                                DRISTY_LIST_ROW_H + DRISTY_LIST_ROW_GAP,
                                DRISTY_COLOR_BG);
        if(di >= total)
            continue;

        cat = dristy_menu_category_at(di);
        prev_cat = di > 0U ? dristy_menu_category_at((uint8_t)(di - 1U)) : cat;
        if(di == scroll_offset && (di == 0U || cat != prev_cat))
        {
            dristy_ui_draw_section_label((uint16_t)(y - 18U),
                                         dristy_app_category_label(cat));
        }

        dristy_menu_row_at(di, &row);
        dristy_ui_draw_list_row(y, dristy_menu_row_id(di),
                                row.title ? row.title : "",
                                row.subtitle ? row.subtitle : "",
                                (uint8_t)(di == selected_display));
    }

    dristy_ui_draw_list_scrollbar(selected_display, total, scroll_offset,
                                  MENU_LIST_VISIBLE_ROWS);
}

void menu_draw_item_at(uint8_t index, const hk_app_t *app, uint8_t selected)
{
    (void)index;
    (void)app;
    (void)selected;
    /* List uses menu_draw_list_viewport from hk_menu.c */
}

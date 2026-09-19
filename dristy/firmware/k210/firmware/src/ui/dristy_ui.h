#ifndef DRISTY_UI_H
#define DRISTY_UI_H

#include <stdint.h>

typedef enum
{
    DRISTY_FOOTER_BTN_LEFT = 0,
    DRISTY_FOOTER_BTN_OK,
    DRISTY_FOOTER_BTN_RIGHT,
    DRISTY_FOOTER_BTN_BACK,
    DRISTY_FOOTER_BTN_COUNT
} dristy_footer_btn_t;

void dristy_ui_set_sd_mounted(uint8_t mounted);
void dristy_ui_draw_shell_bg(void);

void dristy_ui_draw_menu_header(const char *title, const char *subtitle);
void dristy_ui_draw_menu_footer(uint8_t highlight_btn);
void dristy_ui_draw_list_scrollbar(uint8_t selected, uint8_t total, uint8_t scroll_offset,
                                   uint8_t visible_rows);

void dristy_ui_draw_list_row(uint16_t y, const char *app_id,
                             const char *title, const char *subtitle,
                             uint8_t selected);

void dristy_ui_draw_section_label(uint16_t y, const char *label);

void dristy_ui_draw_app_header(const char *title, const char *subtitle);
void dristy_ui_draw_app_footer(const char *left, const char *ok, const char *right,
                               const char *back);
uint16_t dristy_ui_content_top(void);
uint16_t dristy_ui_content_bottom(void);

#endif

#ifndef HK_UI_H
#define HK_UI_H

#include "../core/hk_app.h"


void menu_draw_chrome(const char *title);
void menu_draw_chrome_ex(const char *title, const char *subtitle);
void menu_draw_title(const char *title);
void menu_draw_item_at(uint8_t index, const hk_app_t *app, uint8_t selected);
void menu_draw_list_viewport(uint8_t scroll_offset, uint8_t selected_display);
void menu_footer_highlight_button(uint8_t btn_id);
void topbar_set_sd_mounted(uint8_t mounted);
void topbar_draw_sd_status(void);
void image_fit_viewport(uint16_t src_w, uint16_t src_h, uint16_t *dst_x, uint16_t *dst_y, uint16_t *dst_w, uint16_t *dst_h);
#endif

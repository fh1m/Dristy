#ifndef HK_SETTINGS_MENU_VIEW_H
#define HK_SETTINGS_MENU_VIEW_H

#include <stdint.h>

void settings_menu_view_open(const char *title);
void settings_menu_view_clear_rows(void);
void settings_menu_view_draw_row(uint8_t slot,
                                 const char *title,
                                 const char *value,
                                 uint8_t selected,
                                 uint8_t editing);

#endif

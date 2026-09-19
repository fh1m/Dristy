#ifndef HK_TERMINAL_VIEW_H
#define HK_TERMINAL_VIEW_H

#include "terminal_types.h"

terminal_geometry_t terminal_view_geometry(terminal_font_size_t font_size);
void terminal_view_render(terminal_font_size_t font_size);
void terminal_view_draw_icon(uint16_t x, uint16_t y, uint16_t color, uint16_t bg);

#endif

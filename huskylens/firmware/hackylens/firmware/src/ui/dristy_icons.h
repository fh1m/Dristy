#ifndef DRISTY_ICONS_H
#define DRISTY_ICONS_H

#include <stdint.h>

/* Draw a 48×48 (within MENU_ICON cell) glyph for the given app id. */
void dristy_icon_draw(const char *app_id,
                      uint16_t x, uint16_t y, uint16_t box,
                      uint16_t fg, uint16_t bg);

#endif

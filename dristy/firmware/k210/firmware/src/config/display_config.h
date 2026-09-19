#ifndef HK_DISPLAY_CONFIG_H
#define HK_DISPLAY_CONFIG_H

#include "dristy_font_1bpp.h"
#include "dristy_font_cyrillic_1bpp.h"

#define HK_DISPLAY_REQUIRED_WIDTH 320
#define HK_DISPLAY_REQUIRED_HEIGHT 240

#define COLOR_BLACK 0x0000
#define COLOR_WHITE 0xFFFF
#define COLOR_TERM_GREEN 0x07E0

#define TERM_COLS (HK_DISPLAY_REQUIRED_WIDTH / DRISTY_FONT_W)

#endif

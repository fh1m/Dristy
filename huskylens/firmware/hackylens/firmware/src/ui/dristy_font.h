#ifndef DRISTY_FONT_H
#define DRISTY_FONT_H

#include <stdint.h>

#include "dristy_font_1bpp.h"

const uint8_t *dristy_font_glyph(uint32_t codepoint);
const uint8_t *dristy_font_small_glyph(uint32_t codepoint);

#endif

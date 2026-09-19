#include "dristy_font.h"

const uint8_t *dristy_font_glyph(uint32_t codepoint)
{
    uint32_t index;

    if(codepoint < DRISTY_FONT_FIRST || codepoint > DRISTY_FONT_LAST)
        codepoint = '?';
    index = codepoint - DRISTY_FONT_FIRST;
    return &g_dristy_font_1bpp[index * DRISTY_FONT_H * DRISTY_FONT_ROW_BYTES];
}

const uint8_t *dristy_font_small_glyph(uint32_t codepoint)
{
    uint32_t index;

    if(codepoint < DRISTY_FONT_FIRST || codepoint > DRISTY_FONT_LAST)
        codepoint = '?';
    index = codepoint - DRISTY_FONT_FIRST;
    return &g_dristy_font_small_1bpp[index * DRISTY_FONT_SMALL_H * DRISTY_FONT_SMALL_ROW_BYTES];
}

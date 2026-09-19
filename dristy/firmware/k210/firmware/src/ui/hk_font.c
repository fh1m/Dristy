#include "hk_font.h"

#include "dristy_font_1bpp.h"
#include "dristy_font_cyrillic_1bpp.h"

const uint8_t *hk_font_glyph(uint32_t codepoint)
{
    uint32_t glyph_index;

    if(codepoint == DRISTY_CYRILLIC_YO_UPPER)
        glyph_index = 0U;
    else if(codepoint >= DRISTY_CYRILLIC_FIRST &&
            codepoint <= DRISTY_CYRILLIC_LAST)
        glyph_index = 1U + codepoint - DRISTY_CYRILLIC_FIRST;
    else if(codepoint == DRISTY_CYRILLIC_YO_LOWER)
        glyph_index = DRISTY_CYRILLIC_COUNT - 1U;
    else
    {
        if(codepoint < DRISTY_FONT_FIRST || codepoint > DRISTY_FONT_LAST)
            codepoint = '?';
        glyph_index = codepoint - DRISTY_FONT_FIRST;
        return &g_dristy_font_1bpp[
            glyph_index * DRISTY_FONT_H * DRISTY_FONT_ROW_BYTES];
    }
    return &g_dristy_font_cyrillic_1bpp[
        glyph_index * DRISTY_FONT_H * DRISTY_FONT_ROW_BYTES];
}

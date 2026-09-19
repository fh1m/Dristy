#include "hk_font.h"

#include "hackylens_font_1bpp.h"
#include "hackylens_font_cyrillic_1bpp.h"

const uint8_t *hk_font_glyph(uint32_t codepoint)
{
    uint32_t glyph_index;

    if(codepoint == HACKYLENS_CYRILLIC_YO_UPPER)
        glyph_index = 0U;
    else if(codepoint >= HACKYLENS_CYRILLIC_FIRST &&
            codepoint <= HACKYLENS_CYRILLIC_LAST)
        glyph_index = 1U + codepoint - HACKYLENS_CYRILLIC_FIRST;
    else if(codepoint == HACKYLENS_CYRILLIC_YO_LOWER)
        glyph_index = HACKYLENS_CYRILLIC_COUNT - 1U;
    else
    {
        if(codepoint < HACKYLENS_FONT_FIRST || codepoint > HACKYLENS_FONT_LAST)
            codepoint = '?';
        glyph_index = codepoint - HACKYLENS_FONT_FIRST;
        return &g_hackylens_font_1bpp[
            glyph_index * HACKYLENS_FONT_H * HACKYLENS_FONT_ROW_BYTES];
    }
    return &g_hackylens_font_cyrillic_1bpp[
        glyph_index * HACKYLENS_FONT_H * HACKYLENS_FONT_ROW_BYTES];
}

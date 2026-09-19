#include "hk_string.h"

static size_t utf8_encode(uint32_t codepoint, char *output, size_t capacity)
{
    if(codepoint <= 0x7FU)
    {
        if(capacity < 1U)
            return 0;
        output[0] = (char)codepoint;
        return 1;
    }
    if(codepoint <= 0x7FFU)
    {
        if(capacity < 2U)
            return 0;
        output[0] = (char)(0xC0U | (codepoint >> 6));
        output[1] = (char)(0x80U | (codepoint & 0x3FU));
        return 2;
    }
    if(codepoint <= 0xFFFFU)
    {
        if(capacity < 3U)
            return 0;
        output[0] = (char)(0xE0U | (codepoint >> 12));
        output[1] = (char)(0x80U | ((codepoint >> 6) & 0x3FU));
        output[2] = (char)(0x80U | (codepoint & 0x3FU));
        return 3;
    }
    if(codepoint <= 0x10FFFFU)
    {
        if(capacity < 4U)
            return 0;
        output[0] = (char)(0xF0U | (codepoint >> 18));
        output[1] = (char)(0x80U | ((codepoint >> 12) & 0x3FU));
        output[2] = (char)(0x80U | ((codepoint >> 6) & 0x3FU));
        output[3] = (char)(0x80U | (codepoint & 0x3FU));
        return 4;
    }
    return 0;
}

char ascii_lower(char c)
{
    if(c >= 'A' && c <= 'Z')
        return (char)(c + ('a' - 'A'));
    return c;
}

uint8_t str_eq_ci(const char *a, const char *b)
{
    while(*a && *b)
    {
        if(ascii_lower(*a++) != ascii_lower(*b++))
            return 0;
    }
    return *a == '\0' && *b == '\0';
}

size_t utf16_to_utf8(const uint16_t *input, size_t input_units, char *output, size_t output_size)
{
    size_t used = 0;

    if(!output || output_size == 0U)
        return 0;
    for(size_t i = 0; input && i < input_units; i++)
    {
        uint32_t codepoint = input[i];
        size_t encoded;

        if(codepoint == 0U || codepoint == 0xFFFFU)
            break;
        if(codepoint >= 0xD800U && codepoint <= 0xDBFFU && i + 1U < input_units &&
           input[i + 1U] >= 0xDC00U && input[i + 1U] <= 0xDFFFU)
        {
            codepoint = 0x10000U + ((codepoint - 0xD800U) << 10) +
                        (input[++i] - 0xDC00U);
        }
        else if(codepoint >= 0xD800U && codepoint <= 0xDFFFU)
        {
            codepoint = '?';
        }
        encoded = utf8_encode(codepoint, output + used, output_size - used - 1U);
        if(encoded == 0U)
            break;
        used += encoded;
    }
    output[used] = '\0';
    return used;
}

uint32_t utf8_next(const char **cursor)
{
    const uint8_t *text;
    uint32_t codepoint;
    uint8_t needed;

    if(!cursor || !*cursor || !**cursor)
        return 0;
    text = (const uint8_t *)*cursor;
    if(text[0] < 0x80U)
    {
        *cursor += 1;
        return text[0];
    }
    if((text[0] & 0xE0U) == 0xC0U)
    {
        codepoint = text[0] & 0x1FU;
        needed = 1;
    }
    else if((text[0] & 0xF0U) == 0xE0U)
    {
        codepoint = text[0] & 0x0FU;
        needed = 2;
    }
    else if((text[0] & 0xF8U) == 0xF0U)
    {
        codepoint = text[0] & 0x07U;
        needed = 3;
    }
    else
    {
        *cursor += 1;
        return '?';
    }
    for(uint8_t i = 1; i <= needed; i++)
    {
        if(text[i] == 0U || (text[i] & 0xC0U) != 0x80U)
        {
            *cursor += 1;
            return '?';
        }
        codepoint = (codepoint << 6) | (text[i] & 0x3FU);
    }
    *cursor += needed + 1U;
    return codepoint;
}

size_t utf8_glyph_count(const char *text)
{
    size_t count = 0;

    while(text && *text)
    {
        (void)utf8_next(&text);
        count++;
    }
    return count;
}

size_t utf8_copy_glyphs(char *output, size_t output_size, const char *text, size_t max_glyphs)
{
    size_t used = 0;
    size_t glyphs = 0;

    if(!output || output_size == 0U)
        return 0;
    while(text && *text && glyphs < max_glyphs)
    {
        const char *next = text;
        size_t bytes;

        (void)utf8_next(&next);
        bytes = (size_t)(next - text);
        if(used + bytes >= output_size)
            break;
        for(size_t i = 0; i < bytes; i++)
            output[used++] = text[i];
        text = next;
        glyphs++;
    }
    output[used] = '\0';
    return used;
}

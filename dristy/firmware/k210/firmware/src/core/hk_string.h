#ifndef HK_STRING_H
#define HK_STRING_H

#include <stddef.h>
#include <stdint.h>

char ascii_lower(char c);
uint8_t str_eq_ci(const char *a, const char *b);
size_t utf16_to_utf8(const uint16_t *input, size_t input_units, char *output, size_t output_size);
uint32_t utf8_next(const char **cursor);
size_t utf8_glyph_count(const char *text);
size_t utf8_copy_glyphs(char *output, size_t output_size, const char *text, size_t max_glyphs);

#endif

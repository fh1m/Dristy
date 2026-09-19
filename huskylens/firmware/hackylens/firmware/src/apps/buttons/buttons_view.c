#include "buttons_view.h"

#include <stdio.h>
#include <string.h>

#include "../../config/display_config.h"
#include "../../config/input_config.h"
#include "../../ui/display_binding.h"
#include "../../ui/hk_ui.h"

#define BUTTONS_FONT_SCALE 2U
#define BUTTONS_FONT_W (HACKYLENS_FONT_W / BUTTONS_FONT_SCALE)
#define BUTTONS_FONT_H (HACKYLENS_FONT_H / BUTTONS_FONT_SCALE)

static uint8_t buttons_view_small_pixel(
    char character, uint16_t x, uint16_t y)
{
    const uint8_t *glyph = hk_font_glyph((uint8_t)character);

    for(uint16_t dy = 0U; dy < BUTTONS_FONT_SCALE; dy++)
    {
        for(uint16_t dx = 0U; dx < BUTTONS_FONT_SCALE; dx++)
        {
            uint16_t source_x = x * BUTTONS_FONT_SCALE + dx;
            uint16_t source_y = y * BUTTONS_FONT_SCALE + dy;
            uint8_t bit = glyph[
                source_y * HACKYLENS_FONT_ROW_BYTES + source_x / 8U] &
                (uint8_t)(0x80U >> (source_x & 7U));

            if(bit)
                return 1U;
        }
    }
    return 0U;
}

static void buttons_view_draw_small_text(
    uint16_t x, uint16_t y, const char *text, uint16_t foreground)
{
    uint16_t width = HK_DISPLAY_REQUIRED_WIDTH - x - 8U;
    size_t length = strlen(text);

    for(uint16_t row = 0U; row < BUTTONS_FONT_H; row++)
    {
        uint8_t *pixels = hk_ui_display_row_buffer();

        for(uint16_t column = 0U; column < width; column++)
        {
            size_t character = column / BUTTONS_FONT_W;
            uint16_t color = character < length &&
                buttons_view_small_pixel(
                    text[character], column % BUTTONS_FONT_W, row) ?
                foreground : COLOR_BLACK;

            pixels[column * 2U] = (uint8_t)(color >> 8);
            pixels[column * 2U + 1U] = (uint8_t)color;
        }
        hk_ui_display_write_row(x, (uint16_t)(y + row), width, pixels);
    }
}

static const char *buttons_view_status(
    const buttons_view_state_t *state, uint32_t mask)
{
    if(state->repeat_error & mask)
        return "REPEAT";
    if(state->state & mask)
        return (state->hold_passed & mask) ? "HOLD" : "PRESS";
    if(state->passed & mask)
        return "PASS";
    return "WAIT";
}

static void buttons_view_draw_row(
    uint16_t y, const char *name, uint32_t mask, uint8_t index,
    const buttons_view_state_t *state)
{
    char line[40];

    snprintf(
        line, sizeof(line), "%-5s %s P:%02u R:%02u %-6s", name,
        (state->state & mask) ? "DN" : "UP",
        (unsigned)state->pressed_count[index],
        (unsigned)state->released_count[index],
        buttons_view_status(state, mask));
    buttons_view_draw_small_text(
        12, y, line,
        (state->repeat_error & mask) ? COLOR_WHITE : COLOR_TERM_GREEN);
}

static void buttons_view_draw_footer(const buttons_view_state_t *state)
{
    char line[40];
    uint8_t passed = 0U;

    for(uint8_t index = 0U; index < 4U; index++)
        if(state->passed & (UINT32_C(1) << index))
            passed++;
    snprintf(
        line, sizeof(line), "RESULT: %u/4%s", (unsigned)passed,
        state->repeat_error ? "  REPEAT ERROR" : "");
    buttons_view_draw_small_text(12, 184, line, COLOR_TERM_GREEN);
    buttons_view_draw_small_text(
        12, 207, "HOLD OK+BACK 1S TO EXIT", COLOR_TERM_GREEN);
}

void buttons_view_render(const buttons_view_state_t *state)
{
    menu_draw_chrome("BUTTON TEST");
    buttons_view_draw_row(42, "LEFT", BUTTON_LEFT, 0U, state);
    buttons_view_draw_row(74, "OK", BUTTON_OK, 1U, state);
    buttons_view_draw_row(106, "RIGHT", BUTTON_RIGHT, 2U, state);
    buttons_view_draw_row(138, "BACK", BUTTON_BACK, 3U, state);
    buttons_view_draw_footer(state);
}

void buttons_view_update(
    const buttons_view_state_t *state, uint32_t changed, uint8_t footer_changed)
{
    if(changed & BUTTON_LEFT)
        buttons_view_draw_row(42, "LEFT", BUTTON_LEFT, 0U, state);
    if(changed & BUTTON_OK)
        buttons_view_draw_row(74, "OK", BUTTON_OK, 1U, state);
    if(changed & BUTTON_RIGHT)
        buttons_view_draw_row(106, "RIGHT", BUTTON_RIGHT, 2U, state);
    if(changed & BUTTON_BACK)
        buttons_view_draw_row(138, "BACK", BUTTON_BACK, 3U, state);
    if(footer_changed)
        buttons_view_draw_footer(state);
}

void buttons_view_draw_icon(uint16_t x, uint16_t y, uint16_t color, uint16_t bg)
{
    (void)bg;
    hk_ui_display_draw_rect(x + 13, y + 12, 14, 14, 2, color);
    hk_ui_display_draw_rect(x + 33, y + 12, 14, 14, 2, color);
    hk_ui_display_draw_rect(x + 13, y + 34, 14, 14, 2, color);
    hk_ui_display_draw_rect(x + 33, y + 34, 14, 14, 2, color);
}

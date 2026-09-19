#include "terminal_view.h"

#include "../../config/display_config.h"
#include "../../ui/dristy_theme.h"
#include "../../ui/display_binding.h"
#include "terminal_buffer.h"
#include "terminal_config.h"

#define TERMINAL_MAX_VIEW_COLUMNS (HK_DISPLAY_REQUIRED_WIDTH / (DRISTY_FONT_W / TERMINAL_SMALL_SCALE))

terminal_geometry_t terminal_view_geometry(terminal_font_size_t font_size)
{
    uint16_t scale = font_size == TERMINAL_FONT_SMALL ? TERMINAL_SMALL_SCALE : TERMINAL_NORMAL_SCALE;
    terminal_geometry_t geometry = {
        .columns = (uint16_t)(HK_DISPLAY_REQUIRED_WIDTH / (DRISTY_FONT_W / scale)),
        .rows = (uint16_t)(HK_DISPLAY_REQUIRED_HEIGHT / (DRISTY_FONT_H / scale)),
        .cell_width = (uint16_t)(DRISTY_FONT_W / scale),
        .cell_height = (uint16_t)(DRISTY_FONT_H / scale),
    };

    return geometry;
}

static uint8_t terminal_glyph_pixel(char c, uint16_t x, uint16_t y, uint16_t scale)
{
    const uint8_t *glyph = hk_font_glyph(c);

    for(uint16_t dy = 0U; dy < scale; dy++)
    {
        for(uint16_t dx = 0U; dx < scale; dx++)
        {
            uint16_t source_x = x * scale + dx;
            uint16_t source_y = y * scale + dy;
            uint8_t bit = glyph[source_y * DRISTY_FONT_ROW_BYTES + source_x / 8U] &
                          (uint8_t)(0x80U >> (source_x & 7U));
            if(bit)
                return 1U;
        }
    }
    return 0U;
}

static uint8_t terminal_indicator_pixel(uint16_t x,
                                        uint16_t y,
                                        uint16_t content_width,
                                        const terminal_buffer_status_t *status,
                                        uint16_t *color)
{
    const uint16_t track_y = 2U;
    const uint16_t track_h = HK_DISPLAY_REQUIRED_HEIGHT - 4U;
    uint16_t indicator_x;
    uint16_t thumb_h;
    uint16_t thumb_y;
    uint32_t top;

    if(content_width >= HK_DISPLAY_REQUIRED_WIDTH || !status)
        return 0U;
    indicator_x = (uint16_t)(content_width + (HK_DISPLAY_REQUIRED_WIDTH - content_width - 2U) / 2U);
    if(x < indicator_x || x >= indicator_x + 2U || y < track_y || y >= track_y + track_h)
        return 0U;

    if(status->max_scroll_offset == 0U)
    {
        if(y >= track_y + track_h - 4U)
        {
            *color = DRISTY_COLOR_ACCENT;
            return 1U;
        }
        return 0U;
    }

    thumb_h = (uint16_t)(((uint32_t)track_h * status->visible_rows) / status->total_rows);
    if(thumb_h < 4U)
        thumb_h = 4U;
    if(thumb_h > track_h)
        thumb_h = track_h;
    top = status->max_scroll_offset - status->scroll_offset;
    thumb_y = (uint16_t)(track_y + (top * (track_h - thumb_h)) / status->max_scroll_offset);
    if(y >= thumb_y && y < thumb_y + thumb_h)
    {
        *color = status->auto_follow ? DRISTY_COLOR_ACCENT : DRISTY_COLOR_TEXT_DIM;
        return 1U;
    }
    if(x == indicator_x && (y == track_y || y == track_y + track_h - 1U))
    {
        *color = DRISTY_COLOR_BORDER;
        return 1U;
    }
    return 0U;
}

void terminal_view_render(terminal_font_size_t font_size)
{
    terminal_geometry_t geometry = terminal_view_geometry(font_size);
    terminal_buffer_status_t status = terminal_buffer_status();
    uint16_t scale = font_size == TERMINAL_FONT_SMALL ? TERMINAL_SMALL_SCALE : TERMINAL_NORMAL_SCALE;
    uint16_t content_width = geometry.columns * geometry.cell_width;
    char row_text[TERMINAL_MAX_VIEW_COLUMNS + 1U];
    uint16_t loaded_row = UINT16_MAX;

    for(uint16_t y = 0U; y < HK_DISPLAY_REQUIRED_HEIGHT; y++)
    {
        uint8_t *pixels = hk_ui_display_row_buffer();
        uint16_t row = y / geometry.cell_height;

        if(row != loaded_row)
        {
            terminal_buffer_visible_row(row, row_text, (size_t)geometry.columns + 1U);
            loaded_row = row;
        }
        for(uint16_t x = 0U; x < HK_DISPLAY_REQUIRED_WIDTH; x++)
        {
            uint16_t color = COLOR_BLACK;
            uint16_t col = x / geometry.cell_width;

            if(col < geometry.columns && row < geometry.rows &&
               terminal_glyph_pixel(row_text[col],
                                    x % geometry.cell_width,
                                    y % geometry.cell_height,
                                    scale))
                color = DRISTY_COLOR_TEXT;
            terminal_indicator_pixel(x, y, content_width, &status, &color);
            pixels[x * 2U] = (uint8_t)(color >> 8);
            pixels[x * 2U + 1U] = (uint8_t)(color & 0xFFU);
        }
        hk_ui_display_write_row(
            0U, y, HK_DISPLAY_REQUIRED_WIDTH, pixels);
    }
}

void terminal_view_draw_icon(uint16_t x, uint16_t y, uint16_t color, uint16_t bg)
{
    (void)bg;
    hk_ui_display_draw_rect(x + 8U, y + 12U, 44U, 36U, 2U, color);
    hk_ui_display_fill_rect(x + 10U, y + 18U, 40U, 2U, color);
    hk_ui_display_fill_rect(x + 15U, y + 26U, 2U, 2U, color);
    hk_ui_display_fill_rect(x + 17U, y + 28U, 2U, 2U, color);
    hk_ui_display_fill_rect(x + 15U, y + 30U, 2U, 2U, color);
    hk_ui_display_fill_rect(x + 24U, y + 31U, 10U, 2U, color);
    hk_ui_display_fill_rect(x + 38U, y + 29U, 3U, 7U, color);
    hk_ui_display_fill_rect(x + 15U, y + 39U, 26U, 2U, color);
    hk_ui_display_fill_rect(x + 15U, y + 43U, 18U, 2U, color);
}

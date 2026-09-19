#include "terminal_buffer.h"

#include <string.h>

#include "terminal_config.h"

typedef struct
{
    char text[TERMINAL_HISTORY_LINE_LENGTH + 1U];
    uint16_t length;
} terminal_line_t;

static terminal_line_t g_lines[TERMINAL_HISTORY_LINE_COUNT];
static uint16_t g_head;
static uint16_t g_count;
static uint16_t g_columns;
static uint16_t g_rows;
static uint32_t g_scroll_offset;
static uint8_t g_auto_follow;

static terminal_line_t *terminal_line_at(uint16_t index)
{
    return &g_lines[(g_head + index) % TERMINAL_HISTORY_LINE_COUNT];
}

static uint32_t terminal_line_rows(const terminal_line_t *line, uint16_t columns)
{
    if(!line || columns == 0U || line->length == 0U)
        return 1U;
    return ((uint32_t)line->length + columns - 1U) / columns;
}

static uint32_t terminal_total_rows_for(uint16_t columns)
{
    uint32_t total = 0U;

    for(uint16_t i = 0; i < g_count; i++)
        total += terminal_line_rows(terminal_line_at(i), columns);
    return total;
}

static uint32_t terminal_max_scroll_offset(void)
{
    uint32_t total = terminal_total_rows_for(g_columns);

    return total > g_rows ? total - g_rows : 0U;
}

static void terminal_clamp_viewport(void)
{
    uint32_t maximum = terminal_max_scroll_offset();

    if(g_scroll_offset > maximum)
        g_scroll_offset = maximum;
    if(g_auto_follow)
        g_scroll_offset = 0U;
}

static void terminal_note_added_rows(uint32_t rows)
{
    if(!g_auto_follow)
        g_scroll_offset += rows;
}

static terminal_line_t *terminal_append_line(void)
{
    terminal_line_t *line;

    if(g_count < TERMINAL_HISTORY_LINE_COUNT)
    {
        line = terminal_line_at(g_count);
        g_count++;
    }
    else
    {
        g_head = (uint16_t)((g_head + 1U) % TERMINAL_HISTORY_LINE_COUNT);
        line = terminal_line_at((uint16_t)(g_count - 1U));
    }
    memset(line, 0, sizeof(*line));
    terminal_note_added_rows(1U);
    return line;
}

static void terminal_buffer_putc(char c)
{
    terminal_line_t *line = terminal_line_at((uint16_t)(g_count - 1U));
    uint32_t before;
    uint32_t after;

    if(c == '\r')
        return;
    if(c == '\n')
    {
        terminal_append_line();
        terminal_clamp_viewport();
        return;
    }
    if(c == '\t')
    {
        for(uint8_t i = 0; i < 4U; i++)
            terminal_buffer_putc(' ');
        return;
    }
    if((unsigned char)c < ' ' || (unsigned char)c > '~')
        c = '?';

    if(line->length >= TERMINAL_HISTORY_LINE_LENGTH)
        line = terminal_append_line();

    before = terminal_line_rows(line, g_columns);
    line->text[line->length++] = c;
    line->text[line->length] = '\0';
    after = terminal_line_rows(line, g_columns);
    if(after > before)
        terminal_note_added_rows(after - before);
    terminal_clamp_viewport();
}

void terminal_buffer_init(uint16_t columns, uint16_t rows)
{
    memset(g_lines, 0, sizeof(g_lines));
    g_head = 0U;
    g_count = 1U;
    g_columns = columns ? columns : 1U;
    g_rows = rows ? rows : 1U;
    g_scroll_offset = 0U;
    g_auto_follow = 1U;
}

void terminal_buffer_set_geometry(uint16_t columns, uint16_t rows)
{
    uint32_t old_top;
    uint32_t remaining;
    uint32_t anchor_char = 0U;
    uint32_t desired_top = 0U;
    uint32_t maximum;
    uint16_t anchor_line = 0U;

    columns = columns ? columns : 1U;
    rows = rows ? rows : 1U;
    if(columns == g_columns && rows == g_rows)
        return;

    maximum = terminal_max_scroll_offset();
    old_top = maximum - (g_scroll_offset > maximum ? maximum : g_scroll_offset);
    remaining = old_top;
    for(anchor_line = 0U; anchor_line < g_count; anchor_line++)
    {
        uint32_t line_rows = terminal_line_rows(terminal_line_at(anchor_line), g_columns);
        if(remaining < line_rows)
        {
            anchor_char = remaining * g_columns;
            break;
        }
        remaining -= line_rows;
    }
    if(anchor_line >= g_count)
        anchor_line = (uint16_t)(g_count - 1U);

    g_columns = columns;
    g_rows = rows;
    if(g_auto_follow)
    {
        g_scroll_offset = 0U;
        return;
    }

    for(uint16_t i = 0U; i < anchor_line; i++)
        desired_top += terminal_line_rows(terminal_line_at(i), g_columns);
    desired_top += anchor_char / g_columns;
    maximum = terminal_max_scroll_offset();
    if(desired_top > maximum)
        desired_top = maximum;
    g_scroll_offset = maximum - desired_top;
}

void terminal_buffer_write(const char *text)
{
    if(!text)
        return;
    while(*text)
        terminal_buffer_putc(*text++);
}

void terminal_buffer_scroll_up(void)
{
    uint32_t maximum = terminal_max_scroll_offset();

    if(g_scroll_offset < maximum)
    {
        g_scroll_offset++;
        g_auto_follow = 0U;
    }
}

void terminal_buffer_scroll_down(void)
{
    if(g_scroll_offset > 0U)
        g_scroll_offset--;
    if(g_scroll_offset == 0U)
        g_auto_follow = 1U;
}

void terminal_buffer_follow_latest(void)
{
    g_scroll_offset = 0U;
    g_auto_follow = 1U;
}

void terminal_buffer_visible_row(uint16_t row, char *text, size_t text_size)
{
    uint32_t maximum;
    uint32_t target;

    if(!text || text_size == 0U)
        return;
    memset(text, ' ', text_size - 1U);
    text[text_size - 1U] = '\0';

    maximum = terminal_max_scroll_offset();
    target = maximum - (g_scroll_offset > maximum ? maximum : g_scroll_offset) + row;
    for(uint16_t i = 0U; i < g_count; i++)
    {
        const terminal_line_t *line = terminal_line_at(i);
        uint32_t line_rows = terminal_line_rows(line, g_columns);

        if(target < line_rows)
        {
            uint32_t start = target * g_columns;
            size_t available = line->length > start ? (size_t)(line->length - start) : 0U;
            size_t copy = available < g_columns ? available : g_columns;

            if(copy > text_size - 1U)
                copy = text_size - 1U;
            if(copy > 0U)
                memcpy(text, &line->text[start], copy);
            return;
        }
        target -= line_rows;
    }
}

terminal_buffer_status_t terminal_buffer_status(void)
{
    terminal_buffer_status_t status = {
        .total_rows = terminal_total_rows_for(g_columns),
        .scroll_offset = g_scroll_offset,
        .max_scroll_offset = terminal_max_scroll_offset(),
        .visible_rows = g_rows,
        .auto_follow = g_auto_follow,
    };

    return status;
}

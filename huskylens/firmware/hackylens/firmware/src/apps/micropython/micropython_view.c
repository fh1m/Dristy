#include "micropython_view.h"

#include <stdio.h>
#include <string.h>

#include "../../config/display_config.h"
#include "../../core/hk_string.h"
#include "../../ui/display_binding.h"
#include "../../ui/hk_ui.h"

#define PY_INFO_Y 30U
#define PY_INFO_H 22U
#define PY_BODY_Y 53U
#define PY_BODY_H 187U
#define PY_TEXT_X 8U
#define PY_TEXT_COLUMNS 25U
#define PY_LIST_ROW_H 24U
#define PY_LIST_ROW_PITCH 26U
#define PY_ACTION_ROW_Y0 57U
#define PY_ACTION_ROW_H 28U
#define PY_ACTION_ROW_PITCH 34U
#define PY_TEXT_ROW_PITCH 20U
#define PY_FONT_W 12U
#define PY_FONT_H 20U
#define PY_MAX_GLYPHS (HK_DISPLAY_REQUIRED_WIDTH / PY_FONT_W)

static uint32_t small_text_next(const char **text)
{
    const uint8_t *cursor = (const uint8_t *)*text;
    uint8_t first = *cursor++;
    uint32_t codepoint;
    uint8_t required;

    if(first < 0x80U)
    {
        *text = (const char *)cursor;
        return first;
    }
    if((first & 0xE0U) == 0xC0U)
    {
        codepoint = first & 0x1FU;
        required = 1U;
    }
    else if((first & 0xF0U) == 0xE0U)
    {
        codepoint = first & 0x0FU;
        required = 2U;
    }
    else
    {
        codepoint = first & 0x07U;
        required = 3U;
    }
    while(required-- && (*cursor & 0xC0U) == 0x80U)
        codepoint = (codepoint << 6) | (*cursor++ & 0x3FU);
    *text = (const char *)cursor;
    return codepoint;
}

static uint8_t small_glyph_coverage(uint32_t codepoint, uint16_t x,
                                    uint16_t y)
{
    const uint8_t *glyph = hk_font_glyph(codepoint);
    uint16_t source_x0 = (uint16_t)(x * HACKYLENS_FONT_W / PY_FONT_W);
    uint16_t source_x1 = (uint16_t)((x + 1U) * HACKYLENS_FONT_W /
                                    PY_FONT_W);
    uint16_t source_y0 = (uint16_t)(y * HACKYLENS_FONT_H / PY_FONT_H);
    uint16_t source_y1 = (uint16_t)((y + 1U) * HACKYLENS_FONT_H /
                                    PY_FONT_H);
    uint16_t covered = 0U;
    uint16_t samples = 0U;

    for(uint16_t source_y = source_y0; source_y < source_y1; source_y++)
    {
        for(uint16_t source_x = source_x0; source_x < source_x1; source_x++)
        {
            uint8_t bit = glyph[
                source_y * HACKYLENS_FONT_ROW_BYTES + source_x / 8U] &
                (uint8_t)(0x80U >> (source_x & 7U));

            covered += bit ? 1U : 0U;
            samples++;
        }
    }
    return samples ? (uint8_t)((covered * 255U + samples / 2U) / samples) : 0U;
}

static uint16_t blend_color(uint16_t foreground, uint16_t background,
                            uint8_t coverage)
{
    uint16_t inverse = (uint16_t)(255U - coverage);
    uint16_t red = (uint16_t)((((foreground >> 11) & 0x1FU) * coverage +
                               ((background >> 11) & 0x1FU) * inverse +
                               127U) / 255U);
    uint16_t green = (uint16_t)((((foreground >> 5) & 0x3FU) * coverage +
                                 ((background >> 5) & 0x3FU) * inverse +
                                 127U) / 255U);
    uint16_t blue = (uint16_t)(((foreground & 0x1FU) * coverage +
                                (background & 0x1FU) * inverse +
                                127U) / 255U);

    return (uint16_t)((red << 11) | (green << 5) | blue);
}

static void draw_small_text(uint16_t x, uint16_t y, uint16_t width,
                            const char *text, uint16_t foreground,
                            uint16_t background)
{
    uint32_t glyphs[PY_MAX_GLYPHS];
    uint16_t glyph_count = 0U;
    const char *cursor = text ? text : "";

    if(x >= HK_DISPLAY_REQUIRED_WIDTH || y + PY_FONT_H >
       HK_DISPLAY_REQUIRED_HEIGHT)
        return;
    if(width > HK_DISPLAY_REQUIRED_WIDTH - x)
        width = HK_DISPLAY_REQUIRED_WIDTH - x;
    while(*cursor && glyph_count < width / PY_FONT_W)
        glyphs[glyph_count++] = small_text_next(&cursor);
    for(uint16_t row = 0U; row < PY_FONT_H; row++)
    {
        uint8_t *pixels = hk_ui_display_row_buffer();

        for(uint16_t column = 0U; column < width; column++)
        {
            uint16_t glyph = (uint16_t)(column / PY_FONT_W);
            uint8_t coverage = glyph < glyph_count ?
                small_glyph_coverage(glyphs[glyph],
                                     (uint16_t)(column % PY_FONT_W), row) : 0U;
            uint16_t color = blend_color(foreground, background, coverage);

            pixels[column * 2U] = (uint8_t)(color >> 8);
            pixels[column * 2U + 1U] = (uint8_t)color;
        }
        hk_ui_display_write_row(x, (uint16_t)(y + row), width, pixels);
    }
}

static void draw_small_centered(uint16_t y, const char *text,
                                uint16_t foreground, uint16_t background)
{
    const char *cursor = text ? text : "";
    uint16_t glyphs = 0U;
    uint16_t width;
    uint16_t x;

    while(*cursor && glyphs < PY_MAX_GLYPHS)
    {
        (void)small_text_next(&cursor);
        glyphs++;
    }
    width = (uint16_t)(glyphs * PY_FONT_W);
    x = width < HK_DISPLAY_REQUIRED_WIDTH ?
        (uint16_t)((HK_DISPLAY_REQUIRED_WIDTH - width) / 2U) : 0U;
    draw_small_text(x, y, width, text, foreground, background);
}

static const char *runtime_label(micropython_runtime_state_t state)
{
    switch(state)
    {
    case MICROPYTHON_RUNTIME_STARTING: return "STARTING";
    case MICROPYTHON_RUNTIME_RUNNING: return "RUNNING";
    case MICROPYTHON_RUNTIME_STOPPING: return "STOPPING";
    case MICROPYTHON_RUNTIME_FINISHED: return "FINISHED";
    case MICROPYTHON_RUNTIME_ERROR: return "ERROR";
    default: return "STOPPED";
    }
}

static const char *filesystem_label(userfs_state_t state)
{
    switch(state)
    {
    case USERFS_STATE_MOUNTED: return "MOUNTED";
    case USERFS_STATE_UNFORMATTED: return "UNFORMATTED";
    case USERFS_STATE_UNSUPPORTED_FLASH: return "UNSUPPORTED";
    case USERFS_STATE_CORRUPT: return "CORRUPT";
    case USERFS_STATE_IO_ERROR: return "I/O ERROR";
    default: return "OFFLINE";
    }
}

static const char *mode_title(micropython_ui_mode_t mode)
{
    switch(mode)
    {
    case MICROPYTHON_UI_ACTIONS: return "PY ACTIONS";
    case MICROPYTHON_UI_PREVIEW: return "PY CODE";
    case MICROPYTHON_UI_CONSOLE: return "PY LOGS";
    case MICROPYTHON_UI_DELETE_CONFIRM: return "DELETE";
    default: return "MICRO-PYTHON";
    }
}

static const char *action_label(const micropython_view_state_t *ui,
                                uint8_t index)
{
    switch(index)
    {
    case MICROPYTHON_ACTION_RUN: return "RUN";
    case MICROPYTHON_ACTION_VIEW: return "VIEW CODE";
    case MICROPYTHON_ACTION_LOGS: return "LOGS";
    case MICROPYTHON_ACTION_STARTUP:
        return ui->startup[0] &&
               strcmp(ui->startup, ui->selected_name) == 0 ?
               "CLEAR STARTUP" : "SET STARTUP";
    case MICROPYTHON_ACTION_DELETE: return "DELETE";
    default: return "";
    }
}

static void fitted_text(char *output, size_t output_size, const char *text,
                        size_t columns)
{
    utf8_copy_glyphs(output, output_size, text ? text : "", columns);
}

static void format_size(char *output, size_t output_size, uint32_t bytes)
{
    if(bytes < 1024U)
        snprintf(output, output_size, "%luB", (unsigned long)bytes);
    else if(bytes < 1024U * 1024U)
        snprintf(output, output_size, "%luK", (unsigned long)(bytes / 1024U));
    else
        snprintf(output, output_size, "%luM",
                 (unsigned long)(bytes / (1024U * 1024U)));
}

static void draw_info(const micropython_view_state_t *ui,
                      const micropython_runtime_status_t *runtime,
                      const userfs_status_t *filesystem)
{
    char line[96];
    char clipped[26];

    hk_ui_display_fill_rect(0U, PY_INFO_Y, HK_DISPLAY_REQUIRED_WIDTH,
                            PY_INFO_H, COLOR_BLACK);
    if(ui->status[0])
    {
        fitted_text(line, sizeof(line), ui->status,
                    MICROPYTHON_STATUS_COLUMNS);
    }
    else if(filesystem->state != USERFS_STATE_MOUNTED &&
       ui->mode != MICROPYTHON_UI_CONSOLE)
    {
        snprintf(line, sizeof(line), "USERFS %s",
                 filesystem_label(filesystem->state));
    }
    else if(ui->mode == MICROPYTHON_UI_LIST)
    {
        uint32_t free_bytes = filesystem->total_bytes > filesystem->used_bytes ?
                              filesystem->total_bytes - filesystem->used_bytes : 0U;
        char free_size[12];
        format_size(free_size, sizeof(free_size), free_bytes);
        snprintf(line, sizeof(line), "%lu PY   %s FREE   STARTUP %s",
                 (unsigned long)ui->file_count, free_size,
                 ui->startup[0] ? "ON" : "OFF");
    }
    else if(ui->mode == MICROPYTHON_UI_CONSOLE)
    {
        snprintf(line, sizeof(line), "RUN %lu  %s  %lu/%lu",
                 (unsigned long)runtime->run_id, runtime_label(runtime->state),
                 (unsigned long)(ui->log_top + (ui->log_count ? 1U : 0U)),
                 (unsigned long)ui->log_total);
    }
    else if(ui->mode == MICROPYTHON_UI_PREVIEW)
    {
        char name[25];
        fitted_text(name, sizeof(name), ui->selected_name, 24U);
        snprintf(line, sizeof(line), "%s  LINE %lu", name,
                 (unsigned long)ui->preview_line);
    }
    else
    {
        fitted_text(line, sizeof(line), ui->selected_name, PY_TEXT_COLUMNS);
    }
    fitted_text(clipped, sizeof(clipped), line, PY_TEXT_COLUMNS);
    draw_small_text(PY_TEXT_X, 31U,
                    HK_DISPLAY_REQUIRED_WIDTH - PY_TEXT_X * 2U, clipped,
                    COLOR_TERM_GREEN, COLOR_BLACK);
}

static void draw_list_row(const micropython_view_state_t *ui, uint8_t row)
{
    uint16_t y = (uint16_t)(PY_BODY_Y + row * PY_LIST_ROW_PITCH);
    uint8_t selected = row < ui->visible_count &&
                       ui->list_top + row == ui->selected_index;
    uint16_t fg = selected ? COLOR_BLACK : COLOR_TERM_GREEN;
    uint16_t bg = selected ? COLOR_TERM_GREEN : COLOR_BLACK;
    char name[USERFS_NAME_MAX + 1U];
    char size[12];
    char line[96];
    char clipped[26];

    hk_ui_display_fill_rect(6U, y, HK_DISPLAY_REQUIRED_WIDTH - 12U,
                            PY_LIST_ROW_H, bg);
    if(row >= ui->visible_count)
        return;
    fitted_text(name, sizeof(name), ui->files[row].name, 14U);
    format_size(size, sizeof(size), ui->files[row].size);
    snprintf(line, sizeof(line), "%c %-14s %8s",
             ui->files[row].startup ? '*' : ' ', name, size);
    fitted_text(clipped, sizeof(clipped), line, PY_TEXT_COLUMNS);
    draw_small_text(PY_TEXT_X, (uint16_t)(y + 2U),
                    HK_DISPLAY_REQUIRED_WIDTH - PY_TEXT_X * 2U,
                    clipped, fg, bg);
}

static void draw_action_row(const micropython_view_state_t *ui, uint8_t row)
{
    uint16_t y = (uint16_t)(PY_ACTION_ROW_Y0 + row * PY_ACTION_ROW_PITCH);
    uint8_t selected = row == ui->action_index;
    uint16_t fg = selected ? COLOR_BLACK : COLOR_TERM_GREEN;
    uint16_t bg = selected ? COLOR_TERM_GREEN : COLOR_BLACK;

    hk_ui_display_fill_rect(20U, y, HK_DISPLAY_REQUIRED_WIDTH - 40U,
                            PY_ACTION_ROW_H, bg);
    hk_ui_display_draw_text_at(28U, (uint16_t)(y + 2U),
                               action_label(ui, row), fg, bg);
}

static void draw_body(const micropython_view_state_t *ui,
                      const userfs_status_t *filesystem)
{
    char line[48];

    hk_ui_display_fill_rect(0U, PY_BODY_Y, HK_DISPLAY_REQUIRED_WIDTH,
                            PY_BODY_H, COLOR_BLACK);
    if(filesystem->state != USERFS_STATE_MOUNTED &&
       ui->mode != MICROPYTHON_UI_CONSOLE)
    {
        draw_small_centered(94U, "STORAGE NOT READY",
                            COLOR_TERM_GREEN, COLOR_BLACK);
        return;
    }
    if(ui->mode == MICROPYTHON_UI_LIST)
    {
        if(!ui->file_count)
        {
            draw_small_centered(84U, "NO PYTHON FILES",
                                COLOR_TERM_GREEN, COLOR_BLACK);
            draw_small_centered(112U, "UPLOAD .PY FROM PC",
                                COLOR_WHITE, COLOR_BLACK);
            draw_small_centered(140U, "WITH HKPY TOOL",
                                COLOR_WHITE, COLOR_BLACK);
            return;
        }
        for(uint8_t row = 0U; row < MICROPYTHON_LIST_VISIBLE_ROWS; row++)
            draw_list_row(ui, row);
        return;
    }
    if(ui->mode == MICROPYTHON_UI_ACTIONS)
    {
        for(uint8_t row = 0U; row < MICROPYTHON_ACTION_COUNT; row++)
            draw_action_row(ui, row);
        return;
    }
    if(ui->mode == MICROPYTHON_UI_PREVIEW)
    {
        for(uint8_t row = 0U; row < ui->preview_count; row++)
        {
            snprintf(line, sizeof(line), "%5lu %s",
                     (unsigned long)ui->preview_line_number[row],
                     ui->preview[row]);
            draw_small_text(
                PY_TEXT_X, (uint16_t)(PY_BODY_Y + row * PY_TEXT_ROW_PITCH),
                HK_DISPLAY_REQUIRED_WIDTH - PY_TEXT_X * 2U,
                line, COLOR_TERM_GREEN, COLOR_BLACK);
        }
        return;
    }
    if(ui->mode == MICROPYTHON_UI_CONSOLE)
    {
        for(uint8_t row = 0U; row < ui->log_count; row++)
            draw_small_text(
                PY_TEXT_X, (uint16_t)(PY_BODY_Y + row * PY_TEXT_ROW_PITCH),
                HK_DISPLAY_REQUIRED_WIDTH - PY_TEXT_X * 2U,
                ui->log_lines[row], COLOR_TERM_GREEN, COLOR_BLACK);
        return;
    }
    draw_small_centered(76U, "DELETE THIS SCRIPT?",
                        COLOR_WHITE, COLOR_BLACK);
    fitted_text(line, sizeof(line), ui->selected_name, PY_TEXT_COLUMNS);
    draw_small_centered(108U, line, COLOR_TERM_GREEN, COLOR_BLACK);
    if(ui->startup[0] && strcmp(ui->startup, ui->selected_name) == 0)
        draw_small_centered(140U, "STARTUP WILL BE CLEARED",
                            COLOR_WHITE, COLOR_BLACK);
}

static void add_region(hk_ui_display_rect_t *regions, uint16_t *count,
                       uint16_t x, uint16_t y, uint16_t width, uint16_t height)
{
    if(*count >= HK_UI_DISPLAY_FRAME_MAX_DIRTY_RECTS)
        return;
    regions[*count] = (hk_ui_display_rect_t){x, y, width, height};
    (*count)++;
}

void micropython_view_render(
    const micropython_view_state_t *ui,
    const micropython_runtime_status_t *runtime,
    const userfs_status_t *filesystem,
    const micropython_view_update_t *update)
{
    hk_ui_display_surface_t frame;
    hk_ui_display_rect_t regions[HK_UI_DISPLAY_FRAME_MAX_DIRTY_RECTS];
    uint16_t region_count = 0U;

    if(!ui || !runtime || !filesystem || !update ||
       !hk_ui_display_frame_acquire(&frame))
        return;
    if(update->full)
    {
        menu_draw_chrome(mode_title(ui->mode));
        draw_info(ui, runtime, filesystem);
        draw_body(ui, filesystem);
        if(!hk_ui_display_frame_present(frame.lease_id))
            hk_ui_display_frame_cancel(frame.lease_id);
        return;
    }
    if(update->info)
    {
        draw_info(ui, runtime, filesystem);
        add_region(regions, &region_count, 0U, PY_INFO_Y,
                   HK_DISPLAY_REQUIRED_WIDTH, PY_INFO_H);
    }
    if(update->body)
    {
        draw_body(ui, filesystem);
        add_region(regions, &region_count, 0U, PY_BODY_Y,
                   HK_DISPLAY_REQUIRED_WIDTH, PY_BODY_H);
    }
    else if(update->row_mask)
    {
        uint8_t count = ui->mode == MICROPYTHON_UI_ACTIONS ?
                        MICROPYTHON_ACTION_COUNT :
                        MICROPYTHON_LIST_VISIBLE_ROWS;
        for(uint8_t row = 0U; row < count; row++)
        {
            if(!(update->row_mask & (uint8_t)(1U << row)))
                continue;
            if(ui->mode == MICROPYTHON_UI_ACTIONS)
            {
                draw_action_row(ui, row);
                add_region(regions, &region_count, 20U,
                           (uint16_t)(PY_ACTION_ROW_Y0 +
                                      row * PY_ACTION_ROW_PITCH),
                           HK_DISPLAY_REQUIRED_WIDTH - 40U, PY_ACTION_ROW_H);
            }
            else if(ui->mode == MICROPYTHON_UI_LIST)
            {
                draw_list_row(ui, row);
                add_region(regions, &region_count, 6U,
                           (uint16_t)(PY_BODY_Y + row * PY_LIST_ROW_PITCH),
                           HK_DISPLAY_REQUIRED_WIDTH - 12U, PY_LIST_ROW_H);
            }
        }
    }
    if(!region_count)
    {
        hk_ui_display_frame_cancel(frame.lease_id);
        return;
    }
    if(!hk_ui_display_frame_present_regions(frame.lease_id, regions,
                                             region_count))
        hk_ui_display_frame_cancel(frame.lease_id);
}

void micropython_view_draw_icon(uint16_t x, uint16_t y,
                                uint16_t color, uint16_t background)
{
    (void)background;
    hk_ui_display_draw_rect(x + 9U, y + 10U, 42U, 40U, 2U, color);
    hk_ui_display_draw_text_at(x + 16U, y + 22U, "PY", color, COLOR_BLACK);
    hk_ui_display_fill_rect(x + 12U, y + 13U, 7U, 3U, color);
    hk_ui_display_fill_rect(x + 41U, y + 44U, 7U, 3U, color);
}

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "display_binding.h"
#include "display_config.h"
#include "micropython_view.h"

static unsigned g_failures;
static unsigned g_frame_acquires;
static unsigned g_full_presents;
static unsigned g_region_presents;
static unsigned g_cancels;
static unsigned g_chrome_draws;
static unsigned g_fill_count;
static unsigned g_text_count;
static unsigned g_operation;
static unsigned g_info_text_rows;
static hk_ui_display_rect_t g_regions[HK_UI_DISPLAY_FRAME_MAX_DIRTY_RECTS];
static uint16_t g_region_count;
static uint8_t g_surface[320U * 240U * 2U];
static uint8_t g_row[320U * 2U];
static uint8_t g_blank_glyph[HACKYLENS_FONT_H * HACKYLENS_FONT_ROW_BYTES];

static void check(int condition, const char *message)
{
    if(!condition)
    {
        fprintf(stderr, "FAIL: %s\n", message);
        g_failures++;
    }
}

static void reset_calls(void)
{
    g_frame_acquires = 0U;
    g_full_presents = 0U;
    g_region_presents = 0U;
    g_cancels = 0U;
    g_chrome_draws = 0U;
    g_fill_count = 0U;
    g_text_count = 0U;
    g_operation = 0U;
    g_info_text_rows = 0U;
    g_region_count = 0U;
}

void menu_draw_chrome(const char *title)
{
    check(title && strlen(title) <= 40U, "chrome title fits display");
    g_chrome_draws++;
}

const uint8_t *hk_font_glyph(uint32_t codepoint)
{
    (void)codepoint;
    return g_blank_glyph;
}

uint8_t *hk_ui_display_row_buffer(void)
{
    return g_row;
}

void hk_ui_display_write_row(uint16_t x, uint16_t y, uint16_t width,
                             const uint8_t *pixels)
{
    (void)pixels;
    g_operation++;
    check(width && x + width <= 320U && y < 240U,
          "small-font row stays on screen");
    if(y >= 31U && y < 51U)
        g_info_text_rows++;
}

uint8_t hk_ui_display_frame_acquire(hk_ui_display_surface_t *surface)
{
    g_frame_acquires++;
    *surface = (hk_ui_display_surface_t){
        g_surface, 320U, 240U, 640U, 77U,
    };
    return 1U;
}

uint8_t hk_ui_display_frame_present(uint32_t lease_id)
{
    check(lease_id == 77U, "full present uses acquired lease");
    g_full_presents++;
    return 1U;
}

uint8_t hk_ui_display_frame_present_regions(
    uint32_t lease_id, const hk_ui_display_rect_t *regions,
    uint16_t region_count)
{
    check(lease_id == 77U, "region present uses acquired lease");
    check(region_count <= HK_UI_DISPLAY_FRAME_MAX_DIRTY_RECTS,
          "dirty region count is bounded");
    g_region_count = region_count;
    memcpy(g_regions, regions, region_count * sizeof(regions[0]));
    g_region_presents++;
    return 1U;
}

void hk_ui_display_frame_cancel(uint32_t lease_id)
{
    check(lease_id == 77U, "cancel uses acquired lease");
    g_cancels++;
}

void hk_ui_display_fill_rect(uint16_t x, uint16_t y,
                             uint16_t width, uint16_t height,
                             uint16_t color)
{
    (void)color;
    g_operation++;
    check(width && height, "filled region is non-empty");
    check(x + width <= 320U && y + height <= 240U,
          "filled region stays on screen");
    g_fill_count++;
}

void hk_ui_display_draw_rect(uint16_t x, uint16_t y,
                             uint16_t width, uint16_t height,
                             uint16_t thickness, uint16_t color)
{
    (void)x;
    (void)y;
    (void)width;
    (void)height;
    (void)thickness;
    (void)color;
}

void hk_ui_display_draw_text_at(uint16_t x, uint16_t y, const char *text,
                                uint16_t foreground, uint16_t background)
{
    size_t glyphs = strlen(text);
    (void)foreground;
    (void)background;
    g_operation++;
    check(x + glyphs * HACKYLENS_FONT_W <= 320U,
          "text stays within display width");
    check((uint32_t)y + HACKYLENS_FONT_H <= 240U,
          "text stays within display height");
    g_text_count++;
}

void hk_ui_display_draw_text_centered(uint16_t y, const char *text,
                                      uint16_t foreground,
                                      uint16_t background)
{
    (void)foreground;
    (void)background;
    check(strlen(text) * HACKYLENS_FONT_W <= 320U,
          "centered text stays within display width");
    check((uint32_t)y + HACKYLENS_FONT_H <= 240U,
          "centered text stays within display height");
    g_text_count++;
}

static void seed(micropython_view_state_t *ui,
                 micropython_runtime_status_t *runtime,
                 userfs_status_t *filesystem)
{
    memset(ui, 0, sizeof(*ui));
    memset(runtime, 0, sizeof(*runtime));
    memset(filesystem, 0, sizeof(*filesystem));
    ui->mode = MICROPYTHON_UI_LIST;
    ui->file_count = 7U;
    ui->visible_count = 7U;
    ui->selected_index = 1U;
    snprintf(ui->startup, sizeof(ui->startup), "startup.py");
    for(uint8_t row = 0U; row < 7U; row++)
    {
        snprintf(ui->files[row].name, sizeof(ui->files[row].name),
                 "very-long-python-file-name-number-%u.py", row);
        ui->files[row].size = row ? 1234567U : 17U;
        ui->files[row].startup = row == 2U;
    }
    filesystem->state = USERFS_STATE_MOUNTED;
    filesystem->total_bytes = 16U * 1024U * 1024U;
    filesystem->used_bytes = 4U * 1024U * 1024U;
    runtime->state = MICROPYTHON_RUNTIME_RUNNING;
    runtime->run_id = UINT32_MAX;
}

int main(void)
{
    micropython_view_state_t ui;
    micropython_runtime_status_t runtime;
    userfs_status_t filesystem;
    micropython_view_update_t update;

    seed(&ui, &runtime, &filesystem);
    reset_calls();
    update = (micropython_view_update_t){1U, 0U, 0U, 0U};
    micropython_view_render(&ui, &runtime, &filesystem, &update);
    check(g_frame_acquires == 1U && g_full_presents == 1U,
          "full screen is composed and presented once");
    check(g_region_presents == 0U && g_cancels == 0U,
          "full screen has no intermediate presents");
    check(g_chrome_draws == 1U, "full screen draws chrome once");
    check(g_info_text_rows == 20U,
          "full screen draws one 12x20 information line");

    reset_calls();
    update = (micropython_view_update_t){0U, 0U, 0U, 0x03U};
    micropython_view_render(&ui, &runtime, &filesystem, &update);
    check(g_chrome_draws == 0U && g_full_presents == 0U,
          "selection change does not redraw chrome or full screen");
    check(g_region_presents == 1U && g_region_count == 2U,
          "selection change presents exactly two rows once");
    check(g_regions[0].y >= 53U && g_regions[0].y + g_regions[0].height <= 240U &&
          g_regions[1].y >= 53U && g_regions[1].y + g_regions[1].height <= 240U,
          "list rows remain inside body region");

    ui.mode = MICROPYTHON_UI_CONSOLE;
    ui.log_count = 9U;
    ui.log_total = 48U;
    ui.log_top = 39U;
    for(uint8_t row = 0U; row < 9U; row++)
        snprintf(ui.log_lines[row], sizeof(ui.log_lines[row]),
                 "console-line-%u", row);
    reset_calls();
    update = (micropython_view_update_t){0U, 1U, 1U, 0U};
    micropython_view_render(&ui, &runtime, &filesystem, &update);
    check(g_frame_acquires == 1U && g_region_presents == 1U &&
          g_region_count == 2U,
          "console update is one two-region present");
    check(g_regions[0].y + g_regions[0].height <= g_regions[1].y,
          "info and body regions never overlap");
    check(g_chrome_draws == 0U, "live console never redraws chrome");

    memset(ui.status, 'X', MICROPYTHON_STATUS_COLUMNS);
    ui.status[MICROPYTHON_STATUS_COLUMNS] = '\0';
    reset_calls();
    update = (micropython_view_update_t){0U, 1U, 0U, 0U};
    micropython_view_render(&ui, &runtime, &filesystem, &update);
    check(g_info_text_rows == 20U && g_region_count == 1U,
          "status replaces the information line");

    if(g_failures)
        return 1;
    puts("MICROPYTHON_VIEW_OK bounds=1 atomic=1 rows=2 regions=2 font=12");
    return 0;
}

#include "files_view.h"

#include <stdio.h>
#include <string.h>

#include "../../ui/hk_ui.h"
#include "files_types.h"
#include "../../core/file_name.h"
#include "../../core/hk_string.h"

#include "../../config/display_config.h"
#include "files_layout.h"

#include "../../ui/display_binding.h"
#include "../../ui/dristy_theme.h"
#include "../../ui/dristy_ui.h"
#include "../../services/frame_workspace.h"
#include "files_view_port.h"

static hk_ui_display_surface_t g_animation_surface;
static hk_indexed_frame_t g_animation_previous_frame;
static hk_indexed_frame_t g_animation_current_frame;
static uint16_t g_animation_canvas_w;
static uint16_t g_animation_canvas_h;
static uint16_t g_animation_view_x;
static uint16_t g_animation_view_y;
static uint16_t g_animation_view_w;
static uint16_t g_animation_view_h;
static uint16_t g_animation_background;
static uint16_t g_animation_source_x[HK_DISPLAY_REQUIRED_WIDTH];
static uint16_t g_animation_source_y[HK_DISPLAY_REQUIRED_HEIGHT];
static uint8_t *g_animation_backup;
static frame_workspace_borrow_t g_animation_workspace;
static uint8_t g_animation_frame_open;
static uint8_t g_animation_reset_pending;
static uint8_t g_animation_previous_valid;
static uint8_t g_animation_backup_valid;

static void files_view_enter_impl(void)
{
}

static void files_view_draw_chrome(const char *title, const char *subtitle)
{
    dristy_ui_draw_shell_bg();
    dristy_ui_draw_app_header(title ? title : "Files", subtitle ? subtitle : "OK open  BACK menu");
    dristy_ui_draw_app_footer("Up", "Open", "Down", "Menu");
}

static void files_view_draw_status_impl(const char *line)
{
    files_view_draw_chrome("Files", line);
    hk_ui_display_draw_text_centered(96, line, DRISTY_COLOR_TEXT, DRISTY_COLOR_BG);
}

static void files_view_draw_row_impl(uint8_t row, const file_list_item_t *items, uint8_t count, uint8_t top, uint8_t index)
{
    uint8_t entry_index = (uint8_t)(top + row);
    uint16_t y = FILES_ROW_Y0 + row * FILES_ROW_H;
    uint8_t selected = entry_index == index;
    uint16_t fg = selected ? DRISTY_COLOR_BG : DRISTY_COLOR_TEXT;
    uint16_t bg = selected ? DRISTY_COLOR_ACCENT : DRISTY_COLOR_PANEL;
    char line[FILE_NAME_MAX + 4U];

    hk_ui_display_fill_rect(8, y, HK_DISPLAY_REQUIRED_WIDTH - 16, FILES_ROW_H - 4, bg);
    if(entry_index >= count)
        return;

    line[0] = items[entry_index].kind == FILE_ITEM_DIRECTORY ? 'D' : 'F';
    line[1] = ' ';
    utf8_copy_glyphs(&line[2], sizeof(line) - 2U, items[entry_index].name, 18U);
    hk_ui_display_draw_text_at(12, (uint16_t)(y + (FILES_ROW_H - HACKYLENS_FONT_H) / 2U), line, fg, bg);
}

static void files_view_render_list_impl(uint8_t sd_present, uint8_t fat_mounted, const file_list_item_t *items, uint8_t count, uint8_t top, uint8_t index)
{
    files_view_draw_chrome("Files", NULL);
    if(!sd_present)
    {
        hk_ui_display_draw_text_centered(96, "NO SD", DRISTY_COLOR_TEXT_DIM, DRISTY_COLOR_BG);
        return;
    }
    if(!fat_mounted)
    {
        hk_ui_display_draw_text_centered(96, "FAT32 ONLY", DRISTY_COLOR_TEXT_DIM, DRISTY_COLOR_BG);
        return;
    }
    if(count == 0)
    {
        hk_ui_display_draw_text_centered(96, "EMPTY", DRISTY_COLOR_TEXT_DIM, DRISTY_COLOR_BG);
        return;
    }

    for(uint8_t row = 0; row < FILES_ROWS; row++)
        files_view_draw_row_impl(row, items, count, top, index);
}

static void files_view_render_preview_impl(const char *preview, uint16_t len, uint8_t page)
{
    uint16_t chars_per_page = FILE_PREVIEW_ROWS * TERM_COLS;
    uint16_t start = (uint16_t)page * chars_per_page;

    files_view_draw_chrome("Preview", "L/R page  BACK list");
    if(len == 0)
    {
        hk_ui_display_draw_text_centered(96, "EMPTY", DRISTY_COLOR_TEXT_DIM, DRISTY_COLOR_BG);
        return;
    }

    for(uint8_t row = 0; row < FILE_PREVIEW_ROWS; row++)
    {
        char line[TERM_COLS + 1];
        uint16_t offset = start + row * TERM_COLS;
        memset(line, ' ', TERM_COLS);
        line[TERM_COLS] = '\0';
        for(uint8_t col = 0; col < TERM_COLS && offset + col < len; col++)
            line[col] = preview[offset + col];
        hk_ui_display_draw_text_at(0, FILES_ROW_Y0 + row * HACKYLENS_FONT_H, line, DRISTY_COLOR_TEXT,
                                   DRISTY_COLOR_BG);
    }
}

static void files_view_clear_image_impl(void)
{
    hk_ui_display_fill_rect(0, 0, HK_DISPLAY_REQUIRED_WIDTH, HK_DISPLAY_REQUIRED_HEIGHT, COLOR_BLACK);
}

static void files_view_render_rgb888_scaled_row_at(uint16_t x0, uint16_t y, uint16_t dst_w, const uint8_t *row, uint16_t src_w, uint8_t bgr_order)
{
    for(uint16_t x = 0; x < dst_w; x++)
    {
        uint32_t src_x = (uint32_t)x * src_w / dst_w;
        const uint8_t *px = &row[src_x * 3U];
        uint8_t r = bgr_order ? px[2] : px[0];
        uint8_t g = px[1];
        uint8_t b = bgr_order ? px[0] : px[2];
        uint16_t color = (uint16_t)(((uint16_t)(r & 0xF8U) << 8) |
                                    ((uint16_t)(g & 0xFCU) << 3) |
                                    ((uint16_t)b >> 3));
        hk_ui_display_row_buffer()[x * 2U + 0U] = color >> 8;
        hk_ui_display_row_buffer()[x * 2U + 1U] = color & 0xFF;
    }
    hk_ui_display_write_row(
        x0, y, dst_w, hk_ui_display_row_buffer());
}

static void files_view_render_rgb565le_scaled_row_at(uint16_t x0, uint16_t y, uint16_t dst_w, const uint8_t *row, uint16_t src_w)
{
    for(uint16_t x = 0; x < dst_w; x++)
    {
        uint32_t src_x = (uint32_t)x * src_w / dst_w;
        uint16_t color = (uint16_t)row[src_x * 2U] | ((uint16_t)row[src_x * 2U + 1U] << 8);
        hk_ui_display_row_buffer()[x * 2U + 0U] = color >> 8;
        hk_ui_display_row_buffer()[x * 2U + 1U] = color & 0xFF;
    }
    hk_ui_display_write_row(
        x0, y, dst_w, hk_ui_display_row_buffer());
}

static void files_view_render_image_row_span_impl(uint32_t src_y, uint32_t src_h, const uint8_t *row, uint16_t src_w, uint8_t bpp, uint8_t bgr_order)
{
    uint16_t dst_x;
    uint16_t dst_y;
    uint16_t dst_w;
    uint16_t dst_h;
    uint16_t y0;
    uint16_t y1;

    image_fit_viewport(src_w, (uint16_t)src_h, &dst_x, &dst_y, &dst_w, &dst_h);
    y0 = (uint16_t)(dst_y + src_y * dst_h / src_h);
    y1 = (uint16_t)(dst_y + ((src_y + 1U) * dst_h + src_h - 1U) / src_h);

    if(y1 <= y0)
        y1 = (uint16_t)(y0 + 1U);
    if(y1 > dst_y + dst_h)
        y1 = (uint16_t)(dst_y + dst_h);

    for(uint16_t y = y0; y < y1; y++)
    {
        if(bpp == 16)
            files_view_render_rgb565le_scaled_row_at(dst_x, y, dst_w, row, src_w);
        else
            files_view_render_rgb888_scaled_row_at(dst_x, y, dst_w, row, src_w, bgr_order);
    }
}

static void files_view_surface_pixel(uint16_t x, uint16_t y, uint16_t color)
{
    uint8_t *pixel;

    if(x >= g_animation_surface.width || y >= g_animation_surface.height)
        return;
    pixel = g_animation_surface.rgb565_be +
            (uint32_t)y * g_animation_surface.stride_bytes + (uint32_t)x * 2U;
    pixel[0] = (uint8_t)(color >> 8);
    pixel[1] = (uint8_t)color;
}

static uint8_t files_view_source_inside(uint16_t dst_x, uint16_t dst_y,
                                        uint16_t src_x, uint16_t src_y,
                                        uint16_t src_w, uint16_t src_h)
{
    uint16_t logical_x = g_animation_source_x[dst_x];
    uint16_t logical_y = g_animation_source_y[dst_y];
    return logical_x >= src_x && logical_x < (uint32_t)src_x + src_w &&
           logical_y >= src_y && logical_y < (uint32_t)src_y + src_h;
}

static void files_view_fill_logical_rect(uint16_t src_x, uint16_t src_y,
                                         uint16_t src_w, uint16_t src_h,
                                         uint16_t color)
{
    for(uint16_t y = 0; y < g_animation_view_h; y++)
    {
        for(uint16_t x = 0; x < g_animation_view_w; x++)
        {
            if(files_view_source_inside(x, y, src_x, src_y, src_w, src_h))
                files_view_surface_pixel((uint16_t)(g_animation_view_x + x),
                                         (uint16_t)(g_animation_view_y + y), color);
        }
    }
}

static void files_view_reset_animation_surface(void)
{
    memset(g_animation_surface.rgb565_be, 0,
           (uint32_t)g_animation_surface.stride_bytes * g_animation_surface.height);
    files_view_fill_logical_rect(0, 0, g_animation_canvas_w, g_animation_canvas_h,
                                 g_animation_background);
    g_animation_reset_pending = 0;
    g_animation_previous_valid = 0;
    g_animation_backup_valid = 0;
}

static void files_view_apply_previous_disposal(void)
{
    if(!g_animation_previous_valid)
        return;
    if(g_animation_previous_frame.disposal == 2U)
    {
        files_view_fill_logical_rect(g_animation_previous_frame.frame_x,
                                     g_animation_previous_frame.frame_y,
                                     g_animation_previous_frame.frame_w,
                                     g_animation_previous_frame.frame_h,
                                     g_animation_background);
    }
    else if(g_animation_previous_frame.disposal == 3U &&
            g_animation_backup_valid && g_animation_backup)
    {
        memcpy(g_animation_surface.rgb565_be, g_animation_backup,
               (uint32_t)g_animation_surface.stride_bytes * g_animation_surface.height);
    }
}

static uint8_t files_view_animation_begin_impl(uint16_t canvas_w, uint16_t canvas_h,
                                               uint16_t background_rgb565)
{
    if(canvas_w == 0 || canvas_h == 0)
        return 0;
    if(g_animation_frame_open)
    {
        hk_ui_display_frame_cancel(g_animation_surface.lease_id);
        g_animation_frame_open = 0;
    }
    g_animation_canvas_w = canvas_w;
    g_animation_canvas_h = canvas_h;
    g_animation_background = background_rgb565;
    image_fit_viewport(canvas_w, canvas_h, &g_animation_view_x, &g_animation_view_y,
                       &g_animation_view_w, &g_animation_view_h);
    for(uint16_t x = 0; x < g_animation_view_w; x++)
        g_animation_source_x[x] = (uint16_t)((uint32_t)x * canvas_w / g_animation_view_w);
    for(uint16_t y = 0; y < g_animation_view_h; y++)
        g_animation_source_y[y] = (uint16_t)((uint32_t)y * canvas_h / g_animation_view_h);
    if(g_animation_workspace.generation != 0U)
        (void)frame_workspace_release(&g_animation_workspace);
    if(!frame_workspace_borrow(
            HK_DISPLAY_REQUIRED_WIDTH * HK_DISPLAY_REQUIRED_HEIGHT * 2U,
            &g_animation_workspace))
        return 0;
    g_animation_backup = g_animation_workspace.data;
    g_animation_reset_pending = 1;
    g_animation_previous_valid = 0;
    g_animation_backup_valid = 0;
    return g_animation_backup != NULL;
}

static uint8_t files_view_animation_frame_begin_impl(const hk_indexed_frame_t *frame)
{
    uint32_t surface_bytes;

    if(!frame || frame->canvas_w != g_animation_canvas_w ||
       frame->canvas_h != g_animation_canvas_h || g_animation_frame_open ||
       !hk_ui_display_frame_acquire(&g_animation_surface))
        return 0;
    g_animation_frame_open = 1;
    surface_bytes = (uint32_t)g_animation_surface.stride_bytes * g_animation_surface.height;
    if(g_animation_reset_pending)
        files_view_reset_animation_surface();
    else
        files_view_apply_previous_disposal();

    g_animation_current_frame = *frame;
    if(frame->disposal == 3U)
    {
        memcpy(g_animation_backup, g_animation_surface.rgb565_be, surface_bytes);
        g_animation_backup_valid = 1;
    }
    else
    {
        g_animation_backup_valid = 0;
    }
    return 1;
}

static void files_view_animation_render_indexed_row_impl(const hk_indexed_frame_t *frame,
                                                         uint16_t frame_row,
                                                         const uint8_t *indices,
                                                         const uint16_t *palette,
                                                         uint16_t palette_size)
{
    uint16_t logical_y;

    if(!g_animation_frame_open || !frame || !indices || !palette ||
       frame_row >= frame->frame_h)
        return;
    logical_y = (uint16_t)(frame->frame_y + frame_row);
    for(uint16_t y = 0; y < g_animation_view_h; y++)
    {
        uint16_t source_y = g_animation_source_y[y];
        if(source_y != logical_y)
            continue;
        for(uint16_t x = 0; x < g_animation_view_w; x++)
        {
            uint16_t source_x = g_animation_source_x[x];
            uint16_t local_x;
            uint8_t index;

            if(source_x < frame->frame_x || source_x >= (uint32_t)frame->frame_x + frame->frame_w)
                continue;
            local_x = (uint16_t)(source_x - frame->frame_x);
            index = indices[local_x];
            if(index >= palette_size || index == frame->transparent_index)
                continue;
            files_view_surface_pixel((uint16_t)(g_animation_view_x + x),
                                     (uint16_t)(g_animation_view_y + y), palette[index]);
        }
    }
}

static uint8_t files_view_animation_frame_end_impl(void)
{
    uint32_t lease_id;

    if(!g_animation_frame_open)
        return 0;
    lease_id = g_animation_surface.lease_id;
    g_animation_frame_open = 0;
    g_animation_previous_frame = g_animation_current_frame;
    g_animation_previous_valid = 1;
    if(!hk_ui_display_frame_present(lease_id))
    {
        hk_ui_display_frame_cancel(lease_id);
        return 0;
    }
    return 1;
}

static void files_view_animation_end_impl(void)
{
    if(g_animation_frame_open)
        hk_ui_display_frame_cancel(g_animation_surface.lease_id);
    memset(&g_animation_surface, 0, sizeof(g_animation_surface));
    memset(&g_animation_previous_frame, 0, sizeof(g_animation_previous_frame));
    memset(&g_animation_current_frame, 0, sizeof(g_animation_current_frame));
    g_animation_frame_open = 0;
    g_animation_reset_pending = 0;
    g_animation_previous_valid = 0;
    g_animation_backup_valid = 0;
    g_animation_backup = NULL;
    if(g_animation_workspace.generation != 0U)
        (void)frame_workspace_release(&g_animation_workspace);
}

static void files_view_draw_delete_confirm_impl(const char *name)
{
    files_view_draw_chrome("Delete", "Confirm remove file");
    dristy_ui_draw_app_footer(NULL, "Delete", NULL, "Cancel");
    hk_ui_display_draw_text_centered(68, "Delete this file?", DRISTY_COLOR_TEXT, DRISTY_COLOR_BG);
    hk_ui_display_draw_text_centered(104, name ? name : "", DRISTY_COLOR_TEXT_DIM, DRISTY_COLOR_BG);
}

void files_view_init(void)
{
    static const files_view_ops_t ops = {
        .enter = files_view_enter_impl,
        .draw_status = files_view_draw_status_impl,
        .draw_row = files_view_draw_row_impl,
        .render_list = files_view_render_list_impl,
        .render_preview = files_view_render_preview_impl,
        .clear_image = files_view_clear_image_impl,
        .render_image_row_span = files_view_render_image_row_span_impl,
        .animation_begin = files_view_animation_begin_impl,
        .animation_frame_begin = files_view_animation_frame_begin_impl,
        .animation_render_indexed_row = files_view_animation_render_indexed_row_impl,
        .animation_frame_end = files_view_animation_frame_end_impl,
        .animation_end = files_view_animation_end_impl,
        .draw_delete_confirm = files_view_draw_delete_confirm_impl,
    };

    files_view_register(&ops);
}

void files_view_draw_icon(uint16_t x, uint16_t y, uint16_t color, uint16_t bg)
{
    (void)bg;
    hk_ui_display_fill_rect(x + 20, y + 8, 20, 2, color);
    hk_ui_display_fill_rect(x + 18, y + 12, 2, 38, color);
    hk_ui_display_fill_rect(x + 42, y + 14, 2, 36, color);
    hk_ui_display_fill_rect(x + 20, y + 50, 22, 2, color);
    hk_ui_display_fill_rect(x + 38, y + 8, 2, 8, color);
    hk_ui_display_fill_rect(x + 40, y + 14, 4, 2, color);
    hk_ui_display_fill_rect(x + 23, y + 15, 4, 8, color);
    hk_ui_display_fill_rect(x + 29, y + 15, 4, 8, color);
    hk_ui_display_fill_rect(x + 35, y + 15, 4, 8, color);
    hk_ui_display_fill_rect(x + 24, y + 39, 14, 2, color);
    hk_ui_display_fill_rect(x + 24, y + 44, 10, 2, color);
}

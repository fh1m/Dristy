#include "files_presenter.h"

#include <stddef.h>

#include "files_view_port.h"
#include "file_browser_list.h"
#include "file_browser_mode.h"
#include "../../storage/fat32_volume.h"
#include "image_viewer.h"
#include "file_preview.h"

static const files_view_ops_t *g_files_view_ops;

void files_view_register(const files_view_ops_t *ops)
{
    g_files_view_ops = ops;
}

static void files_presenter_draw_status(const char *line)
{
    if(g_files_view_ops && g_files_view_ops->draw_status)
        g_files_view_ops->draw_status(line);
}

void files_presenter_show_status(const char *line)
{
    files_presenter_draw_status(line);
}

void files_presenter_enter(void)
{
    if(g_files_view_ops && g_files_view_ops->enter)
        g_files_view_ops->enter();
}

void files_presenter_draw_row(uint8_t row)
{
    if(g_files_view_ops && g_files_view_ops->draw_row)
        g_files_view_ops->draw_row(row, files_list_items(), files_count(), files_top(), files_index());
}

void files_presenter_render_list(void)
{
    if(files_count())
        files_ensure_visible();
    if(g_files_view_ops && g_files_view_ops->render_list)
        g_files_view_ops->render_list(hk_sd_present(), hk_fat_mounted(), files_list_items(), files_count(), files_top(), files_index());
}

void files_presenter_render_preview(void)
{
    if(g_files_view_ops && g_files_view_ops->render_preview)
        g_files_view_ops->render_preview(files_preview_text(), files_preview_len(), files_preview_page());
}

static void files_presenter_begin_image(void *context)
{
    (void)context;
    if(g_files_view_ops && g_files_view_ops->clear_image)
        g_files_view_ops->clear_image();
}

void files_presenter_draw_delete_confirm(const char *name)
{
    if(g_files_view_ops && g_files_view_ops->draw_delete_confirm)
        g_files_view_ops->draw_delete_confirm(name);
}

static void files_presenter_render_image_row(void *context, uint32_t src_y, uint32_t src_h, const uint8_t *row, uint16_t src_w, uint8_t bpp, uint8_t bgr_order)
{
    (void)context;
    if(g_files_view_ops && g_files_view_ops->render_image_row_span)
        g_files_view_ops->render_image_row_span(src_y, src_h, row, src_w, bpp, bgr_order);
}

static uint8_t files_presenter_animation_begin(void *context, uint16_t canvas_w,
                                               uint16_t canvas_h, uint16_t background_rgb565)
{
    (void)context;
    return g_files_view_ops && g_files_view_ops->animation_begin &&
           g_files_view_ops->animation_begin(canvas_w, canvas_h, background_rgb565);
}

static uint8_t files_presenter_animation_frame_begin(void *context, const file_gif_frame_t *frame)
{
    (void)context;
    return g_files_view_ops && g_files_view_ops->animation_frame_begin &&
           g_files_view_ops->animation_frame_begin(frame);
}

static void files_presenter_animation_render_indexed_row(void *context,
                                                         const file_gif_frame_t *frame,
                                                         uint16_t frame_row,
                                                         const uint8_t *indices,
                                                         const uint16_t *palette,
                                                         uint16_t palette_size)
{
    (void)context;
    if(g_files_view_ops && g_files_view_ops->animation_render_indexed_row)
        g_files_view_ops->animation_render_indexed_row(frame, frame_row, indices,
                                                       palette, palette_size);
}

static uint8_t files_presenter_animation_frame_end(void *context)
{
    (void)context;
    return g_files_view_ops && g_files_view_ops->animation_frame_end &&
           g_files_view_ops->animation_frame_end();
}

static void files_presenter_animation_end(void *context)
{
    (void)context;
    if(g_files_view_ops && g_files_view_ops->animation_end)
        g_files_view_ops->animation_end();
}

static const file_image_sink_t *files_presenter_image_sink(void)
{
    static const file_image_sink_t sink = {
        .begin = files_presenter_begin_image,
        .render_row_span = files_presenter_render_image_row,
        .animation_begin = files_presenter_animation_begin,
        .animation_frame_begin = files_presenter_animation_frame_begin,
        .animation_render_indexed_row = files_presenter_animation_render_indexed_row,
        .animation_frame_end = files_presenter_animation_frame_end,
        .animation_end = files_presenter_animation_end,
        .context = NULL,
    };
    return &sink;
}

void files_presenter_show_result(file_result_t result)
{
    if(result == FILE_RESULT_READ_ERROR)
        files_presenter_draw_status("READ ERR");
    else if(result == FILE_RESULT_UNSUPPORTED_FORMAT)
        files_presenter_draw_status("UNSUPPORTED");
    else if(result == FILE_RESULT_TOO_LARGE)
        files_presenter_draw_status("TOO LARGE");
    else if(result == FILE_RESULT_DELETE_FAILED)
        files_presenter_draw_status("DELETE FAIL");
    else if(result == FILE_RESULT_NOT_FOUND)
        files_presenter_draw_status("NO FILE");
}

file_result_t files_presenter_render_image(const fat_file_entry_t *entry)
{
    file_result_t result = files_open_image_entry(entry, files_presenter_image_sink());

    if(result != FILE_RESULT_OK)
        files_presenter_show_result(result);
    return result;
}

void files_presenter_tick_image(uint64_t now_us)
{
    file_result_t result = files_image_viewer_tick(now_us);

    if(result != FILE_RESULT_OK)
    {
        files_presenter_show_result(result);
        files_image_viewer_close();
    }
}

void files_presenter_close_image(void)
{
    files_image_viewer_close();
}

uint8_t files_presenter_image_is_animation(void)
{
    return files_image_viewer_is_animation();
}

uint8_t files_presenter_toggle_image_pause(uint64_t now_us)
{
    return files_image_viewer_toggle_pause(now_us);
}

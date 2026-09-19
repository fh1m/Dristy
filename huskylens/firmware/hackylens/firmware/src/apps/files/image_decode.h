#ifndef IMAGE_DECODE_H
#define IMAGE_DECODE_H

#include "../../storage/fat_file_entry.h"
#include "file_result.h"

#include "files_image_config.h"
#include "indexed_image.h"

#include "../../storage/fat32_file.h"
#include "../../storage/fat32_stream.h"

typedef hk_indexed_frame_t file_gif_frame_t;

typedef struct
{
    void (*begin)(void *context);
    void (*render_row_span)(void *context, uint32_t src_y, uint32_t src_h, const uint8_t *row, uint16_t src_w, uint8_t bpp, uint8_t bgr_order);
    uint8_t (*animation_begin)(void *context, uint16_t canvas_w, uint16_t canvas_h, uint16_t background_rgb565);
    uint8_t (*animation_frame_begin)(void *context, const file_gif_frame_t *frame);
    void (*animation_render_indexed_row)(void *context, const file_gif_frame_t *frame,
                                         uint16_t frame_row, const uint8_t *indices,
                                         const uint16_t *palette, uint16_t palette_size);
    uint8_t (*animation_frame_end)(void *context);
    void (*animation_end)(void *context);
    void *context;
} file_image_sink_t;

uint8_t *image_decode_row_buffer(void);
uint32_t image_decode_row_buffer_size(void);

file_result_t files_open_png(const fat_file_entry_t *entry, const file_image_sink_t *sink);
file_result_t files_open_bmp(const fat_file_entry_t *entry, const file_image_sink_t *sink);
file_result_t files_open_raw565(const fat_file_entry_t *entry, const file_image_sink_t *sink);
file_result_t files_open_ppm(const fat_file_entry_t *entry, const file_image_sink_t *sink);
file_result_t files_open_gif(const fat_file_entry_t *entry, const file_image_sink_t *sink);
file_result_t files_gif_tick(uint64_t now_us);
void files_gif_close(void);
uint8_t files_gif_active(void);
uint8_t files_gif_is_animation(void);
uint8_t files_gif_toggle_pause(uint64_t now_us);

#endif

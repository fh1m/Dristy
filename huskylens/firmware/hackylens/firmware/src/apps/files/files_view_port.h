#ifndef HK_FILES_VIEW_PORT_H
#define HK_FILES_VIEW_PORT_H

#include <stdint.h>

#include "files_types.h"
#include "indexed_image.h"

typedef struct
{
    void (*enter)(void);
    void (*draw_status)(const char *line);
    void (*draw_row)(uint8_t row, const file_list_item_t *items, uint8_t count, uint8_t top, uint8_t index);
    void (*render_list)(uint8_t sd_present, uint8_t fat_mounted, const file_list_item_t *items, uint8_t count, uint8_t top, uint8_t index);
    void (*render_preview)(const char *preview, uint16_t len, uint8_t page);
    void (*clear_image)(void);
    void (*render_image_row_span)(uint32_t src_y, uint32_t src_h, const uint8_t *row, uint16_t src_w, uint8_t bpp, uint8_t bgr_order);
    uint8_t (*animation_begin)(uint16_t canvas_w, uint16_t canvas_h, uint16_t background_rgb565);
    uint8_t (*animation_frame_begin)(const hk_indexed_frame_t *frame);
    void (*animation_render_indexed_row)(const hk_indexed_frame_t *frame, uint16_t frame_row,
                                         const uint8_t *indices, const uint16_t *palette,
                                         uint16_t palette_size);
    uint8_t (*animation_frame_end)(void);
    void (*animation_end)(void);
    void (*draw_delete_confirm)(const char *name);
} files_view_ops_t;

void files_view_register(const files_view_ops_t *ops);

#endif

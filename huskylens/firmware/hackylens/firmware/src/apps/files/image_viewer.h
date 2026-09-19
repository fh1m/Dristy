#ifndef IMAGE_VIEWER_H
#define IMAGE_VIEWER_H

#include "../../storage/fat_file_entry.h"
#include "file_result.h"
#include "image_decode.h"


uint8_t files_entry_is_image(const fat_file_entry_t *entry);
file_result_t files_open_image_entry(const fat_file_entry_t *entry, const file_image_sink_t *sink);
file_result_t files_image_viewer_tick(uint64_t now_us);
void files_image_viewer_close(void);
uint8_t files_image_viewer_is_animation(void);
uint8_t files_image_viewer_toggle_pause(uint64_t now_us);

#endif

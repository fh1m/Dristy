#ifndef FILES_PRESENTER_H
#define FILES_PRESENTER_H

#include <stdint.h>

#include "file_result.h"
#include "image_decode.h"

void files_presenter_enter(void);
void files_presenter_show_status(const char *line);
void files_presenter_show_result(file_result_t result);
void files_presenter_render_list(void);
void files_presenter_render_preview(void);
void files_presenter_draw_row(uint8_t row);
void files_presenter_draw_delete_confirm(const char *name);
file_result_t files_presenter_render_image(const fat_file_entry_t *entry);
void files_presenter_tick_image(uint64_t now_us);
void files_presenter_close_image(void);
uint8_t files_presenter_image_is_animation(void);
uint8_t files_presenter_toggle_image_pause(uint64_t now_us);

#endif

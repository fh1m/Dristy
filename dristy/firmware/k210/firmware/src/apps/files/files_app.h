#ifndef HK_FILES_APP_H
#define HK_FILES_APP_H

#include "../../core/hk_app.h"

void files_enter(const hk_input_snapshot_t *input);
void files_exit(void);
void files_tick(const hk_input_snapshot_t *input);
void files_handle_buttons(const hk_input_snapshot_t *input);
void files_handle_sd_event(hk_sd_event_t event);
void files_draw_icon(uint16_t x, uint16_t y, uint16_t color, uint16_t bg);

#endif

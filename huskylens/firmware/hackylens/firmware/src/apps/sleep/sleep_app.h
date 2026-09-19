#ifndef HK_SLEEP_APP_H
#define HK_SLEEP_APP_H

#include "../../core/hk_app.h"

void sleep_enter(const hk_input_snapshot_t *input);
void sleep_handle_buttons(const hk_input_snapshot_t *input);
void sleep_background_tick(const hk_input_snapshot_t *input);
void sleep_draw_icon(uint16_t x, uint16_t y, uint16_t color, uint16_t bg);

#endif

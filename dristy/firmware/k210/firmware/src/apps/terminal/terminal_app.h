#ifndef HK_TERMINAL_APP_H
#define HK_TERMINAL_APP_H

#include "terminal_config.h"

void terminal_enter(const hk_input_snapshot_t *input);
void terminal_exit(void);
void terminal_tick(const hk_input_snapshot_t *input);
void terminal_handle_buttons(const hk_input_snapshot_t *input);
void terminal_draw_icon(uint16_t x, uint16_t y, uint16_t color, uint16_t bg);

#endif

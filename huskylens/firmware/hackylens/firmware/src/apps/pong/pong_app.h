#ifndef PONG_APP_H
#define PONG_APP_H

#include "pong_config.h"

void pong_enter(const hk_input_snapshot_t *input);
void pong_tick(const hk_input_snapshot_t *input);
void pong_handle_buttons(const hk_input_snapshot_t *input);
void pong_draw_icon(uint16_t x, uint16_t y, uint16_t color, uint16_t bg);

#endif

#ifndef PONG_VIEW_H
#define PONG_VIEW_H

#include <stdint.h>

#include "pong_config.h"

typedef struct
{
    int16_t player_x;
    int16_t ai_x;
    int16_t ball_x;
    int16_t ball_y;
    int16_t trail_x[PONG_TRAIL_LENGTH];
    int16_t trail_y[PONG_TRAIL_LENGTH];
    int16_t flash_x;
    int16_t flash_y;
    uint8_t player_score;
    uint8_t ai_score;
    uint8_t trail_count;
    uint8_t flash_ticks;
} pong_view_state_t;

void pong_view_render_initial(pong_view_state_t state);
void pong_view_render_score(pong_view_state_t state);
void pong_view_render_frame(pong_view_state_t previous, pong_view_state_t current);
void pong_view_draw_icon(uint16_t x, uint16_t y, uint16_t color, uint16_t bg);

#endif

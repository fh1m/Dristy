#ifndef BUTTONS_VIEW_H
#define BUTTONS_VIEW_H

#include <stdint.h>

typedef struct
{
    uint32_t state;
    uint32_t hold_passed;
    uint32_t passed;
    uint32_t repeat_error;
    uint16_t pressed_count[4];
    uint16_t released_count[4];
} buttons_view_state_t;

void buttons_view_render(const buttons_view_state_t *state);
void buttons_view_update(
    const buttons_view_state_t *state, uint32_t changed, uint8_t footer_changed);
void buttons_view_draw_icon(uint16_t x, uint16_t y, uint16_t color, uint16_t bg);

#endif

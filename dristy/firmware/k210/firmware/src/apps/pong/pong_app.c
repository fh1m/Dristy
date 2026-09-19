#include "pong_app.h"

#include "pong_config.h"
#include "pong_controller.h"
#include "pong_view.h"

void pong_enter(const hk_input_snapshot_t *input)
{
    pong_controller_enter(input);
}

void pong_tick(const hk_input_snapshot_t *input)
{
    pong_controller_tick(input);
}

void pong_handle_buttons(const hk_input_snapshot_t *input)
{
    pong_controller_handle_buttons(input);
}

void pong_draw_icon(uint16_t x, uint16_t y, uint16_t color, uint16_t bg)
{
    pong_view_draw_icon(x, y, color, bg);
}

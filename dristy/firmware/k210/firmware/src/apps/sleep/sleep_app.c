#include "sleep_app.h"

#include "../../core/hk_app.h"
#include "sleep_controller.h"
#include "sleep_view.h"

void sleep_enter(const hk_input_snapshot_t *input)
{
    sleep_controller_enter(input);
}

void sleep_handle_buttons(const hk_input_snapshot_t *input)
{
    sleep_controller_handle_buttons(input);
}

void sleep_background_tick(const hk_input_snapshot_t *input)
{
    auto_sleep_controller_tick(input);
}

void sleep_draw_icon(uint16_t x, uint16_t y, uint16_t color, uint16_t bg)
{
    sleep_view_draw_icon(x, y, color, bg);
}

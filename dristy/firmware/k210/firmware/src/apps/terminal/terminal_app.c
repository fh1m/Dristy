#include "terminal_app.h"

#include "terminal_controller.h"
#include "terminal_view.h"

void terminal_enter(const hk_input_snapshot_t *input)
{
    terminal_controller_enter(input);
}

void terminal_exit(void)
{
    terminal_controller_exit();
}

void terminal_tick(const hk_input_snapshot_t *input)
{
    terminal_controller_tick(input);
}

void terminal_handle_buttons(const hk_input_snapshot_t *input)
{
    terminal_controller_handle_input(input);
}

void terminal_draw_icon(uint16_t x, uint16_t y, uint16_t color, uint16_t bg)
{
    terminal_view_draw_icon(x, y, color, bg);
}

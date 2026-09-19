#include "vision_mode_app.h"

#include "vision_mode_controller.h"
#include "vision_mode_view.h"

uint8_t vision_mode_open(dristy_mode_t mode, const hk_input_snapshot_t *input)
{
    vision_mode_controller_set_pending_mode(mode);
    return vision_mode_controller_open(input);
}

void vision_mode_enter(const hk_input_snapshot_t *input)
{
    vision_mode_controller_enter(input);
}

void vision_mode_exit(void)
{
    vision_mode_controller_exit();
}

void vision_mode_tick(const hk_input_snapshot_t *input)
{
    vision_mode_controller_tick(input);
}

void vision_mode_handle_buttons(const hk_input_snapshot_t *input)
{
    vision_mode_controller_handle_buttons(input);
}

void vision_mode_draw_icon(uint16_t x, uint16_t y, uint16_t color, uint16_t bg)
{
    vision_mode_view_draw_icon(x, y, color, bg);
}

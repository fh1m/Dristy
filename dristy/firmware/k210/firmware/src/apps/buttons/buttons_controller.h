#ifndef BUTTONS_CONTROLLER_H
#define BUTTONS_CONTROLLER_H

#include "../../core/hk_app.h"

void buttons_controller_enter(const hk_input_snapshot_t *input);
void buttons_controller_tick(const hk_input_snapshot_t *input);
void buttons_controller_handle_buttons(const hk_input_snapshot_t *input);

#endif

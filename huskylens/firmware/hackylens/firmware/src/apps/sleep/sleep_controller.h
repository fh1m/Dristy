#ifndef SLEEP_CONTROLLER_H
#define SLEEP_CONTROLLER_H

#include "../../core/hk_app.h"

void sleep_controller_enter(const hk_input_snapshot_t *input);
void sleep_controller_handle_buttons(const hk_input_snapshot_t *input);
void auto_sleep_controller_tick(const hk_input_snapshot_t *input);

#endif

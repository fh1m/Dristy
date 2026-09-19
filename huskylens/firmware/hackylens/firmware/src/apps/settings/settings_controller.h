#ifndef SETTINGS_CONTROLLER_H
#define SETTINGS_CONTROLLER_H

#include "../../core/hk_app.h"

void settings_controller_enter(const hk_input_snapshot_t *input);
void settings_controller_exit(void);
void settings_controller_tick(const hk_input_snapshot_t *input);
void settings_controller_handle_buttons(const hk_input_snapshot_t *input);

#endif

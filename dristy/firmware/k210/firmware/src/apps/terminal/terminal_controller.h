#ifndef HK_TERMINAL_CONTROLLER_H
#define HK_TERMINAL_CONTROLLER_H

#include "../../core/hk_app.h"

void terminal_controller_enter(const hk_input_snapshot_t *input);
void terminal_controller_exit(void);
void terminal_controller_tick(const hk_input_snapshot_t *input);
void terminal_controller_handle_input(const hk_input_snapshot_t *input);

#endif

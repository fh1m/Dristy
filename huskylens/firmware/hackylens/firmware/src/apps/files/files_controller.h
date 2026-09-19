#ifndef FILES_CONTROLLER_H
#define FILES_CONTROLLER_H

#include "../../core/hk_app.h"


void files_controller_enter(const hk_input_snapshot_t *input);
void files_controller_exit(void);
void files_controller_tick(const hk_input_snapshot_t *input);
void files_controller_handle_buttons(const hk_input_snapshot_t *input);
void files_controller_reset_input(void);

#endif

#ifndef HK_OBJECT_DETECT_CONTROLLER_H
#define HK_OBJECT_DETECT_CONTROLLER_H

#include "../../core/hk_app.h"

void object_detect_controller_enter(const hk_input_snapshot_t *input);
void object_detect_controller_exit(void);
void object_detect_controller_tick(const hk_input_snapshot_t *input);
void object_detect_controller_handle_buttons(const hk_input_snapshot_t *input);
void object_detect_controller_background_tick(void);

#endif

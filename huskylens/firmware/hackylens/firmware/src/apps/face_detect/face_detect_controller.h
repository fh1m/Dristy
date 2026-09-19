#ifndef HK_FACE_DETECT_CONTROLLER_H
#define HK_FACE_DETECT_CONTROLLER_H

#include "../../core/hk_app.h"

void face_detect_controller_enter(const hk_input_snapshot_t *input);
void face_detect_controller_exit(void);
void face_detect_controller_tick(const hk_input_snapshot_t *input);
void face_detect_controller_handle_buttons(const hk_input_snapshot_t *input);

#endif

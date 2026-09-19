#ifndef HK_APRILTAG_CONTROLLER_H
#define HK_APRILTAG_CONTROLLER_H

#include "../../core/hk_app.h"

void apriltag_controller_enter(const hk_input_snapshot_t *input);
void apriltag_controller_exit(void);
void apriltag_controller_tick(const hk_input_snapshot_t *input);
void apriltag_controller_handle_buttons(const hk_input_snapshot_t *input);
int16_t apriltag_controller_target_id(void);

#endif

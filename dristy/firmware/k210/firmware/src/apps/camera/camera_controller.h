#ifndef HK_CAMERA_CONTROLLER_H
#define HK_CAMERA_CONTROLLER_H

#include "../../core/hk_app.h"

void camera_controller_enter(const hk_input_snapshot_t *input);
void camera_controller_exit(void);
void camera_controller_tick(const hk_input_snapshot_t *input);
void camera_controller_handle_input(const hk_input_snapshot_t *input);
uint8_t camera_controller_settings_active(void);

#endif

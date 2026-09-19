#ifndef HK_VISION_MODE_CONTROLLER_H
#define HK_VISION_MODE_CONTROLLER_H

#include "../../core/hk_app.h"
#include "../../dristy/dristy_modes.h"

void vision_mode_controller_set_pending_mode(dristy_mode_t mode);
uint8_t vision_mode_controller_open(const hk_input_snapshot_t *input);
void vision_mode_controller_enter(const hk_input_snapshot_t *input);
void vision_mode_controller_exit(void);
void vision_mode_controller_tick(const hk_input_snapshot_t *input);
void vision_mode_controller_handle_buttons(const hk_input_snapshot_t *input);

dristy_mode_t vision_mode_controller_active_mode(void);
uint8_t vision_mode_controller_apply_host_mode(dristy_mode_t mode);

#endif

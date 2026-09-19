#ifndef HK_VISION_MODE_BRIDGE_H
#define HK_VISION_MODE_BRIDGE_H

#include "../../core/hk_app.h"
#include "../../dristy/dristy_modes.h"

void vision_mode_bridge_start(dristy_mode_t mode);
uint8_t vision_mode_bridge_same_route(dristy_mode_t from, dristy_mode_t to);
void vision_mode_bridge_stop(void);
void vision_mode_bridge_tick(const hk_input_snapshot_t *input);
void vision_mode_bridge_handle_buttons(const hk_input_snapshot_t *input);
uint8_t vision_mode_bridge_active(void);

#endif

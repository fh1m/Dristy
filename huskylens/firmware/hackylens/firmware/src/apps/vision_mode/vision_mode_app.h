#ifndef HK_VISION_MODE_APP_H
#define HK_VISION_MODE_APP_H

#include "../../core/hk_app.h"
#include "../../dristy/dristy_modes.h"

#if HK_ENABLE_DRISTY
extern const hk_app_t g_vision_mode_app;
#endif

uint8_t vision_mode_open(dristy_mode_t mode, const hk_input_snapshot_t *input);

void vision_mode_enter(const hk_input_snapshot_t *input);
void vision_mode_exit(void);
void vision_mode_tick(const hk_input_snapshot_t *input);
void vision_mode_handle_buttons(const hk_input_snapshot_t *input);
void vision_mode_draw_icon(uint16_t x, uint16_t y, uint16_t color, uint16_t bg);

#endif

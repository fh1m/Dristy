#ifndef HK_CAMERA_APP_H
#define HK_CAMERA_APP_H

#include "../../core/hk_app.h"

extern const char g_camera_debug_help[];

void camera_enter(const hk_input_snapshot_t *input);
void camera_exit(void);
void camera_tick(const hk_input_snapshot_t *input);
void camera_handle_buttons(const hk_input_snapshot_t *input);
uint8_t camera_owns_screen(screen_t screen);
uint8_t camera_handle_debug_command(const char *cmd);
void camera_draw_icon(uint16_t x, uint16_t y, uint16_t color, uint16_t bg);

#endif

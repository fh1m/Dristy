#ifndef HK_QR_CAMERA_APP_H
#define HK_QR_CAMERA_APP_H

#include "../../core/hk_app.h"

extern const char g_qr_camera_debug_help[];

void qr_camera_enter(const hk_input_snapshot_t *input);
void qr_camera_exit(void);
void qr_camera_tick(const hk_input_snapshot_t *input);
void qr_camera_handle_buttons(const hk_input_snapshot_t *input);
uint8_t qr_camera_owns_screen(screen_t screen);
uint8_t qr_camera_handle_debug_command(const char *cmd);
void qr_camera_draw_icon(uint16_t x, uint16_t y, uint16_t color, uint16_t bg);

#endif

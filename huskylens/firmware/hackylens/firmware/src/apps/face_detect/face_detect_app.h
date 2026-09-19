#ifndef HK_FACE_DETECT_APP_H
#define HK_FACE_DETECT_APP_H

#include "../../core/hk_app.h"

extern const char g_face_detect_debug_help[];

void face_detect_enter(const hk_input_snapshot_t *input);
void face_detect_exit(void);
void face_detect_tick(const hk_input_snapshot_t *input);
void face_detect_handle_buttons(const hk_input_snapshot_t *input);
void face_detect_background_tick(const hk_input_snapshot_t *input);
uint8_t face_detect_handle_debug_command(const char *cmd);
void face_detect_draw_icon(uint16_t x, uint16_t y, uint16_t color, uint16_t bg);

#endif

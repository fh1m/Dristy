#ifndef HK_OBJECT_DETECT_APP_H
#define HK_OBJECT_DETECT_APP_H

#include "../../core/hk_app.h"

extern const char g_object_detect_debug_help[];

void object_detect_enter(const hk_input_snapshot_t *input);
void object_detect_exit(void);
void object_detect_tick(const hk_input_snapshot_t *input);
void object_detect_handle_buttons(const hk_input_snapshot_t *input);
void object_detect_background_tick(const hk_input_snapshot_t *input);
uint8_t object_detect_handle_debug_command(const char *cmd);
void object_detect_draw_icon(uint16_t x, uint16_t y,
                             uint16_t color, uint16_t bg);

#endif

#ifndef HK_CAMERA_SETTINGS_H
#define HK_CAMERA_SETTINGS_H

#include "../../core/hk_app.h"

typedef enum
{
    CAMERA_SETTINGS_EXIT_NONE = 0,
    CAMERA_SETTINGS_EXIT_RESUME,
    CAMERA_SETTINGS_EXIT_REINIT,
} camera_settings_exit_t;

void camera_settings_open(void);
void camera_settings_close(void);
void camera_settings_tick(const hk_input_snapshot_t *input);
uint8_t camera_settings_active(void);
camera_settings_exit_t camera_settings_handle_input(const hk_input_snapshot_t *input);

#endif

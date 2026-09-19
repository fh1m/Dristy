#ifndef CAMERA_SETTINGS_STATE_H
#define CAMERA_SETTINGS_STATE_H

#include "../../core/camera_types.h"

void camera_settings_force_size(camera_size_t size);
uint8_t camera_settings_consume_size_pending(void);

#endif

#ifndef HK_CAMERA_SIZES_H
#define HK_CAMERA_SIZES_H

#include "camera_types.h"


const char *camera_size_label(camera_size_t size);
uint16_t camera_size_width(camera_size_t size);
uint16_t camera_size_height(camera_size_t size);
uint8_t camera_size_is_safe(camera_size_t size);
camera_size_t camera_size_next(camera_size_t current);

#endif

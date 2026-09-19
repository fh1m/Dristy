#ifndef HK_CAMERA_TYPES_H
#define HK_CAMERA_TYPES_H

#include <stdint.h>

typedef enum
{
    CAMERA_SIZE_160X120 = 0,
    CAMERA_SIZE_320X240,
    CAMERA_SIZE_640X480,
    CAMERA_SIZE_COUNT,
} camera_size_t;

typedef enum
{
    CAMERA_LIGHT_LED = 0,
    CAMERA_LIGHT_RGB,
} camera_light_mode_t;

#endif

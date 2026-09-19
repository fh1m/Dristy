#ifndef HK_CAMERA_PHOTO_CAPTURE_H
#define HK_CAMERA_PHOTO_CAPTURE_H

#include <stddef.h>
#include <stdint.h>

typedef enum
{
    CAMERA_PHOTO_BEGIN_READY = 0,
    CAMERA_PHOTO_BEGIN_NOT_READY,
    CAMERA_PHOTO_BEGIN_NO_FRAME,
} camera_photo_begin_t;

camera_photo_begin_t camera_photo_capture_begin(char *status, size_t status_size);
void camera_photo_capture_end(void);

#endif

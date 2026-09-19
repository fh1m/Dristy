#ifndef CAMERA_PHOTO_CONTROLLER_H
#define CAMERA_PHOTO_CONTROLLER_H

typedef void (*camera_photo_preview_redraw_t)(void);

void camera_photo_controller_take(camera_photo_preview_redraw_t redraw_preview);

#endif

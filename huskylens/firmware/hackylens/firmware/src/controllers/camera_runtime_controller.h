#ifndef HK_CAMERA_RUNTIME_CONTROLLER_H
#define HK_CAMERA_RUNTIME_CONTROLLER_H

#include "../core/hk_app.h"
#include "../ui/camera_view.h"

typedef enum
{
    CAMERA_RUNTIME_PHOTO = 0,
    CAMERA_RUNTIME_QR,
    CAMERA_RUNTIME_FACE_DETECT,
    CAMERA_RUNTIME_APRILTAG,
    CAMERA_RUNTIME_OBJECT_DETECT,
    CAMERA_RUNTIME_CLASSICAL,
} camera_runtime_mode_t;

typedef enum
{
    CAMERA_RUNTIME_INPUT_NONE = 0,
    CAMERA_RUNTIME_INPUT_EXIT,
    CAMERA_RUNTIME_INPUT_OK_RELEASE,
} camera_runtime_input_event_t;

void camera_runtime_enter(camera_runtime_mode_t mode, const hk_input_snapshot_t *input);
uint8_t camera_runtime_tick(const hk_input_snapshot_t *input);
typedef void (*camera_runtime_frame_consumer_t)(const volatile uint16_t *pixels,
                                                uint16_t width,
                                                uint16_t height,
                                                uint32_t sequence,
                                                void *context);
typedef void (*camera_runtime_frame_overlay_t)(camera_view_present_t *present,
                                               uint16_t width,
                                               uint16_t height,
                                               uint32_t sequence,
                                               void *context);
uint8_t camera_runtime_tick_with_consumer(const hk_input_snapshot_t *input,
                                          camera_runtime_frame_consumer_t consumer,
                                          void *context);
uint8_t camera_runtime_tick_with_pipeline(const hk_input_snapshot_t *input,
                                          camera_runtime_frame_consumer_t consumer,
                                          void *consumer_context,
                                          camera_runtime_frame_overlay_t overlay,
                                          void *overlay_context);
camera_runtime_input_event_t camera_runtime_handle_input(const hk_input_snapshot_t *input);
uint8_t camera_runtime_ok_hold_triggered(const hk_input_snapshot_t *input);
void camera_runtime_redraw_preview(void);

#endif

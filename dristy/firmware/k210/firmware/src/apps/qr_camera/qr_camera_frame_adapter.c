#include "qr_camera_frame_adapter.h"

#include <stddef.h>

#include "../../services/camera_frame.h"
#include "../../services/camera_input.h"
#include "../../services/camera_session.h"

uint8_t qr_camera_frame_acquire(uint8_t force, qr_camera_decode_frame_t *frame)
{
    const uint8_t *data;
    uint16_t width;
    uint16_t height;

    if(!camera_service_frame_snapshot(force ? 250U : 0U, &data, NULL, &width, &height))
        return 0;

    camera_service_freeze(1);
    if(frame)
    {
        frame->pixels = (const uint16_t *)data;
        frame->width = width;
        frame->height = height;
        frame->using_snapshot = 1;
    }
    return 1;
}

void qr_camera_frame_resume_if_needed(uint8_t using_snapshot)
{
    if(!using_snapshot)
        return;

    camera_service_freeze(0);
    camera_service_frame_snapshot_done(1);
}

void qr_camera_frame_result_close(uint8_t ok_is_down)
{
    camera_service_freeze(0);
    camera_service_frame_snapshot_done(1);
    camera_input_ignore_until_release(ok_is_down);
}

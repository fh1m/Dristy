#include "camera_frame.h"

#include "camera_capture.h"
#include "internal/camera_session_state.h"

uint16_t camera_service_frame_pixel(uint32_t x, uint32_t y)
{
    const uint16_t *src = camera_capture_snapshot_pixels();
    if(!src)
        return 0;
    return src[y * camera_session_width() + x];
}

void camera_service_frame_info(uint16_t *width, uint16_t *height)
{
    if(width)
        *width = camera_session_width();
    if(height)
        *height = camera_session_height();
}

uint8_t camera_service_preview_acquire(camera_preview_frame_t *frame)
{
    camera_capture_frame_t capture;

    if(!frame)
        return 0;
    if(!camera_capture_acquire(&capture))
    {
        const uint16_t *snapshot = camera_capture_snapshot_pixels();
        if(!snapshot)
            return 0;
        capture.pixels = snapshot;
        capture.lease_id = 0;
        capture.sequence = 0;
    }
    frame->pixels = (volatile uint16_t *)capture.pixels;
    frame->width = camera_session_width();
    frame->height = camera_session_height();
    frame->rotate = camera_session_preview_rotate();
    frame->lease_id = capture.lease_id;
    frame->sequence = capture.sequence;
    return 1;
}

void camera_service_preview_release(camera_preview_frame_t *frame)
{
    if(!frame)
        return;
    if(frame->lease_id)
        camera_capture_release(frame->lease_id);
    frame->lease_id = 0;
}

uint8_t camera_service_frame_snapshot(uint32_t wait_ms, const uint8_t **data, uint32_t *bytes, uint16_t *width, uint16_t *height)
{
    if(!camera_session_ready() || !camera_session_snapshot_capture(wait_ms))
        return 0;

    if(width)
        *width = camera_session_width();
    if(height)
        *height = camera_session_height();
    if(bytes)
        *bytes = (uint32_t)camera_session_width() * camera_session_height() * 2U;
    if(data)
        *data = (const uint8_t *)camera_capture_snapshot_pixels();
    return 1;
}

void camera_service_frame_snapshot_done(uint8_t resume_preview)
{
    camera_session_snapshot_resume(resume_preview);
}

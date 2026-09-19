#include "camera_capture.h"

#include <stdio.h>

#include "../config/camera_config.h"
#include "../drivers/camera_stream.h"
#include "hal_time.h"

static uint8_t g_camera_frame_timeout_shown;
static uint8_t g_camera_frame_timeout_event;
static uint32_t g_camera_timeout_count;
static uint64_t g_camera_wait_started_us;
static camera_stream_frame_t g_snapshot_frame;

static const char *camera_capture_prefix(const char *prefix)
{
    return prefix ? prefix : "[CAM]";
}

static void camera_capture_wait_reset(void)
{
    g_camera_wait_started_us = hal_time_us();
}

uint8_t camera_capture_start(uint16_t width, uint16_t height, uint8_t burst)
{
    camera_capture_reset_session();
    camera_capture_reset_counters();
    camera_capture_wait_reset();
    return camera_stream_start(width, height, burst);
}

void camera_capture_reset_flow(void)
{
    camera_capture_resume_preview();
}

void camera_capture_reset_session(void)
{
    if(g_snapshot_frame.lease_id)
        camera_stream_release(g_snapshot_frame.lease_id);
    g_snapshot_frame.pixels = NULL;
    g_snapshot_frame.lease_id = 0;
    g_snapshot_frame.sequence = 0;
    camera_stream_stop();
    g_camera_frame_timeout_shown = 0;
    g_camera_frame_timeout_event = 0;
    g_camera_wait_started_us = 0;
}

void camera_capture_reset_counters(void)
{
    g_camera_timeout_count = 0;
}

void camera_capture_clear_timeout(void)
{
    g_camera_frame_timeout_shown = 0;
    g_camera_frame_timeout_event = 0;
    camera_capture_wait_reset();
}

void camera_capture_status(camera_capture_status_t *status)
{
    camera_stream_status_t stream;
    uint64_t elapsed_us;

    if(!status)
        return;
    camera_stream_status(&stream);
    elapsed_us = g_camera_wait_started_us ? hal_time_us() - g_camera_wait_started_us : 0;
    status->have_frame = stream.have_frame;
    status->capture_state = stream.state;
    status->frame_wait_ms = elapsed_us > 65535000ULL ? 65535U : (uint16_t)(elapsed_us / 1000ULL);
    status->frame_timeout_shown = g_camera_frame_timeout_shown;
    status->frame_start_count = stream.irq_start_count;
    status->convert_count = stream.convert_count;
    status->frame_finish_count = stream.irq_finish_count;
    status->captured_count = stream.captured_count;
    status->timeout_count = g_camera_timeout_count;
    status->ready_drop_count = stream.ready_drop_count;
    status->busy_drop_count = stream.busy_drop_count;
    status->last_sts = stream.last_status;
}

uint8_t camera_capture_tick(const char *log_prefix)
{
    camera_stream_status_t stream;
    uint64_t now = hal_time_us();

    camera_stream_status(&stream);
    if(stream.have_frame)
    {
        g_camera_frame_timeout_shown = 0;
        g_camera_wait_started_us = now;
        return 1;
    }
    if(!stream.running || stream.paused)
        return 0;
    if(g_camera_wait_started_us == 0)
        g_camera_wait_started_us = now;
    if(now - g_camera_wait_started_us >= CAMERA_FRAME_TIMEOUT_US)
    {
        g_camera_timeout_count++;
        g_camera_wait_started_us = now;
        if(!g_camera_frame_timeout_shown)
        {
            printf("%s frame timeout sts=0x%08X\r\n", camera_capture_prefix(log_prefix), stream.last_status);
            g_camera_frame_timeout_event = 1;
            g_camera_frame_timeout_shown = 1;
        }
    }
    return 0;
}

uint8_t camera_capture_have_frame(void)
{
    return camera_stream_have_frame();
}

uint8_t camera_capture_consume_timeout(void)
{
    if(!g_camera_frame_timeout_event)
        return 0;
    g_camera_frame_timeout_event = 0;
    return 1;
}

uint8_t camera_capture_acquire(camera_capture_frame_t *frame)
{
    camera_stream_frame_t stream_frame;

    if(!frame || !camera_stream_acquire_latest(&stream_frame))
        return 0;
    frame->pixels = stream_frame.pixels;
    frame->lease_id = stream_frame.lease_id;
    frame->sequence = stream_frame.sequence;
    return 1;
}

void camera_capture_release(uint32_t lease_id)
{
    camera_stream_release(lease_id);
}

uint8_t camera_capture_snapshot(uint16_t width, uint16_t height, uint32_t wait_ms, const char *log_prefix)
{
    uint32_t bytes = (uint32_t)width * height * 2U;
    uint64_t deadline = hal_time_us() + (uint64_t)wait_ms * 1000ULL;
    camera_capture_frame_t frame;
    (void)log_prefix;

    if(bytes > camera_stream_frame_bytes() || g_snapshot_frame.lease_id)
        return 0;
    do
    {
        if(camera_capture_acquire(&frame))
        {
            camera_stream_pause();
            g_snapshot_frame.pixels = frame.pixels;
            g_snapshot_frame.lease_id = frame.lease_id;
            g_snapshot_frame.sequence = frame.sequence;
            return 1;
        }
        if(wait_ms == 0)
            break;
        hal_sleep_ms(1);
    } while(hal_time_us() < deadline);
    return 0;
}

void camera_capture_pause(void)
{
    camera_stream_pause();
}

void camera_capture_snapshot_done(uint8_t resume_preview)
{
    if(g_snapshot_frame.lease_id)
        camera_stream_release(g_snapshot_frame.lease_id);
    g_snapshot_frame.pixels = NULL;
    g_snapshot_frame.lease_id = 0;
    g_snapshot_frame.sequence = 0;
    if(resume_preview)
    {
        camera_stream_resume();
        camera_capture_clear_timeout();
    }
}

void camera_capture_resume_preview(void)
{
    camera_capture_snapshot_done(1);
}

const uint16_t *camera_capture_snapshot_pixels(void)
{
    return (const uint16_t *)g_snapshot_frame.pixels;
}

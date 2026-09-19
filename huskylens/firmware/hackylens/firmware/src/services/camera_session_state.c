#include "internal/camera_session_state.h"

#include "camera_status.h"

#include "../core/camera_types.h"

#include "../config/display_config.h"

#include "../core/hk_camera_sizes.h"

#include "camera_capture.h"
#include "camera_session.h"
#include "../drivers/ov2640_sensor.h"

static uint8_t g_camera_initialized;
static uint8_t g_camera_ever_initialized;
static uint8_t g_camera_ready;
static uint8_t g_camera_frozen;
static uint8_t g_camera_preview_rotate;
static uint8_t g_camera_colorbar_enabled;
static uint16_t g_camera_w = HK_DISPLAY_REQUIRED_WIDTH;
static uint16_t g_camera_h = HK_DISPLAY_REQUIRED_HEIGHT;
static const char *g_camera_fail_reason = "NO SENSOR";
static uint16_t g_camera_mid;
static uint16_t g_camera_pid;
static uint8_t g_qr_camera_mode;
static uint8_t g_camera_qvga_mode;

const char *camera_log_prefix(void)
{
    return g_qr_camera_mode ? "[QR]" : "[CAM]";
}

void camera_active_size_set(camera_size_t size)
{
    g_camera_w = camera_size_width(size);
    g_camera_h = camera_size_height(size);
}

uint8_t camera_session_initialized(void)
{
    return g_camera_initialized;
}

uint8_t camera_session_ever_initialized(void)
{
    return g_camera_ever_initialized;
}

uint8_t camera_session_ready(void)
{
    return g_camera_ready;
}

uint8_t camera_session_qr_mode(void)
{
    return g_qr_camera_mode;
}

uint8_t camera_session_qvga_mode(void)
{
    return g_camera_qvga_mode;
}

uint16_t camera_session_width(void)
{
    return g_camera_w;
}

uint16_t camera_session_height(void)
{
    return g_camera_h;
}

uint8_t camera_session_preview_rotate(void)
{
    return g_camera_preview_rotate;
}

uint8_t camera_session_colorbar_enabled(void)
{
    return g_camera_colorbar_enabled;
}

void camera_session_set_qr_mode(uint8_t qr_mode)
{
    g_qr_camera_mode = qr_mode ? 1 : 0;
}

void camera_session_set_qvga_mode(uint8_t enabled)
{
    g_camera_qvga_mode = enabled ? 1 : 0;
}

void camera_session_set_ready(uint8_t ready)
{
    g_camera_ready = ready ? 1 : 0;
}

void camera_session_set_preview_rotate(uint8_t rotate)
{
    g_camera_preview_rotate = rotate ? 1 : 0;
}

void camera_session_set_frozen(uint8_t frozen)
{
    g_camera_frozen = frozen ? 1 : 0;
}

void camera_session_set_fail_reason(const char *reason)
{
    g_camera_fail_reason = reason;
}

void camera_session_mark_started(void)
{
    g_camera_initialized = 1;
    g_camera_ever_initialized = 1;
    g_camera_ready = 1;
}

void camera_session_mark_stopped(void)
{
    g_camera_initialized = 0;
    g_camera_ready = 0;
    g_camera_frozen = 0;
    g_camera_preview_rotate = 0;
}

void camera_session_sensor_id(uint16_t *mid, uint16_t *pid)
{
    if(mid)
        *mid = g_camera_mid;
    if(pid)
        *pid = g_camera_pid;
}

void camera_service_status(camera_service_status_t *status)
{
    camera_capture_status_t capture;

    if(!status)
        return;

    camera_capture_status(&capture);
    status->initialized = g_camera_initialized;
    status->ready = g_camera_ready;
    status->have_frame = capture.have_frame;
    status->colorbar_enabled = g_camera_colorbar_enabled;
    status->capture_state = capture.capture_state;
    status->frame_wait_ms = capture.frame_wait_ms;
    status->frame_timeout_shown = capture.frame_timeout_shown;
    status->width = g_camera_w;
    status->height = g_camera_h;
    status->mid = g_camera_mid;
    status->pid = g_camera_pid;
    status->last_sts = capture.last_sts;
    status->frame_start_count = capture.frame_start_count;
    status->convert_count = capture.convert_count;
    status->frame_finish_count = capture.frame_finish_count;
    status->captured_count = capture.captured_count;
    status->timeout_count = capture.timeout_count;
    status->ready_drop_count = capture.ready_drop_count;
    status->busy_drop_count = capture.busy_drop_count;
    status->fail_reason = g_camera_fail_reason;
}

void camera_service_note_probe_result(uint16_t mid, uint16_t pid)
{
    g_camera_mid = mid;
    g_camera_pid = pid;
}

uint8_t camera_service_is_qr_mode(void)
{
    return g_qr_camera_mode;
}

void camera_service_clear_mode(void)
{
    g_qr_camera_mode = 0;
    g_camera_qvga_mode = 0;
}

void camera_service_set_qvga_mode(uint8_t enabled)
{
    camera_session_set_qvga_mode(enabled);
}

uint8_t camera_service_capture_ready(void)
{
    return g_camera_ready && !g_camera_frozen;
}

const char *camera_service_fail_reason(void)
{
    return g_camera_fail_reason ? g_camera_fail_reason : "UNKNOWN";
}

uint8_t camera_service_toggle_colorbar(void)
{
    g_camera_colorbar_enabled = g_camera_colorbar_enabled ? 0 : 1;
    if(g_camera_ready)
        ov2640_apply_colorbar(g_camera_colorbar_enabled);
    return g_camera_colorbar_enabled;
}

void camera_service_freeze(uint8_t frozen)
{
    camera_session_set_frozen(frozen);
    if(frozen)
        camera_capture_pause();
}

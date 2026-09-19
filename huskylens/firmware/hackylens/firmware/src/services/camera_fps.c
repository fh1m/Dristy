#include "camera_frame.h"

#include <stddef.h>

#include <stdio.h>
#include "camera_persist_settings.h"
#include "camera_status.h"
#include "camera_fps.h"
#include "camera_session_preferences.h"

#include "settings_persistence.h"
#include "hal_time.h"

static uint8_t g_camera_fps_enabled = 1;
static uint32_t g_camera_fps_total_frames;
static uint32_t g_camera_fps_window_frames;
static uint32_t g_camera_fps_value_x10;
static uint32_t g_camera_fps_avg_x10;
static uint64_t g_camera_fps_start_us;
static uint64_t g_camera_fps_window_us;
static uint64_t g_camera_compose_total_us;
static uint64_t g_camera_present_total_us;
static uint32_t g_camera_timing_samples;
static uint32_t g_camera_compose_max_us;
static uint32_t g_camera_present_max_us;

static uint32_t camera_fps_calc_x10(uint32_t frames, uint64_t elapsed_us)
{
    if(elapsed_us == 0)
        return 0;
    return (uint32_t)(((uint64_t)frames * 10000000ULL + elapsed_us / 2ULL) / elapsed_us);
}

static uint32_t camera_fps_timing_average(uint64_t total_us)
{
    if(g_camera_timing_samples == 0)
        return 0;
    return (uint32_t)(total_us / g_camera_timing_samples);
}

uint8_t camera_fps_enabled_state(void)
{
    return g_camera_fps_enabled;
}

void camera_fps_apply_persisted(uint8_t enabled)
{
    g_camera_fps_enabled = enabled ? 1 : 0;
}

uint32_t camera_fps_current_x10(void)
{
    return g_camera_fps_value_x10;
}

uint32_t camera_fps_average_x10(void)
{
    return g_camera_fps_avg_x10;
}

void camera_fps_mark_stopped(void)
{
    g_camera_fps_start_us = 0;
}

void camera_service_fps_reset(void)
{
    uint64_t now = hal_time_us();

    g_camera_fps_total_frames = 0;
    g_camera_fps_window_frames = 0;
    g_camera_fps_value_x10 = 0;
    g_camera_fps_avg_x10 = 0;
    g_camera_compose_total_us = 0;
    g_camera_present_total_us = 0;
    g_camera_timing_samples = 0;
    g_camera_compose_max_us = 0;
    g_camera_present_max_us = 0;
    g_camera_fps_start_us = now;
    g_camera_fps_window_us = now;
    printf("%s fps counter %s\r\n", camera_log_prefix(),
           camera_session_preferences_fps_enabled() ? "ON" : "OFF");
}

void camera_service_fps_note_present(uint32_t compose_us, uint32_t present_us)
{
    g_camera_compose_total_us += compose_us;
    g_camera_present_total_us += present_us;
    g_camera_timing_samples++;
    if(compose_us > g_camera_compose_max_us)
        g_camera_compose_max_us = compose_us;
    if(present_us > g_camera_present_max_us)
        g_camera_present_max_us = present_us;
}

void camera_service_fps_on_frame(void)
{
    uint64_t now = hal_time_us();
    uint64_t elapsed;

    if(g_camera_fps_start_us == 0)
    {
        camera_service_fps_reset();
        now = hal_time_us();
    }

    g_camera_fps_total_frames++;
    g_camera_fps_window_frames++;

    elapsed = now - g_camera_fps_start_us;
    g_camera_fps_avg_x10 = camera_fps_calc_x10(g_camera_fps_total_frames, elapsed);

    elapsed = now - g_camera_fps_window_us;
    if(elapsed >= 1000000ULL)
    {
        g_camera_fps_value_x10 = camera_fps_calc_x10(g_camera_fps_window_frames, elapsed);
        g_camera_fps_window_frames = 0;
        g_camera_fps_window_us = now;
        if(camera_session_preferences_fps_enabled())
        {
            uint16_t width = 0;
            uint16_t height = 0;
            camera_service_frame_info(&width, &height);
            printf("%s fps=%u.%u avg=%u.%u frames=%u size=%ux%u compose_us=%u compose_max=%u present_us=%u present_max=%u\r\n",
                   camera_log_prefix(),
                   (unsigned)(g_camera_fps_value_x10 / 10U),
                   (unsigned)(g_camera_fps_value_x10 % 10U),
                   (unsigned)(g_camera_fps_avg_x10 / 10U),
                   (unsigned)(g_camera_fps_avg_x10 % 10U),
                   (unsigned)g_camera_fps_total_frames,
                   width,
                   height,
                   (unsigned)camera_fps_timing_average(g_camera_compose_total_us),
                   (unsigned)g_camera_compose_max_us,
                   (unsigned)camera_fps_timing_average(g_camera_present_total_us),
                   (unsigned)g_camera_present_max_us);
        }
    }
}

void camera_fps_log_summary(const char *prefix, uint16_t width, uint16_t height)
{
    uint64_t now;

    if(g_camera_fps_total_frames == 0)
        return;

    now = hal_time_us();
    g_camera_fps_avg_x10 = camera_fps_calc_x10(g_camera_fps_total_frames, now - g_camera_fps_start_us);
    printf("%s fps summary avg=%u.%u frames=%u size=%ux%u compose_us=%u compose_max=%u present_us=%u present_max=%u\r\n",
           prefix ? prefix : "[CAM]",
           (unsigned)(g_camera_fps_avg_x10 / 10U),
           (unsigned)(g_camera_fps_avg_x10 % 10U),
           (unsigned)g_camera_fps_total_frames,
           width,
           height,
           (unsigned)camera_fps_timing_average(g_camera_compose_total_us),
           (unsigned)g_camera_compose_max_us,
           (unsigned)camera_fps_timing_average(g_camera_present_total_us),
           (unsigned)g_camera_present_max_us);
}

uint8_t camera_service_fps_overlay_enabled(void)
{
    return camera_session_preferences_fps_enabled();
}

void camera_service_format_fps_overlay(char *line, size_t line_size)
{
    if(g_camera_fps_value_x10 == 0)
        snprintf(line, line_size, "FPS --.-");
    else
        snprintf(line, line_size, "FPS %u.%u", (unsigned)(g_camera_fps_value_x10 / 10U), (unsigned)(g_camera_fps_value_x10 % 10U));
}

uint8_t camera_service_fps_enabled(void)
{
    return g_camera_fps_enabled;
}

void camera_service_format_fps(char *line, size_t line_size)
{
    snprintf(line, line_size,
             "HKFPS on=%u fps=%u.%u avg=%u.%u compose_us=%u compose_max=%u present_us=%u present_max=%u\r\n",
             g_camera_fps_enabled,
             (unsigned)(g_camera_fps_value_x10 / 10U),
             (unsigned)(g_camera_fps_value_x10 % 10U),
             (unsigned)(g_camera_fps_avg_x10 / 10U),
             (unsigned)(g_camera_fps_avg_x10 % 10U),
             (unsigned)camera_fps_timing_average(g_camera_compose_total_us),
             (unsigned)g_camera_compose_max_us,
             (unsigned)camera_fps_timing_average(g_camera_present_total_us),
             (unsigned)g_camera_present_max_us);
}

void camera_service_set_fps_enabled(uint8_t enabled)
{
    g_camera_fps_enabled = enabled ? 1 : 0;
    settings_mark_dirty(1);
}

uint8_t camera_service_toggle_fps(void)
{
    g_camera_fps_enabled = g_camera_fps_enabled ? 0 : 1;
    settings_mark_dirty(1);
    return g_camera_fps_enabled;
}

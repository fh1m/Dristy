#include "camera_frame.h"

#include <stdio.h>
#include "pins.h"
#include "camera_light.h"
#include "camera_persist_settings.h"
#include "camera_status.h"

#include "../core/camera_types.h"

#include "../config/camera_config.h"

#include "settings_lights.h"
#include "../core/hk_camera_sizes.h"
#include "../drivers/ov2640_sensor.h"
#include "hal_dvp.h"
#include "hal_time.h"
#include "camera_capture.h"
#include "camera_fps.h"
#include "camera_input.h"
#include "camera_session.h"
#include "internal/camera_session_state.h"
#include "internal/camera_settings_state.h"

uint8_t camera_session_snapshot_capture(uint32_t wait_ms)
{
    if(!camera_session_ready())
        return 0;

    return camera_capture_snapshot(camera_session_width(), camera_session_height(), wait_ms, camera_log_prefix());
}

void camera_session_snapshot_resume(uint8_t resume_preview)
{
    camera_capture_snapshot_done(camera_session_ready() && resume_preview);
}

void camera_service_enter_begin(uint8_t qr_mode, uint8_t ok_is_down)
{
    camera_session_set_qr_mode(qr_mode);

    camera_session_set_frozen(0);
    camera_capture_reset_session();
    camera_input_reset(ok_is_down);
    camera_service_set_light_active(1);
    camera_service_set_light_level(0);
    camera_light_apply();
    camera_service_fps_reset();
}

uint8_t camera_service_have_frame(void)
{
    return camera_capture_have_frame();
}

uint8_t camera_service_capture_frame_tick(void)
{
    if(!camera_capture_tick(camera_log_prefix()))
        return 0;

    return 1;
}

uint8_t camera_service_start(void)
{
    uint32_t actual_xclk;
    uint32_t actual_sccb;
    uint16_t width;
    uint16_t height;
    uint16_t mid;
    uint16_t pid;
    camera_size_t requested;

    if(camera_session_initialized())
        return camera_session_ready();

    requested = camera_session_qr_mode() ? CAMERA_SIZE_640X480 :
        (camera_session_qvga_mode() ? CAMERA_SIZE_320X240 : camera_service_size());

    printf(camera_session_ever_initialized() ? "%s reinit\r\n" : "%s init\r\n", camera_log_prefix());
    printf(
        "%s pins PCLK=" IO_CAM_PCLK_LABEL
        " XCLK=" IO_CAM_XCLK_LABEL
        " HREF=" IO_CAM_HREF_LABEL
        " PWDN=" IO_CAM_PWDN_LABEL
        " VSYNC=" IO_CAM_VSYNC_LABEL
        " RST=" IO_CAM_RST_LABEL
        " SCCB=" IO_CAM_SCCB_SCLK_LABEL "/" IO_CAM_SCCB_SDA_LABEL "\r\n",
        camera_log_prefix()
    );
    camera_session_set_fail_reason("NO SENSOR");

    if(!camera_size_is_safe(requested))
    {
        requested = CAMERA_SIZE_320X240;
        if(!camera_session_qr_mode())
            camera_settings_force_size(requested);
    }
    printf("%s safe size=%s\r\n", camera_log_prefix(), camera_size_label(requested));

    camera_active_size_set(requested);
    camera_session_set_preview_rotate(0);
    width = camera_session_width();
    height = camera_session_height();

    ov2640_init_bus(&actual_xclk, &actual_sccb);
    printf("%s xclk=%u sccb=%u\r\n", camera_log_prefix(), actual_xclk, actual_sccb);

    ov2640_probe_id(&mid, &pid);
    camera_service_note_probe_result(mid, pid);
    printf("%s sccb addr=0x%02X mid=0x%04X pid=0x%04X\r\n", camera_log_prefix(), CAMERA_SCCB_ADDR, mid, pid);

    if(mid != 0x7FA2 || pid != 0x2642)
    {
        printf("%s sensor probe retry\r\n", camera_log_prefix());
        ov2640_init_bus(&actual_xclk, &actual_sccb);
        printf("%s xclk_retry=%u sccb=%u\r\n", camera_log_prefix(), actual_xclk, actual_sccb);
        hal_sleep_ms(20);
        ov2640_probe_id(&mid, &pid);
        camera_service_note_probe_result(mid, pid);
        printf("%s sccb retry addr=0x%02X mid=0x%04X pid=0x%04X\r\n", camera_log_prefix(), CAMERA_SCCB_ADDR, mid, pid);
    }

    if(mid != 0x7FA2 || pid != 0x2642)
    {
        printf("%s sensor probe failed mid=0x%04X pid=0x%04X\r\n", camera_log_prefix(), mid, pid);
        camera_session_set_fail_reason("NO SENSOR ID");
        camera_session_set_ready(0);
        return 0;
    }

    printf("%s sensor OV2640\r\n", camera_log_prefix());
    camera_session_set_fail_reason("NONE");
    ov2640_apply_init_table();
    ov2640_apply_common_tuning();
    ov2640_apply_output_size(width, height);
    ov2640_apply_colorbar(camera_session_colorbar_enabled());

    if((width % 32U) == 0)
    {
        printf("%s dvp burst=1 width=%u\r\n", camera_log_prefix(), width);
    }
    else
    {
        printf("%s dvp burst=0 width=%u\r\n", camera_log_prefix(), width);
    }
    if(!camera_capture_start(width, height, (width % 32U) == 0))
    {
        camera_session_set_fail_reason("DVP IRQ");
        camera_session_set_ready(0);
        return 0;
    }
    camera_session_mark_started();
    printf("%s dvp %ux%u RGB565 display\r\n", camera_log_prefix(), width, height);
    return 1;
}

uint8_t camera_service_consume_frame_timeout(void)
{
    return camera_capture_consume_timeout();
}

void camera_stop(void)
{
    if(camera_service_light_active())
        camera_light_restore_global();

    if(camera_session_initialized())
    {
        camera_fps_log_summary(camera_log_prefix(), camera_session_width(), camera_session_height());
        hal_dvp_output_display(0);
        hal_dvp_output_ai(0);
        camera_session_mark_stopped();
        camera_capture_reset_session();
        camera_input_cancel();
        camera_fps_mark_stopped();
        printf("%s stop\r\n", camera_log_prefix());
    }
}

camera_settings_return_t camera_service_prepare_settings_return(uint8_t qr_mode,
                                                                 uint8_t ok_is_down)
{
    if(qr_mode)
    {
        camera_session_set_frozen(0);
        camera_capture_reset_flow();
        camera_input_return_from_settings(ok_is_down);
        return CAMERA_SETTINGS_RETURN_QR_CAMERA;
    }

    if(camera_settings_consume_size_pending() || !camera_session_ready())
    {
        return CAMERA_SETTINGS_RETURN_REINIT;
    }

    camera_session_set_frozen(0);
    camera_capture_reset_flow();
    camera_input_return_from_settings(ok_is_down);
    return CAMERA_SETTINGS_RETURN_CAMERA;
}

void camera_service_resume_from_settings(uint8_t ok_is_down)
{
    camera_session_set_frozen(0);
    camera_capture_reset_flow();
    camera_input_return_from_settings(ok_is_down);
}

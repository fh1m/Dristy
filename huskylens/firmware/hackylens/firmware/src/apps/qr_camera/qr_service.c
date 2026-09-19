#include "qr_service.h"

#include <stdio.h>

#include <hackylens/capability/time.h>

#include "../../services/camera_frame.h"
#include "qr_camera_frame_adapter.h"

#include "qr_config.h"

#include "qr_decoder_engine.h"
#include "qr_result.h"
#include "../../core/hk_binary.h"
#include "../../config/settings_config.h"
#include "../../services/settings_persistence.h"
#include "../../services/settings_service.h"
#include "../../core/hk_capability_client.h"

static uint64_t g_qr_last_decode_us;
static uint64_t g_qr_last_status_log_us;
static int g_qr_last_code_count = -1;
static hk_time_t s_qr_time;
static hk_owner_t s_qr_time_owner;

static uint64_t qr_time_now_us(void)
{
    static const hk_capability_request_t request = HK_TIME_REQUEST_0_1_INIT;
    hk_owner_t owner = capability_client_current_owner();
    uint64_t value = 0U;

    if(hk_owner_is_zero(owner))
        return 0U;
    if(owner.slot != s_qr_time_owner.slot ||
       owner.generation != s_qr_time_owner.generation ||
       hk_lease_is_zero(&s_qr_time.lease))
    {
        s_qr_time.lease = HK_LEASE_NONE;
        s_qr_time_owner = owner;
        if(hk_time_acquire(owner, &request, &s_qr_time) != HK_OK)
            return 0U;
    }
    if(hk_time_now_us(owner, &s_qr_time, &value) != HK_OK)
        return 0U;
    return value;
}

void qr_service_enter(void)
{
    g_qr_last_decode_us = 0;
    g_qr_last_status_log_us = 0;
    g_qr_last_code_count = -1;
    qr_decoder_engine_reset_session();
    qr_result_reset();
}

static void qr_log_info(uint8_t force)
{
    qr_engine_stats_t stats;
    uint64_t now = qr_time_now_us();

    if(!force && g_qr_last_status_log_us != 0 && now - g_qr_last_status_log_us < QR_STATUS_LOG_INTERVAL_US)
        return;

    qr_decoder_engine_stats(&stats);
    g_qr_last_status_log_us = now;
    printf("[QR] info decodes=%u codes=%d status=%s ok=%u frame=%u luma=%u/%u/%u\r\n",
           (unsigned)stats.decode_count,
           stats.code_count,
           qr_result_status(),
           stats.last_decode_ok,
           camera_service_have_frame(),
           stats.luma_min,
           stats.luma_avg,
           stats.luma_max);
}

static void qr_log_code_count_if_changed(void)
{
    qr_engine_stats_t stats;

    qr_decoder_engine_stats(&stats);
    if(stats.code_count == g_qr_last_code_count)
        return;

    printf("[QR] codes=%d decodes=%u\r\n", stats.code_count, (unsigned)stats.decode_count);
    g_qr_last_code_count = stats.code_count;
}

static uint64_t qr_decode_interval_us(void)
{
    uint8_t rate = settings_qr_decode_rate();
    return 1000000ULL / rate;
}

static qr_decode_result_t qr_decode_current_frame(uint8_t force)
{
    qr_camera_decode_frame_t camera_frame;
    qr_engine_frame_t engine_frame;
    qr_engine_result_t engine_result;

    if(!qr_camera_frame_acquire(force, &camera_frame))
    {
        qr_result_set_status("NO FRAME");
        if(force)
            printf("[QR] decode no frame\r\n");
        qr_log_info(force);
        return QR_DECODE_NO_FRAME;
    }

    engine_frame.pixels = camera_frame.pixels;
    engine_frame.width = camera_frame.width;
    engine_frame.height = camera_frame.height;
    engine_result = qr_decoder_engine_decode(&engine_frame, force);

    if(engine_result == QR_ENGINE_DECODER_FAIL)
    {
        qr_camera_frame_resume_if_needed(camera_frame.using_snapshot);
        return QR_DECODE_DECODER_FAIL;
    }

    if(engine_result == QR_ENGINE_FOUND)
    {
        qr_log_info(force);
        return QR_DECODE_FOUND;
    }

    qr_log_code_count_if_changed();
    if(force)
        printf("[QR] %s\r\n", qr_result_status());

    qr_camera_frame_resume_if_needed(camera_frame.using_snapshot);
    qr_log_info(force);
    return QR_DECODE_NOT_FOUND;
}

qr_decode_result_t qr_service_decode_maybe(uint8_t force)
{
    uint64_t now = qr_time_now_us();
    uint64_t interval_us = qr_decode_interval_us();

    if(force)
    {
        g_qr_last_decode_us = now;
    }
    else if(g_qr_last_decode_us == 0)
    {
        g_qr_last_decode_us = now;
    }
    else
    {
        if(now - g_qr_last_decode_us < interval_us)
            return QR_DECODE_IDLE;

        do
        {
            g_qr_last_decode_us += interval_us;
        } while(now - g_qr_last_decode_us >= interval_us);
    }

    return qr_decode_current_frame(force);
}

qr_decode_result_t qr_service_decode_force(void)
{
    return qr_service_decode_maybe(1);
}

void qr_service_format_info(char *line, size_t line_size, const char *screen)
{
    qr_engine_stats_t stats;

    qr_decoder_engine_stats(&stats);
    snprintf(line, line_size,
             "HKQRINFO screen=%s ready=%u frame=%u rate=%u decodes=%u codes=%d status=%s ok=%u payload=\"%s\" luma=%u/%u/%u\r\n",
             screen,
             stats.decoder_ready,
             camera_service_have_frame(),
             settings_qr_decode_rate(),
             (unsigned)stats.decode_count,
             stats.code_count,
             qr_result_status(),
             stats.last_decode_ok,
             qr_result_has_payload() ? qr_result_payload_text() : "",
             stats.luma_min,
             stats.luma_avg,
             stats.luma_max);
}

uint8_t qr_service_decode_rate(void)
{
    return settings_qr_decode_rate();
}

void qr_service_set_decode_rate(uint8_t rate)
{
    settings_set_qr_decode_rate(rate);
    g_qr_last_decode_us = 0;
}

uint8_t qr_service_cycle_decode_rate(void)
{
    uint8_t rate = settings_qr_decode_rate();
    rate = rate >= QR_DECODE_RATE_MAX ? QR_DECODE_RATE_MIN : (uint8_t)(rate + 1U);
    settings_set_qr_decode_rate(rate);
    g_qr_last_decode_us = 0;
    settings_mark_dirty(1);
    return rate;
}

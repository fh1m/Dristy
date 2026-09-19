#include "qr_decoder_engine.h"

#include <stdio.h>

#include "qr_config.h"

#include "qr_luma.h"
#include "qr_result.h"

#include "quirc.h"

static struct quirc *g_qr_decoder;
static uint8_t g_qr_decoder_ready;
static uint16_t g_qr_decoder_w;
static uint16_t g_qr_decoder_h;
static uint32_t g_qr_decode_count;
static int g_qr_code_count;
static uint8_t g_qr_luma_min;
static uint8_t g_qr_luma_max;
static uint8_t g_qr_luma_avg;
static uint8_t g_qr_last_decode_ok;

void qr_decoder_engine_reset_session(void)
{
    g_qr_decode_count = 0;
    g_qr_code_count = 0;
    g_qr_last_decode_ok = 0;
    g_qr_luma_min = 0;
    g_qr_luma_avg = 0;
    g_qr_luma_max = 0;
}

void qr_decoder_engine_stats(qr_engine_stats_t *stats)
{
    if(!stats)
        return;

    stats->decoder_ready = g_qr_decoder_ready;
    stats->decode_count = g_qr_decode_count;
    stats->code_count = g_qr_code_count;
    stats->last_decode_ok = g_qr_last_decode_ok;
    stats->luma_min = g_qr_luma_min;
    stats->luma_avg = g_qr_luma_avg;
    stats->luma_max = g_qr_luma_max;
}

static uint8_t qr_decoder_init(uint16_t target_w, uint16_t target_h)
{
    if(g_qr_decoder_ready && g_qr_decoder_w == target_w && g_qr_decoder_h == target_h)
        return 1;

    if(!g_qr_decoder)
        g_qr_decoder = quirc_new();
    if(!g_qr_decoder)
    {
        qr_result_set_status("NO HEAP");
        printf("[QR] decoder alloc failed\r\n");
        return 0;
    }

    if(quirc_resize(g_qr_decoder, target_w, target_h) < 0)
    {
        g_qr_decoder_ready = 0;
        g_qr_decoder_w = 0;
        g_qr_decoder_h = 0;
        qr_result_set_status("NO BUFFER");
        printf("[QR] decoder resize failed %ux%u\r\n", target_w, target_h);
        return 0;
    }

    g_qr_decoder_ready = 1;
    g_qr_decoder_w = target_w;
    g_qr_decoder_h = target_h;
    printf("[QR] quirc ready %ux%u version=%s\r\n", target_w, target_h, quirc_version());
    return 1;
}

static uint8_t qr_decode_codes(int count, uint8_t force, const char *mode)
{
    for(int i = 0; i < count; i++)
    {
        struct quirc_code code;
        struct quirc_data data;
        quirc_decode_error_t err;

        quirc_extract(g_qr_decoder, i, &code);
        err = quirc_decode(&code, &data);
        if(err != QUIRC_SUCCESS)
        {
            quirc_flip(&code);
            err = quirc_decode(&code, &data);
        }

        if(err == QUIRC_SUCCESS)
        {
            qr_result_set_payload(data.payload, (uint16_t)data.payload_len);
            g_qr_last_decode_ok = 1;
            qr_result_set_status(mode);
            printf("[QR] payload=%s mode=%s\r\n", qr_result_payload_text(), mode);
            return 1;
        }

        if(force)
            printf("[QR] decode err=%s mode=%s\r\n", quirc_strerror(err), mode);
    }

    return 0;
}

static int qr_decode_image_pass(const qr_engine_frame_t *frame,
                                uint16_t target_w,
                                uint16_t target_h,
                                uint8_t mode,
                                const char *label,
                                uint8_t force,
                                uint8_t update_stats)
{
    uint8_t *image;
    int width;
    int height;
    int count;
    qr_luma_stats_t luma = {
        .min = g_qr_luma_min,
        .avg = g_qr_luma_avg,
        .max = g_qr_luma_max,
    };

    image = quirc_begin(g_qr_decoder, &width, &height);
    if(!image || width != target_w || height != target_h)
    {
        qr_result_set_status("BAD BUFFER");
        printf("[QR] begin failed w=%d h=%d\r\n", width, height);
        return -1;
    }

    qr_luma_fill_image(frame->pixels,
                       image,
                       target_w,
                       target_h,
                       frame->width,
                       0,
                       0,
                       frame->width,
                       frame->height,
                       mode,
                       update_stats,
                       &luma);
    if(update_stats)
    {
        g_qr_luma_min = luma.min;
        g_qr_luma_avg = luma.avg;
        g_qr_luma_max = luma.max;
    }
    quirc_end(g_qr_decoder);
    count = quirc_count(g_qr_decoder);
    if(count > g_qr_code_count)
        g_qr_code_count = count;
    if(force)
        printf("[QR] pass=%s codes=%d luma=%u/%u/%u\r\n", label, count, g_qr_luma_min, g_qr_luma_avg, g_qr_luma_max);
    if(qr_decode_codes(count, force, label))
        return 1;
    return 0;
}

qr_engine_result_t qr_decoder_engine_decode(const qr_engine_frame_t *frame, uint8_t force)
{
    uint16_t target_w;
    uint16_t target_h;
    int pass_result;

    if(!frame || !frame->pixels || frame->width == 0 || frame->height == 0)
    {
        qr_result_set_status("NO FRAME");
        return QR_ENGINE_NOT_FOUND;
    }

    target_w = force ? frame->width : QR_AUTOSCAN_W;
    target_h = force ? frame->height : QR_AUTOSCAN_H;

    if(!qr_decoder_init(target_w, target_h))
        return QR_ENGINE_DECODER_FAIL;

    g_qr_code_count = 0;
    g_qr_last_decode_ok = 0;
    g_qr_decode_count++;

    pass_result = qr_decode_image_pass(frame, target_w, target_h, 0, "raw", force, 1);
    if(pass_result < 0)
        return QR_ENGINE_DECODER_FAIL;
    if(pass_result > 0)
        return QR_ENGINE_FOUND;

    if(!force)
    {
        qr_result_clear_payload();
        qr_result_set_status(g_qr_code_count == 0 ? "NO QR" : "NO DATA");
        return QR_ENGINE_NOT_FOUND;
    }

    pass_result = qr_decode_image_pass(frame, target_w, target_h, QR_PASS_INVERT, "invert", force, 0);
    if(pass_result > 0)
        return QR_ENGINE_FOUND;

    pass_result = qr_decode_image_pass(frame, target_w, target_h, QR_PASS_STRETCH, "stretch", force, 0);
    if(pass_result > 0)
        return QR_ENGINE_FOUND;

    pass_result = qr_decode_image_pass(frame, target_w, target_h, QR_PASS_STRETCH | QR_PASS_INVERT, "stretch-invert", force, 0);
    if(pass_result > 0)
        return QR_ENGINE_FOUND;

    qr_result_clear_payload();
    qr_result_set_status(g_qr_code_count == 0 ? "NO QR" : "NO DATA");
    return QR_ENGINE_NOT_FOUND;
}

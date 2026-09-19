#include "dristy_boot.h"

#include "hk_config.h"

#if HK_ENABLE_DRISTY

#include <stdio.h>

#include "dristy_bench.h"
#include "dristy_config.h"
#include "dristy_lcd.h"
#include "dristy_led.h"
#include "dristy_modes.h"
#include "dristy_pipeline.h"
#include "dristy_protocol.h"
#include "dristy_uart_bridge.h"

#include "../services/external_link_service.h"
#include "dristy_host_mode.h"

static dristy_protocol_ctx_t g_proto_ctx;
static uint32_t g_fps_x10;
static uint32_t g_infer_us;
static uint32_t g_decode_us;
static uint32_t g_kpu_mhz;
static uint32_t g_frame_count;

static void proto_set_mode(dristy_mode_t mode)
{
    (void)dristy_pipeline_set_mode(mode);
    g_proto_ctx.current_mode = mode;
    dristy_host_mode_request(mode);
}

static void proto_set_threshold(uint16_t conf_x100)
{
    dristy_pipeline_set_confidence((uint8_t)(conf_x100 / 100U));
}

static void proto_set_nms(uint16_t nms_x100)
{
    dristy_pipeline_set_nms((uint8_t)(nms_x100 / 100U));
}

void dristy_boot_init(void)
{
    dristy_pipeline_config_t cfg = DRISTY_PIPELINE_CONFIG_DEFAULT;

    (void)dristy_config_init();
    (void)dristy_pipeline_init(&cfg);
    (void)dristy_pipeline_start();
    (void)dristy_config_apply();

    dristy_uart_bridge_init();
    dristy_uart_bridge_set_uart_baud_fn(external_link_service_set_uart_baud);
    dristy_led_init();
    dristy_lcd_init();
    dristy_bench_init();

    g_fps_x10 = 0U;
    g_infer_us = 0U;
    g_decode_us = 0U;
    g_kpu_mhz = 400U;
    g_frame_count = 0U;

    g_proto_ctx.tracker = dristy_pipeline_tracker();
    g_proto_ctx.current_mode = cfg.initial_mode;
    g_proto_ctx.fps_x10 = &g_fps_x10;
    g_proto_ctx.infer_us = &g_infer_us;
    g_proto_ctx.decode_us = &g_decode_us;
    g_proto_ctx.kpu_mhz = &g_kpu_mhz;
    g_proto_ctx.frame_count = &g_frame_count;
    g_proto_ctx.set_mode = proto_set_mode;
    g_proto_ctx.set_threshold = proto_set_threshold;
    g_proto_ctx.set_nms = proto_set_nms;
    g_proto_ctx.lcd_state = dristy_lcd_state();
    g_proto_ctx.set_lcd = dristy_lcd_set_state;
    g_proto_ctx.set_lcd_brightness = dristy_lcd_set_brightness;

    dristy_protocol_set_pipeline(&g_proto_ctx);
    printf("[DRISTY] runtime ready %s\r\n", DRISTY_VERSION);
}

void dristy_boot_sync_from_pipeline(void)
{
    const dristy_pipeline_state_t *st = dristy_pipeline_state();

    if(!st)
        return;
    g_fps_x10 = st->fps_x10;
    g_frame_count = st->frame_count;
    g_infer_us = st->last_inference_us;
    g_proto_ctx.current_mode = st->mode;
}

#endif /* HK_ENABLE_DRISTY */

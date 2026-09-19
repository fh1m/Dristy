#include "dristy_protocol.h"

#include "hk_config.h"

#include <stdio.h>
#include <string.h>

#include "dristy_bench.h"
#include "dristy_result_bus.h"
#include "dristy_control_output.h"
#include "dristy_pipeline.h"
#include "dristy_camera_ctrl.h"
#include "dristy_led.h"
#include "dristy_config.h"
#include "dristy_aruco.h"
#include "dristy_motion.h"
#include "dristy_colour.h"

/* ---------------------------------------------------------------------------
 * Dristy Link Protocol implementation
 *
 * This is the command dispatcher. Each DLP command reads from the global
 * pipeline context (tracker, flow, tags, perf counters) and serialises
 * results into the response buffer.
 * ------------------------------------------------------------------------- */

static dristy_protocol_ctx_t g_ctx;

void dristy_protocol_set_pipeline(void *pipeline_ctx)
{
    if(pipeline_ctx)
        g_ctx = *(dristy_protocol_ctx_t *)pipeline_ctx;
}

/* Serialise little-endian values into response buffer */
static void put_u16(uint8_t *buf, uint16_t v) { buf[0] = v & 0xFF; buf[1] = (v >> 8) & 0xFF; }
static void put_i16(uint8_t *buf, int16_t v) { put_u16(buf, (uint16_t)v); }

static uint8_t handle_identify(uint8_t *buf, uint8_t *len)
{
    uint8_t n = 0;
    const char prefix[] = "DRISTY";
    const char *ver = DRISTY_VERSION;

    memcpy(buf, prefix, sizeof(prefix) - 1U);
    n = (uint8_t)(sizeof(prefix) - 1U);
    buf[n++] = 0;
    while(*ver && n < DLP_MAX_PAYLOAD)
        buf[n++] = (uint8_t)*ver++;
    *len = n;
    return DLP_RET_IDENTITY;
}

static uint8_t handle_get_mode(uint8_t *buf, uint8_t *len)
{
    buf[0] = (uint8_t)g_ctx.current_mode;
    const dristy_mode_info_t *info = dristy_mode_info(g_ctx.current_mode);
    if(info && info->short_name)
    {
        uint8_t slen = 0;
        while(info->short_name[slen] && slen < 8) slen++;
        memcpy(buf + 1, info->short_name, slen);
        *len = 1 + slen;
    }
    else
    {
        *len = 1;
    }
    return DLP_RET_MODE;
}

static uint8_t handle_get_tracks(uint8_t *buf, uint8_t *len)
{
    dristy_track_report_t reports[DRISTY_TRACKER_MAX_TRACKS];
    uint8_t count;
    uint8_t offset = 0;

    if(!g_ctx.tracker)
    {
        *len = 0;
        return DLP_RET_TRACK;
    }

    count = dristy_tracker_report(g_ctx.tracker, reports,
                                  DRISTY_TRACKER_MAX_TRACKS);

    /* Pack reports: 16 bytes each, fit max 15 in 240 bytes */
    for(uint8_t i = 0; i < count && offset + 16 <= DLP_MAX_PAYLOAD; i++)
    {
        const dristy_track_report_t *r = &reports[i];
        put_u16(buf + offset,       r->id);
        put_i16(buf + offset + 2,   r->cx);
        put_i16(buf + offset + 4,   r->cy);
        put_i16(buf + offset + 6,   r->w);
        put_i16(buf + offset + 8,   r->h);
        put_i16(buf + offset + 10,  r->vx_x10);
        put_i16(buf + offset + 12,  r->vy_x10);
        put_u16(buf + offset + 14,  (uint16_t)(r->age > 65535 ? 65535 : r->age));
        offset += 16;
    }
    *len = offset;
    return DLP_RET_TRACK;
}

static uint8_t handle_get_perf(uint8_t *buf, uint8_t *len)
{
    dlp_perf_report_t perf;
    memset(&perf, 0, sizeof(perf));

    if(g_ctx.fps_x10)    perf.fps_x10    = (uint16_t)*g_ctx.fps_x10;
    if(g_ctx.infer_us)   perf.infer_us   = *g_ctx.infer_us > 65535 ?
                                            65535 : (uint16_t)*g_ctx.infer_us;
    if(g_ctx.decode_us)  perf.decode_us   = *g_ctx.decode_us > 65535 ?
                                            65535 : (uint16_t)*g_ctx.decode_us;
    if(g_ctx.kpu_mhz)   perf.kpu_mhz    = (uint16_t)*g_ctx.kpu_mhz;
    if(g_ctx.frame_count)perf.frame_count = (uint16_t)*g_ctx.frame_count;

    memcpy(buf, &perf, sizeof(perf));
    *len = sizeof(perf);
    return DLP_RET_PERF;
}

static uint8_t handle_set_mode(const uint8_t *data, uint8_t data_len)
{
    if(data_len < 1 || !g_ctx.set_mode)
        return 0;
    g_ctx.set_mode((dristy_mode_t)data[0]);
    g_ctx.current_mode = (dristy_mode_t)data[0];
    return 1;
}

static uint8_t handle_set_threshold(const uint8_t *data, uint8_t data_len)
{
    if(data_len < 2 || !g_ctx.set_threshold)
        return 0;
    uint16_t thresh = (uint16_t)data[0] | ((uint16_t)data[1] << 8);
    g_ctx.set_threshold(thresh);
    return 1;
}

int dristy_protocol_handle(uint8_t command,
                           const uint8_t *data, uint8_t data_len,
                           uint8_t *response_cmd,
                           uint8_t *response_buf, uint8_t *response_len)
{
    *response_len = 0;

    if(command < 0x40 || command > 0x7F)
        return 0; /* not a DLP command */

    switch(command)
    {
    case DLP_CMD_IDENTIFY:
        *response_cmd = handle_identify(response_buf, response_len);
        return 1;

    case DLP_CMD_GET_MODE:
        *response_cmd = handle_get_mode(response_buf, response_len);
        return 1;

    case DLP_CMD_SET_MODE:
        if(handle_set_mode(data, data_len))
        {
            *response_cmd = handle_get_mode(response_buf, response_len);
            return 1;
        }
        return -1;

    case DLP_CMD_GET_TRACKS:
        *response_cmd = handle_get_tracks(response_buf, response_len);
        return 1;

    case DLP_CMD_GET_PERF:
        *response_cmd = handle_get_perf(response_buf, response_len);
        return 1;

    case DLP_CMD_SET_THRESHOLD:
        if(handle_set_threshold(data, data_len))
        {
            *response_cmd = DLP_RET_PERF; /* ACK with perf */
            return handle_get_perf(response_buf, response_len) ? 1 : -1;
        }
        return -1;

    case DLP_CMD_GET_TAGS:
    {
        const dristy_result_snapshot_t *snap = dristy_result_bus_read();
        uint8_t offset = 0;
        if(snap)
        {
            for(uint8_t i = 0; i < snap->tag_count &&
                offset + sizeof(dlp_tag_wire_t) <= DLP_MAX_PAYLOAD; i++)
            {
                const dristy_result_tag_t *t = &snap->tags[i];
                dlp_tag_wire_t w;
                w.tag_id = t->tag_id;
                w.family = t->family;
                w.cx = t->cx;
                w.cy = t->cy;
                w.hamming = t->hamming;
                w.tx_mm = t->pose_valid ? (int16_t)t->tx_mm : 0;
                w.ty_mm = t->pose_valid ? (int16_t)t->ty_mm : 0;
                w.tz_mm = t->pose_valid ? (int16_t)t->tz_mm : 0;
                w.yaw_deg_x10 = t->pose_valid ? (int16_t)(t->yaw_deg * 10) : 0;
                memcpy(response_buf + offset, &w, sizeof(w));
                offset += sizeof(w);
            }
        }
        *response_len = offset;
        *response_cmd = DLP_RET_TAG;
        return 1;
    }

    case DLP_CMD_GET_FLOW:
    {
        const dristy_result_snapshot_t *snap = dristy_result_bus_read();
        uint8_t offset = 0;
        if(snap)
        {
            for(uint8_t i = 0; i < snap->flow_count && offset + 8 <= DLP_MAX_PAYLOAD; i++)
            {
                const dristy_result_flow_t *f = &snap->flow[i];
                put_i16(response_buf + offset, f->dx_x100);
                put_i16(response_buf + offset + 2, f->dy_x100);
                put_i16(response_buf + offset + 4, f->roi_cx);
                put_i16(response_buf + offset + 6, f->roi_cy);
                offset += 8;
            }
        }
        *response_len = offset;
        *response_cmd = DLP_RET_FLOW;
        return 1;
    }

    case DLP_CMD_GET_QR:
    {
        const dristy_result_snapshot_t *snap = dristy_result_bus_read();
        uint8_t offset = 0;
        if(snap)
        {
            for(uint8_t i = 0; i < snap->qr_count && offset + 2 < DLP_MAX_PAYLOAD; i++)
            {
                const dristy_result_qr_t *q = &snap->qr[i];
                uint8_t slen = q->data_len > 127U ? 127U : (uint8_t)q->data_len;
                if(offset + 1U + slen > DLP_MAX_PAYLOAD)
                    break;
                response_buf[offset++] = slen;
                memcpy(response_buf + offset, q->data, slen);
                offset += slen;
            }
        }
        *response_len = offset;
        *response_cmd = DLP_RET_QR;
        return 1;
    }

    case DLP_CMD_SET_COLOUR_THRESH:
    {
        if(data_len >= 6U)
        {
            dristy_colour_set_thresholds(data[0], data[1], data[2],
                                         data[3], data[4], data[5]);
        }
        *response_cmd = 0x2E;
        *response_len = 0;
        return 1;
    }

    case DLP_CMD_GET_BLOBS:
    {
        const dristy_result_snapshot_t *snap = dristy_result_bus_read();
        uint8_t offset = 0;
        if(snap)
        {
            for(uint8_t i = 0; i < snap->blob_count &&
                offset + 10 <= DLP_MAX_PAYLOAD; i++)
            {
                const dristy_result_blob_t *b = &snap->blobs[i];
                put_i16(response_buf + offset, b->cx);
                put_i16(response_buf + offset + 2, b->cy);
                put_i16(response_buf + offset + 4, b->w);
                put_i16(response_buf + offset + 6, b->h);
                response_buf[offset + 8] = b->colour_id;
                response_buf[offset + 9] = (uint8_t)(b->density * 100);
                offset += 10;
            }
        }
        *response_len = offset;
        *response_cmd = DLP_RET_BLOB;
        return 1;
    }

    /* Control-system commands — bridge to compact binary protocol */

    case DLP_CMD_CTRL_TARGET:
    {
        /* Build a compact target packet directly into response buffer */
        uint8_t n = dristy_control_output_target(response_buf, DLP_MAX_PAYLOAD);
        *response_len = n;
        *response_cmd = DLP_RET_TRACK; /* reuse return code */
        return 1;
    }

    case DLP_CMD_CTRL_FULL:
    {
        /* Note: full frame may exceed 250 bytes. We cap at DLP_MAX_PAYLOAD.
         * For full-frame transfer, use the control output port directly. */
        uint16_t n = dristy_control_output_full_frame(
            response_buf, DLP_MAX_PAYLOAD);
        *response_len = (uint8_t)(n > 250 ? 250 : n);
        *response_cmd = DLP_RET_TRACK;
        return 1;
    }

    case DLP_CMD_SET_TARGET_ID:
    {
        if(data_len >= 2)
        {
            uint16_t tid = (uint16_t)data[0] | ((uint16_t)data[1] << 8);
            dristy_pipeline_set_target_track(tid);
        }
        *response_cmd = 0x2E; /* RETURN_OK */
        *response_len = 0;
        return 1;
    }

    case DLP_CMD_SET_TARGET_CLS:
    {
        if(data_len >= 1)
            dristy_pipeline_set_target_class(data[0]);
        *response_cmd = 0x2E;
        *response_len = 0;
        return 1;
    }

    case DLP_CMD_GET_RESULT:
    {
        /* Return a summary of the latest result bus snapshot */
        const dristy_result_snapshot_t *snap = dristy_result_bus_read();
        if(snap)
        {
            uint8_t off = 0;
            /* 4 bytes: frame number */
            put_u16(response_buf + off, (uint16_t)(snap->frame_number & 0xFFFF));
            off += 2;
            /* 2 bytes: fps × 10 */
            put_u16(response_buf + off, snap->fps_x10);
            off += 2;
            /* 2 bytes: pipeline_us */
            put_u16(response_buf + off, (uint16_t)(snap->pipeline_us > 65535 ?
                                                    65535 : snap->pipeline_us));
            off += 2;
            /* 2 bytes: inference_us */
            put_u16(response_buf + off, (uint16_t)(snap->inference_us > 65535 ?
                                                    65535 : snap->inference_us));
            off += 2;
            /* 1 byte: mode */
            response_buf[off++] = (uint8_t)snap->mode;
            /* 1 byte: counts */
            response_buf[off++] = snap->detection_count;
            response_buf[off++] = snap->track_count;
            response_buf[off++] = snap->tag_count;
            response_buf[off++] = snap->blob_count;
            response_buf[off++] = snap->flow_count;
            response_buf[off++] = snap->qr_count;
            response_buf[off++] = snap->line_valid;
            {
                const dristy_motion_result_t *mr = dristy_motion_result();
                uint16_t mp = 0U;

                if(mr)
                    mp = (uint16_t)(mr->motion_percent * 10.0f);
                put_u16(response_buf + off, mp);
                off += 2;
            }
            /* 1 byte: target type */
            response_buf[off++] = (uint8_t)snap->target.type;
            /* 4 bytes: target error */
            put_i16(response_buf + off, snap->target.error_x); off += 2;
            put_i16(response_buf + off, snap->target.error_y); off += 2;
            *response_len = off;
        }
        else
        {
            *response_len = 0;
        }
        *response_cmd = DLP_RET_PERF;
        return 1;
    }

    /* Benchmark commands */
    case DLP_CMD_BENCH_ENABLE:
        if(data_len >= 1)
            dristy_bench_enable(data[0]);
        *response_cmd = 0x2E; /* OK */
        *response_len = 0;
        return 1;

    case DLP_CMD_BENCH_REPORT:
    {
        uint16_t n = dristy_bench_serialize(response_buf, DLP_MAX_PAYLOAD);
        *response_len = (uint8_t)(n > 250 ? 250 : n);
        *response_cmd = DLP_RET_PERF;
        return 1;
    }

    case DLP_CMD_BENCH_RESET:
        dristy_bench_reset();
        *response_cmd = 0x2E;
        *response_len = 0;
        return 1;

    case DLP_CMD_BENCH_PRINT:
        dristy_bench_print_report();
        *response_cmd = 0x2E;
        *response_len = 0;
        return 1;

    /* ── LCD control ──────────────────────────────────────────────── */

    case DLP_CMD_LCD_CONTROL:
    {
        if(data_len >= 1)
        {
            g_ctx.lcd_state = data[0];
            if(g_ctx.set_lcd)
                g_ctx.set_lcd(data[0]);
        }
        response_buf[0] = g_ctx.lcd_state;
        *response_len = 1;
        *response_cmd = DLP_RET_LCD_STATE;
        return 1;
    }

    case DLP_CMD_LCD_BRIGHTNESS:
    {
        if(data_len >= 1 && g_ctx.set_lcd_brightness)
            g_ctx.set_lcd_brightness(data[0]);
        *response_cmd = 0x2E;
        *response_len = 0;
        return 1;
    }

    /* ── Camera ISP control ──────────────────────────────────────── */

    case DLP_CMD_CAM_PRESET:
    {
        if(data_len >= 1)
            dristy_camera_ctrl_set_preset((dristy_cam_preset_t)data[0]);
        *response_cmd = 0x2E;
        *response_len = 0;
        return 1;
    }

    case DLP_CMD_CAM_SET_PARAM:
    {
        /* param_id(1) + value(2) = 3 bytes */
        if(data_len >= 3)
        {
            uint8_t param_id = data[0];
            int16_t value = (int16_t)((uint16_t)data[1] | ((uint16_t)data[2] << 8));
            switch(param_id)
            {
            case 0: dristy_camera_ctrl_set_brightness((int8_t)value); break;
            case 1: dristy_camera_ctrl_set_contrast((int8_t)value); break;
            case 2: dristy_camera_ctrl_set_saturation((int8_t)value); break;
            case 3: dristy_camera_ctrl_set_sharpness((int8_t)value); break;
            case 4: dristy_camera_ctrl_set_exposure((uint8_t)(value >> 8),
                                                     (uint16_t)(value & 0xFF)); break;
            case 5: dristy_camera_ctrl_set_gain((uint8_t)(value >> 8),
                                                 (uint8_t)(value & 0xFF), 2); break;
            case 6: dristy_camera_ctrl_set_awb((uint8_t)(value >> 8),
                                                (uint8_t)(value & 0xFF)); break;
            case 7: dristy_camera_ctrl_set_mirror_flip(
                        (uint8_t)(value & 0x01), (uint8_t)((value >> 1) & 0x01)); break;
            }
            dristy_camera_ctrl_apply();
        }
        *response_cmd = 0x2E;
        *response_len = 0;
        return 1;
    }

    case DLP_CMD_CAM_GET_PARAMS:
    {
        const dristy_camera_params_t *p = dristy_camera_ctrl_params();
        if(p)
        {
            uint8_t off = 0;
            response_buf[off++] = (uint8_t)p->preset;
            response_buf[off++] = p->aec_enable;
            response_buf[off++] = p->agc_enable;
            put_u16(response_buf + off, p->manual_exposure); off += 2;
            response_buf[off++] = p->manual_gain;
            response_buf[off++] = p->awb_enable;
            response_buf[off++] = p->wb_mode;
            response_buf[off++] = (uint8_t)(p->brightness + 2); /* map -2..+2 → 0..4 */
            response_buf[off++] = (uint8_t)(p->contrast + 2);
            response_buf[off++] = (uint8_t)(p->saturation + 2);
            response_buf[off++] = (uint8_t)p->sharpness;
            response_buf[off++] = p->hmirror;
            response_buf[off++] = p->vflip;
            *response_len = off;
        }
        *response_cmd = DLP_RET_CAM_PARAMS;
        return 1;
    }

    /* ── LED control ─────────────────────────────────────────────── */

    case DLP_CMD_LED_MODE:
        if(data_len >= 1)
            dristy_led_set_mode((dristy_led_mode_t)data[0]);
        *response_cmd = 0x2E;
        *response_len = 0;
        return 1;

    case DLP_CMD_LED_BRIGHTNESS:
        if(data_len >= 1)
            dristy_led_set_brightness(data[0]);
        *response_cmd = 0x2E;
        *response_len = 0;
        return 1;

    case DLP_CMD_LED_COLOR:
        if(data_len >= 3)
        {
            dristy_led_color_t c = { data[0], data[1], data[2] };
            dristy_led_set_color(c);
        }
        *response_cmd = 0x2E;
        *response_len = 0;
        return 1;

    case DLP_CMD_LED_ILLUMINATION:
        if(data_len >= 1)
            dristy_led_set_illumination(data[0]);
        *response_cmd = 0x2E;
        *response_len = 0;
        return 1;

    /* ── Config persistence ──────────────────────────────────────── */

    case DLP_CMD_CONFIG_SAVE:
    {
        uint8_t ok = dristy_config_save();
        response_buf[0] = ok;
        *response_len = 1;
        *response_cmd = 0x2E;
        return 1;
    }

    case DLP_CMD_CONFIG_LOAD:
        dristy_config_init(); /* re-reads from flash */
        dristy_config_apply();
        *response_cmd = 0x2E;
        *response_len = 0;
        return 1;

    case DLP_CMD_CONFIG_RESET:
        dristy_config_reset();
        dristy_config_apply();
        *response_cmd = 0x2E;
        *response_len = 0;
        return 1;

    /* ── ArUco config ────────────────────────────────────────────── */

    case DLP_CMD_ARUCO_SET_DICT:
        if(data_len >= 1)
            dristy_aruco_set_dict((dristy_aruco_dict_t)data[0]);
        *response_cmd = 0x2E;
        *response_len = 0;
        return 1;

    case DLP_CMD_ARUCO_SET_SIZE:
    {
        if(data_len >= 2)
        {
            uint16_t size_x10 = (uint16_t)data[0] | ((uint16_t)data[1] << 8);
            dristy_aruco_set_marker_size(size_x10 / 10.0f);
        }
        *response_cmd = 0x2E;
        *response_len = 0;
        return 1;
    }

    /* ── Motion config ───────────────────────────────────────────── */

    case DLP_CMD_MOTION_SENSITIVITY:
        if(data_len >= 1)
            dristy_motion_set_sensitivity(data[0]);
        *response_cmd = 0x2E;
        *response_len = 0;
        return 1;

    default:
        return 0; /* unrecognised DLP command */
    }
}

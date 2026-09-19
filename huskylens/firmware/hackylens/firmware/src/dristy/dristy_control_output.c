#include "dristy_control_output.h"

#include <string.h>

#include "dristy_pipeline.h"
#include "hal_time.h"

/* ---------------------------------------------------------------------------
 * CRC-8/MAXIM (Dallas/Maxim 1-Wire CRC)
 * Polynomial: 0x31 (x⁸ + x⁵ + x⁴ + 1)
 * Init: 0x00
 *
 * Chosen because it's the same CRC used by many sensor ICs (BME280, SHT3x)
 * so most embedded platforms already have a fast implementation. It catches
 * all single-bit errors and most multi-bit errors in short packets.
 * ------------------------------------------------------------------------- */

uint8_t dristy_crc8(const uint8_t *data, uint16_t len)
{
    uint8_t crc = 0x00;
    for(uint16_t i = 0; i < len; i++)
    {
        crc ^= data[i];
        for(uint8_t bit = 0; bit < 8; bit++)
        {
            if(crc & 0x80)
                crc = (uint8_t)((crc << 1) ^ 0x31);
            else
                crc <<= 1;
        }
    }
    return crc;
}

/* Build packet header: [SYNC] [TYPE] [LEN_LO] [LEN_HI] */
static uint8_t build_header(uint8_t *buf, uint8_t type, uint16_t payload_len)
{
    buf[0] = DRISTY_CTRL_SYNC;
    buf[1] = type;
    buf[2] = (uint8_t)(payload_len & 0xFF);
    buf[3] = (uint8_t)((payload_len >> 8) & 0xFF);
    return 4;
}

/* Append CRC over buf[1..end-1] (type + len + payload) and return total size */
static uint16_t finalize_packet(uint8_t *buf, uint16_t total_before_crc)
{
    /* CRC covers everything after SYNC byte */
    buf[total_before_crc] = dristy_crc8(buf + 1, total_before_crc - 1);
    return total_before_crc + 1;
}

void dristy_control_output_init(void)
{
    /* Nothing to init yet — stateless output generator */
}

uint8_t dristy_control_output_heartbeat(uint8_t *buf, uint8_t buf_size)
{
    dristy_ctrl_heartbeat_t hb;
    const dristy_pipeline_state_t *state = dristy_pipeline_state();
    const dristy_result_snapshot_t *snap = dristy_result_bus_read();
    uint8_t offset;

    if(buf_size < 4 + sizeof(hb) + 1)
        return 0;

    hb.timestamp_ms = (uint32_t)(hal_time_us() / 1000ULL);
    hb.mode = (uint8_t)(state ? state->mode : 0xFF);
    hb.fps = state ? (uint8_t)(state->fps_x10 / 10) : 0;
    hb.target_valid = (snap && snap->target.type != DRISTY_TARGET_NONE) ? 1 : 0;
    hb.status = 0; /* OK */
    if(state && !state->running)
        hb.status = 3;

    offset = build_header(buf, DRISTY_CTRL_HEARTBEAT, sizeof(hb));
    memcpy(buf + offset, &hb, sizeof(hb));
    return (uint8_t)finalize_packet(buf, offset + sizeof(hb));
}

uint8_t dristy_control_output_target(uint8_t *buf, uint8_t buf_size)
{
    dristy_ctrl_target_t tgt;
    const dristy_result_snapshot_t *snap = dristy_result_bus_read();
    uint8_t offset;

    if(!snap || buf_size < 4 + sizeof(tgt) + 1)
        return 0;

    memset(&tgt, 0, sizeof(tgt));
    tgt.timestamp_ms = (uint32_t)(snap->timestamp_us / 1000ULL);
    tgt.target_type = (uint8_t)snap->target.type;

    if(snap->target.type != DRISTY_TARGET_NONE)
    {
        tgt.error_x = snap->target.error_x;
        tgt.error_y = snap->target.error_y;
        tgt.size = snap->target.size;
        tgt.error_rate_x = snap->target.error_rate_x;
        tgt.error_rate_y = snap->target.error_rate_y;
        tgt.range_mm = snap->target.has_3d ?
            (uint16_t)(snap->target.range_mm > 65535 ? 65535 : snap->target.range_mm) : 0;
        tgt.track_id = snap->target.track_id;
        tgt.confidence = (uint8_t)(snap->target.confidence / 10);
        if(tgt.confidence > 100) tgt.confidence = 100;
    }

    offset = build_header(buf, DRISTY_CTRL_TARGET, sizeof(tgt));
    memcpy(buf + offset, &tgt, sizeof(tgt));
    return (uint8_t)finalize_packet(buf, offset + sizeof(tgt));
}

uint16_t dristy_control_output_full_frame(uint8_t *buf, uint16_t buf_size)
{
    const dristy_result_snapshot_t *snap = dristy_result_bus_read();
    uint16_t offset;
    uint16_t payload_start;

    if(!snap || buf_size < 64)
        return 0;

    offset = build_header(buf, DRISTY_CTRL_FULL_FRAME, 0); /* len filled later */
    payload_start = offset;

    /* 1. Primary target (always first — 20 bytes) */
    {
        dristy_ctrl_target_t tgt;
        memset(&tgt, 0, sizeof(tgt));
        tgt.timestamp_ms = (uint32_t)(snap->timestamp_us / 1000ULL);
        tgt.target_type = (uint8_t)snap->target.type;
        if(snap->target.type != DRISTY_TARGET_NONE)
        {
            tgt.error_x = snap->target.error_x;
            tgt.error_y = snap->target.error_y;
            tgt.size = snap->target.size;
            tgt.error_rate_x = snap->target.error_rate_x;
            tgt.error_rate_y = snap->target.error_rate_y;
            tgt.range_mm = snap->target.has_3d ?
                (uint16_t)(snap->target.range_mm > 65535 ? 65535 :
                           snap->target.range_mm) : 0;
            tgt.track_id = snap->target.track_id;
            tgt.confidence = (uint8_t)(snap->target.confidence / 10);
        }
        if(offset + sizeof(tgt) > buf_size - 1)
            goto done;
        memcpy(buf + offset, &tgt, sizeof(tgt));
        offset += sizeof(tgt);
    }

    /* 2. Track count + tracks */
    {
        uint8_t count = snap->track_count;
        if(count > 20) count = 20;
        if(offset + 1 + count * sizeof(dristy_ctrl_track_t) > buf_size - 1)
            count = (uint8_t)((buf_size - 1 - offset - 1) / sizeof(dristy_ctrl_track_t));
        buf[offset++] = count;

        for(uint8_t i = 0; i < count; i++)
        {
            dristy_ctrl_track_t ct;
            const dristy_result_track_t *t = &snap->tracks[i];
            ct.id = t->id;
            ct.norm_cx = t->norm_cx;
            ct.norm_cy = t->norm_cy;
            ct.vel_x = t->vel_norm_x;
            ct.vel_y = t->vel_norm_y;
            ct.cls = t->cls;
            ct.confidence = (uint8_t)(t->confidence / 10);
            memcpy(buf + offset, &ct, sizeof(ct));
            offset += sizeof(ct);
        }
    }

    /* 3. Tag count + tags */
    {
        uint8_t count = snap->tag_count;
        if(count > 8) count = 8;
        if(offset + 1 + count * sizeof(dristy_ctrl_tag_t) > buf_size - 1)
            count = (uint8_t)((buf_size - 1 - offset - 1) / sizeof(dristy_ctrl_tag_t));
        buf[offset++] = count;

        for(uint8_t i = 0; i < count; i++)
        {
            dristy_ctrl_tag_t ct;
            const dristy_result_tag_t *t = &snap->tags[i];
            ct.tag_id = t->tag_id;
            ct.norm_cx = t->norm_cx;
            ct.norm_cy = t->norm_cy;
            ct.range_mm = t->pose_valid ?
                (uint16_t)(t->range_mm > 65535 ? 65535 : t->range_mm) : 0;
            ct.yaw_x10 = (int16_t)(t->yaw_deg * 10.0f);
            ct.pitch_x10 = (int16_t)(t->pitch_deg * 10.0f);
            ct.family = t->family;
            ct.hamming = t->hamming;
            memcpy(buf + offset, &ct, sizeof(ct));
            offset += sizeof(ct);
        }
    }

    /* 4. Flow count + flow cells */
    {
        uint8_t count = snap->flow_count;
        if(count > 16) count = 16;
        buf[offset++] = count;
        /* Flow cells are 8 bytes each (dristy_result_flow_t is 10, compact to 8) */
        for(uint8_t i = 0; i < count && offset + 8 <= buf_size - 1; i++)
        {
            const dristy_result_flow_t *f = &snap->flow[i];
            memcpy(buf + offset, &f->dx_x100, 4); /* dx, dy */
            memcpy(buf + offset + 4, &f->roi_cx, 4); /* roi position */
            offset += 8;
        }
    }

done:
    /* Fix up payload length in header */
    {
        uint16_t payload_len = offset - payload_start;
        buf[2] = (uint8_t)(payload_len & 0xFF);
        buf[3] = (uint8_t)((payload_len >> 8) & 0xFF);
    }

    return finalize_packet(buf, offset);
}

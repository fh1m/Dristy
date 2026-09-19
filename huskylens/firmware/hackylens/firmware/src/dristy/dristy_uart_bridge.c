#include "dristy_uart_bridge.h"

#include <string.h>

#include "dristy_control_output.h"
#include "dristy_husky_compat.h"
#include "dristy_protocol.h"
#include "dristy_result_bus.h"

#define DLP_SYNC0 0x55U
#define DLP_SYNC1 0xAAU
#define DLP_ADDR  0x11U
#define DLP_RX_MAX 260U

typedef enum
{
    BRIDGE_IDLE = 0,
    BRIDGE_DLP_SYNC1,
    BRIDGE_DLP_BODY,
    BRIDGE_HK_PASS,
} bridge_state_t;

static bridge_state_t g_state;
static uint8_t g_dlp_rx[DLP_RX_MAX];
static uint16_t g_dlp_len;
static uint8_t g_ctrl_stream;
static void (*g_set_baud)(uint32_t baud);

static uint16_t encode_dlp_response(uint8_t resp_cmd,
                                    const uint8_t *payload, uint8_t payload_len,
                                    uint8_t *out, uint16_t out_cap)
{
    uint16_t total;
    uint8_t sum;
    uint16_t i;

    if(out_cap < (uint16_t)(6U + payload_len))
        return 0U;

    out[0] = DLP_SYNC0;
    out[1] = DLP_SYNC1;
    out[2] = DLP_ADDR;
    out[3] = payload_len;
    out[4] = resp_cmd;
    if(payload_len && payload)
        memcpy(out + 5, payload, payload_len);
    total = (uint16_t)(5U + payload_len);
    sum = 0U;
    for(i = 2U; i < total; i++)
        sum = (uint8_t)(sum + out[i]);
    out[total] = sum;
    return (uint16_t)(total + 1U);
}

static uint8_t dispatch_dlp_command(uint8_t cmd, const uint8_t *data, uint8_t data_len,
                                    uint8_t *out, uint16_t out_cap, uint16_t *out_len)
{
    uint8_t resp_cmd = 0U;
    uint8_t resp_buf[DLP_MAX_PAYLOAD];
    uint8_t resp_len = 0U;
    int rc;

    if(cmd >= 0x40U && cmd <= 0x7FU)
    {
        rc = dristy_protocol_handle(cmd, data, data_len,
                                  &resp_cmd, resp_buf, &resp_len);
        if(rc <= 0)
            return 0U;
    }
    else if(cmd >= 0x20U && cmd <= 0x3EU)
    {
        rc = dristy_husky_compat_handle(cmd, data, data_len,
                                        &resp_cmd, resp_buf, &resp_len);
        if(rc <= 0)
            return 0U;
    }
    else
        return 0U;

    *out_len = encode_dlp_response(resp_cmd, resp_buf, resp_len, out, out_cap);
    return (*out_len > 0U) ? 1U : 0U;
}

static void reset_dlp(void)
{
    g_dlp_len = 0U;
    g_state = BRIDGE_IDLE;
}

void dristy_uart_bridge_init(void)
{
    g_state = BRIDGE_IDLE;
    g_dlp_len = 0U;
    g_ctrl_stream = 0U;
    g_set_baud = 0;
}

void dristy_uart_bridge_set_uart_baud_fn(void (*fn)(uint32_t baud))
{
    g_set_baud = fn;
}

uint8_t dristy_uart_bridge_ctrl_stream_active(void)
{
    return g_ctrl_stream;
}

void dristy_uart_bridge_set_ctrl_stream(uint8_t enabled)
{
    g_ctrl_stream = enabled ? 1U : 0U;
}

uint8_t dristy_uart_bridge_feed(uint8_t byte,
                                uint8_t *out, uint16_t out_cap,
                                uint16_t *out_len,
                                uint8_t *route_hk)
{
    *out_len = 0U;
    *route_hk = 0U;

    if(g_state == BRIDGE_HK_PASS)
    {
        *route_hk = 1U;
        if(byte == DLP_SYNC0)
        {
            g_state = BRIDGE_DLP_SYNC1;
            g_dlp_len = 0U;
            g_dlp_rx[g_dlp_len++] = byte;
            *route_hk = 0U;
        }
        return 0U;
    }

    if(g_state == BRIDGE_IDLE)
    {
        if(byte == DLP_SYNC0)
        {
            g_state = BRIDGE_DLP_SYNC1;
            g_dlp_len = 0U;
            g_dlp_rx[g_dlp_len++] = byte;
            return 0U;
        }
        if(byte == (uint8_t)'H')
        {
            g_state = BRIDGE_HK_PASS;
            *route_hk = 1U;
            return 0U;
        }
        return 0U;
    }

    if(g_state == BRIDGE_DLP_SYNC1)
    {
        g_dlp_rx[g_dlp_len++] = byte;
        if(byte != DLP_SYNC1)
        {
            if(byte == DLP_SYNC0)
            {
                g_dlp_len = 1U;
                g_dlp_rx[0] = DLP_SYNC0;
            }
            else
                reset_dlp();
            return 0U;
        }
        g_state = BRIDGE_DLP_BODY;
        return 0U;
    }

    if(g_state == BRIDGE_DLP_BODY)
    {
        if(g_dlp_len >= DLP_RX_MAX)
        {
            reset_dlp();
            return 0U;
        }
        g_dlp_rx[g_dlp_len++] = byte;

        if(g_dlp_len < 5U)
            return 0U;

        {
            uint8_t data_len = g_dlp_rx[3];
            uint16_t frame_len = (uint16_t)(5U + data_len + 1U);

            if(g_dlp_len < frame_len)
                return 0U;

            {
                uint8_t cmd = g_dlp_rx[4];
                const uint8_t *payload = &g_dlp_rx[5];
                uint8_t sum = 0U;
                uint16_t i;

                for(i = 2U; i < (uint16_t)(5U + data_len); i++)
                    sum = (uint8_t)(sum + g_dlp_rx[i]);
                if(sum != g_dlp_rx[5U + data_len])
                {
                    reset_dlp();
                    return 0U;
                }

                if(cmd == DLP_CMD_CTRL_STREAM_ON)
                    g_ctrl_stream = (data_len >= 1U) ? payload[0] : 1U;
                else if(cmd == DLP_CMD_CTRL_STREAM_OFF)
                    g_ctrl_stream = 0U;
                else if(cmd == DLP_CMD_SET_BAUD && data_len >= 4U && g_set_baud)
                {
                    uint32_t baud = (uint32_t)payload[0] |
                                    ((uint32_t)payload[1] << 8) |
                                    ((uint32_t)payload[2] << 16) |
                                    ((uint32_t)payload[3] << 24);
                    g_set_baud(baud);
                }

                reset_dlp();
                return dispatch_dlp_command(cmd, payload, data_len,
                                            out, out_cap, out_len);
            }
        }
    }

    return 0U;
}

uint8_t dristy_uart_bridge_process_debug_byte(uint8_t byte,
                                              uint8_t *out, uint16_t out_cap,
                                              uint16_t *out_len)
{
    uint8_t route_hk = 0U;

    *out_len = 0U;
    if(dristy_uart_bridge_feed(byte, out, out_cap, out_len, &route_hk))
        return 1U;
    if(g_state != BRIDGE_IDLE && g_state != BRIDGE_HK_PASS)
        return 1U;
    return 0U;
}

void dristy_uart_bridge_tick(void)
{
    /* Unsolicited control output when stream enabled (best-effort). */
    static uint32_t last_frame;
    uint32_t fn;

    if(!g_ctrl_stream)
        return;

    fn = dristy_result_bus_frame_number();
    if(fn == 0U || fn == last_frame)
        return;
    last_frame = fn;
    /* TX queue handled by external_link when we add push hook — MVP: host polls DLP */
    (void)dristy_control_output_target;
}

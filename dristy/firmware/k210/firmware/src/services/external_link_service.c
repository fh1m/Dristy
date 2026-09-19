#include "external_link_service.h"

#include <stdio.h>
#include <string.h>

#include <dristy/capability/external_link.h>
#include <dristy/capability/time.h>

#include "pins.h"
#include "../core/hk_capability_client.h"
#include "../core/hk_string.h"
#include "debug_console_service.h"
#include "external_link_protocol.h"
#include "settings_persistence.h"
#include "settings_service.h"
#include "vision_result_service.h"
#include "hk_config.h"
#if HK_ENABLE_DRISTY
#include "../dristy/dristy_uart_bridge.h"
#endif

#define LINK_RESULT_HEADER_SIZE 10U
#define LINK_ITEM_WIRE_SIZE 16U

static external_link_transport_t g_transport = EXTERNAL_LINK_UART;
typedef union
{
    hk_link_stream_parser_t parser;
    uint8_t response[HK_LINK_MAX_FRAME];
} external_link_uart_storage_t;

static external_link_uart_storage_t g_uart_storage;
static hk_owner_t g_owner;
static hk_external_link_t g_link;
static hk_time_t g_time;
static hk_external_link_op_t g_uart_operation;
static uint16_t g_uart_response_length;
static uint64_t g_uart_response_not_before;
static uint32_t g_rx_frames;
static uint32_t g_tx_frames;
static uint32_t g_bad_frames;
static uint32_t g_rx_bytes;
static uint32_t g_uart_baud = 115200U;
static uint8_t g_suspended;

#define EXTERNAL_LINK_SERVICE_UART_RESPONSE_TIMEOUT_US UINT64_C(1000000)
#define EXTERNAL_LINK_SERVICE_UART_TURNAROUND_US UINT64_C(1000)

static void write_u16(uint8_t *data, uint16_t value)
{
    data[0] = (uint8_t)value;
    data[1] = (uint8_t)(value >> 8);
}

static void write_u32(uint8_t *data, uint32_t value)
{
    data[0] = (uint8_t)value;
    data[1] = (uint8_t)(value >> 8);
    data[2] = (uint8_t)(value >> 16);
    data[3] = (uint8_t)(value >> 24);
}

static uint16_t make_response(const hk_link_message_t *request, uint8_t *frame, size_t capacity)
{
    uint8_t payload[HK_LINK_MAX_PAYLOAD];
    uint16_t payload_length = 0U;
    uint8_t response_type;
    vision_result_snapshot_t results;

    if(request->payload_length != 0U && request->type != HK_LINK_PING)
    {
        response_type = HK_LINK_ERROR;
        payload[0] = 2U;
        payload_length = 1U;
    }
    else if(request->type == HK_LINK_GET_INFO)
    {
        response_type = HK_LINK_INFO;
        write_u16(payload, 0x000FU); /* UART, I2C, BLOCK and ARROW. */
        payload[2] = VISION_RESULT_MAX_ITEMS;
        payload[3] = (uint8_t)g_transport;
        payload[4] = EXTERNAL_LINK_I2C_ADDRESS;
        payload[5] = 0U;
        write_u32(payload + 6, g_uart_baud);
        payload_length = 10U;
    }
    else if(request->type == HK_LINK_GET_RESULTS)
    {
        response_type = HK_LINK_RESULTS;
        vision_result_snapshot(&results);
        write_u32(payload, results.frame_id);
        payload[4] = results.source;
        payload[5] = results.count;
        write_u16(payload + 6, results.width);
        write_u16(payload + 8, results.height);
        payload_length = LINK_RESULT_HEADER_SIZE;
        for(uint8_t i = 0U; i < results.count; i++)
        {
            const vision_result_item_t *item = &results.items[i];
            uint8_t *wire = payload + payload_length;
            wire[0] = item->kind;
            wire[1] = item->flags;
            write_u16(wire + 2, item->id);
            write_u16(wire + 4, item->x0);
            write_u16(wire + 6, item->y0);
            write_u16(wire + 8, item->x1);
            write_u16(wire + 10, item->y1);
            write_u16(wire + 12, item->confidence);
            write_u16(wire + 14, item->reserved);
            payload_length += LINK_ITEM_WIRE_SIZE;
        }
    }
    else if(request->type == HK_LINK_PING)
    {
        response_type = HK_LINK_PONG;
        payload_length = request->payload_length;
        if(payload_length)
            memcpy(payload, request->payload, payload_length);
    }
    else
    {
        response_type = HK_LINK_ERROR;
        payload[0] = 1U;
        payload_length = 1U;
    }

    return (uint16_t)hk_link_frame_encode(response_type, request->sequence, payload,
                                          payload_length, frame, capacity);
}

static void handle_uart_message(const hk_link_message_t *message)
{
    uint64_t now = 0U;

    g_rx_frames++;
    g_uart_response_length = make_response(
        message, g_uart_storage.response, sizeof(g_uart_storage.response));
    if(g_uart_response_length == 0U)
        return;
    if(!hk_lease_is_zero(&g_time.lease))
        (void)hk_time_now_us(g_owner, &g_time, &now);
    /* Preserve the existing half-duplex turnaround without blocking core 0. */
    g_uart_response_not_before = now +
        EXTERNAL_LINK_SERVICE_UART_TURNAROUND_US;
}

static hk_result_t service_configure(void)
{
    if(hk_lease_is_zero(&g_link.lease))
        return HK_ERR_STALE_HANDLE;
    if(g_transport == EXTERNAL_LINK_I2C)
    {
        const hk_external_link_i2c_target_config_t config = {
            sizeof(hk_external_link_i2c_target_config_t),
            HK_EXTERNAL_LINK_I2C_TARGET_CONFIG_VERSION,
            EXTERNAL_LINK_I2C_ADDRESS, 0U, 0U,
        };
        return hk_external_link_configure_i2c_target(
            g_owner, &g_link, &config);
    }
    else
    {
        const hk_external_link_uart_config_t config = {
            sizeof(hk_external_link_uart_config_t),
            HK_EXTERNAL_LINK_UART_CONFIG_VERSION,
            g_uart_baud, 0U,
        };
        return hk_external_link_configure_uart(g_owner, &g_link, &config);
    }
}

static hk_result_t service_acquire(void)
{
    hk_capability_request_t request = HK_EXTERNAL_LINK_REQUEST_0_1_INIT;
    hk_result_t result;

    if(!hk_lease_is_zero(&g_link.lease))
        return HK_OK;
    request.required_features =
        HK_EXTERNAL_LINK_FEATURE_UART |
        HK_EXTERNAL_LINK_FEATURE_I2C_TARGET;
    result = hk_external_link_acquire(
        g_owner, &request, request.required_features, &g_link);
    if(result == HK_OK)
        result = service_configure();
    if(result != HK_OK && !hk_lease_is_zero(&g_link.lease))
    {
        (void)hk_external_link_release(
            g_owner, HK_DEADLINE_IMMEDIATE, &g_link);
    }
    return result;
}

static void service_cancel_uart(void)
{
    hk_external_link_op_progress_t progress;

    if(g_uart_operation.generation != 0U &&
       !hk_lease_is_zero(&g_link.lease))
        (void)hk_external_link_cancel(
            g_owner, &g_link, &g_uart_operation, &progress);
    g_uart_operation = HK_EXTERNAL_LINK_OP_NONE;
    g_uart_response_length = 0U;
    g_uart_response_not_before = 0U;
}

void external_link_service_init(external_link_transport_t transport)
{
    hk_capability_request_t time_request = HK_TIME_REQUEST_0_1_INIT;

    memset(&g_uart_storage, 0, sizeof(g_uart_storage));
    g_link.lease = HK_LEASE_NONE;
    g_time.lease = HK_LEASE_NONE;
    g_uart_operation = HK_EXTERNAL_LINK_OP_NONE;
    g_uart_response_length = 0U;
    g_uart_response_not_before = 0U;
    g_uart_baud = settings_external_link_uart_baud();
    g_suspended = 0U;
    g_owner = capability_client_consumer_owner(
        "consumer:external-link-service");
    time_request.required_features = HK_TIME_FEATURE_MONOTONIC_US;
    if(!hk_owner_is_zero(g_owner))
        (void)hk_time_acquire(g_owner, &time_request, &g_time);
    hk_link_stream_reset(&g_uart_storage.parser);
    external_link_service_set_transport(transport);
}

void external_link_service_set_transport(external_link_transport_t transport)
{
    uint8_t already_acquired = !hk_lease_is_zero(&g_link.lease);

    g_transport = transport == EXTERNAL_LINK_I2C ?
        EXTERNAL_LINK_I2C : EXTERNAL_LINK_UART;
    service_cancel_uart();
    hk_link_stream_reset(&g_uart_storage.parser);
    if(g_suspended)
        return;
    if(service_acquire() != HK_OK ||
       (already_acquired && service_configure() != HK_OK))
        return;
    if(g_transport == EXTERNAL_LINK_I2C)
        printf("[LINK] I2C slave 0x32 " IO_EXTERNAL_I2C_R_LABEL "/"
               IO_EXTERNAL_I2C_T_LABEL "\r\n");
    else
        printf("[LINK] UART1 %u " IO_EXTERNAL_UART_R_LABEL "/"
               IO_EXTERNAL_UART_T_LABEL "\r\n", (unsigned)g_uart_baud);
}

external_link_transport_t external_link_service_transport(void)
{
    return g_transport;
}

void external_link_service_set_uart_baud(uint32_t baud)
{
    if(baud != 9600U && baud != 115200U && baud != 1000000U)
        baud = 115200U;
    g_uart_baud = baud;
    hk_link_stream_reset(&g_uart_storage.parser);
    if(g_transport == EXTERNAL_LINK_UART && !g_suspended &&
       !hk_lease_is_zero(&g_link.lease))
        (void)service_configure();
}

uint32_t external_link_service_uart_baud(void)
{
    return g_uart_baud;
}

void external_link_service_suspend(void)
{
    if(g_suspended)
        return;
    service_cancel_uart();
    if(!hk_lease_is_zero(&g_link.lease))
        (void)hk_external_link_release(
            g_owner, HK_DEADLINE_IMMEDIATE, &g_link);
    g_suspended = 1U;
}

void external_link_service_resume(void)
{
    if(!g_suspended)
        return;
    g_suspended = 0U;
    (void)service_acquire();
}

uint8_t external_link_service_suspended(void)
{
    return g_suspended;
}

void external_link_service_tick(void)
{
    hk_result_t result;

    if(g_suspended)
        return;
    if(service_acquire() != HK_OK)
        return;
    if(g_transport == EXTERNAL_LINK_UART)
    {
        uint8_t data[32];
        hk_buffer_view_t view = {
            data, sizeof(data), 0U, HK_BUFFER_ACCESS_WRITABLE,
        };
        hk_link_message_t message;
        uint32_t count = 0U;

        if(g_uart_operation.generation != 0U)
        {
            hk_external_link_op_progress_t progress;

            result = hk_external_link_poll(
                g_owner, &g_link, &g_uart_operation, &progress);
            if(result == HK_PENDING)
                return;
            if(result == HK_OK)
                g_tx_frames++;
            else
                g_bad_frames++;
            g_uart_operation = HK_EXTERNAL_LINK_OP_NONE;
            g_uart_response_length = 0U;
        }
        if(g_uart_response_length != 0U)
        {
            uint64_t now;
            hk_deadline_t deadline;
            hk_buffer_view_t response = {
                g_uart_storage.response, g_uart_response_length, 0U,
                HK_BUFFER_ACCESS_READABLE,
            };

            if(hk_time_now_us(g_owner, &g_time, &now) != HK_OK ||
               now < g_uart_response_not_before)
                return;
            if(hk_time_deadline_after_us(
                   g_owner, &g_time,
                   EXTERNAL_LINK_SERVICE_UART_RESPONSE_TIMEOUT_US,
                   &deadline) != HK_OK)
            {
                g_bad_frames++;
                g_uart_response_length = 0U;
                return;
            }
            result = hk_external_link_uart_write_begin(
                g_owner, &g_link, &response, deadline, NULL,
                &g_uart_operation);
            if(result != HK_PENDING)
            {
                g_bad_frames++;
                g_uart_operation = HK_EXTERNAL_LINK_OP_NONE;
                g_uart_response_length = 0U;
            }
            return;
        }
        result = hk_external_link_uart_read(
            g_owner, &g_link, &view, &count);
        if(result != HK_OK)
        {
            g_bad_frames++;
            return;
        }
        g_rx_bytes += count;
        for(uint32_t i = 0U; i < count; i++)
        {
#if HK_ENABLE_DRISTY
            uint8_t route_hk = 0U;
            uint16_t dlp_out_len = 0U;
            uint8_t dlp_out[280];
            uint64_t now = 0U;

            if(hk_time_now_us(g_owner, &g_time, &now) != HK_OK)
                now = 0U;
            if(dristy_uart_bridge_feed(
                   data[i], dlp_out, (uint16_t)sizeof(dlp_out),
                   &dlp_out_len, &route_hk))
            {
                if(dlp_out_len > 0U &&
                   dlp_out_len <= sizeof(g_uart_storage.response) &&
                   g_uart_response_length == 0U)
                {
                    memcpy(g_uart_storage.response, dlp_out, dlp_out_len);
                    g_uart_response_length = dlp_out_len;
                    g_uart_response_not_before = now;
                }
            }
            if(route_hk)
#endif
            if(hk_link_stream_feed(
                   &g_uart_storage.parser, data[i], &message))
            {
                handle_uart_message(&message);
                /* One borrowed response buffer backs the asynchronous write.
                 * Leave any later frame in the hardware FIFO until that
                 * response reaches a terminal state. */
                break;
            }
        }
        return;
    }

    {
        uint8_t i2c_frame[HK_LINK_MAX_FRAME];
        hk_buffer_view_t rx = {
            i2c_frame, sizeof(i2c_frame), 0U,
            HK_BUFFER_ACCESS_WRITABLE,
        };
        hk_external_link_target_event_t event;

        result = hk_external_link_i2c_target_poll(
            g_owner, &g_link, &rx, &event);
        if(result == HK_PENDING)
            return;
        if(result != HK_OK)
        {
            g_bad_frames++;
            (void)service_configure();
            return;
        }
        if(event.type == HK_EXTERNAL_LINK_TARGET_EVENT_WRITE)
        {
            hk_link_message_t message;

            g_rx_bytes += event.received_bytes;
            if(hk_link_frame_decode(
                   i2c_frame, event.received_bytes, &message))
            {
                uint16_t length;
                hk_buffer_view_t tx;

                g_rx_frames++;
                length = make_response(
                    &message, i2c_frame, sizeof(i2c_frame));
                tx = (hk_buffer_view_t){
                    i2c_frame, length, 0U, HK_BUFFER_ACCESS_READABLE,
                };
                if(length != 0U &&
                   hk_external_link_i2c_target_preload_response(
                       g_owner, &g_link, &tx) == HK_OK)
                    g_tx_frames++;
                else
                    g_bad_frames++;
            }
            else
                g_bad_frames++;
        }
    }
}

void external_link_service_format_info(char *line, size_t line_size)
{
    if(line && line_size)
    {
        const char *pins = g_transport == EXTERNAL_LINK_I2C
            ? IO_EXTERNAL_I2C_R_LABEL "/" IO_EXTERNAL_I2C_T_LABEL
            : IO_EXTERNAL_UART_R_LABEL "/" IO_EXTERNAL_UART_T_LABEL;
        snprintf(
            line, line_size,
            "HKLINKINFO mode=%s pins=%s uart=%u i2c=0x32 bytes=%u rx=%u tx=%u bad=%u\r\n",
            g_transport == EXTERNAL_LINK_I2C ? "I2C" : "UART",
            pins,
            (unsigned)g_uart_baud,
            (unsigned)g_rx_bytes, (unsigned)g_rx_frames, (unsigned)g_tx_frames,
            (unsigned)g_bad_frames
        );
    }
}

uint8_t external_link_service_handle_debug_command(const char *command)
{
    char line[128];
    external_link_transport_t transport;
    external_link_uart_speed_t speed;

    if(str_eq_ci(command, "HKLINKINFO"))
    {
        external_link_service_format_info(line, sizeof(line));
        debug_console_write_text(line);
        return 1U;
    }
    if(str_eq_ci(command, "HKLINK9600"))
        speed = EXTERNAL_LINK_UART_SPEED_9600;
    else if(str_eq_ci(command, "HKLINK115200"))
        speed = EXTERNAL_LINK_UART_SPEED_115200;
    else if(str_eq_ci(command, "HKLINK1000000"))
        speed = EXTERNAL_LINK_UART_SPEED_1000000;
    else
        speed = EXTERNAL_LINK_UART_SPEED_COUNT;

    if(speed < EXTERNAL_LINK_UART_SPEED_COUNT)
    {
        settings_set_external_link_uart_speed(speed);
        settings_set_external_link_transport(EXTERNAL_LINK_UART);
        settings_mark_dirty(1U);
        external_link_service_set_uart_baud(settings_external_link_uart_baud());
        external_link_service_set_transport(EXTERNAL_LINK_UART);
        external_link_service_format_info(line, sizeof(line));
        debug_console_write_text(line);
        return 1U;
    }

    if(str_eq_ci(command, "HKLINKUART"))
        transport = EXTERNAL_LINK_UART;
    else if(str_eq_ci(command, "HKLINKI2C"))
        transport = EXTERNAL_LINK_I2C;
    else
        return 0U;

    settings_set_external_link_transport(transport);
    settings_mark_dirty(1U);
    external_link_service_set_transport(transport);
    external_link_service_format_info(line, sizeof(line));
    debug_console_write_text(line);
    return 1U;
}

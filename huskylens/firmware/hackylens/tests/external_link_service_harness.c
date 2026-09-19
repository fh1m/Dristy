#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <hackylens/capability/external_link.h>
#include <hackylens/capability/time.h>

#include "external_link_protocol.h"
#include "external_link_service.h"
#include "vision_result_service.h"

#define CHECK(condition)                                                   \
    do                                                                     \
    {                                                                      \
        if(!(condition))                                                   \
        {                                                                  \
            fprintf(stderr, "CHECK failed at line %d: %s\n",             \
                    __LINE__, #condition);                                 \
            return 1;                                                      \
        }                                                                  \
    } while(0)

static const hk_owner_t SERVICE_OWNER = {4U, 9U};
static uint64_t s_now_us;
static uint32_t s_link_generation;
static uint32_t s_acquires;
static uint32_t s_releases;
static uint32_t s_uart_configs;
static uint32_t s_target_configs;
static uint32_t s_cancel_calls;
static uint32_t s_poll_calls;
static uint32_t s_request_capability_id;
static uint8_t s_uart_rx[HK_LINK_MAX_FRAME];
static uint32_t s_uart_rx_size;
static uint8_t s_uart_tx[HK_LINK_MAX_FRAME];
static uint32_t s_uart_tx_size;
static const uint8_t *s_borrowed_uart_tx;
static uint32_t s_borrowed_uart_size;
static hk_external_link_op_t s_operation;
static uint8_t s_target_pending;
static hk_external_link_target_event_t s_target_event;
static uint8_t s_target_rx[HK_LINK_MAX_FRAME];
static uint8_t s_target_tx[HK_LINK_MAX_FRAME];
static uint32_t s_target_tx_size;

hk_owner_t capability_client_consumer_owner(const char *consumer_id)
{
    return consumer_id && strcmp(
        consumer_id, "consumer:external-link-service") == 0 ?
        SERVICE_OWNER : HK_OWNER_NONE;
}

hk_result_t hk_time_acquire(
    hk_owner_t owner, const hk_capability_request_t *request,
    hk_time_t *handle)
{
    if(!handle || !request || request->id != HK_CAPABILITY_ID_TIME)
        return HK_ERR_INVALID_ARGUMENT;
    handle->lease = (hk_lease_t){
        8U, 1U, owner, HK_CAPABILITY_ID_TIME,
    };
    return HK_OK;
}

hk_result_t hk_time_now_us(
    hk_owner_t owner, const hk_time_t *handle, uint64_t *now_us)
{
    (void)owner;
    if(!handle || !now_us)
        return HK_ERR_INVALID_ARGUMENT;
    *now_us = s_now_us;
    return HK_OK;
}

hk_result_t hk_time_deadline_after_us(
    hk_owner_t owner, const hk_time_t *handle, uint64_t duration_us,
    hk_deadline_t *deadline)
{
    (void)owner;
    (void)handle;
    if(!deadline || UINT64_MAX - s_now_us < duration_us)
        return HK_ERR_LIMIT;
    deadline->at_us = s_now_us + duration_us;
    return HK_OK;
}

hk_result_t hk_external_link_acquire(
    hk_owner_t owner, const hk_capability_request_t *request,
    uint64_t mode_features, hk_external_link_t *handle)
{
    if(!request || !handle || request->id != HK_CAPABILITY_ID_EXTERNAL_LINK ||
       mode_features != (HK_EXTERNAL_LINK_FEATURE_UART |
                         HK_EXTERNAL_LINK_FEATURE_I2C_TARGET))
        return HK_ERR_INVALID_ARGUMENT;
    s_request_capability_id = request->id;
    s_acquires++;
    handle->lease = (hk_lease_t){
        3U, ++s_link_generation, owner, HK_CAPABILITY_ID_EXTERNAL_LINK,
    };
    return HK_OK;
}

hk_result_t hk_external_link_release(
    hk_owner_t owner, hk_deadline_t deadline, hk_external_link_t *handle)
{
    (void)owner;
    (void)deadline;
    if(!handle)
        return HK_ERR_INVALID_ARGUMENT;
    if(hk_lease_is_zero(&handle->lease))
        return HK_OK;
    s_releases++;
    handle->lease = HK_LEASE_NONE;
    return HK_OK;
}

hk_result_t hk_external_link_configure_uart(
    hk_owner_t owner, const hk_external_link_t *handle,
    const hk_external_link_uart_config_t *config)
{
    (void)owner;
    if(!handle || !config || config->baud != 115200U)
        return HK_ERR_INVALID_ARGUMENT;
    s_uart_configs++;
    return HK_OK;
}

hk_result_t hk_external_link_configure_i2c_target(
    hk_owner_t owner, const hk_external_link_t *handle,
    const hk_external_link_i2c_target_config_t *config)
{
    (void)owner;
    if(!handle || !config || config->address != 0x32U)
        return HK_ERR_INVALID_ARGUMENT;
    s_target_configs++;
    return HK_OK;
}

hk_result_t hk_external_link_uart_read(
    hk_owner_t owner, const hk_external_link_t *handle,
    hk_buffer_view_t *rx, uint32_t *received_bytes)
{
    uint32_t size;

    (void)owner;
    (void)handle;
    if(!rx || !received_bytes)
        return HK_ERR_INVALID_ARGUMENT;
    size = s_uart_rx_size;
    if(size > rx->size_bytes)
        size = rx->size_bytes;
    if(size != 0U)
        memcpy(rx->data, s_uart_rx, size);
    if(size < s_uart_rx_size)
        memmove(s_uart_rx, s_uart_rx + size, s_uart_rx_size - size);
    s_uart_rx_size -= size;
    *received_bytes = size;
    return HK_OK;
}

hk_result_t hk_external_link_uart_write_begin(
    hk_owner_t owner, const hk_external_link_t *handle,
    const hk_buffer_view_t *tx, hk_deadline_t deadline,
    const hk_cancel_t *cancel, hk_external_link_op_t *operation)
{
    (void)owner;
    (void)handle;
    (void)cancel;
    if(!tx || !operation || deadline.at_us != s_now_us + UINT64_C(1000000))
        return HK_ERR_INVALID_ARGUMENT;
    s_borrowed_uart_tx = (const uint8_t *)tx->data;
    s_borrowed_uart_size = tx->size_bytes;
    s_operation = (hk_external_link_op_t){1U, 1U};
    *operation = s_operation;
    s_poll_calls = 0U;
    return HK_PENDING;
}

hk_result_t hk_external_link_poll(
    hk_owner_t owner, const hk_external_link_t *handle,
    const hk_external_link_op_t *operation,
    hk_external_link_op_progress_t *progress)
{
    (void)owner;
    (void)handle;
    if(!operation || !progress || operation->generation != s_operation.generation)
        return HK_ERR_STALE_HANDLE;
    *progress = (hk_external_link_op_progress_t){
        sizeof(*progress), HK_EXTERNAL_LINK_OP_PROGRESS_VERSION,
        HK_EXTERNAL_LINK_OP_UART_WRITE, 0U, 0U, 0U, HK_PENDING, 0U,
    };
    if(s_poll_calls++ == 0U)
    {
        progress->tx_completed_bytes = s_borrowed_uart_size > 4U ?
            4U : s_borrowed_uart_size;
        return HK_PENDING;
    }
    memcpy(s_uart_tx, s_borrowed_uart_tx, s_borrowed_uart_size);
    s_uart_tx_size = s_borrowed_uart_size;
    progress->tx_completed_bytes = s_borrowed_uart_size;
    progress->flags = HK_EXTERNAL_LINK_PROGRESS_TERMINAL;
    progress->terminal_result = HK_OK;
    s_borrowed_uart_tx = NULL;
    s_borrowed_uart_size = 0U;
    return HK_OK;
}

hk_result_t hk_external_link_cancel(
    hk_owner_t owner, const hk_external_link_t *handle,
    const hk_external_link_op_t *operation,
    hk_external_link_op_progress_t *progress)
{
    (void)owner;
    (void)handle;
    (void)operation;
    s_cancel_calls++;
    s_borrowed_uart_tx = NULL;
    s_borrowed_uart_size = 0U;
    if(progress)
        memset(progress, 0, sizeof(*progress));
    return HK_ERR_CANCELLED;
}

hk_result_t hk_external_link_i2c_target_poll(
    hk_owner_t owner, const hk_external_link_t *handle,
    hk_buffer_view_t *rx, hk_external_link_target_event_t *event)
{
    (void)owner;
    (void)handle;
    if(!rx || !event)
        return HK_ERR_INVALID_ARGUMENT;
    if(!s_target_pending)
        return HK_PENDING;
    *event = s_target_event;
    if(event->type == HK_EXTERNAL_LINK_TARGET_EVENT_WRITE)
        memcpy(rx->data, s_target_rx, event->received_bytes);
    s_target_pending = 0U;
    return HK_OK;
}

hk_result_t hk_external_link_i2c_target_preload_response(
    hk_owner_t owner, const hk_external_link_t *handle,
    const hk_buffer_view_t *tx)
{
    (void)owner;
    (void)handle;
    if(!tx || tx->size_bytes > sizeof(s_target_tx))
        return HK_ERR_INVALID_ARGUMENT;
    memcpy(s_target_tx, tx->data, tx->size_bytes);
    s_target_tx_size = tx->size_bytes;
    return HK_OK;
}

uint32_t settings_external_link_uart_baud(void)
{
    return 115200U;
}

void settings_set_external_link_transport(external_link_transport_t transport)
{
    (void)transport;
}

void settings_set_external_link_uart_speed(external_link_uart_speed_t speed)
{
    (void)speed;
}

void settings_mark_dirty(uint8_t immediate)
{
    (void)immediate;
}

void vision_result_snapshot(vision_result_snapshot_t *snapshot)
{
    memset(snapshot, 0, sizeof(*snapshot));
}

uint8_t str_eq_ci(const char *left, const char *right)
{
    return (uint8_t)(left && right && strcmp(left, right) == 0);
}

void debug_console_write_text(const char *text)
{
    (void)text;
}

int main(void)
{
    static const uint8_t ping_payload[] = {0x11U, 0x22U, 0x33U};
    uint8_t ping[HK_LINK_MAX_FRAME];
    uint8_t expected[HK_LINK_MAX_FRAME];
    size_t ping_size;
    size_t expected_size;

    ping_size = hk_link_frame_encode(
        HK_LINK_PING, 0x1234U, ping_payload, sizeof(ping_payload),
        ping, sizeof(ping));
    expected_size = hk_link_frame_encode(
        HK_LINK_PONG, 0x1234U, ping_payload, sizeof(ping_payload),
        expected, sizeof(expected));
    CHECK(ping_size != 0U && expected_size != 0U);

    external_link_service_init(EXTERNAL_LINK_UART);
    CHECK(s_request_capability_id == HK_CAPABILITY_ID_EXTERNAL_LINK);
    CHECK(s_acquires == 1U && s_uart_configs == 1U);
    memcpy(s_uart_rx, ping, ping_size);
    s_uart_rx_size = (uint32_t)ping_size;
    external_link_service_tick();
    CHECK(s_uart_tx_size == 0U);
    s_now_us = 999U;
    external_link_service_tick();
    CHECK(s_operation.generation == 0U);
    s_now_us = 1000U;
    external_link_service_tick();
    CHECK(s_operation.generation == 1U && s_borrowed_uart_tx != NULL);
    external_link_service_tick();
    CHECK(s_uart_tx_size == 0U);
    external_link_service_tick();
    CHECK(s_uart_tx_size == expected_size);
    CHECK(memcmp(s_uart_tx, expected, expected_size) == 0);

    external_link_service_set_transport(EXTERNAL_LINK_I2C);
    CHECK(s_target_configs == 1U);
    memcpy(s_target_rx, ping, ping_size);
    s_target_event = (hk_external_link_target_event_t){
        sizeof(s_target_event), HK_EXTERNAL_LINK_TARGET_EVENT_VERSION,
        HK_EXTERNAL_LINK_TARGET_EVENT_WRITE, (uint32_t)ping_size, 0U, 0U,
    };
    s_target_pending = 1U;
    external_link_service_tick();
    CHECK(s_target_tx_size == expected_size);
    CHECK(memcmp(s_target_tx, expected, expected_size) == 0);

    external_link_service_suspend();
    external_link_service_suspend();
    CHECK(external_link_service_suspended());
    CHECK(s_releases == 1U);
    external_link_service_resume();
    external_link_service_resume();
    CHECK(!external_link_service_suspended());
    CHECK(s_acquires == 2U && s_target_configs == 2U);
    CHECK(s_cancel_calls == 0U);
    puts("EXTERNAL_LINK_SERVICE_OK protocol=v1 reacquire=1 uart_async=1 target=1");
    return 0;
}

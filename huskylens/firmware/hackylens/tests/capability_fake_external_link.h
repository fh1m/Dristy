#ifndef HK_CAPABILITY_FAKE_EXTERNAL_LINK_H
#define HK_CAPABILITY_FAKE_EXTERNAL_LINK_H

#include <hackylens/capability/external_link.h>

#define HK_FAKE_EXTERNAL_LINK_MAX_POLL_BYTES UINT32_C(32)
#define HK_FAKE_EXTERNAL_LINK_MAX_TRANSFER_BYTES UINT32_C(256)
#define HK_FAKE_EXTERNAL_LINK_MAX_EVENTS UINT32_C(64)

typedef enum
{
    HK_FAKE_EXTERNAL_LINK_EVENT_ACQUIRE = 1,
    HK_FAKE_EXTERNAL_LINK_EVENT_RELEASE = 2,
    HK_FAKE_EXTERNAL_LINK_EVENT_ROUTE = 3,
    HK_FAKE_EXTERNAL_LINK_EVENT_RESET = 4,
    HK_FAKE_EXTERNAL_LINK_EVENT_UART_TX = 5,
    HK_FAKE_EXTERNAL_LINK_EVENT_UART_RX = 6,
    HK_FAKE_EXTERNAL_LINK_EVENT_I2C_TX = 7,
    HK_FAKE_EXTERNAL_LINK_EVENT_I2C_RX = 8,
    HK_FAKE_EXTERNAL_LINK_EVENT_TARGET_WRITE = 9,
    HK_FAKE_EXTERNAL_LINK_EVENT_TARGET_PRELOAD = 10,
    HK_FAKE_EXTERNAL_LINK_EVENT_TARGET_READ = 11,
    HK_FAKE_EXTERNAL_LINK_EVENT_TERMINAL = 12
} hk_fake_external_link_event_type_t;

typedef struct
{
    uint32_t type;
    uint32_t mode;
    uint32_t operation_kind;
    uint32_t offset;
    uint32_t size_bytes;
    hk_result_t result;
    hk_deadline_t deadline;
} hk_fake_external_link_event_t;

typedef struct
{
    uint32_t active_leases;
    uint32_t active_operations;
    uint32_t current_mode;
    uint32_t route_changes;
    uint32_t peripheral_resets;
    uint32_t poll_calls;
    uint32_t event_count;
    uint32_t uart_tx_bytes;
    uint32_t uart_rx_bytes;
    uint32_t i2c_tx_bytes;
    uint32_t i2c_rx_bytes;
    uint32_t target_write_bytes;
    uint32_t target_preload_bytes;
    uint32_t target_read_bytes;
    uint32_t target_zero_fill_bytes;
    uint32_t target_preload_replacements;
    uint32_t borrowed_tx_bytes;
    uint32_t borrowed_rx_bytes;
    hk_deadline_t original_deadline;
    hk_result_t last_result;
} hk_fake_external_link_metrics_t;

void hk_fake_external_link_reset(uint64_t features);
void hk_fake_external_link_set_now_us(uint64_t now_us);
void hk_fake_external_link_set_uart_drain_polls(uint32_t polls);
hk_result_t hk_fake_external_link_feed_uart_rx(
    const uint8_t *bytes, uint32_t size_bytes);
hk_result_t hk_fake_external_link_set_i2c_rx(
    const uint8_t *bytes, uint32_t size_bytes);
void hk_fake_external_link_fail_next_i2c(
    hk_result_t result, uint32_t after_bytes);
hk_result_t hk_fake_external_link_push_target_event(
    uint32_t type,
    const uint8_t *bytes,
    uint32_t received_bytes,
    uint32_t requested_bytes);
const uint8_t *hk_fake_external_link_target_preload(uint32_t *size_bytes);
const uint8_t *hk_fake_external_link_target_read(uint32_t *size_bytes);
const hk_fake_external_link_metrics_t *hk_fake_external_link_metrics(void);
const hk_fake_external_link_event_t *hk_fake_external_link_event(
    uint32_t index);

#endif

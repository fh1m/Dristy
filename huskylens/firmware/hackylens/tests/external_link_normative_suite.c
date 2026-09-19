#include "external_link_normative_suite.h"

#include <stdio.h>
#include <string.h>

#define SUITE_CHECK(condition)                                             \
    do                                                                     \
    {                                                                      \
        if(!(condition))                                                   \
        {                                                                  \
            fprintf(stderr, "normative CHECK failed at line %d: %s\n",   \
                    __LINE__, #condition);                                 \
            return 1;                                                      \
        }                                                                  \
    } while(0)

static const hk_owner_t OWNER_A = {11U, 17U};
static const hk_owner_t OWNER_B = {12U, 19U};

static uint8_t cancel_probe(const void *context)
{
    return *(const uint8_t *)context;
}

int external_link_normative_suite_run(
    const external_link_normative_backend_t *backend)
{
    hk_capability_request_t request = HK_EXTERNAL_LINK_REQUEST_0_1_INIT;
    hk_external_link_t link = {0};
    hk_external_link_t second = {0};
    hk_external_link_t stale = {0};
    hk_external_link_t blocked = {0};
    hk_external_link_info_t info;
    hk_external_link_op_t operation = HK_EXTERNAL_LINK_OP_NONE;
    hk_external_link_op_progress_t progress;
    hk_external_link_target_event_t event;
    hk_external_link_uart_config_t uart = {
        sizeof(uart), HK_EXTERNAL_LINK_UART_CONFIG_VERSION, 115200U, 0U,
    };
    hk_external_link_i2c_controller_config_t controller = {
        sizeof(controller), HK_EXTERNAL_LINK_I2C_CONTROLLER_CONFIG_VERSION,
        400000U, 0U,
    };
    hk_external_link_i2c_target_config_t target = {
        sizeof(target), HK_EXTERNAL_LINK_I2C_TARGET_CONFIG_VERSION,
        0x32U, 0U, 0U,
    };
    uint8_t uart_bytes[70];
    uint8_t i2c_tx[20];
    uint8_t i2c_source[20];
    uint8_t i2c_rx[20] = {0};
    uint8_t target_write[4] = {1U, 2U, 3U, 4U};
    uint8_t target_receive[8] = {0};
    uint8_t target_response[3] = {9U, 8U, 7U};
    uint8_t target_observed[5] = {0};
    hk_buffer_view_t uart_view = {
        uart_bytes, sizeof(uart_bytes), 0U, HK_BUFFER_ACCESS_READABLE,
    };
    hk_external_link_i2c_transfer_t transfer = {
        sizeof(transfer), HK_EXTERNAL_LINK_I2C_TRANSFER_VERSION,
        0x50U, 0U,
        {i2c_tx, sizeof(i2c_tx), 0U, HK_BUFFER_ACCESS_READABLE},
        {i2c_rx, sizeof(i2c_rx), 0U, HK_BUFFER_ACCESS_WRITABLE},
        0U,
    };
    hk_buffer_view_t target_rx = {
        target_receive, sizeof(target_receive), 0U,
        HK_BUFFER_ACCESS_WRITABLE,
    };
    hk_buffer_view_t target_tx = {
        target_response, sizeof(target_response), 0U,
        HK_BUFFER_ACCESS_READABLE,
    };
    uint8_t cancelled = 0U;
    hk_cancel_t cancel = {cancel_probe, &cancelled};
    uint32_t effects;

    SUITE_CHECK(backend && backend->reset && backend->set_now_us &&
                backend->set_i2c_rx && backend->target_write &&
                backend->target_read && backend->uart_tx_bytes);
    backend->reset();
    backend->set_now_us(100U);
    for(uint32_t i = 0U; i < sizeof(uart_bytes); ++i)
        uart_bytes[i] = (uint8_t)i;
    for(uint32_t i = 0U; i < sizeof(i2c_tx); ++i)
    {
        i2c_tx[i] = (uint8_t)(0x20U + i);
        i2c_source[i] = (uint8_t)(0x80U + i);
    }

    request.required_features = HK_EXTERNAL_LINK_FEATURES_0_1;
    SUITE_CHECK(hk_external_link_acquire(
        OWNER_A, &request, HK_EXTERNAL_LINK_FEATURES_0_1, &link) == HK_OK);
    SUITE_CHECK(hk_external_link_get_info(OWNER_A, &link, &info) == HK_OK);
    SUITE_CHECK(info.features == HK_EXTERNAL_LINK_FEATURES_0_1);
    SUITE_CHECK(info.maximum_poll_bytes == 32U);
    SUITE_CHECK(info.maximum_i2c_write_bytes == 256U);
    SUITE_CHECK(hk_external_link_acquire(
        OWNER_B, &request, HK_EXTERNAL_LINK_FEATURE_UART, &second) ==
        HK_ERR_BUSY);

    uart.struct_size = (uint16_t)(sizeof(uart) - 1U);
    SUITE_CHECK(hk_external_link_configure_uart(
        OWNER_A, &link, &uart) == HK_ERR_INVALID_ARGUMENT);
    uart.struct_size = sizeof(uart);
    uart.struct_version++;
    SUITE_CHECK(hk_external_link_configure_uart(
        OWNER_A, &link, &uart) == HK_ERR_VERSION_INCOMPATIBLE);
    uart.struct_version = HK_EXTERNAL_LINK_UART_CONFIG_VERSION;

    controller.struct_size = (uint16_t)(sizeof(controller) - 1U);
    SUITE_CHECK(hk_external_link_configure_i2c_controller(
        OWNER_A, &link, &controller) == HK_ERR_INVALID_ARGUMENT);
    controller.struct_size = sizeof(controller);
    controller.struct_version++;
    SUITE_CHECK(hk_external_link_configure_i2c_controller(
        OWNER_A, &link, &controller) == HK_ERR_VERSION_INCOMPATIBLE);
    controller.struct_version =
        HK_EXTERNAL_LINK_I2C_CONTROLLER_CONFIG_VERSION;

    target.struct_size = (uint16_t)(sizeof(target) - 1U);
    SUITE_CHECK(hk_external_link_configure_i2c_target(
        OWNER_A, &link, &target) == HK_ERR_INVALID_ARGUMENT);
    target.struct_size = sizeof(target);
    target.struct_version++;
    SUITE_CHECK(hk_external_link_configure_i2c_target(
        OWNER_A, &link, &target) == HK_ERR_VERSION_INCOMPATIBLE);
    target.struct_version = HK_EXTERNAL_LINK_I2C_TARGET_CONFIG_VERSION;

    transfer.struct_size = (uint16_t)(sizeof(transfer) - 1U);
    SUITE_CHECK(hk_external_link_i2c_transfer_begin(
        OWNER_A, &link, &transfer, (hk_deadline_t){1000U}, NULL,
        &operation) == HK_ERR_INVALID_ARGUMENT);
    transfer.struct_size = sizeof(transfer);
    transfer.struct_version++;
    SUITE_CHECK(hk_external_link_i2c_transfer_begin(
        OWNER_A, &link, &transfer, (hk_deadline_t){1000U}, NULL,
        &operation) == HK_ERR_VERSION_INCOMPATIBLE);
    transfer.struct_version = HK_EXTERNAL_LINK_I2C_TRANSFER_VERSION;

    SUITE_CHECK(hk_external_link_configure_uart(
        OWNER_A, &link, &uart) == HK_OK);
    SUITE_CHECK(hk_external_link_uart_write_begin(
        OWNER_A, &link, &uart_view, (hk_deadline_t){1000U}, NULL,
        &operation) == HK_PENDING);
    SUITE_CHECK(hk_external_link_poll(
        OWNER_A, &link, &operation, &progress) == HK_PENDING);
    SUITE_CHECK(progress.tx_completed_bytes == 32U);
    SUITE_CHECK(hk_external_link_poll(
        OWNER_A, &link, &operation, &progress) == HK_PENDING);
    SUITE_CHECK(progress.tx_completed_bytes == 64U);
    SUITE_CHECK(hk_external_link_poll(
        OWNER_A, &link, &operation, &progress) == HK_OK);
    SUITE_CHECK(progress.tx_completed_bytes == sizeof(uart_bytes));
    SUITE_CHECK((progress.flags & HK_EXTERNAL_LINK_PROGRESS_TERMINAL) != 0U);

    SUITE_CHECK(hk_external_link_uart_write_begin(
        OWNER_A, &link, &uart_view, (hk_deadline_t){1000U}, &cancel,
        &operation) == HK_PENDING);
    SUITE_CHECK(hk_external_link_poll(
        OWNER_A, &link, &operation, &progress) == HK_PENDING);
    SUITE_CHECK(progress.tx_completed_bytes == 32U);
    cancelled = 1U;
    SUITE_CHECK(hk_external_link_poll(
        OWNER_A, &link, &operation, &progress) == HK_ERR_CANCELLED);
    effects = backend->uart_tx_bytes();
    SUITE_CHECK(hk_external_link_poll(
        OWNER_A, &link, &operation, &progress) == HK_ERR_CANCELLED);
    SUITE_CHECK(backend->uart_tx_bytes() == effects);
    cancelled = 0U;

    SUITE_CHECK(hk_external_link_configure_i2c_controller(
        OWNER_A, &link, &controller) == HK_OK);
    backend->set_i2c_rx(i2c_source, sizeof(i2c_source));
    SUITE_CHECK(hk_external_link_i2c_transfer_begin(
        OWNER_A, &link, &transfer, (hk_deadline_t){1000U}, NULL,
        &operation) == HK_PENDING);
    SUITE_CHECK(hk_external_link_poll(
        OWNER_A, &link, &operation, &progress) == HK_PENDING);
    SUITE_CHECK(progress.tx_completed_bytes == 20U);
    SUITE_CHECK(progress.rx_completed_bytes == 12U);
    SUITE_CHECK(hk_external_link_poll(
        OWNER_A, &link, &operation, &progress) == HK_OK);
    SUITE_CHECK(progress.rx_completed_bytes == 20U);
    SUITE_CHECK(memcmp(i2c_rx, i2c_source, sizeof(i2c_rx)) == 0);

    SUITE_CHECK(hk_external_link_configure_i2c_target(
        OWNER_A, &link, &target) == HK_OK);
    backend->target_write(target_write, sizeof(target_write));
    SUITE_CHECK(hk_external_link_i2c_target_poll(
        OWNER_A, &link, &target_rx, &event) == HK_OK);
    SUITE_CHECK(event.type == HK_EXTERNAL_LINK_TARGET_EVENT_WRITE);
    SUITE_CHECK(event.received_bytes == sizeof(target_write));
    SUITE_CHECK(memcmp(target_receive, target_write, sizeof(target_write)) == 0);
    SUITE_CHECK(hk_external_link_i2c_target_preload_response(
        OWNER_A, &link, &target_tx) == HK_OK);
    backend->target_read(target_observed, sizeof(target_observed));
    SUITE_CHECK(target_observed[0] == 9U && target_observed[1] == 8U &&
                target_observed[2] == 7U && target_observed[3] == 0U &&
                target_observed[4] == 0U);
    target_rx.data = NULL;
    target_rx.size_bytes = 0U;
    SUITE_CHECK(hk_external_link_i2c_target_poll(
        OWNER_A, &link, &target_rx, &event) == HK_OK);
    SUITE_CHECK(event.type == HK_EXTERNAL_LINK_TARGET_EVENT_READ);
    SUITE_CHECK(event.requested_bytes == sizeof(target_observed));

    stale = link;
    backend->set_now_us(2000U);
    SUITE_CHECK(hk_external_link_release(
        OWNER_A, (hk_deadline_t){1000U}, &link) ==
        HK_ERR_DEADLINE_EXCEEDED);
    SUITE_CHECK(hk_lease_is_zero(&link.lease));
    SUITE_CHECK(hk_external_link_get_info(
        OWNER_A, &stale, &info) == HK_ERR_STALE_HANDLE);
    SUITE_CHECK(hk_external_link_acquire(
        OWNER_A, &request, HK_EXTERNAL_LINK_FEATURE_UART, &blocked) ==
        HK_ERR_INVALID_STATE);
    SUITE_CHECK(hk_external_link_release(
        OWNER_A, HK_DEADLINE_IMMEDIATE, &link) == HK_OK);
    return 0;
}

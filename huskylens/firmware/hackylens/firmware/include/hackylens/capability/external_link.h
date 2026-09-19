#ifndef HACKYLENS_CAPABILITY_EXTERNAL_LINK_H
#define HACKYLENS_CAPABILITY_EXTERNAL_LINK_H

#include "owner.h"

#ifdef __cplusplus
extern "C" {
#endif

#define HK_CAPABILITY_ID_EXTERNAL_LINK UINT32_C(0x00010004)

#define HK_EXTERNAL_LINK_FEATURE_UART (UINT64_C(1) << 0)
#define HK_EXTERNAL_LINK_FEATURE_I2C_CONTROLLER (UINT64_C(1) << 1)
#define HK_EXTERNAL_LINK_FEATURE_I2C_TARGET (UINT64_C(1) << 2)
#define HK_EXTERNAL_LINK_FEATURES_0_1                                  \
    (HK_EXTERNAL_LINK_FEATURE_UART |                                  \
     HK_EXTERNAL_LINK_FEATURE_I2C_CONTROLLER |                        \
     HK_EXTERNAL_LINK_FEATURE_I2C_TARGET)

#define HK_EXTERNAL_LINK_MODE_UNCONFIGURED UINT32_C(0)
#define HK_EXTERNAL_LINK_MODE_UART UINT32_C(1)
#define HK_EXTERNAL_LINK_MODE_I2C_CONTROLLER UINT32_C(2)
#define HK_EXTERNAL_LINK_MODE_I2C_TARGET UINT32_C(3)

#define HK_EXTERNAL_LINK_OP_UART_WRITE UINT32_C(1)
#define HK_EXTERNAL_LINK_OP_I2C_TRANSFER UINT32_C(2)

#define HK_EXTERNAL_LINK_PROGRESS_UART_DRAINING (UINT32_C(1) << 0)
#define HK_EXTERNAL_LINK_PROGRESS_RX_PREFIX_READABLE (UINT32_C(1) << 1)
#define HK_EXTERNAL_LINK_PROGRESS_TERMINAL (UINT32_C(1) << 2)

#define HK_EXTERNAL_LINK_TARGET_EVENT_NONE UINT32_C(0)
#define HK_EXTERNAL_LINK_TARGET_EVENT_WRITE UINT32_C(1)
#define HK_EXTERNAL_LINK_TARGET_EVENT_READ UINT32_C(2)
#define HK_EXTERNAL_LINK_TARGET_FILL_BYTE UINT8_C(0x00)

#define HK_EXTERNAL_LINK_INFO_VERSION 1U
#define HK_EXTERNAL_LINK_UART_CONFIG_VERSION 1U
#define HK_EXTERNAL_LINK_I2C_CONTROLLER_CONFIG_VERSION 1U
#define HK_EXTERNAL_LINK_I2C_TARGET_CONFIG_VERSION 1U
#define HK_EXTERNAL_LINK_I2C_TRANSFER_VERSION 1U
#define HK_EXTERNAL_LINK_OP_PROGRESS_VERSION 1U
#define HK_EXTERNAL_LINK_TARGET_EVENT_VERSION 1U

#define HK_EXTERNAL_LINK_REQUEST_0_1_INIT                              \
    {                                                                 \
        sizeof(hk_capability_request_t), HK_CAPABILITY_REQUEST_VERSION, \
        HK_CAPABILITY_ID_EXTERNAL_LINK, {0U, 1U, 0U, 0U},             \
        {0U, 2U, 0U, 0U}, 0U, 0U, 0U                                 \
    }

typedef struct
{
    uint16_t struct_size;
    uint16_t struct_version;
    uint64_t features;
    uint32_t uart_minimum_baud;
    uint32_t uart_maximum_baud;
    uint32_t i2c_controller_minimum_hz;
    uint32_t i2c_controller_maximum_hz;
    uint32_t maximum_poll_bytes;
    uint16_t maximum_i2c_write_bytes;
    uint16_t maximum_i2c_read_bytes;
    uint16_t maximum_target_receive_bytes;
    uint16_t maximum_target_response_bytes;
    uint32_t reserved;
} hk_external_link_info_t;

typedef struct
{
    uint16_t struct_size;
    uint16_t struct_version;
    uint32_t baud;
    uint32_t reserved;
} hk_external_link_uart_config_t;

typedef struct
{
    uint16_t struct_size;
    uint16_t struct_version;
    uint32_t frequency_hz;
    uint32_t reserved;
} hk_external_link_i2c_controller_config_t;

typedef struct
{
    uint16_t struct_size;
    uint16_t struct_version;
    uint16_t address;
    uint16_t reserved0;
    uint32_t reserved1;
} hk_external_link_i2c_target_config_t;

typedef struct
{
    uint16_t struct_size;
    uint16_t struct_version;
    uint16_t address;
    uint16_t reserved0;
    hk_buffer_view_t tx;
    hk_buffer_view_t rx;
    uint32_t reserved1;
} hk_external_link_i2c_transfer_t;

typedef struct
{
    uint32_t slot;
    uint32_t generation;
} hk_external_link_op_t;

#define HK_EXTERNAL_LINK_OP_NONE ((hk_external_link_op_t){0U, 0U})

typedef struct
{
    uint16_t struct_size;
    uint16_t struct_version;
    uint32_t kind;
    uint32_t tx_completed_bytes;
    uint32_t rx_completed_bytes;
    uint32_t flags;
    hk_result_t terminal_result;
    uint32_t reserved;
} hk_external_link_op_progress_t;

typedef struct
{
    uint16_t struct_size;
    uint16_t struct_version;
    uint32_t type;
    uint32_t received_bytes;
    uint32_t requested_bytes;
    uint32_t reserved;
} hk_external_link_target_event_t;

HK_DECLARE_CAPABILITY_HANDLE(hk_external_link_t);

hk_result_t hk_external_link_acquire(
    hk_owner_t owner,
    const hk_capability_request_t *request,
    uint64_t mode_features,
    hk_external_link_t *handle);
hk_result_t hk_external_link_release(
    hk_owner_t owner,
    hk_deadline_t deadline,
    hk_external_link_t *handle);
hk_result_t hk_external_link_get_info(
    hk_owner_t owner,
    const hk_external_link_t *handle,
    hk_external_link_info_t *info);
hk_result_t hk_external_link_get_mode(
    hk_owner_t owner,
    const hk_external_link_t *handle,
    uint32_t *mode);

hk_result_t hk_external_link_configure_uart(
    hk_owner_t owner,
    const hk_external_link_t *handle,
    const hk_external_link_uart_config_t *config);
hk_result_t hk_external_link_configure_i2c_controller(
    hk_owner_t owner,
    const hk_external_link_t *handle,
    const hk_external_link_i2c_controller_config_t *config);
hk_result_t hk_external_link_configure_i2c_target(
    hk_owner_t owner,
    const hk_external_link_t *handle,
    const hk_external_link_i2c_target_config_t *config);

hk_result_t hk_external_link_uart_write_begin(
    hk_owner_t owner,
    const hk_external_link_t *handle,
    const hk_buffer_view_t *tx,
    hk_deadline_t deadline,
    const hk_cancel_t *cancel,
    hk_external_link_op_t *operation);
hk_result_t hk_external_link_uart_read(
    hk_owner_t owner,
    const hk_external_link_t *handle,
    hk_buffer_view_t *rx,
    uint32_t *received_bytes);
hk_result_t hk_external_link_i2c_transfer_begin(
    hk_owner_t owner,
    const hk_external_link_t *handle,
    const hk_external_link_i2c_transfer_t *transfer,
    hk_deadline_t deadline,
    const hk_cancel_t *cancel,
    hk_external_link_op_t *operation);
hk_result_t hk_external_link_poll(
    hk_owner_t owner,
    const hk_external_link_t *handle,
    const hk_external_link_op_t *operation,
    hk_external_link_op_progress_t *progress);
hk_result_t hk_external_link_cancel(
    hk_owner_t owner,
    const hk_external_link_t *handle,
    const hk_external_link_op_t *operation,
    hk_external_link_op_progress_t *progress);

hk_result_t hk_external_link_i2c_target_poll(
    hk_owner_t owner,
    const hk_external_link_t *handle,
    hk_buffer_view_t *rx,
    hk_external_link_target_event_t *event);
hk_result_t hk_external_link_i2c_target_preload_response(
    hk_owner_t owner,
    const hk_external_link_t *handle,
    const hk_buffer_view_t *tx);

#ifdef __cplusplus
}
#endif

#endif

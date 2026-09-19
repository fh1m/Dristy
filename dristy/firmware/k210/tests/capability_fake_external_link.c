#include "capability_fake_external_link.h"

#include <stddef.h>
#include <string.h>

#define HK_FAKE_UART_MINIMUM_BAUD UINT32_C(1200)
#define HK_FAKE_UART_MAXIMUM_BAUD UINT32_C(1000000)
#define HK_FAKE_I2C_MINIMUM_HZ UINT32_C(100000)
#define HK_FAKE_I2C_MAXIMUM_HZ UINT32_C(400000)

enum
{
    HK_FAKE_OP_NONE = 0,
    HK_FAKE_OP_IN_FLIGHT = 1,
    HK_FAKE_OP_TERMINAL = 2
};

typedef struct
{
    uint32_t state;
    uint32_t generation;
    uint32_t kind;
    uint8_t terminal_observed;
    uint8_t reserved[3];
    hk_deadline_t deadline;
    hk_cancel_t cancel;
    const uint8_t *tx;
    uint8_t *rx;
    uint32_t tx_size;
    uint32_t rx_size;
    uint32_t tx_done;
    uint32_t rx_done;
    hk_result_t terminal_result;
    hk_result_t injected_result;
    uint32_t injected_after_bytes;
    uint32_t uart_drain_polls;
} hk_fake_external_link_operation_t;

typedef struct
{
    uint64_t features;
    uint64_t acquired_features;
    uint64_t now_us;
    uint32_t lease_generation;
    uint32_t next_operation_generation;
    uint32_t mode;
    uint32_t uart_drain_polls;
    uint8_t lease_active;
    uint8_t target_event_pending;
    uint8_t target_preload_active;
    uint8_t quarantined;
    hk_owner_t owner;
    hk_fake_external_link_operation_t operation;
    hk_result_t next_i2c_result;
    uint32_t next_i2c_after_bytes;
    uint8_t uart_rx[HK_FAKE_EXTERNAL_LINK_MAX_TRANSFER_BYTES];
    uint32_t uart_rx_size;
    uint8_t i2c_rx[HK_FAKE_EXTERNAL_LINK_MAX_TRANSFER_BYTES];
    uint32_t i2c_rx_size;
    uint32_t target_event_type;
    uint32_t target_event_received_bytes;
    uint32_t target_event_requested_bytes;
    uint8_t target_event_data[HK_FAKE_EXTERNAL_LINK_MAX_TRANSFER_BYTES];
    uint8_t target_preload[HK_FAKE_EXTERNAL_LINK_MAX_TRANSFER_BYTES];
    uint32_t target_preload_size;
    uint8_t target_read[HK_FAKE_EXTERNAL_LINK_MAX_TRANSFER_BYTES];
    uint32_t target_read_size;
    hk_fake_external_link_metrics_t metrics;
    hk_fake_external_link_event_t events[HK_FAKE_EXTERNAL_LINK_MAX_EVENTS];
} hk_fake_external_link_state_t;

static hk_fake_external_link_state_t g_fake;

static uint8_t owner_equal(hk_owner_t left, hk_owner_t right)
{
    return (uint8_t)(left.slot == right.slot &&
                     left.generation == right.generation);
}

static uint8_t operation_is_zero(const hk_external_link_op_t *operation)
{
    return (uint8_t)(operation && operation->slot == 0U &&
                     operation->generation == 0U);
}

static uint8_t version_less(hk_version_t left, hk_version_t right)
{
    if (left.major != right.major)
        return (uint8_t)(left.major < right.major);
    if (left.minor != right.minor)
        return (uint8_t)(left.minor < right.minor);
    return (uint8_t)(left.patch < right.patch);
}

static uint8_t deadline_expired(hk_deadline_t deadline)
{
    return (uint8_t)(deadline.at_us != 0U &&
                     g_fake.now_us >= deadline.at_us);
}

static uint8_t cancellation_requested(const hk_cancel_t *cancel)
{
    return (uint8_t)(cancel && cancel->probe &&
                     cancel->probe(cancel->context) != 0U);
}

static hk_result_t remember(hk_result_t result)
{
    g_fake.metrics.last_result = result;
    return result;
}

static void log_event(
    uint32_t type,
    uint32_t operation_kind,
    uint32_t offset,
    uint32_t size_bytes,
    hk_result_t result,
    hk_deadline_t deadline)
{
    hk_fake_external_link_event_t *event;

    if (g_fake.metrics.event_count >= HK_FAKE_EXTERNAL_LINK_MAX_EVENTS)
        return;
    event = &g_fake.events[g_fake.metrics.event_count++];
    event->type = type;
    event->mode = g_fake.mode;
    event->operation_kind = operation_kind;
    event->offset = offset;
    event->size_bytes = size_bytes;
    event->result = result;
    event->deadline = deadline;
}

static void reset_peripheral(void)
{
    ++g_fake.metrics.peripheral_resets;
    log_event(
        HK_FAKE_EXTERNAL_LINK_EVENT_RESET,
        g_fake.operation.kind,
        0U,
        0U,
        HK_OK,
        g_fake.operation.deadline);
}

static void clear_borrowed_buffers(void)
{
    g_fake.metrics.borrowed_tx_bytes = 0U;
    g_fake.metrics.borrowed_rx_bytes = 0U;
}

static void clear_target_state(void)
{
    g_fake.target_event_pending = 0U;
    g_fake.target_event_type = HK_EXTERNAL_LINK_TARGET_EVENT_NONE;
    g_fake.target_event_received_bytes = 0U;
    g_fake.target_event_requested_bytes = 0U;
    g_fake.target_preload_active = 0U;
    g_fake.target_preload_size = 0U;
    g_fake.target_read_size = 0U;
    memset(g_fake.target_event_data, 0, sizeof(g_fake.target_event_data));
    memset(g_fake.target_preload, 0, sizeof(g_fake.target_preload));
    memset(g_fake.target_read, 0, sizeof(g_fake.target_read));
}

static hk_result_t validate_request(
    const hk_capability_request_t *request,
    uint64_t mode_features)
{
    const hk_version_t implemented = {0U, 1U, 0U, 0U};
    uint64_t required;

    if (!request || request->struct_size < sizeof(*request) ||
        request->struct_version != HK_CAPABILITY_REQUEST_VERSION ||
        request->reserved != 0U || request->minimum.reserved != 0U ||
        request->maximum_exclusive.reserved != 0U ||
        request->id != HK_CAPABILITY_ID_EXTERNAL_LINK ||
        mode_features == 0U ||
        (mode_features & ~HK_EXTERNAL_LINK_FEATURES_0_1) != 0U ||
        (request->required_features & ~HK_EXTERNAL_LINK_FEATURES_0_1) != 0U)
        return HK_ERR_INVALID_ARGUMENT;
    if (version_less(implemented, request->minimum) ||
        !version_less(implemented, request->maximum_exclusive))
        return HK_ERR_VERSION_INCOMPATIBLE;
    required = request->required_features | mode_features;
    if ((required & ~g_fake.features) != 0U)
        return HK_ERR_FEATURE_UNAVAILABLE;
    return HK_OK;
}

static hk_result_t validate_handle(
    hk_owner_t owner,
    const hk_external_link_t *handle)
{
    if (!handle || hk_lease_is_zero(&handle->lease) ||
        handle->lease.capability_id != HK_CAPABILITY_ID_EXTERNAL_LINK)
        return HK_ERR_INVALID_ARGUMENT;
    if (!owner_equal(owner, handle->lease.owner))
        return HK_ERR_WRONG_OWNER;
    if (!g_fake.lease_active ||
        handle->lease.generation != g_fake.lease_generation ||
        handle->lease.slot != 0U || !owner_equal(owner, g_fake.owner))
        return HK_ERR_STALE_HANDLE;
    return HK_OK;
}

static hk_result_t validate_operation(
    const hk_external_link_op_t *operation)
{
    if (!operation || operation_is_zero(operation))
        return HK_ERR_INVALID_ARGUMENT;
    if (operation->slot != 0U ||
        g_fake.operation.state == HK_FAKE_OP_NONE ||
        operation->generation != g_fake.operation.generation)
        return HK_ERR_STALE_HANDLE;
    return HK_OK;
}

static hk_result_t validate_input_struct(
    uint16_t struct_size,
    uint16_t expected_size,
    uint16_t struct_version,
    uint16_t expected_version)
{
    if (struct_size < expected_size)
        return HK_ERR_INVALID_ARGUMENT;
    if (struct_version != expected_version)
        return HK_ERR_VERSION_INCOMPATIBLE;
    return HK_OK;
}

static hk_result_t validate_vector(
    const hk_buffer_view_t *view,
    uint32_t required_access,
    uint32_t maximum_bytes,
    uint8_t allow_empty)
{
    if (!view || view->stride_bytes != 0U ||
        (view->flags & required_access) == 0U ||
        (view->flags & ~(HK_BUFFER_ACCESS_READABLE |
                         HK_BUFFER_ACCESS_WRITABLE)) != 0U)
        return HK_ERR_INVALID_ARGUMENT;
    if (view->size_bytes == 0U)
    {
        if (!allow_empty || view->data != NULL)
            return HK_ERR_INVALID_ARGUMENT;
        return HK_OK;
    }
    if (!view->data)
        return HK_ERR_INVALID_ARGUMENT;
    if (view->size_bytes > maximum_bytes)
        return HK_ERR_LIMIT;
    return HK_OK;
}

static hk_result_t validate_mode_feature(uint64_t feature)
{
    if ((g_fake.features & feature) == 0U)
        return HK_ERR_FEATURE_UNAVAILABLE;
    if ((g_fake.acquired_features & feature) == 0U)
        return HK_ERR_NOT_DECLARED;
    return HK_OK;
}

static hk_result_t check_begin_state(
    hk_deadline_t deadline,
    const hk_cancel_t *cancel,
    hk_external_link_op_t *operation)
{
    if (!operation || deadline.at_us == UINT64_MAX)
        return HK_ERR_INVALID_ARGUMENT;
    *operation = HK_EXTERNAL_LINK_OP_NONE;
    if (g_fake.operation.state == HK_FAKE_OP_IN_FLIGHT ||
        (g_fake.operation.state == HK_FAKE_OP_TERMINAL &&
         !g_fake.operation.terminal_observed))
        return HK_ERR_BUSY;
    if (g_fake.next_operation_generation == UINT32_MAX)
        return HK_ERR_LIMIT;
    if (cancellation_requested(cancel))
        return HK_ERR_CANCELLED;
    if (deadline_expired(deadline))
        return HK_ERR_DEADLINE_EXCEEDED;
    return HK_OK;
}

static void start_operation(
    uint32_t kind,
    const uint8_t *tx,
    uint32_t tx_size,
    uint8_t *rx,
    uint32_t rx_size,
    hk_deadline_t deadline,
    const hk_cancel_t *cancel,
    hk_external_link_op_t *operation)
{
    uint32_t generation = ++g_fake.next_operation_generation;

    if (generation == 0U)
        generation = ++g_fake.next_operation_generation;
    memset(&g_fake.operation, 0, sizeof(g_fake.operation));
    g_fake.operation.state = HK_FAKE_OP_IN_FLIGHT;
    g_fake.operation.generation = generation;
    g_fake.operation.kind = kind;
    g_fake.operation.deadline = deadline;
    if (cancel)
        g_fake.operation.cancel = *cancel;
    g_fake.operation.tx = tx;
    g_fake.operation.rx = rx;
    g_fake.operation.tx_size = tx_size;
    g_fake.operation.rx_size = rx_size;
    g_fake.operation.terminal_result = HK_PENDING;
    g_fake.operation.uart_drain_polls = g_fake.uart_drain_polls;
    if (kind == HK_EXTERNAL_LINK_OP_I2C_TRANSFER)
    {
        g_fake.operation.injected_result = g_fake.next_i2c_result;
        g_fake.operation.injected_after_bytes = g_fake.next_i2c_after_bytes;
        g_fake.next_i2c_result = HK_OK;
        g_fake.next_i2c_after_bytes = 0U;
    }
    operation->slot = 0U;
    operation->generation = generation;
    g_fake.metrics.active_operations = 1U;
    g_fake.metrics.borrowed_tx_bytes = tx_size;
    g_fake.metrics.borrowed_rx_bytes = rx_size;
    g_fake.metrics.original_deadline = deadline;
}

static hk_result_t latch_terminal(hk_result_t result)
{
    g_fake.operation.state = HK_FAKE_OP_TERMINAL;
    g_fake.operation.terminal_result = result;
    g_fake.metrics.active_operations = 0U;
    clear_borrowed_buffers();
    if (result == HK_ERR_CANCELLED ||
        result == HK_ERR_DEADLINE_EXCEEDED || result == HK_ERR_IO)
        reset_peripheral();
    log_event(
        HK_FAKE_EXTERNAL_LINK_EVENT_TERMINAL,
        g_fake.operation.kind,
        g_fake.operation.tx_done + g_fake.operation.rx_done,
        0U,
        result,
        g_fake.operation.deadline);
    return result;
}

static void fill_progress(hk_external_link_op_progress_t *progress)
{
    uint32_t flags = 0U;

    memset(progress, 0, sizeof(*progress));
    progress->struct_size = sizeof(*progress);
    progress->struct_version = HK_EXTERNAL_LINK_OP_PROGRESS_VERSION;
    progress->kind = g_fake.operation.kind;
    progress->tx_completed_bytes = g_fake.operation.tx_done;
    progress->rx_completed_bytes = g_fake.operation.rx_done;
    if (g_fake.operation.kind == HK_EXTERNAL_LINK_OP_UART_WRITE &&
        g_fake.operation.state == HK_FAKE_OP_IN_FLIGHT &&
        g_fake.operation.tx_done == g_fake.operation.tx_size)
        flags |= HK_EXTERNAL_LINK_PROGRESS_UART_DRAINING;
    if (g_fake.operation.rx_done != 0U)
        flags |= HK_EXTERNAL_LINK_PROGRESS_RX_PREFIX_READABLE;
    if (g_fake.operation.state == HK_FAKE_OP_TERMINAL)
        flags |= HK_EXTERNAL_LINK_PROGRESS_TERMINAL;
    progress->flags = flags;
    progress->terminal_result =
        g_fake.operation.state == HK_FAKE_OP_TERMINAL
            ? g_fake.operation.terminal_result
            : HK_PENDING;
}

static hk_result_t set_mode(uint32_t mode)
{
    if (g_fake.operation.state == HK_FAKE_OP_IN_FLIGHT)
        return HK_ERR_BUSY;
    clear_target_state();
    if (g_fake.mode != HK_EXTERNAL_LINK_MODE_UNCONFIGURED)
        reset_peripheral();
    if (g_fake.mode != mode)
    {
        ++g_fake.metrics.route_changes;
        g_fake.mode = mode;
        g_fake.metrics.current_mode = mode;
        log_event(
            HK_FAKE_EXTERNAL_LINK_EVENT_ROUTE,
            0U,
            0U,
            0U,
            HK_OK,
            HK_DEADLINE_IMMEDIATE);
    }
    return HK_OK;
}

void hk_fake_external_link_reset(uint64_t features)
{
    memset(&g_fake, 0, sizeof(g_fake));
    g_fake.features = features & HK_EXTERNAL_LINK_FEATURES_0_1;
    g_fake.lease_generation = 1U;
    g_fake.next_i2c_result = HK_OK;
    g_fake.metrics.current_mode = HK_EXTERNAL_LINK_MODE_UNCONFIGURED;
    g_fake.metrics.last_result = HK_OK;
}

void hk_fake_external_link_set_now_us(uint64_t now_us)
{
    g_fake.now_us = now_us;
}

void hk_fake_external_link_set_uart_drain_polls(uint32_t polls)
{
    g_fake.uart_drain_polls = polls;
}

hk_result_t hk_fake_external_link_feed_uart_rx(
    const uint8_t *bytes, uint32_t size_bytes)
{
    if ((!bytes && size_bytes != 0U) ||
        size_bytes > HK_FAKE_EXTERNAL_LINK_MAX_TRANSFER_BYTES -
                         g_fake.uart_rx_size)
        return remember(HK_ERR_LIMIT);
    if (size_bytes != 0U)
        memcpy(&g_fake.uart_rx[g_fake.uart_rx_size], bytes, size_bytes);
    g_fake.uart_rx_size += size_bytes;
    return remember(HK_OK);
}

hk_result_t hk_fake_external_link_set_i2c_rx(
    const uint8_t *bytes, uint32_t size_bytes)
{
    if ((!bytes && size_bytes != 0U) ||
        size_bytes > HK_FAKE_EXTERNAL_LINK_MAX_TRANSFER_BYTES)
        return remember(HK_ERR_LIMIT);
    if (size_bytes != 0U)
        memcpy(g_fake.i2c_rx, bytes, size_bytes);
    g_fake.i2c_rx_size = size_bytes;
    return remember(HK_OK);
}

void hk_fake_external_link_fail_next_i2c(
    hk_result_t result, uint32_t after_bytes)
{
    g_fake.next_i2c_result = result;
    g_fake.next_i2c_after_bytes = after_bytes;
}

hk_result_t hk_fake_external_link_push_target_event(
    uint32_t type,
    const uint8_t *bytes,
    uint32_t received_bytes,
    uint32_t requested_bytes)
{
    uint32_t captured;
    uint32_t prefix;

    if (!g_fake.lease_active ||
        g_fake.mode != HK_EXTERNAL_LINK_MODE_I2C_TARGET)
        return remember(HK_ERR_INVALID_STATE);
    if (g_fake.target_event_pending)
        return remember(HK_ERR_BUSY);
    if ((type != HK_EXTERNAL_LINK_TARGET_EVENT_WRITE &&
         type != HK_EXTERNAL_LINK_TARGET_EVENT_READ) ||
        received_bytes > HK_FAKE_EXTERNAL_LINK_MAX_TRANSFER_BYTES ||
        (!bytes && received_bytes != 0U) ||
        (type == HK_EXTERNAL_LINK_TARGET_EVENT_WRITE &&
         requested_bytes != 0U) ||
        (type == HK_EXTERNAL_LINK_TARGET_EVENT_READ &&
         (received_bytes != 0U || requested_bytes == 0U)))
        return remember(HK_ERR_INVALID_ARGUMENT);
    if (received_bytes != 0U)
        memcpy(g_fake.target_event_data, bytes, received_bytes);
    if (type == HK_EXTERNAL_LINK_TARGET_EVENT_READ)
    {
        captured = requested_bytes;
        if (captured > HK_FAKE_EXTERNAL_LINK_MAX_TRANSFER_BYTES)
            captured = HK_FAKE_EXTERNAL_LINK_MAX_TRANSFER_BYTES;
        prefix = g_fake.target_preload_active ?
                     g_fake.target_preload_size :
                     0U;
        if (prefix > captured)
            prefix = captured;
        if (prefix != 0U)
            memcpy(g_fake.target_read, g_fake.target_preload, prefix);
        if (captured > prefix)
            memset(
                &g_fake.target_read[prefix],
                HK_EXTERNAL_LINK_TARGET_FILL_BYTE,
                captured - prefix);
        g_fake.target_read_size = captured;
        g_fake.metrics.target_read_bytes += requested_bytes;
        g_fake.metrics.target_zero_fill_bytes +=
            requested_bytes -
            (g_fake.target_preload_active &&
                     g_fake.target_preload_size < requested_bytes
                 ? g_fake.target_preload_size
                 : (g_fake.target_preload_active ? requested_bytes : 0U));
        log_event(
            HK_FAKE_EXTERNAL_LINK_EVENT_TARGET_READ,
            0U,
            0U,
            requested_bytes,
            HK_OK,
            HK_DEADLINE_IMMEDIATE);
        g_fake.target_preload_active = 0U;
        g_fake.target_preload_size = 0U;
        memset(g_fake.target_preload, 0, sizeof(g_fake.target_preload));
    }
    g_fake.target_event_type = type;
    g_fake.target_event_received_bytes = received_bytes;
    g_fake.target_event_requested_bytes = requested_bytes;
    g_fake.target_event_pending = 1U;
    return remember(HK_OK);
}

const uint8_t *hk_fake_external_link_target_preload(uint32_t *size_bytes)
{
    if (size_bytes)
        *size_bytes = g_fake.target_preload_active ?
                          g_fake.target_preload_size :
                          0U;
    return g_fake.target_preload;
}

const uint8_t *hk_fake_external_link_target_read(uint32_t *size_bytes)
{
    if (size_bytes)
        *size_bytes = g_fake.target_read_size;
    return g_fake.target_read;
}

const hk_fake_external_link_metrics_t *hk_fake_external_link_metrics(void)
{
    return &g_fake.metrics;
}

const hk_fake_external_link_event_t *hk_fake_external_link_event(
    uint32_t index)
{
    if (index >= g_fake.metrics.event_count)
        return NULL;
    return &g_fake.events[index];
}

hk_result_t hk_external_link_acquire(
    hk_owner_t owner,
    const hk_capability_request_t *request,
    uint64_t mode_features,
    hk_external_link_t *handle)
{
    hk_result_t result;
    uint32_t generation;

    if (!handle || !hk_lease_is_zero(&handle->lease) ||
        hk_owner_is_zero(owner))
        return remember(HK_ERR_INVALID_ARGUMENT);
    result = validate_request(request, mode_features);
    if (result != HK_OK)
        return remember(result);
    if (g_fake.quarantined)
        return remember(HK_ERR_INVALID_STATE);
    if (g_fake.lease_active)
        return remember(HK_ERR_BUSY);
    if (g_fake.lease_generation == UINT32_MAX)
        return remember(HK_ERR_LIMIT);
    generation = ++g_fake.lease_generation;
    g_fake.lease_active = 1U;
    g_fake.owner = owner;
    g_fake.acquired_features = request->required_features | mode_features;
    g_fake.mode = HK_EXTERNAL_LINK_MODE_UNCONFIGURED;
    g_fake.metrics.current_mode = g_fake.mode;
    g_fake.metrics.active_leases = 1U;
    handle->lease.slot = 0U;
    handle->lease.generation = generation;
    handle->lease.owner = owner;
    handle->lease.capability_id = HK_CAPABILITY_ID_EXTERNAL_LINK;
    log_event(
        HK_FAKE_EXTERNAL_LINK_EVENT_ACQUIRE,
        0U,
        0U,
        0U,
        HK_OK,
        HK_DEADLINE_IMMEDIATE);
    return remember(HK_OK);
}

hk_result_t hk_external_link_release(
    hk_owner_t owner,
    hk_deadline_t deadline,
    hk_external_link_t *handle)
{
    hk_result_t result;

    if (!handle || deadline.at_us == UINT64_MAX)
        return remember(HK_ERR_INVALID_ARGUMENT);
    if (hk_lease_is_zero(&handle->lease))
        return remember(HK_OK);
    if (handle->lease.capability_id == 0U)
        return remember(HK_ERR_STALE_HANDLE);
    if (handle->lease.capability_id != HK_CAPABILITY_ID_EXTERNAL_LINK)
        return remember(HK_ERR_INVALID_ARGUMENT);
    if (handle->lease.generation == 0U ||
        handle->lease.owner.generation == 0U)
        return remember(HK_ERR_STALE_HANDLE);
    result = validate_handle(owner, handle);
    if (result != HK_OK)
        return remember(result);
    if (deadline_expired(deadline))
    {
        memset(&g_fake.operation, 0, sizeof(g_fake.operation));
        clear_borrowed_buffers();
        g_fake.metrics.active_operations = 0U;
        g_fake.metrics.active_leases = 0U;
        g_fake.lease_active = 0U;
        g_fake.quarantined = 1U;
        handle->lease = HK_LEASE_NONE;
        return remember(HK_ERR_DEADLINE_EXCEEDED);
    }
    if (g_fake.operation.state == HK_FAKE_OP_IN_FLIGHT)
        reset_peripheral();
    memset(&g_fake.operation, 0, sizeof(g_fake.operation));
    clear_borrowed_buffers();
    g_fake.metrics.active_operations = 0U;
    g_fake.metrics.active_leases = 0U;
    g_fake.metrics.current_mode = HK_EXTERNAL_LINK_MODE_UNCONFIGURED;
    clear_target_state();
    g_fake.mode = HK_EXTERNAL_LINK_MODE_UNCONFIGURED;
    g_fake.lease_active = 0U;
    g_fake.acquired_features = 0U;
    log_event(
        HK_FAKE_EXTERNAL_LINK_EVENT_RELEASE,
        0U,
        0U,
        0U,
        HK_OK,
        deadline);
    handle->lease = HK_LEASE_NONE;
    return remember(HK_OK);
}

hk_result_t hk_external_link_get_info(
    hk_owner_t owner,
    const hk_external_link_t *handle,
    hk_external_link_info_t *info)
{
    hk_result_t result;

    if (!info)
        return remember(HK_ERR_INVALID_ARGUMENT);
    result = validate_handle(owner, handle);
    if (result != HK_OK)
        return remember(result);
    memset(info, 0, sizeof(*info));
    info->struct_size = sizeof(*info);
    info->struct_version = HK_EXTERNAL_LINK_INFO_VERSION;
    info->features = g_fake.features;
    info->uart_minimum_baud = HK_FAKE_UART_MINIMUM_BAUD;
    info->uart_maximum_baud = HK_FAKE_UART_MAXIMUM_BAUD;
    info->i2c_controller_minimum_hz = HK_FAKE_I2C_MINIMUM_HZ;
    info->i2c_controller_maximum_hz = HK_FAKE_I2C_MAXIMUM_HZ;
    info->maximum_poll_bytes = HK_FAKE_EXTERNAL_LINK_MAX_POLL_BYTES;
    info->maximum_i2c_write_bytes =
        (uint16_t)HK_FAKE_EXTERNAL_LINK_MAX_TRANSFER_BYTES;
    info->maximum_i2c_read_bytes =
        (uint16_t)HK_FAKE_EXTERNAL_LINK_MAX_TRANSFER_BYTES;
    info->maximum_target_receive_bytes =
        (uint16_t)HK_FAKE_EXTERNAL_LINK_MAX_TRANSFER_BYTES;
    info->maximum_target_response_bytes =
        (uint16_t)HK_FAKE_EXTERNAL_LINK_MAX_TRANSFER_BYTES;
    return remember(HK_OK);
}

hk_result_t hk_external_link_get_mode(
    hk_owner_t owner,
    const hk_external_link_t *handle,
    uint32_t *mode)
{
    hk_result_t result;

    if (!mode)
        return remember(HK_ERR_INVALID_ARGUMENT);
    result = validate_handle(owner, handle);
    if (result != HK_OK)
        return remember(result);
    *mode = g_fake.mode;
    return remember(HK_OK);
}

hk_result_t hk_external_link_configure_uart(
    hk_owner_t owner,
    const hk_external_link_t *handle,
    const hk_external_link_uart_config_t *config)
{
    hk_result_t result;

    if (!config)
        return remember(HK_ERR_INVALID_ARGUMENT);
    result = validate_input_struct(
        config->struct_size,
        (uint16_t)sizeof(*config),
        config->struct_version,
        HK_EXTERNAL_LINK_UART_CONFIG_VERSION);
    if (result != HK_OK || config->reserved != 0U ||
        config->baud < HK_FAKE_UART_MINIMUM_BAUD ||
        config->baud > HK_FAKE_UART_MAXIMUM_BAUD)
        return remember(result == HK_OK ? HK_ERR_INVALID_ARGUMENT : result);
    result = validate_handle(owner, handle);
    if (result == HK_OK)
        result = validate_mode_feature(HK_EXTERNAL_LINK_FEATURE_UART);
    if (result == HK_OK)
        result = set_mode(HK_EXTERNAL_LINK_MODE_UART);
    return remember(result);
}

hk_result_t hk_external_link_configure_i2c_controller(
    hk_owner_t owner,
    const hk_external_link_t *handle,
    const hk_external_link_i2c_controller_config_t *config)
{
    hk_result_t result;

    if (!config)
        return remember(HK_ERR_INVALID_ARGUMENT);
    result = validate_input_struct(
        config->struct_size,
        (uint16_t)sizeof(*config),
        config->struct_version,
        HK_EXTERNAL_LINK_I2C_CONTROLLER_CONFIG_VERSION);
    if (result != HK_OK || config->reserved != 0U ||
        config->frequency_hz < HK_FAKE_I2C_MINIMUM_HZ ||
        config->frequency_hz > HK_FAKE_I2C_MAXIMUM_HZ)
        return remember(result == HK_OK ? HK_ERR_INVALID_ARGUMENT : result);
    result = validate_handle(owner, handle);
    if (result == HK_OK)
        result = validate_mode_feature(
            HK_EXTERNAL_LINK_FEATURE_I2C_CONTROLLER);
    if (result == HK_OK)
        result = set_mode(HK_EXTERNAL_LINK_MODE_I2C_CONTROLLER);
    return remember(result);
}

hk_result_t hk_external_link_configure_i2c_target(
    hk_owner_t owner,
    const hk_external_link_t *handle,
    const hk_external_link_i2c_target_config_t *config)
{
    hk_result_t result;

    if (!config)
        return remember(HK_ERR_INVALID_ARGUMENT);
    result = validate_input_struct(
        config->struct_size,
        (uint16_t)sizeof(*config),
        config->struct_version,
        HK_EXTERNAL_LINK_I2C_TARGET_CONFIG_VERSION);
    if (result != HK_OK || config->reserved0 != 0U ||
        config->reserved1 != 0U || config->address > UINT16_C(0x7f))
        return remember(result == HK_OK ? HK_ERR_INVALID_ARGUMENT : result);
    result = validate_handle(owner, handle);
    if (result == HK_OK)
        result = validate_mode_feature(HK_EXTERNAL_LINK_FEATURE_I2C_TARGET);
    if (result == HK_OK)
        result = set_mode(HK_EXTERNAL_LINK_MODE_I2C_TARGET);
    return remember(result);
}

hk_result_t hk_external_link_uart_write_begin(
    hk_owner_t owner,
    const hk_external_link_t *handle,
    const hk_buffer_view_t *tx,
    hk_deadline_t deadline,
    const hk_cancel_t *cancel,
    hk_external_link_op_t *operation)
{
    hk_result_t result = validate_vector(
        tx,
        HK_BUFFER_ACCESS_READABLE,
        HK_FAKE_EXTERNAL_LINK_MAX_TRANSFER_BYTES,
        0U);

    if (result != HK_OK)
        return remember(result);
    result = validate_handle(owner, handle);
    if (result == HK_OK && g_fake.mode != HK_EXTERNAL_LINK_MODE_UART)
        result = HK_ERR_INVALID_STATE;
    if (result == HK_OK)
        result = check_begin_state(deadline, cancel, operation);
    if (result != HK_OK)
        return remember(result);
    start_operation(
        HK_EXTERNAL_LINK_OP_UART_WRITE,
        (const uint8_t *)tx->data,
        tx->size_bytes,
        NULL,
        0U,
        deadline,
        cancel,
        operation);
    return remember(HK_PENDING);
}

hk_result_t hk_external_link_uart_read(
    hk_owner_t owner,
    const hk_external_link_t *handle,
    hk_buffer_view_t *rx,
    uint32_t *received_bytes)
{
    hk_result_t result;
    uint32_t amount;

    if (!received_bytes)
        return remember(HK_ERR_INVALID_ARGUMENT);
    *received_bytes = 0U;
    result = validate_vector(
        rx,
        HK_BUFFER_ACCESS_WRITABLE,
        HK_FAKE_EXTERNAL_LINK_MAX_TRANSFER_BYTES,
        1U);
    if (result != HK_OK)
        return remember(result);
    result = validate_handle(owner, handle);
    if (result != HK_OK)
        return remember(result);
    if (g_fake.mode != HK_EXTERNAL_LINK_MODE_UART)
        return remember(HK_ERR_INVALID_STATE);
    amount = g_fake.uart_rx_size;
    if (amount > rx->size_bytes)
        amount = rx->size_bytes;
    if (amount > HK_FAKE_EXTERNAL_LINK_MAX_POLL_BYTES)
        amount = HK_FAKE_EXTERNAL_LINK_MAX_POLL_BYTES;
    if (amount != 0U)
    {
        memcpy(rx->data, g_fake.uart_rx, amount);
        memmove(
            g_fake.uart_rx,
            &g_fake.uart_rx[amount],
            g_fake.uart_rx_size - amount);
        g_fake.uart_rx_size -= amount;
        g_fake.metrics.uart_rx_bytes += amount;
        log_event(
            HK_FAKE_EXTERNAL_LINK_EVENT_UART_RX,
            0U,
            g_fake.metrics.uart_rx_bytes - amount,
            amount,
            HK_OK,
            HK_DEADLINE_IMMEDIATE);
    }
    *received_bytes = amount;
    return remember(HK_OK);
}

hk_result_t hk_external_link_i2c_transfer_begin(
    hk_owner_t owner,
    const hk_external_link_t *handle,
    const hk_external_link_i2c_transfer_t *transfer,
    hk_deadline_t deadline,
    const hk_cancel_t *cancel,
    hk_external_link_op_t *operation)
{
    hk_result_t result;

    if (!transfer)
        return remember(HK_ERR_INVALID_ARGUMENT);
    result = validate_input_struct(
        transfer->struct_size,
        (uint16_t)sizeof(*transfer),
        transfer->struct_version,
        HK_EXTERNAL_LINK_I2C_TRANSFER_VERSION);
    if (result != HK_OK || transfer->reserved0 != 0U ||
        transfer->reserved1 != 0U || transfer->address > UINT16_C(0x7f))
        return remember(result == HK_OK ? HK_ERR_INVALID_ARGUMENT : result);
    result = validate_vector(
        &transfer->tx,
        HK_BUFFER_ACCESS_READABLE,
        HK_FAKE_EXTERNAL_LINK_MAX_TRANSFER_BYTES,
        1U);
    if (result == HK_OK)
        result = validate_vector(
            &transfer->rx,
            HK_BUFFER_ACCESS_WRITABLE,
            HK_FAKE_EXTERNAL_LINK_MAX_TRANSFER_BYTES,
            1U);
    if (result != HK_OK ||
        (transfer->tx.size_bytes == 0U && transfer->rx.size_bytes == 0U))
        return remember(result == HK_OK ? HK_ERR_INVALID_ARGUMENT : result);
    result = validate_handle(owner, handle);
    if (result == HK_OK &&
        g_fake.mode != HK_EXTERNAL_LINK_MODE_I2C_CONTROLLER)
        result = HK_ERR_INVALID_STATE;
    if (result == HK_OK)
        result = check_begin_state(deadline, cancel, operation);
    if (result != HK_OK)
        return remember(result);
    start_operation(
        HK_EXTERNAL_LINK_OP_I2C_TRANSFER,
        (const uint8_t *)transfer->tx.data,
        transfer->tx.size_bytes,
        (uint8_t *)transfer->rx.data,
        transfer->rx.size_bytes,
        deadline,
        cancel,
        operation);
    return remember(HK_PENDING);
}

hk_result_t hk_external_link_poll(
    hk_owner_t owner,
    const hk_external_link_t *handle,
    const hk_external_link_op_t *operation,
    hk_external_link_op_progress_t *progress)
{
    hk_result_t result;
    uint32_t budget = HK_FAKE_EXTERNAL_LINK_MAX_POLL_BYTES;
    uint32_t amount;
    uint32_t total;

    if (!progress)
        return remember(HK_ERR_INVALID_ARGUMENT);
    result = validate_handle(owner, handle);
    if (result == HK_OK)
        result = validate_operation(operation);
    if (result != HK_OK)
        return remember(result);
    ++g_fake.metrics.poll_calls;
    if (g_fake.operation.state == HK_FAKE_OP_TERMINAL)
    {
        fill_progress(progress);
        g_fake.operation.terminal_observed = 1U;
        return remember(g_fake.operation.terminal_result);
    }
    if (cancellation_requested(&g_fake.operation.cancel))
    {
        result = latch_terminal(HK_ERR_CANCELLED);
        fill_progress(progress);
        g_fake.operation.terminal_observed = 1U;
        return remember(result);
    }
    if (deadline_expired(g_fake.operation.deadline))
    {
        result = latch_terminal(HK_ERR_DEADLINE_EXCEEDED);
        fill_progress(progress);
        g_fake.operation.terminal_observed = 1U;
        return remember(result);
    }

    if (g_fake.operation.kind == HK_EXTERNAL_LINK_OP_UART_WRITE)
    {
        amount = g_fake.operation.tx_size - g_fake.operation.tx_done;
        if (amount > budget)
            amount = budget;
        if (amount != 0U)
        {
            uint32_t offset = g_fake.operation.tx_done;
            g_fake.operation.tx_done += amount;
            g_fake.metrics.uart_tx_bytes += amount;
            log_event(
                HK_FAKE_EXTERNAL_LINK_EVENT_UART_TX,
                g_fake.operation.kind,
                offset,
                amount,
                HK_PENDING,
                g_fake.operation.deadline);
        }
        if (g_fake.operation.tx_done == g_fake.operation.tx_size)
        {
            if (g_fake.operation.uart_drain_polls != 0U)
                --g_fake.operation.uart_drain_polls;
            else
                result = latch_terminal(HK_OK);
        }
    }
    else
    {
        total = g_fake.operation.tx_done + g_fake.operation.rx_done;
        if (g_fake.operation.injected_result != HK_OK)
        {
            if (total >= g_fake.operation.injected_after_bytes)
            {
                result = latch_terminal(g_fake.operation.injected_result);
                fill_progress(progress);
                g_fake.operation.terminal_observed = 1U;
                return remember(result);
            }
            if (budget > g_fake.operation.injected_after_bytes - total)
                budget = g_fake.operation.injected_after_bytes - total;
        }
        amount = g_fake.operation.tx_size - g_fake.operation.tx_done;
        if (amount > budget)
            amount = budget;
        if (amount != 0U)
        {
            uint32_t offset = g_fake.operation.tx_done;
            g_fake.operation.tx_done += amount;
            budget -= amount;
            g_fake.metrics.i2c_tx_bytes += amount;
            log_event(
                HK_FAKE_EXTERNAL_LINK_EVENT_I2C_TX,
                g_fake.operation.kind,
                offset,
                amount,
                HK_PENDING,
                g_fake.operation.deadline);
        }
        amount = g_fake.operation.rx_size - g_fake.operation.rx_done;
        if (amount > budget)
            amount = budget;
        if (amount != 0U)
        {
            uint32_t offset = g_fake.operation.rx_done;
            uint32_t index;
            for (index = 0U; index < amount; ++index)
            {
                uint32_t source = offset + index;
                g_fake.operation.rx[source] =
                    source < g_fake.i2c_rx_size
                        ? g_fake.i2c_rx[source]
                        : (uint8_t)(UINT8_C(0x80) + (uint8_t)source);
            }
            g_fake.operation.rx_done += amount;
            g_fake.metrics.i2c_rx_bytes += amount;
            log_event(
                HK_FAKE_EXTERNAL_LINK_EVENT_I2C_RX,
                g_fake.operation.kind,
                offset,
                amount,
                HK_PENDING,
                g_fake.operation.deadline);
        }
        total = g_fake.operation.tx_done + g_fake.operation.rx_done;
        if (g_fake.operation.injected_result != HK_OK &&
            total >= g_fake.operation.injected_after_bytes)
            result = latch_terminal(g_fake.operation.injected_result);
        else if (g_fake.operation.tx_done == g_fake.operation.tx_size &&
                 g_fake.operation.rx_done == g_fake.operation.rx_size)
            result = latch_terminal(HK_OK);
    }
    if (g_fake.operation.state == HK_FAKE_OP_IN_FLIGHT &&
        g_fake.operation.deadline.at_us == 0U)
        (void)latch_terminal(HK_ERR_DEADLINE_EXCEEDED);
    fill_progress(progress);
    if (g_fake.operation.state == HK_FAKE_OP_TERMINAL)
    {
        g_fake.operation.terminal_observed = 1U;
        return remember(g_fake.operation.terminal_result);
    }
    return remember(HK_PENDING);
}

hk_result_t hk_external_link_cancel(
    hk_owner_t owner,
    const hk_external_link_t *handle,
    const hk_external_link_op_t *operation,
    hk_external_link_op_progress_t *progress)
{
    hk_result_t result;

    if (!progress)
        return remember(HK_ERR_INVALID_ARGUMENT);
    result = validate_handle(owner, handle);
    if (result == HK_OK)
        result = validate_operation(operation);
    if (result != HK_OK)
        return remember(result);
    if (g_fake.operation.state == HK_FAKE_OP_IN_FLIGHT)
        result = latch_terminal(HK_ERR_CANCELLED);
    else
        result = g_fake.operation.terminal_result;
    fill_progress(progress);
    g_fake.operation.terminal_observed = 1U;
    return remember(result);
}

hk_result_t hk_external_link_i2c_target_poll(
    hk_owner_t owner,
    const hk_external_link_t *handle,
    hk_buffer_view_t *rx,
    hk_external_link_target_event_t *event)
{
    hk_result_t result;

    if (!event)
        return remember(HK_ERR_INVALID_ARGUMENT);
    result = validate_vector(
        rx,
        HK_BUFFER_ACCESS_WRITABLE,
        HK_FAKE_EXTERNAL_LINK_MAX_TRANSFER_BYTES,
        1U);
    if (result != HK_OK)
        return remember(result);
    result = validate_handle(owner, handle);
    if (result != HK_OK)
        return remember(result);
    if (g_fake.mode != HK_EXTERNAL_LINK_MODE_I2C_TARGET)
        return remember(HK_ERR_INVALID_STATE);
    memset(event, 0, sizeof(*event));
    event->struct_size = sizeof(*event);
    event->struct_version = HK_EXTERNAL_LINK_TARGET_EVENT_VERSION;
    if (!g_fake.target_event_pending)
    {
        event->type = HK_EXTERNAL_LINK_TARGET_EVENT_NONE;
        return remember(HK_PENDING);
    }
    if (rx->size_bytes < g_fake.target_event_received_bytes)
        return remember(HK_ERR_LIMIT);
    if (g_fake.target_event_received_bytes != 0U)
        memcpy(
            rx->data,
            g_fake.target_event_data,
            g_fake.target_event_received_bytes);
    event->type = g_fake.target_event_type;
    event->received_bytes = g_fake.target_event_received_bytes;
    event->requested_bytes = g_fake.target_event_requested_bytes;
    if (g_fake.target_event_type == HK_EXTERNAL_LINK_TARGET_EVENT_WRITE)
    {
        g_fake.metrics.target_write_bytes +=
            g_fake.target_event_received_bytes;
        log_event(
            HK_FAKE_EXTERNAL_LINK_EVENT_TARGET_WRITE,
            0U,
            0U,
            g_fake.target_event_received_bytes,
            HK_OK,
            HK_DEADLINE_IMMEDIATE);
    }
    g_fake.target_event_pending = 0U;
    return remember(HK_OK);
}

hk_result_t hk_external_link_i2c_target_preload_response(
    hk_owner_t owner,
    const hk_external_link_t *handle,
    const hk_buffer_view_t *tx)
{
    hk_result_t result = validate_vector(
        tx,
        HK_BUFFER_ACCESS_READABLE,
        HK_FAKE_EXTERNAL_LINK_MAX_TRANSFER_BYTES,
        1U);

    if (result != HK_OK)
        return remember(result);
    result = validate_handle(owner, handle);
    if (result != HK_OK)
        return remember(result);
    if (g_fake.mode != HK_EXTERNAL_LINK_MODE_I2C_TARGET)
        return remember(HK_ERR_INVALID_STATE);
    if (g_fake.target_preload_active)
        ++g_fake.metrics.target_preload_replacements;
    memset(g_fake.target_preload, 0, sizeof(g_fake.target_preload));
    if (tx->size_bytes != 0U)
        memcpy(g_fake.target_preload, tx->data, tx->size_bytes);
    g_fake.target_preload_size = tx->size_bytes;
    g_fake.target_preload_active = (uint8_t)(tx->size_bytes != 0U);
    g_fake.metrics.target_preload_bytes += tx->size_bytes;
    log_event(
        HK_FAKE_EXTERNAL_LINK_EVENT_TARGET_PRELOAD,
        0U,
        0U,
        tx->size_bytes,
        HK_OK,
        HK_DEADLINE_IMMEDIATE);
    return remember(HK_OK);
}

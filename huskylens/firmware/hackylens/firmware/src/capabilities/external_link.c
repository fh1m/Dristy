#include <hackylens/capability/external_link.h>

#include <limits.h>
#include <stddef.h>

#include "capability_core_binding.h"
#include "external_link_provider.h"

static hk_result_t quarantine_internal(
    hk_owner_t owner, const hk_external_link_t *handle, hk_result_t result)
{
    if(result == HK_ERR_INTERNAL && handle)
        (void)capability_owner_runtime_quarantine(
            owner, &handle->lease, HK_CAPABILITY_ID_EXTERNAL_LINK);
    return result;
}

static hk_result_t provider_for(
    hk_owner_t owner, const hk_external_link_t *handle,
    hk_external_link_provider_t **provider)
{
    void *context = NULL;
    hk_result_t result;

    if(!handle || !provider)
        return HK_ERR_INVALID_ARGUMENT;
    result = capability_owner_runtime_validate(
        owner, &handle->lease, HK_CAPABILITY_ID_EXTERNAL_LINK, &context);
    if(result != HK_OK)
        return result;
    *provider = (hk_external_link_provider_t *)context;
    if(!*provider || !(*provider)->open || !(*provider)->close ||
       !(*provider)->get_info || !(*provider)->get_mode ||
       !(*provider)->configure_uart ||
       !(*provider)->configure_i2c_controller ||
       !(*provider)->configure_i2c_target ||
       !(*provider)->uart_write_begin || !(*provider)->uart_read ||
       !(*provider)->i2c_transfer_begin || !(*provider)->poll ||
       !(*provider)->cancel || !(*provider)->target_poll ||
       !(*provider)->target_preload || (*provider)->reserved != 0U)
    {
        (void)capability_owner_runtime_quarantine(
            owner, &handle->lease, HK_CAPABILITY_ID_EXTERNAL_LINK);
        return HK_ERR_INTERNAL;
    }
    return HK_OK;
}

static hk_result_t validate_view(
    const hk_buffer_view_t *view, uint32_t access, uint32_t maximum,
    uint8_t allow_empty)
{
    if(!view || view->stride_bytes != 0U || view->flags != access ||
       (!view->data && view->size_bytes != 0U) ||
       (!allow_empty && view->size_bytes == 0U))
        return HK_ERR_INVALID_ARGUMENT;
    if(view->size_bytes > maximum)
        return HK_ERR_LIMIT;
    return HK_OK;
}

static hk_result_t validate_struct(
    uint16_t size, uint16_t expected_size,
    uint16_t version, uint16_t expected_version)
{
    if(size < expected_size)
        return HK_ERR_INVALID_ARGUMENT;
    if(version != expected_version)
        return HK_ERR_VERSION_INCOMPATIBLE;
    return HK_OK;
}

hk_result_t hk_external_link_acquire(
    hk_owner_t owner, const hk_capability_request_t *request,
    uint64_t mode_features, hk_external_link_t *handle)
{
    hk_capability_request_t provider_request;
    hk_external_link_provider_t *provider;
    hk_result_t result;

    if(!request || !handle || mode_features == 0U ||
       (mode_features & ~HK_EXTERNAL_LINK_FEATURES_0_1) != 0U)
        return HK_ERR_INVALID_ARGUMENT;
    handle->lease = HK_LEASE_NONE;
    provider_request = *request;
    provider_request.required_features |= mode_features;
    result = capability_owner_runtime_acquire(
        owner, &provider_request, HK_CAPABILITY_ID_EXTERNAL_LINK,
        &handle->lease);
    if(result != HK_OK)
        return result;
    result = provider_for(owner, handle, &provider);
    if(result == HK_OK)
        result = provider->open(
            provider->context, &handle->lease, mode_features);
    if(result != HK_OK)
    {
        (void)capability_owner_runtime_release(
            owner, HK_CAPABILITY_ID_EXTERNAL_LINK,
            HK_DEADLINE_IMMEDIATE, &handle->lease);
        return result;
    }
    return HK_OK;
}

hk_result_t hk_external_link_release(
    hk_owner_t owner, hk_deadline_t deadline, hk_external_link_t *handle)
{
    hk_external_link_provider_t *provider;
    hk_result_t result;

    if(!handle || deadline.at_us == UINT64_MAX)
        return HK_ERR_INVALID_ARGUMENT;
    if(hk_lease_is_zero(&handle->lease))
        return HK_OK;
    result = provider_for(owner, handle, &provider);
    if(result != HK_OK)
        return result;
    result = provider->close(provider->context, &handle->lease, deadline);
    if(result != HK_OK)
    {
        hk_result_t cleanup_result;

        (void)capability_owner_runtime_quarantine(
            owner, &handle->lease, HK_CAPABILITY_ID_EXTERNAL_LINK);
        cleanup_result = capability_owner_runtime_release(
            owner, HK_CAPABILITY_ID_EXTERNAL_LINK, deadline, &handle->lease);
        (void)cleanup_result;
        return result;
    }
    return capability_owner_runtime_release(
        owner, HK_CAPABILITY_ID_EXTERNAL_LINK, deadline, &handle->lease);
}

#define EXTERNAL_CALL(name, argument)                                         \
    do {                                                                       \
        hk_external_link_provider_t *provider;                                 \
        hk_result_t result = provider_for(owner, handle, &provider);           \
        if(result == HK_OK)                                                    \
            result = provider->name(                                           \
                provider->context, &handle->lease, argument);                  \
        return quarantine_internal(owner, handle, result);                     \
    } while(0)

hk_result_t hk_external_link_get_info(
    hk_owner_t owner, const hk_external_link_t *handle,
    hk_external_link_info_t *info)
{
    if(!info)
        return HK_ERR_INVALID_ARGUMENT;
    EXTERNAL_CALL(get_info, info);
}

hk_result_t hk_external_link_get_mode(
    hk_owner_t owner, const hk_external_link_t *handle, uint32_t *mode)
{
    if(!mode)
        return HK_ERR_INVALID_ARGUMENT;
    EXTERNAL_CALL(get_mode, mode);
}

hk_result_t hk_external_link_configure_uart(
    hk_owner_t owner, const hk_external_link_t *handle,
    const hk_external_link_uart_config_t *config)
{
    hk_result_t result;

    if(!config)
        return HK_ERR_INVALID_ARGUMENT;
    result = validate_struct(
        config->struct_size, sizeof(*config), config->struct_version,
        HK_EXTERNAL_LINK_UART_CONFIG_VERSION);
    if(result != HK_OK)
        return result;
    if(config->reserved != 0U)
        return HK_ERR_INVALID_ARGUMENT;
    EXTERNAL_CALL(configure_uart, config);
}

hk_result_t hk_external_link_configure_i2c_controller(
    hk_owner_t owner, const hk_external_link_t *handle,
    const hk_external_link_i2c_controller_config_t *config)
{
    hk_result_t result;

    if(!config)
        return HK_ERR_INVALID_ARGUMENT;
    result = validate_struct(
        config->struct_size, sizeof(*config), config->struct_version,
        HK_EXTERNAL_LINK_I2C_CONTROLLER_CONFIG_VERSION);
    if(result != HK_OK)
        return result;
    if(config->reserved != 0U)
        return HK_ERR_INVALID_ARGUMENT;
    EXTERNAL_CALL(configure_i2c_controller, config);
}

hk_result_t hk_external_link_configure_i2c_target(
    hk_owner_t owner, const hk_external_link_t *handle,
    const hk_external_link_i2c_target_config_t *config)
{
    hk_result_t result;

    if(!config)
        return HK_ERR_INVALID_ARGUMENT;
    result = validate_struct(
        config->struct_size, sizeof(*config), config->struct_version,
        HK_EXTERNAL_LINK_I2C_TARGET_CONFIG_VERSION);
    if(result != HK_OK)
        return result;
    if(config->reserved0 != 0U || config->reserved1 != 0U ||
       config->address > UINT16_C(0x7f))
        return HK_ERR_INVALID_ARGUMENT;
    EXTERNAL_CALL(configure_i2c_target, config);
}

hk_result_t hk_external_link_uart_write_begin(
    hk_owner_t owner, const hk_external_link_t *handle,
    const hk_buffer_view_t *tx, hk_deadline_t deadline,
    const hk_cancel_t *cancel, hk_external_link_op_t *operation)
{
    hk_external_link_provider_t *provider;
    hk_result_t result;

    if(!operation || deadline.at_us == UINT64_MAX)
        return HK_ERR_INVALID_ARGUMENT;
    *operation = HK_EXTERNAL_LINK_OP_NONE;
    result = validate_view(tx, HK_BUFFER_ACCESS_READABLE, 256U, 0U);
    if(result != HK_OK)
        return result;
    result = provider_for(owner, handle, &provider);
    if(result == HK_OK)
        result = provider->uart_write_begin(
            provider->context, &handle->lease, tx, deadline, cancel,
            operation);
    return quarantine_internal(owner, handle, result);
}

hk_result_t hk_external_link_uart_read(
    hk_owner_t owner, const hk_external_link_t *handle,
    hk_buffer_view_t *rx, uint32_t *received_bytes)
{
    hk_external_link_provider_t *provider;
    hk_result_t result;

    if(!received_bytes)
        return HK_ERR_INVALID_ARGUMENT;
    *received_bytes = 0U;
    result = validate_view(rx, HK_BUFFER_ACCESS_WRITABLE, 256U, 1U);
    if(result != HK_OK)
        return result;
    result = provider_for(owner, handle, &provider);
    if(result == HK_OK)
        result = provider->uart_read(
            provider->context, &handle->lease, rx, received_bytes);
    return quarantine_internal(owner, handle, result);
}

hk_result_t hk_external_link_i2c_transfer_begin(
    hk_owner_t owner, const hk_external_link_t *handle,
    const hk_external_link_i2c_transfer_t *transfer,
    hk_deadline_t deadline, const hk_cancel_t *cancel,
    hk_external_link_op_t *operation)
{
    hk_external_link_provider_t *provider;
    hk_result_t result;

    if(!transfer || !operation || deadline.at_us == UINT64_MAX)
        return HK_ERR_INVALID_ARGUMENT;
    *operation = HK_EXTERNAL_LINK_OP_NONE;
    result = validate_struct(
        transfer->struct_size, sizeof(*transfer), transfer->struct_version,
        HK_EXTERNAL_LINK_I2C_TRANSFER_VERSION);
    if(result != HK_OK)
        return result;
    if(transfer->reserved0 != 0U || transfer->reserved1 != 0U ||
       transfer->address > UINT16_C(0x7f))
        return HK_ERR_INVALID_ARGUMENT;
    result = validate_view(
        &transfer->tx, HK_BUFFER_ACCESS_READABLE, 256U, 1U);
    if(result == HK_OK)
        result = validate_view(
            &transfer->rx, HK_BUFFER_ACCESS_WRITABLE, 256U, 1U);
    if(result != HK_OK)
        return result;
    if(transfer->tx.size_bytes == 0U && transfer->rx.size_bytes == 0U)
        return HK_ERR_INVALID_ARGUMENT;
    result = provider_for(owner, handle, &provider);
    if(result == HK_OK)
        result = provider->i2c_transfer_begin(
            provider->context, &handle->lease, transfer, deadline, cancel,
            operation);
    return quarantine_internal(owner, handle, result);
}

static hk_result_t operation_call(
    hk_owner_t owner, const hk_external_link_t *handle,
    const hk_external_link_op_t *operation,
    hk_external_link_op_progress_t *progress, uint8_t cancel)
{
    hk_external_link_provider_t *provider;
    hk_result_t result;

    if(!operation || !progress)
        return HK_ERR_INVALID_ARGUMENT;
    result = provider_for(owner, handle, &provider);
    if(result == HK_OK)
        result = (cancel ? provider->cancel : provider->poll)(
            provider->context, &handle->lease, operation, progress);
    return quarantine_internal(owner, handle, result);
}

hk_result_t hk_external_link_poll(
    hk_owner_t owner, const hk_external_link_t *handle,
    const hk_external_link_op_t *operation,
    hk_external_link_op_progress_t *progress)
{
    return operation_call(owner, handle, operation, progress, 0U);
}

hk_result_t hk_external_link_cancel(
    hk_owner_t owner, const hk_external_link_t *handle,
    const hk_external_link_op_t *operation,
    hk_external_link_op_progress_t *progress)
{
    return operation_call(owner, handle, operation, progress, 1U);
}

hk_result_t hk_external_link_i2c_target_poll(
    hk_owner_t owner, const hk_external_link_t *handle,
    hk_buffer_view_t *rx, hk_external_link_target_event_t *event)
{
    hk_external_link_provider_t *provider;
    hk_result_t result;

    if(!event)
        return HK_ERR_INVALID_ARGUMENT;
    result = validate_view(rx, HK_BUFFER_ACCESS_WRITABLE, 256U, 1U);
    if(result != HK_OK)
        return result;
    result = provider_for(owner, handle, &provider);
    if(result == HK_OK)
        result = provider->target_poll(
            provider->context, &handle->lease, rx, event);
    return quarantine_internal(owner, handle, result);
}

hk_result_t hk_external_link_i2c_target_preload_response(
    hk_owner_t owner, const hk_external_link_t *handle,
    const hk_buffer_view_t *tx)
{
    hk_external_link_provider_t *provider;
    hk_result_t result = validate_view(
        tx, HK_BUFFER_ACCESS_READABLE, 256U, 1U);

    if(result != HK_OK)
        return result;
    result = provider_for(owner, handle, &provider);
    if(result == HK_OK)
        result = provider->target_preload(
            provider->context, &handle->lease, tx);
    return quarantine_internal(owner, handle, result);
}

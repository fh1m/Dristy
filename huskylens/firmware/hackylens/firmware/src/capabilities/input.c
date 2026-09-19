#include <hackylens/capability/input.h>

#include <limits.h>
#include <stddef.h>

#include "capability_core_binding.h"
#include "input_provider.h"

static hk_result_t input_provider_for(
    hk_owner_t owner,
    const hk_input_t *handle,
    hk_input_provider_t **provider)
{
    void *context = NULL;
    hk_result_t result;

    if(!handle || !provider)
        return HK_ERR_INVALID_ARGUMENT;
    result = capability_owner_runtime_validate(
        owner, &handle->lease, HK_CAPABILITY_ID_INPUT, &context);
    if(result != HK_OK)
        return result;
    *provider = (hk_input_provider_t *)context;
    if(!*provider || !(*provider)->open_cursor || !(*provider)->close_cursor ||
       !(*provider)->get_info || !(*provider)->get_state ||
       !(*provider)->next_event || (*provider)->reserved != 0U)
    {
        (void)capability_owner_runtime_quarantine(
            owner, &handle->lease, HK_CAPABILITY_ID_INPUT);
        return HK_ERR_INTERNAL;
    }
    return HK_OK;
}

static hk_result_t quarantine_internal(
    hk_owner_t owner, const hk_input_t *handle, hk_result_t result)
{
    if(result == HK_ERR_INTERNAL)
        (void)capability_owner_runtime_quarantine(
            owner, &handle->lease, HK_CAPABILITY_ID_INPUT);
    return result;
}

hk_result_t hk_input_acquire(
    hk_owner_t owner,
    const hk_capability_request_t *request,
    hk_input_t *handle)
{
    hk_input_provider_t *provider;
    hk_result_t result;

    if(!handle)
        return HK_ERR_INVALID_ARGUMENT;
    handle->lease = HK_LEASE_NONE;
    result = capability_owner_runtime_acquire(
        owner, request, HK_CAPABILITY_ID_INPUT, &handle->lease);
    if(result != HK_OK)
        return result;
    result = input_provider_for(owner, handle, &provider);
    if(result == HK_OK)
        result = provider->open_cursor(provider->context, &handle->lease);
    if(result != HK_OK)
    {
        (void)capability_owner_runtime_release(
            owner, HK_CAPABILITY_ID_INPUT, HK_DEADLINE_IMMEDIATE,
            &handle->lease);
        return result;
    }
    return HK_OK;
}

hk_result_t hk_input_release(
    hk_owner_t owner,
    hk_deadline_t deadline,
    hk_input_t *handle)
{
    hk_input_provider_t *provider;
    hk_result_t result;

    if(!handle)
        return HK_ERR_INVALID_ARGUMENT;
    if(deadline.at_us == UINT64_MAX)
        return HK_ERR_INVALID_ARGUMENT;
    if(hk_lease_is_zero(&handle->lease))
        return HK_OK;
    result = input_provider_for(owner, handle, &provider);
    if(result != HK_OK)
        return result;
    result = provider->close_cursor(provider->context, &handle->lease);
    if(result != HK_OK)
        return quarantine_internal(owner, handle, result);
    return capability_owner_runtime_release(
        owner, HK_CAPABILITY_ID_INPUT, deadline, &handle->lease);
}

hk_result_t hk_input_get_info(
    hk_owner_t owner,
    const hk_input_t *handle,
    hk_input_info_t *info)
{
    hk_input_provider_t *provider;
    hk_result_t result;

    if(!info)
        return HK_ERR_INVALID_ARGUMENT;
    result = input_provider_for(owner, handle, &provider);
    if(result == HK_OK)
        result = provider->get_info(provider->context, info);
    return quarantine_internal(owner, handle, result);
}

hk_result_t hk_input_get_state(
    hk_owner_t owner,
    const hk_input_t *handle,
    uint32_t *state)
{
    hk_input_provider_t *provider;
    hk_result_t result;

    if(!state)
        return HK_ERR_INVALID_ARGUMENT;
    result = input_provider_for(owner, handle, &provider);
    if(result == HK_OK)
        result = provider->get_state(provider->context, state);
    return quarantine_internal(owner, handle, result);
}

hk_result_t hk_input_next_event(
    hk_owner_t owner,
    const hk_input_t *handle,
    hk_input_event_t *event)
{
    hk_input_provider_t *provider;
    hk_result_t result;

    if(!event)
        return HK_ERR_INVALID_ARGUMENT;
    result = input_provider_for(owner, handle, &provider);
    if(result == HK_OK)
        result = provider->next_event(
            provider->context, &handle->lease, event);
    return quarantine_internal(owner, handle, result);
}

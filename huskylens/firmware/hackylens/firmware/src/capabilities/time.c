#include <hackylens/capability/time.h>

#include <limits.h>
#include <stddef.h>

#include "time_provider.h"
#include "capability_core_binding.h"

static hk_result_t time_provider_for(
    hk_owner_t owner,
    const hk_time_t *handle,
    hk_time_provider_t **provider)
{
    void *context = NULL;
    hk_result_t result;

    if(!handle || !provider)
        return HK_ERR_INVALID_ARGUMENT;
    result = capability_owner_runtime_validate(
        owner, &handle->lease, HK_CAPABILITY_ID_TIME, &context);
    if(result != HK_OK)
        return result;
    *provider = (hk_time_provider_t *)context;
    if(!*provider || !(*provider)->now_us || !(*provider)->sleep_us ||
       (*provider)->max_sleep_us == 0U ||
       (*provider)->max_sleep_us > HK_TIME_MAX_SLEEP_US ||
       (*provider)->max_slice_us == 0U ||
       (*provider)->max_slice_us > HK_TIME_CANCEL_PROBE_MAX_US ||
       (*provider)->reserved != 0U)
    {
        (void)capability_owner_runtime_quarantine(
            owner, &handle->lease, HK_CAPABILITY_ID_TIME);
        return HK_ERR_INTERNAL;
    }
    return HK_OK;
}

static hk_result_t time_now(
    hk_owner_t owner,
    const hk_time_t *handle,
    hk_time_provider_t **provider,
    uint64_t *value)
{
    hk_result_t result = time_provider_for(owner, handle, provider);

    if(result != HK_OK)
        return result;
    result = (*provider)->now_us((*provider)->context, value);
    if(result == HK_ERR_INTERNAL)
        (void)capability_owner_runtime_quarantine(
            owner, &handle->lease, HK_CAPABILITY_ID_TIME);
    return result;
}

static uint8_t cancelled(const hk_cancel_t *cancel)
{
    return (uint8_t)(cancel && cancel->probe &&
                     cancel->probe(cancel->context));
}

hk_result_t hk_time_acquire(
    hk_owner_t owner,
    const hk_capability_request_t *request,
    hk_time_t *handle)
{
    if(!handle)
        return HK_ERR_INVALID_ARGUMENT;
    handle->lease = HK_LEASE_NONE;
    return capability_owner_runtime_acquire(
        owner, request, HK_CAPABILITY_ID_TIME, &handle->lease);
}

hk_result_t hk_time_release(
    hk_owner_t owner,
    hk_deadline_t deadline,
    hk_time_t *handle)
{
    if(!handle)
        return HK_ERR_INVALID_ARGUMENT;
    return capability_owner_runtime_release(
        owner, HK_CAPABILITY_ID_TIME, deadline, &handle->lease);
}

hk_result_t hk_time_now_us(
    hk_owner_t owner,
    const hk_time_t *handle,
    uint64_t *value)
{
    hk_time_provider_t *provider;

    if(!value)
        return HK_ERR_INVALID_ARGUMENT;
    return time_now(owner, handle, &provider, value);
}

hk_result_t hk_time_deadline_after_us(
    hk_owner_t owner,
    const hk_time_t *handle,
    uint64_t duration_us,
    hk_deadline_t *deadline)
{
    hk_time_provider_t *provider;
    uint64_t now;
    hk_result_t result;

    if(!deadline)
        return HK_ERR_INVALID_ARGUMENT;
    deadline->at_us = 0U;
    result = time_now(owner, handle, &provider, &now);
    if(result != HK_OK)
        return result;
    if(duration_us > provider->max_sleep_us ||
       duration_us >= UINT64_MAX - now)
        return HK_ERR_LIMIT;
    deadline->at_us = now + duration_us;
    return HK_OK;
}

hk_result_t hk_time_sleep_until(
    hk_owner_t owner,
    const hk_time_t *handle,
    hk_deadline_t wake_target,
    hk_deadline_t operation_deadline,
    const hk_cancel_t *cancel)
{
    hk_time_provider_t *provider;
    uint64_t now;
    hk_result_t result;

    if(wake_target.at_us == UINT64_MAX ||
       operation_deadline.at_us == UINT64_MAX)
        return HK_ERR_INVALID_ARGUMENT;
    result = time_now(owner, handle, &provider, &now);
    if(result != HK_OK)
        return result;
    if(now >= wake_target.at_us)
        return HK_OK;
    if(cancelled(cancel))
        return HK_ERR_CANCELLED;
    if(operation_deadline.at_us == 0U || now >= operation_deadline.at_us)
        return HK_ERR_DEADLINE_EXCEEDED;
    if(wake_target.at_us - now > provider->max_sleep_us ||
       operation_deadline.at_us - now > provider->max_sleep_us)
        return HK_ERR_LIMIT;

    while(1)
    {
        uint64_t stop_at = wake_target.at_us < operation_deadline.at_us ?
                           wake_target.at_us : operation_deadline.at_us;
        uint64_t slice_us = stop_at - now;
        uint64_t previous = now;

        if(slice_us > provider->max_slice_us)
            slice_us = provider->max_slice_us;
        result = provider->sleep_us(provider->context, slice_us);
        if(result != HK_OK)
        {
            if(result == HK_ERR_INTERNAL)
                (void)capability_owner_runtime_quarantine(
                    owner, &handle->lease, HK_CAPABILITY_ID_TIME);
            return result;
        }
        result = time_now(owner, handle, &provider, &now);
        if(result != HK_OK)
            return result;
        if(now <= previous)
        {
            (void)capability_owner_runtime_quarantine(
                owner, &handle->lease, HK_CAPABILITY_ID_TIME);
            return HK_ERR_INTERNAL;
        }
        if(now >= wake_target.at_us)
            return HK_OK;
        if(cancelled(cancel))
            return HK_ERR_CANCELLED;
        if(now >= operation_deadline.at_us)
            return HK_ERR_DEADLINE_EXCEEDED;
    }
}

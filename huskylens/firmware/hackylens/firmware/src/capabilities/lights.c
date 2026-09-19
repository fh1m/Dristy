#include <hackylens/capability/lights.h>

#include <limits.h>
#include <stddef.h>

#include "capability_core_binding.h"
#include "lights_provider.h"

static hk_result_t lights_provider_for(
    hk_owner_t owner,
    const hk_lights_t *handle,
    hk_lights_provider_t **provider)
{
    void *context = NULL;
    hk_result_t result;

    if(!handle || !provider)
        return HK_ERR_INVALID_ARGUMENT;
    result = capability_owner_runtime_validate(
        owner, &handle->lease, HK_CAPABILITY_ID_LIGHTS, &context);
    if(result != HK_OK)
        return result;
    *provider = (hk_lights_provider_t *)context;
    if(!*provider || !(*provider)->open_channels ||
       !(*provider)->close_channels || !(*provider)->get_info ||
       !(*provider)->set_level || !(*provider)->set_rgb ||
       (*provider)->reserved != 0U)
    {
        (void)capability_owner_runtime_quarantine(
            owner, &handle->lease, HK_CAPABILITY_ID_LIGHTS);
        return HK_ERR_INTERNAL;
    }
    return HK_OK;
}

static hk_result_t quarantine_internal(
    hk_owner_t owner, const hk_lights_t *handle, hk_result_t result)
{
    if(result == HK_ERR_INTERNAL)
        (void)capability_owner_runtime_quarantine(
            owner, &handle->lease, HK_CAPABILITY_ID_LIGHTS);
    return result;
}

hk_result_t hk_lights_acquire(
    hk_owner_t owner,
    const hk_capability_request_t *request,
    uint32_t channels,
    hk_lights_t *handle)
{
    hk_capability_request_t channel_request;
    hk_lights_provider_t *provider;
    hk_result_t result;
    uint64_t channel_features = 0U;

    if(!request || request->struct_size < sizeof(*request) || !handle ||
       channels == 0U ||
       (channels & ~HK_LIGHTS_CHANNEL_ALL) != 0U)
        return HK_ERR_INVALID_ARGUMENT;
    handle->lease = HK_LEASE_NONE;
    if(channels & HK_LIGHTS_CHANNEL_BACKLIGHT)
        channel_features |= HK_LIGHTS_FEATURE_BACKLIGHT;
    if(channels & HK_LIGHTS_CHANNEL_ILLUMINATION)
        channel_features |= HK_LIGHTS_FEATURE_ILLUMINATION;
    if(channels & HK_LIGHTS_CHANNEL_RGB)
        channel_features |= HK_LIGHTS_FEATURE_RGB;
    channel_request = *request;
    channel_request.required_features |= channel_features;
    result = capability_owner_runtime_acquire(
        owner, &channel_request, HK_CAPABILITY_ID_LIGHTS, &handle->lease);
    if(result != HK_OK)
        return result;
    result = lights_provider_for(owner, handle, &provider);
    if(result == HK_OK)
        result = provider->open_channels(
            provider->context, &handle->lease, channels);
    if(result != HK_OK)
    {
        (void)capability_owner_runtime_release(
            owner, HK_CAPABILITY_ID_LIGHTS, HK_DEADLINE_IMMEDIATE,
            &handle->lease);
        return result;
    }
    return HK_OK;
}

hk_result_t hk_lights_release(
    hk_owner_t owner,
    hk_deadline_t deadline,
    hk_lights_t *handle)
{
    hk_lights_provider_t *provider;
    hk_result_t result;

    if(!handle || deadline.at_us == UINT64_MAX)
        return HK_ERR_INVALID_ARGUMENT;
    if(hk_lease_is_zero(&handle->lease))
        return HK_OK;
    result = lights_provider_for(owner, handle, &provider);
    if(result != HK_OK)
        return result;
    result = provider->close_channels(
        provider->context, &handle->lease, deadline);
    if(result != HK_OK)
        return quarantine_internal(owner, handle, result);
    return capability_owner_runtime_release(
        owner, HK_CAPABILITY_ID_LIGHTS, deadline, &handle->lease);
}

hk_result_t hk_lights_get_info(
    hk_owner_t owner,
    const hk_lights_t *handle,
    hk_lights_info_t *info)
{
    hk_lights_provider_t *provider;
    hk_result_t result;

    if(!info)
        return HK_ERR_INVALID_ARGUMENT;
    result = lights_provider_for(owner, handle, &provider);
    if(result == HK_OK)
        result = provider->get_info(provider->context, info);
    return quarantine_internal(owner, handle, result);
}

hk_result_t hk_lights_set_level(
    hk_owner_t owner,
    const hk_lights_t *handle,
    uint32_t channel,
    uint16_t level,
    hk_deadline_t deadline,
    const hk_cancel_t *cancel)
{
    hk_lights_provider_t *provider;
    hk_result_t result;

    if((channel != HK_LIGHTS_CHANNEL_BACKLIGHT &&
        channel != HK_LIGHTS_CHANNEL_ILLUMINATION) ||
       level > HK_LIGHTS_LEVEL_MAX || deadline.at_us == UINT64_MAX)
        return HK_ERR_INVALID_ARGUMENT;
    result = lights_provider_for(owner, handle, &provider);
    if(result == HK_OK)
        result = provider->set_level(
            provider->context, &handle->lease, channel, level,
            deadline, cancel);
    return quarantine_internal(owner, handle, result);
}

hk_result_t hk_lights_set_rgb(
    hk_owner_t owner,
    const hk_lights_t *handle,
    uint16_t red,
    uint16_t green,
    uint16_t blue,
    hk_deadline_t deadline,
    const hk_cancel_t *cancel)
{
    hk_lights_provider_t *provider;
    hk_result_t result;

    if(red > HK_LIGHTS_LEVEL_MAX || green > HK_LIGHTS_LEVEL_MAX ||
       blue > HK_LIGHTS_LEVEL_MAX || deadline.at_us == UINT64_MAX)
        return HK_ERR_INVALID_ARGUMENT;
    result = lights_provider_for(owner, handle, &provider);
    if(result == HK_OK)
        result = provider->set_rgb(
            provider->context, &handle->lease, red, green, blue,
            deadline, cancel);
    return quarantine_internal(owner, handle, result);
}

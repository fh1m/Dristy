#include <hackylens/capability/display.h>

#include <limits.h>
#include <stddef.h>

#include "capability_core_binding.h"
#include "display_provider.h"
#include "display_stage_private.h"

static hk_result_t display_provider_for(
    hk_owner_t owner, const hk_display_t *handle,
    hk_display_provider_t **provider)
{
    void *context = NULL;
    hk_result_t result;

    if(!handle || !provider)
        return HK_ERR_INVALID_ARGUMENT;
    result = capability_owner_runtime_validate(
        owner, &handle->lease, HK_CAPABILITY_ID_DISPLAY, &context);
    if(result != HK_OK)
        return result;
    *provider = (hk_display_provider_t *)context;
    if(!*provider || !(*provider)->open_plane || !(*provider)->close_plane ||
       !(*provider)->get_info || !(*provider)->begin_batch ||
       !(*provider)->set_clip || !(*provider)->clear ||
       !(*provider)->fill_rect || !(*provider)->stroke_rect ||
       !(*provider)->text || !(*provider)->blit ||
       !(*provider)->mark_dirty || !(*provider)->surface_acquire ||
       !(*provider)->present || !(*provider)->abort ||
       !(*provider)->stage_checkpoint || !(*provider)->stage_restore ||
       !(*provider)->stage_keep_last_clear || (*provider)->reserved != 0U)
    {
        (void)capability_owner_runtime_quarantine(
            owner, &handle->lease, HK_CAPABILITY_ID_DISPLAY);
        return HK_ERR_INTERNAL;
    }
    return HK_OK;
}

static hk_result_t quarantine_internal(
    hk_owner_t owner, const hk_display_t *handle, hk_result_t result)
{
    if(result == HK_ERR_INTERNAL)
        (void)capability_owner_runtime_quarantine(
            owner, &handle->lease, HK_CAPABILITY_ID_DISPLAY);
    return result;
}

hk_result_t hk_display_acquire(
    hk_owner_t owner, const hk_capability_request_t *request,
    uint32_t plane, hk_display_t *handle)
{
    hk_capability_request_t plane_request;
    hk_display_provider_t *provider;
    hk_result_t result;

    if(!request || !handle ||
       (plane != HK_DISPLAY_PLANE_BASE &&
        plane != HK_DISPLAY_PLANE_OVERLAY))
        return HK_ERR_INVALID_ARGUMENT;
    handle->lease = HK_LEASE_NONE;
    plane_request = *request;
    plane_request.required_features |= plane == HK_DISPLAY_PLANE_BASE ?
        HK_DISPLAY_FEATURE_BASE_PLANE : HK_DISPLAY_FEATURE_OVERLAY_PLANE;
    result = capability_owner_runtime_acquire(
        owner, &plane_request, HK_CAPABILITY_ID_DISPLAY, &handle->lease);
    if(result != HK_OK)
        return result;
    result = display_provider_for(owner, handle, &provider);
    if(result == HK_OK)
        result = provider->open_plane(
            provider->context, &handle->lease, plane);
    if(result != HK_OK)
    {
        (void)capability_owner_runtime_release(
            owner, HK_CAPABILITY_ID_DISPLAY, HK_DEADLINE_IMMEDIATE,
            &handle->lease);
        return result;
    }
    return HK_OK;
}

hk_result_t hk_display_release(
    hk_owner_t owner, hk_deadline_t deadline, hk_display_t *handle)
{
    hk_display_provider_t *provider;
    hk_result_t result;

    if(!handle || deadline.at_us == UINT64_MAX)
        return HK_ERR_INVALID_ARGUMENT;
    if(hk_lease_is_zero(&handle->lease))
        return HK_OK;
    result = display_provider_for(owner, handle, &provider);
    if(result != HK_OK)
        return result;
    result = provider->close_plane(
        provider->context, &handle->lease, deadline);
    if(result != HK_OK)
        return quarantine_internal(owner, handle, result);
    return capability_owner_runtime_release(
        owner, HK_CAPABILITY_ID_DISPLAY, deadline, &handle->lease);
}

hk_result_t hk_display_get_info(
    hk_owner_t owner, const hk_display_t *handle, hk_display_info_t *info)
{
    hk_display_provider_t *provider;
    hk_result_t result;
    if(!info)
        return HK_ERR_INVALID_ARGUMENT;
    result = display_provider_for(owner, handle, &provider);
    if(result == HK_OK)
        result = provider->get_info(provider->context, info);
    return quarantine_internal(owner, handle, result);
}

#define DISPLAY_CALL0(name)                                                   \
    hk_result_t hk_display_##name(                                            \
        hk_owner_t owner, const hk_display_t *handle)                         \
    {                                                                          \
        hk_display_provider_t *provider;                                       \
        hk_result_t result = display_provider_for(owner, handle, &provider);   \
        if(result == HK_OK)                                                    \
            result = provider->name(provider->context, &handle->lease);        \
        return quarantine_internal(owner, handle, result);                     \
    }

DISPLAY_CALL0(begin_batch)
DISPLAY_CALL0(abort)

hk_result_t hk_display_set_clip(
    hk_owner_t owner, const hk_display_t *handle,
    const hk_display_rect_t *clip)
{
    hk_display_provider_t *provider;
    hk_result_t result = display_provider_for(owner, handle, &provider);
    if(result == HK_OK)
        result = provider->set_clip(provider->context, &handle->lease, clip);
    return quarantine_internal(owner, handle, result);
}

hk_result_t hk_display_clear(
    hk_owner_t owner, const hk_display_t *handle, uint16_t color)
{
    hk_display_provider_t *provider;
    hk_result_t result = display_provider_for(owner, handle, &provider);
    if(result == HK_OK)
        result = provider->clear(provider->context, &handle->lease, color);
    return quarantine_internal(owner, handle, result);
}

#define DISPLAY_RECT_CALL(name)                                               \
    hk_result_t hk_display_##name(                                            \
        hk_owner_t owner, const hk_display_t *handle,                         \
        const hk_display_rect_t *rect, uint16_t color)                        \
    {                                                                          \
        hk_display_provider_t *provider;                                       \
        hk_result_t result = display_provider_for(owner, handle, &provider);   \
        if(result == HK_OK)                                                    \
            result = provider->name(                                           \
                provider->context, &handle->lease, rect, color);               \
        return quarantine_internal(owner, handle, result);                     \
    }

DISPLAY_RECT_CALL(fill_rect)
DISPLAY_RECT_CALL(stroke_rect)

hk_result_t hk_display_text(
    hk_owner_t owner, const hk_display_t *handle,
    const hk_display_rect_t *bounds, const char *utf8,
    uint32_t size_bytes, uint16_t color)
{
    hk_display_provider_t *provider;
    hk_result_t result = display_provider_for(owner, handle, &provider);
    if(result == HK_OK)
        result = provider->text(
            provider->context, &handle->lease, bounds,
            utf8, size_bytes, color);
    return quarantine_internal(owner, handle, result);
}

hk_result_t hk_display_blit(
    hk_owner_t owner, const hk_display_t *handle,
    const hk_display_rect_t *destination, const hk_buffer_view_t *pixels,
    uint32_t pixel_format)
{
    hk_display_provider_t *provider;
    hk_result_t result = display_provider_for(owner, handle, &provider);
    if(result == HK_OK)
        result = provider->blit(
            provider->context, &handle->lease, destination,
            pixels, pixel_format);
    return quarantine_internal(owner, handle, result);
}

hk_result_t hk_display_mark_dirty(
    hk_owner_t owner, const hk_display_t *handle,
    const hk_display_rect_t *rect)
{
    hk_display_provider_t *provider;
    hk_result_t result = display_provider_for(owner, handle, &provider);
    if(result == HK_OK)
        result = provider->mark_dirty(
            provider->context, &handle->lease, rect);
    return quarantine_internal(owner, handle, result);
}

hk_result_t hk_display_surface_acquire(
    hk_owner_t owner, const hk_display_t *handle,
    hk_display_surface_t *surface)
{
    hk_display_provider_t *provider;
    hk_result_t result;
    if(!surface)
        return HK_ERR_INVALID_ARGUMENT;
    result = display_provider_for(owner, handle, &provider);
    if(result == HK_OK)
        result = provider->surface_acquire(
            provider->context, &handle->lease, surface);
    return quarantine_internal(owner, handle, result);
}

hk_result_t hk_display_present(
    hk_owner_t owner, const hk_display_t *handle,
    hk_deadline_t deadline, const hk_cancel_t *cancel)
{
    hk_display_provider_t *provider;
    hk_result_t result;
    if(deadline.at_us == UINT64_MAX)
        return HK_ERR_INVALID_ARGUMENT;
    result = display_provider_for(owner, handle, &provider);
    if(result == HK_OK)
        result = provider->present(
            provider->context, &handle->lease, deadline, cancel);
    return quarantine_internal(owner, handle, result);
}

hk_result_t hk_display_stage_checkpoint(
    hk_owner_t owner, const hk_display_t *handle,
    uint16_t *commands, uint16_t *text_bytes)
{
    hk_display_provider_t *provider;
    hk_result_t result;
    if(!commands || !text_bytes)
        return HK_ERR_INVALID_ARGUMENT;
    result = display_provider_for(owner, handle, &provider);
    if(result == HK_OK)
        result = provider->stage_checkpoint(
            provider->context, &handle->lease, commands, text_bytes);
    return quarantine_internal(owner, handle, result);
}

hk_result_t hk_display_stage_restore(
    hk_owner_t owner, const hk_display_t *handle,
    uint16_t commands, uint16_t text_bytes)
{
    hk_display_provider_t *provider;
    hk_result_t result = display_provider_for(owner, handle, &provider);
    if(result == HK_OK)
        result = provider->stage_restore(
            provider->context, &handle->lease, commands, text_bytes);
    return quarantine_internal(owner, handle, result);
}

hk_result_t hk_display_stage_keep_last_clear(
    hk_owner_t owner, const hk_display_t *handle)
{
    hk_display_provider_t *provider;
    hk_result_t result = display_provider_for(owner, handle, &provider);
    if(result == HK_OK)
        result = provider->stage_keep_last_clear(
            provider->context, &handle->lease);
    return quarantine_internal(owner, handle, result);
}

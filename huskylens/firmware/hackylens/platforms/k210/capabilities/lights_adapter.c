#include "../../../firmware/src/capabilities/capability_provider.h"
#include "../../../firmware/src/capabilities/lights_provider.h"
#include "../../../firmware/src/drivers/hk_lights.h"

#include <hackylens/capability/lights.h>

#include <stddef.h>

#include "../hal/hal_time.h"

#define K210_LIGHTS_MAX_LEASES 16U

typedef struct
{
    hk_lease_t lease;
    uint32_t channels;
    uint8_t active;
} k210_lights_lease_t;

typedef struct
{
    k210_lights_lease_t leases[K210_LIGHTS_MAX_LEASES];
    uint8_t prepared;
} k210_lights_state_t;

static k210_lights_state_t s_lights;

static uint8_t owner_equal(hk_owner_t left, hk_owner_t right)
{
    return (uint8_t)(left.slot == right.slot &&
                     left.generation == right.generation);
}

static uint8_t lease_equal(const hk_lease_t *left, const hk_lease_t *right)
{
    return (uint8_t)(left && right && left->slot == right->slot &&
                     left->generation == right->generation &&
                     left->capability_id == right->capability_id &&
                     owner_equal(left->owner, right->owner));
}

static void safe_off(uint32_t channels)
{
    if(channels & HK_LIGHTS_CHANNEL_BACKLIGHT)
        lights_screen_backlight_off();
    if(channels & HK_LIGHTS_CHANNEL_ILLUMINATION)
        lights_illum_set(0U, 0U);
    if(channels & HK_LIGHTS_CHANNEL_RGB)
        lights_rgb_set(0U, 0U, 0U, 0U);
}

static uint8_t deadline_expired(hk_deadline_t deadline)
{
    return (uint8_t)(deadline.at_us != 0U &&
                     hal_time_us() >= deadline.at_us);
}

static k210_lights_lease_t *find_lease(
    k210_lights_state_t *state, const hk_lease_t *lease)
{
    if(!state || !lease)
        return NULL;
    for(uint16_t index = 0U; index < K210_LIGHTS_MAX_LEASES; index++)
    {
        if(state->leases[index].active &&
           lease_equal(&state->leases[index].lease, lease))
            return &state->leases[index];
    }
    return NULL;
}

static hk_result_t k210_lights_open(
    void *context, const hk_lease_t *lease, uint32_t channels)
{
    k210_lights_state_t *state = (k210_lights_state_t *)context;
    k210_lights_lease_t *free_slot = NULL;

    if(!state || !lease || channels == 0U ||
       (channels & ~HK_LIGHTS_CHANNEL_ALL) != 0U)
        return HK_ERR_INVALID_ARGUMENT;
    for(uint16_t index = 0U; index < K210_LIGHTS_MAX_LEASES; index++)
    {
        k210_lights_lease_t *slot = &state->leases[index];

        if(!slot->active)
        {
            if(!free_slot)
                free_slot = slot;
            continue;
        }
        if((slot->channels & channels) != 0U)
            return HK_ERR_BUSY;
    }
    if(!free_slot)
        return HK_ERR_LIMIT;
    if(!state->prepared)
    {
        lights_driver_prepare();
        state->prepared = 1U;
    }
    free_slot->lease = *lease;
    free_slot->channels = channels;
    free_slot->active = 1U;
    return HK_OK;
}

static hk_result_t k210_lights_close(
    void *context, const hk_lease_t *lease, hk_deadline_t deadline)
{
    k210_lights_state_t *state = (k210_lights_state_t *)context;
    k210_lights_lease_t *slot = find_lease(state, lease);

    if(!slot)
        return HK_ERR_INTERNAL;
    if(deadline_expired(deadline))
        return HK_ERR_DEADLINE_EXCEEDED;
    safe_off(slot->channels);
    slot->lease = HK_LEASE_NONE;
    slot->channels = 0U;
    slot->active = 0U;
    return HK_OK;
}

static hk_result_t k210_lights_info(
    void *context, hk_lights_info_t *info)
{
    (void)context;
    if(!info)
        return HK_ERR_INVALID_ARGUMENT;
    *info = (hk_lights_info_t){
        sizeof(hk_lights_info_t), HK_LIGHTS_INFO_VERSION,
        HK_LIGHTS_CHANNEL_ALL, HK_LIGHTS_LEVEL_MAX, 0U,
    };
    return HK_OK;
}

static hk_result_t validate_write(
    k210_lights_state_t *state, const hk_lease_t *lease,
    uint32_t channels, hk_deadline_t deadline, const hk_cancel_t *cancel)
{
    k210_lights_lease_t *slot = find_lease(state, lease);

    if(!slot)
        return HK_ERR_INTERNAL;
    if((slot->channels & channels) != channels)
        return HK_ERR_WRONG_OWNER;
    if(cancel && cancel->probe && cancel->probe(cancel->context))
        return HK_ERR_CANCELLED;
    if(deadline.at_us != 0U && hal_time_us() >= deadline.at_us)
        return HK_ERR_DEADLINE_EXCEEDED;
    return HK_OK;
}

static uint8_t percent(uint16_t level)
{
    return (uint8_t)((level + 5U) / 10U);
}

static hk_result_t k210_lights_set_level(
    void *context, const hk_lease_t *lease, uint32_t channel,
    uint16_t level, hk_deadline_t deadline, const hk_cancel_t *cancel)
{
    k210_lights_state_t *state = (k210_lights_state_t *)context;
    hk_result_t result = validate_write(
        state, lease, channel, deadline, cancel);

    if(result != HK_OK)
        return result;
    if(channel == HK_LIGHTS_CHANNEL_BACKLIGHT)
    {
        if(level == 0U)
            lights_screen_backlight_off();
        else
            lights_screen_backlight_set(percent(level));
        return HK_OK;
    }
    if(channel == HK_LIGHTS_CHANNEL_ILLUMINATION)
    {
        lights_illum_set(level != 0U, percent(level));
        return HK_OK;
    }
    return HK_ERR_INVALID_ARGUMENT;
}

static hk_result_t k210_lights_set_rgb(
    void *context, const hk_lease_t *lease, uint16_t red,
    uint16_t green, uint16_t blue, hk_deadline_t deadline,
    const hk_cancel_t *cancel)
{
    k210_lights_state_t *state = (k210_lights_state_t *)context;
    hk_result_t result = validate_write(
        state, lease, HK_LIGHTS_CHANNEL_RGB, deadline, cancel);

    if(result != HK_OK)
        return result;
    lights_rgb_set((red | green | blue) != 0U,
                   percent(red), percent(green), percent(blue));
    return HK_OK;
}

static hk_result_t k210_lights_cleanup(
    void *context, hk_owner_t owner, hk_deadline_t deadline)
{
    hk_lights_provider_t *provider = (hk_lights_provider_t *)context;
    k210_lights_state_t *state;

    if(!provider || !provider->context)
        return HK_ERR_INTERNAL;
    state = (k210_lights_state_t *)provider->context;
    for(uint16_t index = 0U; index < K210_LIGHTS_MAX_LEASES; index++)
    {
        k210_lights_lease_t *slot = &state->leases[index];

        if(slot->active && owner_equal(slot->lease.owner, owner))
        {
            if(deadline_expired(deadline))
                return HK_ERR_DEADLINE_EXCEEDED;
            break;
        }
    }
    for(uint16_t index = 0U; index < K210_LIGHTS_MAX_LEASES; index++)
    {
        k210_lights_lease_t *slot = &state->leases[index];

        if(!slot->active || !owner_equal(slot->lease.owner, owner))
            continue;
        safe_off(slot->channels);
        slot->lease = HK_LEASE_NONE;
        slot->channels = 0U;
        slot->active = 0U;
    }
    return HK_OK;
}

static hk_result_t k210_lights_cleanup_lease(
    void *context, const hk_lease_t *lease, hk_deadline_t deadline)
{
    hk_lights_provider_t *provider = (hk_lights_provider_t *)context;
    k210_lights_state_t *state;

    if(!provider || !provider->context)
        return HK_ERR_INTERNAL;
    state = (k210_lights_state_t *)provider->context;
    if(!find_lease(state, lease))
        return HK_OK;
    return k210_lights_close(state, lease, deadline);
}

static hk_result_t k210_lights_cleanup_dispatch(
    void *context, hk_owner_t owner, uint16_t target_core,
    hk_deadline_t deadline)
{
    if(target_core != 0U)
        return HK_ERR_WRONG_CONTEXT;
    return k210_lights_cleanup(context, owner, deadline);
}

static hk_lights_provider_t s_lights_provider = {
    .context = &s_lights,
    .open_channels = k210_lights_open,
    .close_channels = k210_lights_close,
    .get_info = k210_lights_info,
    .set_level = k210_lights_set_level,
    .set_rgb = k210_lights_set_rgb,
};

const hk_capability_provider_t hk_k210_lights_provider = {
    .context = &s_lights_provider,
    .cleanup_lease = k210_lights_cleanup_lease,
    .cleanup = k210_lights_cleanup,
    .cleanup_dispatch = k210_lights_cleanup_dispatch,
    .max_leases = 16U,
};

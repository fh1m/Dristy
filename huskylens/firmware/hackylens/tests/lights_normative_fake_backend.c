#include "lights_normative_backend.h"

#include <hackylens/capability/lights.h>

#include <string.h>

#include "../firmware/src/capabilities/lights_provider.h"

typedef struct
{
    hk_lease_t lease;
    uint32_t channels;
    uint8_t active;
} fake_slot_t;

typedef struct
{
    fake_slot_t slots[8];
    uint64_t now_us;
    uint32_t effect_count;
    uint32_t active_mask;
    uint32_t safe_off_mask;
} fake_lights_t;

static fake_lights_t s_fake;

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

static fake_slot_t *fake_find(const hk_lease_t *lease)
{
    for(uint16_t index = 0U; index < 8U; index++)
    {
        if(s_fake.slots[index].active &&
           lease_equal(&s_fake.slots[index].lease, lease))
            return &s_fake.slots[index];
    }
    return NULL;
}

static void fake_safe_off(uint32_t channels)
{
    s_fake.safe_off_mask |= channels;
    s_fake.active_mask &= ~channels;
    s_fake.effect_count++;
}

static hk_result_t fake_open(
    void *context, const hk_lease_t *lease, uint32_t channels)
{
    fake_slot_t *free_slot = NULL;

    (void)context;
    for(uint16_t index = 0U; index < 8U; index++)
    {
        fake_slot_t *slot = &s_fake.slots[index];

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
    free_slot->lease = *lease;
    free_slot->channels = channels;
    free_slot->active = 1U;
    return HK_OK;
}

static hk_result_t fake_close(
    void *context, const hk_lease_t *lease, hk_deadline_t deadline)
{
    fake_slot_t *slot;

    (void)context;
    slot = fake_find(lease);
    if(!slot)
        return HK_ERR_INTERNAL;
    if(deadline.at_us != 0U && s_fake.now_us >= deadline.at_us)
        return HK_ERR_DEADLINE_EXCEEDED;
    fake_safe_off(slot->channels);
    memset(slot, 0, sizeof(*slot));
    return HK_OK;
}

static hk_result_t fake_info(void *context, hk_lights_info_t *info)
{
    (void)context;
    if(!info)
        return HK_ERR_INVALID_ARGUMENT;
    *info = (hk_lights_info_t){
        sizeof(*info), HK_LIGHTS_INFO_VERSION, HK_LIGHTS_CHANNEL_ALL,
        HK_LIGHTS_LEVEL_MAX, 0U,
    };
    return HK_OK;
}

static hk_result_t fake_validate_write(
    const hk_lease_t *lease, uint32_t channels, hk_deadline_t deadline,
    const hk_cancel_t *cancel)
{
    fake_slot_t *slot = fake_find(lease);

    if(!slot)
        return HK_ERR_INTERNAL;
    if((slot->channels & channels) != channels)
        return HK_ERR_WRONG_OWNER;
    if(cancel && cancel->probe && cancel->probe(cancel->context))
        return HK_ERR_CANCELLED;
    if(deadline.at_us != 0U && s_fake.now_us >= deadline.at_us)
        return HK_ERR_DEADLINE_EXCEEDED;
    return HK_OK;
}

static hk_result_t fake_level(
    void *context, const hk_lease_t *lease, uint32_t channel,
    uint16_t level, hk_deadline_t deadline, const hk_cancel_t *cancel)
{
    hk_result_t result;

    (void)context;
    result = fake_validate_write(lease, channel, deadline, cancel);
    if(result != HK_OK)
        return result;
    if(channel != HK_LIGHTS_CHANNEL_BACKLIGHT &&
       channel != HK_LIGHTS_CHANNEL_ILLUMINATION)
        return HK_ERR_INVALID_ARGUMENT;
    if(level != 0U)
        s_fake.active_mask |= channel;
    else
        s_fake.active_mask &= ~channel;
    s_fake.effect_count++;
    return HK_OK;
}

static hk_result_t fake_rgb(
    void *context, const hk_lease_t *lease, uint16_t red,
    uint16_t green, uint16_t blue, hk_deadline_t deadline,
    const hk_cancel_t *cancel)
{
    hk_result_t result;

    (void)context;
    result = fake_validate_write(
        lease, HK_LIGHTS_CHANNEL_RGB, deadline, cancel);
    if(result != HK_OK)
        return result;
    if((red | green | blue) != 0U)
        s_fake.active_mask |= HK_LIGHTS_CHANNEL_RGB;
    else
        s_fake.active_mask &= ~HK_LIGHTS_CHANNEL_RGB;
    s_fake.effect_count++;
    return HK_OK;
}

static hk_lights_provider_t s_lights_provider = {
    .context = &s_fake,
    .open_channels = fake_open,
    .close_channels = fake_close,
    .get_info = fake_info,
    .set_level = fake_level,
    .set_rgb = fake_rgb,
};

static hk_result_t fake_cleanup(
    void *context, hk_owner_t owner, hk_deadline_t deadline)
{
    (void)context;
    for(uint16_t index = 0U; index < 8U; index++)
    {
        fake_slot_t *slot = &s_fake.slots[index];

        if(slot->active && owner_equal(slot->lease.owner, owner) &&
           deadline.at_us != 0U && s_fake.now_us >= deadline.at_us)
            return HK_ERR_DEADLINE_EXCEEDED;
    }
    for(uint16_t index = 0U; index < 8U; index++)
    {
        fake_slot_t *slot = &s_fake.slots[index];

        if(!slot->active || !owner_equal(slot->lease.owner, owner))
            continue;
        fake_safe_off(slot->channels);
        memset(slot, 0, sizeof(*slot));
    }
    return HK_OK;
}

static hk_result_t fake_cleanup_lease(
    void *context, const hk_lease_t *lease, hk_deadline_t deadline)
{
    if(!fake_find(lease))
        return HK_OK;
    return fake_close(context, lease, deadline);
}

static hk_result_t fake_cleanup_dispatch(
    void *context, hk_owner_t owner, uint16_t target_core,
    hk_deadline_t deadline)
{
    return target_core == 0U ? fake_cleanup(context, owner, deadline) :
                              HK_ERR_WRONG_CONTEXT;
}

static const hk_capability_provider_t s_provider = {
    .context = &s_lights_provider,
    .cleanup_lease = fake_cleanup_lease,
    .cleanup = fake_cleanup,
    .cleanup_dispatch = fake_cleanup_dispatch,
    .max_leases = 8U,
};

const hk_capability_provider_t *lights_normative_backend_provider(void)
{
    return &s_provider;
}

const char *lights_normative_backend_name(void)
{
    return "fake";
}

void lights_normative_backend_reset(uint64_t now_us)
{
    memset(&s_fake, 0, sizeof(s_fake));
    s_fake.now_us = now_us;
}

void lights_normative_backend_set_now(uint64_t now_us)
{
    s_fake.now_us = now_us;
}

uint32_t lights_normative_backend_effect_count(void)
{
    return s_fake.effect_count;
}

uint32_t lights_normative_backend_active_mask(void)
{
    return s_fake.active_mask;
}

uint32_t lights_normative_backend_safe_off_mask(void)
{
    return s_fake.safe_off_mask;
}

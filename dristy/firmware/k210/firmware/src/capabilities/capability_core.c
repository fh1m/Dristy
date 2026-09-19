#include "capability_provider.h"

#include <limits.h>
#include <string.h>

static int version_compare(hk_version_t left, hk_version_t right)
{
    if(left.major != right.major)
        return left.major < right.major ? -1 : 1;
    if(left.minor != right.minor)
        return left.minor < right.minor ? -1 : 1;
    if(left.patch != right.patch)
        return left.patch < right.patch ? -1 : 1;
    return 0;
}

static uint8_t version_is_valid(hk_version_t version)
{
    return (uint8_t)(version.reserved == 0U);
}

static uint8_t deadline_is_valid(hk_deadline_t deadline)
{
    return (uint8_t)(deadline.at_us != UINT64_MAX);
}

static uint8_t owner_equal(hk_owner_t left, hk_owner_t right)
{
    return (uint8_t)(left.slot == right.slot && left.generation == right.generation);
}

static hk_result_t validate_owner(
    hk_capability_core_t *core,
    hk_owner_t owner,
    hk_capability_owner_slot_t **slot)
{
    hk_capability_owner_slot_t *candidate;

    if(!core || !core->initialized)
        return HK_ERR_INVALID_STATE;
    if(owner.slot >= HK_CAPABILITY_MAX_OWNERS || owner.generation == 0U)
        return HK_ERR_STALE_HANDLE;
    candidate = &core->owners[owner.slot];
    if(!candidate->active || candidate->generation != owner.generation)
        return HK_ERR_STALE_HANDLE;
    if(slot)
        *slot = candidate;
    return HK_OK;
}

static int provider_compare(
    const hk_capability_info_t *info,
    hk_capability_id_t id,
    uint16_t instance)
{
    if(info->id != id)
        return info->id < id ? -1 : 1;
    if(info->instance != instance)
        return info->instance < instance ? -1 : 1;
    return 0;
}

static int find_provider(
    const hk_capability_core_t *core,
    hk_capability_id_t id,
    uint16_t instance)
{
    uint16_t index;

    for(index = 0U; index < core->provider_count; index++)
    {
        int comparison = provider_compare(&core->inventory[index], id, instance);
        if(comparison == 0)
            return (int)index;
        if(comparison > 0)
            break;
    }
    return -1;
}

static uint8_t affinity_matches(
    const hk_capability_info_t *info,
    uint16_t current_core)
{
    return (uint8_t)(info->affinity_core == HK_CAPABILITY_CORE_ANY ||
                     info->affinity_core == current_core);
}

static uint8_t grant_allows(
    const hk_capability_owner_slot_t *owner,
    const hk_capability_request_t *request)
{
    uint16_t index;

    for(index = 0U; index < owner->grant_count; index++)
    {
        const hk_capability_request_t *grant = &owner->grants[index].request;
        if(grant->id != request->id || grant->instance != request->instance)
            continue;
        if(version_compare(request->minimum, grant->minimum) < 0 ||
           version_compare(request->maximum_exclusive,
                           grant->maximum_exclusive) > 0)
            continue;
        if((request->required_features & ~grant->required_features) != 0U)
            continue;
        return 1U;
    }
    return 0U;
}

static hk_result_t validate_request(const hk_capability_request_t *request)
{
    if(!request || request->struct_size < sizeof(*request) ||
       request->struct_version != HK_CAPABILITY_REQUEST_VERSION ||
       request->id == 0U || request->reserved != 0U ||
       !version_is_valid(request->minimum) ||
       !version_is_valid(request->maximum_exclusive) ||
       version_compare(request->minimum, request->maximum_exclusive) >= 0)
        return HK_ERR_INVALID_ARGUMENT;
    return HK_OK;
}

static void retire_or_advance_owner(hk_capability_owner_slot_t *slot)
{
    slot->active = 0U;
    slot->grants = NULL;
    slot->grant_count = 0U;
    if(slot->generation == UINT32_MAX)
        slot->retired = 1U;
    else
        slot->generation++;
}

static void retire_or_advance_lease(hk_capability_lease_slot_t *slot)
{
    slot->active = 0U;
    slot->owner = HK_OWNER_NONE;
    slot->capability_id = 0U;
    slot->provider_index = 0U;
    if(slot->generation == UINT32_MAX)
        slot->retired = 1U;
    else
        slot->generation++;
}

static hk_result_t cleanup_lease(
    hk_capability_core_t *core,
    hk_capability_lease_slot_t *slot,
    hk_deadline_t deadline)
{
    uint16_t provider_index = slot->provider_index;
    hk_lease_t lease = {
        (uint32_t)(slot - core->leases), slot->generation, slot->owner,
        slot->capability_id,
    };
    hk_result_t result = HK_OK;

    if(core->providers[provider_index]->cleanup_lease)
        result = core->providers[provider_index]->cleanup_lease(
            core->providers[provider_index]->context, &lease, deadline);
    else if(core->providers[provider_index]->cleanup)
        result = core->providers[provider_index]->cleanup(
            core->providers[provider_index]->context, slot->owner, deadline);
    if(core->provider_state[provider_index].active_leases > 0U)
        core->provider_state[provider_index].active_leases--;
    retire_or_advance_lease(slot);
    if(result != HK_OK)
    {
        core->provider_state[provider_index].quarantined = 1U;
        return HK_ERR_INTERNAL;
    }
    return HK_OK;
}

static hk_result_t cleanup_lease_for_owner_close(
    hk_capability_core_t *core,
    hk_capability_lease_slot_t *slot,
    uint16_t current_core,
    hk_deadline_t deadline)
{
    uint16_t provider_index = slot->provider_index;
    const hk_capability_info_t *info = &core->inventory[provider_index];
    const hk_capability_provider_t *provider = core->providers[provider_index];
    hk_result_t result = HK_OK;

    if(provider->cleanup)
    {
        if(affinity_matches(info, current_core))
            result = provider->cleanup(provider->context, slot->owner, deadline);
        else
            result = provider->cleanup_dispatch(
                provider->context, slot->owner, info->affinity_core, deadline);
    }
    if(core->provider_state[provider_index].active_leases > 0U)
        core->provider_state[provider_index].active_leases--;
    retire_or_advance_lease(slot);
    if(result != HK_OK)
    {
        core->provider_state[provider_index].quarantined = 1U;
        return HK_ERR_INTERNAL;
    }
    return HK_OK;
}

hk_result_t hk_capability_core_init(
    hk_capability_core_t *core,
    const hk_capability_info_t *inventory,
    const hk_capability_provider_t *const *providers,
    uint16_t provider_count)
{
    uint16_t index;

    if(!core || provider_count > HK_CAPABILITY_MAX_PROVIDERS ||
       (provider_count > 0U && (!inventory || !providers)))
        return HK_ERR_INVALID_ARGUMENT;
    memset(core, 0, sizeof(*core));
    for(index = 0U; index < provider_count; index++)
    {
        const hk_capability_info_t *info = &inventory[index];
        uint32_t sharing = info->flags &
            (HK_CAPABILITY_FLAG_SHARED | HK_CAPABILITY_FLAG_EXCLUSIVE);
        uint16_t limit_index;
        if(info->struct_size < sizeof(*info) ||
           info->struct_version != HK_CAPABILITY_INFO_VERSION ||
           info->id == 0U || info->reserved != 0U ||
           !version_is_valid(info->version) ||
           (info->limit_count > 0U && !info->limits) ||
           (sharing != HK_CAPABILITY_FLAG_SHARED &&
            sharing != HK_CAPABILITY_FLAG_EXCLUSIVE) ||
           !providers[index] ||
           providers[index]->max_leases == 0U ||
           (info->affinity_core != HK_CAPABILITY_CORE_ANY &&
            providers[index]->cleanup &&
            !providers[index]->cleanup_dispatch) ||
           providers[index]->reserved != 0U ||
           (index > 0U && provider_compare(
               &inventory[index - 1U], info->id, info->instance) >= 0))
            return HK_ERR_INVALID_ARGUMENT;
        for(limit_index = 0U; limit_index < info->limit_count; limit_index++)
        {
            const hk_capability_limit_t *limit = &info->limits[limit_index];
            if(limit->struct_size < sizeof(*limit) ||
               limit->struct_version != HK_CAPABILITY_LIMIT_VERSION ||
               limit->key == 0U ||
               (limit_index > 0U &&
                info->limits[limit_index - 1U].key >= limit->key))
                return HK_ERR_INVALID_ARGUMENT;
        }
    }
    core->inventory = inventory;
    core->providers = providers;
    core->provider_count = provider_count;
    core->initialized = 1U;
    return HK_OK;
}

hk_result_t hk_capability_core_owner_open(
    hk_capability_core_t *core,
    const hk_capability_grant_t *grants,
    uint16_t grant_count,
    hk_owner_t *owner)
{
    uint32_t index;

    if(!core || !core->initialized || !owner ||
       grant_count > HK_CAPABILITY_MAX_GRANTS_PER_OWNER ||
       (grant_count > 0U && !grants))
        return HK_ERR_INVALID_ARGUMENT;
    *owner = HK_OWNER_NONE;
    for(index = 0U; index < grant_count; index++)
    {
        if(validate_request(&grants[index].request) != HK_OK)
            return HK_ERR_INVALID_ARGUMENT;
    }
    for(index = 0U; index < HK_CAPABILITY_MAX_OWNERS; index++)
    {
        hk_capability_owner_slot_t *slot = &core->owners[index];
        if(slot->active || slot->retired)
            continue;
        if(slot->generation == 0U)
            slot->generation = 1U;
        slot->grants = grants;
        slot->grant_count = grant_count;
        slot->active = 1U;
        owner->slot = index;
        owner->generation = slot->generation;
        return HK_OK;
    }
    return HK_ERR_LIMIT;
}

hk_result_t hk_capability_core_owner_close(
    hk_capability_core_t *core,
    hk_owner_t owner,
    uint16_t current_core,
    hk_deadline_t deadline)
{
    hk_capability_owner_slot_t *owner_slot;
    hk_result_t result;
    hk_result_t cleanup_result = HK_OK;
    uint32_t index;

    if(!deadline_is_valid(deadline))
        return HK_ERR_INVALID_ARGUMENT;
    result = validate_owner(core, owner, &owner_slot);
    if(result != HK_OK)
        return result;
    for(index = 0U; index < HK_CAPABILITY_MAX_LEASES; index++)
    {
        hk_capability_lease_slot_t *lease = &core->leases[index];
        if(!lease->active || !owner_equal(lease->owner, owner))
            continue;
        if(cleanup_lease_for_owner_close(
               core, lease, current_core, deadline) != HK_OK)
            cleanup_result = HK_ERR_INTERNAL;
    }
    retire_or_advance_owner(owner_slot);
    return cleanup_result;
}

hk_result_t hk_capability_core_acquire(
    hk_capability_core_t *core,
    hk_owner_t owner,
    const hk_capability_request_t *request,
    hk_capability_id_t expected_type,
    uint16_t current_core,
    hk_lease_t *lease)
{
    hk_capability_owner_slot_t *owner_slot;
    hk_capability_lease_slot_t *lease_slot = NULL;
    hk_result_t result;
    int provider_index;
    uint32_t index;

    if(!lease)
        return HK_ERR_INVALID_ARGUMENT;
    *lease = HK_LEASE_NONE;
    result = validate_request(request);
    if(result != HK_OK || expected_type == 0U || expected_type != request->id)
        return HK_ERR_INVALID_ARGUMENT;
    result = validate_owner(core, owner, &owner_slot);
    if(result != HK_OK)
        return result;
    provider_index = find_provider(core, request->id, request->instance);
    if(provider_index < 0)
        return HK_ERR_CAPABILITY_ABSENT;
    if(!affinity_matches(&core->inventory[provider_index], current_core))
        return HK_ERR_WRONG_CONTEXT;
    if(version_compare(core->inventory[provider_index].version,
                       request->minimum) < 0 ||
       version_compare(core->inventory[provider_index].version,
                       request->maximum_exclusive) >= 0)
        return HK_ERR_VERSION_INCOMPATIBLE;
    if((request->required_features &
        ~core->inventory[provider_index].features) != 0U)
        return HK_ERR_FEATURE_UNAVAILABLE;
    if(!grant_allows(owner_slot, request))
        return HK_ERR_NOT_DECLARED;
    if(core->provider_state[provider_index].quarantined)
        return HK_ERR_INVALID_STATE;
    if((core->inventory[provider_index].flags & HK_CAPABILITY_FLAG_EXCLUSIVE) &&
       core->provider_state[provider_index].active_leases > 0U)
        return HK_ERR_BUSY;
    if(core->provider_state[provider_index].active_leases >=
       core->providers[provider_index]->max_leases)
        return HK_ERR_BUSY;
    for(index = 0U; index < HK_CAPABILITY_MAX_LEASES; index++)
    {
        if(!core->leases[index].active && !core->leases[index].retired)
        {
            lease_slot = &core->leases[index];
            break;
        }
    }
    if(!lease_slot)
        return HK_ERR_LIMIT;
    if(core->providers[provider_index]->acquire)
    {
        result = core->providers[provider_index]->acquire(
            core->providers[provider_index]->context, owner);
        if(result != HK_OK)
            return result;
    }
    if(lease_slot->generation == 0U)
        lease_slot->generation = 1U;
    lease_slot->owner = owner;
    lease_slot->capability_id = request->id;
    lease_slot->provider_index = (uint16_t)provider_index;
    lease_slot->active = 1U;
    core->provider_state[provider_index].active_leases++;
    lease->slot = (uint32_t)(lease_slot - core->leases);
    lease->generation = lease_slot->generation;
    lease->owner = owner;
    lease->capability_id = request->id;
    return HK_OK;
}

static hk_result_t validate_lease(
    hk_capability_core_t *core,
    hk_owner_t owner,
    const hk_lease_t *lease,
    hk_capability_id_t expected_type,
    uint16_t current_core,
    uint8_t allow_quarantined,
    void **provider_context)
{
    hk_capability_lease_slot_t *slot;
    hk_result_t result = validate_owner(core, owner, NULL);

    if(result != HK_OK)
        return result;
    if(!lease || hk_lease_is_zero(lease) || expected_type == 0U)
        return HK_ERR_STALE_HANDLE;
    if(!owner_equal(lease->owner, owner))
        return HK_ERR_WRONG_OWNER;
    if(lease->capability_id != expected_type)
        return HK_ERR_INVALID_ARGUMENT;
    if(lease->slot >= HK_CAPABILITY_MAX_LEASES || lease->generation == 0U)
        return HK_ERR_STALE_HANDLE;
    slot = &core->leases[lease->slot];
    if(!slot->active || slot->generation != lease->generation ||
       !owner_equal(slot->owner, owner) ||
       slot->capability_id != lease->capability_id)
        return HK_ERR_STALE_HANDLE;
    if(!affinity_matches(&core->inventory[slot->provider_index], current_core))
        return HK_ERR_WRONG_CONTEXT;
    if(!allow_quarantined &&
       core->provider_state[slot->provider_index].quarantined)
        return HK_ERR_INVALID_STATE;
    if(provider_context)
        *provider_context = core->providers[slot->provider_index]->context;
    return HK_OK;
}

hk_result_t hk_capability_core_validate_lease(
    hk_capability_core_t *core,
    hk_owner_t owner,
    const hk_lease_t *lease,
    hk_capability_id_t expected_type,
    uint16_t current_core,
    void **provider_context)
{
    return validate_lease(
        core, owner, lease, expected_type, current_core, 0U, provider_context);
}

hk_result_t hk_capability_core_quarantine_lease(
    hk_capability_core_t *core,
    hk_owner_t owner,
    const hk_lease_t *lease,
    hk_capability_id_t expected_type,
    uint16_t current_core)
{
    hk_capability_lease_slot_t *slot;
    hk_result_t result = validate_lease(
        core, owner, lease, expected_type, current_core, 1U, NULL);

    if(result != HK_OK)
        return result;
    slot = &core->leases[lease->slot];
    core->provider_state[slot->provider_index].quarantined = 1U;
    return HK_OK;
}

hk_result_t hk_capability_core_release(
    hk_capability_core_t *core,
    hk_owner_t owner,
    hk_capability_id_t expected_type,
    uint16_t current_core,
    hk_deadline_t deadline,
    hk_lease_t *lease)
{
    hk_capability_lease_slot_t *slot;
    hk_result_t result;

    if(!lease || !deadline_is_valid(deadline))
        return HK_ERR_INVALID_ARGUMENT;
    if(hk_lease_is_zero(lease))
        return HK_OK;
    result = validate_lease(
        core, owner, lease, expected_type, current_core, 1U, NULL);
    if(result != HK_OK)
        return result;
    slot = &core->leases[lease->slot];
    result = cleanup_lease(core, slot, deadline);
    *lease = HK_LEASE_NONE;
    return result;
}

hk_result_t hk_capability_core_recover(
    hk_capability_core_t *core,
    hk_capability_id_t capability_id,
    uint16_t instance,
    uint16_t current_core,
    hk_deadline_t deadline)
{
    int provider_index;
    hk_result_t result;

    if(!core || !core->initialized || capability_id == 0U ||
       !deadline_is_valid(deadline))
        return HK_ERR_INVALID_ARGUMENT;
    provider_index = find_provider(core, capability_id, instance);
    if(provider_index < 0)
        return HK_ERR_CAPABILITY_ABSENT;
    if(!affinity_matches(&core->inventory[provider_index], current_core))
        return HK_ERR_WRONG_CONTEXT;
    if(!core->provider_state[provider_index].quarantined)
        return HK_OK;
    if(core->provider_state[provider_index].active_leases > 0U)
        return HK_ERR_BUSY;
    if(!core->providers[provider_index]->recover)
        return HK_ERR_INVALID_STATE;
    result = core->providers[provider_index]->recover(
        core->providers[provider_index]->context, deadline);
    if(result == HK_OK)
        core->provider_state[provider_index].quarantined = 0U;
    return result;
}

const hk_capability_info_t *hk_capability_core_inventory(
    const hk_capability_core_t *core,
    uint16_t *count)
{
    if(count)
        *count = core && core->initialized ? core->provider_count : 0U;
    return core && core->initialized ? core->inventory : NULL;
}

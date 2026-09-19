#include "capability_fake_provider.h"

#include <string.h>

void capability_fake_provider_reset(capability_fake_provider_t *fake)
{
    memset(fake, 0, sizeof(*fake));
    fake->acquire_result = HK_OK;
    fake->cleanup_result = HK_OK;
    fake->recover_result = HK_OK;
}

hk_result_t capability_fake_acquire(void *context, hk_owner_t owner)
{
    capability_fake_provider_t *fake = context;
    fake->acquire_calls++;
    fake->last_owner = owner;
    return fake->acquire_result;
}

hk_result_t capability_fake_cleanup(
    void *context,
    hk_owner_t owner,
    hk_deadline_t deadline)
{
    capability_fake_provider_t *fake = context;
    fake->cleanup_calls++;
    fake->last_owner = owner;
    fake->last_deadline = deadline;
    return fake->cleanup_result;
}

hk_result_t capability_fake_cleanup_dispatch(
    void *context,
    hk_owner_t owner,
    uint16_t target_core,
    hk_deadline_t deadline)
{
    capability_fake_provider_t *fake = context;
    fake->cleanup_dispatch_calls++;
    fake->last_cleanup_target_core = target_core;
    return capability_fake_cleanup(context, owner, deadline);
}

hk_result_t capability_fake_recover(void *context, hk_deadline_t deadline)
{
    capability_fake_provider_t *fake = context;
    fake->recover_calls++;
    fake->last_deadline = deadline;
    return fake->recover_result;
}

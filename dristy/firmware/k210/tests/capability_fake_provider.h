#ifndef HK_CAPABILITY_FAKE_PROVIDER_H
#define HK_CAPABILITY_FAKE_PROVIDER_H

#include "../firmware/src/capabilities/capability_provider.h"

typedef struct
{
    hk_result_t acquire_result;
    hk_result_t cleanup_result;
    hk_result_t recover_result;
    uint32_t acquire_calls;
    uint32_t cleanup_calls;
    uint32_t cleanup_dispatch_calls;
    uint32_t recover_calls;
    uint16_t last_cleanup_target_core;
    hk_owner_t last_owner;
    hk_deadline_t last_deadline;
} capability_fake_provider_t;

void capability_fake_provider_reset(capability_fake_provider_t *fake);
hk_result_t capability_fake_acquire(void *context, hk_owner_t owner);
hk_result_t capability_fake_cleanup(
    void *context,
    hk_owner_t owner,
    hk_deadline_t deadline);
hk_result_t capability_fake_cleanup_dispatch(
    void *context,
    hk_owner_t owner,
    uint16_t target_core,
    hk_deadline_t deadline);
hk_result_t capability_fake_recover(void *context, hk_deadline_t deadline);

#endif

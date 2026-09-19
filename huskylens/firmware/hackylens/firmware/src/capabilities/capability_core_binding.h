#ifndef HK_CAPABILITY_CORE_BINDING_H
#define HK_CAPABILITY_CORE_BINDING_H

#include <hackylens/capability/owner.h>

/* Private bridge from public capability implementations to the runtime core. */
hk_result_t capability_owner_runtime_acquire(
    hk_owner_t owner,
    const hk_capability_request_t *request,
    hk_capability_id_t expected_type,
    hk_lease_t *lease);
hk_result_t capability_owner_runtime_release(
    hk_owner_t owner,
    hk_capability_id_t expected_type,
    hk_deadline_t deadline,
    hk_lease_t *lease);
hk_result_t capability_owner_runtime_validate(
    hk_owner_t owner,
    const hk_lease_t *lease,
    hk_capability_id_t expected_type,
    void **provider_context);
hk_result_t capability_owner_runtime_quarantine(
    hk_owner_t owner,
    const hk_lease_t *lease,
    hk_capability_id_t expected_type);

#endif

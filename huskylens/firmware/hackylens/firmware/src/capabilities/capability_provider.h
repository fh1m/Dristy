#ifndef HK_CAPABILITY_PROVIDER_H
#define HK_CAPABILITY_PROVIDER_H

#include <stddef.h>
#include <stdint.h>

#include <hackylens/capability/inventory.h>
#include <hackylens/capability/owner.h>

#define HK_CAPABILITY_MAX_OWNERS 16U
#define HK_CAPABILITY_MAX_LEASES 32U
#define HK_CAPABILITY_MAX_PROVIDERS 16U
#define HK_CAPABILITY_MAX_GRANTS_PER_OWNER 16U

typedef hk_result_t (*hk_capability_provider_acquire_fn)(
    void *context,
    hk_owner_t owner);
typedef hk_result_t (*hk_capability_provider_cleanup_lease_fn)(
    void *context,
    const hk_lease_t *lease,
    hk_deadline_t deadline);
typedef hk_result_t (*hk_capability_provider_cleanup_fn)(
    void *context,
    hk_owner_t owner,
    hk_deadline_t deadline);
typedef hk_result_t (*hk_capability_provider_cleanup_dispatch_fn)(
    void *context,
    hk_owner_t owner,
    uint16_t target_core,
    hk_deadline_t deadline);
typedef hk_result_t (*hk_capability_provider_recover_fn)(
    void *context,
    hk_deadline_t deadline);

typedef struct
{
    void *context;
    hk_capability_provider_acquire_fn acquire;
    hk_capability_provider_cleanup_lease_fn cleanup_lease;
    hk_capability_provider_cleanup_fn cleanup;
    hk_capability_provider_cleanup_dispatch_fn cleanup_dispatch;
    hk_capability_provider_recover_fn recover;
    uint16_t max_leases;
    uint16_t reserved;
} hk_capability_provider_t;

typedef struct
{
    hk_capability_request_t request;
} hk_capability_grant_t;

typedef struct
{
    const hk_capability_grant_t *grants;
    uint16_t grant_count;
    uint16_t reserved;
    uint32_t generation;
    uint8_t active;
    uint8_t retired;
} hk_capability_owner_slot_t;

typedef struct
{
    hk_owner_t owner;
    hk_capability_id_t capability_id;
    uint32_t generation;
    uint16_t provider_index;
    uint8_t active;
    uint8_t retired;
} hk_capability_lease_slot_t;

typedef struct
{
    uint16_t active_leases;
    uint8_t quarantined;
    uint8_t reserved;
} hk_capability_provider_state_t;

typedef struct
{
    const hk_capability_info_t *inventory;
    const hk_capability_provider_t *const *providers;
    uint16_t provider_count;
    uint8_t initialized;
    uint8_t reserved;
    hk_capability_owner_slot_t owners[HK_CAPABILITY_MAX_OWNERS];
    hk_capability_lease_slot_t leases[HK_CAPABILITY_MAX_LEASES];
    hk_capability_provider_state_t provider_state[HK_CAPABILITY_MAX_PROVIDERS];
} hk_capability_core_t;

hk_result_t hk_capability_core_init(
    hk_capability_core_t *core,
    const hk_capability_info_t *inventory,
    const hk_capability_provider_t *const *providers,
    uint16_t provider_count);
hk_result_t hk_capability_core_owner_open(
    hk_capability_core_t *core,
    const hk_capability_grant_t *grants,
    uint16_t grant_count,
    hk_owner_t *owner);
hk_result_t hk_capability_core_owner_close(
    hk_capability_core_t *core,
    hk_owner_t owner,
    uint16_t current_core,
    hk_deadline_t deadline);
hk_result_t hk_capability_core_acquire(
    hk_capability_core_t *core,
    hk_owner_t owner,
    const hk_capability_request_t *request,
    hk_capability_id_t expected_type,
    uint16_t current_core,
    hk_lease_t *lease);
hk_result_t hk_capability_core_release(
    hk_capability_core_t *core,
    hk_owner_t owner,
    hk_capability_id_t expected_type,
    uint16_t current_core,
    hk_deadline_t deadline,
    hk_lease_t *lease);
hk_result_t hk_capability_core_validate_lease(
    hk_capability_core_t *core,
    hk_owner_t owner,
    const hk_lease_t *lease,
    hk_capability_id_t expected_type,
    uint16_t current_core,
    void **provider_context);
hk_result_t hk_capability_core_quarantine_lease(
    hk_capability_core_t *core,
    hk_owner_t owner,
    const hk_lease_t *lease,
    hk_capability_id_t expected_type,
    uint16_t current_core);
hk_result_t hk_capability_core_recover(
    hk_capability_core_t *core,
    hk_capability_id_t capability_id,
    uint16_t instance,
    uint16_t current_core,
    hk_deadline_t deadline);
const hk_capability_info_t *hk_capability_core_inventory(
    const hk_capability_core_t *core,
    uint16_t *count);

#endif

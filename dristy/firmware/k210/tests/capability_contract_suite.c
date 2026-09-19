#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "capability_fake_provider.h"
#include "../firmware/src/core/hk_app.h"
#include "../firmware/src/runtime/capability_owner_runtime.h"

#define CAP_TIME 0x00010001U
#define CAP_INPUT 0x00010002U
#define FEATURE_A (1ULL << 0)
#define FEATURE_B (1ULL << 1)

HK_DECLARE_CAPABILITY_HANDLE(test_capability_handle_t);
_Static_assert(sizeof(hk_result_t) == 4U, "hk_result_t ABI");
_Static_assert(sizeof(hk_capability_id_t) == 4U, "capability ID ABI");
_Static_assert(sizeof(test_capability_handle_t) == sizeof(hk_lease_t),
               "typed handles contain only one lease token");

#define CHECK(expression)                                                     \
    do                                                                        \
    {                                                                         \
        if(!(expression))                                                     \
        {                                                                     \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__,         \
                    #expression);                                             \
            exit(1);                                                          \
        }                                                                     \
    } while(0)

typedef struct
{
    uint32_t before;
    hk_capability_core_t core;
    uint32_t after;
} guarded_core_t;

static capability_fake_provider_t s_fake[2];
static hk_capability_info_t s_inventory[2];
static hk_capability_provider_t s_providers[2];
static const hk_capability_provider_t *s_provider_refs[2];
static hk_capability_grant_t s_grants[2];

static hk_version_t version(uint16_t major, uint16_t minor, uint16_t patch)
{
    hk_version_t result = {major, minor, patch, 0U};
    return result;
}

static hk_capability_request_t request(
    hk_capability_id_t id,
    uint64_t features)
{
    hk_capability_request_t result;
    memset(&result, 0, sizeof(result));
    result.struct_size = sizeof(result);
    result.struct_version = HK_CAPABILITY_REQUEST_VERSION;
    result.id = id;
    result.minimum = version(0U, 1U, 0U);
    result.maximum_exclusive = version(0U, 2U, 0U);
    result.required_features = features;
    return result;
}

static void setup_core_with_affinity(
    hk_capability_core_t *core,
    uint16_t time_core,
    uint16_t input_core)
{
    uint16_t index;

    memset(s_inventory, 0, sizeof(s_inventory));
    memset(s_providers, 0, sizeof(s_providers));
    memset(s_grants, 0, sizeof(s_grants));
    for(index = 0U; index < 2U; index++)
    {
        capability_fake_provider_reset(&s_fake[index]);
        s_inventory[index].struct_size = sizeof(s_inventory[index]);
        s_inventory[index].struct_version = HK_CAPABILITY_INFO_VERSION;
        s_inventory[index].id = CAP_TIME + index;
        s_inventory[index].version = version(0U, 1U, 0U);
        s_inventory[index].features = FEATURE_A | FEATURE_B;
        s_inventory[index].flags = index == 0U
            ? HK_CAPABILITY_FLAG_SHARED : HK_CAPABILITY_FLAG_EXCLUSIVE;
        s_inventory[index].affinity_core = index == 0U ? time_core : input_core;
        s_providers[index].context = &s_fake[index];
        s_providers[index].acquire = capability_fake_acquire;
        s_providers[index].cleanup = capability_fake_cleanup;
        s_providers[index].cleanup_dispatch = capability_fake_cleanup_dispatch;
        s_providers[index].recover = capability_fake_recover;
        s_providers[index].max_leases = UINT16_MAX;
        s_provider_refs[index] = &s_providers[index];
        s_grants[index].request = request(CAP_TIME + index,
                                          FEATURE_A | FEATURE_B);
    }
    CHECK(hk_capability_core_init(
        core, s_inventory, s_provider_refs, 2U) == HK_OK);
}

static void setup_core(hk_capability_core_t *core)
{
    setup_core_with_affinity(core, HK_CAPABILITY_CORE_ANY, 1U);
}

static hk_owner_t open_owner(hk_capability_core_t *core)
{
    hk_owner_t owner;
    CHECK(hk_capability_core_owner_open(
        core, s_grants, 2U, &owner) == HK_OK);
    return owner;
}

static hk_lease_t acquire_time(hk_capability_core_t *core, hk_owner_t owner)
{
    hk_capability_request_t wanted = request(CAP_TIME, FEATURE_A);
    hk_lease_t lease;
    CHECK(hk_capability_core_acquire(
        core, owner, &wanted, CAP_TIME, 0U, &lease) == HK_OK);
    return lease;
}

static void test_acquire_release_and_stale_copy(void)
{
    hk_capability_core_t core;
    hk_owner_t owner;
    hk_lease_t lease;
    hk_lease_t copy;

    setup_core(&core);
    owner = open_owner(&core);
    lease = acquire_time(&core, owner);
    copy = lease;
    CHECK(s_fake[0].acquire_calls == 1U);
    CHECK(hk_capability_core_release(
        &core, owner, CAP_TIME, 0U, (hk_deadline_t){123U}, &lease) == HK_OK);
    CHECK(hk_lease_is_zero(&lease));
    CHECK(s_fake[0].cleanup_calls == 1U);
    CHECK(s_fake[0].last_deadline.at_us == 123U);
    CHECK(hk_capability_core_release(
        &core, owner, CAP_TIME, 0U, HK_DEADLINE_IMMEDIATE, &lease) == HK_OK);
    CHECK(s_fake[0].cleanup_calls == 1U);
    CHECK(hk_capability_core_release(
        &core, owner, CAP_TIME, 0U, HK_DEADLINE_IMMEDIATE, &copy) ==
          HK_ERR_STALE_HANDLE);
    CHECK(hk_capability_core_owner_close(
        &core, owner, 0U, HK_DEADLINE_IMMEDIATE) == HK_OK);
}

static void test_wrong_owner_type_context_and_inactive_owner(void)
{
    hk_capability_core_t core;
    hk_owner_t first;
    hk_owner_t second;
    hk_lease_t lease;
    hk_capability_request_t input = request(CAP_INPUT, FEATURE_A);

    setup_core(&core);
    first = open_owner(&core);
    second = open_owner(&core);
    lease = acquire_time(&core, first);
    CHECK(hk_capability_core_validate_lease(
        &core, second, &lease, CAP_TIME, 0U, NULL) == HK_ERR_WRONG_OWNER);
    CHECK(hk_capability_core_validate_lease(
        &core, first, &lease, CAP_INPUT, 0U, NULL) == HK_ERR_INVALID_ARGUMENT);
    CHECK(hk_capability_core_acquire(
        &core, first, &input, CAP_INPUT, 0U, &lease) == HK_ERR_WRONG_CONTEXT);
    CHECK(s_fake[1].acquire_calls == 0U);
    CHECK(hk_capability_core_owner_close(
        &core, first, 0U, HK_DEADLINE_IMMEDIATE) == HK_OK);
    CHECK(hk_capability_core_validate_lease(
        &core, first, &lease, CAP_TIME, 0U, NULL) == HK_ERR_STALE_HANDLE);
    CHECK(hk_capability_core_owner_close(
        &core, second, 0U, HK_DEADLINE_IMMEDIATE) == HK_OK);
}

static void test_mixed_affinity_owner_close(void)
{
    hk_capability_core_t core;
    hk_owner_t owner;
    hk_lease_t time_lease;
    hk_lease_t input_lease;
    hk_capability_request_t input = request(CAP_INPUT, FEATURE_A);
    hk_deadline_t deadline = {800U};

    setup_core_with_affinity(&core, 0U, 1U);
    owner = open_owner(&core);
    time_lease = acquire_time(&core, owner);
    CHECK(hk_capability_core_acquire(
        &core, owner, &input, CAP_INPUT, 1U, &input_lease) == HK_OK);
    CHECK(hk_capability_core_owner_close(
        &core, owner, 0U, deadline) == HK_OK);
    CHECK(s_fake[0].cleanup_calls == 1U);
    CHECK(s_fake[0].cleanup_dispatch_calls == 0U);
    CHECK(s_fake[1].cleanup_calls == 1U);
    CHECK(s_fake[1].cleanup_dispatch_calls == 1U);
    CHECK(s_fake[1].last_cleanup_target_core == 1U);
    CHECK(s_fake[0].last_deadline.at_us == deadline.at_us);
    CHECK(s_fake[1].last_deadline.at_us == deadline.at_us);
    CHECK(core.provider_state[0].quarantined == 0U);
    CHECK(core.provider_state[1].quarantined == 0U);
    CHECK(core.leases[time_lease.slot].active == 0U);
    CHECK(core.leases[input_lease.slot].active == 0U);
    CHECK(hk_capability_core_validate_lease(
        &core, owner, &time_lease, CAP_TIME, 0U, NULL) == HK_ERR_STALE_HANDLE);
    CHECK(hk_capability_core_validate_lease(
        &core, owner, &input_lease, CAP_INPUT, 1U, NULL) ==
          HK_ERR_STALE_HANDLE);
    CHECK(hk_capability_core_owner_close(
        &core, owner, 0U, deadline) == HK_ERR_STALE_HANDLE);
}

static void test_mixed_affinity_owner_close_partial_failure(void)
{
    hk_capability_core_t core;
    hk_owner_t owner;
    hk_lease_t time_lease;
    hk_lease_t input_lease;
    hk_capability_request_t input = request(CAP_INPUT, FEATURE_A);

    setup_core_with_affinity(&core, 0U, 1U);
    owner = open_owner(&core);
    time_lease = acquire_time(&core, owner);
    CHECK(hk_capability_core_acquire(
        &core, owner, &input, CAP_INPUT, 1U, &input_lease) == HK_OK);
    s_fake[0].cleanup_result = HK_ERR_IO;
    CHECK(hk_capability_core_owner_close(
        &core, owner, 0U, (hk_deadline_t){900U}) == HK_ERR_INTERNAL);
    CHECK(s_fake[0].cleanup_calls == 1U);
    CHECK(s_fake[1].cleanup_calls == 1U);
    CHECK(s_fake[1].cleanup_dispatch_calls == 1U);
    CHECK(core.provider_state[0].quarantined == 1U);
    CHECK(core.provider_state[1].quarantined == 0U);
    CHECK(core.leases[time_lease.slot].active == 0U);
    CHECK(core.leases[input_lease.slot].active == 0U);
    CHECK(hk_capability_core_owner_close(
        &core, owner, 0U, HK_DEADLINE_IMMEDIATE) == HK_ERR_STALE_HANDLE);
}

static void test_wrong_core_operations_remain_strict(void)
{
    hk_capability_core_t core;
    hk_owner_t owner;
    hk_lease_t input_lease;
    hk_capability_request_t input = request(CAP_INPUT, FEATURE_A);
    void *provider_context = NULL;

    setup_core_with_affinity(&core, 0U, 1U);
    owner = open_owner(&core);
    CHECK(hk_capability_core_acquire(
        &core, owner, &input, CAP_INPUT, 0U, &input_lease) ==
          HK_ERR_WRONG_CONTEXT);
    CHECK(s_fake[1].acquire_calls == 0U);
    CHECK(hk_capability_core_acquire(
        &core, owner, &input, CAP_INPUT, 1U, &input_lease) == HK_OK);
    CHECK(hk_capability_core_validate_lease(
        &core, owner, &input_lease, CAP_INPUT, 0U, &provider_context) ==
          HK_ERR_WRONG_CONTEXT);
    CHECK(provider_context == NULL);
    CHECK(hk_capability_core_release(
        &core, owner, CAP_INPUT, 0U, HK_DEADLINE_IMMEDIATE, &input_lease) ==
          HK_ERR_WRONG_CONTEXT);
    CHECK(s_fake[1].cleanup_calls == 0U);
    CHECK(hk_capability_core_release(
        &core, owner, CAP_INPUT, 1U, HK_DEADLINE_IMMEDIATE, &input_lease) ==
          HK_OK);
    CHECK(hk_capability_core_owner_close(
        &core, owner, 0U, HK_DEADLINE_IMMEDIATE) == HK_OK);
}

static void test_owner_close_cleanup_failure_and_recovery(void)
{
    hk_capability_core_t core;
    hk_owner_t owner;
    hk_owner_t next;
    hk_lease_t time_lease;
    hk_lease_t input_lease;
    hk_capability_request_t input = request(CAP_INPUT, FEATURE_A);

    setup_core(&core);
    owner = open_owner(&core);
    time_lease = acquire_time(&core, owner);
    CHECK(hk_capability_core_acquire(
        &core, owner, &input, CAP_INPUT, 1U, &input_lease) == HK_OK);
    s_fake[1].cleanup_result = HK_ERR_IO;
    CHECK(hk_capability_core_owner_close(
        &core, owner, 1U, (hk_deadline_t){500U}) == HK_ERR_INTERNAL);
    CHECK(s_fake[0].cleanup_calls == 1U);
    CHECK(s_fake[1].cleanup_calls == 1U);
    CHECK(hk_capability_core_validate_lease(
        &core, owner, &time_lease, CAP_TIME, 0U, NULL) == HK_ERR_STALE_HANDLE);
    next = open_owner(&core);
    CHECK(hk_capability_core_acquire(
        &core, next, &input, CAP_INPUT, 1U, &input_lease) == HK_ERR_INVALID_STATE);
    CHECK(hk_capability_core_recover(
        &core, CAP_INPUT, 0U, 0U, (hk_deadline_t){600U}) ==
          HK_ERR_WRONG_CONTEXT);
    s_fake[1].recover_result = HK_ERR_IO;
    CHECK(hk_capability_core_recover(
        &core, CAP_INPUT, 0U, 1U, (hk_deadline_t){600U}) == HK_ERR_IO);
    CHECK(hk_capability_core_acquire(
        &core, next, &input, CAP_INPUT, 1U, &input_lease) == HK_ERR_INVALID_STATE);
    s_fake[1].cleanup_result = HK_OK;
    s_fake[1].recover_result = HK_OK;
    CHECK(hk_capability_core_recover(
        &core, CAP_INPUT, 0U, 1U, (hk_deadline_t){700U}) == HK_OK);
    CHECK(hk_capability_core_acquire(
        &core, next, &input, CAP_INPUT, 1U, &input_lease) == HK_OK);
    CHECK(hk_capability_core_owner_close(
        &core, next, 1U, HK_DEADLINE_IMMEDIATE) == HK_OK);
}

static void test_quarantine_still_allows_remaining_release(void)
{
    hk_capability_core_t core;
    hk_owner_t owner;
    hk_lease_t first;
    hk_lease_t second;

    setup_core(&core);
    owner = open_owner(&core);
    first = acquire_time(&core, owner);
    second = acquire_time(&core, owner);
    s_fake[0].cleanup_result = HK_ERR_IO;
    CHECK(hk_capability_core_release(
        &core, owner, CAP_TIME, 0U, HK_DEADLINE_IMMEDIATE, &first) ==
          HK_ERR_INTERNAL);
    CHECK(hk_capability_core_validate_lease(
        &core, owner, &second, CAP_TIME, 0U, NULL) == HK_ERR_INVALID_STATE);
    CHECK(hk_capability_core_recover(
        &core, CAP_TIME, 0U, 0U, (hk_deadline_t){10U}) == HK_ERR_BUSY);
    s_fake[0].cleanup_result = HK_OK;
    CHECK(hk_capability_core_release(
        &core, owner, CAP_TIME, 0U, HK_DEADLINE_IMMEDIATE, &second) == HK_OK);
    CHECK(hk_capability_core_recover(
        &core, CAP_TIME, 0U, 0U, (hk_deadline_t){20U}) == HK_OK);
    CHECK(hk_capability_core_owner_close(
        &core, owner, 0U, HK_DEADLINE_IMMEDIATE) == HK_OK);
}

static void test_fixed_capacity_and_generation_exhaustion(void)
{
    guarded_core_t guarded;
    hk_owner_t owners[HK_CAPABILITY_MAX_OWNERS];
    hk_lease_t leases[HK_CAPABILITY_MAX_LEASES];
    hk_owner_t extra_owner;
    hk_lease_t extra_lease;
    hk_capability_request_t wanted = request(CAP_TIME, FEATURE_A);
    uint32_t index;

    guarded.before = 0x13579BDFU;
    guarded.after = 0x2468ACE0U;
    setup_core(&guarded.core);
    for(index = 0U; index < HK_CAPABILITY_MAX_OWNERS; index++)
        owners[index] = open_owner(&guarded.core);
    CHECK(hk_capability_core_owner_open(
        &guarded.core, s_grants, 2U, &extra_owner) == HK_ERR_LIMIT);
    for(index = 0U; index < HK_CAPABILITY_MAX_LEASES; index++)
    {
        CHECK(hk_capability_core_acquire(
            &guarded.core, owners[0], &wanted, CAP_TIME, 0U,
            &leases[index]) == HK_OK);
    }
    CHECK(hk_capability_core_acquire(
        &guarded.core, owners[0], &wanted, CAP_TIME, 0U,
        &extra_lease) == HK_ERR_LIMIT);
    CHECK(guarded.before == 0x13579BDFU);
    CHECK(guarded.after == 0x2468ACE0U);
    for(index = 0U; index < HK_CAPABILITY_MAX_OWNERS; index++)
        CHECK(hk_capability_core_owner_close(
            &guarded.core, owners[index], 0U, HK_DEADLINE_IMMEDIATE) == HK_OK);

    setup_core(&guarded.core);
    guarded.core.owners[0].generation = UINT32_MAX;
    owners[0] = open_owner(&guarded.core);
    CHECK(hk_capability_core_owner_close(
        &guarded.core, owners[0], 0U, HK_DEADLINE_IMMEDIATE) == HK_OK);
    CHECK(guarded.core.owners[0].retired == 1U);
    owners[1] = open_owner(&guarded.core);
    CHECK(owners[1].slot == 1U);

    guarded.core.leases[0].generation = UINT32_MAX;
    leases[0] = acquire_time(&guarded.core, owners[1]);
    CHECK(leases[0].slot == 0U);
    CHECK(hk_capability_core_release(
        &guarded.core, owners[1], CAP_TIME, 0U,
        HK_DEADLINE_IMMEDIATE, &leases[0]) == HK_OK);
    CHECK(guarded.core.leases[0].retired == 1U);
    leases[1] = acquire_time(&guarded.core, owners[1]);
    CHECK(leases[1].slot == 1U);
    CHECK(hk_capability_core_owner_close(
        &guarded.core, owners[1], 0U, HK_DEADLINE_IMMEDIATE) == HK_OK);
}

static void test_negotiation_grants_and_inventory_validation(void)
{
    hk_capability_core_t core;
    hk_capability_limit_t limits[2];
    hk_owner_t owner;
    hk_lease_t lease;
    hk_capability_request_t wanted;
    uint16_t count = 99U;

    setup_core(&core);
    CHECK(hk_capability_core_inventory(&core, &count) == s_inventory);
    CHECK(count == 2U);
    CHECK(hk_capability_core_owner_open(&core, NULL, 0U, &owner) == HK_OK);
    wanted = request(CAP_TIME, FEATURE_A);
    CHECK(hk_capability_core_acquire(
        &core, owner, &wanted, CAP_TIME, 0U, &lease) == HK_ERR_NOT_DECLARED);
    wanted.minimum = version(0U, 2U, 0U);
    wanted.maximum_exclusive = version(0U, 3U, 0U);
    CHECK(hk_capability_core_acquire(
        &core, owner, &wanted, CAP_TIME, 0U, &lease) ==
          HK_ERR_VERSION_INCOMPATIBLE);
    wanted = request(CAP_TIME, 1ULL << 8);
    CHECK(hk_capability_core_acquire(
        &core, owner, &wanted, CAP_TIME, 0U, &lease) ==
          HK_ERR_FEATURE_UNAVAILABLE);
    wanted = request(0x0001FFFFU, 0U);
    CHECK(hk_capability_core_acquire(
        &core, owner, &wanted, wanted.id, 0U, &lease) ==
          HK_ERR_CAPABILITY_ABSENT);
    CHECK(hk_capability_core_owner_close(
        &core, owner, 0U, HK_DEADLINE_IMMEDIATE) == HK_OK);

    s_inventory[1] = s_inventory[0];
    CHECK(hk_capability_core_init(
        &core, s_inventory, s_provider_refs, 2U) == HK_ERR_INVALID_ARGUMENT);

    setup_core(&core);
    s_grants[0].request.struct_version = 0U;
    CHECK(hk_capability_core_owner_open(
        &core, s_grants, 1U, &owner) == HK_ERR_INVALID_ARGUMENT);
    CHECK(hk_capability_core_owner_open(
        &core, s_grants, HK_CAPABILITY_MAX_GRANTS_PER_OWNER + 1U,
        &owner) == HK_ERR_INVALID_ARGUMENT);

    setup_core(&core);
    memset(limits, 0, sizeof(limits));
    limits[0].struct_size = sizeof(limits[0]);
    limits[0].struct_version = HK_CAPABILITY_LIMIT_VERSION;
    limits[0].key = 2U;
    limits[1] = limits[0];
    limits[1].key = 1U;
    s_inventory[0].limits = limits;
    s_inventory[0].limit_count = 2U;
    CHECK(hk_capability_core_init(
        &core, s_inventory, s_provider_refs, 2U) == HK_ERR_INVALID_ARGUMENT);
}

static void test_private_runtime_owner_binding_and_empty_inventory(void)
{
    hk_app_t first;
    hk_app_t second;
    hk_owner_t owner;
    const hk_capability_info_t *entries = (const hk_capability_info_t *)1;
    uint16_t count = 99U;

    memset(&first, 0, sizeof(first));
    memset(&second, 0, sizeof(second));
    first.id = "first";
    second.id = "second";
    CHECK(hk_capability_inventory_get(&entries, &count) == HK_OK);
    CHECK(entries == NULL);
    CHECK(count == 0U);
    CHECK(capability_owner_runtime_enter(&first) == HK_OK);
    owner = capability_owner_runtime_current(&first);
    CHECK(!hk_owner_is_zero(owner));
    CHECK(hk_owner_is_zero(capability_owner_runtime_current(&second)));
    CHECK(capability_owner_runtime_exit(&second) == HK_ERR_WRONG_OWNER);
    CHECK(capability_owner_runtime_exit(&first) == HK_OK);
    CHECK(hk_owner_is_zero(capability_owner_runtime_current(&first)));
    CHECK(capability_owner_runtime_exit(NULL) == HK_OK);
}

int main(void)
{
    test_acquire_release_and_stale_copy();
    test_wrong_owner_type_context_and_inactive_owner();
    test_mixed_affinity_owner_close();
    test_mixed_affinity_owner_close_partial_failure();
    test_wrong_core_operations_remain_strict();
    test_owner_close_cleanup_failure_and_recovery();
    test_quarantine_still_allows_remaining_release();
    test_fixed_capacity_and_generation_exhaustion();
    test_negotiation_grants_and_inventory_validation();
    test_private_runtime_owner_binding_and_empty_inventory();
    puts("CAPABILITY_CONTRACT_OK owners=16 leases=32 providers=16");
    return 0;
}

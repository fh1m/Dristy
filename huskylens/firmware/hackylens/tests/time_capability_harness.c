#include <hackylens/capability/time.h>

#include <limits.h>
#include <stdio.h>
#include <string.h>

#include "../firmware/src/capabilities/capability_provider.h"
#include "time_normative_backend.h"

#define CHECK(condition)                                                     \
    do                                                                       \
    {                                                                        \
        if(!(condition))                                                     \
        {                                                                    \
            printf("TIME_NORMATIVE_FAIL backend=%s line=%d\n",            \
                   time_normative_backend_name(), __LINE__);                 \
            return 1;                                                        \
        }                                                                    \
    } while(0)

static hk_capability_core_t s_core;
static hk_owner_t s_owner;
static const hk_capability_provider_t *s_provider_ref;
static const hk_capability_limit_t s_limits[] = {
    {sizeof(hk_capability_limit_t), HK_CAPABILITY_LIMIT_VERSION,
     1U, HK_TIME_MAX_SLEEP_US},
};
static const hk_capability_info_t s_inventory = {
    sizeof(hk_capability_info_t), HK_CAPABILITY_INFO_VERSION,
    HK_CAPABILITY_ID_TIME, {0U, 1U, 0U, 0U}, HK_TIME_FEATURES_0_1,
    HK_CAPABILITY_FLAG_SHARED, 0U, HK_CAPABILITY_CORE_ANY,
    s_limits, 1U, 0U,
};
static const hk_capability_grant_t s_grant = {
    .request = HK_TIME_REQUEST_0_1_INIT,
};
static uint32_t s_cancel_polls;
static uint32_t s_cancel_on_poll;

hk_result_t capability_owner_runtime_acquire(
    hk_owner_t owner, const hk_capability_request_t *request,
    hk_capability_id_t expected_type, hk_lease_t *lease)
{
    return hk_capability_core_acquire(
        &s_core, owner, request, expected_type, 0U, lease);
}

hk_result_t capability_owner_runtime_release(
    hk_owner_t owner, hk_capability_id_t expected_type,
    hk_deadline_t deadline, hk_lease_t *lease)
{
    return hk_capability_core_release(
        &s_core, owner, expected_type, 0U, deadline, lease);
}

hk_result_t capability_owner_runtime_validate(
    hk_owner_t owner, const hk_lease_t *lease,
    hk_capability_id_t expected_type, void **provider_context)
{
    return hk_capability_core_validate_lease(
        &s_core, owner, lease, expected_type, 0U, provider_context);
}

hk_result_t capability_owner_runtime_quarantine(
    hk_owner_t owner, const hk_lease_t *lease,
    hk_capability_id_t expected_type)
{
    return hk_capability_core_quarantine_lease(
        &s_core, owner, lease, expected_type, 0U);
}

static uint8_t cancel_probe(const void *context)
{
    (void)context;
    s_cancel_polls++;
    return (uint8_t)(s_cancel_on_poll != 0U &&
                     s_cancel_polls >= s_cancel_on_poll);
}

static int reset(hk_time_t *time, uint64_t *base)
{
    memset(&s_core, 0, sizeof(s_core));
    s_cancel_polls = 0U;
    s_cancel_on_poll = 0U;
    *base = time_normative_backend_reset();
    s_provider_ref = time_normative_backend_provider();
    CHECK(s_provider_ref != NULL);
    CHECK(hk_capability_core_init(
        &s_core, &s_inventory, &s_provider_ref, 1U) == HK_OK);
    CHECK(hk_capability_core_owner_open(
        &s_core, &s_grant, 1U, &s_owner) == HK_OK);
    CHECK(hk_time_acquire(s_owner, &s_grant.request, time) == HK_OK);
    return 0;
}

int main(void)
{
    hk_time_t time;
    hk_deadline_t target;
    hk_deadline_t deadline;
    hk_cancel_t cancel = {cancel_probe, NULL};
    uint64_t base;
    uint64_t now;

    CHECK(reset(&time, &base) == 0);
    CHECK(hk_time_now_us(s_owner, &time, &now) == HK_OK && now == base);
    CHECK(hk_time_deadline_after_us(
        s_owner, &time, 12000U, &target) == HK_OK);
    CHECK(target.at_us == base + 12000U);
    CHECK(hk_time_sleep_until(
        s_owner, &time, target, target, NULL) == HK_OK);
    CHECK(time_normative_backend_sleep_calls() == 3U &&
          time_normative_backend_slept_us() == 12000U);

    CHECK(reset(&time, &base) == 0);
    target.at_us = base;
    deadline.at_us = 0U;
    s_cancel_on_poll = 1U;
    CHECK(hk_time_sleep_until(
        s_owner, &time, target, deadline, &cancel) == HK_OK);
    CHECK(time_normative_backend_sleep_calls() == 0U && s_cancel_polls == 0U);

    CHECK(reset(&time, &base) == 0);
    target.at_us = base + 1000U;
    deadline.at_us = 0U;
    CHECK(hk_time_sleep_until(
        s_owner, &time, target, deadline, NULL) == HK_ERR_DEADLINE_EXCEEDED);
    CHECK(time_normative_backend_sleep_calls() == 0U);

    CHECK(reset(&time, &base) == 0);
    target.at_us = base + 20000U;
    deadline.at_us = target.at_us;
    s_cancel_on_poll = 1U;
    CHECK(hk_time_sleep_until(
        s_owner, &time, target, deadline, &cancel) == HK_ERR_CANCELLED);
    CHECK(time_normative_backend_sleep_calls() == 0U);

    CHECK(reset(&time, &base) == 0);
    target.at_us = base + 20000U;
    deadline.at_us = target.at_us;
    s_cancel_on_poll = 3U;
    CHECK(hk_time_sleep_until(
        s_owner, &time, target, deadline, &cancel) == HK_ERR_CANCELLED);
    CHECK(time_normative_backend_sleep_calls() == 2U &&
          time_normative_backend_slept_us() == 10000U);

    CHECK(reset(&time, &base) == 0);
    target.at_us = base + 20000U;
    deadline.at_us = base + 10000U;
    s_cancel_on_poll = 3U;
    CHECK(hk_time_sleep_until(
        s_owner, &time, target, deadline, &cancel) == HK_ERR_CANCELLED);
    CHECK(time_normative_backend_slept_us() == 10000U);

    CHECK(reset(&time, &base) == 0);
    target.at_us = base + 10000U;
    deadline.at_us = target.at_us;
    s_cancel_on_poll = 3U;
    CHECK(hk_time_sleep_until(
        s_owner, &time, target, deadline, &cancel) == HK_OK);

    CHECK(reset(&time, &base) == 0);
    target.at_us = base + 1U;
    deadline.at_us = target.at_us;
    time_normative_backend_set_freeze(1U);
    CHECK(hk_time_sleep_until(
        s_owner, &time, target, deadline, NULL) == HK_ERR_INTERNAL);
    CHECK(hk_time_now_us(s_owner, &time, &now) == HK_ERR_INVALID_STATE);

    CHECK(reset(&time, &base) == 0);
    CHECK(hk_time_now_us(s_owner, &time, &now) == HK_OK && now == base);
    time_normative_backend_set_now(base - 1U);
    CHECK(hk_time_now_us(s_owner, &time, &now) == HK_ERR_INTERNAL);
    CHECK(hk_time_release(
        s_owner, HK_DEADLINE_IMMEDIATE, &time) == HK_OK);

    /* Keep the near-UINT64_MAX clock case last: the real K210 provider's
       process-lifetime monotonic guard intentionally cannot be reset. */
    CHECK(reset(&time, &base) == 0);
    CHECK(hk_time_deadline_after_us(
        s_owner, &time, HK_TIME_MAX_SLEEP_US + 1U,
        &deadline) == HK_ERR_LIMIT);
    time_normative_backend_set_now(UINT64_MAX - 4U);
    CHECK(hk_time_deadline_after_us(
        s_owner, &time, 4U, &deadline) == HK_ERR_LIMIT);

    printf("TIME_NORMATIVE_OK backend=%s cases=10 max_slice_us=%llu\n",
           time_normative_backend_name(),
           (unsigned long long)HK_TIME_CANCEL_PROBE_MAX_US);
    return 0;
}

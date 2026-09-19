#include <hackylens/capability/input.h>

#include <stdio.h>
#include <string.h>

#include "../firmware/src/capabilities/capability_provider.h"
#include "input_normative_backend.h"

#define CHECK(condition)                                                     \
    do                                                                       \
    {                                                                        \
        if(!(condition))                                                     \
        {                                                                    \
            printf("INPUT_NORMATIVE_FAIL backend=%s line=%d\n",          \
                   input_normative_backend_name(), __LINE__);              \
            return 1;                                                        \
        }                                                                    \
    } while(0)

static hk_capability_core_t s_core;
static hk_owner_t s_owner_a;
static hk_owner_t s_owner_b;
static const hk_capability_provider_t *s_provider_ref;
static const hk_capability_info_t s_inventory = {
    sizeof(hk_capability_info_t), HK_CAPABILITY_INFO_VERSION,
    HK_CAPABILITY_ID_INPUT, {0U, 1U, 0U, 0U}, HK_INPUT_FEATURES_0_1,
    HK_CAPABILITY_FLAG_SHARED, 0U, 0U, NULL, 0U, 0U,
};
static const hk_capability_grant_t s_grant = {
    .request = HK_INPUT_REQUEST_0_1_INIT,
};

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

static int accepted(uint64_t start_us, uint32_t raw)
{
    CHECK(input_normative_backend_sample(start_us, raw) == HK_OK);
    CHECK(input_normative_backend_sample(start_us + 10000U, raw) == HK_OK);
    CHECK(input_normative_backend_sample(start_us + 20000U, raw) == HK_OK);
    return 0;
}

int main(void)
{
    hk_input_t input_a;
    hk_input_t input_b;
    hk_input_t input_c;
    hk_input_event_t event;
    hk_input_info_t info;
    hk_owner_t owner_c;
    uint32_t state;
    uint64_t now = 0U;

    input_normative_backend_reset();
    s_provider_ref = input_normative_backend_provider();
    CHECK(s_provider_ref != NULL);
    CHECK(hk_capability_core_init(
        &s_core, &s_inventory, &s_provider_ref, 1U) == HK_OK);
    CHECK(hk_capability_core_owner_open(
        &s_core, &s_grant, 1U, &s_owner_a) == HK_OK);
    CHECK(hk_capability_core_owner_open(
        &s_core, &s_grant, 1U, &s_owner_b) == HK_OK);
    CHECK(hk_input_acquire(s_owner_a, &s_grant.request, &input_a) == HK_OK);
    CHECK(hk_input_acquire(s_owner_b, &s_grant.request, &input_b) == HK_OK);
    CHECK(hk_input_get_info(s_owner_a, &input_a, &info) == HK_OK);
    CHECK(info.supported_buttons == HK_INPUT_BUTTON_ALL &&
          info.sample_interval_us == 10000U &&
          info.debounce_interval_us == 20000U &&
          info.event_capacity == 8U);

    /* Bounce does not become a stable edge. */
    now = 10000U;
    CHECK(input_normative_backend_sample(now, HK_INPUT_BUTTON_LEFT) == HK_OK);
    now += 10000U;
    CHECK(input_normative_backend_sample(now, 0U) == HK_OK);
    now += 10000U;
    CHECK(input_normative_backend_sample(now, HK_INPUT_BUTTON_LEFT) == HK_OK);
    now += 10000U;
    CHECK(input_normative_backend_sample(now, HK_INPUT_BUTTON_LEFT) == HK_OK);
    now += 10000U;
    CHECK(input_normative_backend_sample(now, HK_INPUT_BUTTON_LEFT) == HK_OK);
    CHECK(hk_input_next_event(s_owner_a, &input_a, &event) == HK_OK);
    CHECK(event.sequence == 1U && event.timestamp_us == now &&
          event.state == HK_INPUT_BUTTON_LEFT &&
          event.changed == HK_INPUT_BUTTON_LEFT &&
          event.pressed == HK_INPUT_BUTTON_LEFT && event.released == 0U);
    CHECK(hk_input_next_event(s_owner_a, &input_a, &event) == HK_PENDING);

    /* A held state never repeats, while the other lease sees the same edge. */
    now += 10000U;
    CHECK(input_normative_backend_sample(now, HK_INPUT_BUTTON_LEFT) == HK_OK);
    CHECK(hk_input_next_event(s_owner_a, &input_a, &event) == HK_PENDING);
    CHECK(hk_input_next_event(s_owner_b, &input_b, &event) == HK_OK);
    CHECK(event.sequence == 1U && event.pressed == HK_INPUT_BUTTON_LEFT);

    now += 10000U;
    CHECK(accepted(now, 0U) == 0);
    now += 20000U;
    CHECK(hk_input_next_event(s_owner_a, &input_a, &event) == HK_OK);
    CHECK(event.released == HK_INPUT_BUTTON_LEFT && event.pressed == 0U);

    now += 10000U;
    CHECK(accepted(
        now, HK_INPUT_BUTTON_LEFT | HK_INPUT_BUTTON_OK) == 0);
    now += 20000U;
    CHECK(hk_input_next_event(s_owner_a, &input_a, &event) == HK_OK);
    CHECK(event.changed == (HK_INPUT_BUTTON_LEFT | HK_INPUT_BUTTON_OK) &&
          event.pressed == event.changed);
    CHECK(hk_input_get_state(s_owner_b, &input_b, &state) == HK_OK);
    CHECK(state == (HK_INPUT_BUTTON_LEFT | HK_INPUT_BUTTON_OK));

    /* Leave owner B behind and overwrite its unread history. */
    for(uint32_t index = 0U; index < 9U; index++)
    {
        uint32_t raw = (index & 1U) ? HK_INPUT_BUTTON_BACK : 0U;
        now += 10000U;
        CHECK(accepted(now, raw) == 0);
        now += 20000U;
        CHECK(hk_input_next_event(s_owner_a, &input_a, &event) == HK_OK);
    }
    CHECK(hk_input_get_state(s_owner_a, &input_a, &state) == HK_OK);
    CHECK(hk_input_next_event(s_owner_b, &input_b, &event) == HK_ERR_OVERFLOW);
    CHECK(event.dropped == 11U && event.state == state);
    CHECK(hk_input_next_event(s_owner_b, &input_b, &event) == HK_PENDING);

    CHECK(hk_capability_core_validate_lease(
        &s_core, s_owner_a, &input_a.lease, HK_CAPABILITY_ID_INPUT,
        1U, NULL) == HK_ERR_WRONG_CONTEXT);
    CHECK(hk_input_release(
        s_owner_a, (hk_deadline_t){UINT64_MAX}, &input_a) ==
        HK_ERR_INVALID_ARGUMENT);
    CHECK(hk_input_next_event(s_owner_a, &input_a, &event) == HK_PENDING);

    /* Closing an owner cleans up its live lease and retires the old handle. */
    CHECK(hk_capability_core_owner_close(
        &s_core, s_owner_a, 0U, HK_DEADLINE_IMMEDIATE) == HK_OK);
    CHECK(hk_input_get_state(s_owner_a, &input_a, &state) ==
          HK_ERR_STALE_HANDLE);
    CHECK(hk_capability_core_owner_open(
        &s_core, &s_grant, 1U, &owner_c) == HK_OK);
    CHECK(hk_input_acquire(owner_c, &s_grant.request, &input_c) == HK_OK);
    CHECK(hk_input_get_state(owner_c, &input_c, &state) == HK_OK);
    CHECK(hk_input_get_state(s_owner_b, &input_b, &event.state) == HK_OK);
    CHECK(state == event.state);
    CHECK(hk_input_release(
        owner_c, HK_DEADLINE_IMMEDIATE, &input_c) == HK_OK);
    CHECK(hk_input_get_state(s_owner_b, &input_b, &state) == HK_OK);
    CHECK(hk_input_release(
        s_owner_b, HK_DEADLINE_IMMEDIATE, &input_b) == HK_OK);

    printf("INPUT_NORMATIVE_OK backend=%s events=12 capacity=%u dropped=11\n",
           input_normative_backend_name(), (unsigned)HK_INPUT_EVENT_CAPACITY);
    return 0;
}

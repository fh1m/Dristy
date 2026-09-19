#include "input_normative_backend.h"

#include <hackylens/capability/input.h>

#include "../firmware/src/capabilities/input_provider.h"
#include "../firmware/src/capabilities/input_state.h"

static hk_input_state_t s_state;

static hk_result_t fake_open(void *context, const hk_lease_t *lease)
{
    return hk_input_state_open_cursor((hk_input_state_t *)context, lease);
}

static hk_result_t fake_close(void *context, const hk_lease_t *lease)
{
    return hk_input_state_close_cursor((hk_input_state_t *)context, lease);
}

static hk_result_t fake_info(void *context, hk_input_info_t *info)
{
    (void)context;
    if(!info)
        return HK_ERR_INVALID_ARGUMENT;
    *info = (hk_input_info_t){
        sizeof(*info), HK_INPUT_INFO_VERSION, HK_INPUT_BUTTON_ALL,
        HK_INPUT_SAMPLE_INTERVAL_US, HK_INPUT_DEBOUNCE_INTERVAL_US,
        HK_INPUT_EVENT_CAPACITY, 0U,
    };
    return HK_OK;
}

static hk_result_t fake_state(void *context, uint32_t *state)
{
    return hk_input_state_get((hk_input_state_t *)context, state);
}

static hk_result_t fake_event(
    void *context, const hk_lease_t *lease, hk_input_event_t *event)
{
    return hk_input_state_next_event(
        (hk_input_state_t *)context, lease, event);
}

static hk_input_provider_t s_input_provider = {
    .context = &s_state,
    .open_cursor = fake_open,
    .close_cursor = fake_close,
    .get_info = fake_info,
    .get_state = fake_state,
    .next_event = fake_event,
};

static const hk_capability_provider_t s_provider = {
    .context = &s_input_provider,
    .max_leases = 16U,
};

const hk_capability_provider_t *input_normative_backend_provider(void)
{
    return &s_provider;
}

const char *input_normative_backend_name(void)
{
    return "fake";
}

void input_normative_backend_reset(void)
{
    hk_input_state_reset(&s_state);
    (void)hk_input_state_sample(&s_state, 0U, 0U);
}

hk_result_t input_normative_backend_sample(
    uint64_t timestamp_us, uint32_t raw_state)
{
    return hk_input_state_sample(&s_state, timestamp_us, raw_state);
}

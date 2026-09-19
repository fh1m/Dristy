#include "../../../firmware/src/capabilities/capability_provider.h"
#include "../../../firmware/src/capabilities/input_provider.h"
#include "../../../firmware/src/capabilities/input_state.h"
#include "../../../firmware/src/drivers/hk_input.h"

#include <hackylens/capability/input.h>

#include "../hal/hal_time.h"

typedef struct
{
    hk_input_state_t state;
} k210_input_state_t;

static k210_input_state_t s_input;

static hk_result_t k210_input_service(k210_input_state_t *input)
{
    uint64_t now;
    hk_result_t result;

    if(!input)
        return HK_ERR_INVALID_ARGUMENT;
    now = hal_time_us();
    if(input->state.initialized && now < input->state.next_sample_us)
        return HK_OK;
    result = hk_input_state_sample(
        &input->state, now, buttons_read_pressed_mask());
    return result == HK_PENDING ? HK_OK : result;
}

static hk_result_t k210_input_open_cursor(
    void *context, const hk_lease_t *lease)
{
    k210_input_state_t *input = (k210_input_state_t *)context;
    hk_result_t result = k210_input_service(input);

    if(result != HK_OK)
        return result;
    return hk_input_state_open_cursor(&input->state, lease);
}

static hk_result_t k210_input_close_cursor(
    void *context, const hk_lease_t *lease)
{
    k210_input_state_t *input = (k210_input_state_t *)context;

    if(!input)
        return HK_ERR_INVALID_ARGUMENT;
    return hk_input_state_close_cursor(&input->state, lease);
}

static hk_result_t k210_input_get_info(
    void *context, hk_input_info_t *info)
{
    (void)context;
    if(!info)
        return HK_ERR_INVALID_ARGUMENT;
    *info = (hk_input_info_t){
        sizeof(hk_input_info_t), HK_INPUT_INFO_VERSION,
        HK_INPUT_BUTTON_ALL, HK_INPUT_SAMPLE_INTERVAL_US,
        HK_INPUT_DEBOUNCE_INTERVAL_US, HK_INPUT_EVENT_CAPACITY, 0U,
    };
    return HK_OK;
}

static hk_result_t k210_input_get_state(void *context, uint32_t *state)
{
    k210_input_state_t *input = (k210_input_state_t *)context;
    hk_result_t result = k210_input_service(input);

    if(result != HK_OK)
        return result;
    return hk_input_state_get(&input->state, state);
}

static hk_result_t k210_input_next_event(
    void *context, const hk_lease_t *lease, hk_input_event_t *event)
{
    k210_input_state_t *input = (k210_input_state_t *)context;
    hk_result_t result = k210_input_service(input);

    if(result != HK_OK)
        return result;
    return hk_input_state_next_event(&input->state, lease, event);
}

static hk_input_provider_t s_input_provider = {
    .context = &s_input,
    .open_cursor = k210_input_open_cursor,
    .close_cursor = k210_input_close_cursor,
    .get_info = k210_input_get_info,
    .get_state = k210_input_get_state,
    .next_event = k210_input_next_event,
};

const hk_capability_provider_t hk_k210_input_provider = {
    .context = &s_input_provider,
    .max_leases = 16U,
};

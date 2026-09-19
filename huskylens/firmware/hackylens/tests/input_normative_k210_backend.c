#include "input_normative_backend.h"

#include <hackylens/capability/input.h>

#include "../firmware/src/capabilities/input_provider.h"

extern const hk_capability_provider_t hk_k210_input_provider;

static uint64_t s_now_us;
static uint32_t s_raw_state;

uint64_t hal_time_us(void)
{
    return s_now_us;
}

uint32_t buttons_read_pressed_mask(void)
{
    return s_raw_state;
}

const hk_capability_provider_t *input_normative_backend_provider(void)
{
    return &hk_k210_input_provider;
}

const char *input_normative_backend_name(void)
{
    return "k210";
}

void input_normative_backend_reset(void)
{
    s_now_us = 0U;
    s_raw_state = 0U;
}

hk_result_t input_normative_backend_sample(
    uint64_t timestamp_us, uint32_t raw_state)
{
    const hk_capability_provider_t *provider =
        input_normative_backend_provider();
    hk_input_provider_t *input = (hk_input_provider_t *)provider->context;
    uint32_t ignored;

    s_now_us = timestamp_us;
    s_raw_state = raw_state;
    return input->get_state(input->context, &ignored);
}

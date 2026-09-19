#include "../../../firmware/src/capabilities/capability_provider.h"
#include "../../../firmware/src/capabilities/time_provider.h"
#include <hackylens/capability/time.h>

#include <atomic.h>
#include <sleep.h>

#include "../hal/hal_time.h"

typedef struct
{
    spinlock_t lock;
    uint64_t last_us;
    uint8_t observed;
} k210_time_state_t;

static k210_time_state_t s_time_state = {
    .lock = SPINLOCK_INIT,
};

static hk_result_t k210_time_now(void *context, uint64_t *value)
{
    k210_time_state_t *state = (k210_time_state_t *)context;
    uint64_t now;

    if(!state || !value)
        return HK_ERR_INVALID_ARGUMENT;
    spinlock_lock(&state->lock);
    now = hal_time_us();
    if(state->observed && now < state->last_us)
    {
        spinlock_unlock(&state->lock);
        return HK_ERR_INTERNAL;
    }
    state->last_us = now;
    state->observed = 1U;
    *value = now;
    spinlock_unlock(&state->lock);
    return HK_OK;
}

static hk_result_t k210_time_sleep(void *context, uint64_t duration_us)
{
    (void)context;
    if(duration_us == 0U || duration_us > HK_TIME_CANCEL_PROBE_MAX_US)
        return HK_ERR_INVALID_ARGUMENT;
    usleep(duration_us);
    return HK_OK;
}

static hk_time_provider_t s_time_provider = {
    .context = &s_time_state,
    .now_us = k210_time_now,
    .sleep_us = k210_time_sleep,
    .max_sleep_us = HK_TIME_MAX_SLEEP_US,
    .max_slice_us = (uint32_t)HK_TIME_CANCEL_PROBE_MAX_US,
};

const hk_capability_provider_t hk_k210_time_provider = {
    .context = &s_time_provider,
    .max_leases = 16U,
};

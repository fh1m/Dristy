#include "time_normative_backend.h"

#include <string.h>

#include <hackylens/capability/time.h>

#include "../firmware/src/capabilities/time_provider.h"

typedef struct
{
    uint64_t now_us;
    uint64_t last_us;
    uint64_t slept_us;
    uint32_t sleep_calls;
    uint8_t observed;
    uint8_t freeze;
} fake_time_t;

static fake_time_t s_fake;
static uint64_t s_reset_generation;

static hk_result_t fake_now(void *context, uint64_t *value)
{
    fake_time_t *fake = (fake_time_t *)context;

    if(!fake || !value)
        return HK_ERR_INVALID_ARGUMENT;
    if(fake->observed && fake->now_us < fake->last_us)
        return HK_ERR_INTERNAL;
    fake->last_us = fake->now_us;
    fake->observed = 1U;
    *value = fake->now_us;
    return HK_OK;
}

static hk_result_t fake_sleep(void *context, uint64_t duration_us)
{
    fake_time_t *fake = (fake_time_t *)context;

    if(!fake || duration_us == 0U ||
       duration_us > HK_TIME_CANCEL_PROBE_MAX_US)
        return HK_ERR_INVALID_ARGUMENT;
    fake->sleep_calls++;
    fake->slept_us += duration_us;
    if(!fake->freeze)
        fake->now_us += duration_us;
    return HK_OK;
}

static hk_time_provider_t s_time_provider = {
    .context = &s_fake,
    .now_us = fake_now,
    .sleep_us = fake_sleep,
    .max_sleep_us = HK_TIME_MAX_SLEEP_US,
    .max_slice_us = (uint32_t)HK_TIME_CANCEL_PROBE_MAX_US,
};

static const hk_capability_provider_t s_provider = {
    .context = &s_time_provider,
    .max_leases = 16U,
};

const hk_capability_provider_t *time_normative_backend_provider(void)
{
    return &s_provider;
}

const char *time_normative_backend_name(void)
{
    return "fake";
}

uint64_t time_normative_backend_reset(void)
{
    uint64_t base = UINT64_C(1000000) +
        s_reset_generation++ * UINT64_C(100000000);

    memset(&s_fake, 0, sizeof(s_fake));
    s_fake.now_us = base;
    return base;
}

void time_normative_backend_set_now(uint64_t now_us)
{
    s_fake.now_us = now_us;
}

void time_normative_backend_set_freeze(uint8_t freeze)
{
    s_fake.freeze = freeze;
}

uint32_t time_normative_backend_sleep_calls(void)
{
    return s_fake.sleep_calls;
}

uint64_t time_normative_backend_slept_us(void)
{
    return s_fake.slept_us;
}

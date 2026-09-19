#include "time_normative_backend.h"

#include "../firmware/src/capabilities/time_provider.h"

extern const hk_capability_provider_t hk_k210_time_provider;

unsigned int g_test_lock_depth;
unsigned int g_test_lock_calls;
unsigned int g_test_unlock_calls;
unsigned int g_test_lock_violations;

static uint64_t s_now_us;
static uint64_t s_slept_us;
static uint64_t s_reset_generation;
static uint32_t s_sleep_calls;
static uint8_t s_freeze;

uint64_t hal_time_us(void)
{
    return s_now_us;
}

void usleep(uint64_t duration_us)
{
    s_sleep_calls++;
    s_slept_us += duration_us;
    if(!s_freeze)
        s_now_us += duration_us;
}

const hk_capability_provider_t *time_normative_backend_provider(void)
{
    return &hk_k210_time_provider;
}

const char *time_normative_backend_name(void)
{
    return "k210";
}

uint64_t time_normative_backend_reset(void)
{
    s_now_us = UINT64_C(1000000) +
        s_reset_generation++ * UINT64_C(100000000);
    s_slept_us = 0U;
    s_sleep_calls = 0U;
    s_freeze = 0U;
    g_test_lock_depth = 0U;
    g_test_lock_calls = 0U;
    g_test_unlock_calls = 0U;
    g_test_lock_violations = 0U;
    return s_now_us;
}

void time_normative_backend_set_now(uint64_t now_us)
{
    s_now_us = now_us;
}

void time_normative_backend_set_freeze(uint8_t freeze)
{
    s_freeze = freeze;
}

uint32_t time_normative_backend_sleep_calls(void)
{
    return s_sleep_calls;
}

uint64_t time_normative_backend_slept_us(void)
{
    return s_slept_us;
}

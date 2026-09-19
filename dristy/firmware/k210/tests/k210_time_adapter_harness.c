#include <stdint.h>
#include <stdio.h>

#include "../firmware/src/capabilities/capability_provider.h"
#include "../firmware/src/capabilities/time_provider.h"

extern const hk_capability_provider_t hk_k210_time_provider;

unsigned int g_test_lock_depth;
unsigned int g_test_lock_calls;
unsigned int g_test_unlock_calls;
unsigned int g_test_lock_violations;

static uint64_t g_fake_now = 100U;
static unsigned int g_clock_reads;
static unsigned int g_clock_reads_outside_lock;
static uint64_t g_last_sleep_us;

uint64_t hal_time_us(void)
{
    if(g_test_lock_depth != 1U)
        g_clock_reads_outside_lock++;
    g_clock_reads++;
    g_fake_now += 10U;
    return g_fake_now;
}

void usleep(uint64_t duration_us)
{
    g_last_sleep_us = duration_us;
}

#define CHECK(condition)                                                      \
    do                                                                        \
    {                                                                         \
        if(!(condition))                                                      \
        {                                                                     \
            fprintf(stderr, "check failed: %s:%d: %s\n",                    \
                    __FILE__, __LINE__, #condition);                          \
            return 1;                                                         \
        }                                                                     \
    } while(0)

int main(void)
{
    hk_time_provider_t *time =
        (hk_time_provider_t *)hk_k210_time_provider.context;
    uint64_t first = 0U;
    uint64_t second = 0U;

    CHECK(time != NULL);
    CHECK(time->now_us(time->context, &first) == HK_OK);
    CHECK(time->now_us(time->context, &second) == HK_OK);
    CHECK(first == 110U);
    CHECK(second == 120U);
    CHECK(g_clock_reads == 2U);
    CHECK(g_clock_reads_outside_lock == 0U);
    CHECK(g_test_lock_calls == 2U);
    CHECK(g_test_unlock_calls == 2U);
    CHECK(g_test_lock_depth == 0U);
    CHECK(g_test_lock_violations == 0U);
    CHECK(time->sleep_us(time->context, 5000U) == HK_OK);
    CHECK(g_last_sleep_us == 5000U);

    printf("K210_TIME_ANY_CORE_ORDER_OK reads=%u locks=%u\n",
           g_clock_reads, g_test_lock_calls);
    return 0;
}

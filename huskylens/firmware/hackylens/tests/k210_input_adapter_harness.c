#include <hackylens/capability/input.h>

#include <stdio.h>

#include "../firmware/src/capabilities/capability_provider.h"
#include "../firmware/src/capabilities/input_provider.h"

#define CHECK(condition)                                                     \
    do                                                                       \
    {                                                                        \
        if(!(condition))                                                     \
        {                                                                    \
            printf("K210_INPUT_FAIL line=%d\n", __LINE__);                \
            return 1;                                                        \
        }                                                                    \
    } while(0)

extern const hk_capability_provider_t hk_k210_input_provider;

static uint64_t s_now;
static uint32_t s_raw;
static uint32_t s_reads;

uint64_t hal_time_us(void)
{
    return s_now;
}

uint32_t buttons_read_pressed_mask(void)
{
    s_reads++;
    return s_raw;
}

int main(void)
{
    hk_input_provider_t *provider =
        (hk_input_provider_t *)hk_k210_input_provider.context;
    hk_lease_t lease = {
        0U, 1U, {1U, 1U}, HK_CAPABILITY_ID_INPUT,
    };
    hk_input_event_t event;
    uint32_t state;

    CHECK(provider && provider->open_cursor && provider->get_state &&
          provider->next_event);
    CHECK(provider->open_cursor(provider->context, &lease) == HK_OK);
    CHECK(s_reads == 1U);

    s_raw = HK_INPUT_BUTTON_LEFT;
    s_now = 1000U;
    CHECK(provider->get_state(provider->context, &state) == HK_OK);
    CHECK(s_reads == 1U && state == 0U);
    s_now = 10000U;
    CHECK(provider->get_state(provider->context, &state) == HK_OK);
    CHECK(s_reads == 2U && state == 0U);
    s_now = 20000U;
    CHECK(provider->get_state(provider->context, &state) == HK_OK);
    CHECK(s_reads == 3U && state == 0U);
    s_now = 30000U;
    CHECK(provider->get_state(provider->context, &state) == HK_OK);
    CHECK(s_reads == 4U && state == HK_INPUT_BUTTON_LEFT);
    CHECK(provider->next_event(
        provider->context, &lease, &event) == HK_OK);
    CHECK(s_reads == 4U && event.timestamp_us == 30000U &&
          event.pressed == HK_INPUT_BUTTON_LEFT);

    printf("K210_INPUT_SAMPLING_OK reads=4 accepted_us=30000\n");
    return 0;
}

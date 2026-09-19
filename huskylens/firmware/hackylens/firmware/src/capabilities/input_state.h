#ifndef HK_INPUT_STATE_H
#define HK_INPUT_STATE_H

#include <hackylens/capability/input.h>

#define HK_INPUT_CURSOR_CAPACITY 32U

typedef struct
{
    uint32_t generation;
    uint64_t next_sequence;
    uint8_t active;
} hk_input_cursor_t;

typedef struct
{
    hk_input_event_t events[HK_INPUT_EVENT_CAPACITY];
    hk_input_cursor_t cursors[HK_INPUT_CURSOR_CAPACITY];
    uint64_t sequence;
    uint64_t next_sample_us;
    uint64_t candidate_since_us;
    uint32_t stable_state;
    uint32_t candidate_state;
    uint8_t initialized;
} hk_input_state_t;

void hk_input_state_reset(hk_input_state_t *state);
hk_result_t hk_input_state_sample(
    hk_input_state_t *state, uint64_t timestamp_us, uint32_t raw_state);
hk_result_t hk_input_state_open_cursor(
    hk_input_state_t *state, const hk_lease_t *lease);
hk_result_t hk_input_state_close_cursor(
    hk_input_state_t *state, const hk_lease_t *lease);
hk_result_t hk_input_state_get(const hk_input_state_t *state, uint32_t *value);
hk_result_t hk_input_state_next_event(
    hk_input_state_t *state,
    const hk_lease_t *lease,
    hk_input_event_t *event);

#endif

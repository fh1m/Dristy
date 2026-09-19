#include "input_state.h"

#include <limits.h>
#include <string.h>

static hk_result_t validate_lease_slot(
    const hk_lease_t *lease, hk_input_cursor_t **cursor,
    hk_input_state_t *state)
{
    if(!lease || !state || lease->slot >= HK_INPUT_CURSOR_CAPACITY ||
       lease->generation == 0U)
        return HK_ERR_INVALID_ARGUMENT;
    if(cursor)
        *cursor = &state->cursors[lease->slot];
    return HK_OK;
}

void hk_input_state_reset(hk_input_state_t *state)
{
    if(state)
        memset(state, 0, sizeof(*state));
}

hk_result_t hk_input_state_sample(
    hk_input_state_t *state, uint64_t timestamp_us, uint32_t raw_state)
{
    uint32_t changed;
    hk_input_event_t *event;

    if(!state || timestamp_us == UINT64_MAX ||
       (raw_state & ~HK_INPUT_BUTTON_ALL) != 0U)
        return HK_ERR_INVALID_ARGUMENT;
    if(!state->initialized)
    {
        state->stable_state = raw_state;
        state->candidate_state = raw_state;
        state->candidate_since_us = timestamp_us;
        state->next_sample_us = timestamp_us + HK_INPUT_SAMPLE_INTERVAL_US;
        state->initialized = 1U;
        return HK_OK;
    }
    if(timestamp_us < state->candidate_since_us)
        return HK_ERR_INTERNAL;
    if(timestamp_us < state->next_sample_us)
        return HK_PENDING;
    if(timestamp_us > UINT64_MAX - HK_INPUT_SAMPLE_INTERVAL_US)
        return HK_ERR_LIMIT;
    state->next_sample_us = timestamp_us + HK_INPUT_SAMPLE_INTERVAL_US;
    if(raw_state != state->candidate_state)
    {
        state->candidate_state = raw_state;
        state->candidate_since_us = timestamp_us;
        return HK_OK;
    }
    if(raw_state == state->stable_state ||
       timestamp_us - state->candidate_since_us <
           HK_INPUT_DEBOUNCE_INTERVAL_US)
        return HK_OK;
    if(state->sequence == UINT64_MAX)
        return HK_ERR_LIMIT;

    changed = raw_state ^ state->stable_state;
    state->stable_state = raw_state;
    state->sequence++;
    event = &state->events[(state->sequence - 1U) % HK_INPUT_EVENT_CAPACITY];
    event->sequence = state->sequence;
    event->timestamp_us = timestamp_us;
    event->state = raw_state;
    event->changed = changed;
    event->pressed = changed & raw_state;
    event->released = changed & ~raw_state & HK_INPUT_BUTTON_ALL;
    event->dropped = 0U;
    return HK_OK;
}

hk_result_t hk_input_state_open_cursor(
    hk_input_state_t *state, const hk_lease_t *lease)
{
    hk_input_cursor_t *cursor;
    hk_result_t result = validate_lease_slot(lease, &cursor, state);

    if(result != HK_OK)
        return result;
    if(state->sequence == UINT64_MAX)
        return HK_ERR_LIMIT;
    cursor->generation = lease->generation;
    cursor->next_sequence = state->sequence + 1U;
    cursor->active = 1U;
    return HK_OK;
}

hk_result_t hk_input_state_close_cursor(
    hk_input_state_t *state, const hk_lease_t *lease)
{
    hk_input_cursor_t *cursor;
    hk_result_t result = validate_lease_slot(lease, &cursor, state);

    if(result != HK_OK)
        return result;
    if(cursor->active && cursor->generation == lease->generation)
        memset(cursor, 0, sizeof(*cursor));
    return HK_OK;
}

hk_result_t hk_input_state_get(const hk_input_state_t *state, uint32_t *value)
{
    if(!state || !value)
        return HK_ERR_INVALID_ARGUMENT;
    if(!state->initialized)
        return HK_ERR_INVALID_STATE;
    *value = state->stable_state;
    return HK_OK;
}

hk_result_t hk_input_state_next_event(
    hk_input_state_t *state,
    const hk_lease_t *lease,
    hk_input_event_t *event)
{
    hk_input_cursor_t *cursor;
    uint64_t oldest;
    hk_result_t result = validate_lease_slot(lease, &cursor, state);

    if(result != HK_OK || !event)
        return result != HK_OK ? result : HK_ERR_INVALID_ARGUMENT;
    memset(event, 0, sizeof(*event));
    if(!cursor->active || cursor->generation != lease->generation)
        return HK_ERR_STALE_HANDLE;
    if(cursor->next_sequence > state->sequence)
        return HK_PENDING;

    oldest = state->sequence >= HK_INPUT_EVENT_CAPACITY ?
             state->sequence - HK_INPUT_EVENT_CAPACITY + 1U : 1U;
    if(cursor->next_sequence < oldest)
    {
        uint64_t dropped = state->sequence - cursor->next_sequence + 1U;

        event->sequence = state->sequence;
        event->state = state->stable_state;
        event->timestamp_us = state->sequence ?
            state->events[(state->sequence - 1U) % HK_INPUT_EVENT_CAPACITY]
                .timestamp_us : 0U;
        event->dropped = dropped > UINT32_MAX ? UINT32_MAX : (uint32_t)dropped;
        cursor->next_sequence = state->sequence + 1U;
        return HK_ERR_OVERFLOW;
    }
    *event = state->events[
        (cursor->next_sequence - 1U) % HK_INPUT_EVENT_CAPACITY];
    cursor->next_sequence++;
    return HK_OK;
}

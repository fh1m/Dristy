#ifndef HK_INPUT_PROVIDER_H
#define HK_INPUT_PROVIDER_H

#include <hackylens/capability/input.h>

typedef hk_result_t (*hk_input_provider_cursor_fn)(
    void *context, const hk_lease_t *lease);
typedef hk_result_t (*hk_input_provider_info_fn)(
    void *context, hk_input_info_t *info);
typedef hk_result_t (*hk_input_provider_state_fn)(
    void *context, uint32_t *state);
typedef hk_result_t (*hk_input_provider_event_fn)(
    void *context, const hk_lease_t *lease, hk_input_event_t *event);

typedef struct
{
    void *context;
    hk_input_provider_cursor_fn open_cursor;
    hk_input_provider_cursor_fn close_cursor;
    hk_input_provider_info_fn get_info;
    hk_input_provider_state_fn get_state;
    hk_input_provider_event_fn next_event;
    uint32_t reserved;
} hk_input_provider_t;

#endif

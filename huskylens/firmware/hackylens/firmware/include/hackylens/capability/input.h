#ifndef HACKYLENS_CAPABILITY_INPUT_H
#define HACKYLENS_CAPABILITY_INPUT_H

#include "owner.h"

#ifdef __cplusplus
extern "C" {
#endif

#define HK_CAPABILITY_ID_INPUT UINT32_C(0x00010002)

#define HK_INPUT_FEATURE_STATE (UINT64_C(1) << 0)
#define HK_INPUT_FEATURE_EVENTS (UINT64_C(1) << 1)
#define HK_INPUT_FEATURE_DEBOUNCED_BUTTONS (UINT64_C(1) << 2)
#define HK_INPUT_FEATURES_0_1                                      \
    (HK_INPUT_FEATURE_STATE | HK_INPUT_FEATURE_EVENTS |            \
     HK_INPUT_FEATURE_DEBOUNCED_BUTTONS)

#define HK_INPUT_BUTTON_LEFT UINT32_C(0x01)
#define HK_INPUT_BUTTON_OK UINT32_C(0x02)
#define HK_INPUT_BUTTON_RIGHT UINT32_C(0x04)
#define HK_INPUT_BUTTON_BACK UINT32_C(0x08)
#define HK_INPUT_BUTTON_ALL                                        \
    (HK_INPUT_BUTTON_LEFT | HK_INPUT_BUTTON_OK |                   \
     HK_INPUT_BUTTON_RIGHT | HK_INPUT_BUTTON_BACK)

#define HK_INPUT_SAMPLE_INTERVAL_US UINT32_C(10000)
#define HK_INPUT_DEBOUNCE_INTERVAL_US UINT32_C(20000)
#define HK_INPUT_EVENT_CAPACITY UINT16_C(8)
#define HK_INPUT_INFO_VERSION 1U

#define HK_INPUT_REQUEST_0_1_INIT                                  \
    {                                                              \
        sizeof(hk_capability_request_t), HK_CAPABILITY_REQUEST_VERSION, \
        HK_CAPABILITY_ID_INPUT, {0U, 1U, 0U, 0U},                  \
        {0U, 2U, 0U, 0U}, HK_INPUT_FEATURES_0_1, 0U, 0U           \
    }

typedef struct
{
    uint16_t struct_size;
    uint16_t struct_version;
    uint32_t supported_buttons;
    uint32_t sample_interval_us;
    uint32_t debounce_interval_us;
    uint16_t event_capacity;
    uint16_t reserved;
} hk_input_info_t;

typedef struct
{
    uint64_t sequence;
    uint64_t timestamp_us;
    uint32_t state;
    uint32_t changed;
    uint32_t pressed;
    uint32_t released;
    uint32_t dropped;
} hk_input_event_t;

HK_DECLARE_CAPABILITY_HANDLE(hk_input_t);

hk_result_t hk_input_acquire(
    hk_owner_t owner,
    const hk_capability_request_t *request,
    hk_input_t *handle);
hk_result_t hk_input_release(
    hk_owner_t owner,
    hk_deadline_t deadline,
    hk_input_t *handle);
hk_result_t hk_input_get_info(
    hk_owner_t owner,
    const hk_input_t *handle,
    hk_input_info_t *info);
hk_result_t hk_input_get_state(
    hk_owner_t owner,
    const hk_input_t *handle,
    uint32_t *state);
hk_result_t hk_input_next_event(
    hk_owner_t owner,
    const hk_input_t *handle,
    hk_input_event_t *event);

#ifdef __cplusplus
}
#endif

#endif

#ifndef HACKYLENS_CAPABILITY_LIGHTS_H
#define HACKYLENS_CAPABILITY_LIGHTS_H

#include "owner.h"

#ifdef __cplusplus
extern "C" {
#endif

#define HK_CAPABILITY_ID_LIGHTS UINT32_C(0x00010005)

#define HK_LIGHTS_CHANNEL_BACKLIGHT (UINT32_C(1) << 0)
#define HK_LIGHTS_CHANNEL_ILLUMINATION (UINT32_C(1) << 1)
#define HK_LIGHTS_CHANNEL_RGB (UINT32_C(1) << 2)
#define HK_LIGHTS_CHANNEL_ALL                                      \
    (HK_LIGHTS_CHANNEL_BACKLIGHT | HK_LIGHTS_CHANNEL_ILLUMINATION | \
     HK_LIGHTS_CHANNEL_RGB)

#define HK_LIGHTS_FEATURE_BACKLIGHT (UINT64_C(1) << 0)
#define HK_LIGHTS_FEATURE_ILLUMINATION (UINT64_C(1) << 1)
#define HK_LIGHTS_FEATURE_RGB (UINT64_C(1) << 2)
#define HK_LIGHTS_FEATURES_0_1                                    \
    (HK_LIGHTS_FEATURE_BACKLIGHT | HK_LIGHTS_FEATURE_ILLUMINATION | \
     HK_LIGHTS_FEATURE_RGB)

#define HK_LIGHTS_LEVEL_MAX UINT16_C(1000)
#define HK_LIGHTS_INFO_VERSION 1U

#define HK_LIGHTS_REQUEST_0_1_INIT                                \
    {                                                              \
        sizeof(hk_capability_request_t), HK_CAPABILITY_REQUEST_VERSION, \
        HK_CAPABILITY_ID_LIGHTS, {0U, 1U, 0U, 0U},                 \
        {0U, 2U, 0U, 0U}, HK_LIGHTS_FEATURES_0_1, 0U, 0U          \
    }

typedef struct
{
    uint16_t struct_size;
    uint16_t struct_version;
    uint32_t supported_channels;
    uint16_t maximum_level;
    uint16_t reserved;
} hk_lights_info_t;

HK_DECLARE_CAPABILITY_HANDLE(hk_lights_t);

hk_result_t hk_lights_acquire(
    hk_owner_t owner,
    const hk_capability_request_t *request,
    uint32_t channels,
    hk_lights_t *handle);
hk_result_t hk_lights_release(
    hk_owner_t owner,
    hk_deadline_t deadline,
    hk_lights_t *handle);
hk_result_t hk_lights_get_info(
    hk_owner_t owner,
    const hk_lights_t *handle,
    hk_lights_info_t *info);
hk_result_t hk_lights_set_level(
    hk_owner_t owner,
    const hk_lights_t *handle,
    uint32_t channel,
    uint16_t level,
    hk_deadline_t deadline,
    const hk_cancel_t *cancel);
hk_result_t hk_lights_set_rgb(
    hk_owner_t owner,
    const hk_lights_t *handle,
    uint16_t red,
    uint16_t green,
    uint16_t blue,
    hk_deadline_t deadline,
    const hk_cancel_t *cancel);

#ifdef __cplusplus
}
#endif

#endif

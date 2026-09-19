#ifndef HK_LIGHTS_PROVIDER_H
#define HK_LIGHTS_PROVIDER_H

#include <hackylens/capability/lights.h>

typedef hk_result_t (*hk_lights_provider_open_fn)(
    void *context, const hk_lease_t *lease, uint32_t channels);
typedef hk_result_t (*hk_lights_provider_close_fn)(
    void *context, const hk_lease_t *lease, hk_deadline_t deadline);
typedef hk_result_t (*hk_lights_provider_info_fn)(
    void *context, hk_lights_info_t *info);
typedef hk_result_t (*hk_lights_provider_level_fn)(
    void *context, const hk_lease_t *lease, uint32_t channel,
    uint16_t level, hk_deadline_t deadline, const hk_cancel_t *cancel);
typedef hk_result_t (*hk_lights_provider_rgb_fn)(
    void *context, const hk_lease_t *lease, uint16_t red,
    uint16_t green, uint16_t blue, hk_deadline_t deadline,
    const hk_cancel_t *cancel);

typedef struct
{
    void *context;
    hk_lights_provider_open_fn open_channels;
    hk_lights_provider_close_fn close_channels;
    hk_lights_provider_info_fn get_info;
    hk_lights_provider_level_fn set_level;
    hk_lights_provider_rgb_fn set_rgb;
    uint32_t reserved;
} hk_lights_provider_t;

#endif

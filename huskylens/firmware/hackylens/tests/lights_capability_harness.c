#include <hackylens/capability/lights.h>

#include <stdio.h>
#include <string.h>

#include "../firmware/src/capabilities/capability_provider.h"
#include "lights_normative_backend.h"

#define CHECK(condition)                                                     \
    do                                                                       \
    {                                                                        \
        if(!(condition))                                                     \
        {                                                                    \
            printf("LIGHTS_NORMATIVE_FAIL backend=%s line=%d\n",          \
                   lights_normative_backend_name(), __LINE__);               \
            return 1;                                                        \
        }                                                                    \
    } while(0)

static hk_capability_core_t s_core;
static const hk_capability_provider_t *s_provider_ref;
static const hk_capability_info_t s_inventory = {
    sizeof(hk_capability_info_t), HK_CAPABILITY_INFO_VERSION,
    HK_CAPABILITY_ID_LIGHTS, {0U, 1U, 0U, 0U}, HK_LIGHTS_FEATURES_0_1,
    HK_CAPABILITY_FLAG_SHARED, 0U, 0U, NULL, 0U, 0U,
};
static const hk_capability_grant_t s_grant = {
    .request = HK_LIGHTS_REQUEST_0_1_INIT,
};

hk_result_t capability_owner_runtime_acquire(
    hk_owner_t owner, const hk_capability_request_t *request,
    hk_capability_id_t expected_type, hk_lease_t *lease)
{
    return hk_capability_core_acquire(
        &s_core, owner, request, expected_type, 0U, lease);
}

hk_result_t capability_owner_runtime_release(
    hk_owner_t owner, hk_capability_id_t expected_type,
    hk_deadline_t deadline, hk_lease_t *lease)
{
    return hk_capability_core_release(
        &s_core, owner, expected_type, 0U, deadline, lease);
}

hk_result_t capability_owner_runtime_validate(
    hk_owner_t owner, const hk_lease_t *lease,
    hk_capability_id_t expected_type, void **provider_context)
{
    return hk_capability_core_validate_lease(
        &s_core, owner, lease, expected_type, 0U, provider_context);
}

hk_result_t capability_owner_runtime_quarantine(
    hk_owner_t owner, const hk_lease_t *lease,
    hk_capability_id_t expected_type)
{
    return hk_capability_core_quarantine_lease(
        &s_core, owner, lease, expected_type, 0U);
}

static uint8_t cancelled(const void *context)
{
    return *(const uint8_t *)context;
}

int main(void)
{
    hk_capability_request_t request = HK_LIGHTS_REQUEST_0_1_INIT;
    hk_owner_t owner_a;
    hk_owner_t owner_b;
    hk_owner_t owner_c;
    hk_owner_t owner_d;
    hk_owner_t owner_e;
    hk_lights_t illumination;
    hk_lights_t rgb;
    hk_lights_t backlight;
    hk_lights_t same_owner_backlight;
    hk_lights_t replacement;
    hk_lights_t stale_rgb;
    hk_lights_info_t info;
    uint8_t cancel_flag = 1U;
    hk_cancel_t cancel = {cancelled, &cancel_flag};
    hk_capability_grant_t illumination_grant = s_grant;
    hk_capability_request_t illumination_request = request;
    hk_capability_request_t short_request = request;
    uint32_t effects;

    lights_normative_backend_reset(100U);
    s_provider_ref = lights_normative_backend_provider();
    CHECK(s_provider_ref != NULL);
    CHECK(hk_capability_core_init(
        &s_core, &s_inventory, &s_provider_ref, 1U) == HK_OK);
    CHECK(hk_capability_core_owner_open(
        &s_core, &s_grant, 1U, &owner_a) == HK_OK);
    CHECK(hk_capability_core_owner_open(
        &s_core, &s_grant, 1U, &owner_b) == HK_OK);
    CHECK(hk_capability_core_owner_open(
        &s_core, &s_grant, 1U, &owner_c) == HK_OK);
    CHECK(hk_capability_core_owner_open(
        &s_core, &s_grant, 1U, &owner_d) == HK_OK);
    illumination_grant.request.required_features =
        HK_LIGHTS_FEATURE_ILLUMINATION;
    illumination_request.required_features =
        HK_LIGHTS_FEATURE_ILLUMINATION;
    CHECK(hk_capability_core_owner_open(
        &s_core, &illumination_grant, 1U, &owner_e) == HK_OK);
    CHECK(hk_lights_acquire(
        owner_e, &illumination_request, HK_LIGHTS_CHANNEL_RGB,
        &replacement) == HK_ERR_NOT_DECLARED);

    CHECK(hk_lights_acquire(
        owner_a, &request, HK_LIGHTS_CHANNEL_ILLUMINATION,
        &illumination) == HK_OK);
    CHECK(hk_lights_acquire(
        owner_b, &request, HK_LIGHTS_CHANNEL_RGB, &rgb) == HK_OK);
    CHECK(hk_lights_acquire(
        owner_c, &request,
        HK_LIGHTS_CHANNEL_BACKLIGHT | HK_LIGHTS_CHANNEL_ILLUMINATION,
        &backlight) == HK_ERR_BUSY);
    CHECK(hk_lights_acquire(
        owner_c, &request, HK_LIGHTS_CHANNEL_BACKLIGHT,
        &backlight) == HK_OK);
    CHECK(hk_lights_get_info(owner_a, &illumination, &info) == HK_OK);
    CHECK(info.supported_channels == HK_LIGHTS_CHANNEL_ALL &&
          info.maximum_level == HK_LIGHTS_LEVEL_MAX);

    CHECK(hk_lights_set_level(
        owner_b, &illumination, HK_LIGHTS_CHANNEL_ILLUMINATION, 500U,
        HK_DEADLINE_IMMEDIATE, NULL) == HK_ERR_WRONG_OWNER);
    CHECK(hk_lights_set_level(
        owner_a, &illumination, HK_LIGHTS_CHANNEL_BACKLIGHT, 500U,
        HK_DEADLINE_IMMEDIATE, NULL) == HK_ERR_WRONG_OWNER);
    CHECK(hk_lights_set_level(
        owner_a, &illumination, HK_LIGHTS_CHANNEL_ILLUMINATION, 1001U,
        HK_DEADLINE_IMMEDIATE, NULL) == HK_ERR_INVALID_ARGUMENT);
    CHECK(hk_lights_set_level(
        owner_a, &illumination, HK_LIGHTS_CHANNEL_ILLUMINATION, 500U,
        HK_DEADLINE_IMMEDIATE, &cancel) == HK_ERR_CANCELLED);
    CHECK(hk_lights_set_level(
        owner_a, &illumination, HK_LIGHTS_CHANNEL_ILLUMINATION, 500U,
        (hk_deadline_t){100U}, NULL) == HK_ERR_DEADLINE_EXCEEDED);
    CHECK(lights_normative_backend_effect_count() == 0U);

    cancel_flag = 0U;
    CHECK(hk_lights_set_level(
        owner_a, &illumination, HK_LIGHTS_CHANNEL_ILLUMINATION, 500U,
        HK_DEADLINE_IMMEDIATE, &cancel) == HK_OK);
    CHECK(hk_lights_set_rgb(
        owner_b, &rgb, 1000U, 500U, 1U,
        HK_DEADLINE_IMMEDIATE, NULL) == HK_OK);
    CHECK(hk_lights_set_level(
        owner_c, &backlight, HK_LIGHTS_CHANNEL_BACKLIGHT, 900U,
        (hk_deadline_t){101U}, NULL) == HK_OK);
    CHECK(lights_normative_backend_effect_count() == 3U);
    CHECK(lights_normative_backend_active_mask() == HK_LIGHTS_CHANNEL_ALL);

    effects = lights_normative_backend_effect_count();
    CHECK(hk_lights_release(
        owner_c, (hk_deadline_t){100U}, &backlight) ==
        HK_ERR_DEADLINE_EXCEEDED);
    CHECK(lights_normative_backend_effect_count() == effects &&
          (lights_normative_backend_active_mask() &
           HK_LIGHTS_CHANNEL_BACKLIGHT) != 0U &&
          !hk_lease_is_zero(&backlight.lease));
    CHECK(hk_lights_release(
        owner_c, HK_DEADLINE_IMMEDIATE, &backlight) == HK_OK);
    CHECK(lights_normative_backend_effect_count() == effects + 1U &&
          (lights_normative_backend_active_mask() &
           HK_LIGHTS_CHANNEL_BACKLIGHT) == 0U &&
          hk_lease_is_zero(&backlight.lease));

    CHECK(hk_lights_acquire(
        owner_a, &request, HK_LIGHTS_CHANNEL_BACKLIGHT,
        &same_owner_backlight) == HK_OK);
    CHECK(hk_lights_release(
        owner_a, HK_DEADLINE_IMMEDIATE, &same_owner_backlight) == HK_OK);
    CHECK(hk_lights_set_level(
        owner_a, &illumination, HK_LIGHTS_CHANNEL_ILLUMINATION, 600U,
        HK_DEADLINE_IMMEDIATE, NULL) == HK_OK);
    CHECK(hk_lights_release(
        owner_a, HK_DEADLINE_IMMEDIATE, &illumination) == HK_OK);
    CHECK((lights_normative_backend_active_mask() &
           HK_LIGHTS_CHANNEL_ILLUMINATION) == 0U);
    CHECK((lights_normative_backend_safe_off_mask() &
           HK_LIGHTS_CHANNEL_ILLUMINATION) != 0U);

    stale_rgb = rgb;
    CHECK(hk_capability_core_owner_close(
        &s_core, owner_b, 0U, HK_DEADLINE_IMMEDIATE) == HK_OK);
    CHECK((lights_normative_backend_active_mask() &
           HK_LIGHTS_CHANNEL_RGB) == 0U);
    CHECK(hk_lights_set_rgb(
        owner_b, &stale_rgb, 1U, 1U, 1U,
        HK_DEADLINE_IMMEDIATE, NULL) == HK_ERR_STALE_HANDLE);
    CHECK(hk_lights_acquire(
        owner_d, &request, HK_LIGHTS_CHANNEL_RGB, &replacement) == HK_OK);
    CHECK(hk_lights_set_rgb(
        owner_d, &replacement, 25U, 50U, 75U,
        HK_DEADLINE_IMMEDIATE, NULL) == HK_OK);
    CHECK((lights_normative_backend_active_mask() &
           HK_LIGHTS_CHANNEL_RGB) != 0U);
    CHECK(hk_lights_release(
        owner_d, HK_DEADLINE_IMMEDIATE, &replacement) == HK_OK);
    CHECK((lights_normative_backend_active_mask() &
           HK_LIGHTS_CHANNEL_RGB) == 0U);

    CHECK(hk_lights_acquire(
        owner_d, &request, 0U, &replacement) == HK_ERR_INVALID_ARGUMENT);
    CHECK(hk_lights_acquire(
        owner_d, &request, UINT32_C(0x80),
        &replacement) == HK_ERR_INVALID_ARGUMENT);
    short_request.struct_size = (uint16_t)(sizeof(short_request) - 1U);
    CHECK(hk_lights_acquire(
        owner_d, &short_request, HK_LIGHTS_CHANNEL_RGB,
        &replacement) == HK_ERR_INVALID_ARGUMENT);

    CHECK(hk_lights_acquire(
        owner_e, &illumination_request, HK_LIGHTS_CHANNEL_ILLUMINATION,
        &illumination) == HK_OK);
    effects = lights_normative_backend_effect_count();
    CHECK(hk_capability_core_owner_close(
        &s_core, owner_e, 0U, (hk_deadline_t){100U}) == HK_ERR_INTERNAL);
    CHECK(lights_normative_backend_effect_count() == effects);
    CHECK(hk_lights_set_level(
        owner_e, &illumination, HK_LIGHTS_CHANNEL_ILLUMINATION, 1U,
        HK_DEADLINE_IMMEDIATE, NULL) == HK_ERR_STALE_HANDLE);
    CHECK(hk_lights_acquire(
        owner_c, &request, HK_LIGHTS_CHANNEL_BACKLIGHT,
        &replacement) == HK_ERR_INVALID_STATE);
    CHECK(lights_normative_backend_safe_off_mask() == HK_LIGHTS_CHANNEL_ALL);

    printf("LIGHTS_NORMATIVE_OK backend=%s cases=16 effects=%u "
           "safe_off_mask=0x%X level_max=%u\n",
           lights_normative_backend_name(),
           (unsigned)lights_normative_backend_effect_count(),
           (unsigned)lights_normative_backend_safe_off_mask(),
           (unsigned)HK_LIGHTS_LEVEL_MAX);
    return 0;
}

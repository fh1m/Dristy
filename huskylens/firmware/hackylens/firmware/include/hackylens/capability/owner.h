#ifndef HACKYLENS_CAPABILITY_OWNER_H
#define HACKYLENS_CAPABILITY_OWNER_H

#include "common.h"

#ifdef __cplusplus
extern "C" {
#endif

#define HK_OWNER_NONE ((hk_owner_t){0U, 0U})
#define HK_LEASE_NONE \
    ((hk_lease_t){0U, 0U, {0U, 0U}, 0U})

/* Every public typed capability handle uses this exact one-token shape. */
#define HK_DECLARE_CAPABILITY_HANDLE(type_name) \
    typedef struct type_name                    \
    {                                            \
        hk_lease_t lease;                        \
    } type_name

static inline uint8_t hk_owner_is_zero(hk_owner_t owner)
{
    return (uint8_t)(owner.slot == 0U && owner.generation == 0U);
}

static inline uint8_t hk_lease_is_zero(const hk_lease_t *lease)
{
    return (uint8_t)(lease && lease->slot == 0U && lease->generation == 0U &&
                     hk_owner_is_zero(lease->owner) && lease->capability_id == 0U);
}

#ifdef __cplusplus
}
#endif

#endif

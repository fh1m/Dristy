#ifndef HK_CAPABILITY_INVENTORY_BINDING_H
#define HK_CAPABILITY_INVENTORY_BINDING_H

#include "capability_provider.h"

/* Implemented only by build-generated capability_inventory_generated.c. */
void hk_generated_capability_inventory_get(
    const hk_capability_info_t **inventory,
    const hk_capability_provider_t *const **providers,
    uint16_t *count);
const hk_capability_grant_t *hk_generated_capability_grants_for(
    const char *consumer_id,
    uint16_t *count);

#endif

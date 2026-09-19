#include "../firmware/src/capabilities/capability_inventory_binding.h"

#include <stddef.h>

void hk_generated_capability_inventory_get(
    const hk_capability_info_t **inventory,
    const hk_capability_provider_t *const **providers,
    uint16_t *count)
{
    if(inventory)
        *inventory = NULL;
    if(providers)
        *providers = NULL;
    if(count)
        *count = 0U;
}

const hk_capability_grant_t *hk_generated_capability_grants_for(
    const char *consumer_id,
    uint16_t *count)
{
    (void)consumer_id;
    if(count)
        *count = 0U;
    return NULL;
}

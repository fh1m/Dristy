#ifndef HK_CAPABILITY_OWNER_RUNTIME_H
#define HK_CAPABILITY_OWNER_RUNTIME_H

#include <hackylens/capability/inventory.h>
#include <hackylens/capability/owner.h>

#include "../core/hk_capability_client.h"
#include "../capabilities/capability_core_binding.h"

struct hk_app;

hk_result_t capability_owner_runtime_enter(const struct hk_app *app);
hk_result_t capability_owner_runtime_exit(const struct hk_app *app);
hk_owner_t capability_owner_runtime_current(const struct hk_app *app);
hk_result_t capability_owner_runtime_initialize(void);
#endif

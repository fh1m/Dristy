#include "capability_provider.h"

/*
 * Owner lifecycle is implemented in capability_core.c so validation and lease
 * invalidation share one state machine. This translation unit is retained as
 * the stable owner-module boundary for later generated grant wiring.
 */
typedef int hk_capability_owner_module_anchor_t;

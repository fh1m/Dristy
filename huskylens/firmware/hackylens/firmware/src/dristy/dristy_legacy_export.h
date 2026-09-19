#ifndef DRISTY_LEGACY_EXPORT_H
#define DRISTY_LEGACY_EXPORT_H

#include "dristy_modes.h"
#include "dristy_result_bus.h"

/* Pull latest results from running HackyLens apps into the result bus snapshot.
 * Safe to call when apps are inactive (no-op). */
void dristy_legacy_export_into_snap(dristy_result_snapshot_t *snap, dristy_mode_t mode);

#endif

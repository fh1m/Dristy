#include "dristy_result_bus.h"

#include <string.h>

/* ---------------------------------------------------------------------------
 * Double-buffered result bus
 *
 * Two snapshots: "back" is being written by the pipeline, "front" is the
 * latest committed result readable by DLP/LCD/host.
 *
 * Swap is a single atomic pointer exchange. No locks needed because:
 * - Only ONE writer (pipeline on core 0/1, serialised by frame sequence)
 * - Multiple readers are safe because they read a stable "front" buffer
 *   that is not being modified
 *
 * The __attribute__((aligned(64))) ensures DMA-safe alignment and avoids
 * cache line tearing on the K210's dual-core system.
 * ------------------------------------------------------------------------- */

static dristy_result_snapshot_t g_snapshots[2] __attribute__((aligned(64)));
static volatile uint8_t g_front_index;  /* 0 or 1 */
static volatile uint8_t g_committed;    /* 1 after first commit */

void dristy_result_bus_init(void)
{
    memset(g_snapshots, 0, sizeof(g_snapshots));
    g_front_index = 0;
    g_committed = 0;
}

dristy_result_snapshot_t *dristy_result_bus_begin_write(void)
{
    /* Write to whichever buffer is NOT the front */
    uint8_t back = g_front_index ^ 1;
    memset(&g_snapshots[back], 0, sizeof(g_snapshots[0]));
    return &g_snapshots[back];
}

void dristy_result_bus_commit(void)
{
    __sync_synchronize();  /* ensure all writes to back buffer are visible */
    g_front_index ^= 1;   /* atomic swap: back becomes front */
    g_committed = 1;
    __sync_synchronize();
}

const dristy_result_snapshot_t *dristy_result_bus_read(void)
{
    if(!g_committed)
        return 0;
    __sync_synchronize();
    return &g_snapshots[g_front_index];
}

uint32_t dristy_result_bus_frame_number(void)
{
    if(!g_committed)
        return 0;
    __sync_synchronize();
    return g_snapshots[g_front_index].frame_number;
}

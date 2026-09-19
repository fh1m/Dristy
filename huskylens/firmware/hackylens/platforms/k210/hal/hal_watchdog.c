#include "hal_watchdog.h"

#include <stddef.h>

#include "sysctl.h"
#include "wdt.h"

static int hal_watchdog_expired(void *context)
{
    (void)context;
    /* A wedged core-1 native call cannot be unwound safely from an interrupt.
     * Do not acknowledge the first-stage interrupt: WDT1 performs a hardware
     * reset on its second timeout and preserves the watchdog reset cause. */
    for(;;)
    {
    }
    return 0;
}

void hal_watchdog_force_reset(uint64_t timeout_ms)
{
    (void)wdt_init(WDT_DEVICE_1, timeout_ms, hal_watchdog_expired, NULL);
    /* Keep core 0 out of mutable firmware state until the first interrupt and
     * subsequent hardware reset. */
    for(;;)
    {
    }
}

uint8_t hal_watchdog_reset_detected(void)
{
    return sysctl_get_reset_status() == SYSCTL_RESET_STATUS_WDT1;
}

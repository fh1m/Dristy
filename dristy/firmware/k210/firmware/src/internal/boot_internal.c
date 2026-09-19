#include "boot_internal.h"

#include "../../../platforms/k210/hal/hal_watchdog.h"

uint8_t boot_internal_watchdog_reset_detected(void)
{
    return hal_watchdog_reset_detected();
}

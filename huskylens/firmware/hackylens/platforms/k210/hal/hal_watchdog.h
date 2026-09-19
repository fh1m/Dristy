#ifndef HK_HAL_WATCHDOG_H
#define HK_HAL_WATCHDOG_H

#include <stdint.h>

/* WDT1 is reserved for the optional MicroPython runtime's fatal fallback. */
void hal_watchdog_force_reset(uint64_t timeout_ms);
uint8_t hal_watchdog_reset_detected(void);

#endif

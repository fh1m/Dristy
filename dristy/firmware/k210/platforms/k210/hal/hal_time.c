#include "hal_time.h"

#include <sleep.h>
#include <sysctl.h>

uint64_t hal_time_us(void)
{
    return sysctl_get_time_us();
}

void hal_sleep_ms(uint32_t ms)
{
    msleep(ms);
}

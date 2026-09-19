#include "hal_system.h"

#include <sysctl.h>

void hal_system_init_clocks(void)
{
    sysctl_pll_set_freq(SYSCTL_PLL0, 800000000);
}

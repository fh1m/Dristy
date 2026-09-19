#include "platform_bootstrap.h"

#include <stdio.h>

#include "../../../firmware/src/internal/hk_board_port.h"
#include "../../../firmware/src/drivers/lcd_st7789_transport.h"
#include "../hal/hal_system.h"

void platform_bootstrap_init_clocks(void)
{
    hal_system_init_clocks();
    hk_board_ops.early_init();
}

void platform_bootstrap_init_hardware(void)
{
    hk_board_ops.display_prepare();
    lcd_st7789_transport_prepare();
    hk_board_ops.lights_prepare();
    hk_board_ops.buttons_prepare();

    printf("[LCD] init original sequence\r\n");
    lcd_st7789_transport_init();
}

#include "firmware/src/runtime/hk_main.h"
#include "firmware/src/runtime/firmware_startup.h"

#include "firmware/src/controllers/debug_controller.h"
#include "firmware/src/controllers/system_tick_controller.h"

int main(void)
{
    static const hk_main_hooks_t hooks = {
        .startup = firmware_startup,
        .debug_tick = debug_uart_tick,
        .system_tick = system_tick_controller_tick,
    };

    hk_main_set_hooks(&hooks);
    return hk_main();
}

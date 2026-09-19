#include "system_tick_controller.h"

#include "../core/hk_app.h"
#include "../core/hk_app_registry.h"
#include "../core/hk_events.h"

#include "../core/hk_dispatch.h"
#include "../core/hk_screen.h"
#include "../services/settings_persistence.h"
#include "../services/external_link_service.h"
#include "../services/sd_service.h"
#include "debug_controller.h"
#include "hk_config.h"
#if HK_ENABLE_APP_MICROPYTHON
#include "../adapters/micropython/micropython_capability_bridge.h"
#include "../services/micropython_runtime.h"
#endif
#include "hk_config.h"
#if HK_ENABLE_DRISTY
#include "../dristy/dristy_boot.h"
#include "../dristy/dristy_uart_bridge.h"
#include "../dristy/dristy_pipeline.h"
#include "../dristy/dristy_host_mode.h"
#include "../dristy/dristy_led.h"
#endif
void system_tick_controller_tick(const hk_input_snapshot_t *input)
{
#if HK_ENABLE_DRISTY
    (void)dristy_pipeline_tick();
    dristy_boot_sync_from_pipeline();
    dristy_uart_bridge_tick();
    dristy_host_mode_service();
    dristy_led_tick();
#endif
#if HK_ENABLE_APP_MICROPYTHON
    micropython_capability_bridge_tick();
    micropython_runtime_poll();
#endif
    external_link_service_tick();
    debug_uart_tick();
    hk_app_registry_background_tick(input);
    if(hk_app_registry_sd_poll_allowed(hk_screen_get()))
    {
        hk_sd_event_t sd_event = sd_service_tick();
        if(sd_event != HK_SD_EVENT_NONE)
            shell_handle_sd_event(sd_event);
    }
    settings_storage_tick();
}

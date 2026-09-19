#include "firmware_startup.h"
#include "capability_owner_runtime.h"

#include <stdio.h>

#include "../../../platforms/k210/startup/platform_bootstrap.h"

#include "../controllers/boot_controller.h"
#include "../controllers/autostart_controller.h"
#include "../core/hk_menu.h"
#include "../core/hk_screen.h"
#include "../services/settings_lights.h"
#include "../services/debug_console_service.h"
#include "../services/external_link_service.h"
#include "../services/settings_persistence.h"
#include "../services/settings_service.h"
#include "../storage/file_mount.h"
#include "../ui/hk_ui.h"
#include "../ui/display_binding.h"
#include "hk_config.h"
#if HK_ENABLE_DRISTY
#include "../dristy/dristy_boot.h"
#endif

static void firmware_wake_from_sleep(void)
{
    if(hk_screen_get() != SCREEN_SLEEP)
        return;
    screen_brightness_apply();
    illum_led_apply();
    rgb_led_apply();
    printf("[SLEEP] wake\r\n");
    shell_show_menu();
}

static uint8_t firmware_capability_owner_enter(const hk_app_t *app)
{
    return (uint8_t)(capability_owner_runtime_enter(app) == HK_OK);
}

static void firmware_capability_owner_exit(const hk_app_t *app)
{
    (void)capability_owner_runtime_exit(app);
}

void firmware_startup(void)
{
    static const hk_menu_owner_hooks_t owner_hooks = {
        .enter = firmware_capability_owner_enter,
        .exit = firmware_capability_owner_exit,
    };

    menu_owner_hooks_set(&owner_hooks);
    debug_console_init();
    hk_screen_set_wake_handler(firmware_wake_from_sleep);
    platform_bootstrap_init_clocks();
    if(capability_owner_runtime_initialize() != HK_OK)
        printf("[CAPABILITY] owner initialization failed\r\n");
    settings_storage_init();
    platform_bootstrap_init_hardware();
    if(hk_ui_display_prepare() != HK_OK)
        printf("[DISPLAY] capability initialization failed\r\n");
    external_link_service_init(settings_external_link_transport());
#if HK_ENABLE_DRISTY
    dristy_boot_init();
#endif
    screen_brightness_apply();
    illum_led_apply();
    rgb_led_apply();
    boot_controller_startup();
    boot_controller_show_boot_screen();
    topbar_set_sd_mounted(file_mount_if_needed());
    autostart_controller_start();
    debug_console_start_rx();
}

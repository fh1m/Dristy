#include "autostart_controller.h"

#include <stdio.h>

#include "../core/hk_app.h"
#include "../core/hk_app_registry.h"
#include "../core/hk_menu.h"
#include "../config/input_config.h"
#include "../drivers/hk_input.h"
#include "../services/settings_service.h"
#include "hk_config.h"
#if HK_ENABLE_APP_MICROPYTHON
#include "hal_watchdog.h"
#endif

void autostart_controller_start(void)
{
    static const hk_input_snapshot_t startup_input = {0U, 0U, 0U};
    hk_autostart_id_t id = settings_autostart_id();
    const hk_app_t *app;

    if(buttons_read_pressed_mask() & BUTTON_BACK)
    {
        printf("[BOOT] autostart suppressed by BACK\r\n");
        shell_show_menu();
        return;
    }

    if(id == HK_AUTOSTART_OFF)
    {
        printf("[BOOT] autostart=OFF\r\n");
        shell_show_menu();
        return;
    }

#if HK_ENABLE_APP_MICROPYTHON
    if(id == HK_AUTOSTART_MICROPYTHON && hal_watchdog_reset_detected())
    {
        printf("[BOOT] MicroPython autostart suppressed after WDT1 reset\r\n");
        shell_show_menu();
        return;
    }
#endif

    app = hk_app_for_autostart_id(id);
    if(!app)
    {
        printf("[BOOT] autostart=%u unavailable, fallback=MENU\r\n", (unsigned)id);
        shell_show_menu();
        return;
    }

    printf("[BOOT] autostart=%u app=%s\r\n", (unsigned)id, app->title);
    if(!shell_open_app(app, &startup_input))
    {
        printf("[BOOT] autostart=%u open failed, fallback=MENU\r\n", (unsigned)id);
        shell_show_menu();
    }
}

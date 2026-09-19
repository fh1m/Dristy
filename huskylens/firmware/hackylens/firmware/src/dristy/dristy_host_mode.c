#include "dristy_host_mode.h"

#include <stddef.h>

#include "hk_config.h"

#if HK_ENABLE_DRISTY

#include "../apps/vision_mode/vision_mode_app.h"
#include "../apps/vision_mode/vision_mode_controller.h"
#include "../core/hk_app_registry.h"
#include "../core/hk_menu.h"
#include "../core/hk_screen.h"
#include "dristy_lcd.h"
#include "dristy_modes.h"

static volatile uint8_t g_pending;
static volatile dristy_mode_t g_pending_mode = DRISTY_MODE_INVALID;

void dristy_host_mode_request(dristy_mode_t mode)
{
    if(!dristy_mode_info(mode))
        return;
    g_pending_mode = mode;
    g_pending = 1U;
}

void dristy_host_mode_service(void)
{
    dristy_mode_t mode;
    screen_t screen;

    if(!g_pending)
        return;
    g_pending = 0U;
    mode = g_pending_mode;
    if(!dristy_mode_info(mode))
        return;
    if(dristy_lcd_state() == 0U)
        return;

    screen = hk_screen_get();
    if(screen == SCREEN_VISION_MODE &&
       vision_mode_controller_active_mode() == mode)
        return;

    if(screen == SCREEN_VISION_MODE)
        vision_mode_exit();
    else if(screen != SCREEN_MENU)
    {
        const hk_app_t *app = hk_app_for_screen(screen);

        if(app && app->exit)
            app->exit();
        hk_screen_set(SCREEN_MENU);
    }

    vision_mode_controller_set_pending_mode(mode);
    (void)shell_open_app(&g_vision_mode_app, NULL);
}

#endif /* HK_ENABLE_DRISTY */

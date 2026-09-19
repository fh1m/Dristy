#include "../core/hk_app_registry.h"

#include "../core/hk_app.h"

#include <stddef.h>

#include "hk_config.h"

#if HK_ENABLE_APP_TERMINAL
#include "terminal/terminal_app.h"
#endif
#if HK_ENABLE_APP_CAMERA
#include "camera/camera_app.h"
#endif
#if HK_ENABLE_APP_QR_CAMERA
#include "qr_camera/qr_camera_app.h"
#endif
#if HK_ENABLE_APP_FACE_DETECT
#include "face_detect/face_detect_app.h"
#endif
#if HK_ENABLE_APP_APRILTAG
#include "apriltag/apriltag_app.h"
#endif
#if HK_ENABLE_APP_OBJECT_DETECT
#include "object_detect/object_detect_app.h"
#endif
#if HK_ENABLE_APP_FILES
#include "files/files_app.h"
#endif
#if HK_ENABLE_APP_BUTTONS
#include "buttons/buttons_app.h"
#endif
#if HK_ENABLE_APP_PONG
#include "pong/pong_app.h"
#endif
#if HK_ENABLE_APP_SETTINGS
#include "settings/settings_app.h"
#endif
#if HK_ENABLE_APP_SLEEP
#include "sleep/sleep_app.h"
#endif
#if HK_ENABLE_APP_MICROPYTHON
#include "micropython/micropython_app.h"
#endif
#if HK_ENABLE_DRISTY
#include "vision_mode/vision_mode_app.h"
#endif

#if HK_ENABLE_DRISTY
const hk_app_t g_vision_mode_app = {
    .id = "vision_mode",
    .title = "VISION",
    .screen = SCREEN_VISION_MODE,
    .autostart_id = HK_AUTOSTART_OFF,
    .enter = vision_mode_enter,
    .exit = vision_mode_exit,
    .tick = vision_mode_tick,
    .handle_input = vision_mode_handle_buttons,
    .draw_icon = vision_mode_draw_icon,
    .blocks_sd_poll = 1U,
    .tick_interval_ms = 1U,
};
#endif

const hk_app_t g_menu_items[] = {
#if HK_ENABLE_APP_TERMINAL
    {.id = "terminal", .title = "TERMINAL", .screen = HK_TERMINAL_SCREEN,
     .autostart_id = HK_AUTOSTART_TERMINAL,
     .enter = terminal_enter, .exit = terminal_exit, .tick = terminal_tick,
     .handle_input = terminal_handle_buttons, .draw_icon = terminal_draw_icon},
#endif
#if HK_ENABLE_APP_CAMERA
    {.id = "camera", .title = "CAMERA", .screen = SCREEN_CAMERA,
     .autostart_id = HK_AUTOSTART_CAMERA,
     .enter = camera_enter, .exit = camera_exit, .tick = camera_tick,
     .handle_input = camera_handle_buttons, .owns_screen = camera_owns_screen,
     .draw_icon = camera_draw_icon, .blocks_sd_poll = 1U,
     .tick_interval_ms = 1U,
     .handle_debug_command = camera_handle_debug_command,
     .debug_help = g_camera_debug_help},
#endif
#if HK_ENABLE_APP_QR_CAMERA
    {.id = "qr_camera", .title = "QR-CAMERA", .screen = SCREEN_QR_CAMERA,
     .autostart_id = HK_AUTOSTART_QR_CAMERA,
     .enter = qr_camera_enter, .exit = qr_camera_exit, .tick = qr_camera_tick,
     .handle_input = qr_camera_handle_buttons, .owns_screen = qr_camera_owns_screen,
     .draw_icon = qr_camera_draw_icon, .blocks_sd_poll = 1U,
     .tick_interval_ms = 1U,
     .handle_debug_command = qr_camera_handle_debug_command,
     .debug_help = g_qr_camera_debug_help},
#endif
#if HK_ENABLE_APP_FACE_DETECT
    {.id = "face_detect", .title = "FACE DETECT", .screen = SCREEN_FACE_DETECT,
     .autostart_id = HK_AUTOSTART_FACE_DETECT,
     .enter = face_detect_enter, .exit = face_detect_exit, .tick = face_detect_tick,
     .handle_input = face_detect_handle_buttons, .draw_icon = face_detect_draw_icon,
     .background_tick = face_detect_background_tick, .blocks_sd_poll = 1U,
     .tick_interval_ms = 1U,
     .handle_debug_command = face_detect_handle_debug_command,
     .debug_help = g_face_detect_debug_help},
#endif
#if HK_ENABLE_APP_APRILTAG
    {.id = "apriltag", .title = "APRILTAG", .screen = SCREEN_APRILTAG,
     .autostart_id = HK_AUTOSTART_APRILTAG,
     .enter = apriltag_enter, .exit = apriltag_exit, .tick = apriltag_tick,
     .handle_input = apriltag_handle_buttons, .draw_icon = apriltag_draw_icon,
     .background_tick = apriltag_background_tick,
     .tick_interval_ms = 1U,
     .handle_debug_command = apriltag_handle_debug_command,
     .debug_help = g_apriltag_debug_help},
#endif
#if HK_ENABLE_APP_OBJECT_DETECT
    {.id = "object_detect", .title = "OBJECT DETECT",
     .screen = SCREEN_OBJECT_DETECT,
     .autostart_id = HK_AUTOSTART_OBJECT_DETECT,
     .enter = object_detect_enter, .exit = object_detect_exit,
     .tick = object_detect_tick,
     .handle_input = object_detect_handle_buttons,
     .draw_icon = object_detect_draw_icon,
     .background_tick = object_detect_background_tick,
     .blocks_sd_poll = 1U, .tick_interval_ms = 1U,
     .handle_debug_command = object_detect_handle_debug_command,
     .debug_help = g_object_detect_debug_help},
#endif
#if HK_ENABLE_APP_FILES
    {.id = "files", .title = "FILES", .screen = SCREEN_FILES,
     .autostart_id = HK_AUTOSTART_FILES,
     .enter = files_enter, .exit = files_exit, .tick = files_tick,
     .handle_input = files_handle_buttons, .draw_icon = files_draw_icon,
     .handle_sd_event = files_handle_sd_event},
#endif
#if HK_ENABLE_APP_BUTTONS
    {.id = "buttons", .title = "BUTTONS", .screen = SCREEN_BUTTONS,
     .autostart_id = HK_AUTOSTART_BUTTONS,
     .enter = buttons_enter, .tick = buttons_tick,
     .handle_input = buttons_handle_buttons,
     .draw_icon = buttons_draw_icon},
#endif
#if HK_ENABLE_APP_PONG
    {.id = "pong", .title = "PONG", .screen = HK_PONG_SCREEN,
     .autostart_id = HK_AUTOSTART_PONG,
     .enter = pong_enter, .tick = pong_tick, .handle_input = pong_handle_buttons,
     .draw_icon = pong_draw_icon},
#endif
#if HK_ENABLE_APP_SETTINGS
    {.id = "settings", .title = "SETTINGS", .screen = SCREEN_SETTINGS,
     .enter = settings_enter, .exit = settings_exit, .tick = settings_tick,
     .handle_input = settings_handle_buttons, .draw_icon = settings_draw_icon,
     .handle_debug_command = settings_handle_debug_command,
     .debug_help = g_settings_debug_help},
#endif
#if HK_ENABLE_APP_SLEEP
    {.id = "sleep", .title = "SLEEP", .screen = SCREEN_SLEEP,
     .enter = sleep_enter, .handle_input = sleep_handle_buttons,
     .draw_icon = sleep_draw_icon, .background_tick = sleep_background_tick,
     .blocks_sd_poll = 1U},
#endif
#if HK_ENABLE_APP_MICROPYTHON
    {.id = "micropython", .title = "MICRO-PYTHON",
     .screen = HK_MICROPYTHON_SCREEN,
     .autostart_id = HK_AUTOSTART_MICROPYTHON,
     .enter = micropython_enter, .exit = micropython_exit,
     .tick = micropython_tick, .handle_input = micropython_handle_buttons,
     .draw_icon = micropython_draw_icon,
     .background_tick = micropython_background_tick,
     .tick_interval_ms = 5U,
     .handle_debug_command = micropython_handle_debug_command,
     .debug_help = g_micropython_debug_help},
#endif
};

const uint8_t g_menu_item_count = (uint8_t)(sizeof(g_menu_items) / sizeof(g_menu_items[0]));

const hk_app_t *hk_app_for_screen(screen_t screen)
{
    for(uint8_t i = 0; i < g_menu_item_count; i++)
    {
        if(g_menu_items[i].screen == screen)
            return &g_menu_items[i];
    }
#if HK_ENABLE_DRISTY
    if(screen == SCREEN_VISION_MODE)
        return &g_vision_mode_app;
#endif
    for(uint8_t i = 0; i < g_menu_item_count; i++)
    {
        if(g_menu_items[i].owns_screen && g_menu_items[i].owns_screen(screen))
            return &g_menu_items[i];
    }
    return NULL;
}

const hk_app_t *hk_app_for_autostart_id(hk_autostart_id_t id)
{
    if(id <= HK_AUTOSTART_OFF || id >= HK_AUTOSTART_COUNT)
        return NULL;
    for(uint8_t i = 0; i < g_menu_item_count; i++)
    {
        if(g_menu_items[i].autostart_id == id)
            return &g_menu_items[i];
    }
    return NULL;
}

uint8_t hk_app_autostart_count(void)
{
    uint8_t count = 0U;

    for(uint8_t i = 0; i < g_menu_item_count; i++)
        count += g_menu_items[i].autostart_id != HK_AUTOSTART_OFF ? 1U : 0U;
    return count;
}

const hk_app_t *hk_app_autostart_at(uint8_t index)
{
    for(uint8_t i = 0; i < g_menu_item_count; i++)
    {
        if(g_menu_items[i].autostart_id == HK_AUTOSTART_OFF)
            continue;
        if(index == 0U)
            return &g_menu_items[i];
        index--;
    }
    return NULL;
}

void hk_app_registry_background_tick(const hk_input_snapshot_t *input)
{
    for(uint8_t i = 0; i < g_menu_item_count; i++)
    {
        if(g_menu_items[i].background_tick)
            g_menu_items[i].background_tick(input);
    }
}

void hk_app_registry_handle_sd_event(hk_sd_event_t event)
{
    for(uint8_t i = 0; i < g_menu_item_count; i++)
    {
        if(g_menu_items[i].handle_sd_event)
            g_menu_items[i].handle_sd_event(event);
    }
}

uint8_t hk_app_registry_sd_poll_allowed(screen_t screen)
{
    const hk_app_t *app = hk_app_for_screen(screen);
    return !app || !app->blocks_sd_poll;
}

uint8_t hk_app_registry_handle_debug_command(const char *cmd)
{
    for(uint8_t i = 0; i < g_menu_item_count; i++)
    {
        if(g_menu_items[i].handle_debug_command && g_menu_items[i].handle_debug_command(cmd))
            return 1;
    }
    return 0;
}

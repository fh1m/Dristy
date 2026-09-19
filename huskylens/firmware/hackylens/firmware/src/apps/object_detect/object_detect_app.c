#include "object_detect_app.h"

#include <stdio.h>
#include <string.h>

#include "../../core/hk_menu.h"
#include "../../core/hk_screen.h"
#include "../../core/hk_string.h"
#include "../../services/debug_console_service.h"
#include "object_detect_controller.h"
#include "object_detect_detector.h"
#include "object_detect_settings.h"
#include "object_detect_view.h"

const char g_object_detect_debug_help[] = "HKOBJECT/HKOBJECTINFO";

void object_detect_enter(const hk_input_snapshot_t *input)
{
    object_detect_controller_enter(input);
}

void object_detect_exit(void)
{
    object_detect_controller_exit();
}

void object_detect_tick(const hk_input_snapshot_t *input)
{
    object_detect_controller_tick(input);
}

void object_detect_handle_buttons(const hk_input_snapshot_t *input)
{
    object_detect_controller_handle_buttons(input);
}

void object_detect_background_tick(const hk_input_snapshot_t *input)
{
    (void)input;
    object_detect_controller_background_tick();
}

uint8_t object_detect_handle_debug_command(const char *cmd)
{
    char line[512];

    if(str_eq_ci(cmd, "HKOBJECT"))
    {
        if(hk_screen_get() != SCREEN_MENU)
            shell_show_menu();
        object_detect_enter(NULL);
        debug_console_write_text("HKOBJECT OK\r\n");
        return 1U;
    }
    if(!str_eq_ci(cmd, "HKOBJECTINFO"))
        return 0U;
    object_detect_detector_format_info(line, sizeof(line));
    {
        const object_detect_preferences_t *preferences =
            object_detect_settings_preferences();
        size_t length = strlen(line);

        while(length &&
              (line[length - 1U] == '\r' || line[length - 1U] == '\n'))
            line[--length] = '\0';
        snprintf(line + length, sizeof(line) - length,
                 " fps=%u light=%s rgb=%u/%u/%u\r\n",
                 preferences->fps_enabled,
                 preferences->light_mode ? "RGB" : "LED",
                 preferences->rgb_red,
                 preferences->rgb_green,
                 preferences->rgb_blue);
    }
    debug_console_write_text(line);
    return 1U;
}

void object_detect_draw_icon(uint16_t x, uint16_t y,
                             uint16_t color, uint16_t bg)
{
    object_detect_view_draw_icon(x, y, color, bg);
}

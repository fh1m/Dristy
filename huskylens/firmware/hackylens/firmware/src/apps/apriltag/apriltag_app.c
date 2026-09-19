#include "apriltag_app.h"

#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "../../core/hk_menu.h"
#include "../../core/hk_screen.h"
#include "../../core/hk_string.h"
#include "../../services/debug_console_service.h"
#include "apriltag_controller.h"
#include "apriltag_detector.h"
#include "apriltag_settings.h"
#include "apriltag_view.h"

const char g_apriltag_debug_help[] = "HKTAG/HKTAGINFO";

void apriltag_enter(const hk_input_snapshot_t *input) { apriltag_controller_enter(input); }
void apriltag_exit(void) { apriltag_controller_exit(); }
void apriltag_tick(const hk_input_snapshot_t *input) { apriltag_controller_tick(input); }
void apriltag_handle_buttons(const hk_input_snapshot_t *input) { apriltag_controller_handle_buttons(input); }
void apriltag_background_tick(const hk_input_snapshot_t *input)
{
    (void)input;
    apriltag_detector_service_tick();
}

uint8_t apriltag_handle_debug_command(const char *cmd)
{
    char line[512];

    if(str_eq_ci(cmd, "HKTAG"))
    {
        if(hk_screen_get() != SCREEN_MENU)
            shell_show_menu();
        apriltag_enter(NULL);
        debug_console_write_text("HKTAG OK\r\n");
        return 1;
    }
    if(!str_eq_ci(cmd, "HKTAGINFO"))
        return 0;
    apriltag_detector_format_info(line, sizeof(line));
    {
        size_t length = strlen(line);
        const apriltag_preferences_t *preferences = apriltag_settings_preferences();

        while(length && (line[length - 1U] == '\r' || line[length - 1U] == '\n'))
            line[--length] = '\0';
        snprintf(line + length, sizeof(line) - length,
                 " output=%s selected=%u target=%d fps=%u light=%s rgb=%u/%u/%u\r\n",
                 apriltag_settings_output_label(),
                 (unsigned)apriltag_settings_selected_count(),
                 (int)apriltag_controller_target_id(),
                 preferences->fps_enabled,
                 preferences->light_mode ? "RGB" : "LED",
                 preferences->rgb_red, preferences->rgb_green, preferences->rgb_blue);
    }
    debug_console_write_text(line);
    return 1;
}

void apriltag_draw_icon(uint16_t x, uint16_t y, uint16_t color, uint16_t bg)
{
    apriltag_view_draw_icon(x, y, color, bg);
}

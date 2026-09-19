#include "face_detect_app.h"

#include <stddef.h>

#include "../../core/hk_string.h"
#include "../../services/debug_console_service.h"
#include "face_detect_controller.h"
#include "face_detect_detector.h"
#include "face_detect_view.h"

const char g_face_detect_debug_help[] = "HKFACEINFO";

void face_detect_enter(const hk_input_snapshot_t *input) { face_detect_controller_enter(input); }
void face_detect_exit(void) { face_detect_controller_exit(); }
void face_detect_tick(const hk_input_snapshot_t *input) { face_detect_controller_tick(input); }
void face_detect_handle_buttons(const hk_input_snapshot_t *input) { face_detect_controller_handle_buttons(input); }
void face_detect_background_tick(const hk_input_snapshot_t *input)
{
    (void)input;
    face_detect_detector_service_tick();
    face_detect_detector_process_frame();
}

uint8_t face_detect_handle_debug_command(const char *cmd)
{
    char line[192];

    if(!str_eq_ci(cmd, "HKFACEINFO"))
        return 0;
    face_detect_detector_format_info(line, sizeof(line));
    debug_console_write_text(line);
    return 1;
}

void face_detect_draw_icon(uint16_t x, uint16_t y, uint16_t color, uint16_t bg)
{
    face_detect_view_draw_icon(x, y, color, bg);
}

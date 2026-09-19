#include "camera_status_view.h"

#include "dristy_theme.h"
#include "dristy_ui.h"
#include "display_binding.h"

void camera_status_view_draw(const char *line1, const char *line2)
{
    dristy_ui_draw_shell_bg();
    dristy_ui_draw_app_header("Camera", "BACK returns to menu");
    dristy_ui_draw_app_footer(NULL, NULL, NULL, "Menu");
    hk_ui_display_draw_text_centered(100U, line1 ? line1 : "", DRISTY_COLOR_TEXT, DRISTY_COLOR_BG);
    hk_ui_display_draw_text_centered(130U, line2 ? line2 : "", DRISTY_COLOR_TEXT_DIM, DRISTY_COLOR_BG);
}

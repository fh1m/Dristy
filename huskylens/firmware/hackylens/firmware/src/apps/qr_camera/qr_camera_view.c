#include "qr_camera_view.h"

#include "../../ui/display_binding.h"

void qr_camera_view_draw_icon(uint16_t x, uint16_t y, uint16_t color, uint16_t bg)
{
    (void)bg;
    hk_ui_display_draw_rect(x + 8, y + 14, 44, 32, 2, color);
    hk_ui_display_fill_rect(x + 18, y + 8, 18, 6, color);
    hk_ui_display_draw_rect(x + 14, y + 20, 10, 10, 2, color);
    hk_ui_display_draw_rect(x + 36, y + 20, 10, 10, 2, color);
    hk_ui_display_draw_rect(x + 14, y + 34, 10, 10, 2, color);
    hk_ui_display_fill_rect(x + 30, y + 34, 4, 4, color);
    hk_ui_display_fill_rect(x + 38, y + 36, 8, 2, color);
    hk_ui_display_fill_rect(x + 30, y + 42, 12, 2, color);
}

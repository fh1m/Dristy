#include "settings_view.h"

#include "../../ui/display_binding.h"

void settings_view_draw_icon(uint16_t x, uint16_t y, uint16_t color, uint16_t bg)
{
    (void)bg;
    hk_ui_display_fill_rect(x + 27, y + 7, 6, 9, color);
    hk_ui_display_fill_rect(x + 27, y + 44, 6, 9, color);
    hk_ui_display_fill_rect(x + 7, y + 27, 9, 6, color);
    hk_ui_display_fill_rect(x + 44, y + 27, 9, 6, color);
    hk_ui_display_fill_rect(x + 14, y + 14, 7, 7, color);
    hk_ui_display_fill_rect(x + 39, y + 14, 7, 7, color);
    hk_ui_display_fill_rect(x + 14, y + 39, 7, 7, color);
    hk_ui_display_fill_rect(x + 39, y + 39, 7, 7, color);
    hk_ui_display_draw_rect(x + 18, y + 18, 24, 24, 3, color);
    hk_ui_display_draw_rect(x + 24, y + 24, 12, 12, 2, color);
    hk_ui_display_fill_rect(x + 28, y + 28, 4, 4, color);
}

#include "sleep_view.h"

#include "../../config/display_config.h"
#include "../../ui/display_binding.h"
#include "../../ui/hk_ui.h"

void sleep_view_enter(void)
{
    hk_ui_display_fill_rect(0, 0, HK_DISPLAY_REQUIRED_WIDTH, HK_DISPLAY_REQUIRED_HEIGHT, COLOR_BLACK);
}

void sleep_view_draw_icon(uint16_t x, uint16_t y, uint16_t color, uint16_t bg)
{
    (void)bg;
    hk_ui_display_draw_rect(x + 20, y + 10, 20, 30, 2, color);
    hk_ui_display_fill_rect(x + 28, y + 6, 4, 14, color);
    hk_ui_display_fill_rect(x + 22, y + 44, 16, 2, color);
    hk_ui_display_fill_rect(x + 18, y + 49, 24, 2, color);
}

#include "dristy_boot_view.h"

#include "dristy_theme.h"
#include "display_binding.h"
#include "hk_config.h"
#include "../config/display_config.h"

static void draw_lens_mark(uint16_t cx, uint16_t cy, uint16_t r, uint16_t fg)
{
    hk_ui_display_draw_rect(
        (uint16_t)(cx - r), (uint16_t)(cy - r),
        (uint16_t)(r * 2U), (uint16_t)(r * 2U), 3U, fg);
    hk_ui_display_fill_rect(
        (uint16_t)(cx - r + 8U), (uint16_t)(cy - r + 8U),
        (uint16_t)(r * 2U - 16U), (uint16_t)(r * 2U - 16U),
        DRISTY_COLOR_BG);
    hk_ui_display_draw_rect(
        (uint16_t)(cx - r + 12U), (uint16_t)(cy - r + 12U),
        (uint16_t)(r * 2U - 24U), (uint16_t)(r * 2U - 24U), 2U, DRISTY_COLOR_ACCENT);
    hk_ui_display_fill_rect(cx - 4U, cy - 4U, 8U, 8U, DRISTY_COLOR_TEXT);
}

void dristy_boot_view_show(void)
{
    uint16_t w = HK_DISPLAY_REQUIRED_WIDTH;
    uint16_t h = HK_DISPLAY_REQUIRED_HEIGHT;

    hk_ui_display_fill_rect(0, 0, w, h, DRISTY_COLOR_BG);
    hk_ui_display_fill_rect(0, 0, w, 3U, DRISTY_COLOR_ACCENT);
    hk_ui_display_fill_rect(0, h - 3U, w, 3U, DRISTY_COLOR_ACCENT_DIM);

    draw_lens_mark(72U, h / 2U, 36U, DRISTY_COLOR_ACCENT);

    hk_ui_display_draw_dristy_text_at(120U, 88U, "DRISTY", DRISTY_COLOR_ACCENT, DRISTY_COLOR_BG);
    hk_ui_display_draw_dristy_small_text_at(120U, 112U, "vision co-processor",
                                           DRISTY_COLOR_TEXT_DIM, DRISTY_COLOR_BG);
    hk_ui_display_draw_dristy_small_text_at(120U, 132U, DRISTY_VERSION, DRISTY_COLOR_ACCENT,
                                           DRISTY_COLOR_BG);

    hk_ui_display_fill_rect(118U, 158U, 180U, 2U, DRISTY_COLOR_BORDER);
    hk_ui_display_draw_dristy_small_text_at(118U, 166U, "booting...", DRISTY_COLOR_TEXT,
                                           DRISTY_COLOR_BG);
}

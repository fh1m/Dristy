#include "qr_result_view.h"

#include <stdio.h>

#include "../../config/display_config.h"
#include "../../config/menu_layout.h"
#include "qr_layout.h"

#include "../../ui/display_binding.h"
#include "../../ui/dristy_theme.h"
#include "../../ui/dristy_ui.h"

static void qr_result_view_draw_chrome(void)
{
    dristy_ui_draw_shell_bg();
    dristy_ui_draw_app_header("QR result", "Scroll payload if long");
    dristy_ui_draw_app_footer(NULL, "Save", NULL, "Close");
}

void qr_result_view_draw_status(const char *status)
{
    hk_ui_display_fill_rect(0, QR_RESULT_STATUS_Y, HK_DISPLAY_REQUIRED_WIDTH, DRISTY_FONT_H,
                            DRISTY_COLOR_BG);
    if(status && status[0])
        hk_ui_display_draw_text_centered(QR_RESULT_STATUS_Y, status, DRISTY_COLOR_TEXT_DIM,
                                       DRISTY_COLOR_BG);
}

static void qr_result_view_draw_actions(void)
{
    (void)0;
    /* Footer hints drawn in chrome */
}

static void qr_result_view_draw_page_hint(uint16_t scroll_line, uint16_t max_scroll)
{
    if(max_scroll > 0)
    {
        char line[16];
        snprintf(line, sizeof(line), "< %u/%u >", (unsigned)(scroll_line + 1U), (unsigned)(max_scroll + 1U));
        qr_result_view_draw_status(line);
    }
    else
    {
        qr_result_view_draw_status("");
    }
}

void qr_result_view_render(const char *payload, uint16_t scroll_line, uint16_t max_scroll)
{
    if(!payload)
        payload = "";
    if(scroll_line > max_scroll)
        scroll_line = max_scroll;

    qr_result_view_draw_chrome();
    hk_ui_display_draw_rect(QR_RESULT_FRAME_X, QR_RESULT_FRAME_Y, QR_RESULT_FRAME_W,
                            QR_RESULT_FRAME_H, MENU_LINE, DRISTY_COLOR_BORDER);

    if(!payload[0])
    {
        hk_ui_display_draw_text_centered(QR_RESULT_TEXT_Y + DRISTY_FONT_H, "Empty payload",
                                         DRISTY_COLOR_TEXT_DIM, DRISTY_COLOR_BG);
    }
    else
    {
        for(uint16_t row = 0; row < QR_RESULT_TEXT_ROWS; row++)
        {
            uint16_t line_index = (uint16_t)(scroll_line + row);
            uint32_t offset = (uint32_t)line_index * QR_RESULT_TEXT_COLS;
            char line[QR_RESULT_TEXT_COLS + 1U];
            uint8_t len = 0;

            if(!payload[offset])
                break;

            while(len < QR_RESULT_TEXT_COLS && payload[offset + len])
            {
                line[len] = payload[offset + len];
                len++;
            }
            line[len] = '\0';
            hk_ui_display_draw_text_at(QR_RESULT_TEXT_X,
                                       (uint16_t)(QR_RESULT_TEXT_Y + row * DRISTY_FONT_H),
                                       line, DRISTY_COLOR_TEXT, DRISTY_COLOR_BG);
        }
    }

    qr_result_view_draw_page_hint(scroll_line, max_scroll);
    qr_result_view_draw_actions();
}

void qr_result_view_clear(void)
{
    hk_ui_display_fill_rect(0, 0, HK_DISPLAY_REQUIRED_WIDTH, HK_DISPLAY_REQUIRED_HEIGHT,
                            DRISTY_COLOR_BG);
}

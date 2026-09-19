#include "qr_result_controller.h"

#include <stdio.h>
#include <string.h>

#include "../../config/input_config.h"
#include "qr_layout.h"

#include "qr_result.h"
#include "../../storage/file_write_error.h"
#include "qr_text_writer.h"
#include "qr_result_view.h"

static uint16_t qr_result_line_count(void)
{
    const char *payload = qr_result_payload_text();
    uint16_t len = (uint16_t)strlen(payload);

    if(len == 0)
        return 1;
    return (uint16_t)((len + QR_RESULT_TEXT_COLS - 1U) / QR_RESULT_TEXT_COLS);
}

static uint16_t qr_result_max_scroll(void)
{
    uint16_t lines = qr_result_line_count();
    return lines > QR_RESULT_TEXT_ROWS ? (uint16_t)(lines - QR_RESULT_TEXT_ROWS) : 0;
}

void qr_result_controller_render(void)
{
    uint16_t max_scroll = qr_result_max_scroll();

    if(qr_result_scroll_line() > max_scroll)
        qr_result_set_scroll_line(max_scroll);
    qr_result_view_render(qr_result_payload_text(), qr_result_scroll_line(), max_scroll);
}

void qr_result_controller_scroll(int8_t delta)
{
    uint16_t max_scroll = qr_result_max_scroll();
    if(qr_result_scroll(delta, max_scroll))
        qr_result_controller_render();
}

void qr_result_controller_save_text(void)
{
    char saved_name[16];
    char line[24];
    const char *payload = qr_result_payload_text();

    if(qr_text_save_payload_text(payload, saved_name, sizeof(saved_name)))
    {
        snprintf(line, sizeof(line), "SAVED %s", saved_name);
        qr_result_view_draw_status(line);
    }
    else
    {
        const char *error = file_write_last_error() ? file_write_last_error() : "UNKNOWN";
        snprintf(line, sizeof(line), "SAVE FAIL %.10s", error);
        qr_result_view_draw_status(line);
        printf("[QR] save text fail %s\r\n", error);
    }
}

qr_result_input_result_t qr_result_controller_handle_input(uint32_t pressed)
{
    if(!qr_result_open())
        return QR_RESULT_INPUT_NOT_OPEN;

    if(pressed & BUTTON_LEFT)
        qr_result_controller_scroll(-1);
    if(pressed & BUTTON_RIGHT)
        qr_result_controller_scroll(1);
    if(pressed & BUTTON_OK)
        qr_result_controller_save_text();
    if(pressed & BUTTON_BACK)
        return QR_RESULT_INPUT_CLOSE_REQUEST;

    return QR_RESULT_INPUT_HANDLED;
}

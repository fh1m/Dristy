#include "qr_result.h"

#include <stddef.h>

#include <stdio.h>

#include "qr_config.h"


static uint8_t g_qr_have_payload;
static uint8_t g_qr_result_open;
static uint16_t g_qr_result_scroll_line;
static char g_qr_payload[QR_PAYLOAD_MAX];
static char g_qr_status[32] = "NO QR";

void qr_result_reset(void)
{
    g_qr_result_open = 0;
    g_qr_have_payload = 0;
    g_qr_payload[0] = '\0';
    snprintf(g_qr_status, sizeof(g_qr_status), "NO QR");
    g_qr_result_scroll_line = 0;
}

void qr_result_clear_payload(void)
{
    g_qr_have_payload = 0;
    g_qr_payload[0] = '\0';
}

void qr_result_set_payload(const uint8_t *payload, uint16_t payload_len)
{
    uint16_t out = 0;
    uint16_t len = payload_len < (QR_PAYLOAD_MAX - 1U) ? payload_len : (uint16_t)(QR_PAYLOAD_MAX - 1U);

    if(payload == NULL || payload_len == 0)
    {
        qr_result_clear_payload();
        return;
    }

    for(uint16_t i = 0; i < len; i++)
    {
        uint8_t c = payload[i];
        g_qr_payload[out++] = (c >= 32 && c <= 126) ? (char)c : '.';
    }
    g_qr_payload[out] = '\0';
    g_qr_have_payload = out ? 1 : 0;
}

uint8_t qr_result_has_payload(void)
{
    return g_qr_have_payload && g_qr_payload[0] != '\0';
}

const char *qr_result_payload_text(void)
{
    return g_qr_have_payload ? g_qr_payload : "";
}

void qr_result_set_status(const char *status)
{
    snprintf(g_qr_status, sizeof(g_qr_status), "%s", status ? status : "");
}

const char *qr_result_status(void)
{
    return g_qr_status;
}

uint8_t qr_result_open(void)
{
    return g_qr_result_open;
}

void qr_result_show(void)
{
    g_qr_result_open = 1;
    g_qr_result_scroll_line = 0;
}

void qr_result_close_window(void)
{
    g_qr_result_open = 0;
    g_qr_result_scroll_line = 0;
}

uint16_t qr_result_scroll_line(void)
{
    return g_qr_result_scroll_line;
}

void qr_result_set_scroll_line(uint16_t line)
{
    g_qr_result_scroll_line = line;
}

uint8_t qr_result_scroll(int8_t delta, uint16_t max_scroll)
{
    uint16_t previous = g_qr_result_scroll_line;

    if(delta < 0)
        g_qr_result_scroll_line = g_qr_result_scroll_line == 0 ? 0 : (uint16_t)(g_qr_result_scroll_line - 1U);
    else if(delta > 0 && g_qr_result_scroll_line < max_scroll)
        g_qr_result_scroll_line++;

    return previous != g_qr_result_scroll_line;
}

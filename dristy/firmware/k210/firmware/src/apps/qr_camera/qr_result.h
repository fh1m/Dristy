#ifndef HK_QR_RESULT_H
#define HK_QR_RESULT_H

#include <stdint.h>

void qr_result_reset(void);
void qr_result_clear_payload(void);
void qr_result_set_payload(const uint8_t *payload, uint16_t payload_len);
uint8_t qr_result_has_payload(void);
const char *qr_result_payload_text(void);
void qr_result_set_status(const char *status);
const char *qr_result_status(void);

uint8_t qr_result_open(void);
void qr_result_show(void);
void qr_result_close_window(void);
uint16_t qr_result_scroll_line(void);
void qr_result_set_scroll_line(uint16_t line);
uint8_t qr_result_scroll(int8_t delta, uint16_t max_scroll);

#endif

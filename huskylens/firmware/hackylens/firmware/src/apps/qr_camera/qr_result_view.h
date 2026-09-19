#ifndef QR_RESULT_VIEW_H
#define QR_RESULT_VIEW_H

#include <stdint.h>

void qr_result_view_render(const char *payload, uint16_t scroll_line, uint16_t max_scroll);
void qr_result_view_draw_status(const char *status);
void qr_result_view_clear(void);

#endif

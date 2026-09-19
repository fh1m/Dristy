#ifndef QR_RESULT_CONTROLLER_H
#define QR_RESULT_CONTROLLER_H

#include <stdint.h>

typedef enum
{
    QR_RESULT_INPUT_NOT_OPEN = 0,
    QR_RESULT_INPUT_HANDLED,
    QR_RESULT_INPUT_CLOSE_REQUEST,
} qr_result_input_result_t;

void qr_result_controller_render(void);
void qr_result_controller_scroll(int8_t delta);
void qr_result_controller_save_text(void);
qr_result_input_result_t qr_result_controller_handle_input(uint32_t pressed);

#endif

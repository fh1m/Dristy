#ifndef HAL_UART_H
#define HAL_UART_H

#include <stddef.h>
#include <stdint.h>

void hal_debug_uart_start_rx(void);
uint32_t hal_debug_uart_receive(uint8_t *data, size_t len);
uint32_t hal_debug_uart_rx_dropped(void);
void hal_debug_uart_send(const uint8_t *data, size_t len);

#endif

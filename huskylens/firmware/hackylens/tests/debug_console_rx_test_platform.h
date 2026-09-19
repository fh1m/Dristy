#ifndef HK_DEBUG_CONSOLE_RX_TEST_PLATFORM_H
#define HK_DEBUG_CONSOLE_RX_TEST_PLATFORM_H

#include <stddef.h>
#include <stdint.h>

typedef enum
{
    UART_DEVICE_1 = 0,
    UART_DEVICE_2,
    UART_DEVICE_3,
} uart_device_number_t;

typedef enum
{
    UART_SEND = 1,
    UART_RECEIVE = 2,
} uart_interrupt_mode_t;

typedef int (*plic_irq_callback_t)(void *context);

int uart_receive_data(uart_device_number_t channel, char *data, size_t length);
int uart_send_data(uart_device_number_t channel, const char *data,
                   size_t length);
void uart_irq_register(uart_device_number_t channel,
                       uart_interrupt_mode_t interrupt_mode,
                       plic_irq_callback_t callback, void *context,
                       uint32_t priority);
void sys_register_putchar(int (*putchar_callback)(char value));

#endif

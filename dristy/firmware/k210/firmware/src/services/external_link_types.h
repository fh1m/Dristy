#ifndef HK_EXTERNAL_LINK_TYPES_H
#define HK_EXTERNAL_LINK_TYPES_H

typedef enum
{
    EXTERNAL_LINK_UART = 0,
    EXTERNAL_LINK_I2C = 1,
} external_link_transport_t;

typedef enum
{
    EXTERNAL_LINK_UART_SPEED_9600 = 0,
    EXTERNAL_LINK_UART_SPEED_115200 = 1,
    EXTERNAL_LINK_UART_SPEED_1000000 = 2,
    EXTERNAL_LINK_UART_SPEED_COUNT = 3,
} external_link_uart_speed_t;

#endif

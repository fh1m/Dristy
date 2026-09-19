#ifndef HK_EXTERNAL_LINK_SERVICE_H
#define HK_EXTERNAL_LINK_SERVICE_H

#include <stddef.h>
#include <stdint.h>

#include "external_link_types.h"

#define EXTERNAL_LINK_I2C_ADDRESS 0x32U

void external_link_service_init(external_link_transport_t transport);
void external_link_service_set_transport(external_link_transport_t transport);
external_link_transport_t external_link_service_transport(void);
void external_link_service_set_uart_baud(uint32_t baud);
uint32_t external_link_service_uart_baud(void);
/* Temporarily yields the descriptor-selected external connector pins to the
 * MicroPython binding owner. The selected transport remains configured
 * logically and is restored by resume(). */
void external_link_service_suspend(void);
void external_link_service_resume(void);
uint8_t external_link_service_suspended(void);
void external_link_service_tick(void);
uint8_t external_link_service_handle_debug_command(const char *command);
void external_link_service_format_info(char *line, size_t line_size);

#endif

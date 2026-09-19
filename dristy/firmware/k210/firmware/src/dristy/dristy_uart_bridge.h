#ifndef DRISTY_UART_BRIDGE_H
#define DRISTY_UART_BRIDGE_H

#include <stdint.h>

/* Dual-stack UART bridge: Dristy 0x55/0xAA (stock + DLP) and HK 'H''K' frames.
 *
 * Feed each RX byte from the external UART. When a complete DLP/Husky response
 * is ready, out_len > 0. When route_hk is 1, also pass the byte to hk_link_stream_feed. */

void dristy_uart_bridge_init(void);

void dristy_uart_bridge_set_uart_baud_fn(void (*fn)(uint32_t baud));

/* Returns 1 if a response frame was written to out (out_len set). */
uint8_t dristy_uart_bridge_feed(uint8_t byte,
                                uint8_t *out, uint16_t out_cap,
                                uint16_t *out_len,
                                uint8_t *route_hk);

uint8_t dristy_uart_bridge_ctrl_stream_active(void);

void dristy_uart_bridge_set_ctrl_stream(uint8_t enabled);

void dristy_uart_bridge_tick(void);

/* Debug USB UART: consume DLP bytes without mixing into HKHELP line buffer. */
uint8_t dristy_uart_bridge_process_debug_byte(uint8_t byte,
                                              uint8_t *out, uint16_t out_cap,
                                              uint16_t *out_len);

#endif

#ifndef DEBUG_CONSOLE_SERVICE_H
#define DEBUG_CONSOLE_SERVICE_H

#include <stddef.h>
#include <stdint.h>

void debug_console_init(void);
void debug_console_start_rx(void);
size_t debug_console_read(uint8_t *data, size_t len);
uint32_t debug_console_rx_dropped(void);
void debug_console_write(const uint8_t *data, size_t len);
void debug_console_write_text(const char *text);

/* HMPY owns the UART byte stream while framed mode is set. Normal debug
 * output is diverted into a bounded diagnostic ring instead of corrupting a
 * COBS packet. Only the session transport may use write_wire(). */
void debug_console_set_framed_mode(uint8_t enabled);
uint8_t debug_console_framed_mode(void);
void debug_console_write_wire(const uint8_t *data, size_t len);
uint32_t debug_console_diagnostic_cursor(void);
size_t debug_console_read_diagnostics_since(uint32_t *cursor,
                                            uint8_t *data, size_t capacity,
                                            uint32_t *lost_bytes);

#endif

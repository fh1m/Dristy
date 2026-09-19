#ifndef HK_TERMINAL_BUFFER_H
#define HK_TERMINAL_BUFFER_H

#include <stddef.h>
#include <stdint.h>

#include "terminal_types.h"

void terminal_buffer_init(uint16_t columns, uint16_t rows);
void terminal_buffer_set_geometry(uint16_t columns, uint16_t rows);
void terminal_buffer_write(const char *text);
void terminal_buffer_scroll_up(void);
void terminal_buffer_scroll_down(void);
void terminal_buffer_follow_latest(void);
void terminal_buffer_visible_row(uint16_t row, char *text, size_t text_size);
terminal_buffer_status_t terminal_buffer_status(void);

#endif

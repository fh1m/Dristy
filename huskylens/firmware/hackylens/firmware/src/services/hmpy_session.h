#ifndef HK_HMPY_SESSION_H
#define HK_HMPY_SESSION_H

#include <stdint.h>

uint8_t hmpy_session_active(void);
/* Sends the line-mode READY response and atomically claims UART3 framing. */
void hmpy_session_begin(void);
/* Owns UART3 RX while active and pumps bounded response/event work. */
void hmpy_session_tick(void);

#endif

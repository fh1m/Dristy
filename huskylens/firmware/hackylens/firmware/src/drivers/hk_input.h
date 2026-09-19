#ifndef HK_INPUT_H
#define HK_INPUT_H

#include <stdint.h>

/* Immediate active-low hardware sample used during boot before debouncing. */
uint32_t buttons_read_pressed_mask(void);

#endif

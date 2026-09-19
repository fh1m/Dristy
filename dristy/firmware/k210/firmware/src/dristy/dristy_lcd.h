#ifndef DRISTY_LCD_H
#define DRISTY_LCD_H

#include <stdint.h>

void dristy_lcd_init(void);
void dristy_lcd_set_state(uint8_t state);
void dristy_lcd_set_brightness(uint8_t percent);
uint8_t dristy_lcd_state(void);

#endif

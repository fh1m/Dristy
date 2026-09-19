#ifndef HUSKY_ISP_LCD_H
#define HUSKY_ISP_LCD_H

#include <stdint.h>

void lcd_init(void);
void lcd_clear(uint16_t color);
void lcd_fill_rect(uint16_t x, uint16_t y, uint16_t width, uint16_t height,
                   uint16_t color);
void lcd_draw_text_centered(uint16_t y, const char *text, uint8_t scale,
                            uint16_t foreground, uint16_t background);

#endif

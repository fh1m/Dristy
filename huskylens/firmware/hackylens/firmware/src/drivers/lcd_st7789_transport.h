#ifndef HK_LCD_ST7789_TRANSPORT_H
#define HK_LCD_ST7789_TRANSPORT_H

#include <stddef.h>
#include <stdint.h>

#include <hackylens/capability/display.h>

void lcd_st7789_transport_prepare(void);
void lcd_st7789_transport_init(void);
hk_result_t lcd_st7789_transport_begin(
    const hk_display_rect_t *rect, hk_deadline_t deadline);
hk_result_t lcd_st7789_transport_write(
    const uint8_t *pixels, size_t size_bytes,
    hk_deadline_t deadline, const hk_cancel_t *cancel);
hk_result_t lcd_st7789_transport_end(
    hk_deadline_t deadline, const hk_cancel_t *cancel);
void lcd_st7789_transport_abort(void);
uint8_t *lcd_st7789_transport_shadow(void);
uint32_t lcd_st7789_transport_shadow_size(void);
uint32_t lcd_st7789_transport_stride(void);
uint16_t lcd_st7789_transport_shadow_pixel(uint16_t x, uint16_t y);

#endif

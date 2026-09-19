#include "lcd.h"

#include <fpioa.h>
#include <platform.h>
#include <sleep.h>
#include <spi.h>
#include <sysctl.h>

#define LCD_WIDTH 320U
#define LCD_HEIGHT 240U
#define LCD_SPI SPI_DEVICE_0
#define LCD_CS SPI_CHIP_SELECT_3
#define GPIO_LCD_RESET 14U
#define GPIO_LCD_DC 15U
#define GPIO_LCD_BACKLIGHT 13U

static uint8_t line_buffer[LCD_WIDTH * 2U];
static uint8_t lcd_available = 1U;
static volatile spi_t *const lcd_spi = (volatile spi_t *)SPI0_BASE_ADDR;
static volatile uint32_t *const gpiohs = (volatile uint32_t *)GPIOHS_BASE_ADDR;

static void gpio_write(uint8_t pin, uint8_t high)
{
    uint32_t mask = 1U << pin;
    gpiohs[1] &= ~mask; /* input_en */
    gpiohs[2] |= mask;  /* output_en */
    if(high) gpiohs[3] |= mask;
    else gpiohs[3] &= ~mask;
}

static void spi_send_bytes(const uint8_t *data, size_t length)
{
    size_t offset = 0U;
    uint32_t watchdog = 0U;
    if(!lcd_available)
        return;
    lcd_spi->ssienr = 0U;
    lcd_spi->ctrlr0 = (7U << 16) | (1U << 8); /* 8-bit, transmit-only */
    lcd_spi->ssienr = 1U;
    lcd_spi->ser = 1U << LCD_CS;
    while(offset < length)
    {
        size_t available = 32U - lcd_spi->txflr;
        if(available > length - offset) available = length - offset;
        if(available == 0U)
        {
            if(++watchdog >= 1000000U)
            {
                lcd_available = 0U;
                lcd_spi->ser = 0U;
                lcd_spi->ssienr = 0U;
                return;
            }
            continue;
        }
        watchdog = 0U;
        while(available--) lcd_spi->dr[0] = data[offset++];
    }
    watchdog = 0U;
    while((lcd_spi->sr & 0x05U) != 0x04U)
    {
        if(++watchdog >= 1000000U)
        {
            lcd_available = 0U;
            break;
        }
    }
    lcd_spi->ser = 0U;
    lcd_spi->ssienr = 0U;
}

static void lcd_send(uint8_t data_mode, const uint8_t *data, size_t length)
{
    gpio_write(GPIO_LCD_DC, data_mode);
    spi_send_bytes(data, length);
}

static void lcd_command(uint8_t command)
{
    lcd_send(0U, &command, 1U);
}

static void lcd_data_u8(uint8_t data)
{
    lcd_send(1U, &data, 1U);
}

static void lcd_window(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1)
{
    uint8_t data[4];
    lcd_command(0x2A);
    data[0] = (uint8_t)(x0 >> 8); data[1] = (uint8_t)x0;
    data[2] = (uint8_t)(x1 >> 8); data[3] = (uint8_t)x1;
    lcd_send(1U, data, sizeof(data));
    lcd_command(0x2B);
    data[0] = (uint8_t)(y0 >> 8); data[1] = (uint8_t)y0;
    data[2] = (uint8_t)(y1 >> 8); data[3] = (uint8_t)y1;
    lcd_send(1U, data, sizeof(data));
    lcd_command(0x2C);
}

void lcd_init(void)
{
    static const uint8_t b2[] = {0x01, 0x01, 0x00, 0x01, 0x01};
    static const uint8_t d0[] = {0xA4, 0xA1};

    fpioa_set_function(18, FUNC_GPIOHS15);
    fpioa_set_function(19, FUNC_SPI0_SS3);
    fpioa_set_function(20, FUNC_SPI0_SCLK);
    fpioa_set_function(21, FUNC_SPI0_D0);
    fpioa_set_function(22, FUNC_GPIOHS14);
    fpioa_set_function(24, FUNC_GPIOHS13);
    gpio_write(GPIO_LCD_DC, 0U);
    gpio_write(GPIO_LCD_RESET, 1U);
    gpio_write(GPIO_LCD_BACKLIGHT, 1U);

    sysctl_set_spi0_dvp_data(1U);
    sysctl_clock_enable(SYSCTL_CLOCK_SPI0);
    sysctl_clock_set_threshold(SYSCTL_THRESHOLD_SPI0, 0U);
    lcd_spi->baudr = sysctl_clock_get_freq(SYSCTL_CLOCK_SPI0) / 40000000U;
    if(lcd_spi->baudr < 2U) lcd_spi->baudr = 2U;
    lcd_spi->baudr = (lcd_spi->baudr + 1U) & ~1U;
    lcd_spi->imr = 0U;
    lcd_spi->dmacr = 0U;
    lcd_spi->dmatdlr = 0x10U;
    lcd_spi->dmardlr = 0U;
    lcd_spi->ser = 0U;
    lcd_spi->ssienr = 0U;
    lcd_spi->ctrlr0 = 7U << 16;
    lcd_spi->spi_ctrlr0 = 0U;
    lcd_spi->endian = 0U;

#ifdef ISP_LCD_INIT_ONLY
    return;
#endif

    gpio_write(GPIO_LCD_RESET, 1U); msleep(1U);
    gpio_write(GPIO_LCD_RESET, 0U); msleep(1U);
    gpio_write(GPIO_LCD_RESET, 1U); msleep(125U);
    lcd_command(0x11); msleep(120U);
    lcd_command(0x36); lcd_data_u8(0xA0);
    lcd_command(0x3A); lcd_data_u8(0x05);
    lcd_command(0x21);
    lcd_command(0xB2); lcd_send(1U, b2, sizeof(b2));
    lcd_command(0xB7); lcd_data_u8(0x75);
    lcd_command(0xBB); lcd_data_u8(0x22);
    lcd_command(0xC0); lcd_data_u8(0x2C);
    lcd_command(0xC2); lcd_data_u8(0x01);
    lcd_command(0xC3); lcd_data_u8(0x13);
    lcd_command(0xC4); lcd_data_u8(0x20);
    lcd_command(0xC6); lcd_data_u8(0xE1);
    lcd_command(0xD0); lcd_send(1U, d0, sizeof(d0));
    lcd_command(0xD6); lcd_data_u8(0xA1);
    lcd_command(0x29); msleep(20U);
}

void lcd_fill_rect(uint16_t x, uint16_t y, uint16_t width, uint16_t height,
                   uint16_t color)
{
    if(x >= LCD_WIDTH || y >= LCD_HEIGHT || width == 0U || height == 0U)
        return;
    if(x + width > LCD_WIDTH) width = LCD_WIDTH - x;
    if(y + height > LCD_HEIGHT) height = LCD_HEIGHT - y;
    for(uint16_t i = 0; i < width; ++i)
    {
        line_buffer[i * 2U] = (uint8_t)(color >> 8);
        line_buffer[i * 2U + 1U] = (uint8_t)color;
    }
    lcd_window(x, y, x + width - 1U, y + height - 1U);
    gpio_write(GPIO_LCD_DC, 1U);
    for(uint16_t row = 0; row < height; ++row)
        spi_send_bytes(line_buffer, width * 2U);
}

void lcd_clear(uint16_t color)
{
    lcd_fill_rect(0U, 0U, LCD_WIDTH, LCD_HEIGHT, color);
}

static const uint8_t *glyph(char ch)
{
    static const uint8_t blank[5] = {0,0,0,0,0};
    static const uint8_t percent[5] = {0x63,0x13,0x08,0x64,0x63};
    static const uint8_t dash[5] = {0x08,0x08,0x08,0x08,0x08};
    static const uint8_t colon[5] = {0,0x36,0x36,0,0};
    static const uint8_t digits[10][5] = {
        {0x3E,0x51,0x49,0x45,0x3E},{0,0x42,0x7F,0x40,0},
        {0x42,0x61,0x51,0x49,0x46},{0x21,0x41,0x45,0x4B,0x31},
        {0x18,0x14,0x12,0x7F,0x10},{0x27,0x45,0x45,0x45,0x39},
        {0x3C,0x4A,0x49,0x49,0x30},{0x01,0x71,0x09,0x05,0x03},
        {0x36,0x49,0x49,0x49,0x36},{0x06,0x49,0x49,0x29,0x1E}
    };
    static const uint8_t letters[26][5] = {
        {0x7E,0x11,0x11,0x11,0x7E},{0x7F,0x49,0x49,0x49,0x36},
        {0x3E,0x41,0x41,0x41,0x22},{0x7F,0x41,0x41,0x22,0x1C},
        {0x7F,0x49,0x49,0x49,0x41},{0x7F,0x09,0x09,0x09,0x01},
        {0x3E,0x41,0x49,0x49,0x7A},{0x7F,0x08,0x08,0x08,0x7F},
        {0,0x41,0x7F,0x41,0},{0x20,0x40,0x41,0x3F,0x01},
        {0x7F,0x08,0x14,0x22,0x41},{0x7F,0x40,0x40,0x40,0x40},
        {0x7F,0x02,0x0C,0x02,0x7F},{0x7F,0x04,0x08,0x10,0x7F},
        {0x3E,0x41,0x41,0x41,0x3E},{0x7F,0x09,0x09,0x09,0x06},
        {0x3E,0x41,0x51,0x21,0x5E},{0x7F,0x09,0x19,0x29,0x46},
        {0x46,0x49,0x49,0x49,0x31},{0x01,0x01,0x7F,0x01,0x01},
        {0x3F,0x40,0x40,0x40,0x3F},{0x1F,0x20,0x40,0x20,0x1F},
        {0x3F,0x40,0x38,0x40,0x3F},{0x63,0x14,0x08,0x14,0x63},
        {0x07,0x08,0x70,0x08,0x07},{0x61,0x51,0x49,0x45,0x43}
    };
    if(ch >= '0' && ch <= '9') return digits[ch - '0'];
    if(ch >= 'A' && ch <= 'Z') return letters[ch - 'A'];
    if(ch == '%') return percent;
    if(ch == '-') return dash;
    if(ch == ':') return colon;
    return blank;
}

static void draw_char(uint16_t x, uint16_t y, char ch, uint8_t scale,
                      uint16_t foreground, uint16_t background)
{
    const uint8_t *bits = glyph(ch);
    lcd_fill_rect(x, y, 6U * scale, 8U * scale, background);
    for(uint8_t column = 0; column < 5U; ++column)
        for(uint8_t row = 0; row < 7U; ++row)
            if(bits[column] & (1U << row))
                lcd_fill_rect(x + column * scale, y + row * scale,
                              scale, scale, foreground);
}

void lcd_draw_text_centered(uint16_t y, const char *text, uint8_t scale,
                            uint16_t foreground, uint16_t background)
{
    size_t length = 0U;
    while(text[length]) ++length;
    uint16_t width = (uint16_t)(length * 6U * scale);
    uint16_t x = width < LCD_WIDTH ? (LCD_WIDTH - width) / 2U : 0U;
    for(size_t i = 0; i < length; ++i)
        draw_char(x + (uint16_t)i * 6U * scale, y, text[i], scale,
                  foreground, background);
}

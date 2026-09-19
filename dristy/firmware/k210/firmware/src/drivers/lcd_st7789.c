#include "lcd_st7789_transport.h"

#include <stdio.h>

#include "defaults.h"
#include "hal_gpio.h"
#include "hal_pwm.h"
#include "hal_spi.h"
#include "hal_time.h"

static uint8_t s_shadow[LCD_W * LCD_H * 2U]
    __attribute__((aligned(4), section(".bss")));
static uint8_t s_pixel_stream_active;

static uint64_t transport_deadline(hk_deadline_t deadline)
{
    return deadline.at_us == 0U ? UINT64_MAX : deadline.at_us;
}

static hk_result_t terminal_result(
    hk_deadline_t deadline, const hk_cancel_t *cancel)
{
    if(cancel && cancel->probe && cancel->probe(cancel->context))
        return HK_ERR_CANCELLED;
    if(deadline.at_us != 0U && hal_time_us() >= deadline.at_us)
        return HK_ERR_DEADLINE_EXCEEDED;
    return HK_OK;
}

static hk_result_t transfer_failure(hk_deadline_t deadline)
{
    return deadline.at_us != 0U && hal_time_us() >= deadline.at_us ?
        HK_ERR_DEADLINE_EXCEEDED : HK_ERR_IO;
}

static uint8_t send_until(
    const uint8_t *data, size_t size_bytes, hk_deadline_t deadline)
{
    hal_spi_fifo_set_tmod_tx(LCD_SPI);
    return hal_spi_fifo_send_bytes_until(
        LCD_SPI, LCD_CS, data, size_bytes, transport_deadline(deadline));
}

static uint8_t command_until(uint8_t command, hk_deadline_t deadline)
{
    hal_spi_fifo_set_frame_bits(LCD_SPI, 8U);
    hal_gpiohs_write(GPIOHS_LCD_DC_OR_AUX, 0U);
    return send_until(&command, 1U, deadline);
}

static uint8_t data_until(
    const uint8_t *data, size_t size_bytes, hk_deadline_t deadline)
{
    hal_spi_fifo_set_frame_bits(LCD_SPI, 8U);
    hal_gpiohs_write(GPIOHS_LCD_DC_OR_AUX, 1U);
    return send_until(data, size_bytes, deadline);
}

static void command(uint8_t value)
{
    (void)command_until(value, HK_DEADLINE_IMMEDIATE);
}

static void data(const uint8_t *value, size_t size_bytes)
{
    (void)data_until(value, size_bytes, HK_DEADLINE_IMMEDIATE);
}

static void data_u8(uint8_t value)
{
    data(&value, 1U);
}

static void reset_panel(void)
{
    hal_gpiohs_write(GPIOHS_LCD_RST, 1U);
    hal_sleep_ms(1U);
    hal_gpiohs_write(GPIOHS_LCD_RST, 0U);
    hal_sleep_ms(1U);
    hal_gpiohs_write(GPIOHS_LCD_RST, 1U);
    hal_sleep_ms(125U);
}

void lcd_st7789_transport_prepare(void)
{
    hal_pwm_init(SCREEN_BL_PWM_DEVICE);
    hal_spi_init(LCD_SPI, 32U);
    printf("[LCD] spi original-ish clk=%u\r\n",
           hal_spi_set_clock(LCD_SPI, LCD_SPI_HZ));
}

void lcd_st7789_transport_init(void)
{
    static const uint8_t gamma_p[] = {
        0xD0, 0x05, 0x0A, 0x09, 0x08, 0x05, 0x2E,
        0x44, 0x45, 0x0F, 0x17, 0x16, 0x2B, 0x33,
    };
    static const uint8_t gamma_n[] = {
        0xD0, 0x05, 0x0A, 0x09, 0x08, 0x05, 0x2E,
        0x43, 0x45, 0x0F, 0x16, 0x16, 0x2B, 0x33,
    };
    static const uint8_t b2[] = {0x01, 0x01, 0x00, 0x01, 0x01};
    static const uint8_t d0[] = {0xA4, 0xA1};
    reset_panel();
    command(0x11);
    command(0x36); data_u8(0xA0);
    command(0x3A); data_u8(0x05);
    command(0x21);
    command(0xB2); data(b2, sizeof(b2));
    command(0xB7); data_u8(0x75);
    command(0xBB); data_u8(0x22);
    command(0xC0); data_u8(0x2C);
    command(0xC2); data_u8(0x01);
    command(0xC3); data_u8(0x13);
    command(0xC4); data_u8(0x20);
    command(0xC6); data_u8(0xE1);
    command(0xD0); data(d0, sizeof(d0));
    command(0xD6); data_u8(0xA1);
    command(0xE0); data(gamma_p, sizeof(gamma_p));
    command(0xE1); data(gamma_n, sizeof(gamma_n));
    command(0x29);
}

hk_result_t lcd_st7789_transport_begin(
    const hk_display_rect_t *rect, hk_deadline_t deadline)
{
    uint8_t coordinates[4];
    hk_result_t result = terminal_result(deadline, NULL);

    if(result != HK_OK)
        return result;
    if(s_pixel_stream_active)
        return HK_ERR_INVALID_STATE;
    if(!rect || rect->x < 0 || rect->y < 0 || rect->width == 0U ||
       rect->height == 0U ||
       (uint64_t)(uint32_t)rect->x + rect->width > LCD_W ||
       (uint64_t)(uint32_t)rect->y + rect->height > LCD_H)
        return HK_ERR_INVALID_ARGUMENT;
    if(!command_until(0x2AU, deadline))
        return transfer_failure(deadline);
    coordinates[0] = (uint8_t)((uint32_t)rect->x >> 8);
    coordinates[1] = (uint8_t)rect->x;
    coordinates[2] = (uint8_t)(((uint32_t)rect->x + rect->width - 1U) >> 8);
    coordinates[3] = (uint8_t)((uint32_t)rect->x + rect->width - 1U);
    if(!data_until(coordinates, sizeof(coordinates), deadline) ||
       !command_until(0x2BU, deadline))
        return transfer_failure(deadline);
    coordinates[0] = (uint8_t)((uint32_t)rect->y >> 8);
    coordinates[1] = (uint8_t)rect->y;
    coordinates[2] = (uint8_t)(((uint32_t)rect->y + rect->height - 1U) >> 8);
    coordinates[3] = (uint8_t)((uint32_t)rect->y + rect->height - 1U);
    if(!data_until(coordinates, sizeof(coordinates), deadline) ||
       !command_until(0x2CU, deadline))
        return transfer_failure(deadline);
    hal_spi_fifo_set_frame_bits(LCD_SPI, 16U);
    hal_gpiohs_write(GPIOHS_LCD_DC_OR_AUX, 1U);
    if(!hal_spi_fifo_tx_begin_until(
           LCD_SPI, LCD_CS, transport_deadline(deadline)))
        return transfer_failure(deadline);
    s_pixel_stream_active = 1U;
    return HK_OK;
}

hk_result_t lcd_st7789_transport_write(
    const uint8_t *pixels, size_t size_bytes,
    hk_deadline_t deadline, const hk_cancel_t *cancel)
{
    hk_result_t result = terminal_result(deadline, cancel);

    if(result != HK_OK)
    {
        lcd_st7789_transport_abort();
        return result;
    }
    if(!s_pixel_stream_active)
        return HK_ERR_INVALID_STATE;
    if(!pixels || size_bytes == 0U)
        return HK_ERR_INVALID_ARGUMENT;
    if(!hal_spi_fifo_tx_write_u16be_until(
           LCD_SPI, pixels, size_bytes, transport_deadline(deadline)))
    {
        lcd_st7789_transport_abort();
        return transfer_failure(deadline);
    }
    return terminal_result(deadline, cancel);
}

hk_result_t lcd_st7789_transport_end(
    hk_deadline_t deadline, const hk_cancel_t *cancel)
{
    hk_result_t result = terminal_result(deadline, cancel);

    if(!s_pixel_stream_active)
        return HK_ERR_INVALID_STATE;
    if(result != HK_OK)
    {
        lcd_st7789_transport_abort();
        return result;
    }
    if(!hal_spi_fifo_tx_end_until(
           LCD_SPI, transport_deadline(deadline)))
    {
        s_pixel_stream_active = 0U;
        return transfer_failure(deadline);
    }
    s_pixel_stream_active = 0U;
    return terminal_result(deadline, cancel);
}

void lcd_st7789_transport_abort(void)
{
    if(!s_pixel_stream_active)
        return;
    hal_spi_fifo_tx_abort(LCD_SPI);
    s_pixel_stream_active = 0U;
}

uint8_t *lcd_st7789_transport_shadow(void)
{
    return s_shadow;
}

uint32_t lcd_st7789_transport_shadow_size(void)
{
    return sizeof(s_shadow);
}

uint32_t lcd_st7789_transport_stride(void)
{
    return LCD_W * 2U;
}

uint16_t lcd_st7789_transport_shadow_pixel(uint16_t x, uint16_t y)
{
    uint32_t offset;

    if(x >= LCD_W || y >= LCD_H)
        return 0U;
    offset = ((uint32_t)y * LCD_W + x) * 2U;
    return (uint16_t)((uint16_t)s_shadow[offset] << 8) |
           s_shadow[offset + 1U];
}

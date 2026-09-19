#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <hackylens/capability/display.h>

#include "defaults.h"
#include "hal_gpio.h"
#include "hal_pwm.h"
#include "hal_spi.h"
#include "hal_time.h"
#include "lcd_st7789_transport.h"

static uint64_t s_now_us;
static uint32_t s_one_shot_calls;
static uint32_t s_stream_begin_calls;
static uint32_t s_stream_write_calls;
static uint32_t s_stream_end_calls;
static uint32_t s_stream_abort_calls;
static uint32_t s_stream_bytes;
static uint32_t s_frame_bits;
static uint32_t s_u16_write_calls;
static uint8_t s_hal_stream_active;
static uint8_t s_fail_write;
static uint8_t s_fail_end;

static void require_true(uint8_t condition, const char *message)
{
    if(condition)
        return;
    fprintf(stderr, "FAIL: %s\n", message);
    exit(1);
}

uint64_t hal_time_us(void)
{
    return s_now_us;
}

void hal_sleep_ms(uint32_t ms)
{
    s_now_us += (uint64_t)ms * 1000U;
}

void hal_gpiohs_write(uint8_t pin, uint8_t value)
{
    (void)pin;
    (void)value;
}

void hal_pwm_init(uint8_t device)
{
    (void)device;
}

void hal_spi_init(uint8_t device, uint32_t frame_bits)
{
    (void)device;
    (void)frame_bits;
}

uint32_t hal_spi_set_clock(uint8_t device, uint32_t hz)
{
    (void)device;
    return hz;
}

void hal_spi_fifo_set_tmod_tx(uint8_t device)
{
    (void)device;
}

void hal_spi_fifo_set_frame_bits(uint8_t device, uint32_t bits)
{
    (void)device;
    s_frame_bits = bits;
}

uint8_t hal_spi_fifo_send_bytes_until(
    uint8_t device, uint8_t chip_select, const uint8_t *data,
    size_t len, uint64_t deadline_us)
{
    (void)device;
    (void)chip_select;
    if(s_hal_stream_active || (len && !data) || s_now_us >= deadline_us)
        return 0U;
    s_one_shot_calls++;
    return 1U;
}

uint8_t hal_spi_fifo_tx_begin_until(
    uint8_t device, uint8_t chip_select, uint64_t deadline_us)
{
    (void)device;
    (void)chip_select;
    if(s_hal_stream_active || s_now_us >= deadline_us)
        return 0U;
    s_hal_stream_active = 1U;
    s_stream_begin_calls++;
    return 1U;
}

uint8_t hal_spi_fifo_tx_write_until(
    uint8_t device, const uint8_t *data, size_t len,
    uint64_t deadline_us)
{
    (void)device;
    if(!s_hal_stream_active || (len && !data) ||
       s_now_us >= deadline_us || s_fail_write)
        return 0U;
    s_stream_write_calls++;
    s_stream_bytes += (uint32_t)len;
    return 1U;
}

uint8_t hal_spi_fifo_tx_write_u16be_until(
    uint8_t device, const uint8_t *data, size_t len,
    uint64_t deadline_us)
{
    s_u16_write_calls++;
    return hal_spi_fifo_tx_write_until(device, data, len, deadline_us);
}

uint8_t hal_spi_fifo_tx_end_until(uint8_t device, uint64_t deadline_us)
{
    (void)device;
    if(!s_hal_stream_active || s_now_us >= deadline_us || s_fail_end)
    {
        s_hal_stream_active = 0U;
        return 0U;
    }
    s_hal_stream_active = 0U;
    s_stream_end_calls++;
    return 1U;
}

void hal_spi_fifo_tx_abort(uint8_t device)
{
    (void)device;
    if(!s_hal_stream_active)
        return;
    s_hal_stream_active = 0U;
    s_stream_abort_calls++;
}

typedef struct
{
    uint8_t cancelled;
} cancel_fixture_t;

static uint8_t cancel_probe(const void *context)
{
    return ((const cancel_fixture_t *)context)->cancelled;
}

static void reset_fixture(void)
{
    s_now_us = 1U;
    s_one_shot_calls = 0U;
    s_stream_begin_calls = 0U;
    s_stream_write_calls = 0U;
    s_stream_end_calls = 0U;
    s_stream_abort_calls = 0U;
    s_stream_bytes = 0U;
    s_frame_bits = 0U;
    s_u16_write_calls = 0U;
    s_hal_stream_active = 0U;
    s_fail_write = 0U;
    s_fail_end = 0U;
    lcd_st7789_transport_abort();
}

static void test_one_stream_for_many_slices(void)
{
    hk_display_rect_t screen = {0, 0, LCD_W, LCD_H};
    uint8_t pixels[128];
    hk_deadline_t deadline = {1000000U};

    memset(pixels, 0xA5, sizeof(pixels));
    reset_fixture();
    require_true(lcd_st7789_transport_begin(&screen, deadline) == HK_OK,
                 "full-frame stream must begin");
    require_true(s_one_shot_calls == 5U && s_stream_begin_calls == 1U,
                 "window commands must precede exactly one pixel stream");
    require_true(s_frame_bits == 16U,
                 "pixel stream must use packed 16-bit frames");
    for(uint8_t index = 0U; index < 20U; index++)
        require_true(lcd_st7789_transport_write(
                         pixels, sizeof(pixels), deadline, NULL) == HK_OK,
                     "each bounded slice must join the active stream");
    require_true(s_stream_begin_calls == 1U && s_stream_end_calls == 0U &&
                 s_stream_write_calls == 20U &&
                 s_u16_write_calls == 20U &&
                 s_stream_bytes == 20U * sizeof(pixels),
                 "slices must not restart or prematurely end SPI");
    require_true(lcd_st7789_transport_end(deadline, NULL) == HK_OK &&
                 s_stream_end_calls == 1U && !s_hal_stream_active,
                 "one final end must drain and close the stream");
}

static void test_cancel_aborts_without_late_write(void)
{
    hk_display_rect_t rect = {4, 5, 16U, 8U};
    uint8_t pixels[128] = {0};
    cancel_fixture_t fixture = {1U};
    hk_cancel_t cancel = {cancel_probe, &fixture};
    hk_deadline_t deadline = {1000000U};

    reset_fixture();
    require_true(lcd_st7789_transport_begin(&rect, deadline) == HK_OK,
                 "cancel fixture stream must begin");
    require_true(lcd_st7789_transport_write(
                     pixels, sizeof(pixels), deadline, &cancel) ==
                     HK_ERR_CANCELLED,
                 "cancel must stop before the next slice is queued");
    require_true(s_stream_write_calls == 0U && s_stream_abort_calls == 1U &&
                 !s_hal_stream_active,
                 "cancel must abort with no late pixel write");
    require_true(lcd_st7789_transport_begin(&rect, deadline) == HK_OK,
                 "a cancelled stream must permit bounded recovery");
    lcd_st7789_transport_abort();
}

static void test_failure_and_deadline_cleanup(void)
{
    hk_display_rect_t rect = {0, 0, 8U, 8U};
    uint8_t pixels[128] = {0};
    hk_deadline_t deadline = {100U};

    reset_fixture();
    require_true(lcd_st7789_transport_begin(&rect, deadline) == HK_OK,
                 "deadline fixture stream must begin");
    s_now_us = deadline.at_us;
    require_true(lcd_st7789_transport_write(
                     pixels, sizeof(pixels), deadline, NULL) ==
                     HK_ERR_DEADLINE_EXCEEDED && !s_hal_stream_active,
                 "expired deadline must abort the stream");

    reset_fixture();
    require_true(lcd_st7789_transport_begin(&rect, deadline) == HK_OK,
                 "write-failure fixture stream must begin");
    s_fail_write = 1U;
    require_true(lcd_st7789_transport_write(
                     pixels, sizeof(pixels), deadline, NULL) == HK_ERR_IO &&
                 !s_hal_stream_active,
                 "HAL write failure must abort the stream");

    reset_fixture();
    require_true(lcd_st7789_transport_begin(&rect, deadline) == HK_OK,
                 "end-failure fixture stream must begin");
    s_fail_end = 1U;
    require_true(lcd_st7789_transport_end(deadline, NULL) == HK_ERR_IO &&
                 !s_hal_stream_active,
                 "HAL end failure must leave no active stream");
}

int main(void)
{
    test_one_stream_for_many_slices();
    test_cancel_aborts_without_late_write();
    test_failure_and_deadline_cleanup();
    puts("LCD_ST7789_STREAM_OK slices=20 begin=1 end=1 cancel=no-late-write");
    return 0;
}

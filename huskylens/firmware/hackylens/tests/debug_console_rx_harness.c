#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "debug_console_rx_test_platform.h"
#include "debug_console_service.h"

#define TEST_RX_RING_BYTES 2048U
#define TEST_FAKE_FIFO_BYTES 8U
#define TEST_OVERFLOW_BYTES 37U

static uint8_t g_hardware_fifo[TEST_FAKE_FIFO_BYTES];
static size_t g_hardware_read;
static size_t g_hardware_write;
static plic_irq_callback_t g_rx_callback;
static void *g_rx_context;
static int (*g_putchar_callback)(char value);
static uint32_t g_irq_register_calls;
static uint32_t g_putchar_register_calls;
static uint32_t g_uart_init_calls;
static uint32_t g_uart_configure_calls;
static uint32_t g_uart_send_calls;
static uint8_t g_last_sent;

static void require_true(uint8_t condition, const char *message)
{
    if(condition)
        return;
    fprintf(stderr, "FAIL: %s\n", message);
    exit(1);
}

static size_t hardware_pending(void)
{
    return g_hardware_write - g_hardware_read;
}

static void hardware_queue(const uint8_t *data, size_t length)
{
    require_true(hardware_pending() == 0U,
                 "fake hardware FIFO must be empty before loading");
    require_true(length <= sizeof(g_hardware_fifo),
                 "fake hardware FIFO capacity exceeded");
    memcpy(g_hardware_fifo, data, length);
    g_hardware_read = 0U;
    g_hardware_write = length;
}

static void inject_stream(const uint8_t *data, size_t length)
{
    size_t offset = 0U;

    require_true(g_rx_callback != NULL, "RX interrupt must be registered");
    while(offset < length)
    {
        size_t chunk = length - offset;

        if(chunk > TEST_FAKE_FIFO_BYTES)
            chunk = TEST_FAKE_FIFO_BYTES;
        hardware_queue(data + offset, chunk);
        require_true(g_rx_callback(g_rx_context) == 0,
                     "RX callback must acknowledge the interrupt");
        require_true(hardware_pending() == 0U,
                     "RX callback must drain the complete hardware FIFO");
        offset += chunk;
    }
}

static void fill_pattern(uint8_t *data, size_t length, uint32_t seed)
{
    uint32_t state = seed;

    for(size_t i = 0U; i < length; i++)
    {
        state = state * 1664525U + 1013904223U;
        data[i] = (uint8_t)(state >> 24);
    }
}

static void require_exact_read(const uint8_t *expected, size_t length,
                               const char *message)
{
    uint8_t actual[TEST_RX_RING_BYTES];
    size_t received;

    require_true(length <= sizeof(actual), "test read exceeds scratch buffer");
    memset(actual, 0xa5, sizeof(actual));
    received = debug_console_read(actual, length);
    require_true(received == length, message);
    require_true(memcmp(actual, expected, length) == 0, message);
    require_true(debug_console_read(actual, 1U) == 0U,
                 "software RX ring must be empty after exact read");
}

int main(void)
{
    static const uint8_t stale[] = {'H', 'M', 'P', 'Y', '?'};
    static uint8_t exact_22[22];
    static uint8_t exact_1050[1050];
    static uint8_t wrap[1100];
    static uint8_t overflow_keep[TEST_RX_RING_BYTES];
    static uint8_t overflow_drop[TEST_OVERFLOW_BYTES];
    uint8_t byte;

    fill_pattern(exact_22, sizeof(exact_22), 1U);
    fill_pattern(exact_1050, sizeof(exact_1050), 2U);
    fill_pattern(wrap, sizeof(wrap), 3U);
    fill_pattern(overflow_keep, sizeof(overflow_keep), 4U);
    fill_pattern(overflow_drop, sizeof(overflow_drop), 5U);

    debug_console_init();
    require_true(g_putchar_register_calls == 1U && g_putchar_callback != NULL,
                 "debug console must retain one stdout putchar registration");

    hardware_queue(stale, sizeof(stale));
    debug_console_start_rx();
    require_true(hardware_pending() == 0U,
                 "startup must discard stale hardware bytes");
    require_true(debug_console_read(&byte, 1U) == 0U,
                 "discarded startup bytes must not enter the software ring");
    require_true(g_irq_register_calls == 1U,
                 "RX interrupt must be registered exactly once");

    debug_console_start_rx();
    require_true(g_irq_register_calls == 1U,
                 "repeated start must not duplicate IRQ registration");
    require_true(g_uart_init_calls == 0U && g_uart_configure_calls == 0U,
                 "RX startup must not reinitialize the SDK stdout UART");
    require_true(g_putchar_register_calls == 1U,
                 "RX startup must not replace the existing stdout hook");
    require_true(g_putchar_callback('!') == '!',
                 "registered stdout callback must remain callable");
    require_true(g_uart_send_calls == 1U && g_last_sent == '!',
                 "stdout must continue to transmit through UART3");

    inject_stream(exact_22, sizeof(exact_22));
    require_exact_read(exact_22, sizeof(exact_22),
                       "22-byte HMPY-sized input must preserve exact order");

    inject_stream(exact_1050, sizeof(exact_1050));
    require_exact_read(exact_1050, sizeof(exact_1050),
                       "1050-byte input must preserve exact order");

    /* The preceding 1072 bytes put both monotonic indices halfway through the
       ring. This transfer crosses the physical 2048-byte array boundary. */
    inject_stream(wrap, sizeof(wrap));
    require_exact_read(wrap, sizeof(wrap),
                       "wrapped ring input must preserve exact order");

    inject_stream(overflow_keep, sizeof(overflow_keep));
    inject_stream(overflow_drop, sizeof(overflow_drop));
    require_true(hardware_pending() == 0U,
                 "full software ring must not stop hardware FIFO draining");
    require_true(debug_console_rx_dropped() == TEST_OVERFLOW_BYTES,
                 "overflow must count every drop-new byte");
    require_exact_read(overflow_keep, sizeof(overflow_keep),
                       "overflow must retain old bytes and drop only new bytes");

    puts("DEBUG_CONSOLE_RX_OK stale=5 exact=22+1050 wrap=1100 "
         "overflow=37 ring=2048 irq_init=1 stdout=1");
    return 0;
}

int uart_receive_data(uart_device_number_t channel, char *data, size_t length)
{
    size_t available;
    size_t count;

    require_true(channel == UART_DEVICE_3,
                 "debug RX must use UART_DEVICE_3");
    available = hardware_pending();
    count = length < available ? length : available;
    if(count)
    {
        memcpy(data, g_hardware_fifo + g_hardware_read, count);
        g_hardware_read += count;
    }
    return (int)count;
}

int uart_send_data(uart_device_number_t channel, const char *data,
                   size_t length)
{
    require_true(channel == UART_DEVICE_3,
                 "debug TX must use UART_DEVICE_3");
    if(length)
        g_last_sent = (uint8_t)data[length - 1U];
    g_uart_send_calls++;
    return (int)length;
}

void uart_irq_register(uart_device_number_t channel,
                       uart_interrupt_mode_t interrupt_mode,
                       plic_irq_callback_t callback, void *context,
                       uint32_t priority)
{
    require_true(channel == UART_DEVICE_3,
                 "RX interrupt must target UART_DEVICE_3");
    require_true(interrupt_mode == UART_RECEIVE,
                 "RX interrupt must use UART_RECEIVE mode");
    require_true(priority == 2U, "RX interrupt priority must be two");
    g_irq_register_calls++;
    g_rx_callback = callback;
    g_rx_context = context;
}

void sys_register_putchar(int (*putchar_callback)(char value))
{
    g_putchar_register_calls++;
    g_putchar_callback = putchar_callback;
}

/* These deliberately remain unused by the production implementation. Their
   counters make a future accidental UART reconfiguration fail this harness. */
void uart_init(uart_device_number_t channel)
{
    (void)channel;
    g_uart_init_calls++;
}

void uart_configure(uart_device_number_t channel, uint32_t baud,
                    uint32_t width, uint32_t stop, uint32_t parity)
{
    (void)channel;
    (void)baud;
    (void)width;
    (void)stop;
    (void)parity;
    g_uart_configure_calls++;
}

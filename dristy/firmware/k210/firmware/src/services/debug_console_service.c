#include "debug_console_service.h"

#include <string.h>

#if defined(DEBUG_CONSOLE_RX_TESTING)
#include "debug_console_rx_test_platform.h"
#else
#include <syscalls.h>
#endif

#include "hal_uart.h"

#define DEBUG_DIAGNOSTIC_BYTES 1024U
#define DEBUG_DIAGNOSTIC_MASK (DEBUG_DIAGNOSTIC_BYTES - 1U)

static uint8_t g_diagnostic[DEBUG_DIAGNOSTIC_BYTES];
static volatile uint32_t g_diagnostic_write;
static volatile uint8_t g_framed_mode;

_Static_assert((DEBUG_DIAGNOSTIC_BYTES & DEBUG_DIAGNOSTIC_MASK) == 0U,
               "diagnostic ring must be a power of two");

static void debug_console_divert_byte(uint8_t byte)
{
    uint32_t write = g_diagnostic_write;

    g_diagnostic[write & DEBUG_DIAGNOSTIC_MASK] = byte;
    __sync_synchronize();
    g_diagnostic_write = write + 1U;
}

static int debug_console_putchar(char value)
{
    uint8_t byte = (uint8_t)value;

    if(g_framed_mode)
        debug_console_divert_byte(byte);
    else
        hal_debug_uart_send(&byte, 1U);
    return (int)byte;
}

void debug_console_init(void)
{
    g_diagnostic_write = 0U;
    g_framed_mode = 0U;
    sys_register_putchar(debug_console_putchar);
}

void debug_console_start_rx(void)
{
    hal_debug_uart_start_rx();
}

size_t debug_console_read(uint8_t *data, size_t len)
{
    return hal_debug_uart_receive(data, len);
}

uint32_t debug_console_rx_dropped(void)
{
    return hal_debug_uart_rx_dropped();
}

void debug_console_write(const uint8_t *data, size_t len)
{
    if(!data || !len)
        return;
    if(!g_framed_mode)
    {
        hal_debug_uart_send(data, len);
        return;
    }
    for(size_t i = 0U; i < len; i++)
        debug_console_divert_byte(data[i]);
}

void debug_console_write_text(const char *text)
{
    debug_console_write((const uint8_t *)text, strlen(text));
}

void debug_console_set_framed_mode(uint8_t enabled)
{
    __sync_synchronize();
    g_framed_mode = enabled != 0U;
    __sync_synchronize();
}

uint8_t debug_console_framed_mode(void)
{
    return g_framed_mode;
}

void debug_console_write_wire(const uint8_t *data, size_t len)
{
    if(data && len)
        hal_debug_uart_send(data, len);
}

uint32_t debug_console_diagnostic_cursor(void)
{
    __sync_synchronize();
    return g_diagnostic_write;
}

size_t debug_console_read_diagnostics_since(uint32_t *cursor,
                                            uint8_t *data, size_t capacity,
                                            uint32_t *lost_bytes)
{
    uint32_t read;
    uint32_t write;
    uint32_t oldest;
    size_t count;

    if(lost_bytes)
        *lost_bytes = 0U;
    if(!cursor || !data || !capacity)
        return 0U;
    read = *cursor;
    __sync_synchronize();
    write = g_diagnostic_write;
    oldest = write > DEBUG_DIAGNOSTIC_BYTES ?
             write - DEBUG_DIAGNOSTIC_BYTES : 0U;
    if((int32_t)(read - oldest) < 0)
    {
        if(lost_bytes)
            *lost_bytes = oldest - read;
        read = oldest;
    }
    if((int32_t)(write - read) < 0)
        read = write;
    count = write - read;
    if(count > capacity)
        count = capacity;
    for(size_t i = 0U; i < count; i++)
        data[i] = g_diagnostic[(read + (uint32_t)i) & DEBUG_DIAGNOSTIC_MASK];
    *cursor = read + (uint32_t)count;
    return count;
}

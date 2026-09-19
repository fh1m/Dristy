#include "hal_uart.h"

#if defined(DEBUG_CONSOLE_RX_TESTING)
#include "debug_console_rx_test_platform.h"
#else
#include <uart.h>
#endif

#define HAL_DEBUG_UART_RX_BYTES 2048U
#define HAL_DEBUG_UART_RX_MASK (HAL_DEBUG_UART_RX_BYTES - 1U)
#define HAL_DEBUG_UART_FIFO_BYTES 8U
#define HAL_DEBUG_UART_RX_IRQ_PRIORITY 2U

static uint8_t g_rx_ring[HAL_DEBUG_UART_RX_BYTES];
static volatile uint32_t g_rx_read;
static volatile uint32_t g_rx_write;
static volatile uint32_t g_rx_dropped;
static uint8_t g_rx_started;

_Static_assert((HAL_DEBUG_UART_RX_BYTES & HAL_DEBUG_UART_RX_MASK) == 0U,
               "debug UART RX ring must be a power of two");

static void hal_debug_uart_push(uint8_t byte)
{
    uint32_t write = g_rx_write;
    uint32_t read;

    __sync_synchronize();
    read = g_rx_read;
    if((uint32_t)(write - read) >= HAL_DEBUG_UART_RX_BYTES)
    {
        g_rx_dropped++;
        return;
    }
    g_rx_ring[write & HAL_DEBUG_UART_RX_MASK] = byte;
    __sync_synchronize();
    g_rx_write = write + 1U;
}

static void hal_debug_uart_drain_hardware(uint8_t discard)
{
    uint8_t bytes[HAL_DEBUG_UART_FIFO_BYTES];
    uint32_t count;

    do
    {
        count = (uint32_t)uart_receive_data(
            UART_DEVICE_3, (char *)bytes, sizeof(bytes));
        if(!discard)
        {
            for(uint32_t i = 0U; i < count; i++)
                hal_debug_uart_push(bytes[i]);
        }
    } while(count != 0U);
}

static int hal_debug_uart_rx_irq(void *context)
{
    (void)context;
    /* Keep draining after the software ring fills so the UART IRQ is always
       acknowledged and the newest excess bytes are the ones discarded. */
    hal_debug_uart_drain_hardware(0U);
    return 0;
}

void hal_debug_uart_start_rx(void)
{
    if(g_rx_started)
        return;

    /* UART3 is configured by the SDK as the debug/stdout channel. Bytes sent
       while the UI boots can only be fragments of a retried handshake, so
       discard them before exposing the permanent interrupt-backed stream. */
    hal_debug_uart_drain_hardware(1U);
    g_rx_read = 0U;
    g_rx_write = 0U;
    g_rx_dropped = 0U;
    __sync_synchronize();
    uart_irq_register(UART_DEVICE_3, UART_RECEIVE,
                      hal_debug_uart_rx_irq, NULL,
                      HAL_DEBUG_UART_RX_IRQ_PRIORITY);
    g_rx_started = 1U;
}

uint32_t hal_debug_uart_receive(uint8_t *data, size_t len)
{
    uint32_t read;
    uint32_t write;
    uint32_t available;
    size_t count;

    if(!data || !len)
        return 0U;
    read = g_rx_read;
    __sync_synchronize();
    write = g_rx_write;
    available = write - read;
    count = len < (size_t)available ? len : (size_t)available;
    for(size_t i = 0U; i < count; i++)
        data[i] = g_rx_ring[(read + (uint32_t)i) & HAL_DEBUG_UART_RX_MASK];
    __sync_synchronize();
    g_rx_read = read + (uint32_t)count;
    return (uint32_t)count;
}

uint32_t hal_debug_uart_rx_dropped(void)
{
    __sync_synchronize();
    return g_rx_dropped;
}

void hal_debug_uart_send(const uint8_t *data, size_t len)
{
    if(data && len)
        uart_send_data(UART_DEVICE_3, (const char *)data, len);
}

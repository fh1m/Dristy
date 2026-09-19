#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "hal_external_link_test_platform.h"
#include "hal_external_link.h"

static uart_t g_uart;
static i2c_t g_i2c;
volatile uart_t *uart[1] = {&g_uart};
volatile i2c_t *i2c[1] = {&g_i2c};
static plic_irq_callback_t g_uart_rx_irq;
static void *g_uart_rx_irq_context;
static uint8_t g_uart_hardware_rx[8];
static size_t g_uart_hardware_rx_size;
static uint8_t g_uart_received[8];
static size_t g_uart_received_size;
static uint32_t g_uart_irq_register_calls;
static uint32_t g_uart_irq_unregister_calls;
static plic_irq_callback_t g_i2c_irq;
static void *g_i2c_irq_context;
static uint8_t g_i2c_irq_enabled;
static uint32_t g_i2c_irq_register_calls;
static uint32_t g_i2c_irq_unregister_calls;
static uint32_t g_i2c_fifo[8];
static size_t g_i2c_fifo_size;
static uint8_t g_i2c_hardware_rx[8];
static size_t g_i2c_hardware_rx_size;
static uint32_t g_i2c_commands[64];
static size_t g_i2c_command_count;
static size_t g_i2c_expected_commands;
static uint8_t g_i2c_source[32];
static size_t g_i2c_source_size;
static size_t g_i2c_source_position;
static uint8_t g_i2c_fifo_gap;

static void require_true(uint8_t condition, const char *message)
{
    if(condition)
        return;
    fprintf(stderr, "FAIL: %s\n", message);
    exit(1);
}

static void receive_uart_byte(void *context, uint8_t byte)
{
    (void)context;
    require_true(g_uart_received_size < sizeof(g_uart_received),
                 "UART callback receive capacity exceeded");
    g_uart_received[g_uart_received_size++] = byte;
}

static void reset_i2c_model(const uint8_t *source, size_t source_size,
                            size_t expected_commands)
{
    memset(&g_i2c, 0, sizeof(g_i2c));
    memset(g_i2c_fifo, 0, sizeof(g_i2c_fifo));
    memset(g_i2c_hardware_rx, 0, sizeof(g_i2c_hardware_rx));
    memset(g_i2c_commands, 0, sizeof(g_i2c_commands));
    memset(g_i2c_source, 0, sizeof(g_i2c_source));
    if(source_size > sizeof(g_i2c_source))
        source_size = sizeof(g_i2c_source);
    if(source_size != 0U)
        memcpy(g_i2c_source, source, source_size);
    g_i2c_fifo_size = 0U;
    g_i2c_hardware_rx_size = 0U;
    g_i2c_command_count = 0U;
    g_i2c_expected_commands = expected_commands;
    g_i2c_source_size = source_size;
    g_i2c_source_position = 0U;
    g_i2c_fifo_gap = 0U;
}

static void invoke_i2c_irq(uint32_t status)
{
    if(!g_i2c_irq_enabled || !g_i2c_irq)
        return;
    g_i2c.intr_stat = status & g_i2c.intr_mask;
    if(g_i2c.intr_stat != 0U)
        require_true(g_i2c_irq(g_i2c_irq_context) == 0,
                     "I2C interrupt must acknowledge input");
    g_i2c.intr_stat = 0U;
}

static void drain_i2c_command(void)
{
    uint32_t command;
    uint32_t irq_status = 0U;

    require_true(g_i2c_fifo_size != 0U,
                 "I2C hardware cannot drain an empty FIFO");
    command = g_i2c_fifo[0];
    memmove(g_i2c_fifo, g_i2c_fifo + 1,
            (g_i2c_fifo_size - 1U) * sizeof(g_i2c_fifo[0]));
    g_i2c_fifo_size--;
    g_i2c.txflr = (uint32_t)g_i2c_fifo_size;
    if((command & I2C_DATA_CMD_CMD) != 0U)
    {
        uint8_t value = g_i2c_source_position < g_i2c_source_size ?
            g_i2c_source[g_i2c_source_position] :
            (uint8_t)(0x80U + g_i2c_source_position);

        require_true(g_i2c_hardware_rx_size <
                         sizeof(g_i2c_hardware_rx),
                     "I2C RX FIFO overflowed in the model");
        g_i2c_source_position++;
        g_i2c_hardware_rx[g_i2c_hardware_rx_size++] = value;
        g_i2c.rxflr = (uint32_t)g_i2c_hardware_rx_size;
        irq_status |= I2C_INTR_STAT_RX_FULL;
    }
    if(g_i2c_fifo_size <= g_i2c.tx_tl)
        irq_status |= I2C_INTR_STAT_TX_EMPTY;
    if(g_i2c_fifo_size == 0U &&
       g_i2c_command_count < g_i2c_expected_commands)
        g_i2c_fifo_gap = 1U;
    invoke_i2c_irq(irq_status);
    if(g_i2c_fifo_size == 0U &&
       g_i2c_command_count == g_i2c_expected_commands)
    {
        g_i2c.status = I2C_STATUS_TFE;
        invoke_i2c_irq(I2C_INTR_STAT_STOP_DET |
                       (g_i2c_hardware_rx_size != 0U ?
                            I2C_INTR_STAT_RX_FULL : 0U));
    }
}

static void run_i2c_transaction(const uint8_t *tx, size_t tx_size,
                                const uint8_t *source, size_t rx_size)
{
    uint8_t rx[32];
    size_t total = tx_size + rx_size;

    require_true(rx_size <= sizeof(rx), "I2C test RX capacity exceeded");
    memset(rx, 0xa5, sizeof(rx));
    reset_i2c_model(source, rx_size, total);
    hal_external_i2c_controller_start(
        0x42U, 100000U, tx, (uint32_t)tx_size,
        rx, (uint32_t)rx_size);
    require_true(g_i2c_command_count ==
                     (total < 8U ? total : 8U),
                 "controller must prime exactly one FIFO window");
    while(g_i2c_fifo_size != 0U)
        drain_i2c_command();
    require_true(g_i2c_command_count == total,
                 "all I2C commands must be issued");
    require_true(!g_i2c_fifo_gap,
                 "controller must refill before the FIFO becomes empty");
    require_true(hal_external_i2c_controller_tx_accepted() == tx_size,
                 "controller TX acceptance mismatch");
    require_true(hal_external_i2c_controller_rx_received() == rx_size,
                 "controller RX completion mismatch");
    require_true(rx_size == 0U || memcmp(rx, source, rx_size) == 0,
                 "controller RX payload mismatch");
    require_true(hal_external_i2c_controller_idle(),
                 "drained controller transaction must become idle");
    for(size_t index = 0U; index < tx_size; ++index)
        require_true(g_i2c_commands[index] == tx[index],
                     "write prefix changed in the command stream");
    for(size_t index = tx_size; index < total; ++index)
        require_true(g_i2c_commands[index] == I2C_DATA_CMD_CMD,
                     "read command ordering changed");
    hal_external_i2c_stop();
}

int main(void)
{
    static const uint8_t data[10] = {0U, 1U, 2U, 3U, 4U,
                                      5U, 6U, 7U, 8U, 9U};

    g_uart.TFL = 0U;
    g_uart.THR = 0xffU;
    require_true(hal_external_uart_send_ready(data, sizeof(data)) == 8U,
                 "empty FIFO must accept exactly eight bytes");
    require_true(g_uart.THR == 7U,
                 "empty FIFO must queue the first eight bytes in order");

    g_uart.TFL = 7U;
    g_uart.THR = 0xffU;
    require_true(hal_external_uart_send_ready(data, sizeof(data)) == 1U,
                 "TFL=7 must expose one byte of capacity");
    require_true(g_uart.THR == 0U,
                 "single free slot must queue only the first byte");

    g_uart.TFL = 8U;
    g_uart.THR = 0xa5U;
    require_true(hal_external_uart_send_ready(data, sizeof(data)) == 0U,
                 "TFL=8 must report a full FIFO");
    require_true(g_uart.THR == 0xa5U,
                 "full FIFO must not touch THR");

    g_uart.TFL = 9U;
    require_true(hal_external_uart_send_ready(data, sizeof(data)) == 0U,
                 "out-of-range TFL must fail closed");
    require_true(hal_external_uart_send_ready(NULL, sizeof(data)) == 0U,
                 "null input must be a no-op");

    g_uart.TFL = 0U;
    g_uart.LSR = 1U << 6;
    require_true(hal_external_uart_tx_idle(),
                 "TEMT with an empty FIFO must report drained");
    g_uart.TFL = 1U;
    require_true(!hal_external_uart_tx_idle(),
                 "queued FIFO data must prevent drain completion");
    g_uart.TFL = 0U;
    g_uart.LSR = 0U;
    require_true(!hal_external_uart_tx_idle(),
                 "busy shift register must prevent drain completion");
    memcpy(g_uart_hardware_rx, data, 8U);
    g_uart_hardware_rx_size = 8U;
    hal_external_uart_init(115200U, receive_uart_byte, NULL);
    require_true(g_uart_irq_register_calls == 1U && g_uart_rx_irq != NULL,
                 "UART init must register one RX interrupt");
    memcpy(g_uart_hardware_rx, data, 8U);
    g_uart_hardware_rx_size = 8U;
    require_true(g_uart_rx_irq(g_uart_rx_irq_context) == 0,
                 "UART RX interrupt must acknowledge input");
    require_true(g_uart_hardware_rx_size == 0U &&
                 g_uart_received_size == 8U &&
                 memcmp(g_uart_received, data, 8U) == 0,
                 "UART RX interrupt must preserve all FIFO bytes");
    hal_external_uart_stop();
    require_true(g_uart_irq_unregister_calls == 1U && g_uart_rx_irq == NULL,
                 "UART stop must unregister its RX interrupt");

    {
        static const uint8_t prefix[1] = {0x10U};
        static const uint8_t response[8] = {
            'H', 'K', '2', 'I', '2', 'C', '4', '2',
        };

        run_i2c_transaction(prefix, sizeof(prefix),
                            response, sizeof(response));
    }
    {
        uint8_t prefix[20];
        uint8_t response[20];

        for(size_t index = 0U; index < sizeof(prefix); ++index)
        {
            prefix[index] = (uint8_t)(index + 1U);
            response[index] = (uint8_t)(0xc0U + index);
        }
        run_i2c_transaction(prefix, sizeof(prefix),
                            response, sizeof(response));
    }
    {
        static const uint8_t prefix[1] = {0x20U};
        uint8_t rx[16];
        uint8_t before[16];

        memset(rx, 0x5a, sizeof(rx));
        reset_i2c_model(NULL, 0U, sizeof(prefix) + sizeof(rx));
        hal_external_i2c_controller_start(
            0x42U, 100000U, prefix, sizeof(prefix), rx, sizeof(rx));
        drain_i2c_command();
        memcpy(before, rx, sizeof(rx));
        hal_external_i2c_stop();
        invoke_i2c_irq(I2C_INTR_STAT_TX_EMPTY |
                       I2C_INTR_STAT_RX_FULL);
        require_true(memcmp(rx, before, sizeof(rx)) == 0,
                     "stopped controller must not mutate borrowed RX");
    }
    {
        static const uint8_t prefix[1] = {0x30U};

        reset_i2c_model(NULL, 0U, sizeof(prefix));
        hal_external_i2c_controller_start(
            0x41U, 100000U, prefix, sizeof(prefix), NULL, 0U);
        g_i2c.tx_abrt_source = 1U;
        invoke_i2c_irq(I2C_INTR_STAT_TX_ABRT);
        require_true(hal_external_i2c_controller_aborted(),
                     "NACK abort must remain latched");
        hal_external_i2c_stop();
    }
    require_true(g_i2c_irq_register_calls == 4U &&
                 g_i2c_irq_unregister_calls == 4U,
                 "each controller transaction must own one IRQ lifecycle");
    puts("HAL_EXTERNAL_LINK_OK boundaries=4 depth=8 idle=3 uart_irq=8 i2c_continuous=2 i2c_stop=1 i2c_abort=1");
    return 0;
}

void board_external_link_uart_pins(void) {}
void board_external_link_i2c_pins(void) {}
void uart_init(int device) { (void)device; }

void uart_configure(int device, uint32_t baud, int width,
                    int stop, int parity)
{
    (void)device; (void)baud; (void)width; (void)stop; (void)parity;
}

int uart_receive_data(int device, char *data, size_t length)
{
    size_t count;

    (void)device;
    count = length < g_uart_hardware_rx_size ?
        length : g_uart_hardware_rx_size;
    if(count != 0U)
    {
        memcpy(data, g_uart_hardware_rx, count);
        memmove(g_uart_hardware_rx, g_uart_hardware_rx + count,
                g_uart_hardware_rx_size - count);
        g_uart_hardware_rx_size -= count;
    }
    return (int)count;
}

int uart_send_data(int device, const char *data, size_t length)
{
    (void)device; (void)data;
    return (int)length;
}

void uart_irq_register(int device, int mode,
                       plic_irq_callback_t callback, void *context,
                       uint32_t priority)
{
    require_true(device == UART_DEVICE_1 && mode == UART_RECEIVE &&
                 priority == 2U, "UART RX interrupt registration mismatch");
    g_uart_rx_irq = callback;
    g_uart_rx_irq_context = context;
    g_uart_irq_register_calls++;
}

void uart_irq_unregister(int device, int mode)
{
    require_true(device == UART_DEVICE_1 && mode == UART_RECEIVE,
                 "UART RX interrupt unregister mismatch");
    g_uart_rx_irq = NULL;
    g_uart_rx_irq_context = NULL;
    g_uart_irq_unregister_calls++;
}

void i2c_init_as_slave(int device, uint32_t address, uint32_t address_width,
                       const i2c_slave_handler_t *handler)
{
    (void)device; (void)address; (void)address_width; (void)handler;
}

void i2c_init(int device, uint32_t address, uint32_t address_width,
              uint32_t frequency)
{
    (void)device; (void)address; (void)address_width; (void)frequency;
    g_i2c.status = I2C_STATUS_ACTIVITY;
}

int plic_irq_disable(int interrupt)
{
    require_true(interrupt == IRQN_I2C0_INTERRUPT,
                 "unexpected disabled IRQ");
    g_i2c_irq_enabled = 0U;
    return 0;
}

int plic_irq_enable(int interrupt)
{
    require_true(interrupt == IRQN_I2C0_INTERRUPT,
                 "unexpected enabled IRQ");
    g_i2c_irq_enabled = 1U;
    return 0;
}

int plic_set_priority(int interrupt, uint32_t priority)
{
    require_true(interrupt == IRQN_I2C0_INTERRUPT && priority == 2U,
                 "I2C IRQ priority mismatch");
    return 0;
}

void plic_irq_register(int interrupt,
                       plic_irq_callback_t callback, void *context)
{
    require_true(interrupt == IRQN_I2C0_INTERRUPT && callback != NULL,
                 "I2C IRQ registration mismatch");
    g_i2c_irq = callback;
    g_i2c_irq_context = context;
    g_i2c_irq_register_calls++;
}

void plic_irq_unregister(int interrupt)
{
    require_true(interrupt == IRQN_I2C0_INTERRUPT,
                 "I2C IRQ unregister mismatch");
    g_i2c_irq = NULL;
    g_i2c_irq_context = NULL;
    g_i2c_irq_unregister_calls++;
}

void hal_external_i2c_test_write_command(uint32_t command)
{
    require_true(g_i2c_fifo_size < sizeof(g_i2c_fifo) /
                                      sizeof(g_i2c_fifo[0]),
                 "controller wrote past the eight-entry FIFO");
    require_true(g_i2c_command_count < sizeof(g_i2c_commands) /
                                          sizeof(g_i2c_commands[0]),
                 "I2C command log overflowed");
    g_i2c_fifo[g_i2c_fifo_size++] = command;
    g_i2c_commands[g_i2c_command_count++] = command;
    g_i2c.txflr = (uint32_t)g_i2c_fifo_size;
}

uint32_t hal_external_i2c_test_read_data(void)
{
    uint8_t value;

    require_true(g_i2c_hardware_rx_size != 0U,
                 "controller read an empty RX FIFO");
    value = g_i2c_hardware_rx[0];
    memmove(g_i2c_hardware_rx, g_i2c_hardware_rx + 1,
            g_i2c_hardware_rx_size - 1U);
    g_i2c_hardware_rx_size--;
    g_i2c.rxflr = (uint32_t)g_i2c_hardware_rx_size;
    return value;
}

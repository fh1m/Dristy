#include "hal_external_link.h"

#if defined(HAL_EXTERNAL_LINK_TESTING)
#include "hal_external_link_test_platform.h"
#else
#include <i2c.h>
#include <platform.h>
#include <plic.h>
#include <uart.h>

#include "../../../firmware/src/internal/hk_board_port.h"
#endif

#if defined(HAL_EXTERNAL_LINK_TESTING)
#define PREPARE_EXTERNAL_UART() board_external_link_uart_pins()
#define PREPARE_EXTERNAL_I2C() board_external_link_i2c_pins()
#else
#define PREPARE_EXTERNAL_UART() hk_board_ops.external_uart_prepare()
#define PREPARE_EXTERNAL_I2C() hk_board_ops.external_i2c_prepare()
#endif

/* K210 general-purpose UART1/2/3 expose an 8-byte TX FIFO. */
#define HAL_EXTERNAL_UART_FIFO_DEPTH 8U
#define HAL_EXTERNAL_UART_LSR_TEMT (1U << 6)
#define HAL_EXTERNAL_UART_RX_IRQ_PRIORITY 2U
#define HAL_EXTERNAL_I2C_IRQ_PRIORITY 2U
#define HAL_EXTERNAL_I2C_FIFO_DEPTH 8U
#if defined(HAL_EXTERNAL_LINK_TESTING)
#define HAL_EXTERNAL_UART_ADAPTER (uart[UART_DEVICE_1])
#else
#define HAL_EXTERNAL_UART_ADAPTER ((volatile uart_t *)UART1_BASE_ADDR)
#endif

static const hal_external_i2c_callbacks_t *g_i2c_callbacks;
static uint8_t g_i2c_active;
static uint8_t g_i2c_skip_sdk_receive;
static const uint8_t *g_i2c_controller_tx;
static uint8_t *g_i2c_controller_rx;
static uint32_t g_i2c_controller_tx_size;
static uint32_t g_i2c_controller_rx_size;
static volatile uint32_t g_i2c_controller_commands;
static volatile uint32_t g_i2c_controller_rx_received;
static volatile uint8_t g_i2c_controller_aborted;
static uint8_t g_i2c_controller_active;
static hal_external_uart_receive_fn g_uart_receive;
static void *g_uart_receive_context;
static uint8_t g_uart_active;

#if defined(HAL_EXTERNAL_LINK_TESTING)
#define I2C_CONTROLLER_WRITE(adapter, value) \
    hal_external_i2c_test_write_command((value))
#define I2C_CONTROLLER_READ(adapter) \
    hal_external_i2c_test_read_data()
#else
#define I2C_CONTROLLER_WRITE(adapter, value) ((adapter)->data_cmd = (value))
#define I2C_CONTROLLER_READ(adapter) ((adapter)->data_cmd)
#endif

static void i2c_controller_abort(volatile i2c_t *adapter)
{
    g_i2c_controller_aborted = 1U;
    adapter->intr_mask = 0U;
}

static void i2c_controller_drain_rx(volatile i2c_t *adapter)
{
    uint32_t received = g_i2c_controller_rx_received;
    uint32_t drained = 0U;

    while(adapter->rxflr != 0U &&
          received < g_i2c_controller_rx_size &&
          drained < HAL_EXTERNAL_I2C_FIFO_DEPTH)
    {
        g_i2c_controller_rx[received] =
            (uint8_t)I2C_CONTROLLER_READ(adapter);
        __sync_synchronize();
        g_i2c_controller_rx_received = ++received;
        ++drained;
    }
    if(adapter->rxflr != 0U)
        i2c_controller_abort(adapter);
}

static void i2c_controller_fill_tx(volatile i2c_t *adapter)
{
    uint32_t commands = g_i2c_controller_commands;
    uint32_t total = g_i2c_controller_tx_size +
                     g_i2c_controller_rx_size;

    while(adapter->txflr < HAL_EXTERNAL_I2C_FIFO_DEPTH &&
          commands < total)
    {
        uint32_t command = commands < g_i2c_controller_tx_size ?
            I2C_DATA_CMD_DATA(g_i2c_controller_tx[commands]) :
            I2C_DATA_CMD_CMD;

        I2C_CONTROLLER_WRITE(adapter, command);
        __sync_synchronize();
        g_i2c_controller_commands = ++commands;
    }
    if(commands == total)
        adapter->intr_mask &= ~I2C_INTR_MASK_TX_EMPTY;
}

static int i2c_controller_irq(void *context)
{
    volatile i2c_t *adapter = i2c[I2C_DEVICE_0];
    uint32_t status = adapter->intr_stat;

    (void)context;
    if(!g_i2c_controller_active)
        return 0;
    if((status & I2C_INTR_STAT_TX_ABRT) != 0U)
    {
        (void)adapter->clr_tx_abrt;
        i2c_controller_abort(adapter);
        return 0;
    }
    if((status & I2C_INTR_STAT_RX_OVER) != 0U)
    {
        (void)adapter->clr_rx_over;
        i2c_controller_abort(adapter);
        return 0;
    }
    if((status & I2C_INTR_STAT_RX_FULL) != 0U ||
       (status & I2C_INTR_STAT_STOP_DET) != 0U)
        i2c_controller_drain_rx(adapter);
    if(!g_i2c_controller_aborted &&
       (status & I2C_INTR_STAT_TX_EMPTY) != 0U)
        i2c_controller_fill_tx(adapter);
    if((status & I2C_INTR_STAT_STOP_DET) != 0U)
        (void)adapter->clr_stop_det;
    return 0;
}

static int uart_receive_irq(void *context)
{
    uint8_t bytes[HAL_EXTERNAL_UART_FIFO_DEPTH];
    size_t count;

    (void)context;
    do
    {
        count = (size_t)uart_receive_data(
            UART_DEVICE_1, (char *)bytes, sizeof(bytes));
        if(g_uart_receive)
        {
            for(size_t index = 0U; index < count; ++index)
                g_uart_receive(g_uart_receive_context, bytes[index]);
        }
    } while(count != 0U);
    return 0;
}

static void i2c_receive(uint32_t data)
{
    if(g_i2c_skip_sdk_receive)
    {
        g_i2c_skip_sdk_receive = 0U;
        return;
    }
    if(g_i2c_callbacks && g_i2c_callbacks->receive)
        g_i2c_callbacks->receive((uint8_t)data);
}

static uint32_t i2c_transmit(void)
{
    if(g_i2c_callbacks && g_i2c_callbacks->transmit)
        return g_i2c_callbacks->transmit();
    return 0U;
}

static void i2c_event(i2c_event_t event)
{
    if(!g_i2c_callbacks || !g_i2c_callbacks->event)
        return;
    if(event == I2C_EV_STOP)
    {
        volatile i2c_t *adapter = i2c[I2C_DEVICE_0];
        uint8_t sdk_will_read = (adapter->intr_stat & I2C_INTR_STAT_RX_FULL) != 0U;

        /* The SDK announces STOP before servicing RX_FULL and consumes only
           one FIFO byte per IRQ. Drain the completed write here so the main
           loop can prepare the response before the master's read START. */
        while(adapter->rxflr != 0U)
            g_i2c_callbacks->receive((uint8_t)adapter->data_cmd);
        g_i2c_skip_sdk_receive = sdk_will_read;
    }
    g_i2c_callbacks->event(event == I2C_EV_STOP ? HAL_EXTERNAL_I2C_EVENT_STOP :
                                                HAL_EXTERNAL_I2C_EVENT_START);
}

static const i2c_slave_handler_t g_i2c_handler = {
    .on_receive = i2c_receive,
    .on_transmit = i2c_transmit,
    .on_event = i2c_event,
};

void hal_external_uart_stop(void)
{
    if(!g_uart_active)
        return;
    uart_irq_unregister(UART_DEVICE_1, UART_RECEIVE);
    g_uart_active = 0U;
    g_uart_receive = NULL;
    g_uart_receive_context = NULL;
    uart_init(UART_DEVICE_1);
}

void hal_external_uart_init(uint32_t baud,
                            hal_external_uart_receive_fn receive,
                            void *context)
{
    hal_external_i2c_stop();
    hal_external_uart_stop();
    PREPARE_EXTERNAL_UART();
    uart_init(UART_DEVICE_1);
    uart_configure(UART_DEVICE_1, baud, UART_BITWIDTH_8BIT, UART_STOP_1, UART_PARITY_NONE);
    (void)uart_receive_irq(NULL);
    g_uart_receive = receive;
    g_uart_receive_context = context;
    uart_irq_register(UART_DEVICE_1, UART_RECEIVE, uart_receive_irq, NULL,
                      HAL_EXTERNAL_UART_RX_IRQ_PRIORITY);
    g_uart_active = 1U;
}

size_t hal_external_uart_receive(uint8_t *data, size_t len)
{
    return (size_t)uart_receive_data(UART_DEVICE_1, (char *)data, len);
}

void hal_external_uart_send(const uint8_t *data, size_t len)
{
    if(data && len)
        uart_send_data(UART_DEVICE_1, (const char *)data, len);
}

size_t hal_external_uart_send_ready(const uint8_t *data, size_t len)
{
    volatile uart_t *adapter = HAL_EXTERNAL_UART_ADAPTER;
    uint32_t level;
    size_t capacity;

    if(!data || !len)
        return 0U;
    level = adapter->TFL;
    if(level >= HAL_EXTERNAL_UART_FIFO_DEPTH)
        return 0U;
    capacity = HAL_EXTERNAL_UART_FIFO_DEPTH - level;
    if(capacity > len)
        capacity = len;
    for(size_t i = 0U; i < capacity; i++)
        adapter->THR = data[i];
    return capacity;
}

uint8_t hal_external_uart_tx_idle(void)
{
    volatile uart_t *adapter = HAL_EXTERNAL_UART_ADAPTER;

    return adapter->TFL == 0U &&
           (adapter->LSR & HAL_EXTERNAL_UART_LSR_TEMT) != 0U;
}

void hal_external_i2c_init(uint8_t address, const hal_external_i2c_callbacks_t *callbacks)
{
    hal_external_i2c_stop();
    g_i2c_callbacks = callbacks;
    g_i2c_skip_sdk_receive = 0U;
    PREPARE_EXTERNAL_I2C();
    i2c_init_as_slave(I2C_DEVICE_0, address, 7U, &g_i2c_handler);
    g_i2c_active = 1U;
}

void hal_external_i2c_controller_start(
    uint8_t address, uint32_t frequency_hz,
    const uint8_t *tx, uint32_t tx_size,
    uint8_t *rx, uint32_t rx_size)
{
    volatile i2c_t *adapter;

    hal_external_i2c_stop();
    PREPARE_EXTERNAL_I2C();
    i2c_init(I2C_DEVICE_0, address, 7U, frequency_hz);
    adapter = i2c[I2C_DEVICE_0];
    (void)adapter->clr_tx_abrt;
    (void)adapter->clr_intr;
    g_i2c_controller_tx = tx;
    g_i2c_controller_rx = rx;
    g_i2c_controller_tx_size = tx_size;
    g_i2c_controller_rx_size = rx_size;
    g_i2c_controller_commands = 0U;
    g_i2c_controller_rx_received = 0U;
    g_i2c_controller_aborted = 0U;
    g_i2c_controller_active = 1U;
    g_i2c_active = 1U;
    adapter->rx_tl = I2C_RX_TL_VALUE(0U);
    adapter->tx_tl = I2C_TX_TL_VALUE(4U);
    plic_irq_disable(IRQN_I2C0_INTERRUPT);
    plic_set_priority(IRQN_I2C0_INTERRUPT,
                      HAL_EXTERNAL_I2C_IRQ_PRIORITY);
    plic_irq_register(IRQN_I2C0_INTERRUPT, i2c_controller_irq, NULL);
    adapter->intr_mask = I2C_INTR_MASK_RX_FULL |
                         I2C_INTR_MASK_RX_OVER |
                         I2C_INTR_MASK_TX_EMPTY |
                         I2C_INTR_MASK_TX_ABRT |
                         I2C_INTR_MASK_STOP_DET;
    i2c_controller_fill_tx(adapter);
    plic_irq_enable(IRQN_I2C0_INTERRUPT);
}

uint8_t hal_external_i2c_controller_aborted(void)
{
    return (uint8_t)(g_i2c_controller_aborted ||
                     i2c[I2C_DEVICE_0]->tx_abrt_source != 0U);
}

uint32_t hal_external_i2c_controller_tx_accepted(void)
{
    uint32_t commands;

    __sync_synchronize();
    commands = g_i2c_controller_commands;
    return commands < g_i2c_controller_tx_size ?
        commands : g_i2c_controller_tx_size;
}

uint32_t hal_external_i2c_controller_rx_received(void)
{
    __sync_synchronize();
    return g_i2c_controller_rx_received;
}

uint8_t hal_external_i2c_controller_idle(void)
{
    volatile i2c_t *adapter = i2c[I2C_DEVICE_0];

    return (uint8_t)(g_i2c_controller_active &&
                     !g_i2c_controller_aborted &&
                     g_i2c_controller_commands ==
                         g_i2c_controller_tx_size +
                         g_i2c_controller_rx_size &&
                     g_i2c_controller_rx_received ==
                         g_i2c_controller_rx_size &&
                     (adapter->status & I2C_STATUS_ACTIVITY) == 0U &&
                     (adapter->status & I2C_STATUS_TFE) != 0U);
}

uint32_t hal_external_i2c_target_lock(void)
{
    plic_irq_disable(IRQN_I2C0_INTERRUPT);
    return g_i2c_active;
}

void hal_external_i2c_target_unlock(uint32_t state)
{
    if(state)
        plic_irq_enable(IRQN_I2C0_INTERRUPT);
}

void hal_external_i2c_stop(void)
{
    volatile i2c_t *adapter;

    if(!g_i2c_active)
        return;
    adapter = i2c[I2C_DEVICE_0];
    adapter->intr_mask = 0U;
    adapter->enable = 0U;
    plic_irq_disable(IRQN_I2C0_INTERRUPT);
    plic_irq_unregister(IRQN_I2C0_INTERRUPT);
    g_i2c_callbacks = NULL;
    g_i2c_controller_tx = NULL;
    g_i2c_controller_rx = NULL;
    g_i2c_controller_tx_size = 0U;
    g_i2c_controller_rx_size = 0U;
    g_i2c_controller_commands = 0U;
    g_i2c_controller_rx_received = 0U;
    g_i2c_controller_aborted = 0U;
    g_i2c_controller_active = 0U;
    g_i2c_active = 0U;
    g_i2c_skip_sdk_receive = 0U;
}

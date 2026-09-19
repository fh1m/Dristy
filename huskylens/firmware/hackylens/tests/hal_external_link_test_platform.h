#ifndef HK_HAL_EXTERNAL_LINK_TEST_PLATFORM_H
#define HK_HAL_EXTERNAL_LINK_TEST_PLATFORM_H

#include <stddef.h>
#include <stdint.h>

#define UART_DEVICE_1 0
#define UART_BITWIDTH_8BIT 8
#define UART_STOP_1 1
#define UART_PARITY_NONE 0
#define UART_RECEIVE 2
#define I2C_DEVICE_0 0
#define IRQN_I2C0_INTERRUPT 1
#define I2C_INTR_STAT_RX_OVER 0x02U
#define I2C_INTR_STAT_RX_FULL 0x04U
#define I2C_INTR_STAT_TX_EMPTY 0x10U
#define I2C_INTR_STAT_TX_ABRT 0x40U
#define I2C_INTR_STAT_STOP_DET 0x200U
#define I2C_INTR_MASK_RX_OVER I2C_INTR_STAT_RX_OVER
#define I2C_INTR_MASK_RX_FULL I2C_INTR_STAT_RX_FULL
#define I2C_INTR_MASK_TX_EMPTY I2C_INTR_STAT_TX_EMPTY
#define I2C_INTR_MASK_TX_ABRT I2C_INTR_STAT_TX_ABRT
#define I2C_INTR_MASK_STOP_DET I2C_INTR_STAT_STOP_DET
#define I2C_RXFLR_VALUE_MASK 0xffU
#define I2C_TXFLR_VALUE_MASK 0xffU
#define I2C_RX_TL_VALUE(value) ((uint32_t)(value))
#define I2C_TX_TL_VALUE(value) ((uint32_t)(value))
#define I2C_STATUS_ACTIVITY 0x01U
#define I2C_STATUS_TFNF 0x02U
#define I2C_STATUS_TFE 0x04U
#define I2C_DATA_CMD_DATA(value) ((uint32_t)(value))
#define I2C_DATA_CMD_CMD 0x100U

typedef struct
{
    volatile uint32_t THR;
    volatile uint32_t TFL;
    volatile uint32_t LSR;
} uart_t;

typedef struct
{
    volatile uint32_t intr_stat;
    volatile uint32_t raw_intr_stat;
    volatile uint32_t rxflr;
    volatile uint32_t data_cmd;
    volatile uint32_t intr_mask;
    volatile uint32_t rx_tl;
    volatile uint32_t tx_tl;
    volatile uint32_t enable;
    volatile uint32_t clr_intr;
    volatile uint32_t clr_rx_over;
    volatile uint32_t clr_tx_abrt;
    volatile uint32_t clr_stop_det;
    volatile uint32_t tx_abrt_source;
    volatile uint32_t status;
    volatile uint32_t txflr;
} i2c_t;

typedef enum
{
    I2C_EV_START = 0,
    I2C_EV_STOP,
} i2c_event_t;

typedef int (*plic_irq_callback_t)(void *context);

typedef struct
{
    void (*on_receive)(uint32_t data);
    uint32_t (*on_transmit)(void);
    void (*on_event)(i2c_event_t event);
} i2c_slave_handler_t;

extern volatile uart_t *uart[1];
extern volatile i2c_t *i2c[1];

void board_external_link_uart_pins(void);
void board_external_link_i2c_pins(void);
void uart_init(int device);
void uart_configure(int device, uint32_t baud, int width,
                    int stop, int parity);
int uart_receive_data(int device, char *data, size_t length);
int uart_send_data(int device, const char *data, size_t length);
void uart_irq_register(int device, int mode,
                       plic_irq_callback_t callback, void *context,
                       uint32_t priority);
void uart_irq_unregister(int device, int mode);
void i2c_init_as_slave(int device, uint32_t address, uint32_t address_width,
                       const i2c_slave_handler_t *handler);
void i2c_init(int device, uint32_t address, uint32_t address_width,
              uint32_t frequency);
int plic_irq_disable(int interrupt);
int plic_irq_enable(int interrupt);
int plic_set_priority(int interrupt, uint32_t priority);
void plic_irq_register(int interrupt,
                       plic_irq_callback_t callback, void *context);
void plic_irq_unregister(int interrupt);
void hal_external_i2c_test_write_command(uint32_t command);
uint32_t hal_external_i2c_test_read_data(void);

#endif

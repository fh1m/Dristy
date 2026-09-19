#ifndef HK_HAL_EXTERNAL_LINK_H
#define HK_HAL_EXTERNAL_LINK_H

#include <stddef.h>
#include <stdint.h>

typedef enum
{
    HAL_EXTERNAL_I2C_EVENT_START = 0,
    HAL_EXTERNAL_I2C_EVENT_STOP,
} hal_external_i2c_event_t;

typedef struct
{
    void (*receive)(uint8_t byte);
    uint8_t (*transmit)(void);
    void (*event)(hal_external_i2c_event_t event);
} hal_external_i2c_callbacks_t;

typedef void (*hal_external_uart_receive_fn)(void *context, uint8_t byte);

void hal_external_uart_init(uint32_t baud,
                            hal_external_uart_receive_fn receive,
                            void *context);
void hal_external_uart_stop(void);
size_t hal_external_uart_receive(uint8_t *data, size_t len);
void hal_external_uart_send(const uint8_t *data, size_t len);
/* Queue only bytes that fit in the UART TX FIFO right now.  This function
 * never waits for FIFO space and is therefore safe to call from core-0
 * service ticks that must keep STOP/watchdog polling responsive. */
size_t hal_external_uart_send_ready(const uint8_t *data, size_t len);
/* True only after both the TX FIFO and the shift register are empty. */
uint8_t hal_external_uart_tx_idle(void);
void hal_external_i2c_init(uint8_t address, const hal_external_i2c_callbacks_t *callbacks);
/* Start one controller transaction.  The fixed-state ISR keeps the K210's
 * eight-entry command FIFO non-empty across a combined write/read transfer.
 * tx and rx remain borrowed by the caller until stop. */
void hal_external_i2c_controller_start(
    uint8_t address, uint32_t frequency_hz,
    const uint8_t *tx, uint32_t tx_size,
    uint8_t *rx, uint32_t rx_size);
uint8_t hal_external_i2c_controller_aborted(void);
uint32_t hal_external_i2c_controller_tx_accepted(void);
uint32_t hal_external_i2c_controller_rx_received(void);
uint8_t hal_external_i2c_controller_idle(void);
/* Serialize target response publication with the I2C0 ISR. */
uint32_t hal_external_i2c_target_lock(void);
void hal_external_i2c_target_unlock(uint32_t state);
void hal_external_i2c_stop(void);

#endif

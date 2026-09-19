#ifndef HAL_SPI_H
#define HAL_SPI_H

#include <stddef.h>
#include <stdint.h>

void hal_spi_init(uint8_t device, uint32_t frame_bits);
uint32_t hal_spi_set_clock(uint8_t device, uint32_t hz);
void hal_spi0_enable_dvp_data(void);
void hal_spi_standard_init(uint8_t device, uint32_t hz);
void hal_spi_standard_send(uint8_t device, uint8_t chip_select, const uint8_t *cmd, size_t cmd_len, const uint8_t *data, size_t data_len);
/* Sends two buffers under one chip-select without allocating a join buffer. */
void hal_spi_standard_send_segments(uint8_t device, uint8_t chip_select,
                                    const uint8_t *first, size_t first_len,
                                    const uint8_t *second, size_t second_len);
void hal_spi_standard_receive(uint8_t device, uint8_t chip_select, const uint8_t *cmd, size_t cmd_len, uint8_t *data, size_t data_len);
void hal_spi_fifo_config(uint8_t device, uint32_t hz, uint8_t chip_select);
void hal_spi_fifo_set_tmod_tx(uint8_t device);
void hal_spi_fifo_set_frame_bits(uint8_t device, uint32_t bits);
/* Returns zero if the controller does not accept and drain the transfer before
 * its finite deadline. */
uint8_t hal_spi_fifo_send_bytes(uint8_t device, uint8_t chip_select,
                                const uint8_t *data, size_t len);
/* Uses one caller-owned absolute hal_time_us() deadline.  This lets a
 * multi-transfer operation bound the whole transaction instead of granting
 * every segment a fresh timeout. */
uint8_t hal_spi_fifo_send_bytes_until(uint8_t device, uint8_t chip_select,
                                      const uint8_t *data, size_t len,
                                      uint64_t deadline_us);
/* Keeps one TX transaction active across bounded caller-sized writes. */
uint8_t hal_spi_fifo_tx_begin_until(
    uint8_t device, uint8_t chip_select, uint64_t deadline_us);
uint8_t hal_spi_fifo_tx_write_until(
    uint8_t device, const uint8_t *data, size_t len,
    uint64_t deadline_us);
/* Writes big-endian byte pairs as 16-bit SPI frames.  The caller must have
 * selected a 16-bit frame size before beginning the active transaction. */
uint8_t hal_spi_fifo_tx_write_u16be_until(
    uint8_t device, const uint8_t *data, size_t len,
    uint64_t deadline_us);
uint8_t hal_spi_fifo_tx_end_until(uint8_t device, uint64_t deadline_us);
void hal_spi_fifo_tx_abort(uint8_t device);
uint8_t hal_spi_fifo_xfer_u8(uint8_t device, uint8_t out);

#endif

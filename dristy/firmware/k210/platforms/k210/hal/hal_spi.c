#include "hal_spi.h"

#include <stddef.h>

#include <platform.h>
#include <spi.h>
#include <sysctl.h>

#include "hal_time.h"

#define HAL_SPI_FIFO_TIMEOUT_US 500000ULL

static uint8_t s_fifo_tx_active_mask;

static uint64_t deadline_to_cpu_cycles(uint64_t deadline_us)
{
    uint64_t frequency;
    uint64_t seconds;
    uint64_t cycles;

    if(deadline_us == UINT64_MAX)
        return UINT64_MAX;
    frequency = sysctl_clock_get_freq(SYSCTL_CLOCK_CPU);
    seconds = deadline_us / 1000000ULL;
    if(!frequency || seconds > UINT64_MAX / frequency)
        return UINT64_MAX;
    cycles = seconds * frequency;
    deadline_us %= 1000000ULL;
    if(deadline_us > (UINT64_MAX - cycles) / frequency)
        return UINT64_MAX;
    return cycles + deadline_us * frequency / 1000000ULL;
}

static uint8_t cpu_cycle_deadline_reached(uint64_t deadline_cycles)
{
    return (uint8_t)(deadline_cycles != UINT64_MAX &&
                     read_cycle() >= deadline_cycles);
}

static uint8_t fifo_tx_active(uint8_t device)
{
    return (uint8_t)(device < 4U &&
                     (s_fifo_tx_active_mask & (uint8_t)(1U << device)) != 0U);
}

static void fifo_tx_set_active(uint8_t device, uint8_t active)
{
    uint8_t bit = (uint8_t)(1U << device);

    if(active)
        s_fifo_tx_active_mask |= bit;
    else
        s_fifo_tx_active_mask &= (uint8_t)~bit;
}

static volatile spi_t *hal_spi_regs(uint8_t device)
{
    switch(device)
    {
    case 0:
        return (volatile spi_t *)SPI0_BASE_ADDR;
    case 1:
        return (volatile spi_t *)SPI1_BASE_ADDR;
    case 3:
        return (volatile spi_t *)SPI3_BASE_ADDR;
    default:
        return NULL;
    }
}

void hal_spi_init(uint8_t device, uint32_t frame_bits)
{
    spi_init((spi_device_num_t)device, SPI_WORK_MODE_0, SPI_FF_STANDARD, frame_bits, 0);
}

uint32_t hal_spi_set_clock(uint8_t device, uint32_t hz)
{
    return spi_set_clk_rate((spi_device_num_t)device, hz);
}

void hal_spi0_enable_dvp_data(void)
{
    sysctl_set_spi0_dvp_data(1);
}

void hal_spi_standard_init(uint8_t device, uint32_t hz)
{
    hal_spi_init(device, 8);
    hal_spi_set_clock(device, hz);
}

void hal_spi_standard_send(uint8_t device, uint8_t chip_select, const uint8_t *cmd, size_t cmd_len, const uint8_t *data, size_t data_len)
{
    spi_send_data_standard((spi_device_num_t)device, (spi_chip_select_t)chip_select, cmd, cmd_len, data, data_len);
}

void hal_spi_standard_send_segments(uint8_t device, uint8_t chip_select,
                                    const uint8_t *first, size_t first_len,
                                    const uint8_t *second, size_t second_len)
{
    volatile spi_t *spi = hal_spi_regs(device);
    uint32_t tmod_offset;
    size_t first_pos = 0U;
    size_t second_pos = 0U;

    if(!spi || chip_select >= 4U ||
       (first_len && !first) || (second_len && !second) ||
       (!first_len && !second_len))
        return;

    tmod_offset = device == 3U ? 10U : 8U;
    spi->ssienr = 0U;
    spi->ctrlr0 = (spi->ctrlr0 & ~(3U << tmod_offset)) |
                  ((uint32_t)SPI_TMOD_TRANS << tmod_offset);
    spi->ser = 1U << chip_select;
    spi->ssienr = 1U;

    while(first_pos < first_len || second_pos < second_len)
    {
        size_t room = 32U - spi->txflr;

        while(room && first_pos < first_len)
        {
            spi->dr[0] = first[first_pos++];
            room--;
        }
        while(room && second_pos < second_len)
        {
            spi->dr[0] = second[second_pos++];
            room--;
        }
    }

    while((spi->sr & 0x05U) != 0x04U)
        ;
    spi->ser = 0U;
    spi->ssienr = 0U;
}

void hal_spi_standard_receive(uint8_t device, uint8_t chip_select, const uint8_t *cmd, size_t cmd_len, uint8_t *data, size_t data_len)
{
    spi_receive_data_standard((spi_device_num_t)device, (spi_chip_select_t)chip_select, cmd, cmd_len, data, data_len);
}

void hal_spi_fifo_config(uint8_t device, uint32_t hz, uint8_t chip_select)
{
    volatile spi_t *spi = hal_spi_regs(device);
    if(!spi)
        return;

    hal_spi_init(device, 8);
    hal_spi_set_clock(device, hz);
    spi->ssienr = 0;
    spi->ctrlr0 = (spi->ctrlr0 & ~(3U << 8)) | (0U << 8);
    spi->ctrlr1 = 0;
    spi->ser = 1U << chip_select;
    while(spi->rxflr)
        (void)spi->dr[0];
    spi->ssienr = 1;
    fifo_tx_set_active(device, 0U);
}

void hal_spi_fifo_set_tmod_tx(uint8_t device)
{
    volatile spi_t *spi = hal_spi_regs(device);
    if(!spi)
        return;
    spi->ctrlr0 = (spi->ctrlr0 & ~(3U << 8)) | (1U << 8);
}

void hal_spi_fifo_set_frame_bits(uint8_t device, uint32_t bits)
{
    volatile spi_t *spi = hal_spi_regs(device);
    if(!spi || bits == 0)
        return;
    spi->ctrlr0 = ((bits - 1U) << 16) | (spi->ctrlr0 & 0x1FU);
}

uint8_t hal_spi_fifo_send_bytes_until(uint8_t device, uint8_t chip_select,
                                      const uint8_t *data, size_t len,
                                      uint64_t deadline_us)
{
    if((len && !data) ||
       !hal_spi_fifo_tx_begin_until(device, chip_select, deadline_us))
        return 0U;
    if(len == 0U)
        return hal_spi_fifo_tx_end_until(device, deadline_us);
    if(!hal_spi_fifo_tx_write_until(device, data, len, deadline_us))
    {
        hal_spi_fifo_tx_abort(device);
        return 0U;
    }
    return hal_spi_fifo_tx_end_until(device, deadline_us);
}

uint8_t hal_spi_fifo_tx_begin_until(
    uint8_t device, uint8_t chip_select, uint64_t deadline_us)
{
    volatile spi_t *spi = hal_spi_regs(device);

    if(!spi || device >= 4U || chip_select >= 4U ||
       fifo_tx_active(device) || hal_time_us() >= deadline_us)
        return 0U;
    spi->ssienr = 0U;
    hal_spi_fifo_set_tmod_tx(device);
    while(spi->rxflr)
        (void)spi->dr[0];
    spi->ser = 1U << chip_select;
    spi->ssienr = 1U;
    fifo_tx_set_active(device, 1U);
    return 1U;
}

uint8_t hal_spi_fifo_tx_write_until(
    uint8_t device, const uint8_t *data, size_t len,
    uint64_t deadline_us)
{
    volatile spi_t *spi = hal_spi_regs(device);
    uint64_t deadline_cycles = deadline_to_cpu_cycles(deadline_us);
    size_t i = 0U;

    if(!spi || device >= 4U ||
       !fifo_tx_active(device) || (len && !data))
        return 0U;

    while(i < len)
    {
        size_t fifo_len = 32U - spi->txflr;
        if(cpu_cycle_deadline_reached(deadline_cycles))
        {
            hal_spi_fifo_tx_abort(device);
            return 0U;
        }
        if(!fifo_len)
            continue;
        if(fifo_len > len - i)
            fifo_len = len - i;
        while(fifo_len--)
            spi->dr[0] = data[i++];
    }

    return 1U;
}

uint8_t hal_spi_fifo_tx_write_u16be_until(
    uint8_t device, const uint8_t *data, size_t len,
    uint64_t deadline_us)
{
    volatile spi_t *spi = hal_spi_regs(device);
    uint64_t deadline_cycles = deadline_to_cpu_cycles(deadline_us);
    size_t i = 0U;

    if(!spi || device >= 4U || !fifo_tx_active(device) ||
       (len && !data) || (len & 1U))
        return 0U;
    while(i < len)
    {
        size_t fifo_len = 32U - spi->txflr;
        if(cpu_cycle_deadline_reached(deadline_cycles))
        {
            hal_spi_fifo_tx_abort(device);
            return 0U;
        }
        if(!fifo_len)
            continue;
        if(fifo_len > (len - i) / 2U)
            fifo_len = (len - i) / 2U;
        while(fifo_len--)
        {
            spi->dr[0] = ((uint32_t)data[i] << 8) | data[i + 1U];
            i += 2U;
        }
    }

    return 1U;
}

uint8_t hal_spi_fifo_tx_end_until(uint8_t device, uint64_t deadline_us)
{
    volatile spi_t *spi = hal_spi_regs(device);

    if(!spi || device >= 4U || !fifo_tx_active(device))
        return 0U;

    while((spi->sr & 0x05U) != 0x04U)
    {
        if(hal_time_us() >= deadline_us)
            goto timeout;
    }
    if(hal_time_us() > deadline_us)
        goto timeout;
    spi->ser = 0;
    spi->ssienr = 0;
    fifo_tx_set_active(device, 0U);
    return 1U;

timeout:
    hal_spi_fifo_tx_abort(device);
    return 0U;
}

void hal_spi_fifo_tx_abort(uint8_t device)
{
    volatile spi_t *spi = hal_spi_regs(device);

    if(!spi || device >= 4U)
        return;
    spi->ser = 0U;
    spi->ssienr = 0U;
    fifo_tx_set_active(device, 0U);
}

uint8_t hal_spi_fifo_send_bytes(uint8_t device, uint8_t chip_select,
                                const uint8_t *data, size_t len)
{
    uint64_t now = hal_time_us();
    uint64_t deadline = UINT64_MAX - now < HAL_SPI_FIFO_TIMEOUT_US
                            ? UINT64_MAX
                            : now + HAL_SPI_FIFO_TIMEOUT_US;

    return hal_spi_fifo_send_bytes_until(device, chip_select, data, len,
                                         deadline);
}

uint8_t hal_spi_fifo_xfer_u8(uint8_t device, uint8_t out)
{
    volatile spi_t *spi = hal_spi_regs(device);
    if(!spi)
        return 0xFF;

    while(spi->txflr >= 32)
        ;
    spi->dr[0] = out;
    while(spi->rxflr == 0)
        ;
    return (uint8_t)spi->dr[0];
}

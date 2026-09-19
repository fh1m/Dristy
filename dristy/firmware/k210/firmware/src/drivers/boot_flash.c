#include "boot_flash.h"

#include <stddef.h>
#include <string.h>

#include "defaults.h"
#include "flash_layout.h"
#include "../internal/hk_board_port.h"
#include "hal_spi.h"
#include "hal_time.h"

#define BOOT_FLASH_CMD_READ 0x03U
#define BOOT_FLASH_CMD_READ_ID 0x9FU
#define BOOT_FLASH_CMD_READ_STATUS 0x05U
#define BOOT_FLASH_CMD_WRITE_ENABLE 0x06U
#define BOOT_FLASH_CMD_SECTOR_ERASE 0x20U
#define BOOT_FLASH_CMD_PAGE_PROGRAM 0x02U
#define BOOT_FLASH_STATUS_BUSY 0x01U
#define BOOT_FLASH_STATUS_WEL 0x02U
#define BOOT_FLASH_PROGRAM_TIMEOUT_US 500000UL
#define BOOT_FLASH_ERASE_TIMEOUT_US 3000000UL
#define BOOT_FLASH_INIT_TIMEOUT_US 3000000UL
#define BOOT_FLASH_POLL_FAILSAFE 5000000UL

static volatile uint32_t g_boot_flash_lock;
static boot_flash_info_t g_boot_flash_info;

static uint8_t boot_flash_on_core0(void)
{
    unsigned long core_id;
    asm volatile("csrr %0, mhartid" : "=r"(core_id));
    return core_id == 0U;
}

static void boot_flash_lock(void)
{
    while(__sync_lock_test_and_set(&g_boot_flash_lock, 1U))
        ;
    __sync_synchronize();
}

static void boot_flash_unlock(void)
{
    __sync_synchronize();
    __sync_lock_release(&g_boot_flash_lock);
}

static void boot_flash_cmd(const uint8_t *cmd, size_t cmd_len)
{
    hal_spi_standard_send(FLASH_SPI, FLASH_SPI_CS, cmd, cmd_len, NULL, 0);
}

static uint8_t boot_flash_read_status_unlocked(void)
{
    uint8_t cmd = BOOT_FLASH_CMD_READ_STATUS;
    uint8_t status = 0xFFU;
    hal_spi_standard_receive(FLASH_SPI, FLASH_SPI_CS, &cmd, 1U, &status, 1U);
    return status;
}

static boot_flash_result_t boot_flash_mark_fault(boot_flash_result_t result)
{
    g_boot_flash_info.faulted = 1U;
    return result;
}

static boot_flash_result_t boot_flash_wait_ready_unlocked(uint32_t timeout_us)
{
    uint64_t started = hal_time_us();
    uint32_t polls = 0U;

    while(boot_flash_read_status_unlocked() & BOOT_FLASH_STATUS_BUSY)
    {
        uint64_t now = hal_time_us();
        if(now - started >= timeout_us || ++polls >= BOOT_FLASH_POLL_FAILSAFE)
            return boot_flash_mark_fault(BOOT_FLASH_ERROR_TIMEOUT);
    }
    return BOOT_FLASH_OK;
}

static boot_flash_result_t boot_flash_validate_unlocked(uint32_t addr,
                                                        const void *data,
                                                        size_t len)
{
    if(!g_boot_flash_info.initialized)
        return BOOT_FLASH_ERROR_NOT_INITIALIZED;
    if(g_boot_flash_info.faulted)
        return BOOT_FLASH_ERROR_FAULTED;
    if(len != 0U && data == NULL)
        return BOOT_FLASH_ERROR_INVALID_ARGUMENT;
    if(addr > g_boot_flash_info.capacity ||
       len > (size_t)(g_boot_flash_info.capacity - addr))
        return BOOT_FLASH_ERROR_OUT_OF_BOUNDS;
    return BOOT_FLASH_OK;
}

static boot_flash_result_t boot_flash_write_enable_unlocked(void)
{
    uint8_t cmd = BOOT_FLASH_CMD_WRITE_ENABLE;
    boot_flash_cmd(&cmd, 1U);
    if((boot_flash_read_status_unlocked() & BOOT_FLASH_STATUS_WEL) == 0U)
        return boot_flash_mark_fault(BOOT_FLASH_ERROR_WRITE_ENABLE);
    return BOOT_FLASH_OK;
}

static boot_flash_result_t boot_flash_finish_write_unlocked(uint32_t timeout_us)
{
    boot_flash_result_t result = boot_flash_wait_ready_unlocked(timeout_us);

    if(result != BOOT_FLASH_OK)
        return result;
    /* WEL clears when a program/erase instruction has been accepted. */
    if(boot_flash_read_status_unlocked() & BOOT_FLASH_STATUS_WEL)
        return boot_flash_mark_fault(BOOT_FLASH_ERROR_WRITE_ENABLE);
    return BOOT_FLASH_OK;
}

static boot_flash_result_t boot_flash_page_program_unlocked(uint32_t addr,
                                                            const uint8_t *data,
                                                            size_t len)
{
    uint8_t cmd[4] = {
        BOOT_FLASH_CMD_PAGE_PROGRAM,
        (uint8_t)(addr >> 16),
        (uint8_t)(addr >> 8),
        (uint8_t)addr,
    };
    boot_flash_result_t result;

    result = boot_flash_write_enable_unlocked();
    if(result != BOOT_FLASH_OK)
        return result;
    hal_spi_standard_send_segments(FLASH_SPI, FLASH_SPI_CS,
                                   cmd, sizeof(cmd), data, len);
    return boot_flash_finish_write_unlocked(BOOT_FLASH_PROGRAM_TIMEOUT_US);
}

boot_flash_result_t boot_flash_init(uint32_t hz)
{
    uint8_t cmd = BOOT_FLASH_CMD_READ_ID;
    boot_flash_result_t result;
    uint8_t capacity_log2;

    if(!boot_flash_on_core0())
        return BOOT_FLASH_ERROR_WRONG_CORE;
    if(hz == 0U)
        return BOOT_FLASH_ERROR_INVALID_ARGUMENT;

    hk_board_ops.internal_flash_prepare();
    boot_flash_lock();
    memset(&g_boot_flash_info, 0, sizeof(g_boot_flash_info));
    hal_spi_standard_init(FLASH_SPI, hz);
    hal_spi_standard_receive(FLASH_SPI, FLASH_SPI_CS, &cmd, 1U,
                             g_boot_flash_info.jedec_id,
                             sizeof(g_boot_flash_info.jedec_id));

    capacity_log2 = g_boot_flash_info.jedec_id[2];
    if(g_boot_flash_info.jedec_id[0] == 0x00U ||
       g_boot_flash_info.jedec_id[0] == 0xFFU ||
       capacity_log2 >= 32U)
    {
        boot_flash_unlock();
        return BOOT_FLASH_ERROR_UNSUPPORTED_DEVICE;
    }

    g_boot_flash_info.capacity = 1UL << capacity_log2;
    if(g_boot_flash_info.capacity < HK_FLASH_MIN_CAPACITY ||
       g_boot_flash_info.capacity > HK_FLASH_MAX_CAPACITY)
    {
        g_boot_flash_info.capacity = 0U;
        boot_flash_unlock();
        return BOOT_FLASH_ERROR_UNSUPPORTED_DEVICE;
    }

    result = boot_flash_wait_ready_unlocked(BOOT_FLASH_INIT_TIMEOUT_US);
    if(result == BOOT_FLASH_OK)
        g_boot_flash_info.initialized = 1U;
    boot_flash_unlock();
    return result;
}

boot_flash_result_t boot_flash_read_id(uint8_t id[3])
{
    boot_flash_result_t result;

    if(!boot_flash_on_core0())
        return BOOT_FLASH_ERROR_WRONG_CORE;
    if(id == NULL)
        return BOOT_FLASH_ERROR_INVALID_ARGUMENT;
    boot_flash_lock();
    result = boot_flash_validate_unlocked(0U, id, 3U);
    if(result == BOOT_FLASH_OK)
        memcpy(id, g_boot_flash_info.jedec_id, 3U);
    boot_flash_unlock();
    return result;
}

boot_flash_result_t boot_flash_read(uint32_t addr, uint8_t *data, size_t len)
{
    boot_flash_result_t result;
    uint8_t cmd[4] = {
        BOOT_FLASH_CMD_READ,
        (uint8_t)(addr >> 16),
        (uint8_t)(addr >> 8),
        (uint8_t)addr,
    };

    if(!boot_flash_on_core0())
        return BOOT_FLASH_ERROR_WRONG_CORE;
    boot_flash_lock();
    result = boot_flash_validate_unlocked(addr, data, len);
    if(result == BOOT_FLASH_OK && len != 0U)
        hal_spi_standard_receive(FLASH_SPI, FLASH_SPI_CS, cmd, sizeof(cmd), data, len);
    boot_flash_unlock();
    return result;
}

boot_flash_result_t boot_flash_sector_erase(uint32_t addr)
{
    uint8_t cmd[4] = {
        BOOT_FLASH_CMD_SECTOR_ERASE,
        (uint8_t)(addr >> 16),
        (uint8_t)(addr >> 8),
        (uint8_t)addr,
    };
    boot_flash_result_t result;

    if(!boot_flash_on_core0())
        return BOOT_FLASH_ERROR_WRONG_CORE;
    if(addr % HK_FLASH_ERASE_SIZE)
        return BOOT_FLASH_ERROR_ALIGNMENT;
    boot_flash_lock();
    result = boot_flash_validate_unlocked(addr, cmd, HK_FLASH_ERASE_SIZE);
    if(result == BOOT_FLASH_OK)
        result = boot_flash_write_enable_unlocked();
    if(result == BOOT_FLASH_OK)
    {
        boot_flash_cmd(cmd, sizeof(cmd));
        result = boot_flash_finish_write_unlocked(BOOT_FLASH_ERASE_TIMEOUT_US);
    }
    boot_flash_unlock();
    return result;
}

boot_flash_result_t boot_flash_page_program(uint32_t addr, const uint8_t *data,
                                            size_t len)
{
    boot_flash_result_t result;
    size_t page_remaining = HK_FLASH_PROGRAM_SIZE -
                            (addr % HK_FLASH_PROGRAM_SIZE);

    if(!boot_flash_on_core0())
        return BOOT_FLASH_ERROR_WRONG_CORE;
    if(len > page_remaining)
        return BOOT_FLASH_ERROR_ALIGNMENT;
    boot_flash_lock();
    result = boot_flash_validate_unlocked(addr, data, len);
    if(result == BOOT_FLASH_OK && len != 0U)
        result = boot_flash_page_program_unlocked(addr, data, len);
    boot_flash_unlock();
    return result;
}

boot_flash_result_t boot_flash_program(uint32_t addr, const uint8_t *data,
                                      size_t len)
{
    boot_flash_result_t result;

    if(!boot_flash_on_core0())
        return BOOT_FLASH_ERROR_WRONG_CORE;
    boot_flash_lock();
    result = boot_flash_validate_unlocked(addr, data, len);
    while(result == BOOT_FLASH_OK && len != 0U)
    {
        size_t chunk = HK_FLASH_PROGRAM_SIZE - (addr % HK_FLASH_PROGRAM_SIZE);
        if(chunk > len)
            chunk = len;
        result = boot_flash_page_program_unlocked(addr, data, chunk);
        addr += (uint32_t)chunk;
        data += chunk;
        len -= chunk;
    }
    boot_flash_unlock();
    return result;
}

void boot_flash_get_info(boot_flash_info_t *info)
{
    if(info == NULL)
        return;
    boot_flash_lock();
    *info = g_boot_flash_info;
    boot_flash_unlock();
}

const char *boot_flash_result_name(boot_flash_result_t result)
{
    switch(result)
    {
    case BOOT_FLASH_OK:
        return "ok";
    case BOOT_FLASH_ERROR_INVALID_ARGUMENT:
        return "invalid-argument";
    case BOOT_FLASH_ERROR_NOT_INITIALIZED:
        return "not-initialized";
    case BOOT_FLASH_ERROR_WRONG_CORE:
        return "wrong-core";
    case BOOT_FLASH_ERROR_UNSUPPORTED_DEVICE:
        return "unsupported-device";
    case BOOT_FLASH_ERROR_OUT_OF_BOUNDS:
        return "out-of-bounds";
    case BOOT_FLASH_ERROR_ALIGNMENT:
        return "alignment";
    case BOOT_FLASH_ERROR_TIMEOUT:
        return "timeout";
    case BOOT_FLASH_ERROR_WRITE_ENABLE:
        return "write-enable";
    case BOOT_FLASH_ERROR_FAULTED:
        return "faulted";
    default:
        return "unknown";
    }
}

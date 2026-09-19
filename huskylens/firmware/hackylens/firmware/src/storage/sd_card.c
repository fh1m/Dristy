#include "internal/fat32_state_private.h"

#include <stddef.h>

#include <stdio.h>
#include <string.h>

#include "../config/sd_config.h"
#include "fat32_volume.h"
#include "sd_card.h"

#include "../drivers/sd_spi.h"
#include "hal_time.h"

static void sd_deselect(void)
{
    sd_spi_cs_high();
    sd_spi_xfer(0xFF);
}

static uint8_t sd_wait_ready(uint32_t timeout)
{
    while(timeout--)
    {
        if(sd_spi_xfer(0xFF) == 0xFF)
            return 1;
    }
    return 0;
}

static uint8_t sd_command(uint8_t cmd, uint32_t arg, uint8_t crc, uint8_t *extra, uint8_t extra_len, uint8_t release)
{
    uint8_t r1 = 0xFF;

    sd_spi_cs_low();
    sd_spi_xfer(0xFF);
    if(cmd != 0 && !sd_wait_ready(SD_READY_TIMEOUT))
    {
        if(release)
            sd_deselect();
        return 0xFF;
    }
    sd_spi_xfer((uint8_t)(0x40U | cmd));
    sd_spi_xfer((uint8_t)(arg >> 24));
    sd_spi_xfer((uint8_t)(arg >> 16));
    sd_spi_xfer((uint8_t)(arg >> 8));
    sd_spi_xfer((uint8_t)arg);
    sd_spi_xfer(crc);

    for(uint16_t i = 0; i < SD_CMD_TIMEOUT; i++)
    {
        r1 = sd_spi_xfer(0xFF);
        if((r1 & 0x80U) == 0)
            break;
    }

    if(extra && extra_len)
    {
        for(uint8_t i = 0; i < extra_len; i++)
            extra[i] = sd_spi_xfer(0xFF);
    }

    if(release)
        sd_deselect();
    return r1;
}

uint8_t sd_card_read_block(uint32_t lba, uint8_t *dst)
{
    uint32_t arg = fat32_card_is_sdhc() ? lba : lba * SD_BLOCK_SIZE;
    uint8_t r1 = sd_command(17, arg, 0x01, NULL, 0, 0);

    if(r1 != 0x00)
    {
        sd_deselect();
        return 0;
    }

    for(uint16_t i = 0; i < SD_READ_TIMEOUT; i++)
    {
        uint8_t token = sd_spi_xfer(0xFF);
        if(token == 0xFE)
        {
            for(uint16_t j = 0; j < SD_BLOCK_SIZE; j++)
                dst[j] = sd_spi_xfer(0xFF);
            sd_spi_xfer(0xFF);
            sd_spi_xfer(0xFF);
            sd_deselect();
            return 1;
        }
    }

    sd_deselect();
    return 0;
}

static uint8_t sd_write_block_checked(uint32_t lba, const uint8_t *src, uint8_t verify)
{
    uint32_t arg = fat32_card_is_sdhc() ? lba : lba * SD_BLOCK_SIZE;
    uint8_t r1 = sd_command(24, arg, 0x01, NULL, 0, 0);
    uint8_t response;

    if(r1 != 0x00)
    {
        sd_deselect();
        printf("[SD] CMD24 command failed lba=%u r1=0x%02X\r\n", lba, r1);
        return 0;
    }

    sd_spi_xfer(0xFF);
    sd_spi_xfer(0xFE);
    for(uint16_t i = 0; i < SD_BLOCK_SIZE; i++)
        sd_spi_xfer(src[i]);
    sd_spi_xfer(0xFF);
    sd_spi_xfer(0xFF);

    response = 0xFF;
    for(uint8_t i = 0; i < 64; i++)
    {
        response = sd_spi_xfer(0xFF);
        if(response != 0xFF)
            break;
    }
    if((response & 0x1FU) != 0x05)
    {
        sd_deselect();
        printf("[SD] CMD24 data reject lba=%u resp=0x%02X\r\n", lba, response);
        return 0;
    }

    for(uint32_t i = 0; i < SD_WRITE_TIMEOUT; i++)
    {
        if(sd_spi_xfer(0xFF) == 0xFF)
        {
            sd_deselect();
            if(!verify)
                return 1;
            uint8_t *verify_sector = fat32_verify_scratch();
            if(!sd_card_read_block(lba, verify_sector))
            {
                printf("[SD] CMD24 verify read failed lba=%u\r\n", lba);
                return 0;
            }
            if(memcmp(src, verify_sector, SD_BLOCK_SIZE) != 0)
            {
                printf("[SD] CMD24 verify mismatch lba=%u\r\n", lba);
                return 0;
            }
            return 1;
        }
    }

    sd_deselect();
    printf("[SD] CMD24 busy timeout lba=%u\r\n", lba);
    return 0;
}

uint8_t sd_card_write_block(uint32_t lba, const uint8_t *src)
{
    return sd_write_block_checked(lba, src, 1);
}

uint8_t sd_card_write_block_fast(uint32_t lba, const uint8_t *src)
{
    return sd_write_block_checked(lba, src, 0);
}

uint8_t sd_card_init(void)
{
    uint8_t r1;
    uint8_t r7[4] = {0};
    uint8_t ocr[4] = {0};
    uint8_t v2_card = 0;

    fat32_card_reset_state();
    sd_spi_board_init(SD_INIT_CLK_HZ);

    for(uint8_t i = 0; i < 12; i++)
        sd_spi_xfer(0xFF);

    r1 = sd_command(0, 0, 0x95, NULL, 0, 1);
    if(r1 != 0x01)
    {
        if(!fat32_probe_silent())
            printf("[SD] CMD0 failed r1=0x%02X\r\n", r1);
        return 0;
    }

    r1 = sd_command(8, 0x000001AAUL, 0x87, r7, sizeof(r7), 1);
    if(r1 == 0x01 && r7[2] == 0x01 && r7[3] == 0xAA)
        v2_card = 1;

    for(uint16_t i = 0; i < 1000; i++)
    {
        (void)sd_command(55, 0, 0x01, NULL, 0, 1);
        r1 = sd_command(41, v2_card ? 0x40000000UL : 0, 0x01, NULL, 0, 1);
        if(r1 == 0x00)
            break;
        hal_sleep_ms(1);
    }
    if(r1 != 0x00)
    {
        if(!fat32_probe_silent())
            printf("[SD] ACMD41 failed r1=0x%02X\r\n", r1);
        return 0;
    }

    r1 = sd_command(58, 0, 0x01, ocr, sizeof(ocr), 1);
    if(r1 == 0x00 && (ocr[0] & 0x40U))
        fat32_card_set_sdhc(1);
    if(!fat32_card_is_sdhc())
        (void)sd_command(16, SD_BLOCK_SIZE, 0x01, NULL, 0, 1);

    sd_spi_config(SD_RUN_CLK_HZ);
    fat32_card_set_present(1);
    printf("[SD] card init ok type=%s\r\n", fat32_card_is_sdhc() ? "SDHC" : "SDSC");
    return 1;
}

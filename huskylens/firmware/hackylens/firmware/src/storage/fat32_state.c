#include "internal/fat32_state_private.h"

#include "../config/sd_config.h"
#include "fat32_volume.h"

#include "../core/hk_binary.h"
#include "sd_card.h"

static uint8_t g_sd_present;
static uint8_t g_sd_sdhc;
static uint8_t g_sd_probe_silent;
static uint8_t g_fat_mounted;
static uint32_t g_fat_lba_begin;
static uint32_t g_fat_fat_begin;
static uint32_t g_fat_data_begin;
static uint32_t g_fat_root_cluster;
static uint32_t g_fat_sectors_per_fat;
static uint32_t g_fat_total_sectors;
static uint32_t g_fat_cluster_count;
static uint32_t g_fat_alloc_hint = 2;
static uint8_t g_fat_sectors_per_cluster;
static uint8_t g_fat_count;
static uint8_t g_sd_sector[SD_BLOCK_SIZE] __attribute__((aligned(4)));
static uint8_t g_sd_verify_sector[SD_BLOCK_SIZE] __attribute__((aligned(4)));

uint8_t hk_sd_present(void)
{
    return g_sd_present;
}

uint8_t hk_fat_mounted(void)
{
    return g_fat_mounted;
}

uint32_t hk_fat_root_cluster(void)
{
    return g_fat_root_cluster;
}

uint8_t fat32_sectors_per_cluster(void)
{
    return g_fat_sectors_per_cluster;
}

uint32_t fat32_cluster_size(void)
{
    return (uint32_t)g_fat_sectors_per_cluster * SD_BLOCK_SIZE;
}

uint8_t *fat32_sector_scratch(void)
{
    return g_sd_sector;
}

uint8_t fat32_card_is_sdhc(void)
{
    return g_sd_sdhc;
}

void fat32_card_reset_state(void)
{
    g_sd_present = 0;
    g_sd_sdhc = 0;
    g_fat_mounted = 0;
}

void fat32_card_set_sdhc(uint8_t sdhc)
{
    g_sd_sdhc = sdhc ? 1 : 0;
}

void fat32_card_set_present(uint8_t present)
{
    g_sd_present = present ? 1 : 0;
}

uint8_t *fat32_verify_scratch(void)
{
    return g_sd_verify_sector;
}

void fat32_mark_removed(void)
{
    fat32_card_reset_state();
}

void fat32_set_probe_silent(uint8_t silent)
{
    g_sd_probe_silent = silent ? 1 : 0;
}

uint8_t fat32_probe_silent(void)
{
    return g_sd_probe_silent;
}

const fat32_geometry_t *fat32_geometry(void)
{
    static fat32_geometry_t geometry;

    geometry.fat_begin = g_fat_fat_begin;
    geometry.data_begin = g_fat_data_begin;
    geometry.sectors_per_fat = g_fat_sectors_per_fat;
    geometry.cluster_count = g_fat_cluster_count;
    geometry.alloc_hint = g_fat_alloc_hint;
    geometry.sectors_per_cluster = g_fat_sectors_per_cluster;
    geometry.fat_count = g_fat_count;
    return &geometry;
}

void fat32_set_mounted(const fat32_mount_info_t *mount)
{
    g_fat_lba_begin = mount->lba_begin;
    g_fat_fat_begin = mount->fat_begin;
    g_fat_data_begin = mount->data_begin;
    g_fat_root_cluster = mount->root_cluster;
    g_fat_sectors_per_fat = mount->sectors_per_fat;
    g_fat_total_sectors = mount->total_sectors;
    g_fat_cluster_count = mount->cluster_count;
    g_fat_sectors_per_cluster = mount->sectors_per_cluster;
    g_fat_count = mount->fat_count;
    g_fat_alloc_hint = 2;
    g_fat_mounted = 1;
}

void fat32_set_alloc_hint(uint32_t cluster)
{
    g_fat_alloc_hint = cluster;
}

uint8_t fat32_health_check(void)
{
    if(!g_sd_present || !g_fat_mounted)
        return 0;
    if(!sd_card_read_block(g_fat_lba_begin, g_sd_sector))
        return 0;
    if(g_sd_sector[510] != 0x55 || g_sd_sector[511] != 0xAA)
        return 0;
    return rd16(&g_sd_sector[11]) == SD_BLOCK_SIZE;
}

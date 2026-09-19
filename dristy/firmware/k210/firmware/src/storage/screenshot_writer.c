#include "screenshot_writer.h"

#include <stddef.h>

#include <stdio.h>
#include <string.h>

#include "../config/display_config.h"
#include "../config/sd_config.h"
#include "../config/fat32_config.h"
#include "file_mount.h"
#include "screenshot_bmp.h"
#include "fat32_allocation.h"
#include "fat32_volume.h"
#include "internal/fat32_state_private.h"
#include "screenshot_path.h"
#include "sd_card.h"

static const char *g_screenshot_error = "SD WRITE";

static uint8_t screenshot_write_file_chain(const screenshot_pixel_source_t *source, uint32_t first_cluster, uint32_t file_size)
{
    uint32_t cluster = first_cluster;
    uint32_t remaining = file_size;
    uint32_t offset = 0;
    uint8_t sectors_per_cluster = fat32_sectors_per_cluster();
    uint8_t *sector = fat32_sector_scratch();

    while(remaining && cluster >= 2 && cluster < FAT32_EOC)
    {
        uint32_t base = fat_cluster_lba(cluster);
        for(uint8_t s = 0; s < sectors_per_cluster && remaining; s++)
        {
            uint16_t take = remaining > SD_BLOCK_SIZE ? SD_BLOCK_SIZE : (uint16_t)remaining;
            memset(sector, 0, SD_BLOCK_SIZE);
            screenshot_bmp_fill_bytes(source, offset, sector, take);
            if(!sd_card_write_block_fast(base + s, sector))
                return 0;
            offset += take;
            remaining -= take;
        }
        if(remaining)
            cluster = fat_next_cluster(cluster);
    }

    return remaining == 0;
}

uint8_t screenshot_save_current_screen(const screenshot_pixel_source_t *source, char *saved_name, size_t saved_name_size)
{
    uint8_t short_name[11];
    uint32_t first_cluster;
    uint32_t cluster_size;
    uint32_t cluster_count;
    uint32_t file_size = screenshot_bmp_file_size();

    if(!file_mount_if_needed())
    {
        g_screenshot_error = "MOUNT";
        return 0;
    }
    if(!screenshot_path_prepare(short_name, saved_name, saved_name_size, &g_screenshot_error))
    {
        if(g_screenshot_error == NULL)
            g_screenshot_error = "NAME";
        return 0;
    }

    cluster_size = fat32_cluster_size();
    if(cluster_size == 0)
    {
        g_screenshot_error = "CLUSTER";
        return 0;
    }
    cluster_count = (file_size + cluster_size - 1U) / cluster_size;
    if(!fat_alloc_chain(cluster_count, &first_cluster))
    {
        g_screenshot_error = "ALLOC";
        return 0;
    }
    if(!screenshot_write_file_chain(source, first_cluster, file_size))
    {
        g_screenshot_error = "WRITE";
        return 0;
    }
    if(!screenshot_path_add_file_entry(short_name, first_cluster, file_size, &g_screenshot_error))
    {
        if(g_screenshot_error == NULL)
            g_screenshot_error = "DIRENT";
        return 0;
    }

    g_screenshot_error = NULL;
    printf("[SHOT] saved %s frame=%ux%u size=%u cluster=%u\r\n", saved_name, HK_DISPLAY_REQUIRED_WIDTH, HK_DISPLAY_REQUIRED_HEIGHT, (unsigned)file_size, first_cluster);
    return 1;
}

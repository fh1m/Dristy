#include "internal/fat32_state_private.h"

#include <string.h>

#include "../config/sd_config.h"
#include "../config/fat32_config.h"
#include "fat32_allocation.h"

#include "../core/hk_binary.h"
#include "sd_card.h"

uint32_t fat_cluster_lba(uint32_t cluster)
{
    const fat32_geometry_t *geometry = fat32_geometry();
    return geometry->data_begin + (cluster - 2U) * geometry->sectors_per_cluster;
}

uint32_t fat_next_cluster(uint32_t cluster)
{
    const fat32_geometry_t *geometry = fat32_geometry();
    uint8_t *sector_data = fat32_sector_scratch();
    uint32_t fat_offset = cluster * 4U;
    uint32_t sector = geometry->fat_begin + fat_offset / SD_BLOCK_SIZE;
    uint16_t offset = (uint16_t)(fat_offset % SD_BLOCK_SIZE);

    if(!sd_card_read_block(sector, sector_data))
        return FAT32_EOC;
    return rd32(&sector_data[offset]) & 0x0FFFFFFFUL;
}

uint8_t fat_write_entry(uint32_t cluster, uint32_t value)
{
    const fat32_geometry_t *geometry = fat32_geometry();
    uint8_t *sector_data = fat32_sector_scratch();
    uint32_t fat_offset = cluster * 4U;
    uint32_t sector_offset = fat_offset / SD_BLOCK_SIZE;
    uint16_t offset = (uint16_t)(fat_offset % SD_BLOCK_SIZE);

    if(cluster < 2 || cluster >= geometry->cluster_count + 2U)
        return 0;

    for(uint8_t copy = 0; copy < geometry->fat_count; copy++)
    {
        uint32_t sector = geometry->fat_begin + (uint32_t)copy * geometry->sectors_per_fat + sector_offset;
        uint32_t old_value;
        if(!sd_card_read_block(sector, sector_data))
            return 0;
        old_value = rd32(&sector_data[offset]);
        wr32(&sector_data[offset], (old_value & 0xF0000000UL) | (value & 0x0FFFFFFFUL));
        if(!sd_card_write_block(sector, sector_data))
            return 0;
    }

    return 1;
}

uint8_t fat_find_free_cluster(uint32_t *cluster_out)
{
    const fat32_geometry_t *geometry = fat32_geometry();
    uint32_t start = geometry->alloc_hint >= 2 ? geometry->alloc_hint : 2;
    uint32_t max_cluster = geometry->cluster_count + 1U;

    for(uint8_t pass = 0; pass < 2; pass++)
    {
        uint32_t first = pass == 0 ? start : 2;
        uint32_t last = pass == 0 ? max_cluster : start - 1U;
        for(uint32_t cluster = first; cluster <= last; cluster++)
        {
            if(fat_next_cluster(cluster) == 0)
            {
                *cluster_out = cluster;
                fat32_set_alloc_hint(cluster + 1U);
                return 1;
            }
        }
    }

    return 0;
}

uint8_t fat_alloc_chain(uint32_t count, uint32_t *first_out)
{
    uint32_t first = 0;
    uint32_t previous = 0;

    if(count == 0)
        return 0;

    for(uint32_t i = 0; i < count; i++)
    {
        uint32_t cluster;
        if(!fat_find_free_cluster(&cluster))
            return 0;
        if(!fat_write_entry(cluster, FAT32_EOC))
            return 0;
        if(previous && !fat_write_entry(previous, cluster))
            return 0;
        if(first == 0)
            first = cluster;
        previous = cluster;
    }

    *first_out = first;
    return 1;
}

uint8_t fat_free_chain(uint32_t first_cluster)
{
    uint32_t cluster = first_cluster;

    while(cluster >= 2 && cluster < FAT32_EOC)
    {
        uint32_t next = fat_next_cluster(cluster);
        if(!fat_write_entry(cluster, 0))
            return 0;
        if(next >= FAT32_EOC || next < 2)
            break;
        cluster = next;
    }

    if(first_cluster >= 2 && first_cluster < fat32_geometry()->alloc_hint)
        fat32_set_alloc_hint(first_cluster);
    return 1;
}

uint8_t fat_zero_cluster(uint32_t cluster)
{
    const fat32_geometry_t *geometry = fat32_geometry();
    uint8_t *sector_data = fat32_sector_scratch();

    memset(sector_data, 0, SD_BLOCK_SIZE);
    for(uint8_t s = 0; s < geometry->sectors_per_cluster; s++)
    {
        if(!sd_card_write_block(fat_cluster_lba(cluster) + s, sector_data))
            return 0;
    }
    return 1;
}

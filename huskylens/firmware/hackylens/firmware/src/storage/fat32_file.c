#include "internal/fat32_state_private.h"

#include "fat32_types.h"
#include <string.h>

#include "../config/sd_config.h"
#include "../config/fat32_config.h"
#include "fat32_allocation.h"
#include "fat32_file.h"
#include "fat32_stream.h"

#include "sd_card.h"

uint8_t fat_file_read_at(const fat_file_entry_t *entry, uint32_t offset, uint8_t *dst, uint32_t len)
{
    const fat32_geometry_t *geometry = fat32_geometry();
    uint8_t *sector_data = fat32_sector_scratch();
    uint32_t cluster = entry->cluster;
    uint32_t cluster_size = (uint32_t)geometry->sectors_per_cluster * SD_BLOCK_SIZE;
    uint32_t skip_clusters;

    if(offset >= entry->size)
        return len == 0;
    if(offset + len > entry->size)
        len = entry->size - offset;

    skip_clusters = offset / cluster_size;
    offset %= cluster_size;
    for(uint32_t i = 0; i < skip_clusters && cluster >= 2 && cluster < FAT32_EOC; i++)
        cluster = fat_next_cluster(cluster);

    while(len && cluster >= 2 && cluster < FAT32_EOC)
    {
        uint32_t base = fat_cluster_lba(cluster);
        uint8_t sector_index = (uint8_t)(offset / SD_BLOCK_SIZE);
        uint16_t sector_offset = (uint16_t)(offset % SD_BLOCK_SIZE);

        for(uint8_t s = sector_index; s < geometry->sectors_per_cluster && len; s++)
        {
            uint16_t take;
            if(!sd_card_read_block(base + s, sector_data))
                return 0;
            take = (uint16_t)(SD_BLOCK_SIZE - sector_offset);
            if(take > len)
                take = (uint16_t)len;
            memcpy(dst, &sector_data[sector_offset], take);
            dst += take;
            len -= take;
            sector_offset = 0;
        }

        offset = 0;
        if(len)
            cluster = fat_next_cluster(cluster);
    }

    return len == 0;
}

uint8_t fat_stream_open(fat_stream_t *stream, const fat_file_entry_t *entry, uint32_t offset)
{
    const fat32_geometry_t *geometry = fat32_geometry();
    uint32_t cluster = entry->cluster;
    uint32_t cluster_size = (uint32_t)geometry->sectors_per_cluster * SD_BLOCK_SIZE;
    uint32_t skip_clusters;

    if(cluster_size == 0 || offset > entry->size)
        return 0;

    skip_clusters = offset / cluster_size;
    for(uint32_t i = 0; i < skip_clusters && cluster >= 2 && cluster < FAT32_EOC; i++)
        cluster = fat_next_cluster(cluster);

    if(cluster < 2 || cluster >= FAT32_EOC)
        return offset == entry->size;

    stream->entry = entry;
    stream->cluster = cluster;
    stream->file_offset = offset;
    stream->cluster_offset = offset % cluster_size;
    stream->cache_lba = 0xFFFFFFFFUL;
    stream->cache_valid = 0;
    return 1;
}

uint8_t fat_stream_read(fat_stream_t *stream, uint8_t *dst, uint32_t len)
{
    const fat32_geometry_t *geometry = fat32_geometry();
    uint8_t *sector_data = fat32_sector_scratch();
    uint32_t cluster_size = (uint32_t)geometry->sectors_per_cluster * SD_BLOCK_SIZE;

    while(len)
    {
        uint32_t sector_index;
        uint32_t lba;
        uint16_t sector_offset;
        uint16_t take;

        if(stream->file_offset >= stream->entry->size ||
           stream->cluster < 2 || stream->cluster >= FAT32_EOC)
            return 0;

        sector_index = stream->cluster_offset / SD_BLOCK_SIZE;
        sector_offset = (uint16_t)(stream->cluster_offset % SD_BLOCK_SIZE);
        lba = fat_cluster_lba(stream->cluster) + sector_index;

        if(!stream->cache_valid || stream->cache_lba != lba)
        {
            if(!sd_card_read_block(lba, sector_data))
                return 0;
            stream->cache_lba = lba;
            stream->cache_valid = 1;
        }

        take = (uint16_t)(SD_BLOCK_SIZE - sector_offset);
        if(take > len)
            take = (uint16_t)len;
        if(take > stream->entry->size - stream->file_offset)
            take = (uint16_t)(stream->entry->size - stream->file_offset);

        memcpy(dst, &sector_data[sector_offset], take);
        dst += take;
        len -= take;
        stream->file_offset += take;
        stream->cluster_offset += take;

        if(stream->cluster_offset >= cluster_size)
        {
            stream->cluster = fat_next_cluster(stream->cluster);
            stream->cluster_offset = 0;
            stream->cache_valid = 0;
        }
    }

    return 1;
}

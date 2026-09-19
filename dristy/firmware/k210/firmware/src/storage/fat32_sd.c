#include "internal/fat32_state_private.h"

#include <stdio.h>

#include "../config/sd_config.h"
#include "fat32_volume.h"

#include "../core/hk_binary.h"
#include "sd_card.h"

uint8_t fat32_mount(void)
{
    uint32_t bpb_lba = 0;
    uint8_t type = 0;
    uint16_t bytes_per_sector;
    uint16_t reserved;
    uint8_t fats;
    uint32_t total_sectors;
    uint32_t data_sectors;
    uint8_t sectors_per_cluster;
    uint32_t sectors_per_fat;
    uint32_t root_cluster;
    uint32_t fat_begin;
    uint32_t data_begin;
    uint8_t *sector_data = fat32_sector_scratch();

    if(!sd_card_read_block(0, sector_data))
        return 0;

    if(sector_data[510] != 0x55 || sector_data[511] != 0xAA)
        return 0;

    type = sector_data[450];
    if(type == 0x0B || type == 0x0C)
        bpb_lba = rd32(&sector_data[454]);

    if(bpb_lba && !sd_card_read_block(bpb_lba, sector_data))
        return 0;

    bytes_per_sector = rd16(&sector_data[11]);
    reserved = rd16(&sector_data[14]);
    fats = sector_data[16];
    sectors_per_cluster = sector_data[13];
    sectors_per_fat = rd32(&sector_data[36]);
    root_cluster = rd32(&sector_data[44]);
    total_sectors = rd16(&sector_data[19]);
    if(total_sectors == 0)
        total_sectors = rd32(&sector_data[32]);

    if(bytes_per_sector != SD_BLOCK_SIZE || sectors_per_cluster == 0 ||
       sectors_per_fat == 0 || fats == 0 || root_cluster < 2 ||
       total_sectors == 0)
        return 0;

    fat_begin = bpb_lba + reserved;
    data_begin = fat_begin + (uint32_t)fats * sectors_per_fat;
    data_sectors = total_sectors - (data_begin - bpb_lba);

    fat32_mount_info_t mount = {
        .lba_begin = bpb_lba,
        .fat_begin = fat_begin,
        .data_begin = data_begin,
        .root_cluster = root_cluster,
        .sectors_per_fat = sectors_per_fat,
        .total_sectors = total_sectors,
        .cluster_count = data_sectors / sectors_per_cluster,
        .sectors_per_cluster = sectors_per_cluster,
        .fat_count = fats,
    };
    fat32_set_mounted(&mount);
    printf("[SD] FAT32 mounted lba=%u spc=%u root=%u clusters=%u\r\n", bpb_lba, sectors_per_cluster, root_cluster, mount.cluster_count);
    return 1;
}

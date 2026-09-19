#ifndef HK_FAT32_VOLUME_H
#define HK_FAT32_VOLUME_H

#include <stdint.h>

uint8_t fat32_mount(void);
uint8_t hk_sd_present(void);
uint8_t hk_fat_mounted(void);
uint32_t hk_fat_root_cluster(void);
uint8_t fat32_sectors_per_cluster(void);
uint32_t fat32_cluster_size(void);
void fat32_mark_removed(void);
void fat32_set_probe_silent(uint8_t silent);
uint8_t fat32_probe_silent(void);
uint8_t fat32_health_check(void);

#endif

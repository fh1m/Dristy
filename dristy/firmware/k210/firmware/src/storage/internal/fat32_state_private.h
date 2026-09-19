#ifndef FAT32_STATE_PRIVATE_H
#define FAT32_STATE_PRIVATE_H

#include <stdint.h>


typedef struct {
    uint32_t fat_begin;
    uint32_t data_begin;
    uint32_t sectors_per_fat;
    uint32_t cluster_count;
    uint32_t alloc_hint;
    uint8_t sectors_per_cluster;
    uint8_t fat_count;
} fat32_geometry_t;

typedef struct {
    uint32_t lba_begin;
    uint32_t fat_begin;
    uint32_t data_begin;
    uint32_t root_cluster;
    uint32_t sectors_per_fat;
    uint32_t total_sectors;
    uint32_t cluster_count;
    uint8_t sectors_per_cluster;
    uint8_t fat_count;
} fat32_mount_info_t;

const fat32_geometry_t *fat32_geometry(void);
void fat32_set_mounted(const fat32_mount_info_t *mount);
void fat32_set_alloc_hint(uint32_t cluster);
uint8_t fat32_card_is_sdhc(void);
void fat32_card_reset_state(void);
void fat32_card_set_sdhc(uint8_t sdhc);
void fat32_card_set_present(uint8_t present);
uint8_t *fat32_sector_scratch(void);
uint8_t *fat32_verify_scratch(void);

#endif

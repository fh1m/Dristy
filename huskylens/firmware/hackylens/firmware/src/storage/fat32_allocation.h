#ifndef HK_FAT32_ALLOCATION_H
#define HK_FAT32_ALLOCATION_H

#include <stdint.h>

uint32_t fat_cluster_lba(uint32_t cluster);
uint32_t fat_next_cluster(uint32_t cluster);
uint8_t fat_write_entry(uint32_t cluster, uint32_t value);
uint8_t fat_find_free_cluster(uint32_t *cluster_out);
uint8_t fat_alloc_chain(uint32_t count, uint32_t *first_out);
uint8_t fat_free_chain(uint32_t first_cluster);
uint8_t fat_zero_cluster(uint32_t cluster);

#endif

#ifndef HK_FAT32_DIRECTORY_H
#define HK_FAT32_DIRECTORY_H

#include <stdint.h>

#include "fat32_types.h"

uint8_t fat_dir_find_slots(uint32_t dir_cluster, uint8_t needed, fat_dir_slot_t *slot);
uint8_t fat_dir_write_slots(const fat_dir_slot_t *slot, const uint8_t *entries, uint8_t count);
uint8_t fat_dir_mark_deleted(uint32_t dir_cluster, uint32_t entry_ordinal, uint8_t lfn_count);
void fat_make_short_name(uint8_t out[11], const char *base, const char *ext);
uint8_t fat_lfn_checksum(const uint8_t short_name[11]);
void fat_make_lfn_entry(uint8_t *entry, const char *name, uint8_t seq, uint8_t last, uint8_t checksum);
void fat_make_short_entry(uint8_t *entry, const uint8_t short_name[11], uint8_t attr, uint32_t cluster, uint32_t size);
uint8_t fat_init_directory_cluster(uint32_t cluster, uint32_t parent_cluster);

#endif

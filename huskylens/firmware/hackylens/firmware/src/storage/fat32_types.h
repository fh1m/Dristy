#ifndef HK_FAT32_TYPES_H
#define HK_FAT32_TYPES_H

#include <stdint.h>

#include "fat_file_entry.h"

typedef struct
{
    const fat_file_entry_t *entry;
    uint32_t cluster;
    uint32_t file_offset;
    uint32_t cluster_offset;
    uint32_t cache_lba;
    uint8_t cache_valid;
} fat_stream_t;

typedef struct
{
    uint32_t cluster;
    uint8_t sector;
    uint8_t entry;
} fat_dir_slot_t;

#endif

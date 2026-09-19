#ifndef HK_STORAGE_FAT_FILE_ENTRY_H
#define HK_STORAGE_FAT_FILE_ENTRY_H

#include <stdint.h>

#include "../core/file_name.h"

typedef struct
{
    char name[FILE_NAME_MAX];
    uint8_t attr;
    uint32_t cluster;
    uint32_t size;
    uint32_t modified;
    uint32_t dir_ordinal;
    uint8_t lfn_count;
} fat_file_entry_t;

#endif

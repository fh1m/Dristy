#ifndef HK_FAT32_FILE_H
#define HK_FAT32_FILE_H

#include <stdint.h>

#include "fat_file_entry.h"

uint8_t fat_file_read_at(const fat_file_entry_t *entry, uint32_t offset, uint8_t *dst, uint32_t len);

#endif

#ifndef HK_FAT32_STREAM_H
#define HK_FAT32_STREAM_H

#include <stdint.h>

#include "fat32_types.h"

uint8_t fat_stream_open(fat_stream_t *stream, const fat_file_entry_t *entry, uint32_t offset);
uint8_t fat_stream_read(fat_stream_t *stream, uint8_t *dst, uint32_t len);

#endif

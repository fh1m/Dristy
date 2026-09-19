#ifndef FILE_PREVIEW_H
#define FILE_PREVIEW_H

#include <stdint.h>

#include "../../storage/fat_file_entry.h"

uint8_t files_read_preview(const fat_file_entry_t *entry);
const char *files_preview_text(void);
uint16_t files_preview_len(void);

#endif

#ifndef FILE_BROWSER_STATE_H
#define FILE_BROWSER_STATE_H

#include "file_browser_list.h"
#include "file_browser_navigation.h"
#include "file_preview.h"

uint8_t files_append_entry(const char *name, uint8_t attr, uint32_t cluster, uint32_t size,
                           uint32_t modified, uint32_t dir_ordinal, uint8_t lfn_count);
void files_sort_newest_first(void);

char *files_preview_buffer(void);
void files_set_preview_len(uint16_t len);

#endif

#ifndef HK_STORAGE_FILE_BROWSER_LIST_H
#define HK_STORAGE_FILE_BROWSER_LIST_H

#include <stdint.h>

#include "files_types.h"
#include "../../storage/fat_file_entry.h"

const file_list_item_t *files_list_items(void);
const fat_file_entry_t *files_entry_at(uint8_t index);
const fat_file_entry_t *files_selected_entry(void);
uint8_t files_count(void);
uint8_t files_index(void);
uint8_t files_top(void);
void files_set_index(uint8_t index);
void files_reset_list(void);
void files_ensure_visible(void);

#endif

#ifndef HK_STORAGE_FILE_BROWSER_NAVIGATION_H
#define HK_STORAGE_FILE_BROWSER_NAVIGATION_H

#include <stdint.h>

uint32_t files_current_cluster(void);
void files_set_current_cluster(uint32_t cluster);
void files_reset_depth(void);
uint8_t files_push_current_cluster(void);
uint8_t files_pop_cluster(uint32_t *cluster);
uint8_t files_depth(void);

#endif

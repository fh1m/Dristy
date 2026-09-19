#ifndef FILE_BROWSER_MODE_H
#define FILE_BROWSER_MODE_H

#include <stdint.h>

typedef enum
{
    FILES_MODE_LIST,
    FILES_MODE_TEXT,
    FILES_MODE_IMAGE,
    FILES_MODE_DELETE_CONFIRM
} file_browser_mode_t;

file_browser_mode_t files_mode(void);
void files_set_mode(file_browser_mode_t mode);
uint8_t files_preview_page(void);
void files_set_preview_page(uint8_t page);

#endif

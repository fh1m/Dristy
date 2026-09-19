#ifndef HK_FILE_PATH_H
#define HK_FILE_PATH_H

#include <stdint.h>

#include "fat_file_entry.h"

typedef enum
{
    FILE_PATH_OK = 0,
    FILE_PATH_NOT_FOUND,
    FILE_PATH_NOT_DIRECTORY,
    FILE_PATH_INVALID,
    FILE_PATH_IO,
} file_path_result_t;

file_path_result_t file_path_find(const char *path, fat_file_entry_t *entry);

#endif

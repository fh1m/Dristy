#ifndef FILE_DIR_SCAN_H
#define FILE_DIR_SCAN_H

#include <stdint.h>

#include "fat_file_entry.h"

typedef enum {
    FILE_DIR_SCAN_ERROR = 0,
    FILE_DIR_SCAN_FOUND,
    FILE_DIR_SCAN_NOT_FOUND
} file_dir_scan_result_t;

typedef uint8_t (*file_dir_index_parser_t)(const char *name, uint32_t *index_out);

file_dir_scan_result_t file_dir_find_directory(uint32_t parent_cluster, const char *long_name, const char *short_name, uint32_t *cluster_out);
uint8_t file_dir_find_max_file_index(uint32_t dir_cluster, file_dir_index_parser_t parser, uint32_t *max_index_out);

#endif

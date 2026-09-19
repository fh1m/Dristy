#ifndef SCREENSHOT_PATH_H
#define SCREENSHOT_PATH_H

#include <stddef.h>
#include <stdint.h>

uint8_t screenshot_path_prepare(uint8_t short_name[11], char *display, size_t display_size, const char **error);
uint8_t screenshot_path_add_file_entry(const uint8_t short_name[11], uint32_t first_cluster, uint32_t file_size, const char **error);

#endif

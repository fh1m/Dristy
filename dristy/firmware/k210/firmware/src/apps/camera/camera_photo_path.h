#ifndef PHOTO_PATH_H
#define PHOTO_PATH_H

#include "../../core/photo_types.h"

#include "camera_photo_format.h"
#include <stddef.h>
#include <stdint.h>

uint8_t photo_path_prepare(uint8_t short_name[11], char *display, size_t display_size, photo_format_t format);
uint8_t photo_path_add_file_entry(const uint8_t short_name[11], uint32_t first_cluster, uint32_t file_size);

#endif

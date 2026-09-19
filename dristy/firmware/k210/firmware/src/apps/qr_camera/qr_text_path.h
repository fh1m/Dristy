#ifndef QR_TEXT_PATH_H
#define QR_TEXT_PATH_H

#include <stddef.h>
#include <stdint.h>

uint8_t qr_text_path_prepare(uint8_t short_name[11], char *display, size_t display_size);
uint8_t qr_text_path_add_file_entry(const uint8_t short_name[11], uint32_t first_cluster, uint32_t file_size);

#endif

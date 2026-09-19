#ifndef CAMERA_DEBUG_H
#define CAMERA_DEBUG_H

#include <stddef.h>
#include <stdint.h>

void camera_debug_format_camera_info(char *line, size_t line_size, const char *screen);
void camera_debug_format_probe(char *line, size_t line_size, const char *screen);
void camera_debug_format_dvp(char *line, size_t line_size, const char *screen);
void camera_debug_select_reg_bank(uint8_t bank);
uint8_t camera_debug_read_reg(uint8_t bank, uint8_t reg);

#endif

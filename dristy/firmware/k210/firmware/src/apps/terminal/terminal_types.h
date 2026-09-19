#ifndef HK_TERMINAL_TYPES_H
#define HK_TERMINAL_TYPES_H

#include <stdint.h>

typedef enum
{
    TERMINAL_FONT_NORMAL = 0,
    TERMINAL_FONT_SMALL
} terminal_font_size_t;

typedef struct
{
    uint16_t columns;
    uint16_t rows;
    uint16_t cell_width;
    uint16_t cell_height;
} terminal_geometry_t;

typedef struct
{
    uint32_t total_rows;
    uint32_t scroll_offset;
    uint32_t max_scroll_offset;
    uint16_t visible_rows;
    uint8_t auto_follow;
} terminal_buffer_status_t;

#endif

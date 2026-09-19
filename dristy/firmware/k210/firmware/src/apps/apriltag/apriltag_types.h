#ifndef HK_APRILTAG_TYPES_H
#define HK_APRILTAG_TYPES_H

#include <stdint.h>

#define APRILTAG_RESULT_MAX 8U

typedef enum
{
    APRILTAG_OUTPUT_ALL = 0,
    APRILTAG_OUTPUT_SELECTED,
} apriltag_output_mode_t;

typedef struct
{
    uint8_t refine_edges;
    apriltag_output_mode_t output_mode;
    uint8_t fps_enabled;
    uint8_t light_mode;
    uint8_t rgb_red;
    uint8_t rgb_green;
    uint8_t rgb_blue;
} apriltag_preferences_t;

typedef struct
{
    uint16_t id;
    uint8_t hamming;
    uint16_t confidence;
    int16_t x;
    int16_t y;
    int16_t w;
    int16_t h;
    int16_t center_x;
    int16_t center_y;
    int16_t corners[4][2];
} apriltag_result_t;

#endif

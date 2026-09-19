#ifndef HK_OBJECT_DETECT_TYPES_H
#define HK_OBJECT_DETECT_TYPES_H

#include <stdint.h>

#include "object_detect_config.h"

typedef enum
{
    OBJECT_DETECT_LOAD_OK = 0,
    OBJECT_DETECT_LOAD_NO_SD,
    OBJECT_DETECT_LOAD_DIR,
    OBJECT_DETECT_LOAD_FILE,
    OBJECT_DETECT_LOAD_READ,
    OBJECT_DETECT_LOAD_ALLOC,
    OBJECT_DETECT_LOAD_MANIFEST,
    OBJECT_DETECT_LOAD_FORMAT,
    OBJECT_DETECT_LOAD_KPU,
    OBJECT_DETECT_LOAD_WORKER,
    OBJECT_DETECT_LOAD_BUSY,
} object_detect_load_result_t;

typedef struct
{
    uint8_t class_id;
    uint16_t confidence;
    int16_t x;
    int16_t y;
    int16_t w;
    int16_t h;
} object_detect_result_t;

typedef struct
{
    uint8_t fps_enabled;
    uint8_t light_mode;
    uint8_t rgb_red;
    uint8_t rgb_green;
    uint8_t rgb_blue;
    uint8_t confidence;
    uint8_t nms;
} object_detect_preferences_t;

typedef struct
{
    float x;
    float y;
    float w;
    float h;
} object_detect_candidate_t;

typedef struct
{
    object_detect_candidate_t candidates[OBJECT_DETECT_CANDIDATE_MAX];
    float probabilities[OBJECT_DETECT_CANDIDATE_MAX * OBJECT_DETECT_CLASS_COUNT];
    uint8_t suppressed[OBJECT_DETECT_CANDIDATE_MAX];
} object_detect_postprocess_workspace_t;

typedef struct
{
    float raw_min;
    float raw_max;
    float max_objectness;
    float max_class_probability;
    float max_probability;
    uint16_t finite_count;
    uint16_t nonfinite_count;
    uint16_t zero_count;
    uint16_t subnormal_count;
    uint16_t top_cell;
    uint8_t top_anchor;
    uint8_t top_class;
} object_detect_postprocess_stats_t;

#endif

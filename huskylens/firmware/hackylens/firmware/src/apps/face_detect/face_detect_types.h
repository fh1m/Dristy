#ifndef HK_FACE_DETECT_TYPES_H
#define HK_FACE_DETECT_TYPES_H

#include <stdint.h>

#define FACE_DETECT_BOX_MAX 8U

typedef enum
{
    FACE_DETECT_LOAD_OK = 0,
    FACE_DETECT_LOAD_NO_SD,
    FACE_DETECT_LOAD_DIR,
    FACE_DETECT_LOAD_FILE,
    FACE_DETECT_LOAD_READ,
    FACE_DETECT_LOAD_ALLOC,
    FACE_DETECT_LOAD_FORMAT,
    FACE_DETECT_LOAD_KPU,
    FACE_DETECT_LOAD_BUSY,
} face_detect_load_result_t;

typedef struct
{
    int16_t x;
    int16_t y;
    int16_t w;
    int16_t h;
} face_detect_box_t;

#endif

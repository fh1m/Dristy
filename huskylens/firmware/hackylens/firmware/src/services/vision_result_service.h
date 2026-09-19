#ifndef HK_VISION_RESULT_SERVICE_H
#define HK_VISION_RESULT_SERVICE_H

#include <stdint.h>

#define VISION_RESULT_MAX_ITEMS 8U

typedef enum
{
    VISION_ITEM_BLOCK = 1,
    VISION_ITEM_ARROW = 2,
} vision_item_kind_t;

typedef enum
{
    VISION_SOURCE_NONE = 0,
    VISION_SOURCE_FACE = 1,
    VISION_SOURCE_QR = 2,
    VISION_SOURCE_APRILTAG = 3,
    VISION_SOURCE_OBJECT_DETECT = 4,
    VISION_SOURCE_USER = 255,
} vision_source_t;

typedef struct
{
    uint8_t kind;
    uint8_t flags;
    uint16_t id;
    uint16_t x0;
    uint16_t y0;
    uint16_t x1;
    uint16_t y1;
    uint16_t confidence;
    uint16_t reserved;
} vision_result_item_t;

typedef struct
{
    uint32_t frame_id;
    uint8_t source;
    uint8_t count;
    uint16_t width;
    uint16_t height;
    vision_result_item_t items[VISION_RESULT_MAX_ITEMS];
} vision_result_snapshot_t;

void vision_result_publish(vision_source_t source, uint16_t width, uint16_t height,
                           const vision_result_item_t *items, uint8_t count);
void vision_result_clear(vision_source_t source);
void vision_result_snapshot(vision_result_snapshot_t *snapshot);

#endif

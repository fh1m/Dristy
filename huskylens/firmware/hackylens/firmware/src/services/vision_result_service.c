#include "vision_result_service.h"

#include <string.h>

static vision_result_snapshot_t g_snapshot;

void vision_result_publish(vision_source_t source, uint16_t width, uint16_t height,
                           const vision_result_item_t *items, uint8_t count)
{
    if(count > VISION_RESULT_MAX_ITEMS)
        count = VISION_RESULT_MAX_ITEMS;
    g_snapshot.frame_id++;
    g_snapshot.source = (uint8_t)source;
    g_snapshot.count = items ? count : 0U;
    g_snapshot.width = width;
    g_snapshot.height = height;
    if(items && count)
        memcpy(g_snapshot.items, items, (size_t)count * sizeof(*items));
    if(count < VISION_RESULT_MAX_ITEMS)
        memset(g_snapshot.items + count, 0,
               (VISION_RESULT_MAX_ITEMS - count) * sizeof(g_snapshot.items[0]));
}

void vision_result_clear(vision_source_t source)
{
    if(source != VISION_SOURCE_NONE && g_snapshot.source != (uint8_t)source)
        return;
    g_snapshot.frame_id++;
    g_snapshot.source = VISION_SOURCE_NONE;
    g_snapshot.count = 0U;
    g_snapshot.width = 0U;
    g_snapshot.height = 0U;
    memset(g_snapshot.items, 0, sizeof(g_snapshot.items));
}

void vision_result_snapshot(vision_result_snapshot_t *snapshot)
{
    if(snapshot)
        *snapshot = g_snapshot;
}

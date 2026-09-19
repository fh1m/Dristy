#ifndef HK_OBJECT_DETECT_VIEW_H
#define HK_OBJECT_DETECT_VIEW_H

#include <stdint.h>

#include "../../ui/camera_view.h"
#include "object_detect_types.h"

void object_detect_view_compose_results(
    camera_view_present_t *present,
    uint16_t width,
    uint16_t height,
    const object_detect_result_t *results,
    uint8_t count);
void object_detect_view_draw_icon(uint16_t x, uint16_t y,
                                  uint16_t color, uint16_t bg);

#endif

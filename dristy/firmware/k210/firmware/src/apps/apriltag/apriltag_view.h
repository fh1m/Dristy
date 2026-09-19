#ifndef HK_APRILTAG_VIEW_H
#define HK_APRILTAG_VIEW_H

#include <stdint.h>

#include "../../ui/camera_view.h"
#include "apriltag_types.h"

void apriltag_view_compose_results(camera_view_present_t *present,
                                   uint16_t width,
                                   uint16_t height,
                                   const apriltag_result_t *results,
                                   const uint8_t *selected,
                                   uint8_t count);
void apriltag_view_draw_icon(uint16_t x, uint16_t y, uint16_t color, uint16_t bg);

#endif

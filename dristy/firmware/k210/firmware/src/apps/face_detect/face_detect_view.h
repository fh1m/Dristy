#ifndef HK_FACE_DETECT_VIEW_H
#define HK_FACE_DETECT_VIEW_H

#include <stdint.h>

#include "../../ui/camera_view.h"
#include "face_detect_types.h"

void face_detect_view_draw_boxes(uint16_t width, uint16_t height,
                                 const face_detect_box_t *boxes, uint8_t count);
void face_detect_view_compose_boxes(camera_view_present_t *present,
                                    uint16_t width,
                                    uint16_t height,
                                    const face_detect_box_t *boxes,
                                    uint8_t count);
void face_detect_view_draw_icon(uint16_t x, uint16_t y, uint16_t color, uint16_t bg);

#endif

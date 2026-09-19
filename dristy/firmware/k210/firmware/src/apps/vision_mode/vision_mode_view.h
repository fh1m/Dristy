#ifndef HK_VISION_MODE_VIEW_H
#define HK_VISION_MODE_VIEW_H

#include "../../dristy/dristy_modes.h"
#include "../../dristy/dristy_result_bus.h"
#include "../../ui/camera_view.h"

void vision_mode_view_draw_stub(dristy_mode_t mode);
void vision_mode_view_draw_overlays(const dristy_result_snapshot_t *snap);
void vision_mode_view_compose_overlays(camera_view_present_t *present,
                                       uint16_t width,
                                       uint16_t height,
                                       const dristy_result_snapshot_t *snap);
void vision_mode_view_draw_icon(uint16_t x, uint16_t y, uint16_t color, uint16_t bg);

#endif

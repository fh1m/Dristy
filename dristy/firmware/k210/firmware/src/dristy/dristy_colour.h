#ifndef DRISTY_COLOUR_H
#define DRISTY_COLOUR_H

#include <stdint.h>

#include "dristy_result_bus.h"

void dristy_colour_init(void);
void dristy_colour_set_thresholds(uint8_t l_min, uint8_t l_max,
                                  uint8_t a_min, uint8_t a_max,
                                  uint8_t b_min, uint8_t b_max);
void dristy_colour_process_rgb565(const volatile uint16_t *rgb, uint16_t w, uint16_t h,
                                  dristy_result_snapshot_t *snap, uint8_t sort_by_area);

#endif

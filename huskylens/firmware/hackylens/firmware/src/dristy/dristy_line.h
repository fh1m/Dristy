#ifndef DRISTY_LINE_H
#define DRISTY_LINE_H

#include <stdint.h>

#include "dristy_result_bus.h"

void dristy_line_init(void);
void dristy_line_process_gray(const uint8_t *gray, uint16_t w, uint16_t h,
                              dristy_result_snapshot_t *snap);

#endif

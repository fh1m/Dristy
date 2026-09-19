#ifndef DEBUG_SCREENSHOT_STREAM_H
#define DEBUG_SCREENSHOT_STREAM_H

#include "../core/pixel_source.h"

void debug_uart_send_screenshot(const screenshot_pixel_source_t *source);

#endif

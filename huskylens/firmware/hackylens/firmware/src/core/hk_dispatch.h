#ifndef HK_DISPATCH_H
#define HK_DISPATCH_H

#include "hk_app.h"
#include "hk_events.h"

typedef void (*hk_sd_event_handler_t)(hk_sd_event_t event);

void shell_set_sd_event_handler(hk_sd_event_handler_t handler);
void shell_handle_buttons(const hk_input_snapshot_t *input);
void shell_handle_sd_event(hk_sd_event_t event);

#endif

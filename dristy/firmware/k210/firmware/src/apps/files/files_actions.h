#ifndef FILES_ACTIONS_H
#define FILES_ACTIONS_H

#include <stdint.h>

#include "../../core/hk_events.h"

void files_backend_enter(void);
uint8_t files_on_sd_event(hk_sd_event_t event);
void files_nav_delta(int8_t delta);
uint8_t files_back_from_list(void);
void files_open_selected(void);
uint8_t files_open_image_at_index(uint8_t index);
uint8_t files_open_image_from_current_or_next(void);
uint8_t files_delete_confirm_enter(void);
void files_delete_cancel(void);
void files_delete_confirmed(void);
void files_delete_state_reset(void);

#endif

#ifndef HK_SCREEN_H
#define HK_SCREEN_H

#include "hk_app.h"

typedef void (*hk_screen_wake_handler_t)(void);

screen_t hk_screen_get(void);
void hk_screen_set(screen_t screen);
void hk_screen_set_wake_handler(hk_screen_wake_handler_t handler);
void hk_screen_request_wake(void);
const char *screen_label(screen_t screen);
uint64_t hk_last_activity_us(void);
void activity_note(void);

#endif

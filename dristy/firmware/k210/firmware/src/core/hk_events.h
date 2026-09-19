#ifndef HK_EVENTS_H
#define HK_EVENTS_H

#include <stdint.h>

typedef enum
{
    HK_SD_EVENT_NONE = 0,
    HK_SD_EVENT_INSERTED,
    HK_SD_EVENT_REMOVED,
    HK_SD_EVENT_MOUNTED,
    HK_SD_EVENT_ERROR,
} hk_sd_event_t;

#endif

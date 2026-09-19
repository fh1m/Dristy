#ifndef DRISTY_MODE_READINESS_H
#define DRISTY_MODE_READINESS_H

#include <stdint.h>

#include "dristy_modes.h"

typedef enum
{
    DRISTY_READINESS_STUB = 0,
    DRISTY_READINESS_LIVE,
} dristy_readiness_t;

dristy_readiness_t dristy_mode_readiness(dristy_mode_t mode);

const char *dristy_mode_menu_subtitle(dristy_mode_t mode);

#endif

#include "dristy_mode_readiness.h"

#include <stddef.h>

dristy_readiness_t dristy_mode_readiness(dristy_mode_t mode)
{
    if(dristy_mode_info(mode) == NULL)
        return DRISTY_READINESS_STUB;
    return DRISTY_READINESS_LIVE;
}

const char *dristy_mode_menu_subtitle(dristy_mode_t mode)
{
    const dristy_mode_info_t *info = dristy_mode_info(mode);

    if(!info)
        return "";
    return info->short_name;
}

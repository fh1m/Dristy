#include "sd_service.h"

#include <stdio.h>

#include "../core/hk_events.h"

#include "../config/sd_config.h"

#include "../storage/sd_card.h"
#include "../storage/file_mount.h"
#include "../storage/fat32_volume.h"

static uint8_t g_sd_service_poll_ticks;

hk_sd_event_t sd_service_tick(void)
{
    uint8_t mounted;

    if(g_sd_service_poll_ticks > 0)
    {
        g_sd_service_poll_ticks--;
        return HK_SD_EVENT_NONE;
    }

    if(hk_sd_present() && hk_fat_mounted())
    {
        g_sd_service_poll_ticks = SD_POLL_MOUNTED_TICKS;
        if(!fat32_health_check())
        {
            fat32_mark_removed();
            printf("[SD] removed\r\n");
            return HK_SD_EVENT_REMOVED;
        }
        return HK_SD_EVENT_NONE;
    }

    fat32_set_probe_silent(1);
    mounted = file_mount_if_needed();
    fat32_set_probe_silent(0);
    g_sd_service_poll_ticks = SD_POLL_UNMOUNTED_TICKS;
    if(mounted)
    {
        printf("[SD] inserted mounted\r\n");
        return HK_SD_EVENT_MOUNTED;
    }

    return HK_SD_EVENT_NONE;
}

#include "file_mount.h"

#include <stdio.h>

#include "sd_card.h"
#include "fat32_volume.h"

uint8_t file_mount_if_needed(void)
{
    if(hk_sd_present() && hk_fat_mounted())
        return 1U;
    if(!sd_card_init())
        return 0U;
    if(!fat32_mount())
    {
        if(!fat32_probe_silent())
            printf("[SD] FAT32 mount failed\r\n");
        return 0U;
    }
    return 1U;
}

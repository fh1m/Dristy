#include "file_browser_state.h"

#include <string.h>
#include "../../storage/internal/fat32_state_private.h"

#include "../../config/sd_config.h"
#include "../../config/fat32_config.h"
#include "files_config.h"
#include "../../storage/fat32_allocation.h"
#include "file_preview.h"
#include "../../storage/fat32_file.h"
#include "../../storage/fat32_volume.h"
#include "../../storage/sd_card.h"

uint8_t files_read_preview(const fat_file_entry_t *entry)
{
    uint32_t cluster = entry->cluster;
    uint32_t remaining = entry->size;
    uint16_t out = 0;
    char *preview = files_preview_buffer();
    uint8_t sectors_per_cluster = fat32_sectors_per_cluster();
    uint8_t *sector = fat32_sector_scratch();

    memset(preview, 0, FILE_PREVIEW_MAX + 1U);
    while(cluster >= 2 && cluster < FAT32_EOC && remaining && out < FILE_PREVIEW_MAX)
    {
        uint32_t base = fat_cluster_lba(cluster);
        for(uint8_t s = 0; s < sectors_per_cluster && remaining && out < FILE_PREVIEW_MAX; s++)
        {
            uint16_t take;
            if(!sd_card_read_block(base + s, sector))
                return 0;
            take = remaining > SD_BLOCK_SIZE ? SD_BLOCK_SIZE : (uint16_t)remaining;
            if(take > FILE_PREVIEW_MAX - out)
                take = FILE_PREVIEW_MAX - out;
            for(uint16_t i = 0; i < take; i++)
            {
                char c = (char)sector[i];
                if(c < ' ' || c > '~')
                    c = '.';
                preview[out++] = c;
            }
            remaining -= take;
        }
        if(out >= FILE_PREVIEW_MAX || remaining == 0)
            break;
        cluster = fat_next_cluster(cluster);
    }

    files_set_preview_len(out);
    preview[out] = '\0';
    return 1;
}

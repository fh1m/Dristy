#include "file_delete.h"

#include <stdio.h>

#include "../../config/sd_config.h"
#include "../../config/fat32_config.h"
#include "file_browser_state.h"
#include "../../storage/internal/fat32_state_private.h"
#include "file_dir.h"
#include "../../storage/fat32_allocation.h"
#include "../../storage/fat32_directory.h"
#include "../../storage/fat32_volume.h"
#include "../../core/hk_binary.h"
#include "../../storage/sd_card.h"

static uint8_t files_delete_dir_contents(uint32_t dir_cluster)
{
    uint32_t current = dir_cluster;
    uint32_t ordinal = 0;
    uint8_t lfn_count = 0;
    uint8_t sectors_per_cluster = fat32_sectors_per_cluster();
    uint8_t *sector = fat32_sector_scratch();

    while(current >= 2 && current < FAT32_EOC)
    {
        uint32_t base = fat_cluster_lba(current);
        for(uint8_t s = 0; s < sectors_per_cluster; s++)
        {
            if(!sd_card_read_block(base + s, sector))
                return 0;
            for(uint16_t off = 0; off < SD_BLOCK_SIZE; off += 32)
            {
                const uint8_t *e = &sector[off];
                uint8_t attr = e[11];
                uint32_t entry_ordinal = ordinal++;
                uint8_t entry_lfn_count;
                uint32_t cluster;

                if(e[0] == 0x00)
                    return 1;
                if(e[0] == 0xE5)
                {
                    lfn_count = 0;
                    continue;
                }
                if(attr == 0x0F)
                {
                    if(lfn_count < 20)
                        lfn_count++;
                    continue;
                }
                if(attr & FAT_ATTR_VOLUME)
                {
                    lfn_count = 0;
                    continue;
                }
                if(e[0] == '.')
                {
                    lfn_count = 0;
                    continue;
                }

                entry_lfn_count = lfn_count;
                lfn_count = 0;
                cluster = ((uint32_t)rd16(&e[20]) << 16) | rd16(&e[26]);

                if((attr & FAT_ATTR_DIR) && cluster >= 2)
                {
                    if(!files_delete_dir_contents(cluster))
                        return 0;
                }
                if(!fat_dir_mark_deleted(dir_cluster, entry_ordinal, entry_lfn_count))
                    return 0;
                if(cluster >= 2 && !fat_free_chain(cluster))
                    return 0;
            }
        }
        current = fat_next_cluster(current);
    }

    return 1;
}

file_result_t files_delete_selected(void)
{
    fat_file_entry_t entry;
    uint8_t old_index;

    if(!hk_fat_mounted() || files_count() == 0)
        return FILE_RESULT_NOT_FOUND;

    if(!files_selected_entry())
        return FILE_RESULT_NOT_FOUND;
    entry = *files_selected_entry();
    old_index = files_index();

    if((entry.attr & FAT_ATTR_DIR) && entry.cluster < 2)
    {
        printf("[FILES] delete invalid dir %s cluster=%u\r\n", entry.name, entry.cluster);
        return FILE_RESULT_DELETE_FAILED;
    }

    if((entry.attr & FAT_ATTR_DIR) && !files_delete_dir_contents(entry.cluster))
    {
        printf("[FILES] delete recursive failed %s cluster=%u\r\n", entry.name, entry.cluster);
        return FILE_RESULT_DELETE_FAILED;
    }

    if(!fat_dir_mark_deleted(files_current_cluster(), entry.dir_ordinal, entry.lfn_count))
    {
        printf("[FILES] delete entry failed %s\r\n", entry.name);
        return FILE_RESULT_DELETE_FAILED;
    }

    if(entry.cluster >= 2 && !fat_free_chain(entry.cluster))
    {
        printf("[FILES] delete free failed %s cluster=%u\r\n", entry.name, entry.cluster);
        return FILE_RESULT_DELETE_FAILED;
    }

    printf("[FILES] deleted %s cluster=%u size=%u\r\n", entry.name, entry.cluster, (unsigned)entry.size);
    if(!files_load_dir(files_current_cluster()))
    {
        return FILE_RESULT_READ_ERROR;
    }
    if(files_count())
    {
        files_set_index(old_index >= files_count() ? (uint8_t)(files_count() - 1U) : old_index);
        files_ensure_visible();
    }
    return FILE_RESULT_OK;
}

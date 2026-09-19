#include "internal/fat32_state_private.h"

#include <string.h>

#include "fat32_types.h"

#include "../config/sd_config.h"
#include "../config/fat32_config.h"
#include "fat32_allocation.h"
#include "fat32_directory.h"

#include "../core/hk_binary.h"
#include "sd_card.h"

uint8_t fat_dir_find_slots(uint32_t dir_cluster, uint8_t needed, fat_dir_slot_t *slot)
{
    uint32_t current = dir_cluster;
    uint32_t previous = 0;
    const fat32_geometry_t *geometry = fat32_geometry();
    uint8_t *sector_data = fat32_sector_scratch();

    while(current >= 2 && current < FAT32_EOC)
    {
        uint32_t base = fat_cluster_lba(current);
        for(uint8_t s = 0; s < geometry->sectors_per_cluster; s++)
        {
            uint8_t run = 0;
            uint8_t run_start = 0;
            if(!sd_card_read_block(base + s, sector_data))
                return 0;
            for(uint8_t entry = 0; entry < 16; entry++)
            {
                uint8_t first = sector_data[entry * 32U];
                if(first == 0x00 || first == 0xE5)
                {
                    if(run == 0)
                        run_start = entry;
                    run++;
                    if(run >= needed)
                    {
                        slot->cluster = current;
                        slot->sector = s;
                        slot->entry = run_start;
                        return 1;
                    }
                }
                else
                {
                    run = 0;
                }
            }
        }
        previous = current;
        current = fat_next_cluster(current);
    }

    if(previous >= 2)
    {
        uint32_t new_cluster;
        if(!fat_alloc_chain(1, &new_cluster))
            return 0;
        if(!fat_zero_cluster(new_cluster))
            return 0;
        if(!fat_write_entry(previous, new_cluster))
            return 0;
        slot->cluster = new_cluster;
        slot->sector = 0;
        slot->entry = 0;
        return 1;
    }

    return 0;
}

uint8_t fat_dir_write_slots(const fat_dir_slot_t *slot, const uint8_t *entries, uint8_t count)
{
    uint32_t sector = fat_cluster_lba(slot->cluster) + slot->sector;
    uint16_t offset = (uint16_t)slot->entry * 32U;
    uint8_t *sector_data = fat32_sector_scratch();

    if(offset + (uint16_t)count * 32U > SD_BLOCK_SIZE)
        return 0;
    if(!sd_card_read_block(sector, sector_data))
        return 0;
    memcpy(&sector_data[offset], entries, (size_t)count * 32U);
    return sd_card_write_block(sector, sector_data);
}

uint8_t fat_dir_mark_deleted(uint32_t dir_cluster, uint32_t entry_ordinal, uint8_t lfn_count)
{
    uint32_t start = entry_ordinal >= lfn_count ? entry_ordinal - lfn_count : 0;
    uint32_t end = entry_ordinal;
    uint32_t ordinal = 0;
    uint32_t current = dir_cluster;
    const fat32_geometry_t *geometry = fat32_geometry();
    uint8_t *sector_data = fat32_sector_scratch();

    while(current >= 2 && current < FAT32_EOC)
    {
        uint32_t base = fat_cluster_lba(current);
        for(uint8_t s = 0; s < geometry->sectors_per_cluster; s++)
        {
            uint8_t dirty = 0;
            if(!sd_card_read_block(base + s, sector_data))
                return 0;
            for(uint8_t entry = 0; entry < 16; entry++, ordinal++)
            {
                if(ordinal >= start && ordinal <= end)
                {
                    sector_data[entry * 32U] = 0xE5;
                    dirty = 1;
                }
                if(ordinal > end)
                    break;
            }
            if(dirty && !sd_card_write_block(base + s, sector_data))
                return 0;
            if(ordinal > end)
                return 1;
        }
        current = fat_next_cluster(current);
    }

    return 0;
}

void fat_make_short_name(uint8_t out[11], const char *base, const char *ext)
{
    memset(out, ' ', 11);
    for(uint8_t i = 0; i < 8 && base[i]; i++)
        out[i] = (uint8_t)base[i];
    for(uint8_t i = 0; i < 3 && ext[i]; i++)
        out[8 + i] = (uint8_t)ext[i];
}

uint8_t fat_lfn_checksum(const uint8_t short_name[11])
{
    uint8_t sum = 0;
    for(uint8_t i = 0; i < 11; i++)
        sum = (uint8_t)(((sum & 1U) ? 0x80U : 0U) + (sum >> 1) + short_name[i]);
    return sum;
}

void fat_make_lfn_entry(uint8_t *entry, const char *name, uint8_t seq, uint8_t last, uint8_t checksum)
{
    static const uint8_t pos[13] = {1, 3, 5, 7, 9, 14, 16, 18, 20, 22, 24, 28, 30};
    size_t len = strlen(name);

    memset(entry, 0xFF, 32);
    entry[0] = seq | (last ? 0x40U : 0U);
    entry[11] = 0x0F;
    entry[12] = 0x00;
    entry[13] = checksum;
    entry[26] = 0x00;
    entry[27] = 0x00;

    for(uint8_t i = 0; i < 13; i++)
    {
        uint32_t name_index = (uint32_t)(seq - 1U) * 13U + i;
        uint16_t ch = 0xFFFF;
        if(name_index < len)
            ch = (uint8_t)name[name_index];
        else if(name_index == len)
            ch = 0x0000;
        wr16(&entry[pos[i]], ch);
    }
}

void fat_make_short_entry(uint8_t *entry, const uint8_t short_name[11], uint8_t attr, uint32_t cluster, uint32_t size)
{
    memset(entry, 0, 32);
    memcpy(entry, short_name, 11);
    entry[11] = attr;
    wr16(&entry[20], (uint16_t)(cluster >> 16));
    wr16(&entry[26], (uint16_t)cluster);
    wr32(&entry[28], size);
}

uint8_t fat_init_directory_cluster(uint32_t cluster, uint32_t parent_cluster)
{
    uint8_t dot[11];
    uint8_t dotdot[11];
    uint8_t *sector_data = fat32_sector_scratch();

    if(!fat_zero_cluster(cluster))
        return 0;

    memset(sector_data, 0, SD_BLOCK_SIZE);
    memset(dot, ' ', sizeof(dot));
    memset(dotdot, ' ', sizeof(dotdot));
    dot[0] = '.';
    dotdot[0] = '.';
    dotdot[1] = '.';
    fat_make_short_entry(&sector_data[0], dot, FAT_ATTR_DIR, cluster, 0);
    fat_make_short_entry(&sector_data[32], dotdot, FAT_ATTR_DIR, parent_cluster, 0);
    return sd_card_write_block(fat_cluster_lba(cluster), sector_data);
}

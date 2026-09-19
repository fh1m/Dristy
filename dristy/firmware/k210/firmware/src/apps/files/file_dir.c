#include "file_dir.h"

#include <stdio.h>
#include <string.h>

#include "../../config/sd_config.h"
#include "../../config/fat32_config.h"
#include "files_config.h"
#include "../../core/file_name.h"
#include "../../core/hk_string.h"
#include "file_browser_state.h"
#include "../../storage/internal/fat32_state_private.h"
#include "../../storage/file_mount.h"
#include "../../storage/fat32_allocation.h"
#include "../../storage/fat32_volume.h"
#include "../../core/hk_binary.h"
#include "../../storage/sd_card.h"

static void files_add_entry(const char *name, uint8_t attr, uint32_t cluster, uint32_t size,
                            uint32_t modified, uint32_t dir_ordinal, uint8_t lfn_count)
{
    if(name[0] == '\0')
        return;
    files_append_entry(name, attr, cluster, size, modified, dir_ordinal, lfn_count);
}

static void files_short_name(const uint8_t *e, char *out, size_t out_size)
{
    char base[9];
    char ext[4];
    uint8_t bi = 0;
    uint8_t ei = 0;

    memset(base, 0, sizeof(base));
    memset(ext, 0, sizeof(ext));
    for(uint8_t i = 0; i < 8 && e[i] != ' '; i++)
        base[bi++] = (char)e[i];
    for(uint8_t i = 8; i < 11 && e[i] != ' '; i++)
        ext[ei++] = (char)e[i];
    if(ext[0])
        snprintf(out, out_size, "%s.%s", base, ext);
    else
        snprintf(out, out_size, "%s", base);
}

static void files_lfn_store(const uint8_t *e, uint16_t *lfn, size_t lfn_units)
{
    static const uint8_t pos[13] = {1, 3, 5, 7, 9, 14, 16, 18, 20, 22, 24, 28, 30};
    uint8_t seq = (uint8_t)(e[0] & 0x1F);
    uint16_t offset;

    if(e[0] & 0x40)
        memset(lfn, 0, lfn_units * sizeof(*lfn));
    if(seq == 0)
        return;

    offset = (uint16_t)(seq - 1U) * 13U;
    for(uint8_t i = 0; i < 13 && offset + i + 1U < lfn_units; i++)
    {
        uint16_t ch = rd16(&e[pos[i]]);
        if(ch == 0x0000 || ch == 0xFFFF)
        {
            lfn[offset + i] = 0U;
            break;
        }
        lfn[offset + i] = ch;
    }
}

static uint8_t files_parse_dir_sector(const uint8_t *sector, uint16_t *lfn,
                                      uint8_t *lfn_count, uint32_t *ordinal)
{
    for(uint16_t off = 0; off < SD_BLOCK_SIZE; off += 32)
    {
        const uint8_t *e = &sector[off];
        uint8_t attr = e[11];
        char name[FILE_NAME_MAX];
        uint32_t cluster;
        uint32_t entry_ordinal = (*ordinal)++;

        if(e[0] == 0x00)
            return 0;
        if(e[0] == 0xE5)
        {
            lfn[0] = 0U;
            *lfn_count = 0;
            continue;
        }
        if(attr == 0x0F)
        {
            files_lfn_store(e, lfn, FILE_LFN_MAX_UNITS);
            if(*lfn_count < 20)
                (*lfn_count)++;
            continue;
        }
        if(attr & 0x08)
        {
            lfn[0] = 0U;
            *lfn_count = 0;
            continue;
        }
        if(e[0] == '.')
        {
            lfn[0] = 0U;
            *lfn_count = 0;
            continue;
        }

        if(lfn[0])
            utf16_to_utf8(lfn, FILE_LFN_MAX_UNITS, name, sizeof(name));
        else
            files_short_name(e, name, sizeof(name));
        lfn[0] = 0U;

        cluster = ((uint32_t)rd16(&e[20]) << 16) | rd16(&e[26]);
        files_add_entry(name, attr, cluster, rd32(&e[28]),
                        ((uint32_t)rd16(&e[24]) << 16) | rd16(&e[22]),
                        entry_ordinal, *lfn_count);
        *lfn_count = 0;
    }
    return 1;
}

uint8_t files_load_dir(uint32_t cluster)
{
    uint16_t lfn[FILE_LFN_MAX_UNITS];
    uint32_t current = cluster;
    uint32_t ordinal = 0;
    uint8_t lfn_count = 0;
    uint8_t keep_going = 1;
    uint8_t sectors_per_cluster = fat32_sectors_per_cluster();
    uint8_t *sector = fat32_sector_scratch();

    files_reset_list();
    memset(lfn, 0, sizeof(lfn));
    while(current >= 2 && current < FAT32_EOC && keep_going)
    {
        uint32_t base = fat_cluster_lba(current);
        for(uint8_t s = 0; s < sectors_per_cluster; s++)
        {
            if(!sd_card_read_block(base + s, sector))
                return 0;
            keep_going = files_parse_dir_sector(sector, lfn, &lfn_count, &ordinal);
            if(!keep_going || files_count() >= FILES_MAX_ENTRIES)
                break;
        }
        if(!keep_going || files_count() >= FILES_MAX_ENTRIES)
            break;
        current = fat_next_cluster(current);
    }
    files_sort_newest_first();
    return 1;
}

#include "file_dir_scan.h"

#include <stdio.h>
#include <string.h>

#include "../config/fat32_config.h"
#include "../config/sd_config.h"
#include "../core/file_name.h"
#include "../core/hk_binary.h"
#include "../core/hk_string.h"
#include "sd_card.h"
#include "fat32_allocation.h"
#include "fat32_volume.h"
#include "internal/fat32_state_private.h"

typedef uint8_t (*file_dir_visitor_t)(const fat_file_entry_t *entry, void *context);

static void file_dir_short_name(const uint8_t *raw, char *name, size_t name_size)
{
    char base[9] = {0};
    char ext[4] = {0};
    uint8_t base_len = 0U;
    uint8_t ext_len = 0U;

    for(uint8_t i = 0U; i < 8U && raw[i] != ' '; i++)
        base[base_len++] = (char)raw[i];
    for(uint8_t i = 8U; i < 11U && raw[i] != ' '; i++)
        ext[ext_len++] = (char)raw[i];
    if(ext[0])
        snprintf(name, name_size, "%s.%s", base, ext);
    else
        snprintf(name, name_size, "%s", base);
}

static void file_dir_lfn_store(const uint8_t *raw, uint16_t *lfn, size_t lfn_units)
{
    static const uint8_t positions[13] = {1, 3, 5, 7, 9, 14, 16, 18, 20, 22, 24, 28, 30};
    uint8_t sequence = (uint8_t)(raw[0] & 0x1FU);
    uint16_t offset;

    if(raw[0] & 0x40U)
        memset(lfn, 0, lfn_units * sizeof(*lfn));
    if(sequence == 0U)
        return;
    offset = (uint16_t)(sequence - 1U) * 13U;
    for(uint8_t i = 0U; i < 13U && offset + i + 1U < lfn_units; i++)
    {
        uint16_t character = rd16(&raw[positions[i]]);

        if(character == 0x0000U || character == 0xFFFFU)
        {
            lfn[offset + i] = 0U;
            break;
        }
        lfn[offset + i] = character;
    }
}

static uint8_t file_dir_scan(uint32_t directory_cluster,
                             file_dir_visitor_t visitor,
                             void *context)
{
    uint16_t lfn[FILE_LFN_MAX_UNITS] = {0};
    uint32_t current = directory_cluster;
    uint32_t ordinal = 0U;
    uint8_t lfn_count = 0U;
    uint8_t sectors_per_cluster = fat32_sectors_per_cluster();
    uint8_t *sector = fat32_sector_scratch();

    while(current >= 2U && current < FAT32_EOC)
    {
        uint32_t base = fat_cluster_lba(current);

        for(uint8_t sector_index = 0U; sector_index < sectors_per_cluster; sector_index++)
        {
            if(!sd_card_read_block(base + sector_index, sector))
                return 0U;
            for(uint16_t offset = 0U; offset < SD_BLOCK_SIZE; offset += 32U)
            {
                const uint8_t *raw = &sector[offset];
                uint8_t attr = raw[11];
                fat_file_entry_t entry = {0};

                if(raw[0] == 0x00U)
                    return 1U;
                if(raw[0] == 0xE5U)
                {
                    lfn[0] = 0U;
                    lfn_count = 0U;
                    ordinal++;
                    continue;
                }
                if(attr == 0x0FU)
                {
                    file_dir_lfn_store(raw, lfn, FILE_LFN_MAX_UNITS);
                    if(lfn_count < 20U)
                        lfn_count++;
                    ordinal++;
                    continue;
                }
                if((attr & 0x08U) || raw[0] == '.')
                {
                    lfn[0] = 0U;
                    lfn_count = 0U;
                    ordinal++;
                    continue;
                }

                if(lfn[0])
                    utf16_to_utf8(lfn, FILE_LFN_MAX_UNITS, entry.name, sizeof(entry.name));
                else
                    file_dir_short_name(raw, entry.name, sizeof(entry.name));
                entry.attr = attr;
                entry.cluster = ((uint32_t)rd16(&raw[20]) << 16) | rd16(&raw[26]);
                entry.size = rd32(&raw[28]);
                entry.modified = ((uint32_t)rd16(&raw[24]) << 16) | rd16(&raw[22]);
                entry.dir_ordinal = ordinal++;
                entry.lfn_count = lfn_count;
                lfn[0] = 0U;
                lfn_count = 0U;
                if(visitor && visitor(&entry, context))
                    return 1U;
            }
        }
        current = fat_next_cluster(current);
    }
    return 1U;
}

typedef struct
{
    const char *long_name;
    const char *short_name;
    uint32_t *cluster;
    uint8_t found;
} file_dir_find_context_t;

static uint8_t file_dir_find_visitor(const fat_file_entry_t *entry, void *opaque)
{
    file_dir_find_context_t *context = (file_dir_find_context_t *)opaque;

    if(!(entry->attr & FAT_ATTR_DIR) ||
       (!str_eq_ci(entry->name, context->long_name) &&
        !str_eq_ci(entry->name, context->short_name)))
        return 0U;
    if(context->cluster)
        *context->cluster = entry->cluster;
    context->found = 1U;
    return 1U;
}

file_dir_scan_result_t file_dir_find_directory(uint32_t parent_cluster,
                                               const char *long_name,
                                               const char *short_name,
                                               uint32_t *cluster_out)
{
    file_dir_find_context_t context = {long_name, short_name, cluster_out, 0U};

    if(cluster_out)
        *cluster_out = 0U;
    if(!file_dir_scan(parent_cluster, file_dir_find_visitor, &context))
        return FILE_DIR_SCAN_ERROR;
    return context.found ? FILE_DIR_SCAN_FOUND : FILE_DIR_SCAN_NOT_FOUND;
}

typedef struct
{
    file_dir_index_parser_t parser;
    uint32_t maximum;
} file_dir_max_context_t;

static uint8_t file_dir_max_visitor(const fat_file_entry_t *entry, void *opaque)
{
    file_dir_max_context_t *context = (file_dir_max_context_t *)opaque;
    uint32_t index;

    if(!(entry->attr & FAT_ATTR_DIR) && context->parser(entry->name, &index) &&
       index > context->maximum)
        context->maximum = index;
    return 0U;
}

uint8_t file_dir_find_max_file_index(uint32_t dir_cluster,
                                     file_dir_index_parser_t parser,
                                     uint32_t *max_index_out)
{
    file_dir_max_context_t context = {parser, 0U};

    if(max_index_out)
        *max_index_out = 0U;
    if(!parser || !file_dir_scan(dir_cluster, file_dir_max_visitor, &context))
        return 0U;
    if(max_index_out)
        *max_index_out = context.maximum;
    return 1U;
}

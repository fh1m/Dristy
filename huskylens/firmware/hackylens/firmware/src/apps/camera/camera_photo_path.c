#include "camera_photo_path.h"

#include <stdio.h>
#include <string.h>

#include "../../storage/fat32_types.h"
#include "../../core/photo_types.h"

#include "camera_config.h"
#include "../../config/fat32_config.h"

#include "../../storage/file_dir_scan.h"
#include "../../storage/file_write_error.h"
#include "../../storage/fat32_allocation.h"
#include "../../storage/fat32_directory.h"
#include "../../storage/fat32_volume.h"
#include "../../core/hk_string.h"

static uint32_t g_photo_dir_cluster;

static uint8_t photo_create_dir(uint32_t *cluster_out)
{
    uint32_t dir_cluster;
    fat_dir_slot_t slot;
    uint8_t entries[32 * 3];
    uint8_t short_name[11];
    uint8_t checksum;

    if(!fat_alloc_chain(1, &dir_cluster))
    {
        file_write_set_error("MKDIR ALLOC");
        return 0;
    }
    if(!fat_init_directory_cluster(dir_cluster, hk_fat_root_cluster()))
    {
        file_write_set_error("MKDIR ZERO");
        return 0;
    }

    fat_make_short_name(short_name, "HACKY~1", "PHO");
    checksum = fat_lfn_checksum(short_name);
    memset(entries, 0, sizeof(entries));
    fat_make_lfn_entry(&entries[0], PHOTO_DIR_LONG_NAME, 2, 1, checksum);
    fat_make_lfn_entry(&entries[32], PHOTO_DIR_LONG_NAME, 1, 0, checksum);
    fat_make_short_entry(&entries[64], short_name, FAT_ATTR_DIR, dir_cluster, 0);

    if(!fat_dir_find_slots(hk_fat_root_cluster(), 3, &slot))
    {
        file_write_set_error("MKDIR SLOT");
        return 0;
    }
    if(!fat_dir_write_slots(&slot, entries, 3))
    {
        file_write_set_error("MKDIR ENTRY");
        return 0;
    }

    *cluster_out = dir_cluster;
    printf("[PHOTO] mkdir %s cluster=%u\r\n", PHOTO_DIR_LONG_NAME, dir_cluster);
    return 1;
}

static uint8_t photo_ensure_dir(void)
{
    file_dir_scan_result_t result;

    g_photo_dir_cluster = 0;
    result = file_dir_find_directory(hk_fat_root_cluster(), PHOTO_DIR_LONG_NAME, "HACKY~1.PHO", &g_photo_dir_cluster);
    if(result == FILE_DIR_SCAN_ERROR)
    {
        file_write_set_error("ROOT READ");
        printf("[PHOTO] root read failed\r\n");
        return 0;
    }
    if(result == FILE_DIR_SCAN_FOUND)
    {
        printf("[PHOTO] dir exists cluster=%u\r\n", g_photo_dir_cluster);
        return g_photo_dir_cluster >= 2;
    }

    printf("[PHOTO] dir missing, creating %s\r\n", PHOTO_DIR_LONG_NAME);
    return photo_create_dir(&g_photo_dir_cluster);
}

static uint8_t photo_parse_index(const char *name, uint32_t *index_out)
{
    uint32_t value = 0;

    if(strlen(name) != 12)
        return 0;
    if(ascii_lower(name[0]) != 'i' || ascii_lower(name[1]) != 'm' || ascii_lower(name[2]) != 'g')
        return 0;
    if(name[8] != '.')
        return 0;
    for(uint8_t i = 3; i < 8; i++)
    {
        if(name[i] < '0' || name[i] > '9')
            return 0;
        value = value * 10U + (uint32_t)(name[i] - '0');
    }

    *index_out = value;
    return value > 0;
}

static uint8_t photo_next_name(uint8_t short_name[11], char *display, size_t display_size, photo_format_t format)
{
    uint32_t max_index = 0;
    const char *ext = photo_format_ext(format);

    if(!file_dir_find_max_file_index(g_photo_dir_cluster, photo_parse_index, &max_index))
    {
        file_write_set_error("DIR READ");
        return 0;
    }
    if(max_index >= 99999)
    {
        file_write_set_error("NAME FULL");
        return 0;
    }

    snprintf(display, display_size, "IMG%05u.%s", (unsigned)(max_index + 1U), ext);
    fat_make_short_name(short_name, display, ext);
    return 1;
}

uint8_t photo_path_prepare(uint8_t short_name[11], char *display, size_t display_size, photo_format_t format)
{
    if(!photo_ensure_dir())
        return 0;
    return photo_next_name(short_name, display, display_size, format);
}

uint8_t photo_path_add_file_entry(const uint8_t short_name[11], uint32_t first_cluster, uint32_t file_size)
{
    fat_dir_slot_t slot;
    uint8_t entry[32];

    fat_make_short_entry(entry, short_name, FAT_ATTR_ARCHIVE, first_cluster, file_size);
    if(!fat_dir_find_slots(g_photo_dir_cluster, 1, &slot))
    {
        file_write_set_error("FILE SLOT");
        return 0;
    }
    file_write_set_error("FILE ENTRY");
    return fat_dir_write_slots(&slot, entry, 1);
}

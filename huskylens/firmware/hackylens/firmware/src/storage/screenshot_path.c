#include "screenshot_path.h"

#include <stdio.h>
#include <string.h>

#include "fat32_types.h"

#include "../config/screenshot_config.h"
#include "../config/fat32_config.h"

#include "file_dir_scan.h"
#include "fat32_allocation.h"
#include "fat32_directory.h"
#include "fat32_volume.h"
#include "../core/hk_string.h"

static uint32_t g_screenshot_dir_cluster;

static void screenshot_path_error(const char **error, const char *text)
{
    if(error)
        *error = text;
}

static uint8_t screenshot_create_dir(uint32_t *cluster_out, const char **error)
{
    uint32_t dir_cluster;
    fat_dir_slot_t slot;
    uint8_t entries[32 * 3];
    uint8_t short_name[11];
    uint8_t checksum;

    if(!fat_alloc_chain(1, &dir_cluster))
    {
        screenshot_path_error(error, "MKDIR ALLOC");
        return 0;
    }
    if(!fat_init_directory_cluster(dir_cluster, hk_fat_root_cluster()))
    {
        screenshot_path_error(error, "MKDIR ZERO");
        return 0;
    }

    fat_make_short_name(short_name, "HACKY~1", "SCR");
    checksum = fat_lfn_checksum(short_name);
    memset(entries, 0, sizeof(entries));
    fat_make_lfn_entry(&entries[0], SCREENSHOT_DIR_LONG_NAME, 2, 1, checksum);
    fat_make_lfn_entry(&entries[32], SCREENSHOT_DIR_LONG_NAME, 1, 0, checksum);
    fat_make_short_entry(&entries[64], short_name, FAT_ATTR_DIR, dir_cluster, 0);

    if(!fat_dir_find_slots(hk_fat_root_cluster(), 3, &slot))
    {
        screenshot_path_error(error, "MKDIR SLOT");
        return 0;
    }
    if(!fat_dir_write_slots(&slot, entries, 3))
    {
        screenshot_path_error(error, "MKDIR ENTRY");
        return 0;
    }

    *cluster_out = dir_cluster;
    printf("[SHOT] mkdir %s cluster=%u\r\n", SCREENSHOT_DIR_LONG_NAME, dir_cluster);
    return 1;
}

static uint8_t screenshot_ensure_dir(const char **error)
{
    file_dir_scan_result_t result;

    g_screenshot_dir_cluster = 0;
    result = file_dir_find_directory(hk_fat_root_cluster(), SCREENSHOT_DIR_LONG_NAME, "HACKY~1.SCR", &g_screenshot_dir_cluster);
    if(result == FILE_DIR_SCAN_ERROR)
    {
        screenshot_path_error(error, "ROOT READ");
        printf("[SHOT] root read failed\r\n");
        return 0;
    }
    if(result == FILE_DIR_SCAN_FOUND)
    {
        return g_screenshot_dir_cluster >= 2;
    }

    return screenshot_create_dir(&g_screenshot_dir_cluster, error);
}

static uint8_t screenshot_parse_index(const char *name, uint32_t *index_out)
{
    uint32_t value = 0;

    if(strlen(name) != 12)
        return 0;
    if(ascii_lower(name[0]) != 's' || ascii_lower(name[1]) != 'c' || ascii_lower(name[2]) != 'r')
        return 0;
    if(name[8] != '.' || ascii_lower(name[9]) != 'b' || ascii_lower(name[10]) != 'm' || ascii_lower(name[11]) != 'p')
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

static uint8_t screenshot_next_name(uint8_t short_name[11], char *display, size_t display_size, const char **error)
{
    uint32_t max_index = 0;

    if(!file_dir_find_max_file_index(g_screenshot_dir_cluster, screenshot_parse_index, &max_index))
    {
        screenshot_path_error(error, "DIR READ");
        return 0;
    }
    if(max_index >= 99999)
    {
        screenshot_path_error(error, "NAME FULL");
        return 0;
    }

    snprintf(display, display_size, "SCR%05u.BMP", (unsigned)(max_index + 1U));
    fat_make_short_name(short_name, display, "BMP");
    return 1;
}

uint8_t screenshot_path_prepare(uint8_t short_name[11], char *display, size_t display_size, const char **error)
{
    if(!screenshot_ensure_dir(error))
        return 0;
    return screenshot_next_name(short_name, display, display_size, error);
}

uint8_t screenshot_path_add_file_entry(const uint8_t short_name[11], uint32_t first_cluster, uint32_t file_size, const char **error)
{
    fat_dir_slot_t slot;
    uint8_t entry[32];

    fat_make_short_entry(entry, short_name, FAT_ATTR_ARCHIVE, first_cluster, file_size);
    if(!fat_dir_find_slots(g_screenshot_dir_cluster, 1, &slot))
    {
        screenshot_path_error(error, "FILE SLOT");
        return 0;
    }
    screenshot_path_error(error, "FILE ENTRY");
    return fat_dir_write_slots(&slot, entry, 1);
}

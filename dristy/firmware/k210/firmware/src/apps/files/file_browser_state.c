#include "file_browser_state.h"

#include <stddef.h>

#include <string.h>

#include "../../config/fat32_config.h"
#include "files_config.h"
#include "files_layout.h"
#include "file_browser_mode.h"

static uint32_t g_files_cluster;
static uint32_t g_files_stack[FILES_STACK_MAX];
static uint8_t g_files_depth;
static uint8_t g_files_count;
static uint8_t g_files_index;
static uint8_t g_files_top;
static file_browser_mode_t g_files_preview_mode;
static uint8_t g_files_preview_page;
static uint16_t g_files_preview_len;
static char g_files_preview[FILE_PREVIEW_MAX + 1];
static fat_file_entry_t g_files[FILES_MAX_ENTRIES];
static file_list_item_t g_file_list_items[FILES_MAX_ENTRIES];

const file_list_item_t *files_list_items(void)
{
    return g_file_list_items;
}

static void files_refresh_list_items(void)
{
    for(uint8_t i = 0; i < g_files_count; i++)
    {
        g_file_list_items[i].name = g_files[i].name;
        g_file_list_items[i].size = g_files[i].size;
        g_file_list_items[i].kind =
            (g_files[i].attr & FAT_ATTR_DIR) ? FILE_ITEM_DIRECTORY : FILE_ITEM_REGULAR;
    }
}

uint8_t files_append_entry(const char *name, uint8_t attr, uint32_t cluster, uint32_t size,
                           uint32_t modified, uint32_t dir_ordinal, uint8_t lfn_count)
{
    fat_file_entry_t *entry;
    file_list_item_t *item;

    if(g_files_count >= FILES_MAX_ENTRIES)
        return 0;

    entry = &g_files[g_files_count];
    item = &g_file_list_items[g_files_count++];
    strncpy(entry->name, name, FILE_NAME_MAX - 1U);
    entry->name[FILE_NAME_MAX - 1U] = '\0';
    entry->attr = attr;
    entry->cluster = cluster;
    entry->size = size;
    entry->modified = modified;
    entry->dir_ordinal = dir_ordinal;
    entry->lfn_count = lfn_count;

    item->name = entry->name;
    item->size = size;
    item->kind = (attr & FAT_ATTR_DIR) ? FILE_ITEM_DIRECTORY : FILE_ITEM_REGULAR;
    return 1;
}

void files_sort_newest_first(void)
{
    for(uint8_t i = 1; i < g_files_count; i++)
    {
        fat_file_entry_t key = g_files[i];
        uint8_t insert = i;

        while(insert > 0U &&
              (g_files[insert - 1U].modified < key.modified ||
               (g_files[insert - 1U].modified == key.modified &&
                g_files[insert - 1U].dir_ordinal < key.dir_ordinal)))
        {
            g_files[insert] = g_files[insert - 1U];
            insert--;
        }
        g_files[insert] = key;
    }
    files_refresh_list_items();
}

const fat_file_entry_t *files_entry_at(uint8_t index)
{
    if(index >= g_files_count)
        return NULL;
    return &g_files[index];
}

const fat_file_entry_t *files_selected_entry(void)
{
    return files_entry_at(g_files_index);
}

uint8_t files_count(void)
{
    return g_files_count;
}

uint8_t files_index(void)
{
    return g_files_index;
}

uint8_t files_top(void)
{
    return g_files_top;
}

void files_set_index(uint8_t index)
{
    g_files_index = index;
}

void files_reset_list(void)
{
    g_files_count = 0;
    g_files_index = 0;
    g_files_top = 0;
    memset(g_files, 0, sizeof(g_files));
    memset(g_file_list_items, 0, sizeof(g_file_list_items));
}

void files_ensure_visible(void)
{
    if(g_files_index < g_files_top)
        g_files_top = g_files_index;
    if(g_files_index >= g_files_top + FILES_ROWS)
        g_files_top = (uint8_t)(g_files_index - FILES_ROWS + 1);
}

uint32_t files_current_cluster(void)
{
    return g_files_cluster;
}

void files_set_current_cluster(uint32_t cluster)
{
    g_files_cluster = cluster;
}

void files_reset_depth(void)
{
    g_files_depth = 0;
}

uint8_t files_push_current_cluster(void)
{
    if(g_files_depth >= FILES_STACK_MAX)
        return 0;
    g_files_stack[g_files_depth++] = g_files_cluster;
    return 1;
}

uint8_t files_pop_cluster(uint32_t *cluster)
{
    if(g_files_depth == 0)
        return 0;
    g_files_depth--;
    if(cluster)
        *cluster = g_files_stack[g_files_depth];
    return 1;
}

uint8_t files_depth(void)
{
    return g_files_depth;
}

file_browser_mode_t files_mode(void)
{
    return g_files_preview_mode;
}

void files_set_mode(file_browser_mode_t mode)
{
    g_files_preview_mode = mode;
}

uint8_t files_preview_page(void)
{
    return g_files_preview_page;
}

void files_set_preview_page(uint8_t page)
{
    g_files_preview_page = page;
}

char *files_preview_buffer(void)
{
    return g_files_preview;
}

const char *files_preview_text(void)
{
    return g_files_preview;
}

uint16_t files_preview_len(void)
{
    return g_files_preview_len;
}

void files_set_preview_len(uint16_t len)
{
    g_files_preview_len = len;
}

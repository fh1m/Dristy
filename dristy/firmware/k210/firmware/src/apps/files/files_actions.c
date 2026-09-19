#include "files_actions.h"

#include <stdio.h>

#include "../../config/display_config.h"
#include "files_layout.h"
#include "../../config/fat32_config.h"
#include "../../core/file_name.h"
#include "../../core/hk_string.h"
#include "file_browser_list.h"
#include "file_browser_mode.h"
#include "file_browser_navigation.h"
#include "file_dir.h"
#include "file_preview.h"
#include "file_delete.h"
#include "../../storage/fat32_volume.h"
#include "image_viewer.h"
#include "files_presenter.h"

static void files_select_delta(int8_t delta);
static uint8_t files_open_image_delta(int8_t delta);
static file_browser_mode_t g_files_delete_origin_mode = FILES_MODE_LIST;

void files_delete_state_reset(void)
{
    g_files_delete_origin_mode = FILES_MODE_LIST;
}

static void files_preview_page_delta(int8_t delta)
{
    uint16_t chars_per_page = TERM_COLS * FILE_PREVIEW_ROWS;
    uint8_t page = files_preview_page();
    uint8_t max_page = files_preview_len() ? (uint8_t)((files_preview_len() - 1) / chars_per_page) : 0;

    if(delta < 0 && page > 0)
    {
        files_set_preview_page((uint8_t)(page - 1U));
        files_presenter_render_preview();
    }
    else if(delta > 0 && page < max_page)
    {
        files_set_preview_page((uint8_t)(page + 1U));
        files_presenter_render_preview();
    }
}

void files_nav_delta(int8_t delta)
{
    if(files_mode() == FILES_MODE_TEXT)
        files_preview_page_delta(delta);
    else if(files_mode() == FILES_MODE_IMAGE)
        files_open_image_delta(delta);
    else if(files_mode() == FILES_MODE_LIST)
        files_select_delta(delta);
}

static void files_select_delta(int8_t delta)
{
    uint8_t previous = files_index();
    uint8_t old_top = files_top();
    uint8_t previous_row;
    uint8_t current_row;
    uint8_t count = files_count();
    uint8_t index = previous;

    if(count == 0)
        return;

    if(delta < 0)
        index = index == 0 ? count - 1 : index - 1;
    else if(delta > 0)
        index = (uint8_t)((index + 1) % count);

    if(previous != index)
    {
        files_set_index(index);
        files_ensure_visible();
        if(old_top != files_top())
        {
            files_presenter_render_list();
            return;
        }

        previous_row = (uint8_t)(previous - files_top());
        current_row = (uint8_t)(files_index() - files_top());
        if(previous_row < FILES_ROWS)
            files_presenter_draw_row(previous_row);
        if(current_row < FILES_ROWS)
            files_presenter_draw_row(current_row);
    }
}

uint8_t files_open_image_at_index(uint8_t index)
{
    const fat_file_entry_t *entry = files_entry_at(index);

    if(!entry || !files_entry_is_image(entry))
        return 0;

    if(files_presenter_render_image(entry) != FILE_RESULT_OK)
        return 0;

    files_set_index(index);
    files_ensure_visible();
    files_set_mode(FILES_MODE_IMAGE);
    return 1;
}

static uint8_t files_open_image_delta(int8_t delta)
{
    uint8_t index;
    uint8_t count = files_count();

    if(count == 0)
        return 0;

    index = files_index();
    for(uint8_t tries = 0; tries < count; tries++)
    {
        if(delta < 0)
            index = index == 0 ? (uint8_t)(count - 1U) : (uint8_t)(index - 1U);
        else
            index = (uint8_t)((index + 1U) % count);

        if(files_entry_is_image(files_entry_at(index)))
            return files_open_image_at_index(index);
    }

    return 0;
}

uint8_t files_open_image_from_current_or_next(void)
{
    uint8_t count = files_count();
    uint8_t index = files_index();

    if(count == 0)
        return 0;
    if(index >= count)
    {
        index = (uint8_t)(count - 1U);
        files_set_index(index);
    }
    if(files_entry_is_image(files_entry_at(index)) && files_open_image_at_index(index))
        return 1;
    return files_open_image_delta(1);
}

void files_open_selected(void)
{
    const fat_file_entry_t *entry;

    if(!hk_fat_mounted() || files_count() == 0)
        return;

    entry = files_selected_entry();
    if(!entry)
        return;
    if(entry->attr & FAT_ATTR_DIR)
    {
        if(entry->cluster < 2)
            return;
        files_push_current_cluster();
        files_set_current_cluster(entry->cluster);
        if(!files_load_dir(files_current_cluster()))
            files_presenter_show_result(FILE_RESULT_READ_ERROR);
        else
            files_presenter_render_list();
        return;
    }

    if(files_entry_is_image(entry))
    {
        if(files_presenter_render_image(entry) == FILE_RESULT_OK)
            files_set_mode(FILES_MODE_IMAGE);
        return;
    }

    if(!files_read_preview(entry))
    {
        files_presenter_show_result(FILE_RESULT_READ_ERROR);
        return;
    }
    files_set_mode(FILES_MODE_TEXT);
    files_set_preview_page(0);
    files_presenter_render_preview();
}

uint8_t files_back_from_list(void)
{
    uint32_t cluster;

    if(hk_fat_mounted() && files_depth() > 0)
    {
        files_pop_cluster(&cluster);
        files_set_current_cluster(cluster);
        if(!files_load_dir(files_current_cluster()))
            files_presenter_show_result(FILE_RESULT_READ_ERROR);
        else
            files_presenter_render_list();
        return 0;
    }

    return 1;
}

uint8_t files_delete_confirm_enter(void)
{
    const fat_file_entry_t *entry;
    char name[FILE_NAME_MAX];

    if(!hk_fat_mounted() || files_count() == 0)
        return 0;

    entry = files_selected_entry();
    if(!entry)
        return 0;

    utf8_copy_glyphs(name, sizeof(name), entry->name, 20U);
    g_files_delete_origin_mode = files_mode();
    if(g_files_delete_origin_mode == FILES_MODE_IMAGE)
        files_presenter_close_image();
    files_set_mode(FILES_MODE_DELETE_CONFIRM);
    files_presenter_draw_delete_confirm(name);
    return 1;
}

void files_delete_cancel(void)
{
    file_browser_mode_t origin = g_files_delete_origin_mode;

    files_delete_state_reset();
    files_set_mode(origin);

    if(origin == FILES_MODE_IMAGE)
    {
        if(!files_open_image_at_index(files_index()))
        {
            files_set_mode(FILES_MODE_LIST);
            files_presenter_render_list();
        }
    }
    else if(origin == FILES_MODE_TEXT)
    {
        files_presenter_render_preview();
    }
    else
    {
        files_set_mode(FILES_MODE_LIST);
        files_presenter_render_list();
    }
}

void files_delete_confirmed(void)
{
    file_browser_mode_t origin = g_files_delete_origin_mode;
    file_result_t result;

    files_delete_state_reset();
    files_set_mode(FILES_MODE_LIST);
    result = files_delete_selected();

    if(result != FILE_RESULT_OK)
    {
        files_presenter_show_result(result);
        return;
    }

    if(origin == FILES_MODE_IMAGE && files_open_image_from_current_or_next())
        return;

    files_presenter_render_list();
}

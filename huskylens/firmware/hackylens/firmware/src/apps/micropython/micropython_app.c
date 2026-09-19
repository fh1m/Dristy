#include "micropython_app.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "../../config/input_config.h"
#include "../../core/hk_menu.h"
#include "../../core/hk_screen.h"
#include "../../core/hk_string.h"
#include "../../internal/boot_internal.h"
#include "../../services/debug_console_service.h"
#include "../../services/micropython_program.h"
#include "../../services/micropython_runtime.h"
#include "../../storage/userfs.h"
#include "micropython_config.h"
#include "micropython_view.h"

const char g_micropython_debug_help[] =
    "HKMPRUN HKMPTEST HKMPSTOP HKMPSTATUS HKMPLOG HKMPLIST HKMPFORMAT-CONFIRM";

static const char g_smoke_script[] =
    "print('MP_OK', sum(range(10)))\n"
    "for i in range(5):\n"
    "    print('tick', i)\n";
static const char g_stop_test_script[] =
    "print('MP_STOP_TEST_READY')\n"
    "while True:\n"
    "    pass\n";

typedef struct
{
    uint32_t count;
    uint32_t found_index;
    const char *wanted_name;
    uint8_t found;
} file_count_context_t;

typedef struct
{
    uint32_t index;
    uint8_t changed;
} file_page_context_t;

typedef struct
{
    const char *name;
    uint32_t size;
    uint32_t cursor;
    uint8_t data[MICROPYTHON_PREVIEW_READ_CHUNK];
    size_t data_size;
    size_t data_index;
    userfs_result_t result;
} preview_reader_t;

static micropython_view_state_t g_ui;
static char g_logs[MICROPYTHON_LOG_HISTORY_LINES]
                  [MICROPYTHON_LOG_COLUMNS + 1U];
static uint8_t g_log_start;
static uint8_t g_log_count;
static uint32_t g_preview_offset;
static uint32_t g_preview_next_offset;
static uint32_t g_preview_next_line;
static uint32_t g_preview_size;
static uint32_t g_observed_run_id;
static uint8_t g_screen_active;
static micropython_view_update_t g_render_update;
static uint8_t g_render_divider;
static uint8_t g_console_render_ticks;
static uint8_t g_console_live_dirty;
static uint16_t g_list_refresh_ticks;
static micropython_runtime_state_t g_last_state;
static micropython_runtime_exit_t g_last_exit;
static uint32_t g_last_dropped;

static void request_full_render(void)
{
    memset(&g_render_update, 0, sizeof(g_render_update));
    g_render_update.full = 1U;
}

static void request_info_render(void)
{
    if(!g_render_update.full)
        g_render_update.info = 1U;
}

static void request_body_render(void)
{
    if(!g_render_update.full)
    {
        g_render_update.body = 1U;
        g_render_update.row_mask = 0U;
    }
}

static void request_row_render(uint8_t row)
{
    if(!g_render_update.full && !g_render_update.body && row < 8U)
        g_render_update.row_mask |= (uint8_t)(1U << row);
}

static uint8_t render_pending(void)
{
    return g_render_update.full || g_render_update.info ||
           g_render_update.body || g_render_update.row_mask;
}

static uint8_t runtime_active(micropython_runtime_state_t state)
{
    return state == MICROPYTHON_RUNTIME_STARTING ||
           state == MICROPYTHON_RUNTIME_RUNNING ||
           state == MICROPYTHON_RUNTIME_STOPPING;
}

static void set_status(const char *format, ...)
{
    va_list arguments;
    char next[MICROPYTHON_STATUS_COLUMNS + 1U];

    if(!format || !format[0])
        next[0] = '\0';
    else
    {
        va_start(arguments, format);
        vsnprintf(next, sizeof(next), format, arguments);
        va_end(arguments);
    }
    if(strcmp(g_ui.status, next) == 0)
        return;
    snprintf(g_ui.status, sizeof(g_ui.status), "%s", next);
    request_info_render();
}

static uint8_t log_last_index(void)
{
    return (uint8_t)((g_log_start + g_log_count - 1U) %
                     MICROPYTHON_LOG_HISTORY_LINES);
}

static void log_reset(void)
{
    memset(g_logs, 0, sizeof(g_logs));
    g_log_start = 0U;
    g_log_count = 1U;
    g_ui.log_follow = 1U;
    g_ui.log_top = 0U;
    g_console_live_dirty = 1U;
}

static void log_new_line(void)
{
    uint8_t index;

    if(g_log_count < MICROPYTHON_LOG_HISTORY_LINES)
    {
        index = (uint8_t)((g_log_start + g_log_count) %
                          MICROPYTHON_LOG_HISTORY_LINES);
        g_log_count++;
    }
    else
    {
        g_log_start = (uint8_t)((g_log_start + 1U) %
                                MICROPYTHON_LOG_HISTORY_LINES);
        index = log_last_index();
        if(!g_ui.log_follow && g_ui.log_top)
            g_ui.log_top--;
    }
    memset(g_logs[index], 0, sizeof(g_logs[index]));
}

static void log_bytes(const char *data, size_t size)
{
    for(size_t i = 0U; i < size; i++)
    {
        unsigned char c = (unsigned char)data[i];
        char *line;
        size_t used;

        if(c == '\r')
            continue;
        if(c == '\n')
        {
            log_new_line();
            continue;
        }
        line = g_logs[log_last_index()];
        used = strlen(line);
        if(used >= MICROPYTHON_LOG_COLUMNS)
        {
            log_new_line();
            line = g_logs[log_last_index()];
            used = 0U;
        }
        line[used++] = c >= 0x20U && c < 0x7FU ? (char)c : '?';
        line[used] = '\0';
    }
    g_console_live_dirty = 1U;
}

static void log_message(const char *message)
{
    if(g_logs[log_last_index()][0])
        log_new_line();
    log_bytes(message, strlen(message));
    log_new_line();
}

static uint32_t log_effective_count(void)
{
    if(g_log_count == 1U && !g_logs[g_log_start][0])
        return 0U;
    if(g_log_count > 1U && !g_logs[log_last_index()][0])
        return g_log_count - 1U;
    return g_log_count;
}

static const char *log_at(uint32_t logical_index)
{
    uint8_t index = (uint8_t)((g_log_start + logical_index) %
                              MICROPYTHON_LOG_HISTORY_LINES);
    return g_logs[index];
}

static void prepare_log_view(void)
{
    uint32_t total = log_effective_count();
    uint32_t maximum_top = total > MICROPYTHON_LOG_VISIBLE_LINES ?
                           total - MICROPYTHON_LOG_VISIBLE_LINES : 0U;

    if(g_ui.log_follow)
        g_ui.log_top = maximum_top;
    else if(g_ui.log_top > maximum_top)
        g_ui.log_top = maximum_top;
    g_ui.log_total = total;
    g_ui.log_count = (uint8_t)(total - g_ui.log_top);
    if(g_ui.log_count > MICROPYTHON_LOG_VISIBLE_LINES)
        g_ui.log_count = MICROPYTHON_LOG_VISIBLE_LINES;
    memset(g_ui.log_lines, 0, sizeof(g_ui.log_lines));
    for(uint8_t row = 0U; row < g_ui.log_count; row++)
        snprintf(g_ui.log_lines[row], sizeof(g_ui.log_lines[row]), "%s",
                 log_at(g_ui.log_top + row));
}

static uint8_t python_file_name(const char *name)
{
    size_t length = name ? strlen(name) : 0U;

    return length > 3U && name[length - 3U] == '.' &&
           (name[length - 2U] == 'p' || name[length - 2U] == 'P') &&
           (name[length - 1U] == 'y' || name[length - 1U] == 'Y');
}

static void refresh_startup(void)
{
    g_ui.startup[0] = '\0';
    if(userfs_get_startup(g_ui.startup, sizeof(g_ui.startup)) != USERFS_OK)
        g_ui.startup[0] = '\0';
}

static uint8_t count_file(const char *name, uint32_t size, void *context)
{
    file_count_context_t *scan = (file_count_context_t *)context;
    (void)size;

    if(!python_file_name(name))
        return 1U;
    if(scan->wanted_name && scan->wanted_name[0] &&
       strcmp(scan->wanted_name, name) == 0)
    {
        scan->found = 1U;
        scan->found_index = scan->count;
    }
    scan->count++;
    return 1U;
}

static uint8_t load_file_row(const char *name, uint32_t size, void *context)
{
    file_page_context_t *page = (file_page_context_t *)context;

    if(!python_file_name(name))
        return 1U;
    if(page->index >= g_ui.list_top &&
       page->index < g_ui.list_top + MICROPYTHON_LIST_VISIBLE_ROWS)
    {
        uint8_t row = (uint8_t)(page->index - g_ui.list_top);
        uint8_t startup = g_ui.startup[0] &&
                          strcmp(g_ui.startup, name) == 0;

        if(strcmp(g_ui.files[row].name, name) != 0 ||
           g_ui.files[row].size != size ||
           g_ui.files[row].startup != startup)
            page->changed = 1U;
        snprintf(g_ui.files[row].name, sizeof(g_ui.files[row].name), "%s", name);
        g_ui.files[row].size = size;
        g_ui.files[row].startup = startup;
        if(row + 1U > g_ui.visible_count)
            g_ui.visible_count = row + 1U;
    }
    if(page->index == g_ui.selected_index)
    {
        snprintf(g_ui.selected_name, sizeof(g_ui.selected_name), "%s", name);
        g_ui.selected_size = size;
    }
    page->index++;
    return 1U;
}

static uint8_t refresh_file_list(uint8_t preserve_name, uint8_t mark_changes)
{
    file_count_context_t scan;
    file_page_context_t page = {0U};
    userfs_result_t result;
    char wanted[USERFS_NAME_MAX + 1U];
    char old_startup[USERFS_NAME_MAX + 1U];
    char old_selected[USERFS_NAME_MAX + 1U];
    uint32_t old_file_count = g_ui.file_count;
    uint32_t old_selected_index = g_ui.selected_index;
    uint32_t old_list_top = g_ui.list_top;
    uint32_t old_selected_size = g_ui.selected_size;
    uint8_t old_visible_count = g_ui.visible_count;
    uint8_t changed;

    snprintf(wanted, sizeof(wanted), "%s",
             preserve_name ? g_ui.selected_name : "");
    snprintf(old_startup, sizeof(old_startup), "%s", g_ui.startup);
    snprintf(old_selected, sizeof(old_selected), "%s", g_ui.selected_name);
    memset(&scan, 0, sizeof(scan));
    scan.wanted_name = wanted;
    refresh_startup();
    result = userfs_list(count_file, &scan);
    if(result != USERFS_OK)
    {
        set_status("LIST %s", userfs_result_name(result));
        return 0U;
    }
    g_ui.file_count = scan.count;
    if(!scan.count)
    {
        g_ui.selected_index = 0U;
        g_ui.list_top = 0U;
        g_ui.selected_name[0] = '\0';
        g_ui.selected_size = 0U;
        g_ui.visible_count = 0U;
        page.changed = old_visible_count != 0U;
        for(uint8_t row = 0U; row < MICROPYTHON_LIST_VISIBLE_ROWS; row++)
            page.changed |= g_ui.files[row].name[0] != '\0';
        memset(g_ui.files, 0, sizeof(g_ui.files));
        changed = page.changed || old_file_count != 0U ||
                  old_selected_index != 0U || old_list_top != 0U ||
                  old_selected_size != 0U || old_selected[0] != '\0' ||
                  strcmp(old_startup, g_ui.startup) != 0;
        if(mark_changes && changed)
        {
            request_info_render();
            request_body_render();
        }
        return 1U;
    }
    if(preserve_name && scan.found)
        g_ui.selected_index = scan.found_index;
    else if(g_ui.selected_index >= scan.count)
        g_ui.selected_index = scan.count - 1U;
    if(g_ui.selected_index < g_ui.list_top)
        g_ui.list_top = g_ui.selected_index;
    else if(g_ui.selected_index >=
            g_ui.list_top + MICROPYTHON_LIST_VISIBLE_ROWS)
        g_ui.list_top = g_ui.selected_index -
                        MICROPYTHON_LIST_VISIBLE_ROWS + 1U;
    if(g_ui.list_top + MICROPYTHON_LIST_VISIBLE_ROWS > scan.count)
        g_ui.list_top = scan.count > MICROPYTHON_LIST_VISIBLE_ROWS ?
                        scan.count - MICROPYTHON_LIST_VISIBLE_ROWS : 0U;
    g_ui.visible_count = 0U;
    result = userfs_list(load_file_row, &page);
    if(result != USERFS_OK)
    {
        set_status("LIST %s", userfs_result_name(result));
        return 0U;
    }
    for(uint8_t row = g_ui.visible_count;
        row < MICROPYTHON_LIST_VISIBLE_ROWS; row++)
    {
        if(g_ui.files[row].name[0] || g_ui.files[row].size ||
           g_ui.files[row].startup)
            page.changed = 1U;
        memset(&g_ui.files[row], 0, sizeof(g_ui.files[row]));
    }
    if(strncmp(g_ui.status, "LIST ", 5U) == 0)
        set_status("");
    changed = page.changed || old_file_count != g_ui.file_count ||
              old_selected_index != g_ui.selected_index ||
              old_list_top != g_ui.list_top ||
              old_selected_size != g_ui.selected_size ||
              old_visible_count != g_ui.visible_count ||
              strcmp(old_selected, g_ui.selected_name) != 0 ||
              strcmp(old_startup, g_ui.startup) != 0;
    if(mark_changes && changed)
    {
        request_info_render();
        request_body_render();
    }
    return 1U;
}

static int preview_reader_get(preview_reader_t *reader, uint8_t *value)
{
    size_t read_size;

    if(reader->cursor >= reader->size)
        return 0;
    if(reader->data_index >= reader->data_size)
    {
        reader->result = userfs_read(reader->name, reader->cursor,
                                     reader->data, sizeof(reader->data),
                                     &read_size);
        if(reader->result != USERFS_OK)
            return -1;
        reader->data_size = read_size;
        reader->data_index = 0U;
        if(!read_size)
        {
            reader->result = USERFS_ERROR_IO;
            return -1;
        }
    }
    *value = reader->data[reader->data_index++];
    reader->cursor++;
    return 1;
}

static void preview_reader_unget(preview_reader_t *reader)
{
    if(reader->data_index && reader->cursor)
    {
        reader->data_index--;
        reader->cursor--;
    }
}

static int preview_read_line(preview_reader_t *reader, char *output)
{
    uint32_t visual_column = 0U;
    uint8_t truncated = 0U;
    uint8_t consumed = 0U;

    output[0] = '\0';
    while(reader->cursor < reader->size)
    {
        uint8_t value;
        int read_result = preview_reader_get(reader, &value);

        if(read_result < 0)
            return -1;
        if(!read_result)
            break;
        consumed = 1U;
        if(value == '\n')
            break;
        if(value == '\r')
        {
            uint8_t next;
            read_result = preview_reader_get(reader, &next);
            if(read_result > 0 && next != '\n')
                preview_reader_unget(reader);
            else if(read_result < 0)
                return -1;
            break;
        }
        if(value == '\t')
        {
            uint8_t spaces = (uint8_t)(4U - (visual_column % 4U));
            while(spaces--)
            {
                if(visual_column < MICROPYTHON_PREVIEW_COLUMNS)
                {
                    output[visual_column] = ' ';
                    output[visual_column + 1U] = '\0';
                }
                else
                    truncated = 1U;
                visual_column++;
            }
            continue;
        }
        if(visual_column < MICROPYTHON_PREVIEW_COLUMNS)
        {
            output[visual_column] = value >= 0x20U && value < 0x7FU ?
                                    (char)value : '?';
            output[visual_column + 1U] = '\0';
        }
        else
            truncated = 1U;
        visual_column++;
    }
    if(truncated && MICROPYTHON_PREVIEW_COLUMNS)
    {
        output[MICROPYTHON_PREVIEW_COLUMNS - 1U] = '>';
        output[MICROPYTHON_PREVIEW_COLUMNS] = '\0';
    }
    return consumed ? 1 : 0;
}

static uint8_t preview_load(uint32_t offset, uint32_t line_number)
{
    preview_reader_t reader;
    userfs_result_t result;

    result = userfs_stat(g_ui.selected_name, &g_preview_size);
    if(result != USERFS_OK)
    {
        set_status("VIEW %s", userfs_result_name(result));
        g_ui.preview_count = 0U;
        return 0U;
    }
    memset(&reader, 0, sizeof(reader));
    reader.name = g_ui.selected_name;
    reader.size = g_preview_size;
    reader.cursor = offset;
    reader.result = USERFS_OK;
    memset(g_ui.preview, 0, sizeof(g_ui.preview));
    memset(g_ui.preview_line_number, 0, sizeof(g_ui.preview_line_number));
    g_ui.preview_count = 0U;
    g_ui.preview_line = line_number;
    g_preview_offset = offset;
    if(!g_preview_size)
    {
        snprintf(g_ui.preview[0], sizeof(g_ui.preview[0]), "(empty)");
        g_ui.preview_line_number[0] = 1U;
        g_ui.preview_count = 1U;
        g_ui.preview_has_previous = 0U;
        g_ui.preview_has_next = 0U;
        g_preview_next_offset = 0U;
        g_preview_next_line = 1U;
        set_status("");
        return 1U;
    }
    for(uint8_t row = 0U; row < MICROPYTHON_PREVIEW_LINES; row++)
    {
        int line_result;

        if(reader.cursor >= reader.size)
            break;
        g_ui.preview_line_number[row] = line_number++;
        line_result = preview_read_line(&reader, g_ui.preview[row]);
        if(line_result < 0)
        {
            set_status("VIEW %s", userfs_result_name(reader.result));
            g_ui.preview_count = row;
            return 0U;
        }
        if(!line_result)
            break;
        g_ui.preview_count++;
    }
    g_preview_next_offset = reader.cursor;
    g_preview_next_line = line_number;
    g_ui.preview_has_previous = offset != 0U;
    g_ui.preview_has_next = reader.cursor < reader.size;
    set_status("");
    return 1U;
}

static void preview_previous_page(void)
{
    uint8_t data[MICROPYTHON_PREVIEW_READ_CHUNK];
    uint32_t position = g_preview_offset;
    uint32_t wanted = g_ui.preview_line > MICROPYTHON_PREVIEW_LINES ?
                      MICROPYTHON_PREVIEW_LINES : g_ui.preview_line - 1U;
    uint32_t found = 0U;
    uint32_t separators = wanted + 1U;
    uint8_t next = 0U;
    uint8_t next_valid = 0U;

    if(!position || !wanted)
        return;
    while(position && found < separators)
    {
        uint32_t start = position > sizeof(data) ?
                         position - sizeof(data) : 0U;
        size_t read_size = 0U;
        userfs_result_t result = userfs_read(
            g_ui.selected_name, start, data, position - start, &read_size);

        if(result != USERFS_OK || read_size != position - start)
        {
            if(result != USERFS_OK)
                set_status("VIEW %s", userfs_result_name(result));
            else
                set_status("VIEW short-read");
            return;
        }
        for(size_t index = read_size; index > 0U; index--)
        {
            uint8_t value = data[index - 1U];
            if(value == '\n' || (value == '\r' &&
                                  (!next_valid || next != '\n')))
            {
                found++;
                if(found == separators)
                {
                    (void)preview_load(start + (uint32_t)index,
                                       g_ui.preview_line - wanted);
                    return;
                }
            }
            next = value;
            next_valid = 1U;
        }
        position = start;
    }
    (void)preview_load(0U, 1U);
}

static void render_if_needed(uint8_t force)
{
    micropython_runtime_status_t runtime;
    userfs_status_t filesystem;

    if(!g_screen_active)
        return;
    if(force)
        request_full_render();
    if(!render_pending())
        return;
    micropython_runtime_get_status(&runtime);
    userfs_get_status(&filesystem);
    if(g_ui.mode == MICROPYTHON_UI_CONSOLE)
        prepare_log_view();
    micropython_view_render(&g_ui, &runtime, &filesystem, &g_render_update);
    if(g_ui.mode == MICROPYTHON_UI_CONSOLE &&
       (g_render_update.full || g_render_update.body))
    {
        g_console_live_dirty = 0U;
        g_console_render_ticks = 0U;
    }
    memset(&g_render_update, 0, sizeof(g_render_update));
}

static void show_console(void)
{
    uint8_t mode_changed = g_ui.mode != MICROPYTHON_UI_CONSOLE;

    g_ui.mode = MICROPYTHON_UI_CONSOLE;
    g_ui.log_follow = 1U;
    if(mode_changed)
        request_full_render();
    else
    {
        request_info_render();
        request_body_render();
    }
}

static uint8_t run_debug_default(void)
{
    micropython_program_result_t result;

    refresh_startup();
    log_reset();
    if(g_ui.startup[0])
    {
        result = micropython_program_run_file(
            g_ui.startup, MICROPYTHON_RUNTIME_DEFAULT_LIMIT_MS, NULL);
        if(result != MICROPYTHON_PROGRAM_OK)
        {
            log_message("STARTUP RUN FAILED");
            return 0U;
        }
        log_message("RUN STARTUP");
    }
    else if(!micropython_runtime_start(
                g_smoke_script, sizeof(g_smoke_script) - 1U,
                MICROPYTHON_RUNTIME_DEFAULT_LIMIT_MS))
    {
        log_message("VM START FAILED/BUSY");
        return 0U;
    }
    else
        log_message("RUN SMOKE");
    if(g_screen_active)
        show_console();
    return 1U;
}

static void run_selected_file(void)
{
    micropython_program_result_t result;
    uint32_t run_id = 0U;
    char message[USERFS_NAME_MAX + 8U];

    if(!g_ui.selected_name[0])
        return;
    log_reset();
    snprintf(message, sizeof(message), "RUN %s", g_ui.selected_name);
    log_message(message);
    result = micropython_program_run_file(
        g_ui.selected_name, MICROPYTHON_RUNTIME_DEFAULT_LIMIT_MS, &run_id);
    if(result != MICROPYTHON_PROGRAM_OK)
    {
        set_status("RUN %s", micropython_program_result_name(result));
        log_message(g_ui.status);
        return;
    }
    g_observed_run_id = run_id;
    set_status("");
    show_console();
}

static void navigate_files(int8_t delta)
{
    uint32_t previous_index;
    uint32_t previous_top;

    if(!g_ui.file_count)
        return;
    previous_index = g_ui.selected_index;
    previous_top = g_ui.list_top;
    if(delta < 0)
        g_ui.selected_index = g_ui.selected_index ?
                              g_ui.selected_index - 1U : g_ui.file_count - 1U;
    else
        g_ui.selected_index = (g_ui.selected_index + 1U) % g_ui.file_count;
    set_status("");
    (void)refresh_file_list(0U, 0U);
    if(previous_top != g_ui.list_top)
        request_body_render();
    else
    {
        request_row_render((uint8_t)(previous_index - previous_top));
        request_row_render((uint8_t)(g_ui.selected_index - g_ui.list_top));
    }
}

static void navigate_actions(int8_t delta)
{
    uint8_t previous = g_ui.action_index;

    if(delta < 0)
        g_ui.action_index = g_ui.action_index ?
                            g_ui.action_index - 1U :
                            MICROPYTHON_ACTION_COUNT - 1U;
    else
        g_ui.action_index = (uint8_t)((g_ui.action_index + 1U) %
                                      MICROPYTHON_ACTION_COUNT);
    set_status("");
    request_row_render(previous);
    request_row_render(g_ui.action_index);
}

static void execute_action(void)
{
    userfs_result_t result;

    switch((micropython_action_t)g_ui.action_index)
    {
    case MICROPYTHON_ACTION_RUN:
        run_selected_file();
        break;
    case MICROPYTHON_ACTION_VIEW:
        g_ui.mode = MICROPYTHON_UI_PREVIEW;
        (void)preview_load(0U, 1U);
        request_full_render();
        break;
    case MICROPYTHON_ACTION_LOGS:
        show_console();
        break;
    case MICROPYTHON_ACTION_STARTUP:
        if(g_ui.startup[0] &&
           strcmp(g_ui.startup, g_ui.selected_name) == 0)
            result = userfs_set_startup("");
        else
            result = userfs_set_startup(g_ui.selected_name);
        if(result == USERFS_OK)
        {
            (void)refresh_file_list(1U, 0U);
            set_status(g_ui.startup[0] ? "STARTUP SET" : "STARTUP CLEARED");
            request_row_render(MICROPYTHON_ACTION_STARTUP);
        }
        else
            set_status("STARTUP %s", userfs_result_name(result));
        break;
    case MICROPYTHON_ACTION_DELETE:
        g_ui.mode = MICROPYTHON_UI_DELETE_CONFIRM;
        set_status("");
        request_full_render();
        break;
    default:
        break;
    }
}

static void delete_selected(void)
{
    char deleted[USERFS_NAME_MAX + 1U];
    userfs_result_t result;

    snprintf(deleted, sizeof(deleted), "%s", g_ui.selected_name);
    result = userfs_remove(deleted);
    if(result != USERFS_OK)
    {
        set_status("DELETE %s", userfs_result_name(result));
        return;
    }
    g_ui.mode = MICROPYTHON_UI_LIST;
    (void)refresh_file_list(0U, 0U);
    set_status("DELETED %s", deleted);
    request_full_render();
}

static void request_runtime_stop(void)
{
    micropython_runtime_status_t status;

    micropython_runtime_get_status(&status);
    if(status.state != MICROPYTHON_RUNTIME_STOPPING &&
       micropython_runtime_request_stop())
        log_message("STOP REQUESTED");
    if(g_ui.mode == MICROPYTHON_UI_CONSOLE)
    {
        request_info_render();
        request_body_render();
    }
}

void micropython_enter(const hk_input_snapshot_t *input)
{
    userfs_result_t mount_result;
    micropython_runtime_status_t runtime;
    uint8_t watchdog_recovery = boot_internal_watchdog_reset_detected();

    (void)input;
    hk_screen_set(HK_MICROPYTHON_SCREEN);
    g_screen_active = 1U;
    memset(&g_render_update, 0, sizeof(g_render_update));
    g_render_divider = 0U;
    g_console_render_ticks = 0U;
    g_console_live_dirty = 0U;
    g_list_refresh_ticks = 0U;
    memset(&g_ui, 0, sizeof(g_ui));
    g_ui.mode = MICROPYTHON_UI_LIST;
    g_ui.log_follow = 1U;
    log_reset();
    mount_result = userfs_mount();
    if(mount_result != USERFS_OK)
        set_status("USERFS %s", userfs_result_name(mount_result));
    else
    {
        refresh_startup();
        snprintf(g_ui.selected_name, sizeof(g_ui.selected_name), "%s",
                 g_ui.startup);
        (void)refresh_file_list(1U, 0U);
    }
    micropython_runtime_get_status(&runtime);
    g_observed_run_id = runtime.run_id;
    g_last_state = runtime.state;
    g_last_exit = runtime.exit_reason;
    g_last_dropped = runtime.output_dropped;
    if(runtime_active(runtime.state))
        show_console();
    if(watchdog_recovery)
        set_status("WDT RECOVERY: PREVIOUS RUN RESET");
    render_if_needed(1U);
}

void micropython_exit(void)
{
    micropython_runtime_status_t status;

    g_screen_active = 0U;
    micropython_runtime_get_status(&status);
    if(runtime_active(status.state))
        (void)micropython_runtime_request_stop();
}

void micropython_tick(const hk_input_snapshot_t *input)
{
    (void)input;
    if(g_ui.mode == MICROPYTHON_UI_LIST &&
        ++g_list_refresh_ticks >= MICROPYTHON_LIST_REFRESH_TICKS)
    {
        g_list_refresh_ticks = 0U;
        (void)refresh_file_list(1U, 1U);
    }
    if(g_ui.mode == MICROPYTHON_UI_CONSOLE && g_console_live_dirty)
    {
        if(++g_console_render_ticks >= MICROPYTHON_CONSOLE_RENDER_TICKS)
        {
            g_console_render_ticks = 0U;
            g_console_live_dirty = 0U;
            request_info_render();
            request_body_render();
        }
    }
    else
        g_console_render_ticks = 0U;
    if(++g_render_divider >= MICROPYTHON_RENDER_TICKS)
    {
        g_render_divider = 0U;
        render_if_needed(0U);
    }
}

static void handle_list_buttons(uint32_t pressed,
                                micropython_runtime_state_t runtime_state)
{
    if(pressed & BUTTON_BACK)
    {
        if(runtime_active(runtime_state))
        {
            show_console();
            request_runtime_stop();
        }
        else
            shell_show_menu();
        return;
    }
    if(pressed & BUTTON_LEFT)
        navigate_files(-1);
    else if(pressed & BUTTON_RIGHT)
        navigate_files(1);
    else if((pressed & BUTTON_OK) && g_ui.file_count)
    {
        g_ui.action_index = MICROPYTHON_ACTION_RUN;
        g_ui.mode = MICROPYTHON_UI_ACTIONS;
        set_status("");
        request_full_render();
    }
}

static void handle_action_buttons(uint32_t pressed)
{
    if(pressed & BUTTON_BACK)
    {
        g_ui.mode = MICROPYTHON_UI_LIST;
        set_status("");
        request_full_render();
    }
    else if(pressed & BUTTON_LEFT)
        navigate_actions(-1);
    else if(pressed & BUTTON_RIGHT)
        navigate_actions(1);
    else if(pressed & BUTTON_OK)
        execute_action();
}

static void handle_preview_buttons(uint32_t pressed)
{
    if(pressed & BUTTON_BACK)
    {
        g_ui.mode = MICROPYTHON_UI_ACTIONS;
        set_status("");
        request_full_render();
    }
    else if((pressed & BUTTON_LEFT) && g_ui.preview_has_previous)
    {
        preview_previous_page();
        request_info_render();
        request_body_render();
    }
    else if((pressed & BUTTON_RIGHT) && g_ui.preview_has_next)
    {
        (void)preview_load(g_preview_next_offset, g_preview_next_line);
        request_info_render();
        request_body_render();
    }
}

static void handle_console_buttons(uint32_t pressed,
                                   micropython_runtime_state_t runtime_state)
{
    uint32_t total = log_effective_count();
    uint32_t maximum_top = total > MICROPYTHON_LOG_VISIBLE_LINES ?
                           total - MICROPYTHON_LOG_VISIBLE_LINES : 0U;

    if(pressed & BUTTON_LEFT)
    {
        g_ui.log_follow = 0U;
        g_ui.log_top = g_ui.log_top > MICROPYTHON_LOG_VISIBLE_LINES ?
                       g_ui.log_top - MICROPYTHON_LOG_VISIBLE_LINES : 0U;
        request_info_render();
        request_body_render();
    }
    else if(pressed & BUTTON_RIGHT)
    {
        if(g_ui.log_top + MICROPYTHON_LOG_VISIBLE_LINES >= maximum_top)
        {
            g_ui.log_top = maximum_top;
            g_ui.log_follow = 1U;
        }
        else
            g_ui.log_top += MICROPYTHON_LOG_VISIBLE_LINES;
        request_info_render();
        request_body_render();
    }
    if(pressed & BUTTON_OK)
    {
        if(runtime_active(runtime_state))
            request_runtime_stop();
        return;
    }
    if(pressed & BUTTON_BACK)
    {
        if(runtime_active(runtime_state))
            request_runtime_stop();
        else
        {
            (void)refresh_file_list(1U, 0U);
            g_ui.mode = g_ui.file_count ? MICROPYTHON_UI_ACTIONS :
                                         MICROPYTHON_UI_LIST;
            set_status("");
            request_full_render();
        }
    }
}

void micropython_handle_buttons(const hk_input_snapshot_t *input)
{
    micropython_runtime_status_t status;

    micropython_runtime_get_status(&status);
    switch(g_ui.mode)
    {
    case MICROPYTHON_UI_ACTIONS:
        handle_action_buttons(input->pressed);
        break;
    case MICROPYTHON_UI_PREVIEW:
        handle_preview_buttons(input->pressed);
        break;
    case MICROPYTHON_UI_CONSOLE:
        handle_console_buttons(input->pressed, status.state);
        break;
    case MICROPYTHON_UI_DELETE_CONFIRM:
        if(input->pressed & BUTTON_BACK)
        {
            g_ui.mode = MICROPYTHON_UI_ACTIONS;
            set_status("");
            request_full_render();
        }
        else if(input->pressed & BUTTON_OK)
            delete_selected();
        break;
    default:
        handle_list_buttons(input->pressed, status.state);
        break;
    }
}

void micropython_background_tick(const hk_input_snapshot_t *input)
{
    char output[160];
    size_t count;
    micropython_runtime_status_t status;

    (void)input;
    micropython_runtime_poll();
    micropython_runtime_get_status(&status);
    if(status.state != g_last_state || status.exit_reason != g_last_exit)
    {
        g_last_state = status.state;
        g_last_exit = status.exit_reason;
        if(g_screen_active && g_ui.mode == MICROPYTHON_UI_CONSOLE)
        {
            request_info_render();
            request_body_render();
        }
    }
    if(status.run_id != g_observed_run_id)
    {
        g_observed_run_id = status.run_id;
        g_last_dropped = 0U;
        if(g_screen_active && runtime_active(status.state))
            show_console();
    }
    if(status.output_dropped != g_last_dropped)
    {
        if(status.output_dropped > g_last_dropped)
        {
            char message[40];
            snprintf(message, sizeof(message), "[%lu LOG BYTES LOST]",
                     (unsigned long)(status.output_dropped - g_last_dropped));
            log_message(message);
        }
        g_last_dropped = status.output_dropped;
    }
    for(uint8_t pass = 0U; pass < 4U; pass++)
    {
        count = micropython_runtime_read_output(output, sizeof(output));
        if(!count)
            break;
        log_bytes(output, count);
    }
}

void micropython_draw_icon(uint16_t x, uint16_t y,
                           uint16_t color, uint16_t background)
{
    micropython_view_draw_icon(x, y, color, background);
}

static void debug_status(void)
{
    micropython_runtime_status_t runtime;
    userfs_status_t filesystem;
    char line[192];

    refresh_startup();
    micropython_runtime_get_status(&runtime);
    userfs_get_status(&filesystem);
    snprintf(line, sizeof(line),
             "HKMPSTATUS state=%u exit=%u run=%u source=%u pending=%u dropped=%u fs=%u fs_error=%u startup=%s\n",
             (unsigned)runtime.state, (unsigned)runtime.exit_reason,
             (unsigned)runtime.run_id, (unsigned)runtime.source_bytes,
             (unsigned)runtime.output_pending, (unsigned)runtime.output_dropped,
             (unsigned)filesystem.state, (unsigned)filesystem.last_error,
             g_ui.startup[0] ? g_ui.startup : "OFF");
    debug_console_write_text(line);
}

static uint8_t debug_list_item(const char *name, uint32_t size, void *context)
{
    char line[96];
    (void)context;
    snprintf(line, sizeof(line), "HKMPFILE name=%s size=%lu\n", name,
             (unsigned long)size);
    debug_console_write_text(line);
    return 1U;
}

uint8_t micropython_handle_debug_command(const char *command)
{
    if(str_eq_ci(command, "HKMPRUN"))
    {
        debug_console_write_text(run_debug_default() ? "HKMPRUN OK\n" :
                                                      "HKMPRUN ERROR\n");
        return 1U;
    }
    if(str_eq_ci(command, "HKMPTEST"))
    {
        uint8_t started;
        log_reset();
        started = micropython_runtime_start(
            g_stop_test_script, sizeof(g_stop_test_script) - 1U,
            MICROPYTHON_RUNTIME_DEFAULT_LIMIT_MS);
        if(started && g_screen_active)
            show_console();
        debug_console_write_text(started ? "HKMPTEST OK\n" : "HKMPTEST ERROR\n");
        return 1U;
    }
    if(str_eq_ci(command, "HKMPSTOP"))
    {
        debug_console_write_text(micropython_runtime_request_stop() ?
                                 "HKMPSTOP OK\n" : "HKMPSTOP IDLE\n");
        return 1U;
    }
    if(str_eq_ci(command, "HKMPSTATUS"))
    {
        debug_status();
        return 1U;
    }
    if(str_eq_ci(command, "HKMPLOG"))
    {
        uint32_t count = log_effective_count();
        for(uint32_t i = 0U; i < count; i++)
        {
            if(log_at(i)[0])
            {
                debug_console_write_text("HKMPLOG ");
                debug_console_write_text(log_at(i));
                debug_console_write_text("\n");
            }
        }
        return 1U;
    }
    if(str_eq_ci(command, "HKMPLIST"))
    {
        userfs_result_t result = userfs_list(debug_list_item, NULL);
        if(result != USERFS_OK)
        {
            debug_console_write_text("HKMPLIST ERROR ");
            debug_console_write_text(userfs_result_name(result));
            debug_console_write_text("\n");
        }
        else
            debug_console_write_text("HKMPLIST END\n");
        return 1U;
    }
    if(str_eq_ci(command, "HKMPFORMAT-CONFIRM"))
    {
        userfs_result_t result = userfs_format_explicit();
        debug_console_write_text(result == USERFS_OK ? "HKMPFORMAT OK\n" :
                                                       "HKMPFORMAT ERROR\n");
        refresh_startup();
        if(g_screen_active && result == USERFS_OK)
        {
            g_ui.mode = MICROPYTHON_UI_LIST;
            (void)refresh_file_list(0U, 0U);
        }
        if(g_screen_active)
            request_full_render();
        return 1U;
    }
    return 0U;
}

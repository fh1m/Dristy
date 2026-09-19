#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../firmware/src/apps/micropython/micropython_app.h"
#include "../firmware/src/apps/micropython/micropython_view.h"
#include "../firmware/src/config/input_config.h"
#include "../firmware/src/services/debug_console_service.h"
#include "../firmware/src/services/micropython_program.h"
#include "../firmware/src/storage/userfs.h"

typedef struct
{
    char name[USERFS_NAME_MAX + 1U];
    const char *data;
    uint32_t size;
    uint8_t present;
} fake_file_t;

static const char g_startup_source[] =
    "print('one')\r\n"
    "\tprint('two')\n"
    "abcdefghijklmnopqrstuvwxyz-THIS-LINE-IS-LONG\n"
    "line4\nline5\nline6\nline7\nline8\nline9\nline10\nline11\nline12";
static fake_file_t g_files[] = {
    {"notes.txt", "ignored", 7U, 1U},
    {"alpha.py", "print('alpha')\n", 15U, 1U},
    {"startup.py", g_startup_source, sizeof(g_startup_source) - 1U, 1U},
    {"empty.py", "", 0U, 1U},
    {"huge.py", "x", MICROPYTHON_RUNTIME_SOURCE_MAX + 1U, 1U},
};

static micropython_view_state_t g_view;
static micropython_runtime_status_t g_runtime;
static char g_startup[USERFS_NAME_MAX + 1U] = "startup.py";
static char g_run_name[USERFS_NAME_MAX + 1U];
static char g_output[1024];
static size_t g_output_size;
static unsigned g_run_count;
static unsigned g_stop_count;
static unsigned g_menu_count;
static unsigned g_failures;
static uint8_t g_list_busy;
static unsigned g_render_count;
static micropython_view_update_t g_last_update;

static void check(int condition, const char *message)
{
    if(!condition)
    {
        fprintf(stderr, "FAIL: %s\n", message);
        g_failures++;
    }
}

static fake_file_t *find_file(const char *name)
{
    for(size_t i = 0U; i < sizeof(g_files) / sizeof(g_files[0]); i++)
        if(g_files[i].present && strcmp(g_files[i].name, name) == 0)
            return &g_files[i];
    return NULL;
}

static hk_input_snapshot_t pressed(uint32_t button)
{
    hk_input_snapshot_t input = {button, button, button};
    return input;
}

static void press_button(uint32_t button)
{
    hk_input_snapshot_t input = pressed(button);
    micropython_handle_buttons(&input);
    input.state = 0U;
    input.pressed = 0U;
    micropython_tick(&input);
}

static void render(void)
{
    hk_input_snapshot_t input = {0U, 0U, 0U};
    for(unsigned i = 0U; i < MICROPYTHON_CONSOLE_RENDER_TICKS; i++)
        micropython_tick(&input);
}

static void select_action(uint8_t target)
{
    for(unsigned attempt = 0U;
        g_view.action_index != target && attempt < MICROPYTHON_ACTION_COUNT + 1U;
        attempt++)
    {
        press_button(BUTTON_RIGHT);
        render();
    }
    check(g_view.action_index == target, "action navigation reaches target");
}

void hk_screen_set(screen_t screen)
{
    check(screen == HK_MICROPYTHON_SCREEN, "MicroPython screen selected");
}

void shell_show_menu(void)
{
    g_menu_count++;
}

uint8_t boot_internal_watchdog_reset_detected(void)
{
    return 0U;
}

void debug_console_write_text(const char *text)
{
    (void)text;
}

uint8_t str_eq_ci(const char *a, const char *b)
{
    while(*a && *b)
    {
        char ca = *a >= 'a' && *a <= 'z' ? (char)(*a - 'a' + 'A') : *a;
        char cb = *b >= 'a' && *b <= 'z' ? (char)(*b - 'a' + 'A') : *b;
        if(ca != cb)
            return 0U;
        a++;
        b++;
    }
    return *a == *b;
}

void micropython_view_render(const micropython_view_state_t *ui,
                             const micropython_runtime_status_t *runtime,
                             const userfs_status_t *filesystem,
                             const micropython_view_update_t *update)
{
    (void)runtime;
    (void)filesystem;
    g_view = *ui;
    g_last_update = *update;
    g_render_count++;
}

void micropython_view_draw_icon(uint16_t x, uint16_t y,
                                uint16_t color, uint16_t background)
{
    (void)x;
    (void)y;
    (void)color;
    (void)background;
}

userfs_result_t userfs_mount(void)
{
    return USERFS_OK;
}

void userfs_get_status(userfs_status_t *status)
{
    memset(status, 0, sizeof(*status));
    status->state = USERFS_STATE_MOUNTED;
    status->total_bytes = 2U * 1024U * 1024U;
    status->used_bytes = 64U * 1024U;
}

userfs_result_t userfs_list(userfs_list_callback_t callback, void *context)
{
    if(g_list_busy)
        return USERFS_ERROR_BUSY;
    for(size_t i = 0U; i < sizeof(g_files) / sizeof(g_files[0]); i++)
        if(g_files[i].present &&
           !callback(g_files[i].name, g_files[i].size, context))
            break;
    return USERFS_OK;
}

userfs_result_t userfs_stat(const char *name, uint32_t *size)
{
    fake_file_t *file = find_file(name);
    if(!file)
        return USERFS_ERROR_NOT_FOUND;
    *size = file->size;
    return USERFS_OK;
}

userfs_result_t userfs_read(const char *name, uint32_t offset,
                            uint8_t *data, size_t capacity, size_t *read_size)
{
    fake_file_t *file = find_file(name);
    size_t available;
    size_t count;

    *read_size = 0U;
    if(!file)
        return USERFS_ERROR_NOT_FOUND;
    if(offset >= file->size || !file->data)
        return USERFS_OK;
    available = file->size - offset;
    count = capacity < available ? capacity : available;
    if(offset + count > strlen(file->data))
        count = offset < strlen(file->data) ? strlen(file->data) - offset : 0U;
    memcpy(data, file->data + offset, count);
    *read_size = count;
    return USERFS_OK;
}

userfs_result_t userfs_remove(const char *name)
{
    fake_file_t *file = find_file(name);
    if(!file)
        return USERFS_ERROR_NOT_FOUND;
    file->present = 0U;
    if(strcmp(g_startup, name) == 0)
        g_startup[0] = '\0';
    return USERFS_OK;
}

userfs_result_t userfs_set_startup(const char *name)
{
    if(name && name[0] && !find_file(name))
        return USERFS_ERROR_NOT_FOUND;
    snprintf(g_startup, sizeof(g_startup), "%s", name ? name : "");
    return USERFS_OK;
}

userfs_result_t userfs_get_startup(char *name, size_t capacity)
{
    if(!g_startup[0])
        return USERFS_ERROR_NOT_FOUND;
    snprintf(name, capacity, "%s", g_startup);
    return USERFS_OK;
}

userfs_result_t userfs_format_explicit(void)
{
    return USERFS_OK;
}

const char *userfs_result_name(userfs_result_t result)
{
    switch(result)
    {
    case USERFS_OK: return "ok";
    case USERFS_ERROR_BUSY: return "busy";
    case USERFS_ERROR_NOT_FOUND: return "not-found";
    default: return "error";
    }
}

micropython_program_result_t micropython_program_run_file(
    const char *name, uint32_t time_limit_ms, uint32_t *run_id)
{
    fake_file_t *file = find_file(name);
    (void)time_limit_ms;
    if(!file)
        return MICROPYTHON_PROGRAM_NOT_FOUND;
    if(!file->size || file->size > MICROPYTHON_RUNTIME_SOURCE_MAX)
        return MICROPYTHON_PROGRAM_TOO_LARGE;
    snprintf(g_run_name, sizeof(g_run_name), "%s", name);
    g_run_count++;
    g_runtime.state = MICROPYTHON_RUNTIME_RUNNING;
    g_runtime.run_id++;
    if(run_id)
        *run_id = g_runtime.run_id;
    return MICROPYTHON_PROGRAM_OK;
}

const char *micropython_program_result_name(micropython_program_result_t result)
{
    return result == MICROPYTHON_PROGRAM_TOO_LARGE ? "too-large" :
           result == MICROPYTHON_PROGRAM_OK ? "ok" : "error";
}

uint8_t micropython_runtime_start(const char *source, size_t length,
                                  uint32_t time_limit_ms)
{
    (void)source;
    (void)length;
    (void)time_limit_ms;
    g_runtime.state = MICROPYTHON_RUNTIME_RUNNING;
    g_runtime.run_id++;
    return 1U;
}

uint8_t micropython_runtime_request_stop(void)
{
    if(g_runtime.state != MICROPYTHON_RUNTIME_RUNNING &&
       g_runtime.state != MICROPYTHON_RUNTIME_STARTING)
        return 0U;
    g_runtime.state = MICROPYTHON_RUNTIME_STOPPING;
    g_stop_count++;
    return 1U;
}

void micropython_runtime_poll(void)
{
}

void micropython_runtime_get_status(micropython_runtime_status_t *status)
{
    *status = g_runtime;
}

size_t micropython_runtime_read_output(char *destination, size_t capacity)
{
    size_t count = capacity < g_output_size ? capacity : g_output_size;
    memcpy(destination, g_output, count);
    memmove(g_output, g_output + count, g_output_size - count);
    g_output_size -= count;
    return count;
}

static void queue_output(const char *text)
{
    size_t length = strlen(text);
    check(g_output_size + length <= sizeof(g_output), "output fixture capacity");
    memcpy(g_output + g_output_size, text, length);
    g_output_size += length;
}

int main(void)
{
    hk_input_snapshot_t input = {0U, 0U, 0U};

    memset(&g_runtime, 0, sizeof(g_runtime));
    micropython_enter(&input);
    check(g_view.mode == MICROPYTHON_UI_LIST, "enter opens script list");
    check(g_view.file_count == 4U, "only .py files are listed");
    check(strcmp(g_view.selected_name, "startup.py") == 0,
          "startup file is preselected");
    check(g_run_count == 0U && g_runtime.state == MICROPYTHON_RUNTIME_STOPPED,
          "enter never automatically runs startup");
    {
        unsigned renders = g_render_count;
        for(unsigned i = 0U; i < MICROPYTHON_LIST_REFRESH_TICKS; i++)
            micropython_tick(&input);
        check(g_render_count == renders,
              "unchanged periodic USERFS refresh performs no rendering");
    }
    press_button(BUTTON_LEFT);
    render();
    check(!g_last_update.full && !g_last_update.body &&
          g_last_update.row_mask == 0x03U,
          "same-page list navigation redraws exactly old and new rows");
    press_button(BUTTON_RIGHT);
    render();
    check(strcmp(g_view.selected_name, "startup.py") == 0,
          "dirty-row navigation test restores startup selection");

    press_button(BUTTON_OK);
    render();
    check(g_view.mode == MICROPYTHON_UI_ACTIONS, "OK opens action menu");
    select_action(MICROPYTHON_ACTION_VIEW);
    press_button(BUTTON_OK);
    render();
    check(g_view.mode == MICROPYTHON_UI_PREVIEW, "view action opens source");
    check(g_view.preview_line == 1U && g_view.preview_count == 9U,
          "first source page has numbered lines");
    check(strcmp(g_view.preview[1], "    print('two')") == 0,
          "preview expands tabs and accepts CRLF");
    check(g_view.preview[2][MICROPYTHON_PREVIEW_COLUMNS - 1U] == '>',
          "preview marks truncated long lines");
    press_button(BUTTON_RIGHT);
    render();
    check(g_view.preview_line == 10U, "preview advances by one page");
    press_button(BUTTON_LEFT);
    render();
    check(g_view.preview_line == 1U, "preview returns to prior page");
    press_button(BUTTON_BACK);
    render();

    select_action(MICROPYTHON_ACTION_STARTUP);
    press_button(BUTTON_OK);
    render();
    check(!g_startup[0], "startup action clears selected startup");
    press_button(BUTTON_OK);
    render();
    check(strcmp(g_startup, "startup.py") == 0,
          "startup action sets selected startup");

    select_action(MICROPYTHON_ACTION_DELETE);
    press_button(BUTTON_OK);
    render();
    check(g_view.mode == MICROPYTHON_UI_DELETE_CONFIRM,
          "delete requires confirmation");
    press_button(BUTTON_BACK);
    render();
    check(find_file("startup.py") != NULL, "BACK cancels deletion");
    press_button(BUTTON_OK);
    press_button(BUTTON_OK);
    render();
    check(find_file("startup.py") == NULL && !g_startup[0],
          "confirmed delete removes file and clears startup");
    check(g_view.mode == MICROPYTHON_UI_LIST && g_view.file_count == 3U,
          "delete returns to refreshed list");

    press_button(BUTTON_LEFT);
    render();
    check(strcmp(g_view.selected_name, "alpha.py") == 0,
          "list navigation selects runnable script");
    press_button(BUTTON_OK);
    render();
    press_button(BUTTON_OK);
    render();
    check(g_view.mode == MICROPYTHON_UI_CONSOLE && g_run_count == 1U,
          "RUN starts selected file and opens console");
    check(strcmp(g_run_name, g_view.selected_name) == 0,
          "RUN uses exact selected file");
    micropython_background_tick(&input);
    render();
    queue_output("one\ntwo\nthree\n");
    {
        unsigned renders = g_render_count;
        micropython_background_tick(&input);
        for(unsigned i = 0U; i + 1U < MICROPYTHON_CONSOLE_RENDER_TICKS; i++)
            micropython_tick(&input);
        check(g_render_count == renders,
              "live console output is coalesced for 100ms");
        for(unsigned i = 0U; i < MICROPYTHON_RENDER_TICKS; i++)
            micropython_tick(&input);
        check(g_render_count == renders + 1U && g_last_update.body &&
              g_last_update.info && !g_last_update.full,
              "coalesced console output redraws body and info once");
    }
    check(g_view.log_total >= 4U, "console receives live output");
    press_button(BUTTON_BACK);
    render();
    check(g_view.mode == MICROPYTHON_UI_CONSOLE && g_stop_count == 1U,
          "BACK requests stop and stays in console");
    g_runtime.state = MICROPYTHON_RUNTIME_FINISHED;
    g_runtime.exit_reason = MICROPYTHON_EXIT_REQUESTED;
    micropython_background_tick(&input);
    press_button(BUTTON_BACK);
    render();
    check(g_view.mode == MICROPYTHON_UI_ACTIONS,
          "BACK returns to actions only after cleanup");

    for(unsigned i = 0U; i < 24U; i++)
    {
        char line[16];
        snprintf(line, sizeof(line), "log%02u\n", i);
        queue_output(line);
    }
    micropython_background_tick(&input);
    select_action(MICROPYTHON_ACTION_LOGS);
    press_button(BUTTON_OK);
    render();
    check(g_view.log_follow && g_view.log_top > 0U,
          "console follows the tail of long output");
    {
        uint32_t tail = g_view.log_top;
        press_button(BUTTON_LEFT);
        render();
        check(!g_view.log_follow && g_view.log_top < tail,
              "LEFT scrolls completed log history");
    }

    press_button(BUTTON_BACK);
    render();
    press_button(BUTTON_BACK);
    render();
    g_list_busy = 1U;
    for(unsigned i = 0U; i < MICROPYTHON_LIST_REFRESH_TICKS; i++)
        micropython_tick(&input);
    render();
    check(strstr(g_view.status, "busy") != NULL,
          "list busy error is visible without losing navigation state");
    g_list_busy = 0U;

    snprintf(g_files[0].name, sizeof(g_files[0].name), "new.py");
    g_files[0].data = "print('new')\n";
    g_files[0].size = 13U;
    for(unsigned i = 0U; i < MICROPYTHON_LIST_REFRESH_TICKS; i++)
        micropython_tick(&input);
    render();
    check(strcmp(g_view.selected_name, "alpha.py") == 0 &&
          g_view.file_count == 4U,
          "external list refresh preserves selected file by name");
    check(g_view.status[0] == '\0',
          "successful list refresh clears a transient busy error");

    press_button(BUTTON_RIGHT);
    press_button(BUTTON_RIGHT);
    render();
    check(strcmp(g_view.selected_name, "huge.py") == 0,
          "large script selected");
    press_button(BUTTON_OK);
    press_button(BUTTON_OK);
    render();
    check(g_view.mode == MICROPYTHON_UI_ACTIONS &&
          strstr(g_view.status, "too-large") != NULL,
          "oversized script reports a bounded run error");
    press_button(BUTTON_BACK);
    press_button(BUTTON_LEFT);
    render();
    check(strcmp(g_view.selected_name, "empty.py") == 0,
          "empty script selected");
    press_button(BUTTON_OK);
    render();
    select_action(MICROPYTHON_ACTION_VIEW);
    press_button(BUTTON_OK);
    render();
    check(g_view.preview_count == 1U &&
          strcmp(g_view.preview[0], "(empty)") == 0,
          "empty source has an explicit preview state");

    for(size_t i = 0U; i < sizeof(g_files) / sizeof(g_files[0]); i++)
        if(strstr(g_files[i].name, ".py"))
            g_files[i].present = 0U;
    g_runtime.state = MICROPYTHON_RUNTIME_FINISHED;
    micropython_enter(&input);
    check(g_view.mode == MICROPYTHON_UI_LIST && g_view.file_count == 0U,
          "empty USERFS opens the upload hint state");
    check(g_run_count == 1U, "empty USERFS never falls back to smoke autorun");

    if(g_failures)
        return 1;
    puts("MICROPYTHON_APP_OK list=1 preview=1 actions=1 run=1 stop=1 logs=1");
    return 0;
}

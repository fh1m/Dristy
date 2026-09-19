#ifndef HK_MICROPYTHON_VIEW_H
#define HK_MICROPYTHON_VIEW_H

#include <stdint.h>

#include "../../services/micropython_runtime.h"
#include "../../storage/userfs.h"
#include "micropython_config.h"

typedef enum
{
    MICROPYTHON_UI_LIST = 0,
    MICROPYTHON_UI_ACTIONS,
    MICROPYTHON_UI_PREVIEW,
    MICROPYTHON_UI_CONSOLE,
    MICROPYTHON_UI_DELETE_CONFIRM,
} micropython_ui_mode_t;

typedef enum
{
    MICROPYTHON_ACTION_RUN = 0,
    MICROPYTHON_ACTION_VIEW,
    MICROPYTHON_ACTION_LOGS,
    MICROPYTHON_ACTION_STARTUP,
    MICROPYTHON_ACTION_DELETE,
} micropython_action_t;

typedef struct
{
    char name[USERFS_NAME_MAX + 1U];
    uint32_t size;
    uint8_t startup;
} micropython_file_row_t;

typedef struct
{
    micropython_ui_mode_t mode;
    micropython_file_row_t files[MICROPYTHON_LIST_VISIBLE_ROWS];
    uint32_t file_count;
    uint32_t selected_index;
    uint32_t list_top;
    uint8_t visible_count;
    uint8_t action_index;
    char selected_name[USERFS_NAME_MAX + 1U];
    uint32_t selected_size;
    char startup[USERFS_NAME_MAX + 1U];
    char status[MICROPYTHON_STATUS_COLUMNS + 1U];
    char preview[MICROPYTHON_PREVIEW_LINES]
                [MICROPYTHON_PREVIEW_COLUMNS + 1U];
    uint32_t preview_line_number[MICROPYTHON_PREVIEW_LINES];
    uint32_t preview_line;
    uint8_t preview_count;
    uint8_t preview_has_previous;
    uint8_t preview_has_next;
    char log_lines[MICROPYTHON_LOG_VISIBLE_LINES]
                  [MICROPYTHON_LOG_COLUMNS + 1U];
    uint32_t log_top;
    uint32_t log_total;
    uint8_t log_count;
    uint8_t log_follow;
} micropython_view_state_t;

typedef struct
{
    uint8_t full;
    uint8_t info;
    uint8_t body;
    uint8_t row_mask;
} micropython_view_update_t;

void micropython_view_render(
    const micropython_view_state_t *ui,
    const micropython_runtime_status_t *runtime,
    const userfs_status_t *filesystem,
    const micropython_view_update_t *update);
void micropython_view_draw_icon(uint16_t x, uint16_t y,
                                uint16_t color, uint16_t background);

#endif

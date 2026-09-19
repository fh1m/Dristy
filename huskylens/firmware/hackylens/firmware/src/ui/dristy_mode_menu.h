#ifndef DRISTY_MODE_MENU_H
#define DRISTY_MODE_MENU_H

#include <stdint.h>

#include "../core/hk_app.h"
#include "dristy_app_catalog.h"
#include "../dristy/dristy_modes.h"

typedef enum
{
    DRISTY_MENU_ROW_MODE = 0,
    DRISTY_MENU_ROW_APP,
} dristy_menu_row_kind_t;

typedef struct
{
    dristy_menu_row_kind_t kind;
    dristy_mode_t mode;
    uint8_t app_reg_index;
    const char *title;
    const char *subtitle;
    dristy_app_category_t category;
} dristy_menu_row_t;

uint8_t dristy_menu_row_count(void);
void dristy_menu_row_at(uint8_t display_index, dristy_menu_row_t *out);

const char *dristy_menu_row_id(uint8_t display_index);

uint8_t dristy_menu_open_row(uint8_t display_index,
                             const hk_input_snapshot_t *input);

uint8_t dristy_menu_display_index_for_app_reg(uint8_t reg_index);

#endif

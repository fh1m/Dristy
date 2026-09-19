#include "dristy_mode_menu.h"

#include <stdio.h>
#include <string.h>

#include "../core/hk_app_registry.h"
#include "../core/hk_menu.h"
#include "../dristy/dristy_mode_readiness.h"
#include "hk_config.h"

#if HK_ENABLE_DRISTY
#include "../apps/vision_mode/vision_mode_controller.h"
#endif

static dristy_app_category_t category_for_mode(dristy_mode_t mode)
{
    if(mode <= DRISTY_MODE_FACE_RECOGNISE)
        return DRISTY_CAT_NEURAL;
    if(mode < DRISTY_MODE_DETECT_TRACK)
        return DRISTY_CAT_CLASSICAL;
    return DRISTY_CAT_HYBRID;
}

static const char *const s_tool_app_ids[] = {
    "camera",
    "files",
    "terminal",
    "micropython",
    "buttons",
    "pong",
    "settings",
    "sleep",
};

static uint8_t s_rows[48];
static uint8_t s_row_count;

static void rebuild_rows(void)
{
    uint8_t n = 0U;
    uint8_t mi;

    if(s_row_count != 0U)
        return;

    for(mi = 0U; mi < dristy_mode_table_count() && n < sizeof(s_rows); mi++)
        s_rows[n++] = mi;

    for(uint8_t ti = 0U; ti < (uint8_t)(sizeof(s_tool_app_ids) / sizeof(s_tool_app_ids[0]));
        ti++)
    {
        uint8_t r;

        for(r = 0U; r < g_menu_item_count; r++)
        {
            if(g_menu_items[r].id &&
               strcmp(g_menu_items[r].id, s_tool_app_ids[ti]) == 0)
            {
                if(n < sizeof(s_rows))
                    s_rows[n++] = (uint8_t)(0x80U | r);
                break;
            }
        }
    }
    s_row_count = n;
}

static void decode_row(uint8_t code, dristy_menu_row_t *out)
{
    memset(out, 0, sizeof(*out));
    if(code & 0x80U)
    {
        uint8_t reg = (uint8_t)(code & 0x7FU);
        const hk_app_t *app = &g_menu_items[reg];
        const dristy_app_meta_t *meta = dristy_app_meta_for_id(app->id);

        out->kind = DRISTY_MENU_ROW_APP;
        out->app_reg_index = reg;
        out->title = meta ? meta->title : app->title;
        out->subtitle = meta ? meta->subtitle : "";
        out->category = meta ? meta->category : DRISTY_CAT_TOOLS;
        if(strcmp(app->id, "settings") == 0 || strcmp(app->id, "sleep") == 0)
            out->category = DRISTY_CAT_SYSTEM;
        return;
    }

    {
        const dristy_mode_info_t *info = dristy_mode_table_at(code);

        out->kind = DRISTY_MENU_ROW_MODE;
        out->mode = info ? info->mode : DRISTY_MODE_INVALID;
        out->title = info ? info->name : "?";
        out->subtitle = dristy_mode_menu_subtitle(out->mode);
        out->category = category_for_mode(out->mode);
    }
}

uint8_t dristy_menu_row_count(void)
{
    rebuild_rows();
    return s_row_count;
}

void dristy_menu_row_at(uint8_t display_index, dristy_menu_row_t *out)
{
    rebuild_rows();
    if(!out)
        return;
    if(display_index >= s_row_count)
    {
        memset(out, 0, sizeof(*out));
        return;
    }
    decode_row(s_rows[display_index], out);
}

const char *dristy_menu_row_id(uint8_t display_index)
{
    dristy_menu_row_t row;

    dristy_menu_row_at(display_index, &row);
    if(row.kind == DRISTY_MENU_ROW_MODE)
    {
        static char buf[16];
        const dristy_mode_info_t *info = dristy_mode_info(row.mode);

        if(info && info->short_name)
            return info->short_name;
        snprintf(buf, sizeof(buf), "m%02x", (unsigned)row.mode);
        return buf;
    }
    if(row.app_reg_index < g_menu_item_count && g_menu_items[row.app_reg_index].id)
        return g_menu_items[row.app_reg_index].id;
    return "app";
}

uint8_t dristy_menu_open_row(uint8_t display_index,
                             const hk_input_snapshot_t *input)
{
    dristy_menu_row_t row;

    dristy_menu_row_at(display_index, &row);
    if(row.kind == DRISTY_MENU_ROW_MODE)
    {
#if HK_ENABLE_DRISTY
        vision_mode_controller_set_pending_mode(row.mode);
        {
            extern const hk_app_t g_vision_mode_app;

            return shell_open_app(&g_vision_mode_app, input);
        }
#else
        (void)input;
        return 0U;
#endif
    }
    if(row.app_reg_index < g_menu_item_count)
        return shell_open_app(&g_menu_items[row.app_reg_index], input);
    return 0U;
}

uint8_t dristy_menu_display_index_for_app_reg(uint8_t reg_index)
{
    uint8_t i;

    rebuild_rows();
    for(i = 0U; i < s_row_count; i++)
    {
        dristy_menu_row_t row;

        decode_row(s_rows[i], &row);
        if(row.kind == DRISTY_MENU_ROW_APP && row.app_reg_index == reg_index)
            return i;
    }
    return 0U;
}

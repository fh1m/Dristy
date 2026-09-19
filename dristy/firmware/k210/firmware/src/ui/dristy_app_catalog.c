#include "dristy_app_catalog.h"

#include <string.h>

#include "../core/hk_app_registry.h"
#include "dristy_mode_menu.h"

const dristy_app_meta_t *dristy_app_meta_for_id(const char *app_id)
{
    static const struct
    {
        const char *id;
        dristy_app_meta_t meta;
    } g_meta[] = {
        {"camera", {"Camera", "Live preview", DRISTY_CAT_TOOLS}},
        {"object_detect", {"Object detect", "YOLO + boxes", DRISTY_CAT_NEURAL}},
        {"face_detect", {"Face detect", "Face boxes", DRISTY_CAT_NEURAL}},
        {"apriltag", {"AprilTag", "Tag pose", DRISTY_CAT_CLASSICAL}},
        {"qr_camera", {"QR scan", "Decode QR", DRISTY_CAT_CLASSICAL}},
        {"files", {"Files", "SD browser", DRISTY_CAT_TOOLS}},
        {"terminal", {"Terminal", "Debug log", DRISTY_CAT_TOOLS}},
        {"micropython", {"MicroPython", "On-device scripts", DRISTY_CAT_TOOLS}},
        {"pong", {"Pong", "Mini game", DRISTY_CAT_TOOLS}},
        {"buttons", {"Buttons", "Input test", DRISTY_CAT_TOOLS}},
        {"settings", {"Settings", "Device config", DRISTY_CAT_SYSTEM}},
        {"sleep", {"Sleep", "Low power", DRISTY_CAT_SYSTEM}},
    };
    uint8_t i;

    if(!app_id)
        return NULL;
    for(i = 0U; i < (uint8_t)(sizeof(g_meta) / sizeof(g_meta[0])); i++)
    {
        if(strcmp(g_meta[i].id, app_id) == 0)
            return &g_meta[i].meta;
    }
    return NULL;
}

const char *dristy_app_category_label(dristy_app_category_t cat)
{
    switch(cat)
    {
    case DRISTY_CAT_NEURAL:
        return "NEURAL";
    case DRISTY_CAT_CLASSICAL:
        return "CLASSICAL";
    case DRISTY_CAT_HYBRID:
        return "HYBRID";
    case DRISTY_CAT_TOOLS:
        return "TOOLS";
    case DRISTY_CAT_SYSTEM:
        return "SYSTEM";
    default:
        return "";
    }
}

uint8_t dristy_menu_sorted_count(void)
{
    return dristy_menu_row_count();
}

uint8_t dristy_menu_sorted_reg_index(uint8_t display_index)
{
    dristy_menu_row_t row;

    dristy_menu_row_at(display_index, &row);
    if(row.kind == DRISTY_MENU_ROW_APP)
        return row.app_reg_index;
    return 0U;
}

dristy_app_category_t dristy_menu_category_at(uint8_t display_index)
{
    dristy_menu_row_t row;

    dristy_menu_row_at(display_index, &row);
    return row.category;
}

uint8_t dristy_menu_display_index_for_reg(uint8_t reg_index)
{
    return dristy_menu_display_index_for_app_reg(reg_index);
}

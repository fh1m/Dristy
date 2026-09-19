#include "apriltag_settings_menu.h"

#include <stdio.h>

#include "hk_config.h"
#include "apriltag_config.h"
#include "apriltag_settings.h"
#include "../../core/camera_types.h"

static const char *const g_output_choices[] = {"ALL", "SELECTED"};
static const char *const g_light_choices[] = {"LED", "RGB"};

static const settings_menu_item_t g_items[] = {
    {.id = APRILTAG_SETTINGS_REFINE, .title = "Refine Edges",
     .kind = SETTINGS_MENU_ITEM_TOGGLE, .interaction = SETTINGS_MENU_CYCLE_ON_OK},
    {.id = APRILTAG_SETTINGS_OUTPUT, .title = "Output",
     .kind = SETTINGS_MENU_ITEM_CHOICE, .interaction = SETTINGS_MENU_CYCLE_ON_OK,
     .choices = g_output_choices, .choice_count = 2U, .flags = SETTINGS_MENU_ITEM_WRAP},
    {.id = APRILTAG_SETTINGS_CLEAR_IDS, .title = "Clear IDs",
     .kind = SETTINGS_MENU_ITEM_ACTION, .interaction = SETTINGS_MENU_CYCLE_ON_OK},
    {.id = APRILTAG_SETTINGS_FPS, .title = "FPS Counter",
     .kind = SETTINGS_MENU_ITEM_TOGGLE, .interaction = SETTINGS_MENU_CYCLE_ON_OK},
    {.id = APRILTAG_SETTINGS_LIGHT, .title = "Light",
     .kind = SETTINGS_MENU_ITEM_CHOICE, .interaction = SETTINGS_MENU_CYCLE_ON_OK,
     .choices = g_light_choices, .choice_count = 2U, .flags = SETTINGS_MENU_ITEM_WRAP},
    {.id = APRILTAG_SETTINGS_RGB_RED, .title = "RGB Red",
     .kind = SETTINGS_MENU_ITEM_RANGE, .interaction = SETTINGS_MENU_CYCLE_ON_OK,
     .minimum = 0, .maximum = 100, .step = 10, .flags = SETTINGS_MENU_ITEM_WRAP},
    {.id = APRILTAG_SETTINGS_RGB_GREEN, .title = "RGB Green",
     .kind = SETTINGS_MENU_ITEM_RANGE, .interaction = SETTINGS_MENU_CYCLE_ON_OK,
     .minimum = 0, .maximum = 100, .step = 10, .flags = SETTINGS_MENU_ITEM_WRAP},
    {.id = APRILTAG_SETTINGS_RGB_BLUE, .title = "RGB Blue",
     .kind = SETTINGS_MENU_ITEM_RANGE, .interaction = SETTINGS_MENU_CYCLE_ON_OK,
     .minimum = 0, .maximum = 100, .step = 10, .flags = SETTINGS_MENU_ITEM_WRAP},
    {.id = APRILTAG_SETTINGS_VERSION, .title = "Version",
     .kind = SETTINGS_MENU_ITEM_TEXT, .interaction = SETTINGS_MENU_CYCLE_ON_OK},
};

_Static_assert(sizeof(g_items) / sizeof(g_items[0]) == APRILTAG_SETTINGS_ROW_COUNT,
               "AprilTag settings descriptor count changed");

static int32_t apriltag_menu_read(void *context, uint16_t id)
{
    const apriltag_preferences_t *preferences = apriltag_settings_preferences();

    (void)context;
    if(id == APRILTAG_SETTINGS_REFINE)
        return preferences->refine_edges;
    if(id == APRILTAG_SETTINGS_OUTPUT)
        return preferences->output_mode;
    if(id == APRILTAG_SETTINGS_CLEAR_IDS)
        return apriltag_settings_selected_count();
    if(id == APRILTAG_SETTINGS_FPS)
        return preferences->fps_enabled;
    if(id == APRILTAG_SETTINGS_LIGHT)
        return preferences->light_mode;
    if(id == APRILTAG_SETTINGS_RGB_RED)
        return preferences->rgb_red;
    if(id == APRILTAG_SETTINGS_RGB_GREEN)
        return preferences->rgb_green;
    if(id == APRILTAG_SETTINGS_RGB_BLUE)
        return preferences->rgb_blue;
    return 0;
}

static uint8_t apriltag_menu_write(void *context, uint16_t id, int32_t value)
{
    (void)context;
    if(id == APRILTAG_SETTINGS_REFINE)
        return apriltag_settings_set_refine((uint8_t)value);
    if(id == APRILTAG_SETTINGS_OUTPUT)
        return apriltag_settings_set_output((apriltag_output_mode_t)value);
    if(id == APRILTAG_SETTINGS_FPS)
        return apriltag_settings_set_fps((uint8_t)value);
    if(id == APRILTAG_SETTINGS_LIGHT)
        return apriltag_settings_set_light((uint8_t)value);
    if(id >= APRILTAG_SETTINGS_RGB_RED && id <= APRILTAG_SETTINGS_RGB_BLUE)
        return apriltag_settings_set_rgb((uint8_t)(id - APRILTAG_SETTINGS_RGB_RED),
                                         (uint8_t)value);
    return 0U;
}

static uint8_t apriltag_menu_action(void *context, uint16_t id)
{
    (void)context;
    return id == APRILTAG_SETTINGS_CLEAR_IDS ? apriltag_settings_clear_selected() : 0U;
}

static uint8_t apriltag_menu_format(void *context,
                                    uint16_t id,
                                    char *value,
                                    size_t value_size)
{
    (void)context;
    if(id == APRILTAG_SETTINGS_CLEAR_IDS)
    {
        snprintf(value, value_size, "%u", (unsigned)apriltag_settings_selected_count());
        return 1U;
    }
    if(id == APRILTAG_SETTINGS_VERSION)
    {
        snprintf(value, value_size, "v%s", HACKYLENS_VERSION);
        return 1U;
    }
    return 0U;
}

static void apriltag_menu_changed(void *context, uint16_t id)
{
    (void)context;
    if(id == APRILTAG_SETTINGS_REFINE || id == APRILTAG_SETTINGS_FPS ||
       id == APRILTAG_SETTINGS_LIGHT ||
       (id >= APRILTAG_SETTINGS_RGB_RED && id <= APRILTAG_SETTINGS_RGB_BLUE))
        apriltag_settings_apply_session();
}

static const settings_menu_definition_t g_definition = {
    .title = "APRILTAG SETTINGS",
    .items = g_items,
    .item_count = (uint8_t)(sizeof(g_items) / sizeof(g_items[0])),
    .context = NULL,
    .read = apriltag_menu_read,
    .write = apriltag_menu_write,
    .action = apriltag_menu_action,
    .format = apriltag_menu_format,
    .changed = apriltag_menu_changed,
};

const settings_menu_definition_t *apriltag_settings_menu_definition(void)
{
    return &g_definition;
}

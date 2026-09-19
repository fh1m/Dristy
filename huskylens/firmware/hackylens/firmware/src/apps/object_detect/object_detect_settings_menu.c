#include "object_detect_settings_menu.h"

#include <stdio.h>

#include "hk_config.h"
#include "../../core/camera_types.h"
#include "object_detect_config.h"
#include "object_detect_settings.h"

static const char *const g_light_choices[] = {"LED", "RGB"};

static const settings_menu_item_t g_items[] = {
    {.id = OBJECT_DETECT_SETTINGS_CONFIDENCE, .title = "Confidence",
     .kind = SETTINGS_MENU_ITEM_RANGE, .interaction = SETTINGS_MENU_CYCLE_ON_OK,
     .minimum = 10, .maximum = 90, .step = 5, .flags = SETTINGS_MENU_ITEM_WRAP},
    {.id = OBJECT_DETECT_SETTINGS_NMS, .title = "NMS",
     .kind = SETTINGS_MENU_ITEM_RANGE, .interaction = SETTINGS_MENU_CYCLE_ON_OK,
     .minimum = 0, .maximum = 90, .step = 5, .flags = SETTINGS_MENU_ITEM_WRAP},
    {.id = OBJECT_DETECT_SETTINGS_FPS, .title = "FPS Counter",
     .kind = SETTINGS_MENU_ITEM_TOGGLE, .interaction = SETTINGS_MENU_CYCLE_ON_OK},
    {.id = OBJECT_DETECT_SETTINGS_LIGHT, .title = "Light",
     .kind = SETTINGS_MENU_ITEM_CHOICE, .interaction = SETTINGS_MENU_CYCLE_ON_OK,
     .choices = g_light_choices, .choice_count = 2U,
     .flags = SETTINGS_MENU_ITEM_WRAP},
    {.id = OBJECT_DETECT_SETTINGS_RGB_RED, .title = "RGB Red",
     .kind = SETTINGS_MENU_ITEM_RANGE, .interaction = SETTINGS_MENU_CYCLE_ON_OK,
     .minimum = 0, .maximum = 100, .step = 10,
     .flags = SETTINGS_MENU_ITEM_WRAP},
    {.id = OBJECT_DETECT_SETTINGS_RGB_GREEN, .title = "RGB Green",
     .kind = SETTINGS_MENU_ITEM_RANGE, .interaction = SETTINGS_MENU_CYCLE_ON_OK,
     .minimum = 0, .maximum = 100, .step = 10,
     .flags = SETTINGS_MENU_ITEM_WRAP},
    {.id = OBJECT_DETECT_SETTINGS_RGB_BLUE, .title = "RGB Blue",
     .kind = SETTINGS_MENU_ITEM_RANGE, .interaction = SETTINGS_MENU_CYCLE_ON_OK,
     .minimum = 0, .maximum = 100, .step = 10,
     .flags = SETTINGS_MENU_ITEM_WRAP},
    {.id = OBJECT_DETECT_SETTINGS_VERSION, .title = "Version",
     .kind = SETTINGS_MENU_ITEM_TEXT, .interaction = SETTINGS_MENU_CYCLE_ON_OK},
};

_Static_assert(sizeof(g_items) / sizeof(g_items[0]) ==
               OBJECT_DETECT_SETTINGS_ROW_COUNT,
               "Object Detection settings descriptor count changed");

static int32_t object_menu_read(void *context, uint16_t id)
{
    const object_detect_preferences_t *preferences =
        object_detect_settings_preferences();

    (void)context;
    if(id == OBJECT_DETECT_SETTINGS_CONFIDENCE)
        return preferences->confidence;
    if(id == OBJECT_DETECT_SETTINGS_NMS)
        return preferences->nms;
    if(id == OBJECT_DETECT_SETTINGS_FPS)
        return preferences->fps_enabled;
    if(id == OBJECT_DETECT_SETTINGS_LIGHT)
        return preferences->light_mode;
    if(id == OBJECT_DETECT_SETTINGS_RGB_RED)
        return preferences->rgb_red;
    if(id == OBJECT_DETECT_SETTINGS_RGB_GREEN)
        return preferences->rgb_green;
    if(id == OBJECT_DETECT_SETTINGS_RGB_BLUE)
        return preferences->rgb_blue;
    return 0;
}

static uint8_t object_menu_write(void *context, uint16_t id, int32_t value)
{
    (void)context;
    if(id == OBJECT_DETECT_SETTINGS_CONFIDENCE)
        return object_detect_settings_set_confidence((uint8_t)value);
    if(id == OBJECT_DETECT_SETTINGS_NMS)
        return object_detect_settings_set_nms((uint8_t)value);
    if(id == OBJECT_DETECT_SETTINGS_FPS)
        return object_detect_settings_set_fps((uint8_t)value);
    if(id == OBJECT_DETECT_SETTINGS_LIGHT)
        return object_detect_settings_set_light((uint8_t)value);
    if(id >= OBJECT_DETECT_SETTINGS_RGB_RED &&
       id <= OBJECT_DETECT_SETTINGS_RGB_BLUE)
        return object_detect_settings_set_rgb(
            (uint8_t)(id - OBJECT_DETECT_SETTINGS_RGB_RED), (uint8_t)value);
    return 0U;
}

static uint8_t object_menu_format(void *context,
                                  uint16_t id,
                                  char *value,
                                  size_t value_size)
{
    (void)context;
    if(id == OBJECT_DETECT_SETTINGS_VERSION)
    {
        snprintf(value, value_size, "v%s", HACKYLENS_VERSION);
        return 1U;
    }
    return 0U;
}

static void object_menu_changed(void *context, uint16_t id)
{
    (void)context;
    (void)id;
    object_detect_settings_apply_session();
}

static const settings_menu_definition_t g_definition = {
    .title = "OBJECT SETTINGS",
    .items = g_items,
    .item_count = (uint8_t)(sizeof(g_items) / sizeof(g_items[0])),
    .context = NULL,
    .read = object_menu_read,
    .write = object_menu_write,
    .format = object_menu_format,
    .changed = object_menu_changed,
};

const settings_menu_definition_t *object_detect_settings_menu_definition(void)
{
    return &g_definition;
}

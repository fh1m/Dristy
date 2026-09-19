#include "camera_settings_menu.h"

#include <stdio.h>

#include "hk_config.h"
#include "../../core/camera_types.h"
#include "../../core/hk_camera_sizes.h"
#include "../../core/photo_types.h"
#include "../../services/camera_light.h"
#include "../../services/camera_persist_settings.h"
#include "../../services/camera_status.h"
#include "../../services/settings_lights.h"
#include "camera_config.h"
#include "camera_photo_format.h"

typedef enum
{
    CAMERA_MENU_REVIEW = 0,
    CAMERA_MENU_FPS,
    CAMERA_MENU_LIGHT,
    CAMERA_MENU_RGB_RED,
    CAMERA_MENU_RGB_GREEN,
    CAMERA_MENU_RGB_BLUE,
    CAMERA_MENU_FOLDER,
    CAMERA_MENU_FORMAT,
    CAMERA_MENU_SIZE,
    CAMERA_MENU_VERSION,
    CAMERA_MENU_COUNT,
} camera_menu_id_t;

static const char *const g_light_choices[] = {"LED", "RGB"};

static const settings_menu_item_t g_camera_items[] = {
    {.id = CAMERA_MENU_REVIEW, .title = "Review", .kind = SETTINGS_MENU_ITEM_TOGGLE},
    {.id = CAMERA_MENU_FPS, .title = "FPS Counter", .kind = SETTINGS_MENU_ITEM_TOGGLE},
    {.id = CAMERA_MENU_LIGHT, .title = "Light", .kind = SETTINGS_MENU_ITEM_CHOICE,
     .choices = g_light_choices, .choice_count = 2U, .flags = SETTINGS_MENU_ITEM_WRAP},
    {.id = CAMERA_MENU_RGB_RED, .title = "RGB Red", .kind = SETTINGS_MENU_ITEM_RANGE,
     .minimum = 0, .maximum = 100, .step = 10, .flags = SETTINGS_MENU_ITEM_WRAP},
    {.id = CAMERA_MENU_RGB_GREEN, .title = "RGB Green", .kind = SETTINGS_MENU_ITEM_RANGE,
     .minimum = 0, .maximum = 100, .step = 10, .flags = SETTINGS_MENU_ITEM_WRAP},
    {.id = CAMERA_MENU_RGB_BLUE, .title = "RGB Blue", .kind = SETTINGS_MENU_ITEM_RANGE,
     .minimum = 0, .maximum = 100, .step = 10, .flags = SETTINGS_MENU_ITEM_WRAP},
    {.id = CAMERA_MENU_FOLDER, .title = "Folder", .kind = SETTINGS_MENU_ITEM_TEXT},
    {.id = CAMERA_MENU_FORMAT, .title = "Format", .kind = SETTINGS_MENU_ITEM_CHOICE,
     .choice_count = PHOTO_FORMAT_COUNT, .flags = SETTINGS_MENU_ITEM_WRAP},
    {.id = CAMERA_MENU_SIZE, .title = "Size", .kind = SETTINGS_MENU_ITEM_CHOICE,
     .choice_count = CAMERA_SIZE_COUNT, .flags = SETTINGS_MENU_ITEM_WRAP},
    {.id = CAMERA_MENU_VERSION, .title = "Version", .kind = SETTINGS_MENU_ITEM_TEXT},
};

_Static_assert(sizeof(g_camera_items) / sizeof(g_camera_items[0]) == CAMERA_MENU_COUNT,
               "camera settings descriptor count changed");

static camera_rgb_channel_t camera_menu_rgb_channel(uint16_t id)
{
    if(id == CAMERA_MENU_RGB_RED)
        return CAMERA_RGB_RED;
    if(id == CAMERA_MENU_RGB_GREEN)
        return CAMERA_RGB_GREEN;
    return CAMERA_RGB_BLUE;
}

static int32_t camera_menu_read(void *context, uint16_t id)
{
    (void)context;
    if(id == CAMERA_MENU_REVIEW)
        return camera_service_review_after_shot();
    if(id == CAMERA_MENU_FPS)
        return camera_service_fps_enabled();
    if(id == CAMERA_MENU_LIGHT)
        return camera_service_light_mode();
    if(id >= CAMERA_MENU_RGB_RED && id <= CAMERA_MENU_RGB_BLUE)
        return camera_service_rgb_channel(camera_menu_rgb_channel(id));
    if(id == CAMERA_MENU_FORMAT)
        return camera_service_photo_format();
    if(id == CAMERA_MENU_SIZE)
        return camera_service_size();
    return 0;
}

static uint8_t camera_menu_write(void *context, uint16_t id, int32_t value)
{
    (void)context;
    if(id == CAMERA_MENU_REVIEW && value != camera_service_review_after_shot())
    {
        printf("[CAM] review_after_shot=%u\r\n", camera_service_toggle_review_after_shot());
        return 1U;
    }
    if(id == CAMERA_MENU_FPS && value != camera_service_fps_enabled())
    {
        camera_service_set_fps_enabled((uint8_t)value);
        printf("[CAM] fps_counter=%u\r\n", (unsigned)camera_service_fps_enabled());
        return 1U;
    }
    if(id == CAMERA_MENU_LIGHT && value != camera_service_light_mode())
    {
        camera_light_mode_t mode = camera_service_toggle_light_mode();
        printf("[CAM] light_mode=%s\r\n", camera_light_mode_label(mode));
        return 1U;
    }
    if(id >= CAMERA_MENU_RGB_RED && id <= CAMERA_MENU_RGB_BLUE &&
       value != camera_service_rgb_channel(camera_menu_rgb_channel(id)))
    {
        camera_service_cycle_rgb_channel(camera_menu_rgb_channel(id));
        printf("[CAM] rgb=%u/%u/%u\r\n",
               camera_service_rgb_channel(CAMERA_RGB_RED),
               camera_service_rgb_channel(CAMERA_RGB_GREEN),
               camera_service_rgb_channel(CAMERA_RGB_BLUE));
        return 1U;
    }
    if(id == CAMERA_MENU_FORMAT && value != camera_service_photo_format())
    {
        photo_format_t format = camera_service_cycle_photo_format();
        printf("[CAM] photo_format=%s\r\n", photo_format_label(format));
        return 1U;
    }
    if(id == CAMERA_MENU_SIZE && value != camera_service_size())
    {
        camera_size_t size = camera_service_cycle_size();
        printf("%s size_setting=%s\r\n", camera_log_prefix(), camera_size_label(size));
        return 1U;
    }
    return 0U;
}

static uint8_t camera_menu_format(void *context, uint16_t id,
                                  char *value, size_t value_size)
{
    (void)context;
    if(id == CAMERA_MENU_VERSION)
        snprintf(value, value_size, "v%s", HACKYLENS_VERSION);
    else if(id == CAMERA_MENU_FOLDER)
        snprintf(value, value_size, "%s", PHOTO_DIR_LONG_NAME);
    else if(id == CAMERA_MENU_FORMAT)
        snprintf(value, value_size, "%s", photo_format_label(camera_service_photo_format()));
    else if(id == CAMERA_MENU_SIZE)
        snprintf(value, value_size, "%s", camera_size_label(camera_service_size()));
    else
        return 0U;
    return 1U;
}

static const settings_menu_definition_t g_camera_definition = {
    .title = "CAM SETTINGS",
    .items = g_camera_items,
    .item_count = (uint8_t)CAMERA_MENU_COUNT,
    .read = camera_menu_read,
    .write = camera_menu_write,
    .format = camera_menu_format,
};

const settings_menu_definition_t *camera_settings_menu_definition(void)
{
    return &g_camera_definition;
}

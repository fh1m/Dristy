#include "qr_settings_menu.h"

#include <stdio.h>

#include "hk_config.h"
#include "../../core/camera_types.h"
#include "../../services/camera_light.h"
#include "../../services/camera_persist_settings.h"
#include "../../services/settings_lights.h"
#include "qr_config.h"
#include "qr_service.h"

typedef enum
{
    QR_MENU_RATE = 0,
    QR_MENU_FPS,
    QR_MENU_LIGHT,
    QR_MENU_RGB_RED,
    QR_MENU_RGB_GREEN,
    QR_MENU_RGB_BLUE,
    QR_MENU_VERSION,
    QR_MENU_COUNT,
} qr_menu_id_t;

static const char *const g_light_choices[] = {"LED", "RGB"};

static const settings_menu_item_t g_qr_items[] = {
    {.id = QR_MENU_RATE, .title = "Decode Rate", .kind = SETTINGS_MENU_ITEM_RANGE,
     .minimum = QR_DECODE_RATE_MIN, .maximum = QR_DECODE_RATE_MAX, .step = 1,
     .flags = SETTINGS_MENU_ITEM_WRAP},
    {.id = QR_MENU_FPS, .title = "FPS Counter", .kind = SETTINGS_MENU_ITEM_TOGGLE},
    {.id = QR_MENU_LIGHT, .title = "Light", .kind = SETTINGS_MENU_ITEM_CHOICE,
     .choices = g_light_choices, .choice_count = 2U, .flags = SETTINGS_MENU_ITEM_WRAP},
    {.id = QR_MENU_RGB_RED, .title = "RGB Red", .kind = SETTINGS_MENU_ITEM_RANGE,
     .minimum = 0, .maximum = 100, .step = 10, .flags = SETTINGS_MENU_ITEM_WRAP},
    {.id = QR_MENU_RGB_GREEN, .title = "RGB Green", .kind = SETTINGS_MENU_ITEM_RANGE,
     .minimum = 0, .maximum = 100, .step = 10, .flags = SETTINGS_MENU_ITEM_WRAP},
    {.id = QR_MENU_RGB_BLUE, .title = "RGB Blue", .kind = SETTINGS_MENU_ITEM_RANGE,
     .minimum = 0, .maximum = 100, .step = 10, .flags = SETTINGS_MENU_ITEM_WRAP},
    {.id = QR_MENU_VERSION, .title = "Version", .kind = SETTINGS_MENU_ITEM_TEXT},
};

_Static_assert(sizeof(g_qr_items) / sizeof(g_qr_items[0]) == QR_MENU_COUNT,
               "QR settings descriptor count changed");

static camera_rgb_channel_t qr_menu_rgb_channel(uint16_t id)
{
    if(id == QR_MENU_RGB_RED)
        return CAMERA_RGB_RED;
    if(id == QR_MENU_RGB_GREEN)
        return CAMERA_RGB_GREEN;
    return CAMERA_RGB_BLUE;
}

static int32_t qr_menu_read(void *context, uint16_t id)
{
    (void)context;
    if(id == QR_MENU_RATE)
        return qr_service_decode_rate();
    if(id == QR_MENU_FPS)
        return camera_service_fps_enabled();
    if(id == QR_MENU_LIGHT)
        return camera_service_light_mode();
    if(id >= QR_MENU_RGB_RED && id <= QR_MENU_RGB_BLUE)
        return camera_service_rgb_channel(qr_menu_rgb_channel(id));
    return 0;
}

static uint8_t qr_menu_write(void *context, uint16_t id, int32_t value)
{
    (void)context;
    if(id == QR_MENU_RATE && value != qr_service_decode_rate())
    {
        uint8_t rate = qr_service_cycle_decode_rate();
        printf("[QR] decode_rate=%u/s\r\n", rate);
        return 1U;
    }
    if(id == QR_MENU_FPS && value != camera_service_fps_enabled())
    {
        camera_service_set_fps_enabled((uint8_t)value);
        printf("[QR] fps_counter=%u\r\n", (unsigned)camera_service_fps_enabled());
        return 1U;
    }
    if(id == QR_MENU_LIGHT && value != camera_service_light_mode())
    {
        camera_light_mode_t mode = camera_service_toggle_light_mode();
        printf("[QR] light_mode=%s\r\n", camera_light_mode_label(mode));
        return 1U;
    }
    if(id >= QR_MENU_RGB_RED && id <= QR_MENU_RGB_BLUE &&
       value != camera_service_rgb_channel(qr_menu_rgb_channel(id)))
    {
        camera_service_cycle_rgb_channel(qr_menu_rgb_channel(id));
        printf("[QR] rgb=%u/%u/%u\r\n",
               camera_service_rgb_channel(CAMERA_RGB_RED),
               camera_service_rgb_channel(CAMERA_RGB_GREEN),
               camera_service_rgb_channel(CAMERA_RGB_BLUE));
        return 1U;
    }
    return 0U;
}

static uint8_t qr_menu_format(void *context, uint16_t id,
                              char *value, size_t value_size)
{
    (void)context;
    if(id == QR_MENU_VERSION)
        snprintf(value, value_size, "v%s", HACKYLENS_VERSION);
    else if(id == QR_MENU_RATE)
        snprintf(value, value_size, "%u/s", qr_service_decode_rate());
    else
        return 0U;
    return 1U;
}

static const settings_menu_definition_t g_qr_definition = {
    .title = "QR SETTINGS",
    .items = g_qr_items,
    .item_count = (uint8_t)QR_MENU_COUNT,
    .read = qr_menu_read,
    .write = qr_menu_write,
    .format = qr_menu_format,
};

const settings_menu_definition_t *qr_settings_menu_definition(void)
{
    return &g_qr_definition;
}

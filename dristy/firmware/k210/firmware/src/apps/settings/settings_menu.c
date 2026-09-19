#include "settings_menu.h"

#include <stdio.h>

#include "hk_config.h"
#include "../../core/hk_app_registry.h"
#include "../../core/hk_screen.h"
#include "../../services/external_link_service.h"
#include "../../services/external_link_types.h"
#include "../../services/settings_lights.h"
#include "../../services/settings_persistence.h"
#include "../../services/settings_service.h"
#if HK_ENABLE_DRISTY
#include "../../dristy/dristy_pipeline.h"
#include "../../dristy/dristy_uart_bridge.h"
#endif

typedef enum
{
    SETTINGS_APP_LED_SWITCH = 0,
    SETTINGS_APP_LED_BRIGHTNESS,
    SETTINGS_APP_RGB_SWITCH,
    SETTINGS_APP_RGB_RED,
    SETTINGS_APP_RGB_GREEN,
    SETTINGS_APP_RGB_BLUE,
    SETTINGS_APP_SCREEN_BRIGHTNESS,
    SETTINGS_APP_AUTO_SLEEP,
    SETTINGS_APP_EXTERNAL_LINK,
    SETTINGS_APP_UART_SPEED,
    SETTINGS_APP_AUTOSTART,
#if HK_ENABLE_DRISTY
    SETTINGS_APP_HOST_STREAM,
    SETTINGS_APP_DETECT_CONF,
#endif
    SETTINGS_APP_VERSION,
    SETTINGS_APP_ITEM_COUNT,
} settings_app_item_id_t;

static const char *const g_transport_choices[] = {"UART", "I2C"};
static const char *const g_uart_speed_choices[] = {"9600", "115200", "1000000"};

static const settings_menu_item_t g_items[] = {
    {.id = SETTINGS_APP_LED_SWITCH, .title = "LED Switch",
     .kind = SETTINGS_MENU_ITEM_TOGGLE},
    {.id = SETTINGS_APP_LED_BRIGHTNESS, .title = "LED Bright",
     .kind = SETTINGS_MENU_ITEM_RANGE, .interaction = SETTINGS_MENU_EDIT_ON_OK,
     .minimum = 0, .maximum = 100, .step = 10},
    {.id = SETTINGS_APP_RGB_SWITCH, .title = "RGB Switch",
     .kind = SETTINGS_MENU_ITEM_TOGGLE},
    {.id = SETTINGS_APP_RGB_RED, .title = "RGB Red",
     .kind = SETTINGS_MENU_ITEM_RANGE, .interaction = SETTINGS_MENU_EDIT_ON_OK,
     .minimum = 0, .maximum = 100, .step = 10},
    {.id = SETTINGS_APP_RGB_GREEN, .title = "RGB Green",
     .kind = SETTINGS_MENU_ITEM_RANGE, .interaction = SETTINGS_MENU_EDIT_ON_OK,
     .minimum = 0, .maximum = 100, .step = 10},
    {.id = SETTINGS_APP_RGB_BLUE, .title = "RGB Blue",
     .kind = SETTINGS_MENU_ITEM_RANGE, .interaction = SETTINGS_MENU_EDIT_ON_OK,
     .minimum = 0, .maximum = 100, .step = 10},
    {.id = SETTINGS_APP_SCREEN_BRIGHTNESS, .title = "Screen Bright",
     .kind = SETTINGS_MENU_ITEM_RANGE, .interaction = SETTINGS_MENU_EDIT_ON_OK,
     .minimum = 10, .maximum = 100, .step = 10},
    {.id = SETTINGS_APP_AUTO_SLEEP, .title = "Auto Sleep",
     .kind = SETTINGS_MENU_ITEM_RANGE, .interaction = SETTINGS_MENU_EDIT_ON_OK,
     .minimum = 1, .maximum = 30, .step = 1},
    {.id = SETTINGS_APP_EXTERNAL_LINK, .title = "External Link",
     .kind = SETTINGS_MENU_ITEM_CHOICE, .interaction = SETTINGS_MENU_EDIT_ON_OK,
     .choices = g_transport_choices, .choice_count = 2U, .flags = SETTINGS_MENU_ITEM_WRAP},
    {.id = SETTINGS_APP_UART_SPEED, .title = "UART Speed",
     .kind = SETTINGS_MENU_ITEM_CHOICE, .interaction = SETTINGS_MENU_EDIT_ON_OK,
     .choices = g_uart_speed_choices, .choice_count = EXTERNAL_LINK_UART_SPEED_COUNT,
     .flags = SETTINGS_MENU_ITEM_WRAP},
    {.id = SETTINGS_APP_AUTOSTART, .title = "Autostart",
     .kind = SETTINGS_MENU_ITEM_CHOICE, .interaction = SETTINGS_MENU_EDIT_ON_OK,
     .flags = SETTINGS_MENU_ITEM_WRAP},
#if HK_ENABLE_DRISTY
    {.id = SETTINGS_APP_HOST_STREAM, .title = "Host stream",
     .kind = SETTINGS_MENU_ITEM_TOGGLE},
    {.id = SETTINGS_APP_DETECT_CONF, .title = "Detect conf",
     .kind = SETTINGS_MENU_ITEM_RANGE, .interaction = SETTINGS_MENU_EDIT_ON_OK,
     .minimum = 10, .maximum = 90, .step = 5},
#endif
    {.id = SETTINGS_APP_VERSION, .title = "Version", .kind = SETTINGS_MENU_ITEM_TEXT},
};

_Static_assert(sizeof(g_items) / sizeof(g_items[0]) == SETTINGS_APP_ITEM_COUNT,
               "system settings descriptor count changed");

static int32_t settings_app_read(void *context, uint16_t id)
{
    (void)context;
    if(id == SETTINGS_APP_LED_SWITCH)
        return settings_led_enabled();
    if(id == SETTINGS_APP_LED_BRIGHTNESS)
        return settings_led_brightness();
    if(id == SETTINGS_APP_RGB_SWITCH)
        return settings_rgb_enabled();
    if(id == SETTINGS_APP_RGB_RED)
        return settings_rgb_red();
    if(id == SETTINGS_APP_RGB_GREEN)
        return settings_rgb_green();
    if(id == SETTINGS_APP_RGB_BLUE)
        return settings_rgb_blue();
    if(id == SETTINGS_APP_SCREEN_BRIGHTNESS)
        return settings_screen_brightness();
    if(id == SETTINGS_APP_AUTO_SLEEP)
        return settings_auto_sleep_minutes();
    if(id == SETTINGS_APP_EXTERNAL_LINK)
        return settings_external_link_transport();
    if(id == SETTINGS_APP_UART_SPEED)
        return settings_external_link_uart_speed();
    if(id == SETTINGS_APP_AUTOSTART)
    {
        hk_autostart_id_t selected = settings_autostart_id();

        if(selected == HK_AUTOSTART_OFF)
            return 0;
        for(uint8_t index = 0U; index < hk_app_autostart_count(); index++)
        {
            const hk_app_t *app = hk_app_autostart_at(index);
            if(app && app->autostart_id == selected)
                return (int32_t)index + 1;
        }
        return 0;
    }
#if HK_ENABLE_DRISTY
    if(id == SETTINGS_APP_HOST_STREAM)
        return dristy_uart_bridge_ctrl_stream_active();
    if(id == SETTINGS_APP_DETECT_CONF)
        return 30;
#endif
    return 0;
}

static uint8_t settings_app_write_rgb(uint16_t id, uint8_t value)
{
    uint8_t current = id == SETTINGS_APP_RGB_RED ? settings_rgb_red() :
                      (id == SETTINGS_APP_RGB_GREEN ? settings_rgb_green() : settings_rgb_blue());

    if(current == value)
        return 0U;
    if(id == SETTINGS_APP_RGB_RED)
        settings_set_rgb_red(value);
    else if(id == SETTINGS_APP_RGB_GREEN)
        settings_set_rgb_green(value);
    else
        settings_set_rgb_blue(value);
    rgb_led_apply();
    settings_mark_dirty(0U);
    printf("[RGB] on=%u r=%u g=%u b=%u\r\n",
           settings_rgb_enabled(), settings_rgb_red(), settings_rgb_green(), settings_rgb_blue());
    return 1U;
}

static uint8_t settings_app_write(void *context, uint16_t id, int32_t value)
{
    (void)context;
    if(id == SETTINGS_APP_LED_SWITCH && value != settings_led_enabled())
    {
        settings_set_led_enabled((uint8_t)value);
        illum_led_apply();
        settings_mark_dirty(1U);
        printf("[LED] illum on=%u brightness=%u\r\n",
               settings_led_enabled(), settings_led_brightness());
        return 1U;
    }
    if(id == SETTINGS_APP_LED_BRIGHTNESS && value != settings_led_brightness())
    {
        settings_set_led_brightness((uint8_t)value);
        illum_led_apply();
        settings_mark_dirty(0U);
        printf("[LED] illum on=%u brightness=%u\r\n",
               settings_led_enabled(), settings_led_brightness());
        return 1U;
    }
    if(id == SETTINGS_APP_RGB_SWITCH && value != settings_rgb_enabled())
    {
        settings_set_rgb_enabled((uint8_t)value);
        rgb_led_apply();
        settings_mark_dirty(1U);
        printf("[RGB] on=%u r=%u g=%u b=%u\r\n",
               settings_rgb_enabled(), settings_rgb_red(), settings_rgb_green(), settings_rgb_blue());
        return 1U;
    }
    if(id >= SETTINGS_APP_RGB_RED && id <= SETTINGS_APP_RGB_BLUE)
        return settings_app_write_rgb(id, (uint8_t)value);
    if(id == SETTINGS_APP_SCREEN_BRIGHTNESS && value != settings_screen_brightness())
    {
        settings_set_screen_brightness((uint8_t)value);
        screen_brightness_apply();
        settings_mark_dirty(0U);
        printf("[LCD] brightness=%u\r\n", settings_screen_brightness());
        return 1U;
    }
    if(id == SETTINGS_APP_AUTO_SLEEP && value != settings_auto_sleep_minutes())
    {
        settings_set_auto_sleep_minutes((uint8_t)value);
        activity_note();
        settings_mark_dirty(0U);
        printf("[SETTINGS] auto_sleep=%u min\r\n", settings_auto_sleep_minutes());
        return 1U;
    }
    if(id == SETTINGS_APP_EXTERNAL_LINK && value != settings_external_link_transport())
    {
        external_link_transport_t transport = (external_link_transport_t)value;

        settings_set_external_link_transport(transport);
        external_link_service_set_transport(transport);
        settings_mark_dirty(0U);
        printf("[LINK] setting=%s\r\n", transport == EXTERNAL_LINK_I2C ? "I2C" : "UART");
        return 1U;
    }
    if(id == SETTINGS_APP_UART_SPEED && value != settings_external_link_uart_speed())
    {
        settings_set_external_link_uart_speed((external_link_uart_speed_t)value);
        external_link_service_set_uart_baud(settings_external_link_uart_baud());
        settings_mark_dirty(0U);
        printf("[LINK] uart=%u\r\n", (unsigned)settings_external_link_uart_baud());
        return 1U;
    }
    if(id == SETTINGS_APP_AUTOSTART)
    {
        const hk_app_t *app = value > 0 ? hk_app_autostart_at((uint8_t)value - 1U) : NULL;
        hk_autostart_id_t selected = app ? app->autostart_id : HK_AUTOSTART_OFF;

        if(selected == settings_autostart_id())
            return 0U;
        settings_set_autostart_id(selected);
        settings_mark_dirty(0U);
        printf("[SETTINGS] autostart=%u %s\r\n", (unsigned)selected,
               app ? app->title : "OFF");
        return 1U;
    }
#if HK_ENABLE_DRISTY
    if(id == SETTINGS_APP_HOST_STREAM &&
       value != dristy_uart_bridge_ctrl_stream_active())
    {
        dristy_uart_bridge_set_ctrl_stream((uint8_t)value);
        printf("[DRISTY] host_stream=%u\r\n", (unsigned)value);
        return 1U;
    }
    if(id == SETTINGS_APP_DETECT_CONF)
    {
        dristy_pipeline_set_confidence((uint8_t)value);
        printf("[DRISTY] confidence=%ld\r\n", (long)value);
        return 1U;
    }
#endif
    return 0U;
}

static uint8_t settings_app_format(void *context,
                                   uint16_t id,
                                   char *value,
                                   size_t value_size)
{
    (void)context;
    if(id == SETTINGS_APP_AUTO_SLEEP)
        snprintf(value, value_size, "%umin", settings_auto_sleep_minutes());
    else if(id == SETTINGS_APP_AUTOSTART &&
            settings_autostart_id() != HK_AUTOSTART_OFF &&
            !hk_app_for_autostart_id(settings_autostart_id()))
        snprintf(value, value_size, "UNAVAILABLE");
    else if(id == SETTINGS_APP_VERSION)
        snprintf(value, value_size, "v%s", DRISTY_FW_VERSION);
    else
        return 0U;
    return 1U;
}

static uint8_t settings_app_choice_count(void *context, uint16_t id)
{
    (void)context;
    return id == SETTINGS_APP_AUTOSTART ?
           (uint8_t)(hk_app_autostart_count() + 1U) : 0U;
}

static const char *settings_app_choice_label(void *context, uint16_t id, uint8_t index)
{
    const hk_app_t *app;

    (void)context;
    if(id != SETTINGS_APP_AUTOSTART)
        return NULL;
    if(index == 0U)
        return "OFF";
    app = hk_app_autostart_at(index - 1U);
    return app ? app->title : NULL;
}

static void settings_app_committed(void *context, uint16_t id)
{
    (void)context;
    settings_storage_save_now();
    printf("[SETTINGS] edit commit id=%u\r\n", (unsigned)id);
}

static const settings_menu_definition_t g_definition = {
    .title = "SETTINGS",
    .items = g_items,
    .item_count = (uint8_t)SETTINGS_APP_ITEM_COUNT,
    .read = settings_app_read,
    .write = settings_app_write,
    .format = settings_app_format,
    .choice_count = settings_app_choice_count,
    .choice_label = settings_app_choice_label,
    .committed = settings_app_committed,
};

const settings_menu_definition_t *settings_app_menu_definition(void)
{
    return &g_definition;
}

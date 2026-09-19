#include "object_detect_settings.h"

#include <string.h>

#include "../../core/camera_types.h"
#include "../../services/camera_light.h"
#include "../../services/camera_session_preferences.h"
#include "../../services/settings_app_data.h"
#include "../../services/settings_persistence.h"
#include "object_detect_detector.h"

#define OBJECT_DATA_SCHEMA 0U
#define OBJECT_DATA_FLAGS 1U
#define OBJECT_DATA_LIGHT 2U
#define OBJECT_DATA_RED 3U
#define OBJECT_DATA_GREEN 4U
#define OBJECT_DATA_BLUE 5U
#define OBJECT_DATA_CONFIDENCE 6U
#define OBJECT_DATA_NMS 7U

#define OBJECT_FLAG_FPS 0x01U

static object_detect_preferences_t g_preferences;
static uint8_t g_loaded;

static uint8_t clamp_percent(uint8_t value)
{
    return value <= 100U ? value : 100U;
}

static uint8_t clamp_confidence(uint8_t value)
{
    if(value < 10U)
        return 10U;
    return value > 90U ? 90U : value;
}

static uint8_t clamp_nms(uint8_t value)
{
    return value > 90U ? 90U : value;
}

static void object_detect_settings_defaults(void)
{
    memset(&g_preferences, 0, sizeof(g_preferences));
    g_preferences.fps_enabled = 1U;
    g_preferences.light_mode = CAMERA_LIGHT_LED;
    g_preferences.rgb_red = 100U;
    g_preferences.rgb_green = 100U;
    g_preferences.rgb_blue = 100U;
    g_preferences.confidence = OBJECT_DETECT_DEFAULT_CONFIDENCE;
    g_preferences.nms = OBJECT_DETECT_DEFAULT_NMS;
}

static void object_detect_settings_save(void)
{
    uint8_t data[SETTINGS_APP_DATA_SIZE];
    uint8_t *object_data = data + OBJECT_DETECT_SETTINGS_OFFSET;

    settings_app_data_read(data);
    memset(object_data, 0, OBJECT_DETECT_SETTINGS_BYTES);
    object_data[OBJECT_DATA_SCHEMA] = OBJECT_DETECT_SETTINGS_SCHEMA;
    object_data[OBJECT_DATA_FLAGS] =
        g_preferences.fps_enabled ? OBJECT_FLAG_FPS : 0U;
    object_data[OBJECT_DATA_LIGHT] = g_preferences.light_mode;
    object_data[OBJECT_DATA_RED] = g_preferences.rgb_red;
    object_data[OBJECT_DATA_GREEN] = g_preferences.rgb_green;
    object_data[OBJECT_DATA_BLUE] = g_preferences.rgb_blue;
    object_data[OBJECT_DATA_CONFIDENCE] = g_preferences.confidence;
    object_data[OBJECT_DATA_NMS] = g_preferences.nms;
    settings_app_data_write(data);
    settings_mark_dirty(0U);
}

void object_detect_settings_load(void)
{
    uint8_t data[SETTINGS_APP_DATA_SIZE];
    const uint8_t *object_data = data + OBJECT_DETECT_SETTINGS_OFFSET;

    if(g_loaded)
        return;
    g_loaded = 1U;
    object_detect_settings_defaults();
    settings_app_data_read(data);
    if(object_data[OBJECT_DATA_SCHEMA] != OBJECT_DETECT_SETTINGS_SCHEMA)
        return;
    g_preferences.fps_enabled =
        (object_data[OBJECT_DATA_FLAGS] & OBJECT_FLAG_FPS) ? 1U : 0U;
    g_preferences.light_mode = object_data[OBJECT_DATA_LIGHT] == CAMERA_LIGHT_RGB ?
                               CAMERA_LIGHT_RGB : CAMERA_LIGHT_LED;
    g_preferences.rgb_red = clamp_percent(object_data[OBJECT_DATA_RED]);
    g_preferences.rgb_green = clamp_percent(object_data[OBJECT_DATA_GREEN]);
    g_preferences.rgb_blue = clamp_percent(object_data[OBJECT_DATA_BLUE]);
    g_preferences.confidence =
        clamp_confidence(object_data[OBJECT_DATA_CONFIDENCE]);
    g_preferences.nms = clamp_nms(object_data[OBJECT_DATA_NMS]);
}

void object_detect_settings_apply_session(void)
{
    camera_session_preferences_t session;

    object_detect_settings_load();
    session.fps_enabled = g_preferences.fps_enabled;
    session.light_mode = (camera_light_mode_t)g_preferences.light_mode;
    session.rgb_red = g_preferences.rgb_red;
    session.rgb_green = g_preferences.rgb_green;
    session.rgb_blue = g_preferences.rgb_blue;
    camera_session_preferences_override(&session);
    object_detect_detector_set_thresholds(g_preferences.confidence,
                                          g_preferences.nms);
    camera_light_apply();
}

const object_detect_preferences_t *object_detect_settings_preferences(void)
{
    object_detect_settings_load();
    return &g_preferences;
}

uint8_t object_detect_settings_set_confidence(uint8_t value)
{
    object_detect_settings_load();
    value = clamp_confidence(value);
    if(g_preferences.confidence == value)
        return 0U;
    g_preferences.confidence = value;
    object_detect_settings_save();
    return 1U;
}

uint8_t object_detect_settings_set_nms(uint8_t value)
{
    object_detect_settings_load();
    value = clamp_nms(value);
    if(g_preferences.nms == value)
        return 0U;
    g_preferences.nms = value;
    object_detect_settings_save();
    return 1U;
}

uint8_t object_detect_settings_set_fps(uint8_t enabled)
{
    object_detect_settings_load();
    enabled = enabled ? 1U : 0U;
    if(g_preferences.fps_enabled == enabled)
        return 0U;
    g_preferences.fps_enabled = enabled;
    object_detect_settings_save();
    return 1U;
}

uint8_t object_detect_settings_set_light(uint8_t mode)
{
    object_detect_settings_load();
    mode = mode == CAMERA_LIGHT_RGB ? CAMERA_LIGHT_RGB : CAMERA_LIGHT_LED;
    if(g_preferences.light_mode == mode)
        return 0U;
    g_preferences.light_mode = mode;
    object_detect_settings_save();
    return 1U;
}

uint8_t object_detect_settings_set_rgb(uint8_t channel, uint8_t value)
{
    uint8_t *target;

    object_detect_settings_load();
    target = channel == 0U ? &g_preferences.rgb_red :
             (channel == 1U ? &g_preferences.rgb_green :
              &g_preferences.rgb_blue);
    value = clamp_percent(value);
    if(*target == value)
        return 0U;
    *target = value;
    object_detect_settings_save();
    return 1U;
}

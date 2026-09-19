#include "apriltag_settings.h"

#include <string.h>

#include "apriltag_config.h"
#include "apriltag_detector.h"
#include "../../core/camera_types.h"
#include "../../services/camera_light.h"
#include "../../services/camera_session_preferences.h"
#include "../../services/settings_app_data.h"
#include "../../services/settings_persistence.h"

#define APRILTAG_DATA_SCHEMA 0U
#define APRILTAG_DATA_FLAGS 1U
#define APRILTAG_DATA_LIGHT 2U
#define APRILTAG_DATA_RED 3U
#define APRILTAG_DATA_GREEN 4U
#define APRILTAG_DATA_BLUE 5U
#define APRILTAG_DATA_SELECTED 6U
#define APRILTAG_DATA_BYTES (APRILTAG_DATA_SELECTED + APRILTAG_SELECTED_BYTES)

#define APRILTAG_FLAG_REFINE 0x01U
#define APRILTAG_FLAG_SELECTED_OUTPUT 0x02U
#define APRILTAG_FLAG_FPS 0x04U

_Static_assert(APRILTAG_DATA_BYTES <= SETTINGS_APP_DATA_V2_SIZE,
               "APRILTAG settings overlap another app-data partition");

static apriltag_preferences_t g_preferences;
static uint8_t g_selected[APRILTAG_SELECTED_BYTES];
static uint8_t g_loaded;

static uint8_t clamp_percent(uint8_t value)
{
    return value <= 100U ? value : 100U;
}

static void apriltag_settings_defaults(void)
{
    memset(&g_preferences, 0, sizeof(g_preferences));
    memset(g_selected, 0, sizeof(g_selected));
    g_preferences.refine_edges = 0U;
    g_preferences.output_mode = APRILTAG_OUTPUT_ALL;
    g_preferences.fps_enabled = 1U;
    g_preferences.light_mode = CAMERA_LIGHT_LED;
    g_preferences.rgb_red = 100U;
    g_preferences.rgb_green = 100U;
    g_preferences.rgb_blue = 100U;
}

static void apriltag_settings_save(void)
{
    uint8_t data[SETTINGS_APP_DATA_SIZE];

    settings_app_data_read(data);
    memset(data, 0, APRILTAG_DATA_BYTES);
    data[APRILTAG_DATA_SCHEMA] = APRILTAG_SETTINGS_SCHEMA;
    data[APRILTAG_DATA_FLAGS] =
        (g_preferences.refine_edges ? APRILTAG_FLAG_REFINE : 0U) |
        (g_preferences.output_mode == APRILTAG_OUTPUT_SELECTED ? APRILTAG_FLAG_SELECTED_OUTPUT : 0U) |
        (g_preferences.fps_enabled ? APRILTAG_FLAG_FPS : 0U);
    data[APRILTAG_DATA_LIGHT] = g_preferences.light_mode;
    data[APRILTAG_DATA_RED] = g_preferences.rgb_red;
    data[APRILTAG_DATA_GREEN] = g_preferences.rgb_green;
    data[APRILTAG_DATA_BLUE] = g_preferences.rgb_blue;
    memcpy(data + APRILTAG_DATA_SELECTED, g_selected, sizeof(g_selected));
    settings_app_data_write(data);
    settings_mark_dirty(0U);
}

void apriltag_settings_load(void)
{
    uint8_t data[SETTINGS_APP_DATA_SIZE];
    uint8_t flags;

    if(g_loaded)
        return;
    g_loaded = 1U;
    apriltag_settings_defaults();
    settings_app_data_read(data);
    if(data[APRILTAG_DATA_SCHEMA] != APRILTAG_SETTINGS_SCHEMA)
        return;

    flags = data[APRILTAG_DATA_FLAGS];
    g_preferences.refine_edges = (flags & APRILTAG_FLAG_REFINE) ? 1U : 0U;
    g_preferences.output_mode = (flags & APRILTAG_FLAG_SELECTED_OUTPUT) ?
                                APRILTAG_OUTPUT_SELECTED : APRILTAG_OUTPUT_ALL;
    g_preferences.fps_enabled = (flags & APRILTAG_FLAG_FPS) ? 1U : 0U;
    g_preferences.light_mode = data[APRILTAG_DATA_LIGHT] == CAMERA_LIGHT_RGB ?
                               CAMERA_LIGHT_RGB : CAMERA_LIGHT_LED;
    g_preferences.rgb_red = clamp_percent(data[APRILTAG_DATA_RED]);
    g_preferences.rgb_green = clamp_percent(data[APRILTAG_DATA_GREEN]);
    g_preferences.rgb_blue = clamp_percent(data[APRILTAG_DATA_BLUE]);
    memcpy(g_selected, data + APRILTAG_DATA_SELECTED, sizeof(g_selected));
    g_selected[APRILTAG_SELECTED_BYTES - 1U] &= 0x07U;
}

void apriltag_settings_apply_session(void)
{
    camera_session_preferences_t session;

    apriltag_settings_load();
    session.fps_enabled = g_preferences.fps_enabled;
    session.light_mode = (camera_light_mode_t)g_preferences.light_mode;
    session.rgb_red = g_preferences.rgb_red;
    session.rgb_green = g_preferences.rgb_green;
    session.rgb_blue = g_preferences.rgb_blue;
    camera_session_preferences_override(&session);
    apriltag_detector_set_refine_edges(g_preferences.refine_edges);
    camera_light_apply();
}

const apriltag_preferences_t *apriltag_settings_preferences(void)
{
    apriltag_settings_load();
    return &g_preferences;
}

uint8_t apriltag_settings_selected(uint16_t id)
{
    apriltag_settings_load();
    if(id >= APRILTAG_FAMILY_ID_COUNT)
        return 0U;
    return (g_selected[id >> 3U] & (uint8_t)(1U << (id & 7U))) ? 1U : 0U;
}

uint8_t apriltag_settings_toggle_selected(uint16_t id)
{
    uint8_t mask;

    apriltag_settings_load();
    if(id >= APRILTAG_FAMILY_ID_COUNT)
        return 0U;
    mask = (uint8_t)(1U << (id & 7U));
    g_selected[id >> 3U] ^= mask;
    apriltag_settings_save();
    return (g_selected[id >> 3U] & mask) ? 1U : 0U;
}

uint8_t apriltag_settings_clear_selected(void)
{
    apriltag_settings_load();
    if(apriltag_settings_selected_count() == 0U)
        return 0U;
    memset(g_selected, 0, sizeof(g_selected));
    apriltag_settings_save();
    return 1U;
}

uint16_t apriltag_settings_selected_count(void)
{
    uint16_t count = 0U;

    apriltag_settings_load();
    for(uint16_t id = 0U; id < APRILTAG_FAMILY_ID_COUNT; id++)
        count += apriltag_settings_selected(id);
    return count;
}

uint8_t apriltag_settings_set_refine(uint8_t enabled)
{
    apriltag_settings_load();
    enabled = enabled ? 1U : 0U;
    if(g_preferences.refine_edges == enabled)
        return 0U;
    g_preferences.refine_edges = enabled;
    apriltag_settings_save();
    return 1U;
}

uint8_t apriltag_settings_set_output(apriltag_output_mode_t mode)
{
    apriltag_settings_load();
    mode = mode == APRILTAG_OUTPUT_SELECTED ? APRILTAG_OUTPUT_SELECTED : APRILTAG_OUTPUT_ALL;
    if(g_preferences.output_mode == mode)
        return 0U;
    g_preferences.output_mode = mode;
    apriltag_settings_save();
    return 1U;
}

uint8_t apriltag_settings_set_fps(uint8_t enabled)
{
    apriltag_settings_load();
    enabled = enabled ? 1U : 0U;
    if(g_preferences.fps_enabled == enabled)
        return 0U;
    g_preferences.fps_enabled = enabled;
    apriltag_settings_save();
    return 1U;
}

uint8_t apriltag_settings_set_light(uint8_t mode)
{
    apriltag_settings_load();
    mode = mode == CAMERA_LIGHT_RGB ? CAMERA_LIGHT_RGB : CAMERA_LIGHT_LED;
    if(g_preferences.light_mode == mode)
        return 0U;
    g_preferences.light_mode = mode;
    apriltag_settings_save();
    return 1U;
}

uint8_t apriltag_settings_set_rgb(uint8_t channel, uint8_t value)
{
    uint8_t *target = channel == 0U ? &g_preferences.rgb_red :
                      (channel == 1U ? &g_preferences.rgb_green : &g_preferences.rgb_blue);

    apriltag_settings_load();
    value = clamp_percent(value);
    if(*target == value)
        return 0U;
    *target = value;
    apriltag_settings_save();
    return 1U;
}

const char *apriltag_settings_output_label(void)
{
    return apriltag_settings_preferences()->output_mode == APRILTAG_OUTPUT_SELECTED ?
           "SELECTED" : "ALL";
}

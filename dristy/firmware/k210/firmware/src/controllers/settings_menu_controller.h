#ifndef HK_SETTINGS_MENU_CONTROLLER_H
#define HK_SETTINGS_MENU_CONTROLLER_H

#include <stddef.h>
#include <stdint.h>

#include "../core/hk_app.h"

typedef enum
{
    SETTINGS_MENU_ITEM_TOGGLE = 0,
    SETTINGS_MENU_ITEM_CHOICE,
    SETTINGS_MENU_ITEM_RANGE,
    SETTINGS_MENU_ITEM_ACTION,
    SETTINGS_MENU_ITEM_TEXT,
} settings_menu_item_kind_t;

typedef enum
{
    SETTINGS_MENU_CYCLE_ON_OK = 0,
    SETTINGS_MENU_EDIT_ON_OK,
} settings_menu_interaction_t;

typedef enum
{
    SETTINGS_MENU_EVENT_NONE = 0,
    SETTINGS_MENU_EVENT_CLOSE_REQUESTED,
} settings_menu_event_t;

enum
{
    SETTINGS_MENU_ITEM_WRAP = 0x01U,
};

typedef struct
{
    uint16_t id;
    const char *title;
    settings_menu_item_kind_t kind;
    settings_menu_interaction_t interaction;
    int32_t minimum;
    int32_t maximum;
    int32_t step;
    const char *const *choices;
    uint8_t choice_count;
    uint8_t flags;
} settings_menu_item_t;

typedef int32_t (*settings_menu_read_fn)(void *context, uint16_t id);
typedef uint8_t (*settings_menu_write_fn)(void *context, uint16_t id, int32_t value);
typedef uint8_t (*settings_menu_action_fn)(void *context, uint16_t id);
typedef uint8_t (*settings_menu_format_fn)(void *context,
                                           uint16_t id,
                                           char *value,
                                           size_t value_size);
typedef uint8_t (*settings_menu_choice_count_fn)(void *context, uint16_t id);
typedef const char *(*settings_menu_choice_label_fn)(void *context,
                                                     uint16_t id,
                                                     uint8_t index);
typedef void (*settings_menu_changed_fn)(void *context, uint16_t id);
typedef void (*settings_menu_committed_fn)(void *context, uint16_t id);

typedef struct
{
    const char *title;
    const settings_menu_item_t *items;
    uint8_t item_count;
    void *context;
    settings_menu_read_fn read;
    settings_menu_write_fn write;
    settings_menu_action_fn action;
    settings_menu_format_fn format;
    settings_menu_choice_count_fn choice_count;
    settings_menu_choice_label_fn choice_label;
    settings_menu_changed_fn changed;
    settings_menu_committed_fn committed;
} settings_menu_definition_t;

typedef struct
{
    const settings_menu_definition_t *definition;
    uint8_t index;
    uint8_t top;
    uint8_t active;
    uint8_t editing;
    uint32_t repeat_button;
    uint8_t repeat_ticks;
} settings_menu_session_t;

uint8_t settings_menu_open(settings_menu_session_t *session,
                           const settings_menu_definition_t *definition);
void settings_menu_close(settings_menu_session_t *session);
uint8_t settings_menu_active(const settings_menu_session_t *session);
settings_menu_event_t settings_menu_handle_input(settings_menu_session_t *session,
                                                 const hk_input_snapshot_t *input);
void settings_menu_tick(settings_menu_session_t *session, const hk_input_snapshot_t *input);
void settings_menu_redraw_item(settings_menu_session_t *session, uint16_t id);
void settings_menu_redraw_all(settings_menu_session_t *session);

#endif

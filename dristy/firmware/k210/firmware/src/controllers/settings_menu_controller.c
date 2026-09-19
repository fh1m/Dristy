#include "settings_menu_controller.h"

#include <stdio.h>
#include <string.h>

#include "../config/input_config.h"
#include "../config/settings_menu_layout.h"
#include "../ui/settings_menu_view.h"

static const settings_menu_item_t *settings_menu_current(const settings_menu_session_t *session)
{
    if(!session || !session->active || !session->definition ||
       session->index >= session->definition->item_count)
        return NULL;
    return &session->definition->items[session->index];
}

static void settings_menu_repeat_reset(settings_menu_session_t *session)
{
    session->repeat_button = 0U;
    session->repeat_ticks = 0U;
}

static void settings_menu_repeat_start(settings_menu_session_t *session, uint32_t button)
{
    session->repeat_button = button;
    session->repeat_ticks = SETTINGS_MENU_REPEAT_INITIAL_TICKS;
}

static void settings_menu_ensure_visible(settings_menu_session_t *session)
{
    uint8_t item_count = session->definition->item_count;

    if(session->index < session->top)
        session->top = session->index;
    else if(session->index >= session->top + SETTINGS_MENU_VISIBLE_ROWS)
        session->top = (uint8_t)(session->index - SETTINGS_MENU_VISIBLE_ROWS + 1U);
    if(session->top + SETTINGS_MENU_VISIBLE_ROWS > item_count)
        session->top = item_count > SETTINGS_MENU_VISIBLE_ROWS ?
                       item_count - SETTINGS_MENU_VISIBLE_ROWS : 0U;
}

static int32_t settings_menu_read_value(const settings_menu_session_t *session,
                                        const settings_menu_item_t *item)
{
    if(!session->definition->read)
        return 0;
    return session->definition->read(session->definition->context, item->id);
}

static uint8_t settings_menu_choice_count(const settings_menu_session_t *session,
                                          const settings_menu_item_t *item)
{
    if(session->definition->choice_count)
    {
        uint8_t count = session->definition->choice_count(session->definition->context, item->id);
        if(count != 0U)
            return count;
    }
    return item->choice_count;
}

static const char *settings_menu_choice_label(const settings_menu_session_t *session,
                                              const settings_menu_item_t *item,
                                              uint8_t index)
{
    if(session->definition->choice_label)
    {
        const char *label = session->definition->choice_label(session->definition->context,
                                                              item->id, index);
        if(label)
            return label;
    }
    if(item->choices && index < item->choice_count)
        return item->choices[index];
    return NULL;
}

static void settings_menu_format_value(const settings_menu_session_t *session,
                                       const settings_menu_item_t *item,
                                       char *value,
                                       size_t value_size)
{
    int32_t current;

    value[0] = '\0';
    if(session->definition->format &&
       session->definition->format(session->definition->context,
                                   item->id, value, value_size))
        return;

    current = settings_menu_read_value(session, item);
    if(item->kind == SETTINGS_MENU_ITEM_TOGGLE)
        snprintf(value, value_size, "%s", current ? "ON" : "OFF");
    else if(item->kind == SETTINGS_MENU_ITEM_CHOICE)
    {
        uint8_t count = settings_menu_choice_count(session, item);
        const char *label = current >= 0 && current < count ?
                            settings_menu_choice_label(session, item, (uint8_t)current) : NULL;
        if(label)
            snprintf(value, value_size, "%s", label);
    }
    else if(item->kind == SETTINGS_MENU_ITEM_RANGE)
        snprintf(value, value_size, "%ld", (long)current);
}

static void settings_menu_draw_index(settings_menu_session_t *session, uint8_t index)
{
    const settings_menu_item_t *item;
    char value[SETTINGS_MENU_VALUE_SIZE];

    if(index < session->top || index >= session->top + SETTINGS_MENU_VISIBLE_ROWS ||
       index >= session->definition->item_count)
        return;
    item = &session->definition->items[index];
    settings_menu_format_value(session, item, value, sizeof(value));
    settings_menu_view_draw_row((uint8_t)(index - session->top),
                                item->title,
                                value,
                                index == session->index,
                                index == session->index && session->editing);
}

void settings_menu_redraw_all(settings_menu_session_t *session)
{
    if(!session || !session->active || !session->definition)
        return;
    settings_menu_ensure_visible(session);
    settings_menu_view_clear_rows();
    for(uint8_t slot = 0U; slot < SETTINGS_MENU_VISIBLE_ROWS; slot++)
    {
        uint8_t index = (uint8_t)(session->top + slot);
        if(index < session->definition->item_count)
            settings_menu_draw_index(session, index);
    }
}

void settings_menu_redraw_item(settings_menu_session_t *session, uint16_t id)
{
    if(!session || !session->active || !session->definition)
        return;
    for(uint8_t index = 0U; index < session->definition->item_count; index++)
    {
        if(session->definition->items[index].id == id)
        {
            settings_menu_draw_index(session, index);
            return;
        }
    }
}

static void settings_menu_finish_edit(settings_menu_session_t *session, uint8_t redraw)
{
    const settings_menu_item_t *item;

    if(!session || !session->active || !session->editing)
        return;
    item = settings_menu_current(session);
    session->editing = 0U;
    settings_menu_repeat_reset(session);
    if(item && session->definition->committed)
        session->definition->committed(session->definition->context, item->id);
    if(redraw)
        settings_menu_draw_index(session, session->index);
}

uint8_t settings_menu_open(settings_menu_session_t *session,
                           const settings_menu_definition_t *definition)
{
    if(!session || !definition || !definition->items || definition->item_count == 0U)
        return 0U;
    if(session->active)
        settings_menu_close(session);
    memset(session, 0, sizeof(*session));
    session->definition = definition;
    session->active = 1U;
    settings_menu_view_open(definition->title);
    settings_menu_redraw_all(session);
    return 1U;
}

void settings_menu_close(settings_menu_session_t *session)
{
    if(!session)
        return;
    settings_menu_finish_edit(session, 0U);
    session->active = 0U;
    settings_menu_repeat_reset(session);
}

uint8_t settings_menu_active(const settings_menu_session_t *session)
{
    return session && session->active ? 1U : 0U;
}

static void settings_menu_select_delta(settings_menu_session_t *session, int8_t delta)
{
    uint8_t previous = session->index;
    uint8_t previous_top = session->top;
    uint8_t item_count = session->definition->item_count;

    if(delta < 0)
        session->index = session->index == 0U ? item_count - 1U : session->index - 1U;
    else
        session->index = (uint8_t)((session->index + 1U) % item_count);
    settings_menu_ensure_visible(session);
    if(previous_top != session->top)
        settings_menu_redraw_all(session);
    else
    {
        settings_menu_draw_index(session, previous);
        settings_menu_draw_index(session, session->index);
    }
}

static int32_t settings_menu_adjusted_value(const settings_menu_session_t *session,
                                            const settings_menu_item_t *item,
                                            int32_t current,
                                            int8_t delta)
{
    int32_t minimum = item->kind == SETTINGS_MENU_ITEM_CHOICE ? 0 : item->minimum;
    int32_t maximum = item->kind == SETTINGS_MENU_ITEM_CHOICE ?
                      (int32_t)settings_menu_choice_count(session, item) - 1 : item->maximum;
    int32_t step = item->kind == SETTINGS_MENU_ITEM_CHOICE ? 1 : item->step;

    if(step <= 0 || maximum < minimum)
        return current;
    if(current < minimum)
        current = minimum;
    else if(current > maximum)
        current = maximum;
    if(delta < 0)
    {
        if(current < minimum + step)
            return (item->flags & SETTINGS_MENU_ITEM_WRAP) ? maximum : minimum;
        return current - step;
    }
    if(current > maximum - step)
        return (item->flags & SETTINGS_MENU_ITEM_WRAP) ? minimum : maximum;
    return current + step;
}

static uint8_t settings_menu_write_value(settings_menu_session_t *session,
                                         const settings_menu_item_t *item,
                                         int32_t value)
{
    uint8_t changed;

    if(!session->definition->write)
        return 0U;
    changed = session->definition->write(session->definition->context, item->id, value);
    if(changed && session->definition->changed)
        session->definition->changed(session->definition->context, item->id);
    if(changed)
        settings_menu_draw_index(session, session->index);
    return changed;
}

static void settings_menu_adjust(settings_menu_session_t *session, int8_t delta)
{
    const settings_menu_item_t *item = settings_menu_current(session);
    int32_t current;
    int32_t next;

    if(!item || (item->kind != SETTINGS_MENU_ITEM_CHOICE &&
                 item->kind != SETTINGS_MENU_ITEM_RANGE))
        return;
    current = settings_menu_read_value(session, item);
    next = settings_menu_adjusted_value(session, item, current, delta);
    if(next != current)
        settings_menu_write_value(session, item, next);
}

static void settings_menu_activate(settings_menu_session_t *session)
{
    const settings_menu_item_t *item = settings_menu_current(session);
    uint8_t changed;

    if(!item || item->kind == SETTINGS_MENU_ITEM_TEXT)
        return;
    if(item->kind == SETTINGS_MENU_ITEM_ACTION)
    {
        changed = session->definition->action ?
                  session->definition->action(session->definition->context, item->id) : 0U;
        if(changed && session->definition->changed)
            session->definition->changed(session->definition->context, item->id);
        settings_menu_draw_index(session, session->index);
        return;
    }
    if(item->interaction == SETTINGS_MENU_EDIT_ON_OK &&
       (item->kind == SETTINGS_MENU_ITEM_CHOICE || item->kind == SETTINGS_MENU_ITEM_RANGE))
    {
        session->editing = 1U;
        settings_menu_repeat_reset(session);
        settings_menu_draw_index(session, session->index);
        return;
    }
    if(item->kind == SETTINGS_MENU_ITEM_TOGGLE)
        settings_menu_write_value(session, item, settings_menu_read_value(session, item) ? 0 : 1);
    else
        settings_menu_adjust(session, 1);
}

settings_menu_event_t settings_menu_handle_input(settings_menu_session_t *session,
                                                 const hk_input_snapshot_t *input)
{
    if(!session || !session->active || !input)
        return SETTINGS_MENU_EVENT_NONE;
    if(input->pressed & BUTTON_BACK)
    {
        if(session->editing)
        {
            settings_menu_finish_edit(session, 1U);
            return SETTINGS_MENU_EVENT_NONE;
        }
        return SETTINGS_MENU_EVENT_CLOSE_REQUESTED;
    }
    if(input->pressed & BUTTON_OK)
    {
        if(session->editing)
            settings_menu_finish_edit(session, 1U);
        else
            settings_menu_activate(session);
        return SETTINGS_MENU_EVENT_NONE;
    }
    if(session->editing)
    {
        if(input->pressed & BUTTON_LEFT)
        {
            settings_menu_adjust(session, -1);
            settings_menu_repeat_start(session, BUTTON_LEFT);
        }
        else if(input->pressed & BUTTON_RIGHT)
        {
            settings_menu_adjust(session, 1);
            settings_menu_repeat_start(session, BUTTON_RIGHT);
        }
        return SETTINGS_MENU_EVENT_NONE;
    }
    if(input->pressed & BUTTON_LEFT)
        settings_menu_select_delta(session, -1);
    else if(input->pressed & BUTTON_RIGHT)
        settings_menu_select_delta(session, 1);
    return SETTINGS_MENU_EVENT_NONE;
}

void settings_menu_tick(settings_menu_session_t *session, const hk_input_snapshot_t *input)
{
    if(!session || !session->active || !session->editing || !input)
        return;
    if(session->repeat_button == 0U || !(input->state & session->repeat_button))
    {
        if(input->state & BUTTON_LEFT)
            settings_menu_repeat_start(session, BUTTON_LEFT);
        else if(input->state & BUTTON_RIGHT)
            settings_menu_repeat_start(session, BUTTON_RIGHT);
        else
            settings_menu_repeat_reset(session);
        return;
    }
    if(session->repeat_ticks > 0U)
    {
        session->repeat_ticks--;
        return;
    }
    settings_menu_adjust(session, session->repeat_button == BUTTON_LEFT ? -1 : 1);
    session->repeat_ticks = SETTINGS_MENU_REPEAT_NEXT_TICKS;
}

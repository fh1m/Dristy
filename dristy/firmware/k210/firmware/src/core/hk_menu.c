#include "hk_menu.h"

#include <stdio.h>
#include <string.h>

#include "hk_app.h"

#include "../config/input_config.h"
#include "../config/menu_layout.h"
#include "../ui/dristy_app_catalog.h"
#include "../ui/dristy_mode_menu.h"
#include "../ui/dristy_ui.h"
#include "../ui/hk_ui.h"

#include "hk_app_registry.h"
#include "hk_back_exit.h"
#include "hk_config.h"
#include "hk_screen.h"
#if HK_ENABLE_DRISTY
#include "../apps/vision_mode/vision_mode_app.h"
#endif

static uint8_t s_menu_index;
static uint8_t s_scroll_offset;
static uint32_t s_menu_repeat_button;
static uint8_t s_menu_repeat_ticks;
static hk_menu_view_t s_menu_view;
static hk_menu_owner_hooks_t s_owner_hooks;

static void menu_row_at(uint8_t display_index, dristy_menu_row_t *row)
{
    dristy_menu_row_at(display_index, row);
}

static void menu_scroll_ensure_visible(void)
{
    uint8_t total = dristy_menu_sorted_count();

    if(s_menu_index < s_scroll_offset)
        s_scroll_offset = s_menu_index;
    if(s_menu_index >= s_scroll_offset + MENU_LIST_VISIBLE_ROWS)
        s_scroll_offset = (uint8_t)(s_menu_index + 1U - MENU_LIST_VISIBLE_ROWS);
    if(total > MENU_LIST_VISIBLE_ROWS &&
       s_scroll_offset > total - MENU_LIST_VISIBLE_ROWS)
        s_scroll_offset = (uint8_t)(total - MENU_LIST_VISIBLE_ROWS);
}

static const char *menu_selected_title(void)
{
    dristy_menu_row_t row;

    menu_row_at(s_menu_index, &row);
    return row.title ? row.title : "";
}

static void menu_redraw_list(void)
{
    dristy_menu_row_t row;

    menu_row_at(s_menu_index, &row);
    menu_draw_chrome_ex(row.title, row.subtitle);
    menu_draw_list_viewport(s_scroll_offset, s_menu_index);
}

void menu_view_set(const hk_menu_view_t *view)
{
    if(view)
        s_menu_view = *view;
    else
        memset(&s_menu_view, 0, sizeof(s_menu_view));
}

uint8_t hk_menu_index_get(void)
{
    return s_menu_index;
}

void menu_render(void)
{
    hk_back_exit_set_armed(0);
    menu_scroll_ensure_visible();
    menu_redraw_list();
}

void shell_show_menu(void)
{
    const hk_app_t *app = hk_app_for_screen(hk_screen_get());

    if(app && app->exit)
        app->exit();
    if(s_owner_hooks.exit)
        s_owner_hooks.exit(app);
    hk_screen_set(SCREEN_MENU);
    hk_back_exit_set_armed(0);
    menu_render();
    activity_note();
    printf("[SHELL] screen MENU item=%s\r\n", menu_selected_title());
}

uint8_t shell_open_app(const hk_app_t *app, const hk_input_snapshot_t *input)
{
    uint8_t reg;

    if(!app || !app->enter)
        return 0U;
#if HK_ENABLE_DRISTY
    if(app == &g_vision_mode_app)
    {
        s_menu_repeat_button = 0;
        s_menu_repeat_ticks = 0;
        printf("[MENU] open %s\r\n", app->title);
        if(s_owner_hooks.enter && !s_owner_hooks.enter(app))
            return 0U;
        app->enter(input);
        return 1U;
    }
#endif
    for(reg = 0U; reg < g_menu_item_count; reg++)
    {
        if(&g_menu_items[reg] == app)
            break;
    }
    if(reg >= g_menu_item_count)
        return 0U;
    s_menu_index = dristy_menu_display_index_for_reg(reg);
    s_menu_repeat_button = 0;
    s_menu_repeat_ticks = 0;
    menu_scroll_ensure_visible();
    printf("[MENU] open %s\r\n", app->title);
    if(s_owner_hooks.enter && !s_owner_hooks.enter(app))
        return 0U;
    app->enter(input);
    return 1U;
}

void menu_owner_hooks_set(const hk_menu_owner_hooks_t *hooks)
{
    if(hooks)
        s_owner_hooks = *hooks;
    else
        memset(&s_owner_hooks, 0, sizeof(s_owner_hooks));
}

void shell_open_selected(const hk_input_snapshot_t *input)
{
    (void)dristy_menu_open_row(s_menu_index, input);
}

void menu_select_delta(int8_t delta)
{
    uint8_t total = dristy_menu_sorted_count();
    uint8_t previous = s_menu_index;

    if(total == 0U)
        return;
    if(delta < 0)
        s_menu_index = s_menu_index == 0 ? (uint8_t)(total - 1U) : (uint8_t)(s_menu_index - 1U);
    else if(delta > 0)
        s_menu_index = (uint8_t)((s_menu_index + 1U) % total);

    if(previous != s_menu_index)
    {
        menu_scroll_ensure_visible();
        menu_redraw_list();
        if(delta < 0)
            menu_footer_highlight_button(DRISTY_FOOTER_BTN_LEFT);
        else
            menu_footer_highlight_button(DRISTY_FOOTER_BTN_RIGHT);
    }
    printf("[MENU] select %s\r\n", menu_selected_title());
}

void menu_page_delta(int8_t pages)
{
    uint8_t total = dristy_menu_sorted_count();
    int16_t next = (int16_t)s_menu_index +
                   (int16_t)pages * (int16_t)MENU_LIST_VISIBLE_ROWS;

    if(total == 0U)
        return;
    while(next < 0)
        next += total;
    while(next >= total)
        next -= total;
    s_menu_index = (uint8_t)next;
    menu_scroll_ensure_visible();
    menu_redraw_list();
    menu_footer_highlight_button(DRISTY_FOOTER_BTN_BACK);
    printf("[MENU] select %s\r\n", menu_selected_title());
}

void menu_select_delta_cols(int8_t cols)
{
    (void)cols;
    menu_page_delta(1);
}

void menu_select_vertical(void)
{
    menu_page_delta(1);
}

void menu_repeat_reset(void)
{
    s_menu_repeat_button = 0;
    s_menu_repeat_ticks = 0;
}

void menu_repeat_start(uint32_t button)
{
    s_menu_repeat_button = button;
    s_menu_repeat_ticks = MENU_REPEAT_INITIAL_TICKS;
}

void menu_tick(const hk_input_snapshot_t *input)
{
    uint32_t buttons = input->state;

    if(hk_screen_get() != SCREEN_MENU)
    {
        menu_repeat_reset();
        return;
    }

    if(s_menu_repeat_button == 0 || !(buttons & s_menu_repeat_button))
    {
        if(buttons & BUTTON_LEFT)
            menu_repeat_start(BUTTON_LEFT);
        else if(buttons & BUTTON_RIGHT)
            menu_repeat_start(BUTTON_RIGHT);
        else if(buttons & BUTTON_BACK)
            menu_repeat_start(BUTTON_BACK);
        else if((buttons & BUTTON_OK) && (buttons & BUTTON_RIGHT))
            menu_repeat_start(BUTTON_OK | BUTTON_RIGHT);
        else
            menu_repeat_reset();
        return;
    }

    if(s_menu_repeat_ticks > 0)
    {
        s_menu_repeat_ticks--;
        return;
    }

    if(s_menu_repeat_button == BUTTON_LEFT)
        menu_select_delta(-1);
    else if(s_menu_repeat_button == BUTTON_RIGHT)
        menu_select_delta(1);
    else if(s_menu_repeat_button == BUTTON_BACK)
        menu_page_delta(-1);
    else if(s_menu_repeat_button == (BUTTON_OK | BUTTON_RIGHT))
        menu_page_delta(1);
    s_menu_repeat_ticks = MENU_REPEAT_NEXT_TICKS;
}

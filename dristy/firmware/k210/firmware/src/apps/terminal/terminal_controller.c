#include "terminal_controller.h"

#include "../../config/display_config.h"
#include "../../config/input_config.h"
#include "hk_config.h"
#include "../../core/hk_log.h"
#include "../../core/hk_menu.h"
#include "../../core/hk_screen.h"
#include "../../services/settings_persistence.h"
#include "../../services/settings_service.h"
#include "terminal_buffer.h"
#include "terminal_config.h"
#include "terminal_view.h"

static terminal_font_size_t g_font_size;
static uint32_t g_repeat_button;
static uint16_t g_repeat_ticks;
static uint16_t g_ok_hold_ticks;
static uint8_t g_ok_active;
static uint8_t g_ok_hold_fired;
static uint8_t g_buffer_initialized;
static uint8_t g_terminal_dirty;

static void terminal_render(void)
{
    terminal_view_render(g_font_size);
}

static void terminal_log_sink(const char *message)
{
    terminal_buffer_write(message);
    g_terminal_dirty = 1U;
}

static void terminal_repeat_reset(void)
{
    g_repeat_button = 0U;
    g_repeat_ticks = 0U;
}

static void terminal_repeat_start(uint32_t button)
{
    g_repeat_button = button;
    g_repeat_ticks = TERMINAL_REPEAT_INITIAL_TICKS;
}

static void terminal_scroll(uint32_t button)
{
    if(button == BUTTON_LEFT)
        terminal_buffer_scroll_up();
    else if(button == BUTTON_RIGHT)
        terminal_buffer_scroll_down();
    g_terminal_dirty = 1U;
}

static void terminal_toggle_font(void)
{
    terminal_geometry_t geometry;
    uint8_t flags = settings_feature_flags();

    g_font_size = g_font_size == TERMINAL_FONT_NORMAL ? TERMINAL_FONT_SMALL : TERMINAL_FONT_NORMAL;
    geometry = terminal_view_geometry(g_font_size);
    terminal_buffer_set_geometry(geometry.columns, geometry.rows);
    if(g_font_size == TERMINAL_FONT_SMALL)
        flags |= TERMINAL_SETTINGS_FONT_SMALL_FLAG;
    else
        flags &= (uint8_t)~TERMINAL_SETTINGS_FONT_SMALL_FLAG;
    settings_set_feature_flags(flags);
    settings_mark_dirty(1U);
    g_terminal_dirty = 1U;
    shell_printf("[TERM] font %s\r\n", g_font_size == TERMINAL_FONT_SMALL ? "SMALL" : "NORMAL");
}

void terminal_controller_enter(const hk_input_snapshot_t *input)
{
    terminal_geometry_t geometry;
    uint8_t first_init = 0U;

    (void)input;
    g_font_size = (settings_feature_flags() & TERMINAL_SETTINGS_FONT_SMALL_FLAG) ?
                  TERMINAL_FONT_SMALL : TERMINAL_FONT_NORMAL;
    geometry = terminal_view_geometry(g_font_size);
    if(!g_buffer_initialized)
    {
        terminal_buffer_init(geometry.columns, geometry.rows);
        g_buffer_initialized = 1U;
        first_init = 1U;
    }
    else
        terminal_buffer_set_geometry(geometry.columns, geometry.rows);

    terminal_repeat_reset();
    g_ok_hold_ticks = 0U;
    g_ok_active = 0U;
    g_ok_hold_fired = 0U;
    shell_log_set_sink(terminal_log_sink);
    hk_screen_set(HK_TERMINAL_SCREEN);
    g_terminal_dirty = 1U;
    if(first_init)
    {
        shell_printf("[BOOT] Dristy debug terminal\r\n");
        shell_printf("[BOOT] Dristy %s\r\n", DRISTY_VERSION);
        shell_printf("[DISPLAY] capability minimum %ux%u\r\n",
                     (unsigned)HK_DISPLAY_REQUIRED_WIDTH,
                     (unsigned)HK_DISPLAY_REQUIRED_HEIGHT);
        shell_printf("[TERM] LEFT/RIGHT scroll, OK latest\r\n");
        shell_printf("[TERM] hold OK toggles font, BACK menu\r\n");
    }
    else
        shell_printf("[TERM] opened\r\n");
}

void terminal_controller_exit(void)
{
    shell_log_set_sink(NULL);
    terminal_repeat_reset();
    g_ok_active = 0U;
    g_ok_hold_fired = 0U;
}

void terminal_controller_tick(const hk_input_snapshot_t *input)
{
    if(g_repeat_button != 0U)
    {
        if(!(input->state & g_repeat_button))
            terminal_repeat_reset();
        else if(g_repeat_ticks > 0U)
            g_repeat_ticks--;
        else
        {
            terminal_scroll(g_repeat_button);
            g_repeat_ticks = TERMINAL_REPEAT_NEXT_TICKS;
        }
    }

    if(g_ok_active && (input->state & BUTTON_OK) && !g_ok_hold_fired)
    {
        if(g_ok_hold_ticks < TERMINAL_OK_HOLD_TICKS)
            g_ok_hold_ticks++;
        if(g_ok_hold_ticks >= TERMINAL_OK_HOLD_TICKS)
        {
            g_ok_hold_fired = 1U;
            terminal_toggle_font();
        }
    }

    if(g_terminal_dirty)
    {
        g_terminal_dirty = 0U;
        terminal_render();
    }
}

void terminal_controller_handle_input(const hk_input_snapshot_t *input)
{
    if(input->pressed & BUTTON_BACK)
    {
        shell_show_menu();
        return;
    }
    if(input->pressed & BUTTON_LEFT)
    {
        terminal_scroll(BUTTON_LEFT);
        terminal_repeat_start(BUTTON_LEFT);
    }
    if(input->pressed & BUTTON_RIGHT)
    {
        terminal_scroll(BUTTON_RIGHT);
        terminal_repeat_start(BUTTON_RIGHT);
    }
    if(input->pressed & BUTTON_OK)
    {
        g_ok_active = 1U;
        g_ok_hold_ticks = 0U;
        g_ok_hold_fired = 0U;
    }
    if((input->changed & BUTTON_OK) && !(input->state & BUTTON_OK) && g_ok_active)
    {
        if(!g_ok_hold_fired)
        {
            terminal_buffer_follow_latest();
            g_terminal_dirty = 1U;
        }
        g_ok_active = 0U;
        g_ok_hold_ticks = 0U;
        g_ok_hold_fired = 0U;
    }
}

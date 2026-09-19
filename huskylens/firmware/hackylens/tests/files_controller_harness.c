#include <stdint.h>
#include <stdio.h>

#include <hackylens/capability/time.h>

#include "../firmware/src/apps/files/files_controller.h"
#include "../firmware/src/apps/files/file_browser_mode.h"
#include "../firmware/src/apps/files/file_result.h"
#include "../firmware/src/apps/files/image_decode.h"
#include "../firmware/src/config/input_config.h"

static file_browser_mode_t g_mode;
static uint64_t g_now_us;
static uint8_t g_time_fails;
static uint64_t g_requested_features;
static unsigned g_open_selected;
static unsigned g_delete_confirm;
static unsigned g_failures;

static void check(int condition, const char *message)
{
    if(!condition)
    {
        fprintf(stderr, "FAIL: %s\n", message);
        g_failures++;
    }
}

hk_owner_t capability_client_current_owner(void)
{
    return (hk_owner_t){1U, 1U};
}

hk_result_t hk_time_acquire(
    hk_owner_t owner, const hk_capability_request_t *request, hk_time_t *handle)
{
    (void)owner;
    g_requested_features = request->required_features;
    if(g_time_fails)
        return HK_ERR_INVALID_STATE;
    handle->lease.slot = 1U;
    handle->lease.generation = 1U;
    return HK_OK;
}

hk_result_t hk_time_now_us(
    hk_owner_t owner, const hk_time_t *handle, uint64_t *value)
{
    (void)owner;
    (void)handle;
    if(g_time_fails)
        return HK_ERR_INVALID_STATE;
    *value = g_now_us;
    return HK_OK;
}

void hk_screen_set(screen_t screen) { (void)screen; }
screen_t hk_screen_get(void) { return SCREEN_FILES; }
void hk_back_exit_set_armed(uint8_t armed) { (void)armed; }
void shell_show_menu(void) {}
void files_backend_enter(void) {}
void files_nav_delta(int8_t delta) { (void)delta; }
uint8_t files_back_from_list(void) { return 0U; }
void files_open_selected(void) { g_open_selected++; }
uint8_t files_delete_confirm_enter(void)
{
    g_delete_confirm++;
    return 1U;
}
void files_delete_cancel(void) {}
void files_delete_confirmed(void) {}
void files_presenter_close_image(void) {}
void files_presenter_render_list(void) {}
void files_presenter_tick_image(uint64_t now_us) { (void)now_us; }
uint8_t files_presenter_toggle_image_pause(uint64_t now_us)
{
    (void)now_us;
    return 1U;
}
file_browser_mode_t files_mode(void) { return g_mode; }
void files_set_mode(file_browser_mode_t mode) { g_mode = mode; }

static hk_input_snapshot_t snapshot(
    uint32_t state, uint32_t pressed, uint32_t changed)
{
    hk_input_snapshot_t input = {state, pressed, changed};
    return input;
}

static void tap_ok(void)
{
    hk_input_snapshot_t input = snapshot(BUTTON_OK, BUTTON_OK, BUTTON_OK);

    files_controller_handle_buttons(&input);
    input = snapshot(0U, 0U, BUTTON_OK);
    files_controller_handle_buttons(&input);
}

int main(void)
{
    hk_input_snapshot_t input = snapshot(0U, 0U, 0U);

    g_mode = FILES_MODE_LIST;
    g_now_us = 1000U;
    files_controller_enter(&input);
    tap_ok();
    check(g_open_selected == 1U, "short OK must open selected entry");
    check(
        g_requested_features == HK_TIME_FEATURE_MONOTONIC_US,
        "Files must request monotonic time only");

    g_time_fails = 1U;
    files_controller_reset_input();
    tap_ok();
    check(
        g_open_selected == 2U,
        "short OK must remain usable when timing is unavailable");

    files_controller_reset_input();
    input = snapshot(BUTTON_OK, BUTTON_OK, BUTTON_OK);
    files_controller_handle_buttons(&input);
    for(unsigned tick = 0U; tick < 100U; tick++)
        files_controller_tick(&input);
    check(
        g_delete_confirm == 0U,
        "missing timing must never trigger destructive hold action");

    if(g_failures)
        return 1;
    puts("FILES_CONTROLLER_OK open=1 monotonic=1 failure_safe=1");
    return 0;
}

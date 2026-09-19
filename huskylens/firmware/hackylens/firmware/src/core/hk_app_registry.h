#ifndef HK_APP_REGISTRY_H
#define HK_APP_REGISTRY_H

#include "hk_app.h"


extern const hk_app_t g_menu_items[];
extern const uint8_t g_menu_item_count;
#define MENU_ITEM_COUNT g_menu_item_count

const hk_app_t *hk_app_for_screen(screen_t screen);
const hk_app_t *hk_app_for_autostart_id(hk_autostart_id_t id);
uint8_t hk_app_autostart_count(void);
const hk_app_t *hk_app_autostart_at(uint8_t index);
void hk_app_registry_background_tick(const hk_input_snapshot_t *input);
void hk_app_registry_handle_sd_event(hk_sd_event_t event);
uint8_t hk_app_registry_sd_poll_allowed(screen_t screen);
uint8_t hk_app_registry_handle_debug_command(const char *cmd);

#endif

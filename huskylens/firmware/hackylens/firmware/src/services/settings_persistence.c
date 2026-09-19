
#include "../config/settings_config.h"

#include "settings_service.h"
#include "../storage/settings_store_types.h"
#include "../storage/settings_store.h"
#include "settings_payload_codec.h"
#include "settings_snapshot.h"

static uint8_t g_settings_dirty;
static uint8_t g_settings_save_ticks;

void settings_storage_init(void)
{
    settings_store_load_t loaded;
    settings_snapshot_t snapshot;

    settings_defaults();
    settings_store_init(&loaded);
    if(loaded.has_payload)
    {
        settings_payload_decode(&loaded.payload, &snapshot);
        settings_snapshot_apply(&snapshot);
    }
}

void settings_storage_save_now(void)
{
    settings_payload_t payload;
    settings_snapshot_t snapshot;

    if(!settings_store_enabled())
    {
        g_settings_dirty = 0;
        g_settings_save_ticks = 0;
        return;
    }

    settings_snapshot_capture(&snapshot);
    payload = settings_payload_encode(&snapshot);
    if(settings_store_save(&payload))
    {
        g_settings_dirty = 0;
        g_settings_save_ticks = 0;
    }
}

void settings_mark_dirty(uint8_t immediate)
{
    g_settings_dirty = 1;
    g_settings_save_ticks = immediate ? 1 : SETTINGS_SAVE_DELAY_TICKS;
}

void settings_storage_tick(void)
{
    if(!g_settings_dirty)
        return;
    if(g_settings_save_ticks > 0)
    {
        g_settings_save_ticks--;
        return;
    }
    settings_storage_save_now();
}

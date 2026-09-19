#ifndef SETTINGS_STORE_H
#define SETTINGS_STORE_H

#include "settings_store_types.h"


typedef struct
{
    uint8_t has_payload;
    settings_payload_t payload;
} settings_store_load_t;

void settings_store_init(settings_store_load_t *result);
uint8_t settings_store_enabled(void);
uint8_t settings_store_save(const settings_payload_t *payload);

#endif

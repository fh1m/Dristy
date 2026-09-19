#ifndef SETTINGS_PERSISTENCE_H
#define SETTINGS_PERSISTENCE_H

#include <stdint.h>

void settings_storage_init(void);
void settings_storage_save_now(void);
void settings_storage_tick(void);
void settings_mark_dirty(uint8_t immediate);

#endif

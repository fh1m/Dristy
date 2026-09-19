#ifndef HK_SETTINGS_APP_DATA_H
#define HK_SETTINGS_APP_DATA_H

#include <stddef.h>
#include <stdint.h>

#include "../config/settings_config.h"

void settings_app_data_read(uint8_t data[SETTINGS_APP_DATA_SIZE]);
void settings_app_data_write(const uint8_t data[SETTINGS_APP_DATA_SIZE]);

#endif

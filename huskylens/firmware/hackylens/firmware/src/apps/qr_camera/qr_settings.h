#ifndef HK_QR_SETTINGS_H
#define HK_QR_SETTINGS_H

#include "../../core/hk_app.h"

void qr_settings_open(void);
void qr_settings_close(void);
void qr_settings_tick(const hk_input_snapshot_t *input);
uint8_t qr_settings_active(void);
uint8_t qr_settings_handle_input(const hk_input_snapshot_t *input);

#endif

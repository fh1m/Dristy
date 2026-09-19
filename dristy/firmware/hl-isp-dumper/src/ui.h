#ifndef HUSKY_ISP_UI_H
#define HUSKY_ISP_UI_H

#include <stdint.h>

void ui_init(void);
void ui_waiting(void);
void ui_initializing(void);
void ui_erasing(uint32_t done, uint32_t total);
void ui_reading(uint32_t done, uint32_t total);
void ui_flash_ready(void);
uint8_t ui_validate_write(uint32_t address, const uint8_t *data, uint32_t length);
void ui_write_complete(uint32_t address, const uint8_t *data, uint32_t length);
void ui_done(void);
void ui_error(const char *code);
uint8_t ui_percent(void);
uint32_t ui_wire_size(void);

#endif

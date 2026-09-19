#ifndef HK_OV2640_SENSOR_H
#define HK_OV2640_SENSOR_H

#include <stdint.h>

void ov2640_init_bus(uint32_t *actual_xclk, uint32_t *actual_sccb);
void ov2640_write_reg(uint8_t reg, uint8_t value);
uint8_t ov2640_read_reg(uint8_t reg);
void ov2640_probe_id(uint16_t *mid, uint16_t *pid);
void ov2640_apply_init_table(void);
void ov2640_apply_common_tuning(void);
void ov2640_apply_output_size(uint16_t width, uint16_t height);
void ov2640_apply_colorbar(uint8_t enabled);

#endif

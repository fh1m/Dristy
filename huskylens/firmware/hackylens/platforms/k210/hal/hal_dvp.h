#ifndef HAL_DVP_H
#define HAL_DVP_H

#include <stdint.h>

typedef struct
{
    uint32_t cfg;
    uint32_t cmos_cfg;
    uint32_t sccb_cfg;
    uint32_t axi;
    uint32_t sts;
    uint32_t rgb_addr;
} hal_dvp_regs_t;

typedef enum
{
    HAL_DVP_EVENT_FRAME_START = 0,
    HAL_DVP_EVENT_FRAME_FINISH,
} hal_dvp_event_t;

typedef void (*hal_dvp_event_callback_t)(hal_dvp_event_t event, uint32_t status, void *ctx);

void hal_dvp_init_bus(uint8_t div);
uint32_t hal_dvp_set_xclk_rate(uint32_t hz);
uint32_t hal_dvp_set_sccb_rate(uint32_t hz);
void hal_dvp_sccb_write(uint8_t addr, uint8_t reg, uint8_t value);
uint8_t hal_dvp_sccb_read(uint8_t addr, uint8_t reg);
void hal_dvp_config_rgb565(uint16_t width, uint16_t height, uint32_t display_addr, uint8_t burst);
void hal_dvp_set_display_addr(uint32_t display_addr);
void hal_dvp_set_ai_rgb888(uint32_t red_addr, uint32_t green_addr, uint32_t blue_addr);
void hal_dvp_output_display(uint8_t enabled);
void hal_dvp_output_ai(uint8_t enabled);
uint32_t hal_dvp_status(void);
uint8_t hal_dvp_status_has_frame_start(uint32_t status);
uint8_t hal_dvp_status_has_frame_finish(uint32_t status);
void hal_dvp_clear_frame_start(void);
void hal_dvp_clear_frame_finish(void);
void hal_dvp_start_convert(void);
void hal_dvp_disable_capture(void);
void hal_dvp_stop_capture(void);
uint8_t hal_dvp_irq_start(hal_dvp_event_callback_t callback, void *ctx);
void hal_dvp_irq_stop(void);
void hal_dvp_irq_mask(void);
void hal_dvp_irq_unmask(void);
void hal_dvp_read_regs(hal_dvp_regs_t *regs);

#endif

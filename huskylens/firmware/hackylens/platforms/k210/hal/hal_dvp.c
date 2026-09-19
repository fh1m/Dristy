#include "hal_dvp.h"

#include <stddef.h>
#include <dvp.h>
#include <plic.h>

static hal_dvp_event_callback_t g_dvp_event_callback;
static void *g_dvp_event_ctx;

static int hal_dvp_irq_handler(void *ctx)
{
    uint32_t status = dvp->sts;
    (void)ctx;

    if(status & DVP_STS_FRAME_FINISH)
    {
        dvp_clear_interrupt(DVP_STS_FRAME_FINISH);
        if(g_dvp_event_callback)
            g_dvp_event_callback(HAL_DVP_EVENT_FRAME_FINISH, status, g_dvp_event_ctx);
    }
    if(status & DVP_STS_FRAME_START)
    {
        dvp_clear_interrupt(DVP_STS_FRAME_START);
        if(g_dvp_event_callback)
            g_dvp_event_callback(HAL_DVP_EVENT_FRAME_START, status, g_dvp_event_ctx);
    }
    return 0;
}

void hal_dvp_init_bus(uint8_t div)
{
    dvp_init(div);
}

uint32_t hal_dvp_set_xclk_rate(uint32_t hz)
{
    return dvp_set_xclk_rate(hz);
}

uint32_t hal_dvp_set_sccb_rate(uint32_t hz)
{
    return dvp_sccb_set_clk_rate(hz);
}

void hal_dvp_sccb_write(uint8_t addr, uint8_t reg, uint8_t value)
{
    dvp_sccb_send_data(addr, reg, value);
}

uint8_t hal_dvp_sccb_read(uint8_t addr, uint8_t reg)
{
    return dvp_sccb_receive_data(addr, reg);
}

void hal_dvp_config_rgb565(uint16_t width, uint16_t height, uint32_t display_addr, uint8_t burst)
{
    dvp_disable_auto();
    if(burst)
        dvp_enable_burst();
    else
        dvp_disable_burst();

    dvp_set_image_format(DVP_CFG_RGB_FORMAT);
    dvp_set_image_size(width, height);
    dvp_set_display_addr(display_addr);
    dvp_set_output_enable(DVP_OUTPUT_AI, 0);
    dvp_set_output_enable(DVP_OUTPUT_DISPLAY, 1);
    dvp_clear_interrupt(DVP_STS_FRAME_START);
    dvp_clear_interrupt(DVP_STS_FRAME_FINISH);
}

void hal_dvp_set_display_addr(uint32_t display_addr)
{
    dvp_set_display_addr(display_addr);
}

void hal_dvp_set_ai_rgb888(uint32_t red_addr, uint32_t green_addr, uint32_t blue_addr)
{
    dvp_set_ai_addr(red_addr, green_addr, blue_addr);
    dvp_set_output_enable(DVP_OUTPUT_AI, 1);
}

void hal_dvp_output_display(uint8_t enabled)
{
    dvp_set_output_enable(DVP_OUTPUT_DISPLAY, enabled ? 1 : 0);
}

void hal_dvp_output_ai(uint8_t enabled)
{
    dvp_set_output_enable(DVP_OUTPUT_AI, enabled ? 1 : 0);
}

uint32_t hal_dvp_status(void)
{
    return dvp->sts;
}

uint8_t hal_dvp_status_has_frame_start(uint32_t status)
{
    return (status & DVP_STS_FRAME_START) ? 1 : 0;
}

uint8_t hal_dvp_status_has_frame_finish(uint32_t status)
{
    return (status & DVP_STS_FRAME_FINISH) ? 1 : 0;
}

void hal_dvp_clear_frame_start(void)
{
    dvp_clear_interrupt(DVP_STS_FRAME_START);
}

void hal_dvp_clear_frame_finish(void)
{
    dvp_clear_interrupt(DVP_STS_FRAME_FINISH);
}

void hal_dvp_start_convert(void)
{
    dvp->sts = DVP_STS_FRAME_FINISH | DVP_STS_FRAME_FINISH_WE |
               DVP_STS_FRAME_START | DVP_STS_FRAME_START_WE |
               DVP_STS_DVP_EN | DVP_STS_DVP_EN_WE;
}

void hal_dvp_disable_capture(void)
{
    dvp->sts = DVP_STS_DVP_EN_WE;
}

void hal_dvp_stop_capture(void)
{
    dvp->sts = DVP_STS_FRAME_FINISH | DVP_STS_FRAME_FINISH_WE |
               DVP_STS_FRAME_START | DVP_STS_FRAME_START_WE |
               DVP_STS_DVP_EN_WE;
}

uint8_t hal_dvp_irq_start(hal_dvp_event_callback_t callback, void *ctx)
{
    if(!callback)
        return 0;

    plic_irq_disable(IRQN_DVP_INTERRUPT);
    dvp_config_interrupt(DVP_CFG_START_INT_ENABLE | DVP_CFG_FINISH_INT_ENABLE, 0);
    dvp_clear_interrupt(DVP_STS_FRAME_START);
    dvp_clear_interrupt(DVP_STS_FRAME_FINISH);
    g_dvp_event_callback = callback;
    g_dvp_event_ctx = ctx;
    plic_set_priority(IRQN_DVP_INTERRUPT, 1);
    plic_irq_register(IRQN_DVP_INTERRUPT, hal_dvp_irq_handler, NULL);
    dvp_config_interrupt(DVP_CFG_START_INT_ENABLE | DVP_CFG_FINISH_INT_ENABLE, 1);
    plic_irq_enable(IRQN_DVP_INTERRUPT);
    return 1;
}

void hal_dvp_irq_stop(void)
{
    plic_irq_disable(IRQN_DVP_INTERRUPT);
    dvp_config_interrupt(DVP_CFG_START_INT_ENABLE | DVP_CFG_FINISH_INT_ENABLE, 0);
    plic_irq_unregister(IRQN_DVP_INTERRUPT);
    g_dvp_event_callback = NULL;
    g_dvp_event_ctx = NULL;
    dvp_clear_interrupt(DVP_STS_FRAME_START);
    dvp_clear_interrupt(DVP_STS_FRAME_FINISH);
}

void hal_dvp_irq_mask(void)
{
    plic_irq_disable(IRQN_DVP_INTERRUPT);
}

void hal_dvp_irq_unmask(void)
{
    plic_irq_enable(IRQN_DVP_INTERRUPT);
}

void hal_dvp_read_regs(hal_dvp_regs_t *regs)
{
    if(!regs)
        return;

    regs->cfg = dvp->dvp_cfg;
    regs->cmos_cfg = dvp->cmos_cfg;
    regs->sccb_cfg = dvp->sccb_cfg;
    regs->axi = dvp->axi;
    regs->sts = dvp->sts;
    regs->rgb_addr = dvp->rgb_addr;
}

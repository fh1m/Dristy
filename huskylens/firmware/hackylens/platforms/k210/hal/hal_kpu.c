#include "hal_kpu.h"

#include <string.h>

#include <stdint.h>
#include <dmac.h>
#include <kpu.h>
#include <plic.h>
#include <sysctl.h>

static kpu_model_context_t g_context;
static hal_kpu_done_callback_t g_done_callback;
static void *g_done_userdata;
static volatile uint8_t g_busy;
static uint8_t g_loaded;

static void hal_kpu_prepare(void)
{
    static uint8_t prepared;

    if(prepared)
        return;

    /* The K210 AI clock is derived from PLL1.  The regular firmware only
       configures PLL0 for the CPU, whereas Canaan's face_detect demo sets
       PLL1 to 400 MHz before enabling the accelerator.  Leaving PLL1 at its
       boot value starts the DMA transfer but leaves KPU calculation pending. */
    sysctl_pll_set_freq(SYSCTL_PLL1, 400000000UL);
    dmac_init();
    sysctl_clock_enable(SYSCTL_CLOCK_AI);
    prepared = 1;
}

static uint8_t hal_kpu_header_matches(const uint8_t *model, uint32_t model_size,
                                      const ai_model_kmodel_contract_t *contract)
{
    const kpu_kmodel_header_t *header;

    if(!model || !contract || model_size < sizeof(*header))
        return 0;
    header = (const kpu_kmodel_header_t *)model;
    return header->version == contract->version && header->flags == contract->flags &&
           header->arch == contract->arch && header->layers_length == contract->layers_length &&
           header->max_start_address == contract->max_start_address &&
           header->main_mem_usage == contract->main_mem_usage &&
           header->output_count == contract->output_count;
}

static uint8_t hal_kpu_input_matches(const uint8_t *model, uint32_t model_size,
                                     uint32_t expected_bytes, uint32_t *input_bytes)
{
    const kpu_model_layer_header_t *header;
    const kpu_model_conv_layer_argument_t *conv;
    const kpu_layer_argument_t *layer;
    uint32_t offset;
    uint32_t bytes;

    if(!g_context.layers_length)
        return 0;
    header = g_context.layer_headers;
    if(header->type != KL_K210_CONV || header->body_size < sizeof(*conv))
        return 0;
    conv = (const kpu_model_conv_layer_argument_t *)g_context.body_start;
    offset = conv->layer_offset;
    if(offset > model_size || model_size - offset < sizeof(*layer))
        return 0;
    layer = (const kpu_layer_argument_t *)(model + offset);
    bytes = layer->kernel_calc_type_cfg.data.channel_switch_addr * 64U *
            (layer->image_channel_num.data.i_ch_num + 1U);
    if(input_bytes)
        *input_bytes = bytes;
    return bytes == expected_bytes;
}

static void hal_kpu_done(void *userdata)
{
    hal_kpu_done_callback_t callback = g_done_callback;
    void *callback_userdata = g_done_userdata;

    (void)userdata;
    g_busy = 0;
    if(callback)
        callback(callback_userdata);
}

hal_kpu_load_result_t hal_kpu_model_load(const uint8_t *model, uint32_t model_size,
                                         const ai_model_kmodel_contract_t *contract,
                                         uint32_t *input_bytes)
{
    if(input_bytes)
        *input_bytes = 0;
    if(g_loaded || g_busy)
        return HAL_KPU_LOAD_FAILED;
    if(!hal_kpu_header_matches(model, model_size, contract))
        return HAL_KPU_LOAD_FORMAT;

    hal_kpu_prepare();
    memset(&g_context, 0, sizeof(g_context));
    if(kpu_load_kmodel(&g_context, model) != 0)
    {
        kpu_model_free(&g_context);
        memset(&g_context, 0, sizeof(g_context));
        return HAL_KPU_LOAD_FAILED;
    }
    g_loaded = 1;
    if(!hal_kpu_input_matches(model, model_size, contract->input_bytes, input_bytes))
    {
        hal_kpu_model_unload();
        return HAL_KPU_LOAD_FORMAT;
    }
    return HAL_KPU_LOAD_OK;
}

int hal_kpu_run(const uint8_t *input, hal_kpu_done_callback_t callback, void *userdata)
{
    int result;

    if(!g_loaded || g_busy || !input)
        return -1;
    g_done_callback = callback;
    g_done_userdata = userdata;
    g_busy = 1;
    result = kpu_run_kmodel(&g_context, input, DMAC_CHANNEL5, hal_kpu_done, NULL);
    if(result != 0)
    {
        g_busy = 0;
        g_done_callback = NULL;
        g_done_userdata = NULL;
    }
    return result;
}

int hal_kpu_get_output(uint32_t index, const uint8_t **output, size_t *bytes)
{
    uint8_t *sdk_output;
    int result;

    if(!g_loaded || g_busy || !output || !bytes)
        return -1;
    result = kpu_get_output(&g_context, index, &sdk_output, bytes);
    if(result == 0)
        *output = sdk_output;
    return result;
}

uint8_t hal_kpu_is_busy(void) { return g_busy; }

uint8_t hal_kpu_is_loaded(void) { return g_loaded; }

hal_kpu_stop_result_t hal_kpu_stop_and_reset(void)
{
    if(!g_busy)
        return HAL_KPU_STOP_NOT_RUNNING;

    /* KPU inference can complete through either the AI interrupt or the
       input/output DMA channel.  Stop both sources while machine interrupts
       are masked, remove their callbacks, then reset the AI peripheral before
       allowing the model and input memory to be released. */
    sysctl_disable_irq();
    dmac_disable_channel_interrupt(DMAC_CHANNEL5);
    dmac_channel_disable(DMAC_CHANNEL5);
    dmac_irq_unregister(DMAC_CHANNEL5);
    plic_irq_disable(IRQN_AI_INTERRUPT);
    plic_irq_unregister(IRQN_AI_INTERRUPT);
    sysctl_reset(SYSCTL_RESET_AI);
    g_done_callback = NULL;
    g_done_userdata = NULL;
    g_busy = 0;
    sysctl_enable_irq();
    return HAL_KPU_STOP_OK;
}

uint8_t hal_kpu_model_unload(void)
{
    if(g_busy)
        return 0;
    if(g_loaded)
        kpu_model_free(&g_context);
    memset(&g_context, 0, sizeof(g_context));
    g_done_callback = NULL;
    g_done_userdata = NULL;
    g_loaded = 0;
    return 1;
}

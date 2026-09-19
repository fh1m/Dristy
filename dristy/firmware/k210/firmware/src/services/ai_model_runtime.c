#include "ai_model_runtime.h"

#include <string.h>

#include "hal_kpu.h"
#include "hal_time.h"
#include "../storage/ai_model_storage.h"

#define AI_MODEL_DEFAULT_UNLOAD_TIMEOUT_US 2000000ULL
#define AI_MODEL_DMA_ALIGNMENT 256U

static ai_model_runtime_t *g_owner;

static uint32_t element_size(ai_model_element_t element)
{
    return element == AI_MODEL_ELEMENT_F32 ? 4U : 1U;
}

static uint8_t tensor_valid(const ai_model_tensor_t *tensor)
{
    uint32_t elements = 1U;

    if(!tensor || !tensor->bytes || !tensor->rank ||
       tensor->rank > AI_MODEL_MAX_TENSOR_DIMS ||
       tensor->element > AI_MODEL_ELEMENT_F32 ||
       tensor->layout > AI_MODEL_LAYOUT_FLAT)
        return 0U;
    for(uint8_t i = 0U; i < tensor->rank; i++)
    {
        if(!tensor->shape[i] || elements > UINT32_MAX / tensor->shape[i])
            return 0U;
        elements *= tensor->shape[i];
    }
    return elements <= UINT32_MAX / element_size(tensor->element) &&
           elements * element_size(tensor->element) <= tensor->bytes;
}

static uint8_t descriptor_valid(const ai_model_descriptor_t *descriptor)
{
    if(!descriptor || !descriptor->id || !descriptor->id[0] ||
       !descriptor->model_path || !descriptor->model_path[0] ||
       descriptor->max_model_bytes < 28U ||
       !descriptor->output_count || descriptor->output_count > AI_MODEL_MAX_OUTPUTS ||
       descriptor->kmodel.output_count != descriptor->output_count ||
       descriptor->kmodel.input_bytes != descriptor->input.bytes ||
       descriptor->postprocess > AI_MODEL_POSTPROCESS_EMBEDDING ||
       !tensor_valid(&descriptor->input))
        return 0U;
    for(uint8_t i = 0U; i < descriptor->output_count; i++)
        if(!tensor_valid(&descriptor->outputs[i]))
            return 0U;
    return !descriptor->manifest_required ||
           (descriptor->manifest_path && descriptor->manifest_path[0]);
}

static uint8_t contract_equal(const ai_model_kmodel_contract_t *left,
                              const ai_model_kmodel_contract_t *right)
{
    return left->version == right->version && left->flags == right->flags &&
           left->arch == right->arch && left->layers_length == right->layers_length &&
           left->max_start_address == right->max_start_address &&
           left->main_mem_usage == right->main_mem_usage &&
           left->output_count == right->output_count &&
           left->input_bytes == right->input_bytes;
}

static uint8_t manifest_matches(const ai_model_descriptor_t *descriptor,
                                const ai_model_manifest_t *manifest,
                                uint32_t model_size, uint32_t model_crc32)
{
    if(strcmp(descriptor->id, manifest->id) != 0 ||
       manifest->model_size != model_size || manifest->model_crc32 != model_crc32 ||
       (descriptor->expected_model_crc32 &&
        descriptor->expected_model_crc32 != model_crc32) ||
       !contract_equal(&descriptor->kmodel, &manifest->kmodel) ||
       manifest->input_rank != descriptor->input.rank ||
       manifest->input_element != descriptor->input.element ||
       manifest->input_layout != descriptor->input.layout ||
       manifest->normalization != descriptor->normalization.kind ||
       manifest->postprocess != descriptor->postprocess ||
       manifest->output_count != descriptor->output_count ||
       manifest->label_count != descriptor->label_count)
        return 0U;
    for(uint8_t i = 0U; i < descriptor->input.rank; i++)
        if(manifest->input_shape[i] != descriptor->input.shape[i])
            return 0U;
    for(uint8_t i = 0U; i < descriptor->output_count; i++)
        if(manifest->output_bytes[i] != descriptor->outputs[i].bytes)
            return 0U;
    return 1U;
}

static ai_model_result_t storage_result(ai_model_storage_result_t result)
{
    switch(result)
    {
        case AI_MODEL_STORAGE_OK: return AI_MODEL_RESULT_OK;
        case AI_MODEL_STORAGE_NO_SD: return AI_MODEL_RESULT_NO_SD;
        case AI_MODEL_STORAGE_DIRECTORY: return AI_MODEL_RESULT_DIRECTORY;
        case AI_MODEL_STORAGE_FILE: return AI_MODEL_RESULT_FILE;
        case AI_MODEL_STORAGE_SIZE: return AI_MODEL_RESULT_SIZE;
        case AI_MODEL_STORAGE_ALLOC: return AI_MODEL_RESULT_ALLOC;
        case AI_MODEL_STORAGE_READ: return AI_MODEL_RESULT_READ;
        case AI_MODEL_STORAGE_MANIFEST: return AI_MODEL_RESULT_MANIFEST;
        default: return AI_MODEL_RESULT_READ;
    }
}

static void clear_loaded_fields(ai_model_runtime_t *runtime)
{
    runtime->descriptor = NULL;
    runtime->model = NULL;
    runtime->model_size = 0U;
    runtime->model_crc32 = 0U;
    runtime->input_dma_bytes = 0U;
    runtime->run_count = 0U;
    runtime->last_inference_us = 0U;
    runtime->started_us = 0U;
    runtime->unload_requested_us = 0U;
    runtime->completion_pending = 0U;
    runtime->state = AI_MODEL_STATE_UNLOADED;
}

static uint8_t release_resources(ai_model_runtime_t *runtime)
{
    uint8_t *model;

    if(!runtime || !hal_kpu_model_unload())
        return 0U;
    model = runtime->model;
    if(model)
        ai_model_storage_free(model);
    if(g_owner == runtime)
        g_owner = NULL;
    clear_loaded_fields(runtime);
    return 1U;
}

static ai_model_result_t fail_load(ai_model_runtime_t *runtime, ai_model_result_t result)
{
    runtime->result = result;
    if(runtime->model)
        ai_model_storage_free(runtime->model);
    if(g_owner == runtime)
        g_owner = NULL;
    clear_loaded_fields(runtime);
    runtime->result = result;
    return result;
}

static void inference_done(void *userdata)
{
    ai_model_runtime_t *runtime = (ai_model_runtime_t *)userdata;

    if(!runtime)
        return;
    runtime->last_inference_us = (uint32_t)(hal_time_us() - runtime->started_us);
    if(runtime->state == AI_MODEL_STATE_RUNNING)
    {
        runtime->completion_pending = 1U;
        runtime->state = AI_MODEL_STATE_READY;
    }
}

void ai_model_runtime_init(ai_model_runtime_t *runtime)
{
    if(!runtime)
        return;
    memset(runtime, 0, sizeof(*runtime));
    runtime->state = AI_MODEL_STATE_UNLOADED;
    runtime->result = AI_MODEL_RESULT_OK;
}

ai_model_result_t ai_model_runtime_load(ai_model_runtime_t *runtime,
                                        const ai_model_descriptor_t *descriptor)
{
    ai_model_storage_result_t storage;
    ai_model_manifest_t manifest;
    hal_kpu_load_result_t load_result;
    const uint8_t *output;
    size_t output_bytes;

    if(!runtime)
        return AI_MODEL_RESULT_DESCRIPTOR;
    if(!descriptor_valid(descriptor))
    {
        runtime->result = AI_MODEL_RESULT_DESCRIPTOR;
        return runtime->result;
    }
    ai_model_runtime_tick(runtime);
    if(runtime->state != AI_MODEL_STATE_UNLOADED || g_owner ||
       hal_kpu_is_busy() || hal_kpu_is_loaded())
    {
        runtime->result = AI_MODEL_RESULT_BUSY;
        return runtime->result;
    }

    runtime->state = AI_MODEL_STATE_LOADING;
    runtime->descriptor = descriptor;
    g_owner = runtime;
    storage = ai_model_storage_load(descriptor->directory_path, descriptor->model_path,
                                    descriptor->max_model_bytes, AI_MODEL_DMA_ALIGNMENT,
                                    &runtime->model, &runtime->model_size,
                                    &runtime->model_crc32);
    if(storage != AI_MODEL_STORAGE_OK)
        return fail_load(runtime, storage_result(storage));
    if(descriptor->expected_model_crc32 &&
       runtime->model_crc32 != descriptor->expected_model_crc32)
        return fail_load(runtime, AI_MODEL_RESULT_FORMAT);

    if(descriptor->manifest_path && descriptor->manifest_path[0])
    {
        storage = ai_model_storage_load_manifest(descriptor->manifest_path, &manifest);
        if(storage != AI_MODEL_STORAGE_OK)
        {
            if(descriptor->manifest_required || storage != AI_MODEL_STORAGE_FILE)
                return fail_load(runtime, storage_result(storage));
        }
        else if(!manifest_matches(descriptor, &manifest, runtime->model_size,
                                  runtime->model_crc32))
            return fail_load(runtime, AI_MODEL_RESULT_MANIFEST);
    }
    if(descriptor->labels_path && descriptor->labels_path[0])
    {
        storage = ai_model_storage_validate_labels(descriptor->labels_path,
                                                   descriptor->label_count);
        if(storage != AI_MODEL_STORAGE_OK)
            return fail_load(runtime, storage_result(storage));
    }

    load_result = hal_kpu_model_load(runtime->model, runtime->model_size,
                                     &descriptor->kmodel, &runtime->input_dma_bytes);
    if(load_result != HAL_KPU_LOAD_OK)
        return fail_load(runtime, load_result == HAL_KPU_LOAD_FORMAT ?
                         AI_MODEL_RESULT_FORMAT : AI_MODEL_RESULT_KPU);
    for(uint8_t i = 0U; i < descriptor->output_count; i++)
    {
        if(hal_kpu_get_output(i, &output, &output_bytes) != 0 ||
           output_bytes != descriptor->outputs[i].bytes)
        {
            hal_kpu_model_unload();
            return fail_load(runtime, AI_MODEL_RESULT_OUTPUT);
        }
    }

    runtime->state = AI_MODEL_STATE_READY;
    runtime->result = AI_MODEL_RESULT_OK;
    return runtime->result;
}

uint8_t ai_model_runtime_run(ai_model_runtime_t *runtime, const uint8_t *input)
{
    if(!runtime || runtime != g_owner || runtime->state != AI_MODEL_STATE_READY ||
       !input || hal_kpu_is_busy())
        return 0U;
    runtime->completion_pending = 0U;
    runtime->started_us = hal_time_us();
    runtime->state = AI_MODEL_STATE_RUNNING;
    if(hal_kpu_run(input, inference_done, runtime) != 0)
    {
        runtime->state = AI_MODEL_STATE_READY;
        runtime->result = AI_MODEL_RESULT_RUN;
        return 0U;
    }
    runtime->run_count++;
    return 1U;
}

uint8_t ai_model_runtime_take_completion(ai_model_runtime_t *runtime)
{
    if(!runtime || !runtime->completion_pending)
        return 0U;
    runtime->completion_pending = 0U;
    return 1U;
}

int ai_model_runtime_get_output(ai_model_runtime_t *runtime, uint32_t index,
                                const uint8_t **output, size_t *bytes)
{
    if(!runtime || runtime != g_owner || runtime->state != AI_MODEL_STATE_READY ||
       !runtime->descriptor || index >= runtime->descriptor->output_count)
        return -1;
    return hal_kpu_get_output(index, output, bytes);
}

void ai_model_runtime_request_unload(ai_model_runtime_t *runtime)
{
    if(!runtime || runtime->state == AI_MODEL_STATE_UNLOADED)
        return;
    runtime->completion_pending = 0U;
    if(runtime->state == AI_MODEL_STATE_RUNNING)
    {
        runtime->state = AI_MODEL_STATE_UNLOAD_PENDING;
        runtime->unload_requested_us = hal_time_us();
        return;
    }
    if(!release_resources(runtime))
    {
        runtime->state = AI_MODEL_STATE_FAULT;
        runtime->result = AI_MODEL_RESULT_KPU;
    }
}

void ai_model_runtime_tick(ai_model_runtime_t *runtime)
{
    uint64_t timeout;
    hal_kpu_stop_result_t stop_result;

    if(!runtime || runtime->state != AI_MODEL_STATE_UNLOAD_PENDING)
        return;
    if(!hal_kpu_is_busy())
    {
        release_resources(runtime);
        return;
    }
    timeout = runtime->descriptor && runtime->descriptor->unload_timeout_us ?
              runtime->descriptor->unload_timeout_us : AI_MODEL_DEFAULT_UNLOAD_TIMEOUT_US;
    if(hal_time_us() - runtime->unload_requested_us < timeout)
        return;
    stop_result = hal_kpu_stop_and_reset();
    if(stop_result == HAL_KPU_STOP_OK || stop_result == HAL_KPU_STOP_NOT_RUNNING)
    {
        release_resources(runtime);
        return;
    }
    runtime->state = AI_MODEL_STATE_FAULT;
    runtime->result = AI_MODEL_RESULT_STOP;
}

uint8_t ai_model_runtime_loaded(const ai_model_runtime_t *runtime)
{
    return runtime && runtime == g_owner && hal_kpu_is_loaded() &&
           (runtime->state == AI_MODEL_STATE_READY ||
            runtime->state == AI_MODEL_STATE_RUNNING);
}

uint8_t ai_model_runtime_busy(const ai_model_runtime_t *runtime)
{
    return runtime && (runtime->state == AI_MODEL_STATE_RUNNING ||
                       runtime->state == AI_MODEL_STATE_UNLOAD_PENDING);
}

const char *ai_model_runtime_state_label(ai_model_state_t state)
{
    static const char *const labels[] =
        {"UNLOADED", "LOADING", "READY", "RUNNING", "UNLOAD_PENDING", "FAULT"};
    return state <= AI_MODEL_STATE_FAULT ? labels[state] : "INVALID";
}

const char *ai_model_runtime_result_label(ai_model_result_t result)
{
    static const char *const labels[] = {
        "NONE", "NO SD", "MODEL DIR", "MODEL FILE", "MODEL SIZE", "MODEL ALLOC",
        "MODEL READ", "MANIFEST", "DESCRIPTOR", "MODEL FORMAT", "MODEL OUTPUT",
        "KPU BUSY", "MODEL LOAD", "KPU RUN", "KPU STOP"
    };
    return result <= AI_MODEL_RESULT_STOP ? labels[result] : "MODEL ERROR";
}

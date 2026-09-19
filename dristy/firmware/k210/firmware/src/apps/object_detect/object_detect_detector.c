#include "object_detect_detector.h"

#include <stdio.h>
#include <string.h>

#include <dristy/capability/time.h>

#include "../../core/hk_capability_client.h"
#include "../../services/ai_model_runtime.h"
#include "../../services/camera_ai_input.h"
#include "object_detect_postprocess.h"

typedef struct
{
    uint32_t epoch;
    uint32_t sequence;
    uint32_t camera_sequence;
    uint64_t ready_us;
    uint16_t candidate_count;
    uint8_t count;
    object_detect_postprocess_stats_t stats;
    object_detect_result_t items[OBJECT_DETECT_RESULT_MAX];
} object_detect_result_bank_t;

static ai_model_runtime_t g_runtime;
static uint8_t g_runtime_initialized;
static volatile uint8_t g_active;
static volatile uint8_t g_unload_requested;
static volatile uint8_t g_capture_paused;
static volatile uint32_t g_session_epoch;
static volatile uint32_t g_published_sequence;
static uint32_t g_next_result_sequence;
static uint32_t g_inference_camera_sequence;
static uint32_t g_inference_epoch;
static uint32_t g_last_present_camera_sequence;
static uint32_t g_last_frame_lag;
static uint32_t g_submit_count;
static uint32_t g_busy_drop_count;
static volatile uint32_t g_decode_count;
static volatile uint32_t g_last_decode_us;
static volatile uint32_t g_last_pipeline_us;
static uint8_t g_input_stats_ready;
static uint8_t g_input_min[3];
static uint8_t g_input_max[3];
static uint16_t g_input_mean_x10[3];
static uint8_t g_confidence = OBJECT_DETECT_DEFAULT_CONFIDENCE;
static uint8_t g_nms = OBJECT_DETECT_DEFAULT_NMS;
static object_detect_load_result_t g_result = OBJECT_DETECT_LOAD_FORMAT;
static object_detect_result_bank_t g_result_banks[2] __attribute__((aligned(64)));
static object_detect_postprocess_workspace_t g_workspace __attribute__((aligned(64)));
static hk_time_t s_object_time;
static hk_owner_t s_object_time_owner;

static uint64_t object_time_now_us(void)
{
    static const hk_capability_request_t request = HK_TIME_REQUEST_0_1_INIT;
    hk_owner_t owner = capability_client_consumer_owner(
        "consumer:object-detect-detector");
    uint64_t value = 0U;

    if(hk_owner_is_zero(owner))
        return 0U;
    if(owner.slot != s_object_time_owner.slot ||
       owner.generation != s_object_time_owner.generation ||
       hk_lease_is_zero(&s_object_time.lease))
    {
        s_object_time.lease = HK_LEASE_NONE;
        s_object_time_owner = owner;
        if(hk_time_acquire(owner, &request, &s_object_time) != HK_OK)
            return 0U;
    }
    if(hk_time_now_us(owner, &s_object_time, &value) != HK_OK)
        return 0U;
    return value;
}

static const ai_model_descriptor_t g_model_descriptor = {
    .id = "object-detect-voc20",
    .directory_path = OBJECT_DETECT_MODEL_DIRECTORY,
    .model_path = OBJECT_DETECT_MODEL_PATH,
    .manifest_path = OBJECT_DETECT_MANIFEST_PATH,
    .labels_path = OBJECT_DETECT_LABELS_PATH,
    .max_model_bytes = OBJECT_DETECT_MODEL_MAX_BYTES,
    .expected_model_crc32 = 0x107D903CU,
    .unload_timeout_us = OBJECT_DETECT_UNLOAD_TIMEOUT_US,
    .kmodel = {
        .version = 3U,
        .flags = 1U,
        .arch = 0U,
        .layers_length = 17U,
        .max_start_address = 0U,
        .main_mem_usage = 0xAAE8U,
        .output_count = 1U,
        .input_bytes = OBJECT_DETECT_INPUT_BYTES,
    },
    .input = {
        .name = "image",
        .bytes = OBJECT_DETECT_INPUT_BYTES,
        .shape = {1U, 3U, OBJECT_DETECT_INPUT_H, OBJECT_DETECT_INPUT_W},
        .rank = 4U,
        .element = AI_MODEL_ELEMENT_U8,
        .layout = AI_MODEL_LAYOUT_CHW,
    },
    .outputs = {{
        .name = "yolo",
        .bytes = OBJECT_DETECT_OUTPUT_BYTES,
        .shape = {1U, OBJECT_DETECT_CHANNELS,
                  OBJECT_DETECT_GRID_H, OBJECT_DETECT_GRID_W},
        .rank = 4U,
        .element = AI_MODEL_ELEMENT_F32,
        .layout = AI_MODEL_LAYOUT_CHW,
    }},
    .normalization = {
        .kind = AI_MODEL_NORMALIZATION_NONE,
        .scale = {1.0f, 1.0f, 1.0f},
        .bias = {0.0f, 0.0f, 0.0f},
    },
    .postprocess = AI_MODEL_POSTPROCESS_YOLO2,
    .label_count = OBJECT_DETECT_CLASS_COUNT,
    .output_count = 1U,
    .manifest_required = 1U,
};

static const ai_model_descriptor_t g_model_descriptor_legacy = {
    .id = "object-detect-voc20",
    .directory_path = OBJECT_DETECT_MODEL_DIRECTORY_LEGACY,
    .model_path = OBJECT_DETECT_MODEL_PATH_LEGACY,
    .manifest_path = OBJECT_DETECT_MANIFEST_PATH_LEGACY,
    .labels_path = OBJECT_DETECT_LABELS_PATH_LEGACY,
    .max_model_bytes = OBJECT_DETECT_MODEL_MAX_BYTES,
    .expected_model_crc32 = 0x107D903CU,
    .unload_timeout_us = OBJECT_DETECT_UNLOAD_TIMEOUT_US,
    .kmodel = {
        .version = 3U,
        .flags = 1U,
        .arch = 0U,
        .layers_length = 17U,
        .max_start_address = 0U,
        .main_mem_usage = 0xAAE8U,
        .output_count = 1U,
        .input_bytes = OBJECT_DETECT_INPUT_BYTES,
    },
    .input = {
        .name = "image",
        .bytes = OBJECT_DETECT_INPUT_BYTES,
        .shape = {1U, 3U, OBJECT_DETECT_INPUT_H, OBJECT_DETECT_INPUT_W},
        .rank = 4U,
        .element = AI_MODEL_ELEMENT_U8,
        .layout = AI_MODEL_LAYOUT_CHW,
    },
    .outputs = {{
        .name = "yolo",
        .bytes = OBJECT_DETECT_OUTPUT_BYTES,
        .shape = {1U, OBJECT_DETECT_CHANNELS,
                  OBJECT_DETECT_GRID_H, OBJECT_DETECT_GRID_W},
        .rank = 4U,
        .element = AI_MODEL_ELEMENT_F32,
        .layout = AI_MODEL_LAYOUT_CHW,
    }},
    .normalization = {
        .kind = AI_MODEL_NORMALIZATION_NONE,
        .scale = {1.0f, 1.0f, 1.0f},
        .bias = {0.0f, 0.0f, 0.0f},
    },
    .postprocess = AI_MODEL_POSTPROCESS_YOLO2,
    .label_count = OBJECT_DETECT_CLASS_COUNT,
    .output_count = 1U,
    .manifest_required = 1U,
};

static object_detect_result_bank_t *shared_result_bank(uint8_t index)
{
    return &g_result_banks[index & 1U];
}

static int32_t diagnostic_milli(float value)
{
    if(value > 1000000.0f)
        return 1000000000;
    if(value < -1000000.0f)
        return -1000000000;
    return (int32_t)(value * 1000.0f);
}

static object_detect_load_result_t object_result(ai_model_result_t result)
{
    switch(result)
    {
        case AI_MODEL_RESULT_OK: return OBJECT_DETECT_LOAD_OK;
        case AI_MODEL_RESULT_NO_SD: return OBJECT_DETECT_LOAD_NO_SD;
        case AI_MODEL_RESULT_DIRECTORY: return OBJECT_DETECT_LOAD_DIR;
        case AI_MODEL_RESULT_FILE: return OBJECT_DETECT_LOAD_FILE;
        case AI_MODEL_RESULT_ALLOC: return OBJECT_DETECT_LOAD_ALLOC;
        case AI_MODEL_RESULT_SIZE:
        case AI_MODEL_RESULT_READ: return OBJECT_DETECT_LOAD_READ;
        case AI_MODEL_RESULT_MANIFEST: return OBJECT_DETECT_LOAD_MANIFEST;
        case AI_MODEL_RESULT_DESCRIPTOR:
        case AI_MODEL_RESULT_FORMAT:
        case AI_MODEL_RESULT_OUTPUT: return OBJECT_DETECT_LOAD_FORMAT;
        case AI_MODEL_RESULT_BUSY: return OBJECT_DETECT_LOAD_BUSY;
        default: return OBJECT_DETECT_LOAD_KPU;
    }
}

static void reset_detection_state(void)
{
    g_capture_paused = 0U;
    g_published_sequence = 0U;
    g_next_result_sequence = 0U;
    g_inference_camera_sequence = 0U;
    g_inference_epoch = 0U;
    g_last_present_camera_sequence = 0U;
    g_last_frame_lag = 0U;
    g_submit_count = 0U;
    g_busy_drop_count = 0U;
    g_decode_count = 0U;
    g_last_decode_us = 0U;
    g_last_pipeline_us = 0U;
    g_input_stats_ready = 0U;
    memset(g_input_min, 0, sizeof(g_input_min));
    memset(g_input_max, 0, sizeof(g_input_max));
    memset(g_input_mean_x10, 0, sizeof(g_input_mean_x10));
    memset(shared_result_bank(0U), 0, sizeof(g_result_banks[0]));
    memset(shared_result_bank(1U), 0, sizeof(g_result_banks[1]));
}

static uint8_t finish_inference(void)
{
    const uint8_t *output;
    size_t bytes;
    object_detect_result_bank_t *bank;
    uint32_t sequence;
    uint64_t decode_started_us;

    if(!ai_model_runtime_take_completion(&g_runtime))
        return 0U;
    if(ai_model_runtime_get_output(&g_runtime, 0U, &output, &bytes) != 0)
    {
        if(g_capture_paused)
            camera_ai_input_cancel(&g_runtime);
        else
            camera_ai_input_arm(&g_runtime);
        g_result = OBJECT_DETECT_LOAD_KPU;
        return 1U;
    }
    sequence = g_next_result_sequence + 1U;
    if(sequence == 0U)
        sequence = 1U;
    g_next_result_sequence = sequence;
    bank = shared_result_bank((uint8_t)sequence);
    memset(bank, 0, sizeof(*bank));
    bank->epoch = g_inference_epoch;
    bank->sequence = sequence;
    bank->camera_sequence = g_inference_camera_sequence;
    decode_started_us = object_time_now_us();
    bank->count = object_detect_postprocess(
        (const float *)output, bytes, g_confidence, g_nms,
        &g_workspace, bank->items, &bank->candidate_count, &bank->stats);
    bank->ready_us = object_time_now_us();
    g_last_decode_us = (uint32_t)(bank->ready_us - decode_started_us);
    g_last_pipeline_us = (uint32_t)(bank->ready_us - g_runtime.started_us);
    g_decode_count++;
    __sync_synchronize();
    if(g_active && bank->epoch == g_session_epoch)
        g_published_sequence = sequence;
    camera_ai_input_cancel(&g_runtime);
    if(!g_capture_paused)
        camera_ai_input_arm(&g_runtime);
    return 1U;
}

object_detect_load_result_t object_detect_detector_load(void)
{
    ai_model_result_t result;

    if(!g_runtime_initialized)
    {
        ai_model_runtime_init(&g_runtime);
        g_runtime_initialized = 1U;
    }
    object_detect_detector_service_tick();
    if(g_runtime.state != AI_MODEL_STATE_UNLOADED)
    {
        g_result = g_runtime.state == AI_MODEL_STATE_FAULT ?
                   OBJECT_DETECT_LOAD_KPU : OBJECT_DETECT_LOAD_BUSY;
        return g_result;
    }
    reset_detection_state();
    g_session_epoch++;
    if(g_session_epoch == 0U)
        g_session_epoch++;
    if(!camera_ai_input_acquire(&g_runtime, OBJECT_DETECT_INPUT_W,
                                OBJECT_DETECT_INPUT_H,
                                OBJECT_DETECT_INPUT_BYTES))
    {
        g_result = OBJECT_DETECT_LOAD_BUSY;
        return g_result;
    }
    result = ai_model_runtime_load(&g_runtime, &g_model_descriptor);
    if(result != AI_MODEL_RESULT_OK)
        result = ai_model_runtime_load(&g_runtime, &g_model_descriptor_legacy);
    g_result = object_result(result);
    if(result != AI_MODEL_RESULT_OK)
    {
        camera_ai_input_release(&g_runtime);
        return g_result;
    }
    g_active = 1U;
    g_capture_paused = 0U;
    g_unload_requested = 0U;
    printf("[OBJECT] KPU-V3 model=%08X align=%u bytes=%u input=%u "
           "VOC20 grid=10x7x125 core0-post\r\n",
           (unsigned)(uintptr_t)g_runtime.model,
           (unsigned)((uintptr_t)g_runtime.model & 255U),
           (unsigned)g_runtime.model_size,
           (unsigned)g_runtime.input_dma_bytes);
    return g_result;
}

void object_detect_detector_unload(void)
{
    camera_ai_input_cancel(&g_runtime);
    g_active = 0U;
    g_capture_paused = 1U;
    g_session_epoch++;
    if(g_session_epoch == 0U)
        g_session_epoch++;
    g_unload_requested = 1U;
    ai_model_runtime_request_unload(&g_runtime);
}

void object_detect_detector_service_tick(void)
{
    if(g_unload_requested)
    {
        if(g_runtime.state != AI_MODEL_STATE_UNLOADED &&
           g_runtime.state != AI_MODEL_STATE_UNLOAD_PENDING)
            ai_model_runtime_request_unload(&g_runtime);
    }
    ai_model_runtime_tick(&g_runtime);
    if(g_runtime.state == AI_MODEL_STATE_UNLOADED)
    {
        camera_ai_input_release(&g_runtime);
        g_unload_requested = 0U;
    }
    else if(g_runtime.state == AI_MODEL_STATE_FAULT)
        g_result = OBJECT_DETECT_LOAD_KPU;
}

uint8_t object_detect_detector_ready(void)
{
    return g_active && g_result == OBJECT_DETECT_LOAD_OK &&
           ai_model_runtime_loaded(&g_runtime);
}

object_detect_load_result_t object_detect_detector_result(void)
{
    return g_result;
}

void object_detect_detector_attach_camera(void)
{
    (void)camera_ai_input_attach(&g_runtime);
}

void object_detect_detector_process_frame(uint32_t camera_sequence)
{
    uint8_t *input;
    uint8_t rearmed;
    uint32_t input_sequence;

    object_detect_detector_service_tick();
    if(!object_detect_detector_ready())
        return;
    rearmed = finish_inference();
    if(rearmed || ai_model_runtime_busy(&g_runtime))
    {
        g_busy_drop_count++;
        return;
    }

    input = camera_ai_input_data(&g_runtime);
    if(!input)
        return;
    if(!camera_ai_input_take(&g_runtime, &input_sequence))
        return;
    if(!g_input_stats_ready)
    {
        uint32_t plane_bytes = OBJECT_DETECT_INPUT_W * OBJECT_DETECT_INPUT_H;

        for(uint8_t plane = 0U; plane < 3U; plane++)
        {
            const uint8_t *pixels = input + (uint32_t)plane * plane_bytes;
            uint32_t sum = 0U;
            uint8_t minimum = 255U;
            uint8_t maximum = 0U;

            for(uint32_t index = 0U; index < plane_bytes; index++)
            {
                uint8_t value = pixels[index];

                sum += value;
                if(value < minimum)
                    minimum = value;
                if(value > maximum)
                    maximum = value;
            }
            g_input_min[plane] = minimum;
            g_input_max[plane] = maximum;
            g_input_mean_x10[plane] =
                (uint16_t)(((uint64_t)sum * 10U) / plane_bytes);
        }
        g_input_stats_ready = 1U;
    }
    g_inference_camera_sequence = input_sequence;
    g_inference_epoch = g_session_epoch;
    if(!ai_model_runtime_run(&g_runtime, input))
    {
        if(!g_capture_paused)
            camera_ai_input_arm(&g_runtime);
        g_result = OBJECT_DETECT_LOAD_KPU;
        printf("[OBJECT] kpu start failed\r\n");
        return;
    }
    g_submit_count++;
    (void)camera_sequence;
}

void object_detect_detector_invalidate_results(void)
{
    g_session_epoch++;
    if(g_session_epoch == 0U)
        g_session_epoch++;
    g_published_sequence = 0U;
    __sync_synchronize();
}

void object_detect_detector_pause_capture(void)
{
    g_capture_paused = 1U;
    __sync_synchronize();
    camera_ai_input_cancel(&g_runtime);
}

void object_detect_detector_resume_capture(void)
{
    g_capture_paused = 0U;
    __sync_synchronize();
    /*
     * Do not let DVP overwrite the shared planar input while KPU may still
     * be reading the frame that was running when settings opened. The normal
     * completion path rearms capture after that inference is collected.
     */
    if(!ai_model_runtime_busy(&g_runtime))
        camera_ai_input_arm(&g_runtime);
}

void object_detect_detector_set_thresholds(uint8_t confidence, uint8_t nms)
{
    g_confidence = confidence <= 100U ? confidence : 100U;
    g_nms = nms <= 100U ? nms : 100U;
}

const object_detect_result_t *object_detect_detector_results(uint8_t *count)
{
    uint32_t sequence = g_published_sequence;
    object_detect_result_bank_t *bank;

    __sync_synchronize();
    bank = shared_result_bank((uint8_t)sequence);
    if(!g_active || sequence == 0U || bank->epoch != g_session_epoch ||
       bank->sequence != sequence)
    {
        if(count)
            *count = 0U;
        return bank->items;
    }
    if(count)
        *count = bank->count <= OBJECT_DETECT_RESULT_MAX ?
                 bank->count : OBJECT_DETECT_RESULT_MAX;
    return bank->items;
}

uint32_t object_detect_detector_result_sequence(void)
{
    return g_published_sequence;
}

void object_detect_detector_note_present(uint32_t camera_sequence)
{
    uint32_t sequence = g_published_sequence;
    object_detect_result_bank_t *bank = shared_result_bank((uint8_t)sequence);

    g_last_present_camera_sequence = camera_sequence;
    if(sequence && bank->sequence == sequence &&
       camera_sequence >= bank->camera_sequence)
        g_last_frame_lag = camera_sequence - bank->camera_sequence;
    else
        g_last_frame_lag = 0U;
}

const char *object_detect_detector_error_label(object_detect_load_result_t result)
{
    static const char *const labels[] = {
        "NONE", "NO SD", "MODEL DIR", "MODEL FILE", "MODEL READ",
        "MODEL ALLOC", "MANIFEST", "MODEL FORMAT", "MODEL LOAD", "CORE1",
        "AI BUSY",
    };

    return result <= OBJECT_DETECT_LOAD_BUSY ? labels[result] : "MODEL LOAD";
}

void object_detect_detector_format_info(char *line, size_t line_size)
{
    uint8_t count;
    uint32_t sequence = g_published_sequence;
    object_detect_result_bank_t *bank = shared_result_bank((uint8_t)sequence);
    int32_t raw_min_milli = 0;
    int32_t raw_max_milli = 0;
    uint32_t objectness_milli = 0U;
    uint32_t class_milli = 0U;
    uint32_t probability_milli = 0U;

    (void)object_detect_detector_results(&count);
    if(sequence)
    {
        raw_min_milli = diagnostic_milli(bank->stats.raw_min);
        raw_max_milli = diagnostic_milli(bank->stats.raw_max);
        objectness_milli =
            (uint32_t)(bank->stats.max_objectness * 1000.0f + 0.5f);
        class_milli =
            (uint32_t)(bank->stats.max_class_probability * 1000.0f + 0.5f);
        probability_milli =
            (uint32_t)(bank->stats.max_probability * 1000.0f + 0.5f);
    }
    snprintf(line, line_size,
             "HKOBJECTINFO state=%s error=%s model=%u input=320x240 "
             "grid=10x7x125 classes=20 conf=%u nms=%u "
             "infer_us=%u decode_us=%u pipeline_us=%u runs=%u decoded=%u "
             "busy_drop=%u candidates=%u objects=%u result_frame=%u "
             "present_frame=%u frame_lag=%u "
             "input_rgb_x10=%u-%u/%u,%u-%u/%u,%u-%u/%u "
             "raw=%d/%d finite=%u zero=%u sub=%u bad=%u obj=%u cls=%u prob=%u "
             "top=%u/%u/%u\r\n",
             ai_model_runtime_state_label(g_runtime.state),
             object_detect_detector_error_label(g_result),
             (unsigned)g_runtime.model_size,
             g_confidence, g_nms,
             (unsigned)g_runtime.last_inference_us,
             (unsigned)g_last_decode_us,
             (unsigned)g_last_pipeline_us,
             (unsigned)g_submit_count,
             (unsigned)g_decode_count,
             (unsigned)g_busy_drop_count,
             (unsigned)(sequence ? bank->candidate_count : 0U),
             (unsigned)count,
             (unsigned)(sequence ? bank->camera_sequence : 0U),
             (unsigned)g_last_present_camera_sequence,
             (unsigned)g_last_frame_lag,
             g_input_min[0], g_input_max[0], g_input_mean_x10[0],
             g_input_min[1], g_input_max[1], g_input_mean_x10[1],
             g_input_min[2], g_input_max[2], g_input_mean_x10[2],
             (int)raw_min_milli, (int)raw_max_milli,
             (unsigned)(sequence ? bank->stats.finite_count : 0U),
             (unsigned)(sequence ? bank->stats.zero_count : 0U),
             (unsigned)(sequence ? bank->stats.subnormal_count : 0U),
             (unsigned)(sequence ? bank->stats.nonfinite_count : 0U),
             (unsigned)objectness_milli, (unsigned)class_milli,
             (unsigned)probability_milli,
             (unsigned)(sequence ? bank->stats.top_anchor : 0U),
             (unsigned)(sequence ? bank->stats.top_cell : 0U),
             (unsigned)(sequence ? bank->stats.top_class : 0U));
}

#include "face_detect_detector.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "../../services/ai_model_runtime.h"
#include "../../services/camera_ai_input.h"
#include "face_detect_config.h"

/* Exact tensor contract of kendryte-standalone-demo/face_detect/detect.kmodel. */
#define FACE_W 320U
#define FACE_H 240U
#define FACE_PIXELS (FACE_W * FACE_H)
#define FACE_INPUT_BYTES (FACE_PIXELS * 3U)
#define FACE_GRID_W 20U
#define FACE_GRID_H 15U
#define FACE_GRID_CELLS (FACE_GRID_W * FACE_GRID_H)
#define FACE_ANCHORS 5U
#define FACE_OUTPUT_BYTES (FACE_GRID_CELLS * 30U * sizeof(float))
#define FACE_CANDIDATE_MAX 64U
#define FACE_THRESHOLD 0.70f
#define FACE_NMS 0.30f

typedef struct { float x, y, w, h, p; } face_candidate_t;

static const float g_anchors[10] = {1.889f, 2.5245f, 2.9465f, 3.94056f, 3.99987f,
                                    5.3658f, 5.155437f, 6.92275f, 6.718375f, 9.01025f};
static ai_model_runtime_t g_runtime;
static uint8_t g_runtime_initialized;
static uint32_t g_candidates_count;
static face_detect_load_result_t g_result = FACE_DETECT_LOAD_FORMAT;
static face_candidate_t g_candidates[FACE_CANDIDATE_MAX];
static face_detect_box_t g_boxes[FACE_DETECT_BOX_MAX];
static uint8_t g_box_count;
static const ai_model_descriptor_t g_model_descriptor = {
    .id = "face-detect",
    .directory_path = FACE_DETECT_MODEL_DIRECTORY,
    .model_path = FACE_DETECT_MODEL_PATH,
    .manifest_path = NULL,
    .labels_path = NULL,
    .max_model_bytes = 1024U * 1024U,
    .unload_timeout_us = FACE_DETECT_UNLOAD_TIMEOUT_US,
    .kmodel = {
        .version = 3U,
        .flags = 1U,
        .arch = 0U,
        .layers_length = 24U,
        .max_start_address = 0x4400U,
        .main_mem_usage = 0xafc8U,
        .output_count = 1U,
        .input_bytes = FACE_INPUT_BYTES,
    },
    .input = {
        .name = "image",
        .bytes = FACE_INPUT_BYTES,
        .shape = {1U, 3U, FACE_H, FACE_W},
        .rank = 4U,
        .element = AI_MODEL_ELEMENT_U8,
        .layout = AI_MODEL_LAYOUT_CHW,
    },
    .outputs = {{
        .name = "yolo",
        .bytes = FACE_OUTPUT_BYTES,
        .shape = {1U, 30U, FACE_GRID_H, FACE_GRID_W},
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
    .label_count = 1U,
    .output_count = 1U,
    .manifest_required = 0U,
};

static const ai_model_descriptor_t g_model_descriptor_legacy = {
    .id = "face-detect",
    .directory_path = FACE_DETECT_MODEL_DIRECTORY_LEGACY,
    .model_path = FACE_DETECT_MODEL_PATH_LEGACY,
    .manifest_path = NULL,
    .labels_path = NULL,
    .max_model_bytes = 1024U * 1024U,
    .unload_timeout_us = FACE_DETECT_UNLOAD_TIMEOUT_US,
    .kmodel = {
        .version = 3U,
        .flags = 1U,
        .arch = 0U,
        .layers_length = 24U,
        .max_start_address = 0x4400U,
        .main_mem_usage = 0xafc8U,
        .output_count = 1U,
        .input_bytes = FACE_INPUT_BYTES,
    },
    .input = {
        .name = "image",
        .bytes = FACE_INPUT_BYTES,
        .shape = {1U, 3U, FACE_H, FACE_W},
        .rank = 4U,
        .element = AI_MODEL_ELEMENT_U8,
        .layout = AI_MODEL_LAYOUT_CHW,
    },
    .outputs = {{
        .name = "yolo",
        .bytes = FACE_OUTPUT_BYTES,
        .shape = {1U, 30U, FACE_GRID_H, FACE_GRID_W},
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
    .label_count = 1U,
    .output_count = 1U,
    .manifest_required = 0U,
};

static float sigmoidf_fast(float value) { return 1.0f / (1.0f + expf(-value)); }

static float overlap(float c1, float s1, float c2, float s2)
{
    float left = (c1 - s1 * .5f) > (c2 - s2 * .5f) ? (c1 - s1 * .5f) : (c2 - s2 * .5f);
    float right = (c1 + s1 * .5f) < (c2 + s2 * .5f) ? (c1 + s1 * .5f) : (c2 + s2 * .5f);
    return right > left ? right - left : 0.0f;
}

static float candidate_iou(const face_candidate_t *a, const face_candidate_t *b)
{
    float intersection = overlap(a->x, a->w, b->x, b->w) * overlap(a->y, a->h, b->y, b->h);
    float total = a->w * a->h + b->w * b->h - intersection;
    return total > 0.0f ? intersection / total : 0.0f;
}

static int16_t coordinate(float normalized, uint16_t limit)
{
    int32_t value = (int32_t)(normalized * limit);
    if(value < 0) return 0;
    if(value > limit) return (int16_t)limit;
    return (int16_t)value;
}

static void decode_output(const float *output, size_t bytes)
{
    uint8_t used[FACE_CANDIDATE_MAX] = {0};

    g_candidates_count = 0;
    g_box_count = 0;
    if(!output || bytes != FACE_OUTPUT_BYTES)
        return;
    for(uint32_t anchor = 0; anchor < FACE_ANCHORS; anchor++)
    {
        uint32_t base = anchor * FACE_GRID_CELLS * 6U;
        for(uint32_t cell = 0; cell < FACE_GRID_CELLS; cell++)
        {
            float p = sigmoidf_fast(output[base + FACE_GRID_CELLS * 4U + cell]);
            face_candidate_t *candidate;
            if(p < FACE_THRESHOLD || g_candidates_count >= FACE_CANDIDATE_MAX)
                continue;
            candidate = &g_candidates[g_candidates_count++];
            candidate->x = ((cell % FACE_GRID_W) + sigmoidf_fast(output[base + cell])) / FACE_GRID_W;
            candidate->y = ((cell / FACE_GRID_W) + sigmoidf_fast(output[base + FACE_GRID_CELLS + cell])) / FACE_GRID_H;
            candidate->w = expf(output[base + FACE_GRID_CELLS * 2U + cell]) * g_anchors[anchor * 2U] / FACE_GRID_W;
            candidate->h = expf(output[base + FACE_GRID_CELLS * 3U + cell]) * g_anchors[anchor * 2U + 1U] / FACE_GRID_H;
            candidate->p = p;
        }
    }
    while(g_box_count < FACE_DETECT_BOX_MAX)
    {
        int32_t best = -1;
        float best_p = FACE_THRESHOLD;
        for(uint32_t i = 0; i < g_candidates_count; i++)
        {
            uint8_t suppressed = used[i];
            if(g_candidates[i].p <= best_p || suppressed)
                continue;
            for(uint8_t box_index = 0; box_index < g_box_count; box_index++)
            {
                face_candidate_t kept;
                const face_detect_box_t *box = &g_boxes[box_index];
                kept.x = (box->x + box->w * .5f) / FACE_W;
                kept.y = (box->y + box->h * .5f) / FACE_H;
                kept.w = box->w / (float)FACE_W;
                kept.h = box->h / (float)FACE_H;
                if(candidate_iou(&g_candidates[i], &kept) > FACE_NMS)
                {
                    suppressed = 1;
                    break;
                }
            }
            if(!suppressed) { best = (int32_t)i; best_p = g_candidates[i].p; }
        }
        if(best < 0)
            break;
        else
        {
            const face_candidate_t *candidate = &g_candidates[best];
            face_detect_box_t *box = &g_boxes[g_box_count];
            int16_t x1 = coordinate(candidate->x - candidate->w * .5f, FACE_W);
            int16_t y1 = coordinate(candidate->y - candidate->h * .5f, FACE_H);
            int16_t x2 = coordinate(candidate->x + candidate->w * .5f, FACE_W);
            int16_t y2 = coordinate(candidate->y + candidate->h * .5f, FACE_H);
            used[best] = 1;
            if(x2 > x1 && y2 > y1)
            {
                box->x = x1; box->y = y1; box->w = x2 - x1; box->h = y2 - y1;
                g_box_count++;
            }
        }
    }
}

static void finish_inference(void)
{
    const uint8_t *output;
    size_t bytes;
    if(!ai_model_runtime_take_completion(&g_runtime))
        return;
    if(ai_model_runtime_get_output(&g_runtime, 0U, &output, &bytes) == 0)
        decode_output((const float *)output, bytes);
    else
        g_result = FACE_DETECT_LOAD_KPU;
    /* Keep the DVP's RGB planes immutable while KPU uses them, then arm it
       for the next completed camera frame. */
    camera_ai_input_arm(&g_runtime);
}

static void reset_detection_state(void)
{
    g_candidates_count = 0;
    g_box_count = 0;
    memset(g_candidates, 0, sizeof(g_candidates));
    memset(g_boxes, 0, sizeof(g_boxes));
}

static face_detect_load_result_t face_result(ai_model_result_t result)
{
    switch(result)
    {
        case AI_MODEL_RESULT_OK: return FACE_DETECT_LOAD_OK;
        case AI_MODEL_RESULT_NO_SD: return FACE_DETECT_LOAD_NO_SD;
        case AI_MODEL_RESULT_DIRECTORY: return FACE_DETECT_LOAD_DIR;
        case AI_MODEL_RESULT_FILE: return FACE_DETECT_LOAD_FILE;
        case AI_MODEL_RESULT_ALLOC: return FACE_DETECT_LOAD_ALLOC;
        case AI_MODEL_RESULT_SIZE:
        case AI_MODEL_RESULT_READ: return FACE_DETECT_LOAD_READ;
        case AI_MODEL_RESULT_MANIFEST:
        case AI_MODEL_RESULT_DESCRIPTOR:
        case AI_MODEL_RESULT_FORMAT:
        case AI_MODEL_RESULT_OUTPUT: return FACE_DETECT_LOAD_FORMAT;
        case AI_MODEL_RESULT_BUSY: return FACE_DETECT_LOAD_BUSY;
        default: return FACE_DETECT_LOAD_KPU;
    }
}

face_detect_load_result_t face_detect_detector_load(void)
{
    ai_model_result_t result;

    if(!g_runtime_initialized)
    {
        ai_model_runtime_init(&g_runtime);
        g_runtime_initialized = 1U;
    }
    ai_model_runtime_tick(&g_runtime);
    if(g_runtime.state != AI_MODEL_STATE_UNLOADED)
    {
        g_result = g_runtime.state == AI_MODEL_STATE_FAULT ?
                   FACE_DETECT_LOAD_KPU : FACE_DETECT_LOAD_BUSY;
        return g_result;
    }
    reset_detection_state();
    if(!camera_ai_input_acquire(&g_runtime, FACE_W, FACE_H, FACE_INPUT_BYTES))
    {
        g_result = FACE_DETECT_LOAD_BUSY;
        return g_result;
    }
    result = ai_model_runtime_load(&g_runtime, &g_model_descriptor);
    if(result != AI_MODEL_RESULT_OK)
        result = ai_model_runtime_load(&g_runtime, &g_model_descriptor_legacy);
    g_result = face_result(result);
    if(result != AI_MODEL_RESULT_OK)
    {
        camera_ai_input_release(&g_runtime);
        return g_result;
    }
    printf("[FACE] KPU-V3 model=%08X align=%u bytes=%u input=%u, 320x240 YOLO\r\n",
           (unsigned)(uintptr_t)g_runtime.model,
           (unsigned)((uintptr_t)g_runtime.model & 255U),
           (unsigned)g_runtime.model_size, (unsigned)g_runtime.input_dma_bytes);
    return g_result;
}

void face_detect_detector_unload(void)
{
    camera_ai_input_cancel(&g_runtime);
    ai_model_runtime_request_unload(&g_runtime);
}

void face_detect_detector_service_tick(void)
{
    ai_model_state_t before = g_runtime.state;
    ai_model_runtime_tick(&g_runtime);
    if(g_runtime.state == AI_MODEL_STATE_UNLOADED)
        camera_ai_input_release(&g_runtime);
    if(before == AI_MODEL_STATE_UNLOAD_PENDING &&
       g_runtime.state == AI_MODEL_STATE_FAULT)
    {
        g_result = FACE_DETECT_LOAD_KPU;
        printf("[FACE] KPU stop failed, reboot required\r\n");
    }
}

uint8_t face_detect_detector_ready(void)
{
    return g_result == FACE_DETECT_LOAD_OK && ai_model_runtime_loaded(&g_runtime);
}

face_detect_load_result_t face_detect_detector_result(void)
{
    return g_result;
}

void face_detect_detector_attach_camera(void)
{
    (void)camera_ai_input_attach(&g_runtime);
}

void face_detect_detector_process_frame(void)
{
    uint8_t *input = camera_ai_input_data(&g_runtime);
    uint8_t inference_finished;
    uint32_t sequence;
    if(!face_detect_detector_ready())
        return;
    inference_finished = g_runtime.completion_pending;
    finish_inference();
    /* The just-finished UI frame was captured with AI output disabled; wait
       for the following frame after re-arming DVP above. */
    if(inference_finished || ai_model_runtime_busy(&g_runtime))
        return;
    if(!camera_ai_input_take(&g_runtime, &sequence))
        return;
    (void)sequence;
    if(g_runtime.run_count == 0U)
        printf("[FACE] kpu start input=%08X bytes=%u\r\n",
               (unsigned)(uintptr_t)input, (unsigned)g_runtime.input_dma_bytes);
    if(!ai_model_runtime_run(&g_runtime, input))
    {
        camera_ai_input_arm(&g_runtime);
        g_result = FACE_DETECT_LOAD_KPU;
        printf("[FACE] kpu start failed\r\n");
    }
}

const face_detect_box_t *face_detect_detector_boxes(uint8_t *count)
{
    if(count) *count = g_box_count;
    return g_boxes;
}

const char *face_detect_detector_error_label(face_detect_load_result_t result)
{
    static const char *labels[] = {
        "NONE", "NO SD", "MODEL DIR", "MODEL FILE", "MODEL READ",
        "MODEL ALLOC", "MODEL FORMAT", "MODEL LOAD", "AI BUSY"
    };
    return result <= FACE_DETECT_LOAD_BUSY ? labels[result] : "MODEL LOAD";
}

void face_detect_detector_format_info(char *line, size_t line_size)
{
    snprintf(line, line_size, "HKFACEINFO state=%s error=%s size=%u KPU-V3 320x240 grid=20x15x30 us=%u candidates=%u boxes=%u\r\n",
             ai_model_runtime_state_label(g_runtime.state),
             face_detect_detector_error_label(g_result), (unsigned)g_runtime.model_size,
             (unsigned)g_runtime.last_inference_us, (unsigned)g_candidates_count,
             (unsigned)g_box_count);
}

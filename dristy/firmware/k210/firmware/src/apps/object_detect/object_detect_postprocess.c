#include "object_detect_postprocess.h"

#include <float.h>
#include <math.h>
#include <string.h>

static const float g_anchors[OBJECT_DETECT_ANCHOR_COUNT * 2U] = {
    1.08f, 1.19f,
    3.42f, 4.41f,
    6.63f, 11.38f,
    9.42f, 5.11f,
    16.62f, 10.52f,
};

static float sigmoidf_safe(float value)
{
    if(value > 16.0f)
        return 1.0f;
    if(value < -16.0f)
        return 0.0f;
    return 1.0f / (1.0f + expf(-value));
}

static float expf_safe(float value)
{
    if(value > 16.0f)
        value = 16.0f;
    else if(value < -16.0f)
        value = -16.0f;
    return expf(value);
}

static uint8_t finite_float(float value)
{
    uint32_t bits;

    memcpy(&bits, &value, sizeof(bits));
    return (bits & 0x7F800000U) != 0x7F800000U;
}

static float overlap(float center_a, float size_a, float center_b, float size_b)
{
    float left_a = center_a - size_a * 0.5f;
    float left_b = center_b - size_b * 0.5f;
    float right_a = center_a + size_a * 0.5f;
    float right_b = center_b + size_b * 0.5f;
    float left = left_a > left_b ? left_a : left_b;
    float right = right_a < right_b ? right_a : right_b;

    return right > left ? right - left : 0.0f;
}

static float candidate_iou(const object_detect_candidate_t *a,
                           const object_detect_candidate_t *b)
{
    float intersection = overlap(a->x, a->w, b->x, b->w) *
                         overlap(a->y, a->h, b->y, b->h);
    float area = a->w * a->h + b->w * b->h - intersection;

    return area > 0.0f ? intersection / area : 0.0f;
}

static int16_t coordinate(float normalized, uint16_t limit)
{
    int32_t value = (int32_t)(normalized * limit);

    if(value < 0)
        return 0;
    if(value >= limit)
        return (int16_t)(limit - 1U);
    return (int16_t)value;
}

static uint8_t class_probabilities(const float *output,
                                   uint32_t anchor_base,
                                   uint32_t cell,
                                   float probabilities[OBJECT_DETECT_CLASS_COUNT])
{
    float largest = output[anchor_base +
                           (5U * OBJECT_DETECT_GRID_CELLS) + cell];
    float sum = 0.0f;
    uint8_t best = 0U;

    for(uint8_t class_id = 1U; class_id < OBJECT_DETECT_CLASS_COUNT; class_id++)
    {
        float value = output[anchor_base +
                             ((uint32_t)(5U + class_id) *
                              OBJECT_DETECT_GRID_CELLS) + cell];
        if(value > largest)
        {
            largest = value;
            best = class_id;
        }
    }
    for(uint8_t class_id = 0U; class_id < OBJECT_DETECT_CLASS_COUNT; class_id++)
    {
        float value = output[anchor_base +
                             ((uint32_t)(5U + class_id) *
                              OBJECT_DETECT_GRID_CELLS) + cell];
        sum += expf_safe(value - largest);
    }
    if(sum > 0.0f && finite_float(sum))
    {
        for(uint8_t class_id = 0U; class_id < OBJECT_DETECT_CLASS_COUNT;
            class_id++)
        {
            float value = output[anchor_base +
                                 ((uint32_t)(5U + class_id) *
                                  OBJECT_DETECT_GRID_CELLS) + cell];
            probabilities[class_id] = expf_safe(value - largest) / sum;
        }
    }
    else
        memset(probabilities, 0,
               OBJECT_DETECT_CLASS_COUNT * sizeof(probabilities[0]));
    return best;
}

uint8_t object_detect_postprocess(const float *output,
                                  size_t output_bytes,
                                  uint8_t confidence_percent,
                                  uint8_t nms_percent,
                                  object_detect_postprocess_workspace_t *workspace,
                                  object_detect_result_t results[OBJECT_DETECT_RESULT_MAX],
                                  uint16_t *candidate_count,
                                  object_detect_postprocess_stats_t *stats)
{
    float threshold = confidence_percent / 100.0f;
    float nms = nms_percent / 100.0f;
    uint16_t candidates = 0U;
    uint8_t result_count = 0U;

    if(candidate_count)
        *candidate_count = 0U;
    if(!output || !workspace || !results ||
       output_bytes != OBJECT_DETECT_OUTPUT_BYTES)
        return 0U;
    memset(workspace->probabilities, 0, sizeof(workspace->probabilities));
    if(stats)
    {
        memset(stats, 0, sizeof(*stats));
        stats->raw_min = FLT_MAX;
        stats->raw_max = -FLT_MAX;
        for(uint32_t index = 0U;
            index < OBJECT_DETECT_OUTPUT_BYTES / sizeof(float); index++)
        {
            float value = output[index];
            uint32_t bits;

            memcpy(&bits, &value, sizeof(bits));

            if(!finite_float(value))
            {
                stats->nonfinite_count++;
                continue;
            }
            stats->finite_count++;
            if((bits & 0x7FFFFFFFU) == 0U)
                stats->zero_count++;
            else if((bits & 0x7F800000U) == 0U)
                stats->subnormal_count++;
            if(value < stats->raw_min)
                stats->raw_min = value;
            if(value > stats->raw_max)
                stats->raw_max = value;
        }
        if(!stats->finite_count)
        {
            stats->raw_min = 0.0f;
            stats->raw_max = 0.0f;
        }
    }

    for(uint8_t anchor = 0U; anchor < OBJECT_DETECT_ANCHOR_COUNT; anchor++)
    {
        uint32_t anchor_base = (uint32_t)anchor *
            (OBJECT_DETECT_CLASS_COUNT + 5U) * OBJECT_DETECT_GRID_CELLS;

        for(uint32_t cell = 0U; cell < OBJECT_DETECT_GRID_CELLS; cell++)
        {
            uint16_t candidate_index =
                (uint16_t)((uint16_t)anchor * OBJECT_DETECT_GRID_CELLS + cell);
            float objectness = sigmoidf_safe(output[anchor_base +
                                                    4U * OBJECT_DETECT_GRID_CELLS +
                                                    cell]);
            float class_probability[OBJECT_DETECT_CLASS_COUNT];
            uint8_t class_id;
            object_detect_candidate_t *candidate =
                &workspace->candidates[candidate_index];
            uint8_t accepted = 0U;

            class_id = class_probabilities(output, anchor_base, cell,
                                           class_probability);
            if(!finite_float(objectness))
                continue;
            candidate->x = ((cell % OBJECT_DETECT_GRID_W) +
                sigmoidf_safe(output[anchor_base + cell])) / OBJECT_DETECT_GRID_W;
            candidate->y = ((cell / OBJECT_DETECT_GRID_W) +
                sigmoidf_safe(output[anchor_base + OBJECT_DETECT_GRID_CELLS + cell])) /
                OBJECT_DETECT_GRID_H;
            candidate->w = expf_safe(output[anchor_base +
                2U * OBJECT_DETECT_GRID_CELLS + cell]) *
                g_anchors[anchor * 2U] / OBJECT_DETECT_GRID_W;
            candidate->h = expf_safe(output[anchor_base +
                3U * OBJECT_DETECT_GRID_CELLS + cell]) *
                g_anchors[anchor * 2U + 1U] / OBJECT_DETECT_GRID_H;
            for(uint8_t current_class = 0U;
                current_class < OBJECT_DETECT_CLASS_COUNT; current_class++)
            {
                float probability = objectness * class_probability[current_class];

                if(!finite_float(class_probability[current_class]) ||
                   !finite_float(probability))
                    continue;
                if(stats && objectness > stats->max_objectness)
                    stats->max_objectness = objectness;
                if(stats && class_probability[current_class] >
                   stats->max_class_probability)
                    stats->max_class_probability = class_probability[current_class];
                if(stats && probability > stats->max_probability)
                {
                    stats->max_probability = probability;
                    stats->top_anchor = anchor;
                    stats->top_cell = (uint16_t)cell;
                    stats->top_class = current_class;
                }
                if(probability >= threshold)
                {
                    workspace->probabilities[(uint32_t)candidate_index *
                        OBJECT_DETECT_CLASS_COUNT + current_class] = probability;
                    accepted = 1U;
                }
            }
            (void)class_id;
            if(accepted)
                candidates++;
        }
    }

    for(uint8_t class_id = 0U; class_id < OBJECT_DETECT_CLASS_COUNT; class_id++)
    {
        memset(workspace->suppressed, 0, sizeof(workspace->suppressed));
        while(1)
        {
            int32_t best = -1;
            float best_probability = threshold;

            for(uint16_t index = 0U; index < OBJECT_DETECT_CANDIDATE_MAX; index++)
            {
                float probability = workspace->probabilities[
                    (uint32_t)index * OBJECT_DETECT_CLASS_COUNT + class_id];

                if(!workspace->suppressed[index] && probability > best_probability)
                {
                    best = index;
                    best_probability = probability;
                }
            }
            if(best < 0)
                break;
            workspace->suppressed[best] = 1U;
            for(uint16_t index = 0U; index < OBJECT_DETECT_CANDIDATE_MAX; index++)
            {
                if(!workspace->suppressed[index] &&
                   candidate_iou(&workspace->candidates[best],
                                 &workspace->candidates[index]) > nms)
                {
                    workspace->suppressed[index] = 1U;
                    workspace->probabilities[(uint32_t)index *
                        OBJECT_DETECT_CLASS_COUNT + class_id] = 0.0f;
                }
            }
        }
    }

    while(result_count < OBJECT_DETECT_RESULT_MAX)
    {
        int32_t best = -1;
        uint8_t best_class_id = 0U;
        float best_probability = threshold;

        for(uint16_t index = 0U; index < OBJECT_DETECT_CANDIDATE_MAX; index++)
        {
            for(uint8_t class_id = 0U; class_id < OBJECT_DETECT_CLASS_COUNT;
                class_id++)
            {
                float probability = workspace->probabilities[
                    (uint32_t)index * OBJECT_DETECT_CLASS_COUNT + class_id];

                if(probability > best_probability)
                {
                    best = index;
                    best_class_id = class_id;
                    best_probability = probability;
                }
            }
        }
        if(best < 0)
            break;
        workspace->probabilities[(uint32_t)best * OBJECT_DETECT_CLASS_COUNT +
                                 best_class_id] = 0.0f;
        {
            const object_detect_candidate_t *candidate = &workspace->candidates[best];
            object_detect_result_t *result = &results[result_count];
            int16_t x1 = coordinate(candidate->x - candidate->w * 0.5f,
                                    OBJECT_DETECT_INPUT_W);
            int16_t y1 = coordinate(candidate->y - candidate->h * 0.5f,
                                    OBJECT_DETECT_INPUT_H);
            int16_t x2 = coordinate(candidate->x + candidate->w * 0.5f,
                                    OBJECT_DETECT_INPUT_W);
            int16_t y2 = coordinate(candidate->y + candidate->h * 0.5f,
                                    OBJECT_DETECT_INPUT_H);

            if(x2 <= x1 || y2 <= y1)
                continue;
            result->class_id = best_class_id;
            result->confidence = (uint16_t)(best_probability * 1000.0f + 0.5f);
            if(result->confidence > 1000U)
                result->confidence = 1000U;
            result->x = x1;
            result->y = y1;
            result->w = x2 - x1;
            result->h = y2 - y1;
            result_count++;
        }
    }
    if(candidate_count)
        *candidate_count = candidates;
    return result_count;
}

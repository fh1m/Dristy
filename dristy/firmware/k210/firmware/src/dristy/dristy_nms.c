#include "dristy_nms.h"

#include <math.h>

static float compute_iou(const dristy_nms_box_t *a, const dristy_nms_box_t *b)
{
    int16_t a_x1 = a->x, a_y1 = a->y;
    int16_t a_x2 = (int16_t)(a->x + a->w);
    int16_t a_y2 = (int16_t)(a->y + a->h);
    int16_t b_x1 = b->x, b_y1 = b->y;
    int16_t b_x2 = (int16_t)(b->x + b->w);
    int16_t b_y2 = (int16_t)(b->y + b->h);

    int16_t ix1 = a_x1 > b_x1 ? a_x1 : b_x1;
    int16_t iy1 = a_y1 > b_y1 ? a_y1 : b_y1;
    int16_t ix2 = a_x2 < b_x2 ? a_x2 : b_x2;
    int16_t iy2 = a_y2 < b_y2 ? a_y2 : b_y2;

    if(ix2 <= ix1 || iy2 <= iy1)
        return 0.0f;

    float intersection = (float)(ix2 - ix1) * (float)(iy2 - iy1);
    float area_a = (float)a->w * (float)a->h;
    float area_b = (float)b->w * (float)b->h;
    float union_area = area_a + area_b - intersection;

    return union_area > 0.0f ? intersection / union_area : 0.0f;
}

/* Insertion sort (stable, fast for small n ≤ 50) */
static void sort_by_score_desc(dristy_nms_box_t *boxes, uint8_t count)
{
    for(uint8_t i = 1; i < count; i++)
    {
        dristy_nms_box_t key = boxes[i];
        int8_t j = (int8_t)i - 1;
        while(j >= 0 && boxes[j].score < key.score)
        {
            boxes[j + 1] = boxes[j];
            j--;
        }
        boxes[j + 1] = key;
    }
}

uint8_t dristy_nms_apply(dristy_nms_box_t *boxes, uint8_t count,
                         const dristy_nms_config_t *config)
{
    uint8_t survivors = 0;

    if(!boxes || count == 0 || !config)
        return 0;

    sort_by_score_desc(boxes, count);

    for(uint8_t i = 0; i < count; i++)
    {
        if(boxes[i].score < config->score_threshold)
        {
            boxes[i].score = 0.0f;
            continue;
        }

        survivors++;

        for(uint8_t j = i + 1; j < count; j++)
        {
            if(boxes[j].score < config->score_threshold)
                continue;

            /* Class-aware: skip if different classes */
            if(!config->class_agnostic && boxes[i].cls != boxes[j].cls)
                continue;

            float iou = compute_iou(&boxes[i], &boxes[j]);

            switch(config->method)
            {
            case DRISTY_NMS_HARD:
                if(iou > config->iou_threshold)
                    boxes[j].score = 0.0f;
                break;

            case DRISTY_NMS_SOFT_GAUSSIAN:
            {
                /* score *= exp(-IoU²/σ) */
                float decay = expf(-(iou * iou) / config->sigma);
                boxes[j].score *= decay;
                if(boxes[j].score < config->score_threshold)
                    boxes[j].score = 0.0f;
                break;
            }

            case DRISTY_NMS_SOFT_LINEAR:
                if(iou > config->iou_threshold)
                {
                    boxes[j].score *= (1.0f - iou);
                    if(boxes[j].score < config->score_threshold)
                        boxes[j].score = 0.0f;
                }
                break;
            }
        }
    }

    /* Compact: move survivors to front */
    uint8_t write = 0;
    for(uint8_t i = 0; i < count; i++)
    {
        if(boxes[i].score > 0.0f)
        {
            if(write != i)
                boxes[write] = boxes[i];
            write++;
        }
    }
    /* Re-sort survivors by score */
    sort_by_score_desc(boxes, write);

    return write;
}

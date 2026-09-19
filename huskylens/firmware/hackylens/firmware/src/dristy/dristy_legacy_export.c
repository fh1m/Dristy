#include "dristy_legacy_export.h"

#include <string.h>

#include "dristy_pipeline.h"
#include "hk_config.h"
#include "../services/vision_result_service.h"

#if HK_ENABLE_APP_APRILTAG
#include "../apps/apriltag/apriltag_detector.h"
#endif
#if HK_ENABLE_APP_OBJECT_DETECT
#include "../apps/object_detect/object_detect_detector.h"
#endif
#if HK_ENABLE_APP_FACE_DETECT
#include "../apps/face_detect/face_detect_detector.h"
#endif
#if HK_ENABLE_APP_QR_CAMERA
#include "../apps/qr_camera/qr_result.h"
#endif

static dristy_norm_t norm_x(int16_t px, uint16_t w)
{
    return dristy_pixel_to_norm_x(px, w);
}

static dristy_norm_t norm_y(int16_t py, uint16_t h)
{
    return dristy_pixel_to_norm_y(py, h);
}

#if HK_ENABLE_APP_APRILTAG
static void tag_from_apriltag_result(dristy_result_tag_t *t,
                                     const apriltag_result_t *r,
                                     uint16_t w, uint16_t h)
{
    t->tag_id = r->id;
    t->family = 0U;
    t->hamming = r->hamming;
    t->cx = r->center_x;
    t->cy = r->center_y;
    t->norm_cx = norm_x(r->center_x, w);
    t->norm_cy = norm_y(r->center_y, h);
    t->pose_valid = 0U;
    for(uint8_t c = 0U; c < 4U; c++)
    {
        t->corners[c][0] = r->corners[c][0];
        t->corners[c][1] = r->corners[c][1];
    }
}
#endif

static void export_vision_service(dristy_result_snapshot_t *snap)
{
    vision_result_snapshot_t vision;
    uint8_t det_count = snap->detection_count;
    dristy_detection_t dets[DRISTY_MAX_DETECTIONS];

    vision_result_snapshot(&vision);
    if(vision.count == 0U)
        return;

    if(vision.source == (uint8_t)VISION_SOURCE_APRILTAG)
    {
        uint8_t tag_count = 0U;
        uint16_t w = vision.width ? vision.width : 320U;
        uint16_t h = vision.height ? vision.height : 240U;

        for(uint8_t i = 0U; i < vision.count && tag_count < DRISTY_MAX_TAGS; i++)
        {
            const vision_result_item_t *item = &vision.items[i];
            dristy_result_tag_t *t = &snap->tags[tag_count];
            int16_t cx = (int16_t)((item->x0 + item->x1) / 2);
            int16_t cy = (int16_t)((item->y0 + item->y1) / 2);

            t->tag_id = item->id;
            t->family = 0U;
            t->hamming = (uint8_t)item->flags;
            t->cx = cx;
            t->cy = cy;
            t->norm_cx = norm_x(cx, w);
            t->norm_cy = norm_y(cy, h);
            t->pose_valid = 0U;
            t->corners[0][0] = (int16_t)item->x0;
            t->corners[0][1] = (int16_t)item->y0;
            t->corners[1][0] = (int16_t)item->x1;
            t->corners[1][1] = (int16_t)item->y0;
            t->corners[2][0] = (int16_t)item->x1;
            t->corners[2][1] = (int16_t)item->y1;
            t->corners[3][0] = (int16_t)item->x0;
            t->corners[3][1] = (int16_t)item->y1;
            tag_count++;
        }
        if(tag_count > snap->tag_count)
            snap->tag_count = tag_count;
        return;
    }

    for(uint8_t i = 0U; i < vision.count &&
        det_count < DRISTY_MAX_DETECTIONS; i++)
    {
        const vision_result_item_t *item = &vision.items[i];
        int16_t cx = (int16_t)((item->x0 + item->x1) / 2);
        int16_t cy = (int16_t)((item->y0 + item->y1) / 2);
        int16_t bw = (int16_t)(item->x1 - item->x0);
        int16_t bh = (int16_t)(item->y1 - item->y0);
        dristy_result_detection_t *d = &snap->detections[det_count];

        d->x = (int16_t)item->x0;
        d->y = (int16_t)item->y0;
        d->w = bw;
        d->h = bh;
        d->norm_cx = norm_x(cx, vision.width ? vision.width : 320U);
        d->norm_cy = norm_y(cy, vision.height ? vision.height : 240U);
        d->norm_w = (uint16_t)((uint32_t)bw * 2000U /
                               (vision.width ? vision.width : 320U));
        d->norm_h = (uint16_t)((uint32_t)bh * 2000U /
                               (vision.height ? vision.height : 240U));
        d->cls = (uint8_t)(item->id & 0xFFU);
        d->confidence = item->confidence;

        dets[det_count].cx = cx;
        dets[det_count].cy = cy;
        dets[det_count].w = bw;
        dets[det_count].h = bh;
        dets[det_count].cls = d->cls;
        dets[det_count].conf = item->confidence;
        det_count++;
    }
    snap->detection_count = det_count;
    if(det_count > 0U)
        dristy_tracker_update(dristy_pipeline_tracker(), dets, det_count);
}

#if HK_ENABLE_APP_APRILTAG
static void export_apriltag_detector(dristy_result_snapshot_t *snap)
{
    const apriltag_result_t *results;
    uint8_t count;
    uint8_t tag_count = snap->tag_count;

    results = apriltag_detector_results(&count);
    if(!results || count == 0U)
        return;
    if(count > APRILTAG_RESULT_MAX)
        count = APRILTAG_RESULT_MAX;
    for(uint8_t i = 0U; i < count && tag_count < DRISTY_MAX_TAGS; i++)
    {
        tag_from_apriltag_result(&snap->tags[tag_count], &results[i], 320U, 240U);
        tag_count++;
    }
    snap->tag_count = tag_count;
}
#endif

#if HK_ENABLE_APP_OBJECT_DETECT
static void export_object_detector(dristy_result_snapshot_t *snap)
{
    const object_detect_result_t *results;
    uint8_t count;
    uint8_t det_count = snap->detection_count;
    dristy_detection_t dets[DRISTY_MAX_DETECTIONS];

    results = object_detect_detector_results(&count);
    if(!results || count == 0U)
        return;
    for(uint8_t i = 0U; i < count && det_count < DRISTY_MAX_DETECTIONS; i++)
    {
        const object_detect_result_t *r = &results[i];
        dristy_result_detection_t *d = &snap->detections[det_count];
        int16_t cx = (int16_t)(r->x + r->w / 2);
        int16_t cy = (int16_t)(r->y + r->h / 2);

        d->x = r->x;
        d->y = r->y;
        d->w = r->w;
        d->h = r->h;
        d->norm_cx = norm_x(cx, 320);
        d->norm_cy = norm_y(cy, 240);
        d->cls = r->class_id;
        d->confidence = r->confidence;
        dets[det_count].cx = cx;
        dets[det_count].cy = cy;
        dets[det_count].w = r->w;
        dets[det_count].h = r->h;
        dets[det_count].cls = r->class_id;
        dets[det_count].conf = r->confidence;
        det_count++;
    }
    snap->detection_count = det_count;
    if(det_count > 0U)
        dristy_tracker_update(dristy_pipeline_tracker(), dets, det_count);
}
#endif

#if HK_ENABLE_APP_FACE_DETECT
static void export_face_detector(dristy_result_snapshot_t *snap)
{
    const face_detect_box_t *boxes;
    uint8_t count;
    uint8_t det_count = snap->detection_count;

    boxes = face_detect_detector_boxes(&count);
    if(!boxes || count == 0U)
        return;
    for(uint8_t i = 0U; i < count && det_count < DRISTY_MAX_DETECTIONS; i++)
    {
        const face_detect_box_t *r = &boxes[i];
        dristy_result_detection_t *d = &snap->detections[det_count];
        int16_t cx = (int16_t)(r->x + r->w / 2);
        int16_t cy = (int16_t)(r->y + r->h / 2);

        d->x = r->x;
        d->y = r->y;
        d->w = r->w;
        d->h = r->h;
        d->norm_cx = norm_x(cx, 320);
        d->norm_cy = norm_y(cy, 240);
        d->cls = 0U;
        d->confidence = 900U;
        det_count++;
    }
    snap->detection_count = det_count;
}
#endif

#if HK_ENABLE_APP_QR_CAMERA
static void export_qr_payload(dristy_result_snapshot_t *snap)
{
    const char *text;
    size_t len;

    if(!qr_result_has_payload())
        return;
    text = qr_result_payload_text();
    if(!text || !text[0])
        return;
    len = strlen(text);
    if(len >= sizeof(snap->qr[0].data))
        len = sizeof(snap->qr[0].data) - 1U;
    snap->qr_count = 1U;
    snap->qr[0].data_len = (uint16_t)len;
    memcpy(snap->qr[0].data, text, len);
    snap->qr[0].data[len] = '\0';
    snap->qr[0].norm_cx = 0;
    snap->qr[0].norm_cy = 0;
}
#endif

void dristy_legacy_export_into_snap(dristy_result_snapshot_t *snap, dristy_mode_t mode)
{
    const dristy_mode_info_t *info = dristy_mode_info(mode);

    if(!snap || !info)
        return;

    export_vision_service(snap);

    if(mode == DRISTY_MODE_DETECT_TAG)
    {
#if HK_ENABLE_APP_OBJECT_DETECT
        export_object_detector(snap);
#endif
#if HK_ENABLE_APP_APRILTAG
        export_apriltag_detector(snap);
#endif
        return;
    }

    if(info->capabilities & DRISTY_CAP_KPU)
    {
#if HK_ENABLE_APP_OBJECT_DETECT
        if(mode == DRISTY_MODE_DETECT || mode == DRISTY_MODE_DETECT_TRACK ||
           mode == DRISTY_MODE_DETECT_CUSTOM || mode == DRISTY_MODE_CLASSIFY ||
           mode == DRISTY_MODE_DETECT_MOTION)
            export_object_detector(snap);
#endif
#if HK_ENABLE_APP_FACE_DETECT
        if(mode == DRISTY_MODE_FACE_DETECT || mode == DRISTY_MODE_FACE_RECOGNISE)
            export_face_detector(snap);
#endif
    }

    if(info->capabilities & DRISTY_CAP_TAGS)
    {
#if HK_ENABLE_APP_APRILTAG
        if(mode == DRISTY_MODE_APRILTAG || mode == DRISTY_MODE_LANDING_TARGET)
            export_apriltag_detector(snap);
#endif
    }

    if(info->capabilities & DRISTY_CAP_QR)
    {
#if HK_ENABLE_APP_QR_CAMERA
        if(mode == DRISTY_MODE_QR_CODE)
            export_qr_payload(snap);
#endif
    }
}

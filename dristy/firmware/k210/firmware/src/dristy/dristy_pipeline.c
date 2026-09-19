#include "dristy_pipeline.h"

#include <stdio.h>
#include <string.h>

#include "dristy_bench.h"
#include "dristy_nms.h"
#include "dristy_pose.h"
#include "dristy_result_bus.h"
#include "dristy_tracker.h"
#include "dristy_aruco.h"
#include "dristy_motion.h"
#include "dristy_legacy_export.h"
#include "dristy_colour.h"
#include "dristy_line.h"
#include "dristy_flow.h"
#include "dristy_model.h"

#include "../../drivers/camera_stream.h"
#include "../../services/ai_model_runtime.h"
#include "../../services/camera_ai_input.h"
#include "../../services/camera_session.h"
#include "../../services/internal/camera_session_state.h"
#include "../../services/vision_result_service.h"
#include "hal_time.h"

/* ---------------------------------------------------------------------------
 * Pipeline state
 * ------------------------------------------------------------------------- */

/* Camera dimensions (fixed at init) */
static uint16_t g_cam_w = 320;
static uint16_t g_cam_h = 240;

/* Current mode and config */
static dristy_mode_t g_mode = DRISTY_MODE_DETECT_TRACK;
static dristy_pipeline_config_t g_config;
static dristy_pipeline_state_t g_state;

/* Tracker instance */
static dristy_tracker_t g_tracker;

/* NMS config (runtime-adjustable) */
static dristy_nms_config_t g_nms_config = DRISTY_NMS_CONFIG_DEFAULT;

/* AI model runtime (platform SD + KPU services) */
static ai_model_runtime_t g_ai_runtime;
static uint8_t g_ai_initialized;

/* Target selection */
static uint16_t g_target_track_id;
static uint8_t g_target_class = 0xFF;
static uint8_t g_confidence_pct = 30;

/* FPS calculation */
static uint64_t g_fps_window_start_us;
static uint32_t g_fps_window_frames;
static uint16_t g_current_fps_x10;

/* Frame timing */
static uint64_t g_frame_start_us;
static uint32_t g_frame_number;

static uint8_t g_gray_buf[320U * 240U];
static uint32_t g_last_commit_stream_sequence;

static void pipeline_sync_camera_geometry(void)
{
    if(!camera_service_capture_ready())
        return;
    g_cam_w = camera_session_width();
    g_cam_h = camera_session_height();
}

/* ---------------------------------------------------------------------------
 * Coordinate conversion helpers
 * ------------------------------------------------------------------------- */

static dristy_norm_t px_to_norm_x(int16_t px)
{
    return dristy_pixel_to_norm_x(px, g_cam_w);
}

static dristy_norm_t px_to_norm_y(int16_t py)
{
    return dristy_pixel_to_norm_y(py, g_cam_h);
}

/* ---------------------------------------------------------------------------
 * Primary target selection
 *
 * For drone control, we need ONE target. Selection priority:
 * 1. If target_track_id > 0: find that specific track
 * 2. If target_class != 0xFF: find highest-confidence track of that class
 * 3. Otherwise: find the track closest to frame centre
 * ------------------------------------------------------------------------- */

static void select_primary_target(dristy_result_snapshot_t *snap)
{
    const dristy_result_track_t *best = 0;
    int32_t best_score = -1;

    snap->target.type = DRISTY_TARGET_NONE;

    /* Check tracked objects first (most useful for control) */
    for(uint8_t i = 0; i < snap->track_count; i++)
    {
        const dristy_result_track_t *t = &snap->tracks[i];

        /* Specific track ID requested? */
        if(g_target_track_id > 0)
        {
            if(t->id == g_target_track_id)
            {
                best = t;
                break;
            }
            continue;
        }

        /* Class filter */
        if(g_target_class != 0xFF && t->cls != g_target_class)
            continue;

        /* Score: prefer high confidence, close to centre, large size */
        int32_t dist_sq = (int32_t)t->norm_cx * t->norm_cx +
                          (int32_t)t->norm_cy * t->norm_cy;
        int32_t size_bonus = ((int32_t)t->w * t->h) / 100;
        int32_t conf_bonus = t->confidence;
        /* Score = confidence + size - distance_from_centre */
        int32_t score = conf_bonus * 2 + size_bonus - dist_sq / 100;

        if(score > best_score || !best)
        {
            best_score = score;
            best = t;
        }
    }

    if(best)
    {
        snap->target.type = DRISTY_TARGET_TRACK;
        snap->target.error_x = best->norm_cx;  /* positive = target is right of centre */
        snap->target.error_y = best->norm_cy;
        snap->target.size = (uint16_t)(((int32_t)best->w * 2000 / g_cam_w +
                                        (int32_t)best->h * 2000 / g_cam_h) / 2);
        snap->target.error_rate_x = best->vel_norm_x;
        snap->target.error_rate_y = best->vel_norm_y;
        snap->target.track_id = best->id;
        snap->target.cls = best->cls;
        snap->target.confidence = best->confidence;
        snap->target.has_3d = 0;
        return;
    }

    /* Fall back to AprilTag targets (for landing) */
    if(snap->tag_count > 0)
    {
        /* Pick the tag with the best pose (lowest reproj error) */
        const dristy_result_tag_t *best_tag = &snap->tags[0];
        for(uint8_t i = 1; i < snap->tag_count; i++)
        {
            if(snap->tags[i].pose_valid && (!best_tag->pose_valid ||
               snap->tags[i].reproj_error < best_tag->reproj_error))
                best_tag = &snap->tags[i];
        }

        snap->target.type = DRISTY_TARGET_TAG;
        snap->target.error_x = best_tag->norm_cx;
        snap->target.error_y = best_tag->norm_cy;
        snap->target.size = 0;
        snap->target.error_rate_x = 0;
        snap->target.error_rate_y = 0;
        snap->target.track_id = best_tag->tag_id;
        snap->target.cls = best_tag->family;
        snap->target.confidence = 1000;

        if(best_tag->pose_valid)
        {
            snap->target.has_3d = 1;
            snap->target.range_mm = best_tag->range_mm;
            /* bearing = atan2(tx, tz) in degrees */
            if(best_tag->tz_mm > 0.1f)
            {
                snap->target.bearing_deg =
                    best_tag->tx_mm * 57.2958f / best_tag->tz_mm;
                snap->target.elevation_deg =
                    best_tag->ty_mm * 57.2958f / best_tag->tz_mm;
            }
        }
        return;
    }

    /* Fall back to colour blobs */
    if(snap->blob_count > 0)
    {
        snap->target.type = DRISTY_TARGET_BLOB;
        snap->target.error_x = snap->blobs[0].norm_cx;
        snap->target.error_y = snap->blobs[0].norm_cy;
        snap->target.size = (uint16_t)(snap->blobs[0].pixel_count * 2000 /
                                        (g_cam_w * g_cam_h));
        snap->target.confidence = (uint16_t)(snap->blobs[0].density * 1000);
    }
}

/* ---------------------------------------------------------------------------
 * FPS calculation — sliding window, 1-second window
 * ------------------------------------------------------------------------- */

static void update_fps(uint64_t now_us)
{
    g_fps_window_frames++;
    uint64_t elapsed = now_us - g_fps_window_start_us;
    if(elapsed >= 1000000ULL)
    {
        g_current_fps_x10 = (uint16_t)(g_fps_window_frames * 10000000ULL / elapsed);
        g_fps_window_start_us = now_us;
        g_fps_window_frames = 0;
    }
}

/* ---------------------------------------------------------------------------
 * KPU inference result processing
 *
 * Called when KPU completes. Reads raw output, decodes YOLO boxes,
 * applies Soft-NMS, feeds tracker, populates result bus.
 * ------------------------------------------------------------------------- */

static void sync_vision_into_snap(dristy_result_snapshot_t *snap)
{
    dristy_legacy_export_into_snap(snap, g_mode);
}

static void process_kpu_result(dristy_result_snapshot_t *snap)
{
    const uint8_t *output;
    size_t output_bytes;

    sync_vision_into_snap(snap);

    if(!ai_model_runtime_take_completion(&g_ai_runtime))
        return;

    if(ai_model_runtime_get_output(&g_ai_runtime, 0, &output, &output_bytes) != 0)
    {
        camera_ai_input_arm(&g_ai_runtime);
        return;
    }

    snap->inference_us = (uint32_t)g_ai_runtime.last_inference_us;
    g_state.last_inference_us = snap->inference_us;
    g_state.inference_count++;

    /*
     * TODO: Call the appropriate post-processor based on model metadata.
     * For now, this is where the YOLO decode + NMS + tracker feeding
     * would go. The existing object_detect_postprocess() handles YOLO
     * decode — Dristy wraps it with Soft-NMS and the new tracker.
     *
     * Pseudocode for the full path:
     *
     * yolo_decode(output, output_bytes, &raw_boxes, &raw_count);
     * soft_nms(raw_boxes, raw_count, &nms_config);
     *
     * // Convert to detections
     * for each surviving box:
     *   snap->detections[i] = { box coords, normalised, class, conf };
     *
     * // Convert to tracker input
     * dristy_detection_t dets[20];
     * for each detection:
     *   dets[i] = { cx, cy, w, h, cls, conf };
     *
     * // Feed tracker
     * dristy_tracker_update(&g_tracker, dets, det_count);
     *
     * // Copy tracks to result bus
     * dristy_track_report_t reports[20];
     * uint8_t track_count = dristy_tracker_report(&g_tracker, reports, 20);
     * for each report:
     *   snap->tracks[i] = convert_to_result_track(report);
     */

    /* Re-arm camera AI input for next frame (mailbox: overwrites stale) */
    camera_ai_input_cancel(&g_ai_runtime);
    camera_ai_input_arm(&g_ai_runtime);
}

/* ---------------------------------------------------------------------------
 * Classical CV processing (AprilTag, QR, flow, colour)
 * Called on core 1 for modes that don't use the KPU.
 * ------------------------------------------------------------------------- */

static void rgb565_to_gray(const volatile uint16_t *rgb, uint8_t *gray,
                           uint16_t w, uint16_t h)
{
    uint32_t n = (uint32_t)w * (uint32_t)h;
    uint32_t i;

    if(n > (uint32_t)sizeof(g_gray_buf))
        n = (uint32_t)sizeof(g_gray_buf);

    for(i = 0U; i < n; i++)
    {
        uint16_t p = rgb[i];
        uint8_t r = (uint8_t)(((p >> 11) & 0x1FU) << 3);
        uint8_t g = (uint8_t)(((p >> 5) & 0x3FU) << 2);
        uint8_t b = (uint8_t)((p & 0x1FU) << 3);

        gray[i] = (uint8_t)((r * 77U + g * 150U + b * 29U) >> 8);
    }
}

static void classical_aruco(dristy_result_snapshot_t *snap,
                            const uint8_t *gray, uint16_t w, uint16_t h)
{
    uint8_t count;
    const dristy_aruco_detection_t *dets;
    uint8_t n;

    n = dristy_aruco_detect(gray, w, h);
    dets = dristy_aruco_results(&count);
    if(!dets || count == 0U)
        return;
    if(count > DRISTY_MAX_TAGS)
        count = DRISTY_MAX_TAGS;
    snap->tag_count = count;
    for(uint8_t i = 0U; i < count; i++)
    {
        dristy_result_tag_t *t = &snap->tags[i];
        const dristy_aruco_detection_t *d = &dets[i];

        t->tag_id = d->id;
        t->family = (uint8_t)d->dict;
        t->hamming = d->hamming;
        t->cx = d->cx;
        t->cy = d->cy;
        t->norm_cx = px_to_norm_x(d->cx);
        t->norm_cy = px_to_norm_y(d->cy);
        t->pose_valid = d->pose_valid;
        t->tx_mm = d->tx_mm;
        t->ty_mm = d->ty_mm;
        t->tz_mm = d->tz_mm;
        t->roll_deg = d->roll_deg;
        t->pitch_deg = d->pitch_deg;
        t->yaw_deg = d->yaw_deg;
        t->range_mm = d->range_mm;
        t->reproj_error = d->reproj_error;
        for(uint8_t c = 0U; c < 4U; c++)
        {
            t->corners[c][0] = (int16_t)d->corners[c][0];
            t->corners[c][1] = (int16_t)d->corners[c][1];
        }
    }
}

static void classical_motion(dristy_result_snapshot_t *snap,
                             const uint8_t *gray, uint16_t w, uint16_t h)
{
    const dristy_motion_result_t *mr;
    uint8_t i;

    (void)dristy_motion_process(gray, w, h);
    mr = dristy_motion_result();
    if(!mr || mr->region_count == 0U)
        return;
    snap->detection_count = mr->region_count;
    if(snap->detection_count > DRISTY_MAX_DETECTIONS)
        snap->detection_count = DRISTY_MAX_DETECTIONS;
    for(i = 0U; i < snap->detection_count; i++)
    {
        const dristy_motion_region_t *r = &mr->regions[i];
        dristy_result_detection_t *d = &snap->detections[i];

        d->x = r->x;
        d->y = r->y;
        d->w = r->w;
        d->h = r->h;
        d->norm_cx = px_to_norm_x((int16_t)(r->x + r->w / 2));
        d->norm_cy = px_to_norm_y((int16_t)(r->y + r->h / 2));
        d->confidence = (uint16_t)(r->intensity * 1000.0f);
        d->cls = 0U;
    }
}

static uint8_t classical_uses_camera_stream(dristy_mode_t mode)
{
    switch(mode)
    {
    case DRISTY_MODE_APRILTAG:
    case DRISTY_MODE_QR_CODE:
        return 0U;
    default:
        break;
    }
    {
        const dristy_mode_info_t *info = dristy_mode_info(mode);
        return (info && (info->capabilities & DRISTY_CAP_CLASSICAL)) ? 1U : 0U;
    }
}

static void process_classical(dristy_result_snapshot_t *snap,
                              const volatile uint16_t *frame_pixels)
{
    const uint8_t *gray = NULL;
    uint16_t w = g_cam_w;
    uint16_t h = g_cam_h;

    sync_vision_into_snap(snap);

    if(!classical_uses_camera_stream(g_mode))
        return;
    if(!frame_pixels)
        return;

    rgb565_to_gray(frame_pixels, g_gray_buf, w, h);
    gray = g_gray_buf;

    if(g_mode == DRISTY_MODE_COLOUR_TRACK || g_mode == DRISTY_MODE_COLOUR_SORT)
        dristy_colour_process_rgb565(frame_pixels, w, h, snap,
                                     (uint8_t)(g_mode == DRISTY_MODE_COLOUR_SORT));
    if(g_mode == DRISTY_MODE_LINE_FOLLOW)
        dristy_line_process_gray(gray, w, h, snap);
    if(g_mode == DRISTY_MODE_OPTICAL_FLOW || g_mode == DRISTY_MODE_LANDING_TARGET)
        dristy_flow_process_gray(gray, w, h, snap);
    if(g_mode == DRISTY_MODE_ARUCO || g_mode == DRISTY_MODE_DETECT_ARUCO)
        classical_aruco(snap, gray, w, h);
    if(g_mode == DRISTY_MODE_MOTION_DETECT)
        classical_motion(snap, gray, w, h);
    if(g_mode == DRISTY_MODE_DETECT_MOTION)
    {
        dristy_result_snapshot_t motion_only;

        memset(&motion_only, 0, sizeof(motion_only));
        classical_motion(&motion_only, gray, w, h);
        if(motion_only.detection_count == 0U)
            snap->detection_count = 0U;
    }
}

static uint8_t pipeline_commit_frame(const volatile uint16_t *pixels)
{
    uint64_t now_us;
    const dristy_mode_info_t *mode_info;
    dristy_result_snapshot_t *snap;

    mode_info = dristy_mode_info(g_mode);
    if(!mode_info)
        return 0U;

    dristy_bench_start(DRISTY_BENCH_FRAME_TOTAL);
    now_us = hal_time_us();
    snap = dristy_result_bus_begin_write();
    snap->timestamp_us = now_us;
    snap->frame_number = ++g_frame_number;
    snap->mode = g_mode;
    snap->kpu_active = (mode_info->capabilities & DRISTY_CAP_KPU) ? 1 : 0;
    g_frame_start_us = now_us;

    if(mode_info->capabilities & DRISTY_CAP_KPU)
    {
        dristy_bench_start(DRISTY_BENCH_KPU_INFERENCE);
        process_kpu_result(snap);
        dristy_bench_stop(DRISTY_BENCH_KPU_INFERENCE);
    }

    if(mode_info->capabilities & DRISTY_CAP_CLASSICAL)
        process_classical(snap, pixels);
    else
        sync_vision_into_snap(snap);

    dristy_bench_start(DRISTY_BENCH_TRACKER);
    if((mode_info->capabilities & DRISTY_CAP_TRACKER) &&
       snap->detection_count == 0 && g_tracker.track_count > 0)
        dristy_tracker_predict(&g_tracker);

    {
        dristy_track_report_t reports[DRISTY_MAX_TRACKS];
        uint8_t count = dristy_tracker_report(&g_tracker, reports, DRISTY_MAX_TRACKS);
        snap->track_count = count;
        for(uint8_t i = 0; i < count; i++)
        {
            dristy_result_track_t *rt = &snap->tracks[i];
            rt->cx = reports[i].cx;
            rt->cy = reports[i].cy;
            rt->w  = reports[i].w;
            rt->h  = reports[i].h;
            rt->norm_cx = px_to_norm_x((int16_t)(reports[i].cx));
            rt->norm_cy = px_to_norm_y((int16_t)(reports[i].cy));
            int32_t fps = g_current_fps_x10 > 0 ? g_current_fps_x10 : 200;
            rt->vel_norm_x = (int16_t)(
                (int32_t)reports[i].vx_x10 * 2000 / (int32_t)g_cam_w * fps / 100);
            rt->vel_norm_y = (int16_t)(
                (int32_t)reports[i].vy_x10 * 2000 / (int32_t)g_cam_h * fps / 100);
            rt->id = reports[i].id;
            rt->cls = reports[i].cls;
            rt->confidence = reports[i].confidence;
            rt->coasting = reports[i].coast_count > 0 ? 1 : 0;
            rt->age_frames = reports[i].age;
            rt->ttc_ms = 0;
        }
    }
    dristy_bench_stop(DRISTY_BENCH_TRACKER);

    dristy_bench_start(DRISTY_BENCH_TARGET_SELECT);
    select_primary_target(snap);
    dristy_bench_stop(DRISTY_BENCH_TARGET_SELECT);

    dristy_bench_start(DRISTY_BENCH_RESULT_BUS);
    snap->pipeline_us = (uint32_t)(hal_time_us() - g_frame_start_us);
    snap->fps_x10 = g_current_fps_x10;
    g_state.last_pipeline_us = snap->pipeline_us;
    g_state.frame_count = g_frame_number;
    g_state.fps_x10 = g_current_fps_x10;
    dristy_result_bus_commit();
    dristy_bench_stop(DRISTY_BENCH_RESULT_BUS);

    update_fps(now_us);
    dristy_bench_stop(DRISTY_BENCH_FRAME_TOTAL);
    dristy_bench_frame_end();
    return 1U;
}

/* ---------------------------------------------------------------------------
 * Public API
 * ------------------------------------------------------------------------- */

uint8_t dristy_pipeline_init(const dristy_pipeline_config_t *config)
{
    if(config)
        g_config = *config;
    else
    {
        dristy_pipeline_config_t defaults = DRISTY_PIPELINE_CONFIG_DEFAULT;
        g_config = defaults;
    }

    g_cam_w = g_config.camera_width;
    g_cam_h = g_config.camera_height;
    g_mode = g_config.initial_mode;
    g_target_track_id = g_config.target_track_id;
    g_target_class = g_config.target_class;

    /* Initialise subsystems */
    dristy_result_bus_init();
    dristy_tracker_init(&g_tracker, NULL);
    {
        dristy_aruco_config_t acfg = DRISTY_ARUCO_CONFIG_DEFAULT;
        dristy_motion_config_t mcfg = DRISTY_MOTION_CONFIG_DEFAULT;

        (void)dristy_aruco_init(&acfg);
        dristy_motion_init(&mcfg);
        dristy_colour_init();
        dristy_line_init();
        dristy_flow_init();
        dristy_model_init();
    }

    if(!g_ai_initialized)
    {
        ai_model_runtime_init(&g_ai_runtime);
        g_ai_initialized = 1;
    }

    memset(&g_state, 0, sizeof(g_state));
    g_state.mode = g_mode;
    g_frame_number = 0;

    printf("[DRISTY] pipeline init mode=%s cam=%ux%u\r\n",
           dristy_mode_name(g_mode), g_cam_w, g_cam_h);

    return 1;
}

uint8_t dristy_pipeline_start(void)
{
    if(g_state.running)
        return 1;

    g_fps_window_start_us = hal_time_us();
    g_fps_window_frames = 0;
    g_state.running = 1;

    printf("[DRISTY] pipeline started\r\n");
    return 1;
}

void dristy_pipeline_stop(void)
{
    if(!g_state.running)
        return;

    g_state.running = 0;
    printf("[DRISTY] pipeline stopped\r\n");
}

uint8_t dristy_pipeline_tick(void)
{
    const dristy_mode_info_t *mode_info;

    if(!g_state.running)
        return 0;

    mode_info = dristy_mode_info(g_mode);
    if(!mode_info)
        return 0;

    pipeline_sync_camera_geometry();

    if(!camera_service_capture_ready())
        return 0;

    /* Live preview already holds the RGB565 lease. Classical work must
     * ingest that buffer instead of acquire_latest() (which drops READY
     * slots and blanks the LCD). */
    if(classical_uses_camera_stream(g_mode))
        return 0;

    {
        camera_stream_status_t stream_st;

        camera_stream_status(&stream_st);
        if(!stream_st.have_frame ||
           stream_st.last_sequence == g_last_commit_stream_sequence)
            return 0;
        g_last_commit_stream_sequence = stream_st.last_sequence;
    }

    return pipeline_commit_frame(NULL);
}

uint8_t dristy_pipeline_ingest_rgb565(const volatile uint16_t *pixels,
                                      uint16_t width,
                                      uint16_t height)
{
    if(!g_state.running || !pixels || width == 0U || height == 0U)
        return 0U;
    g_cam_w = width;
    g_cam_h = height;
    return pipeline_commit_frame(pixels);
}

uint8_t dristy_pipeline_set_mode(dristy_mode_t mode)
{
    const dristy_mode_info_t *info = dristy_mode_info(mode);
    if(!info)
        return 0;

    printf("[DRISTY] mode switch: %s -> %s\r\n",
           dristy_mode_name(g_mode), dristy_mode_name(mode));

    /* Reset tracker on mode switch */
    dristy_tracker_reset(&g_tracker);

    g_mode = mode;
    g_state.mode = mode;
    g_last_commit_stream_sequence = 0U;

    if(info->model_slot != 0xFFU)
        (void)dristy_model_load_slot(info->model_slot);
    if(g_mode == DRISTY_MODE_OPTICAL_FLOW || g_mode == DRISTY_MODE_LANDING_TARGET)
        dristy_flow_init();
    if(g_mode == DRISTY_MODE_MOTION_DETECT || g_mode == DRISTY_MODE_DETECT_MOTION)
        dristy_motion_reset_background();

    return 1;
}

const dristy_pipeline_state_t *dristy_pipeline_state(void)
{
    return &g_state;
}

dristy_tracker_t *dristy_pipeline_tracker(void)
{
    return &g_tracker;
}

void dristy_pipeline_set_target_track(uint16_t track_id)
{
    g_target_track_id = track_id;
}

void dristy_pipeline_set_target_class(uint8_t class_id)
{
    g_target_class = class_id;
}

void dristy_pipeline_set_confidence(uint8_t percent)
{
    g_confidence_pct = percent > 100 ? 100 : percent;
    g_nms_config.score_threshold = (float)g_confidence_pct / 100.0f * 0.5f;
}

void dristy_pipeline_set_nms(uint8_t percent)
{
    g_nms_config.iou_threshold = (float)percent / 100.0f;
}

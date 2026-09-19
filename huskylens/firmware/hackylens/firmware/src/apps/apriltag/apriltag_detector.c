#include "apriltag_detector.h"

#include <stdio.h>
#include <string.h>

#include <hackylens/capability/time.h>

#include "apriltag.h"
#include "tag36h11.h"

#include "../../core/hk_capability_client.h"
#include "../../services/core1_executor.h"
#include "apriltag_config.h"

typedef enum
{
    APRILTAG_WORKER_OFF = 0,
    APRILTAG_WORKER_STARTING,
    APRILTAG_WORKER_READY,
    APRILTAG_WORKER_BUSY,
    APRILTAG_WORKER_STOPPING,
    APRILTAG_WORKER_ERROR,
} apriltag_worker_state_t;

typedef struct
{
    uint32_t epoch;
    uint8_t count;
    apriltag_result_t items[APRILTAG_RESULT_MAX];
} apriltag_result_bank_t;

static apriltag_detector_t *g_detector;
static apriltag_family_t *g_family;
static uint8_t g_luma[APRILTAG_INPUT_W * APRILTAG_INPUT_H] __attribute__((aligned(64)));
static apriltag_result_bank_t g_result_banks[2] __attribute__((aligned(64)));

typedef enum
{
    APRILTAG_JOB_NONE = 0,
    APRILTAG_JOB_CREATE,
    APRILTAG_JOB_DETECT,
    APRILTAG_JOB_DESTROY,
} apriltag_job_t;

static volatile uint8_t g_active;
static volatile uint8_t g_cleanup_pending;
static volatile uint8_t g_worker_state;
static volatile uint8_t g_job_kind;
static volatile uint32_t g_job_ticket;
static volatile uint32_t g_session_epoch;
static volatile uint32_t g_request_sequence;
static volatile uint32_t g_complete_sequence;
static volatile uint32_t g_published_sequence;
static volatile uint32_t g_request_epoch;
static volatile uint16_t g_request_width;
static volatile uint16_t g_request_height;
static volatile uint32_t g_frame_count;
static volatile uint32_t g_submit_count;
static volatile uint32_t g_busy_drop_count;
static volatile uint32_t g_last_us;
static volatile uint32_t g_last_quads;
static volatile uint32_t g_last_preprocess_us;
static volatile uint32_t g_last_quad_search_us;
static volatile uint32_t g_last_decode_total_us;
static volatile uint32_t g_last_refine_us;
static volatile uint32_t g_last_homography_us;
static volatile uint32_t g_last_payload_us;
static volatile uint32_t g_last_result_us;
static volatile uint32_t g_last_reconcile_us;
static volatile uint32_t g_last_cleanup_us;
static volatile uint8_t g_refine_edges_requested = APRILTAG_REFINE_EDGES;
static hk_time_t s_apriltag_time;
static hk_owner_t s_apriltag_time_owner;

static hk_result_t apriltag_time_prepare(hk_owner_t *owner)
{
    static const hk_capability_request_t request = HK_TIME_REQUEST_0_1_INIT;

    *owner = capability_client_consumer_owner(
        "consumer:apriltag-detector");
    if(hk_owner_is_zero(*owner))
        return HK_ERR_STALE_HANDLE;
    if(owner->slot != s_apriltag_time_owner.slot ||
       owner->generation != s_apriltag_time_owner.generation ||
       hk_lease_is_zero(&s_apriltag_time.lease))
    {
        s_apriltag_time.lease = HK_LEASE_NONE;
        s_apriltag_time_owner = *owner;
        return hk_time_acquire(*owner, &request, &s_apriltag_time);
    }
    return HK_OK;
}

static uint64_t apriltag_time_now_us(void)
{
    hk_owner_t owner;
    uint64_t value = 0U;

    if(apriltag_time_prepare(&owner) != HK_OK ||
       hk_time_now_us(owner, &s_apriltag_time, &value) != HK_OK)
        return 0U;
    return value;
}

static hk_result_t apriltag_time_deadline_after_us(
    uint64_t duration_us, hk_deadline_t *deadline)
{
    hk_owner_t owner;

    if(apriltag_time_prepare(&owner) != HK_OK)
        return HK_ERR_STALE_HANDLE;
    return hk_time_deadline_after_us(
        owner, &s_apriltag_time, duration_us, deadline);
}

static void apriltag_time_sleep_us(uint64_t duration_us)
{
    hk_owner_t owner;
    hk_deadline_t wake;

    if(apriltag_time_prepare(&owner) != HK_OK ||
       hk_time_deadline_after_us(
           owner, &s_apriltag_time, duration_us, &wake) != HK_OK)
        return;
    (void)hk_time_sleep_until(
        owner, &s_apriltag_time, wake, wake, NULL);
}

static void *shared_uncached(void *pointer)
{
    uintptr_t address = (uintptr_t)pointer;

    if(address >= 0x80000000UL && address < 0x80600000UL)
        address -= 0x40000000UL;
    return (void *)address;
}

static uint8_t *shared_luma(void)
{
    return (uint8_t *)shared_uncached(g_luma);
}

static apriltag_result_bank_t *shared_result_bank(uint8_t index)
{
    return (apriltag_result_bank_t *)shared_uncached(&g_result_banks[index & 1U]);
}

static uint8_t rgb565_luma(uint16_t color)
{
    uint16_t r = (uint16_t)(((color >> 11U) & 0x1fU) * 255U / 31U);
    uint16_t g = (uint16_t)(((color >> 5U) & 0x3fU) * 255U / 63U);
    uint16_t b = (uint16_t)((color & 0x1fU) * 255U / 31U);
    return (uint8_t)((r * 77U + g * 150U + b * 29U) >> 8U);
}

static int16_t scaled_coordinate(double value, uint16_t source_limit, uint16_t output_limit)
{
    int32_t coordinate = (int32_t)(value * output_limit / source_limit + 0.5);
    if(coordinate < 0)
        coordinate = 0;
    if(coordinate >= output_limit)
        coordinate = output_limit ? output_limit - 1 : 0;
    return (int16_t)coordinate;
}

static uint8_t detector_create(void)
{
    g_family = tag36h11_create();
    g_detector = apriltag_detector_create();
    if(!g_family || !g_detector)
        return 0;

    apriltag_detector_add_family_bits(g_detector, g_family, APRILTAG_MAX_HAMMING);
    g_detector->nthreads = 1;
    g_detector->quad_decimate = APRILTAG_QUAD_DECIMATE;
    g_detector->quad_sigma = 0.0f;
    g_detector->refine_edges = g_refine_edges_requested ? true : false;
    g_detector->decode_sharpening = 0.25;
    g_detector->debug = false;
    g_detector->qtp.min_cluster_pixels = APRILTAG_MIN_CLUSTER_PIXELS;
    g_detector->qtp.max_nmaxima = 10;
    g_detector->qtp.deglitch = 0;
    g_detector->profile_now_us = apriltag_time_now_us;
    return 1;
}

static void detector_destroy(void)
{
    if(g_detector)
        apriltag_detector_destroy(g_detector);
    if(g_family)
        tag36h11_destroy(g_family);
    g_detector = NULL;
    g_family = NULL;
}

static void detector_run(uint32_t request_sequence, uint32_t request_epoch,
                         uint16_t width, uint16_t height)
{
    image_u8_t image = {
        .width = APRILTAG_INPUT_W,
        .height = APRILTAG_INPUT_H,
        .stride = APRILTAG_INPUT_W,
        .buf = shared_luma(),
    };
    apriltag_result_bank_t *bank;
    zarray_t *detections;
    uint64_t started_us;
    uint32_t publish_sequence;

    started_us = apriltag_time_now_us();
    detections = apriltag_detector_detect(g_detector, &image);
    g_last_us = (uint32_t)(apriltag_time_now_us() - started_us);
    g_last_quads = g_detector->nquads;
    g_last_preprocess_us = g_detector->profile_preprocess_us;
    g_last_quad_search_us = g_detector->profile_quad_search_us;
    g_last_decode_total_us = g_detector->profile_decode_total_us;
    g_last_refine_us = g_detector->profile_refine_us;
    g_last_homography_us = g_detector->profile_homography_us;
    g_last_payload_us = g_detector->profile_payload_us;
    g_last_result_us = g_detector->profile_result_us;
    g_last_reconcile_us = g_detector->profile_reconcile_us;
    g_last_cleanup_us = g_detector->profile_cleanup_us;
    g_frame_count++;

    publish_sequence = g_published_sequence + 1U;
    if(publish_sequence == 0U)
        publish_sequence = 1U;
    bank = shared_result_bank((uint8_t)publish_sequence);
    bank->epoch = request_epoch;
    bank->count = 0;

    if(detections)
    {
        int detection_count = zarray_size(detections);

        for(int i = 0; i < detection_count && bank->count < APRILTAG_RESULT_MAX; i++)
        {
            apriltag_detection_t *detection;
            apriltag_result_t *result = &bank->items[bank->count];
            int16_t min_x = (int16_t)width;
            int16_t min_y = (int16_t)height;
            int16_t max_x = 0;
            int16_t max_y = 0;
            int32_t confidence;

            zarray_get(detections, i, &detection);
            if(!detection || detection->id < 0 || detection->id > 65535)
                continue;
            for(uint8_t corner = 0; corner < 4U; corner++)
            {
                int16_t x = scaled_coordinate(detection->p[corner][0], APRILTAG_INPUT_W, width);
                int16_t y = scaled_coordinate(detection->p[corner][1], APRILTAG_INPUT_H, height);
                result->corners[corner][0] = x;
                result->corners[corner][1] = y;
                if(x < min_x) min_x = x;
                if(y < min_y) min_y = y;
                if(x > max_x) max_x = x;
                if(y > max_y) max_y = y;
            }
            confidence = (int32_t)(detection->decision_margin * 10.0);
            if(confidence < 0) confidence = 0;
            if(confidence > 1000) confidence = 1000;
            result->id = (uint16_t)detection->id;
            result->hamming = (uint8_t)detection->hamming;
            result->confidence = (uint16_t)confidence;
            result->x = min_x;
            result->y = min_y;
            result->w = max_x > min_x ? max_x - min_x : 1;
            result->h = max_y > min_y ? max_y - min_y : 1;
            result->center_x = scaled_coordinate(detection->c[0], APRILTAG_INPUT_W, width);
            result->center_y = scaled_coordinate(detection->c[1], APRILTAG_INPUT_H, height);
            bank->count++;
        }
        apriltag_detections_destroy(detections);
    }

    __sync_synchronize();
    if(g_active && request_epoch == g_session_epoch)
        g_published_sequence = publish_sequence;
    g_complete_sequence = request_sequence;
}

static void apriltag_worker_job(void *context)
{
    apriltag_job_t job = (apriltag_job_t)g_job_kind;

    (void)context;
    if(job == APRILTAG_JOB_CREATE)
    {
        g_worker_state = APRILTAG_WORKER_STARTING;
        if(!detector_create())
        {
            detector_destroy();
            g_worker_state = APRILTAG_WORKER_ERROR;
        }
        else
            g_worker_state = APRILTAG_WORKER_READY;
    }
    else if(job == APRILTAG_JOB_DETECT)
    {
        uint32_t request_sequence = g_request_sequence;
        uint32_t request_epoch = g_request_epoch;
        uint16_t width = g_request_width;
        uint16_t height = g_request_height;

        if(g_detector)
        {
            g_worker_state = APRILTAG_WORKER_BUSY;
            g_detector->refine_edges = g_refine_edges_requested ? true : false;
            detector_run(request_sequence, request_epoch, width, height);
            g_worker_state = g_active ? APRILTAG_WORKER_READY : APRILTAG_WORKER_STOPPING;
        }
    }
    else if(job == APRILTAG_JOB_DESTROY)
    {
        g_worker_state = APRILTAG_WORKER_STOPPING;
        detector_destroy();
        g_complete_sequence = g_request_sequence;
        g_worker_state = APRILTAG_WORKER_OFF;
    }
    __sync_synchronize();
}

void apriltag_detector_service_tick(void)
{
    uint32_t ticket = g_job_ticket;

    if(ticket && core1_executor_complete(ticket))
    {
        g_job_ticket = 0U;
        g_job_kind = APRILTAG_JOB_NONE;
        __sync_synchronize();
    }

    if(!g_active && g_cleanup_pending && !g_job_ticket)
    {
        if(g_detector)
        {
            g_job_kind = APRILTAG_JOB_DESTROY;
            __sync_synchronize();
            g_job_ticket = core1_executor_submit(apriltag_worker_job, NULL);
            if(!g_job_ticket)
                g_job_kind = APRILTAG_JOB_NONE;
        }
        else
        {
            g_cleanup_pending = 0U;
            g_worker_state = APRILTAG_WORKER_OFF;
        }
    }
}

uint8_t apriltag_detector_init(void)
{
    hk_deadline_t deadline;

    if(apriltag_time_deadline_after_us(
           APRILTAG_START_TIMEOUT_US, &deadline) != HK_OK)
    {
        g_worker_state = APRILTAG_WORKER_ERROR;
        return 0U;
    }

    if(!core1_executor_init())
    {
        g_worker_state = APRILTAG_WORKER_ERROR;
        return 0U;
    }
    while((g_cleanup_pending || !core1_executor_idle()) &&
          apriltag_time_now_us() < deadline.at_us)
    {
        apriltag_detector_service_tick();
        apriltag_time_sleep_us(1000U);
    }
    if(g_cleanup_pending || !core1_executor_idle())
    {
        g_worker_state = APRILTAG_WORKER_ERROR;
        return 0U;
    }
    g_session_epoch++;
    if(g_session_epoch == 0U)
        g_session_epoch++;
    g_active = 1U;
    g_cleanup_pending = 0U;
    g_worker_state = APRILTAG_WORKER_STARTING;
    g_job_kind = APRILTAG_JOB_CREATE;
    __sync_synchronize();
    g_job_ticket = core1_executor_submit(apriltag_worker_job, NULL);
    if(!g_job_ticket ||
       !core1_executor_wait(g_job_ticket, APRILTAG_START_TIMEOUT_US))
    {
        g_active = 0U;
        /*
         * A timed-out CREATE can still complete after this core returns. Keep
         * cleanup armed even when the worker has not published g_detector yet.
         */
        g_cleanup_pending = g_job_ticket ? 1U : (g_detector ? 1U : 0U);
        g_worker_state = APRILTAG_WORKER_ERROR;
        return 0U;
    }
    apriltag_detector_service_tick();
    if(g_worker_state != APRILTAG_WORKER_READY)
    {
        g_active = 0U;
        g_cleanup_pending = g_detector ? 1U : 0U;
        return 0U;
    }

    printf("[APRILTAG] core1 TAG36H11 %ux%u decimate=2 refine=%u hamming<=%d\r\n",
           APRILTAG_INPUT_W, APRILTAG_INPUT_H, g_refine_edges_requested,
           APRILTAG_MAX_HAMMING);
    return 1U;
}

void apriltag_detector_deinit(void)
{
    g_session_epoch++;
    if(g_session_epoch == 0U)
        g_session_epoch++;
    g_active = 0U;
    g_cleanup_pending = g_detector ? 1U : 0U;
    __sync_synchronize();
    apriltag_detector_service_tick();
}

uint8_t apriltag_detector_submit(const volatile uint16_t *pixels, uint16_t width, uint16_t height)
{
    uint8_t *luma;
    uint32_t request_sequence;

    apriltag_detector_service_tick();
    if(!pixels || !width || !height || !g_active)
        return 0;
    if(g_worker_state != APRILTAG_WORKER_READY ||
       g_request_sequence != g_complete_sequence || g_job_ticket)
    {
        g_busy_drop_count++;
        return 0;
    }

    request_sequence = g_request_sequence + 1U;
    if(request_sequence == 0U)
        request_sequence = 1U;
    luma = shared_luma();
    for(uint16_t y = 0; y < APRILTAG_INPUT_H; y++)
    {
        uint32_t source_y = (uint32_t)y * height / APRILTAG_INPUT_H;
        const volatile uint16_t *row = pixels + source_y * width;
        uint8_t *output = luma + (uint32_t)y * APRILTAG_INPUT_W;

        for(uint16_t x = 0; x < APRILTAG_INPUT_W; x++)
            output[x] = rgb565_luma(row[(uint32_t)x * width / APRILTAG_INPUT_W]);
    }

    g_request_width = width;
    g_request_height = height;
    g_request_epoch = g_session_epoch;
    g_job_kind = APRILTAG_JOB_DETECT;
    __sync_synchronize();
    g_request_sequence = request_sequence;
    g_job_ticket = core1_executor_submit(apriltag_worker_job, NULL);
    if(!g_job_ticket)
    {
        g_job_kind = APRILTAG_JOB_NONE;
        g_request_sequence = g_complete_sequence;
        g_busy_drop_count++;
        return 0U;
    }
    g_submit_count++;
    return 1;
}

void apriltag_detector_set_refine_edges(uint8_t enabled)
{
    g_refine_edges_requested = enabled ? 1U : 0U;
    __sync_synchronize();
}

const apriltag_result_t *apriltag_detector_results(uint8_t *count)
{
    uint32_t published_sequence = g_published_sequence;
    apriltag_result_bank_t *bank;

    __sync_synchronize();
    bank = shared_result_bank((uint8_t)published_sequence);
    if(!g_active || published_sequence == 0U || bank->epoch != g_session_epoch)
    {
        if(count)
            *count = 0;
        return bank->items;
    }
    if(count)
        *count = bank->count > APRILTAG_RESULT_MAX ? APRILTAG_RESULT_MAX : bank->count;
    return bank->items;
}

uint32_t apriltag_detector_result_sequence(void)
{
    return g_published_sequence;
}

void apriltag_detector_format_info(char *line, size_t line_size)
{
    static const char *const states[] = {"OFF", "STARTING", "READY", "BUSY", "STOPPING", "ERROR"};
    uint8_t state = g_worker_state;
    uint8_t count = 0;

    (void)apriltag_detector_results(&count);
    if(state >= sizeof(states) / sizeof(states[0]))
        state = APRILTAG_WORKER_ERROR;
    snprintf(line, line_size,
             "HKTAGINFO state=%s core=1 family=TAG36H11 input=%ux%u decimate=2 edge_refine=%u frames=%u submitted=%u "
             "busy_drop=%u us=%u quads=%u tags=%u pre=%u search=%u decode=%u refine=%u homo=%u "
             "payload=%u result=%u reconcile=%u cleanup=%u\r\n",
             states[state], APRILTAG_INPUT_W, APRILTAG_INPUT_H, g_refine_edges_requested,
             (unsigned)g_frame_count, (unsigned)g_submit_count, (unsigned)g_busy_drop_count,
             (unsigned)g_last_us, (unsigned)g_last_quads, (unsigned)count,
             (unsigned)g_last_preprocess_us, (unsigned)g_last_quad_search_us,
             (unsigned)g_last_decode_total_us, (unsigned)g_last_refine_us,
             (unsigned)g_last_homography_us, (unsigned)g_last_payload_us,
             (unsigned)g_last_result_us, (unsigned)g_last_reconcile_us,
             (unsigned)g_last_cleanup_us);
}

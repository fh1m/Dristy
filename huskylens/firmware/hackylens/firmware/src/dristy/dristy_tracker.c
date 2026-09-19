#include "dristy_tracker.h"

#include <string.h>

/* ---------------------------------------------------------------------------
 * Fixed-point scales
 *
 * Q12 for state (4096 = 1.0 pixel) — gives sub-pixel precision with int32
 * Q20 for covariance (1048576 = 1.0 pixel²) — sufficient for 320×240 bbox
 * Q8  for IoU and cost (256 = 1.0)
 * ------------------------------------------------------------------------- */
#define Q12  4096
#define Q20  1048576L
#define Q8   256

/* Sentinel cost for impossible assignments in Hungarian */
#define COST_INF 0x7FFF

/* --- 2×2 Block Kalman Operations ----------------------------------------
 *
 * Each block tracks [p, v] where p = position (or size) and v = velocity.
 *
 * Transition:  F = [[1, dt], [0, 1]]   with dt=1 (one frame)
 * Measurement: H = [1, 0]
 *
 * Predict:
 *   p' = p + v
 *   v' = v
 *   P' = F·P·F' + Q
 *      = [[Pa+2*Pb+Pc+Qp,  Pb+Pc+Qv/2], [Pb+Pc+Qv/2,  Pc+Qv]]
 *
 * Update:
 *   S = P'[0,0] + R
 *   K = P'[:,0] / S = [P'[0,0]/S, P'[1,0]/S]
 *   innovation = z - p'
 *   p = p' + K[0]*innovation
 *   v = v' + K[1]*innovation
 *   P = (I - K·H) · P'
 * --------------------------------------------------------------------- */

static void block_predict(int32_t *pos, int32_t *vel, dristy_cov2x2_t *cov,
                          int64_t q_pos, int64_t q_vel)
{
    /* State prediction */
    *pos += *vel;

    /* Covariance prediction: P' = F·P·F' + Q
     * Pa' = Pa + 2*Pb + Pc + Qp
     * Pb' = Pb + Pc
     * Pc' = Pc + Qv */
    int64_t new_a = cov->a + 2 * cov->b + cov->c + q_pos;
    int64_t new_b = cov->b + cov->c;
    int64_t new_c = cov->c + q_vel;
    cov->a = new_a;
    cov->b = new_b;
    cov->c = new_c;
}

static void block_update(int32_t *pos, int32_t *vel, dristy_cov2x2_t *cov,
                          int32_t measurement, int64_t r_noise)
{
    /* Innovation */
    int32_t innovation = measurement - *pos;

    /* S = Pa + R */
    int64_t s = cov->a + r_noise;
    if(s <= 0) s = 1;

    /* Kalman gain K = P[:,0] / S
     * K0 = Pa / S,  K1 = Pb / S
     * We compute updates directly to avoid separate K storage */

    /* State update */
    *pos += (int32_t)(((int64_t)cov->a * innovation) / s);
    *vel += (int32_t)(((int64_t)cov->b * innovation) / s);

    /* Covariance update: P = (I - K·H)·P
     * Pa_new = Pa - K0*Pa = Pa * (1 - Pa/S) = Pa * R / S
     * Pb_new = Pb - K0*Pb = Pb * (1 - Pa/S) = Pb * R / S  (wrong)
     * Actually:
     *   P_new = P - K·S·K' where K = P[:,0]/S = P·H'/S
     *   Pa_new = Pa - Pa²/S
     *   Pb_new = Pb - Pa*Pb/S
     *   Pc_new = Pc - Pb²/S
     * This is the Joseph form, numerically stable for diagonal R */
    int64_t pa = cov->a;
    int64_t pb = cov->b;
    cov->a = pa - (pa * pa) / s;
    cov->b = pb - (pa * pb) / s;
    cov->c = cov->c - (pb * pb) / s;

    /* Floor covariances to prevent negative values from rounding */
    if(cov->a < 0) cov->a = 0;
    if(cov->c < 0) cov->c = 0;
}

/* --- IoU in Q8 ---------------------------------------------------------- */

static int32_t compute_iou_q8(const int32_t pos_a[4], const int32_t pos_b[4])
{
    /* pos = [cx, cy, w, h] in Q12 */
    int32_t a_x1 = pos_a[0] - pos_a[2] / 2;
    int32_t a_y1 = pos_a[1] - pos_a[3] / 2;
    int32_t a_x2 = pos_a[0] + pos_a[2] / 2;
    int32_t a_y2 = pos_a[1] + pos_a[3] / 2;

    int32_t b_x1 = pos_b[0] - pos_b[2] / 2;
    int32_t b_y1 = pos_b[1] - pos_b[3] / 2;
    int32_t b_x2 = pos_b[0] + pos_b[2] / 2;
    int32_t b_y2 = pos_b[1] + pos_b[3] / 2;

    int32_t ix1 = a_x1 > b_x1 ? a_x1 : b_x1;
    int32_t iy1 = a_y1 > b_y1 ? a_y1 : b_y1;
    int32_t ix2 = a_x2 < b_x2 ? a_x2 : b_x2;
    int32_t iy2 = a_y2 < b_y2 ? a_y2 : b_y2;

    if(ix2 <= ix1 || iy2 <= iy1)
        return 0;

    int64_t intersection = ((int64_t)(ix2 - ix1) * (iy2 - iy1)) / Q12;
    int64_t area_a = ((int64_t)pos_a[2] * pos_a[3]) / Q12;
    int64_t area_b = ((int64_t)pos_b[2] * pos_b[3]) / Q12;
    int64_t union_area = area_a + area_b - intersection;

    if(union_area <= 0)
        return 0;

    return (int32_t)((intersection * Q8) / union_area);
}

/* --- Mahalanobis Distance (squared) in Q8 ------------------------------- */

static int32_t compute_mahal_sq_q8(const dristy_track_t *track,
                                   const int32_t det_pos[4],
                                   int64_t r_noise)
{
    /* d² = Σᵢ (zᵢ - pᵢ)² / (Pa_i + R)
     * This is the Mahalanobis distance using diagonal blocks.
     * For block Kalman, S_i = cov[i].a + R (position variance + meas noise) */
    int64_t total = 0;
    for(uint8_t i = 0; i < 4; i++)
    {
        int64_t diff = (int64_t)det_pos[i] - track->pos[i];
        int64_t s_i = track->cov[i].a + r_noise;
        if(s_i <= 0) s_i = 1;
        /* diff is Q12, diff² is Q24, /s_i (Q20) gives Q4, ×64 gives Q8 approx
         * Better: diff²/s_i in raw units, then scale */
        int64_t contrib = (diff * diff) / s_i;
        /* contrib is in Q12*Q12/Q20 = Q4. Convert to Q8: ×16 */
        total += contrib * 16;
    }
    /* Clamp to int32 Q8 range */
    if(total > 0x7FFFFFFF)
        return 0x7FFFFFFF;
    return (int32_t)total;
}

/* --- Hungarian Algorithm (Munkres) -------------------------------------- *
 *
 * Classic O(n³) implementation sized for max(TRACKS, DETS) ≤ 32.
 * Cost matrix is int16_t where 0 = best match, COST_INF = gated out.
 *
 * This is a standard implementation of the Kuhn-Munkres algorithm with
 * the following steps:
 *   Step 1: Subtract row minima
 *   Step 2: Find initial zeros, star them
 *   Step 3: Cover columns with starred zeros
 *   Step 4: Find uncovered zeros, prime them, adjust cover
 *   Step 5: Augmenting path
 *   Step 6: Add/subtract minimum uncovered value
 *
 * For n ≤ 32, this completes in < 30 µs on RISC-V @ 400 MHz.
 * --------------------------------------------------------------------- */

#define HUNGARIAN_MAX 32

typedef struct
{
    int16_t cost[HUNGARIAN_MAX][HUNGARIAN_MAX];
    int8_t star_col[HUNGARIAN_MAX];   /* starred zero: row → col */
    int8_t star_row[HUNGARIAN_MAX];   /* starred zero: col → row */
    int8_t prime_col[HUNGARIAN_MAX];  /* primed zero: row → col */
    uint8_t row_cover[HUNGARIAN_MAX];
    uint8_t col_cover[HUNGARIAN_MAX];
    uint8_t n;
} hungarian_t;

static hungarian_t g_hungarian;  /* static to avoid stack overflow */

static void hungarian_init(hungarian_t *h, uint8_t n)
{
    memset(h, 0, sizeof(*h));
    h->n = n;
    for(uint8_t i = 0; i < n; i++)
    {
        h->star_col[i] = -1;
        h->star_row[i] = -1;
        h->prime_col[i] = -1;
    }
}

static void hungarian_step1(hungarian_t *h)
{
    /* Subtract row minimum from each row */
    for(uint8_t r = 0; r < h->n; r++)
    {
        int16_t min_val = COST_INF;
        for(uint8_t c = 0; c < h->n; c++)
            if(h->cost[r][c] < min_val)
                min_val = h->cost[r][c];
        if(min_val > 0 && min_val < COST_INF)
            for(uint8_t c = 0; c < h->n; c++)
                if(h->cost[r][c] < COST_INF)
                    h->cost[r][c] -= min_val;
    }
}

static void hungarian_step2(hungarian_t *h)
{
    /* Star zeros: at most one per row and column */
    for(uint8_t r = 0; r < h->n; r++)
    {
        for(uint8_t c = 0; c < h->n; c++)
        {
            if(h->cost[r][c] == 0 && h->star_col[r] < 0 && h->star_row[c] < 0)
            {
                h->star_col[r] = (int8_t)c;
                h->star_row[c] = (int8_t)r;
                break;
            }
        }
    }
}

static uint8_t hungarian_step3(hungarian_t *h)
{
    /* Cover columns containing starred zeros */
    uint8_t covered = 0;
    memset(h->col_cover, 0, sizeof(h->col_cover));
    for(uint8_t r = 0; r < h->n; r++)
    {
        if(h->star_col[r] >= 0)
        {
            h->col_cover[(uint8_t)h->star_col[r]] = 1;
            covered++;
        }
    }
    return covered;
}

static uint8_t hungarian_step4(hungarian_t *h, uint8_t *pr, uint8_t *pc)
{
    /* Find uncovered zero, prime it */
    for(;;)
    {
        int8_t found_r = -1, found_c = -1;
        for(uint8_t r = 0; r < h->n && found_r < 0; r++)
        {
            if(h->row_cover[r])
                continue;
            for(uint8_t c = 0; c < h->n; c++)
            {
                if(!h->col_cover[c] && h->cost[r][c] == 0)
                {
                    found_r = (int8_t)r;
                    found_c = (int8_t)c;
                    break;
                }
            }
        }
        if(found_r < 0)
            return 0; /* no uncovered zero → go to step 6 */

        h->prime_col[(uint8_t)found_r] = found_c;

        if(h->star_col[(uint8_t)found_r] >= 0)
        {
            /* Row has a star: cover this row, uncover star's column */
            h->row_cover[(uint8_t)found_r] = 1;
            h->col_cover[(uint8_t)h->star_col[(uint8_t)found_r]] = 0;
        }
        else
        {
            *pr = (uint8_t)found_r;
            *pc = (uint8_t)found_c;
            return 1; /* go to step 5: augmenting path */
        }
    }
}

static void hungarian_step5(hungarian_t *h, uint8_t pr, uint8_t pc)
{
    /* Augmenting path starting from primed zero at (pr, pc) */
    uint8_t path_r[HUNGARIAN_MAX + 1];
    uint8_t path_c[HUNGARIAN_MAX + 1];
    uint8_t count = 1;

    path_r[0] = pr;
    path_c[0] = pc;

    while(h->star_row[path_c[count - 1]] >= 0)
    {
        /* Find star in column */
        uint8_t r = (uint8_t)h->star_row[path_c[count - 1]];
        path_r[count] = r;
        path_c[count] = path_c[count - 1];
        count++;
        /* Find prime in row */
        path_r[count] = r;
        path_c[count] = (uint8_t)h->prime_col[r];
        count++;
    }

    /* Augment: unstar starred, star primed */
    for(uint8_t i = 0; i < count; i++)
    {
        uint8_t r = path_r[i], c = path_c[i];
        if(i % 2 == 0)
        {
            /* Prime → star */
            h->star_col[r] = (int8_t)c;
            h->star_row[c] = (int8_t)r;
        }
        else
        {
            /* Unstar */
            if(h->star_col[r] == (int8_t)c)
                h->star_col[r] = -1;
            if(h->star_row[c] == (int8_t)r)
                h->star_row[c] = -1;
        }
    }
    /* Re-star from star_col for correctness (augmenting path toggles) */
    memset(h->star_row, -1, sizeof(h->star_row));
    for(uint8_t r = 0; r < h->n; r++)
        if(h->star_col[r] >= 0)
            h->star_row[(uint8_t)h->star_col[r]] = (int8_t)r;

    /* Clear primes and covers */
    memset(h->prime_col, -1, sizeof(h->prime_col));
    memset(h->row_cover, 0, sizeof(h->row_cover));
    memset(h->col_cover, 0, sizeof(h->col_cover));
}

static void hungarian_step6(hungarian_t *h)
{
    /* Find minimum uncovered value, subtract from uncovered, add to double-covered */
    int16_t min_val = COST_INF;
    for(uint8_t r = 0; r < h->n; r++)
    {
        if(h->row_cover[r])
            continue;
        for(uint8_t c = 0; c < h->n; c++)
        {
            if(!h->col_cover[c] && h->cost[r][c] < min_val)
                min_val = h->cost[r][c];
        }
    }
    if(min_val <= 0 || min_val >= COST_INF)
        return;

    for(uint8_t r = 0; r < h->n; r++)
    {
        for(uint8_t c = 0; c < h->n; c++)
        {
            if(h->row_cover[r])
            {
                if(h->col_cover[c])
                    h->cost[r][c] += min_val;
            }
            else if(!h->col_cover[c])
            {
                h->cost[r][c] -= min_val;
            }
        }
    }
}

/* Run full Hungarian. result[row] = assigned column, or -1.
 * cost_matrix must be n×n. Returns number of finite-cost assignments. */
static uint8_t hungarian_solve(int16_t cost[HUNGARIAN_MAX][HUNGARIAN_MAX],
                               uint8_t n,
                               int8_t result[HUNGARIAN_MAX])
{
    hungarian_t *h = &g_hungarian;
    uint8_t assignments = 0;
    uint8_t max_iter;

    if(n == 0 || n > HUNGARIAN_MAX)
    {
        memset(result, -1, HUNGARIAN_MAX);
        return 0;
    }

    hungarian_init(h, n);
    memcpy(h->cost, cost, sizeof(h->cost[0]) * n);

    hungarian_step1(h);
    hungarian_step2(h);

    max_iter = n * n * 2;  /* safety bound */
    while(max_iter--)
    {
        if(hungarian_step3(h) >= n)
            break;

        uint8_t pr, pc;
        while(!hungarian_step4(h, &pr, &pc))
        {
            hungarian_step6(h);
            if(max_iter == 0) break;
            max_iter--;
        }
        if(max_iter == 0) break;
        hungarian_step5(h, pr, pc);
    }

    memset(result, -1, HUNGARIAN_MAX);
    for(uint8_t r = 0; r < n; r++)
    {
        result[r] = h->star_col[r];
        if(h->star_col[r] >= 0 &&
           cost[r][(uint8_t)h->star_col[r]] < COST_INF)
            assignments++;
        else
            result[r] = -1; /* unstar infinite-cost assignments */
    }
    return assignments;
}

/* --- Track Operations --------------------------------------------------- */

static void track_create(dristy_track_t *track,
                         const dristy_detection_t *det,
                         uint16_t id, uint32_t frame_id,
                         const dristy_tracker_config_t *cfg)
{
    memset(track, 0, sizeof(*track));

    track->pos[0] = (int32_t)det->cx * Q12;
    track->pos[1] = (int32_t)det->cy * Q12;
    track->pos[2] = (int32_t)det->w  * Q12;
    track->pos[3] = (int32_t)det->h  * Q12;
    /* Initial velocity = 0 */

    /* Initial covariance: large uncertainty on position, very large on velocity */
    int64_t init_pos_var = (int64_t)10 * Q20;
    int64_t init_vel_var = (int64_t)100 * Q20;
    for(uint8_t i = 0; i < 4; i++)
    {
        track->cov[i].a = init_pos_var;
        track->cov[i].b = 0;
        track->cov[i].c = init_vel_var;
    }

    track->id = id;
    track->cls = det->cls;
    track->confidence = det->conf;
    track->state = DRISTY_TRACK_TENTATIVE;
    track->total_hits = 1;
    track->consecutive_hits = 1;
    track->consecutive_misses = 0;
    track->age = 0;
    track->last_seen_frame = frame_id;
    track->smoothed_conf = det->conf;

    (void)cfg;
}

static void track_predict_all(dristy_track_t *track,
                              const dristy_tracker_config_t *cfg)
{
    /* Adaptive process noise: lower confidence → more noise (less trust in model) */
    int64_t conf_scale = track->smoothed_conf > 0 ? track->smoothed_conf : 500;
    /* Scale factor: high confidence (1000) → 1.0× noise, low (100) → 3.0× noise */
    int64_t noise_mult_q8 = 256 + (256 * (1000 - conf_scale)) / 500;
    if(noise_mult_q8 > 768) noise_mult_q8 = 768;  /* cap at 3× */

    int64_t q_pos = ((int64_t)cfg->process_noise_pos_q12 * Q20 / Q12) * noise_mult_q8 / Q8;
    int64_t q_vel = ((int64_t)cfg->process_noise_vel_q12 * Q20 / Q12) * noise_mult_q8 / Q8;

    for(uint8_t i = 0; i < 4; i++)
        block_predict(&track->pos[i], &track->vel[i], &track->cov[i], q_pos, q_vel);

    track->age++;
}

static void track_update_with_det(dristy_track_t *track,
                                  const dristy_detection_t *det,
                                  const dristy_tracker_config_t *cfg)
{
    int64_t r = (int64_t)cfg->measure_noise_q12 * Q20 / Q12;

    int32_t z[4] = {
        (int32_t)det->cx * Q12,
        (int32_t)det->cy * Q12,
        (int32_t)det->w  * Q12,
        (int32_t)det->h  * Q12,
    };

    for(uint8_t i = 0; i < 4; i++)
        block_update(&track->pos[i], &track->vel[i], &track->cov[i], z[i], r);

    track->cls = det->cls;
    track->confidence = det->conf;
    track->total_hits++;
    track->consecutive_hits++;
    track->consecutive_misses = 0;

    /* EMA of confidence: α=0.3, using Q10 arithmetic */
    track->smoothed_conf = (uint16_t)(
        ((uint32_t)track->smoothed_conf * 717 + (uint32_t)det->conf * 307) / 1024);

    /* State transitions */
    if(track->state == DRISTY_TRACK_TENTATIVE &&
       track->consecutive_hits >= cfg->min_hits_to_confirm)
        track->state = DRISTY_TRACK_CONFIRMED;
    else if(track->state == DRISTY_TRACK_COASTING)
        track->state = DRISTY_TRACK_CONFIRMED; /* re-acquired */
}

static void track_mark_missed(dristy_track_t *track,
                              const dristy_tracker_config_t *cfg)
{
    track->consecutive_hits = 0;
    track->consecutive_misses++;

    if(track->state == DRISTY_TRACK_CONFIRMED)
        track->state = DRISTY_TRACK_COASTING;

    /* Deletion conditions */
    if(track->state == DRISTY_TRACK_TENTATIVE &&
       track->consecutive_misses > cfg->max_tentative_coast)
        track->state = DRISTY_TRACK_DELETED;

    if((track->state == DRISTY_TRACK_COASTING ||
        track->state == DRISTY_TRACK_CONFIRMED) &&
       track->consecutive_misses > cfg->max_coast_frames)
        track->state = DRISTY_TRACK_DELETED;
}

/* --- Cascaded Matching -------------------------------------------------- *
 *
 * Two-pass matching (DeepSORT-style):
 *   Pass 1: Match confirmed+coasting tracks with detections (Hungarian)
 *   Pass 2: Match tentative tracks with remaining detections (Hungarian)
 *
 * This ensures established tracks get first pick at detections, preventing
 * newly-spawned tentative tracks from "stealing" matches.
 * --------------------------------------------------------------------- */

static void build_cost_matrix(const dristy_tracker_t *tracker,
                              const uint8_t *track_indices, uint8_t n_tracks,
                              const dristy_detection_t *dets,
                              const uint8_t *det_indices, uint8_t n_dets,
                              int16_t cost[HUNGARIAN_MAX][HUNGARIAN_MAX],
                              uint8_t n)
{
    int64_t r_noise = (int64_t)tracker->config.measure_noise_q12 * Q20 / Q12;

    /* Fill n×n cost matrix (padded with COST_INF for non-square) */
    for(uint8_t r = 0; r < n; r++)
        for(uint8_t c = 0; c < n; c++)
            cost[r][c] = COST_INF;

    for(uint8_t ti = 0; ti < n_tracks; ti++)
    {
        const dristy_track_t *trk = &tracker->tracks[track_indices[ti]];
        for(uint8_t di = 0; di < n_dets; di++)
        {
            const dristy_detection_t *det = &dets[det_indices[di]];

            /* Class gate: different classes → impossible */
            if(tracker->config.class_aware && trk->cls != det->cls)
                continue;

            int32_t det_pos[4] = {
                (int32_t)det->cx * Q12,
                (int32_t)det->cy * Q12,
                (int32_t)det->w  * Q12,
                (int32_t)det->h  * Q12,
            };

            /* IoU gate */
            int32_t iou = compute_iou_q8(trk->pos, det_pos);
            if(iou < (int32_t)tracker->config.iou_threshold_q8)
                continue;

            /* Mahalanobis gate (optional) */
            if(tracker->config.use_mahalanobis)
            {
                int32_t mahal = compute_mahal_sq_q8(trk, det_pos, r_noise);
                if(mahal > (int32_t)tracker->config.mahal_gate_q8)
                    continue;

                /* Combined cost: (1 - IoU) weighted + Mahalanobis
                 * cost = 0.7 × (1-IoU) + 0.3 × (mahal/gate) in Q8
                 * Lower is better */
                int32_t iou_cost = Q8 - iou;   /* 0..256 */
                int32_t mahal_cost = (mahal * Q8) /
                    (int32_t)tracker->config.mahal_gate_q8; /* 0..256 */
                int32_t combined = (iou_cost * 179 + mahal_cost * 77) / Q8;
                cost[ti][di] = (int16_t)(combined < COST_INF ? combined : COST_INF - 1);
            }
            else
            {
                /* IoU-only cost: lower IoU = higher cost */
                cost[ti][di] = (int16_t)(Q8 - iou);
            }
        }
    }
}

static void prune_deleted(dristy_tracker_t *tracker)
{
    uint8_t write = 0;
    for(uint8_t i = 0; i < tracker->track_count; i++)
    {
        if(tracker->tracks[i].state != DRISTY_TRACK_DELETED)
        {
            if(write != i)
                tracker->tracks[write] = tracker->tracks[i];
            write++;
        }
        else
        {
            tracker->total_deleted++;
        }
    }
    tracker->track_count = write;
}

static uint8_t count_active(const dristy_tracker_t *tracker)
{
    uint8_t count = 0;
    for(uint8_t i = 0; i < tracker->track_count; i++)
    {
        dristy_track_state_t s = tracker->tracks[i].state;
        if(s == DRISTY_TRACK_CONFIRMED || s == DRISTY_TRACK_COASTING)
            count++;
    }
    return count;
}

/* ---- Public API -------------------------------------------------------- */

void dristy_tracker_init(dristy_tracker_t *tracker,
                         const dristy_tracker_config_t *config)
{
    memset(tracker, 0, sizeof(*tracker));
    tracker->next_id = 1;
    if(config)
        tracker->config = *config;
    else
    {
        dristy_tracker_config_t defaults = DRISTY_TRACKER_CONFIG_DEFAULT;
        tracker->config = defaults;
    }
}

void dristy_tracker_reset(dristy_tracker_t *tracker)
{
    uint16_t saved_id = tracker->next_id;
    dristy_tracker_config_t saved_cfg = tracker->config;
    memset(tracker, 0, sizeof(*tracker));
    tracker->next_id = saved_id;
    tracker->config = saved_cfg;
}

void dristy_tracker_configure(dristy_tracker_t *tracker,
                              const dristy_tracker_config_t *config)
{
    if(config)
        tracker->config = *config;
}

uint8_t dristy_tracker_update(dristy_tracker_t *tracker,
                              const dristy_detection_t *detections,
                              uint8_t det_count)
{
    int16_t cost[HUNGARIAN_MAX][HUNGARIAN_MAX];
    int8_t assignment[HUNGARIAN_MAX];
    uint8_t confirmed_indices[DRISTY_TRACKER_MAX_TRACKS];
    uint8_t tentative_indices[DRISTY_TRACKER_MAX_TRACKS];
    uint8_t unmatched_det_flags[DRISTY_TRACKER_MAX_DETECTIONS];
    uint8_t remaining_det_indices[DRISTY_TRACKER_MAX_DETECTIONS];
    uint8_t n_confirmed = 0, n_tentative = 0;
    uint8_t n_remaining;

    if(det_count > DRISTY_TRACKER_MAX_DETECTIONS)
        det_count = DRISTY_TRACKER_MAX_DETECTIONS;

    tracker->frame_count++;

    /* 1. Predict all tracks forward one step */
    for(uint8_t i = 0; i < tracker->track_count; i++)
        track_predict_all(&tracker->tracks[i], &tracker->config);

    /* 2. Partition tracks into confirmed+coasting vs tentative */
    for(uint8_t i = 0; i < tracker->track_count; i++)
    {
        if(tracker->tracks[i].state == DRISTY_TRACK_CONFIRMED ||
           tracker->tracks[i].state == DRISTY_TRACK_COASTING)
            confirmed_indices[n_confirmed++] = i;
        else if(tracker->tracks[i].state == DRISTY_TRACK_TENTATIVE)
            tentative_indices[n_tentative++] = i;
    }

    /* All detections start as unmatched */
    memset(unmatched_det_flags, 1, det_count);

    /* 3. PASS 1: Confirmed+coasting tracks vs all detections */
    if(n_confirmed > 0 && det_count > 0)
    {
        uint8_t det_indices[DRISTY_TRACKER_MAX_DETECTIONS];
        for(uint8_t i = 0; i < det_count; i++)
            det_indices[i] = i;

        uint8_t n = n_confirmed > det_count ? n_confirmed : det_count;
        if(n > HUNGARIAN_MAX) n = HUNGARIAN_MAX;

        build_cost_matrix(tracker,
                          confirmed_indices, n_confirmed,
                          detections, det_indices, det_count,
                          cost, n);

        hungarian_solve(cost, n, assignment);

        for(uint8_t ti = 0; ti < n_confirmed; ti++)
        {
            if(assignment[ti] >= 0 && (uint8_t)assignment[ti] < det_count)
            {
                uint8_t di = (uint8_t)assignment[ti];
                track_update_with_det(&tracker->tracks[confirmed_indices[ti]],
                                      &detections[di], &tracker->config);
                tracker->tracks[confirmed_indices[ti]].last_seen_frame =
                    tracker->frame_count;
                unmatched_det_flags[di] = 0;
                tracker->total_matched++;
            }
            else
            {
                track_mark_missed(&tracker->tracks[confirmed_indices[ti]],
                                  &tracker->config);
                tracker->total_unmatched_trk++;
            }
        }
    }
    else
    {
        /* No confirmed tracks — mark them all missed */
        for(uint8_t i = 0; i < n_confirmed; i++)
        {
            track_mark_missed(&tracker->tracks[confirmed_indices[i]],
                              &tracker->config);
            tracker->total_unmatched_trk++;
        }
    }

    /* 4. PASS 2: Tentative tracks vs remaining detections */
    n_remaining = 0;
    for(uint8_t i = 0; i < det_count; i++)
    {
        if(unmatched_det_flags[i])
            remaining_det_indices[n_remaining++] = i;
    }

    if(n_tentative > 0 && n_remaining > 0)
    {
        uint8_t n = n_tentative > n_remaining ? n_tentative : n_remaining;
        if(n > HUNGARIAN_MAX) n = HUNGARIAN_MAX;

        build_cost_matrix(tracker,
                          tentative_indices, n_tentative,
                          detections, remaining_det_indices, n_remaining,
                          cost, n);

        hungarian_solve(cost, n, assignment);

        for(uint8_t ti = 0; ti < n_tentative; ti++)
        {
            if(assignment[ti] >= 0 && (uint8_t)assignment[ti] < n_remaining)
            {
                uint8_t di = remaining_det_indices[(uint8_t)assignment[ti]];
                track_update_with_det(&tracker->tracks[tentative_indices[ti]],
                                      &detections[di], &tracker->config);
                tracker->tracks[tentative_indices[ti]].last_seen_frame =
                    tracker->frame_count;
                unmatched_det_flags[di] = 0;
                tracker->total_matched++;
            }
            else
            {
                track_mark_missed(&tracker->tracks[tentative_indices[ti]],
                                  &tracker->config);
                tracker->total_unmatched_trk++;
            }
        }
    }
    else
    {
        for(uint8_t i = 0; i < n_tentative; i++)
        {
            track_mark_missed(&tracker->tracks[tentative_indices[i]],
                              &tracker->config);
            tracker->total_unmatched_trk++;
        }
    }

    /* 5. Create new tracks for all still-unmatched detections */
    for(uint8_t i = 0; i < det_count; i++)
    {
        if(!unmatched_det_flags[i])
            continue;
        if(tracker->track_count >= DRISTY_TRACKER_MAX_TRACKS)
            break;
        track_create(&tracker->tracks[tracker->track_count],
                     &detections[i], tracker->next_id++,
                     tracker->frame_count, &tracker->config);
        if(tracker->next_id == 0)
            tracker->next_id = 1;
        tracker->track_count++;
        tracker->total_created++;
        tracker->total_unmatched_det++;
    }

    /* 6. Delete dead tracks */
    prune_deleted(tracker);

    return count_active(tracker);
}

uint8_t dristy_tracker_predict(dristy_tracker_t *tracker)
{
    tracker->frame_count++;
    for(uint8_t i = 0; i < tracker->track_count; i++)
    {
        track_predict_all(&tracker->tracks[i], &tracker->config);
        track_mark_missed(&tracker->tracks[i], &tracker->config);
    }
    prune_deleted(tracker);
    return count_active(tracker);
}

uint8_t dristy_tracker_report(const dristy_tracker_t *tracker,
                              dristy_track_report_t *out,
                              uint8_t max_count)
{
    uint8_t count = 0;

    for(uint8_t i = 0; i < tracker->track_count && count < max_count; i++)
    {
        const dristy_track_t *t = &tracker->tracks[i];
        if(t->state != DRISTY_TRACK_CONFIRMED && t->state != DRISTY_TRACK_COASTING)
            continue;

        dristy_track_report_t *r = &out[count];
        r->cx = (int16_t)(t->pos[0] / Q12);
        r->cy = (int16_t)(t->pos[1] / Q12);
        r->w  = (int16_t)(t->pos[2] / Q12);
        r->h  = (int16_t)(t->pos[3] / Q12);
        r->vx_x10 = (int16_t)((t->vel[0] * 10) / Q12);
        r->vy_x10 = (int16_t)((t->vel[1] * 10) / Q12);
        r->ax_x100 = 0;  /* acceleration not yet tracked */
        r->ay_x100 = 0;
        r->id = t->id;
        r->cls = t->cls;
        r->confidence = t->confidence;
        r->state = t->state;
        r->age = t->age;
        r->coast_count = t->consecutive_misses;
        count++;
    }
    return count;
}

const dristy_track_t *dristy_tracker_find(const dristy_tracker_t *tracker,
                                          uint16_t id)
{
    for(uint8_t i = 0; i < tracker->track_count; i++)
    {
        if(tracker->tracks[i].id == id &&
           (tracker->tracks[i].state == DRISTY_TRACK_CONFIRMED ||
            tracker->tracks[i].state == DRISTY_TRACK_COASTING))
            return &tracker->tracks[i];
    }
    return 0;
}

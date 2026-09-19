#ifndef DRISTY_PROTOCOL_H
#define DRISTY_PROTOCOL_H

#include <stdint.h>
#include <stddef.h>

#include "dristy_modes.h"
#include "dristy_tracker.h"

/* ---------------------------------------------------------------------------
 * Dristy Link Protocol (DLP)
 *
 * Wire format is identical to the stock Dristy protocol:
 *   Header:  0x55 0xAA
 *   Address: 0x11
 *   Length:  data_length (1 byte)
 *   Command: 1 byte
 *   Data:    0..255 bytes
 *   Checksum: sum of all bytes from address to last data byte, mod 256
 *
 * Stock commands (0x20–0x3E) are forwarded to Dristy's existing handler.
 * Dristy commands (0x40–0x7F) are handled here.
 * Returns (0x50–0x5F) carry results back to host.
 * ------------------------------------------------------------------------- */

/* Dristy command IDs */
#define DLP_CMD_SET_MODE        0x40
#define DLP_CMD_GET_MODE        0x41
#define DLP_CMD_GET_TRACKS      0x42
#define DLP_CMD_GET_FLOW        0x43
#define DLP_CMD_GET_TAGS        0x44
#define DLP_CMD_GET_QR          0x45
#define DLP_CMD_LOAD_MODEL      0x46
#define DLP_CMD_SET_THRESHOLD   0x47
#define DLP_CMD_SET_CLOCK       0x48
#define DLP_CMD_GET_PERF        0x49
#define DLP_CMD_PUSH_FRAME      0x4A
#define DLP_CMD_GET_RAW_FRAME   0x4B
#define DLP_CMD_SET_ROI         0x4C
#define DLP_CMD_SET_COLOUR_THRESH 0x4D
#define DLP_CMD_GET_BLOBS       0x4E
#define DLP_CMD_IDENTIFY        0x4F

/* Control-system commands (0x60+) */
#define DLP_CMD_CTRL_STREAM_ON  0x60  /* Start auto-streaming control output */
#define DLP_CMD_CTRL_STREAM_OFF 0x61  /* Stop auto-streaming */
#define DLP_CMD_CTRL_TARGET     0x62  /* Request single target packet */
#define DLP_CMD_CTRL_FULL       0x63  /* Request full frame packet */
#define DLP_CMD_SET_TARGET_ID   0x64  /* Lock onto specific track ID (u16) */
#define DLP_CMD_SET_TARGET_CLS  0x65  /* Filter by class (u8, 0xFF=any) */
#define DLP_CMD_SET_BAUD        0x66  /* Change Gravity UART baud (u32) */
#define DLP_CMD_GET_RESULT      0x67  /* Get latest result bus snapshot */
#define DLP_CMD_BENCH_ENABLE    0x68  /* Enable/disable profiler (u8) */
#define DLP_CMD_BENCH_REPORT    0x69  /* Get serialised benchmark report */
#define DLP_CMD_BENCH_RESET     0x6A  /* Reset benchmark counters */
#define DLP_CMD_BENCH_PRINT     0x6B  /* Print report to debug UART */

/* LCD and display commands (0x70+) */
#define DLP_CMD_LCD_CONTROL     0x70  /* u8: 0=off, 1=on+overlay, 2=on+clean */
#define DLP_CMD_LCD_BRIGHTNESS  0x71  /* u8: 0-100 backlight percent */

/* Camera ISP commands (0x72+) */
#define DLP_CMD_CAM_PRESET      0x72  /* u8: dristy_cam_preset_t */
#define DLP_CMD_CAM_SET_PARAM   0x73  /* u8 param_id + u16 value */
#define DLP_CMD_CAM_GET_PARAMS  0x74  /* → returns camera_params_t */

/* LED commands (0x76+) */
#define DLP_CMD_LED_MODE        0x76  /* u8: dristy_led_mode_t */
#define DLP_CMD_LED_BRIGHTNESS  0x77  /* u8: 0-100 */
#define DLP_CMD_LED_COLOR       0x78  /* u8 r, u8 g, u8 b */
#define DLP_CMD_LED_ILLUMINATION 0x79 /* u8: 0-100 front LED */

/* Config persistence commands (0x7A+) */
#define DLP_CMD_CONFIG_SAVE     0x7A  /* Save current config to flash */
#define DLP_CMD_CONFIG_LOAD     0x7B  /* Reload config from flash */
#define DLP_CMD_CONFIG_RESET    0x7C  /* Reset to factory defaults */

/* ArUco / fiducial config (0x7D+) */
#define DLP_CMD_ARUCO_SET_DICT  0x7D  /* u8: dictionary ID */
#define DLP_CMD_ARUCO_SET_SIZE  0x7E  /* u16: marker size mm × 10 */

/* Motion detection config */
#define DLP_CMD_MOTION_SENSITIVITY 0x7F /* u8: 0-100 */

/* Dristy return IDs */
#define DLP_RET_TRACK           0x50
#define DLP_RET_FLOW            0x51
#define DLP_RET_TAG             0x52
#define DLP_RET_QR              0x53
#define DLP_RET_PERF            0x54
#define DLP_RET_BLOB            0x55
#define DLP_RET_MODE            0x56
#define DLP_RET_IDENTITY        0x57
#define DLP_RET_CAM_PARAMS      0x58
#define DLP_RET_LCD_STATE       0x59

/* Performance report structure (12 bytes) */
typedef struct __attribute__((packed))
{
    uint16_t fps_x10;       /* FPS × 10 */
    uint16_t infer_us;      /* last inference time in µs (capped at 65535) */
    uint16_t decode_us;     /* last post-processing time in µs */
    uint16_t cpu_pct;       /* CPU usage percentage × 10 */
    uint16_t kpu_mhz;      /* current KPU clock in MHz */
    uint16_t frame_count;   /* total frames since mode switch */
} dlp_perf_report_t;

/* Track report over the wire (16 bytes) */
typedef struct __attribute__((packed))
{
    uint16_t id;
    int16_t cx;
    int16_t cy;
    int16_t w;
    int16_t h;
    int16_t vx_x10;
    int16_t vy_x10;
    uint16_t age;
} dlp_track_wire_t;

/* Tag report over the wire (20 bytes) */
typedef struct __attribute__((packed))
{
    uint16_t tag_id;
    uint16_t family;        /* TAG36H11=0, TAG25H9=1, TAG16H5=2 */
    int16_t cx;
    int16_t cy;
    uint16_t hamming;
    int16_t tx_mm;          /* translation X in mm (requires calibration) */
    int16_t ty_mm;
    int16_t tz_mm;
    int16_t yaw_deg_x10;   /* yaw × 10 */
} dlp_tag_wire_t;

/* Flow report over the wire (8 bytes) */
typedef struct __attribute__((packed))
{
    int16_t dx_x100;       /* displacement X × 100 (sub-pixel) */
    int16_t dy_x100;       /* displacement Y × 100 */
    int16_t rot_x100;      /* rotation × 100 degrees */
    int16_t response_x100; /* correlation response × 100 (0=noise, 100=perfect) */
} dlp_flow_wire_t;

/* Maximum DLP packet payload */
#define DLP_MAX_PAYLOAD 250

/* Pipeline context registered at boot (see dristy_boot.c) */
typedef struct
{
    dristy_tracker_t *tracker;
    dristy_mode_t current_mode;

    volatile uint32_t *fps_x10;
    volatile uint32_t *infer_us;
    volatile uint32_t *decode_us;
    volatile uint32_t *kpu_mhz;
    volatile uint32_t *frame_count;

    void (*set_mode)(dristy_mode_t mode);
    void (*set_threshold)(uint16_t conf_x100);
    void (*set_nms)(uint16_t nms_x100);

    uint8_t lcd_state; /* 0=off, 1=overlay, 2=clean */
    void (*set_lcd)(uint8_t state);
    void (*set_lcd_brightness)(uint8_t percent);
} dristy_protocol_ctx_t;

/* Process a DLP command. Returns >0 if handled, 0 if not a DLP command.
 * response_buf must be at least DLP_MAX_PAYLOAD bytes.
 * *response_len is set to the number of response data bytes.
 * *response_cmd is set to the response command byte. */
int dristy_protocol_handle(uint8_t command,
                           const uint8_t *data, uint8_t data_len,
                           uint8_t *response_cmd,
                           uint8_t *response_buf, uint8_t *response_len);

/* Register the pipeline state pointers so the protocol can read them.
 * Call once during init after pipeline and tracker are set up. */
void dristy_protocol_set_pipeline(void *pipeline_ctx);

#endif /* DRISTY_PROTOCOL_H */

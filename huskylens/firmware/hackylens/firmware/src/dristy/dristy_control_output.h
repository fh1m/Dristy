#ifndef DRISTY_CONTROL_OUTPUT_H
#define DRISTY_CONTROL_OUTPUT_H

#include <stdint.h>

#include "dristy_result_bus.h"

/* ---------------------------------------------------------------------------
 * Dristy Control Output — Compact Binary Protocol for Flight Controllers
 *
 * This is the fast, fixed-size binary output designed for direct consumption
 * by PID loops, mixers, and control systems on an ESP32-S3, STM32, or
 * flight controller (ArduPilot/PX4/Betaflight).
 *
 * TWO output formats:
 *
 * 1. HEARTBEAT (4 bytes, sent every 100ms even when no detections):
 *    Tells the flight controller "I'm alive, mode X, N fps".
 *    If heartbeat stops → vision failure → failsafe.
 *
 * 2. TARGET FRAME (variable, sent on every new result):
 *    Primary target + optional extra tracks/tags for the control loop.
 *    The first 20 bytes are ALWAYS the primary target — the FC can read
 *    just those 20 bytes and ignore the rest.
 *
 * Wire format:
 *   [SYNC 0xD5] [TYPE] [LEN_LO] [LEN_HI] [PAYLOAD] [CRC8]
 *
 * CRC-8/MAXIM (polynomial 0x31, init 0x00) over TYPE+LEN+PAYLOAD.
 *
 * All multi-byte values are LITTLE-ENDIAN.
 * All coordinates are NORMALISED [-1000, +1000].
 * All velocities are normalised units/second.
 *
 * Baud rate: matches Gravity UART config (default 115200, max 921600).
 * At 115200 baud: 11.5 KB/s → 20-byte target @ 30 Hz = 0.6 KB/s = 5% BW.
 * At 921600 baud: 92 KB/s → full frames with 20 tracks easily fit.
 *
 * ESP32-S3 bridge architecture (future, NOT in firmware now):
 *   Dristy UART1 → ESP32-S3 UART RX → parse → SPI to FC + WiFi to GCS
 *   ESP32-S3 also reads GPS/IMU via its own interfaces and can timestamp-
 *   align vision data with IMU using the Dristy timestamp_us field.
 * ------------------------------------------------------------------------- */

/* Sync byte — 0xD5 = 'D' + 0x11 (Dristy address) */
#define DRISTY_CTRL_SYNC    0xD5

/* Message types */
#define DRISTY_CTRL_HEARTBEAT   0x01
#define DRISTY_CTRL_TARGET      0x02
#define DRISTY_CTRL_TRACKS      0x03
#define DRISTY_CTRL_TAGS        0x04
#define DRISTY_CTRL_FLOW        0x05
#define DRISTY_CTRL_FULL_FRAME  0x06

/* Heartbeat: 8 bytes payload (sent every 100ms minimum) */
typedef struct __attribute__((packed))
{
    uint32_t timestamp_ms;  /* monotonic ms from boot (wraps at ~49 days) */
    uint8_t mode;           /* dristy_mode_t */
    uint8_t fps;            /* current FPS (0-255) */
    uint8_t target_valid;   /* 1 if primary target is being tracked */
    uint8_t status;         /* 0=OK, 1=NO_MODEL, 2=KPU_FAULT, 3=CAM_FAULT */
} dristy_ctrl_heartbeat_t;

/* Primary target: 20 bytes — the MINIMUM a flight controller needs.
 * Read JUST this struct for the simplest possible integration:
 *   error_x → yaw PID input
 *   error_y → pitch PID input
 *   size    → throttle/approach PID input
 *   range_mm → altitude PID input (if has_3d)
 */
typedef struct __attribute__((packed))
{
    uint32_t timestamp_ms;
    int16_t error_x;        /* normalised [-1000, +1000], 0 = centred */
    int16_t error_y;
    uint16_t size;           /* normalised [0, 2000], larger = closer */
    int16_t error_rate_x;   /* d(error)/dt, normalised/second */
    int16_t error_rate_y;
    uint16_t range_mm;       /* 3D range in mm (0 = unknown) */
    uint16_t track_id;
    uint8_t target_type;    /* dristy_target_type_t */
    uint8_t confidence;     /* 0-100 percent */
} dristy_ctrl_target_t;

/* Compact track: 12 bytes per track */
typedef struct __attribute__((packed))
{
    uint16_t id;
    int16_t norm_cx;
    int16_t norm_cy;
    int16_t vel_x;          /* normalised/second */
    int16_t vel_y;
    uint8_t cls;
    uint8_t confidence;     /* 0-100 */
} dristy_ctrl_track_t;

/* Compact tag: 14 bytes per tag */
typedef struct __attribute__((packed))
{
    uint16_t tag_id;
    int16_t norm_cx;
    int16_t norm_cy;
    uint16_t range_mm;      /* 0 = no pose */
    int16_t yaw_x10;        /* degrees × 10 */
    int16_t pitch_x10;
    uint8_t family;
    uint8_t hamming;
} dristy_ctrl_tag_t;

/* --- API ---------------------------------------------------------------- */

/* Initialise the control output system */
void dristy_control_output_init(void);

/* Generate a heartbeat packet into buf. Returns total packet size. */
uint8_t dristy_control_output_heartbeat(uint8_t *buf, uint8_t buf_size);

/* Generate a target packet from the latest result bus snapshot.
 * Returns total packet size (0 if no result available). */
uint8_t dristy_control_output_target(uint8_t *buf, uint8_t buf_size);

/* Generate a full frame packet (target + all tracks + all tags).
 * Returns total packet size. */
uint16_t dristy_control_output_full_frame(uint8_t *buf, uint16_t buf_size);

/* CRC-8/MAXIM calculation */
uint8_t dristy_crc8(const uint8_t *data, uint16_t len);

#endif /* DRISTY_CONTROL_OUTPUT_H */

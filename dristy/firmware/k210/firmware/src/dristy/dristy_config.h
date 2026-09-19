#ifndef DRISTY_CONFIG_H
#define DRISTY_CONFIG_H

#include <stdint.h>

#include "dristy_modes.h"
#include "dristy_camera_ctrl.h"

/* ---------------------------------------------------------------------------
 * Dristy Configuration Persistence
 *
 * Saves/loads all Dristy settings to a reserved flash sector so they survive
 * power cycles. The flight controller can configure Dristy once, and the
 * settings persist without reconfiguration on every boot.
 *
 * Stored in the last 4 KB sector of the user-data flash region (0x0F0000).
 * Format: magic + version + data + CRC32. If CRC fails → use defaults.
 *
 * Settings saved:
 *   - Vision mode
 *   - Detection thresholds
 *   - NMS parameters
 *   - Camera ISP preset and adjustments
 *   - Target class/ID filters
 *   - ArUco dictionary and marker size
 *   - Distance estimation references
 *   - Motion detection sensitivity
 *   - UART baud rate
 *   - LED mode
 * ------------------------------------------------------------------------- */

#define DRISTY_CONFIG_MAGIC     0x44525354  /* 'DRST' */
#define DRISTY_CONFIG_VERSION   1
#define DRISTY_CONFIG_FLASH_ADDR 0x0F0000   /* 960 KB into flash */

/* Persistent config structure (must be < 4096 bytes for one flash sector) */
typedef struct __attribute__((packed))
{
    uint32_t magic;
    uint16_t version;
    uint16_t size;          /* sizeof this struct */

    /* Vision */
    uint8_t mode;           /* dristy_mode_t */
    uint8_t confidence_pct; /* 0-100 */
    uint8_t nms_pct;        /* 0-100 */

    /* Camera */
    uint8_t cam_preset;     /* dristy_cam_preset_t */
    int8_t brightness;
    int8_t contrast;
    int8_t saturation;
    int8_t sharpness;
    uint8_t hmirror;
    uint8_t vflip;

    /* Target selection */
    uint16_t target_track_id;
    uint8_t target_class;

    /* Fiducial */
    uint8_t aruco_dict;     /* dristy_aruco_dict_t */
    uint16_t aruco_size_mm_x10; /* marker size × 10 */

    /* Motion */
    uint8_t motion_diff_thresh;
    uint16_t motion_area_thresh_x10; /* % × 10 */

    /* Communication */
    uint32_t uart_baud;

    /* LED */
    uint8_t led_mode;       /* 0=off, 1=status, 2=detection flash, 3=always on */
    uint8_t led_brightness; /* 0-100 */

    /* Distance estimation */
    uint16_t focal_length_px_x10;
    uint8_t num_size_refs;
    struct __attribute__((packed)) {
        uint8_t class_id;
        uint16_t width_mm;
        uint16_t height_mm;
    } size_refs[16];

    /* Padding + CRC */
    uint8_t _reserved[32];
    uint32_t crc32;
} dristy_config_data_t;

/* --- API ---------------------------------------------------------------- */

/* Initialise config system. Loads from flash if valid, else uses defaults. */
void dristy_config_init(void);

/* Get current config (read-only) */
const dristy_config_data_t *dristy_config_get(void);

/* Save current settings to flash. Returns 1 on success. */
uint8_t dristy_config_save(void);

/* Reset to factory defaults (does NOT save to flash) */
void dristy_config_reset(void);

/* Update individual fields (does NOT auto-save) */
void dristy_config_set_mode(dristy_mode_t mode);
void dristy_config_set_confidence(uint8_t pct);
void dristy_config_set_nms(uint8_t pct);
void dristy_config_set_uart_baud(uint32_t baud);
void dristy_config_set_led(uint8_t mode, uint8_t brightness);

/* Apply loaded config to all subsystems (call after init or load) */
void dristy_config_apply(void);

#endif /* DRISTY_CONFIG_H */

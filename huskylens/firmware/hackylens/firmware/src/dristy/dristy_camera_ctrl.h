#ifndef DRISTY_CAMERA_CTRL_H
#define DRISTY_CAMERA_CTRL_H

#include <stdint.h>

/* ---------------------------------------------------------------------------
 * Dristy Camera ISP Control
 *
 * Runtime control of OV2640 camera parameters for adapting to different
 * environments (indoor, outdoor, night, high-speed). Essential for drones
 * where lighting changes rapidly (sun/shade/clouds).
 *
 * All parameters adjustable via DLP commands at runtime.
 *
 * OV2640 registers used:
 *   Sensor bank (0xFF=0x01):
 *     0x13: COM8  — AEC/AGC enable
 *     0x10: AEC[7:0], 0x45: AEC[15:8]  — manual exposure
 *     0x00: GAIN[7:0]  — manual gain
 *     0x0C: COM3  — mirror/flip
 *   DSP bank (0xFF=0x00):
 *     0x7C: controls register base
 *     0x7D: controls register data
 *     Brightness/contrast/saturation via OV2640 SDE (Special Digital Effects)
 * ------------------------------------------------------------------------- */

/* Camera preset profiles */
typedef enum
{
    DRISTY_CAM_PRESET_AUTO       = 0,  /* fully automatic */
    DRISTY_CAM_PRESET_INDOOR     = 1,  /* indoor: lower shutter, higher gain */
    DRISTY_CAM_PRESET_OUTDOOR    = 2,  /* outdoor: fast shutter, low gain */
    DRISTY_CAM_PRESET_NIGHT      = 3,  /* night: max exposure, high gain */
    DRISTY_CAM_PRESET_FAST       = 4,  /* fast: shortest exposure, for motion */
    DRISTY_CAM_PRESET_TAG_DETECT = 5,  /* optimised for fiducial detection */
    DRISTY_CAM_PRESET_CUSTOM     = 6,  /* all manual */
} dristy_cam_preset_t;

/* Camera parameters (all settable individually or via presets) */
typedef struct
{
    dristy_cam_preset_t preset;

    /* Auto-exposure / Auto-gain control */
    uint8_t aec_enable;         /* 1 = auto exposure, 0 = manual */
    uint8_t agc_enable;         /* 1 = auto gain, 0 = manual */
    uint16_t manual_exposure;   /* manual exposure value (0-1024) */
    uint8_t manual_gain;        /* manual gain (0-255, ~0.5x to 128x) */
    uint8_t gain_ceiling;       /* max auto-gain ceiling (0-7, maps to 2x-128x) */

    /* White balance */
    uint8_t awb_enable;         /* 1 = auto white balance */
    uint8_t wb_mode;            /* 0=auto, 1=sunny, 2=cloudy, 3=office, 4=home */

    /* Image adjustments (-2 to +2 mapped to 0-4) */
    int8_t brightness;          /* -2 to +2 */
    int8_t contrast;            /* -2 to +2 */
    int8_t saturation;          /* -2 to +2 */
    int8_t sharpness;           /* 0-5 */

    /* Special */
    uint8_t hmirror;            /* 1 = horizontal mirror */
    uint8_t vflip;              /* 1 = vertical flip */
    uint8_t colorbar;           /* 1 = test pattern */
    uint8_t night_mode;         /* 1 = reduced frame rate for long exposure */
} dristy_camera_params_t;

#define DRISTY_CAMERA_PARAMS_DEFAULT {      \
    .preset          = DRISTY_CAM_PRESET_AUTO, \
    .aec_enable      = 1,                   \
    .agc_enable      = 1,                   \
    .manual_exposure = 0,                   \
    .manual_gain     = 0,                   \
    .gain_ceiling    = 2,                   \
    .awb_enable      = 1,                   \
    .wb_mode         = 0,                   \
    .brightness      = 0,                   \
    .contrast        = 0,                   \
    .saturation      = 0,                   \
    .sharpness       = 0,                   \
    .hmirror         = 0,                   \
    .vflip           = 0,                   \
    .colorbar        = 0,                   \
    .night_mode      = 0,                   \
}

/* --- API ---------------------------------------------------------------- */

/* Initialise camera control. Call after camera driver init. */
void dristy_camera_ctrl_init(void);

/* Apply a preset profile */
void dristy_camera_ctrl_set_preset(dristy_cam_preset_t preset);

/* Get current parameters */
const dristy_camera_params_t *dristy_camera_ctrl_params(void);

/* Set individual parameters */
void dristy_camera_ctrl_set_exposure(uint8_t auto_en, uint16_t manual);
void dristy_camera_ctrl_set_gain(uint8_t auto_en, uint8_t manual, uint8_t ceiling);
void dristy_camera_ctrl_set_awb(uint8_t enable, uint8_t mode);
void dristy_camera_ctrl_set_brightness(int8_t level);
void dristy_camera_ctrl_set_contrast(int8_t level);
void dristy_camera_ctrl_set_saturation(int8_t level);
void dristy_camera_ctrl_set_sharpness(int8_t level);
void dristy_camera_ctrl_set_mirror_flip(uint8_t hmirror, uint8_t vflip);

/* Apply all current parameters to the OV2640 (call after changing params) */
void dristy_camera_ctrl_apply(void);

#endif /* DRISTY_CAMERA_CTRL_H */

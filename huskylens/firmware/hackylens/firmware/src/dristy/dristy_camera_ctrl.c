#include "dristy_camera_ctrl.h"
#include "../../drivers/ov2640_sensor.h"

#include <stdio.h>

/* ---------------------------------------------------------------------------
 * OV2640 Register Map (relevant subset)
 *
 * The OV2640 has two register banks selected by register 0xFF:
 *   0xFF=0x01: Sensor registers (exposure, gain, timing)
 *   0xFF=0x00: DSP registers (color, effects, scaling)
 *
 * Bank switching: write 0xFF first, then access registers.
 * ------------------------------------------------------------------------- */

/* Sensor bank registers (0xFF=0x01) */
#define OV2640_BANK_SEL     0xFF
#define OV2640_BANK_SENSOR  0x01
#define OV2640_BANK_DSP     0x00

#define OV2640_COM8         0x13    /* AEC/AGC enable bits */
#define OV2640_COM8_AEC     (1<<0)  /* Auto Exposure */
#define OV2640_COM8_AGC     (1<<2)  /* Auto Gain */
#define OV2640_COM8_AWB     (1<<1)  /* Auto White Balance */
#define OV2640_AEC_LO       0x10    /* AEC[7:0] */
#define OV2640_AEC_HI       0x45    /* AEC[15:8] */
#define OV2640_GAIN         0x00    /* Gain[7:0] */
#define OV2640_COM2         0x09    /* Output drive, soft sleep */
#define OV2640_COM3         0x0C    /* Mirror/flip */
#define OV2640_COM7         0x12    /* Reset, format, colorbar */
#define OV2640_COM10        0x15    /* VSYNC/HREF options */

/* DSP bank registers (0xFF=0x00) */
#define OV2640_CTRL0        0xC2    /* SDE control 0 */
#define OV2640_CTRL1        0xC3    /* SDE control 1 */
#define OV2640_SDE          0x7C    /* Special Digital Effects control */
#define OV2640_SDE_DATA     0x7D    /* SDE data register */

/* Night mode */
#define OV2640_COM11        0x3B    /* Night mode, band filter */
#define OV2640_COM11_NIGHT  (1<<5)

/* Gain ceiling */
#define OV2640_COM9         0x14    /* AGC gain ceiling */

static dristy_camera_params_t g_params;

/* Helper: write to sensor bank */
static void sensor_write(uint8_t reg, uint8_t val)
{
    ov2640_write_reg(OV2640_BANK_SEL, OV2640_BANK_SENSOR);
    ov2640_write_reg(reg, val);
}

/* Helper: write to DSP bank */
static void dsp_write(uint8_t reg, uint8_t val)
{
    ov2640_write_reg(OV2640_BANK_SEL, OV2640_BANK_DSP);
    ov2640_write_reg(reg, val);
}

/* Helper: read from sensor bank */
static uint8_t sensor_read(uint8_t reg)
{
    ov2640_write_reg(OV2640_BANK_SEL, OV2640_BANK_SENSOR);
    return ov2640_read_reg(reg);
}

void dristy_camera_ctrl_init(void)
{
    dristy_camera_params_t defaults = DRISTY_CAMERA_PARAMS_DEFAULT;
    g_params = defaults;
    printf("[DRISTY] camera ctrl init\r\n");
}

void dristy_camera_ctrl_set_preset(dristy_cam_preset_t preset)
{
    g_params.preset = preset;

    switch(preset)
    {
    case DRISTY_CAM_PRESET_AUTO:
        g_params.aec_enable = 1;
        g_params.agc_enable = 1;
        g_params.awb_enable = 1;
        g_params.gain_ceiling = 2;
        g_params.brightness = 0;
        g_params.contrast = 0;
        g_params.saturation = 0;
        g_params.night_mode = 0;
        break;

    case DRISTY_CAM_PRESET_INDOOR:
        g_params.aec_enable = 1;
        g_params.agc_enable = 1;
        g_params.awb_enable = 1;
        g_params.gain_ceiling = 4; /* higher gain for dim indoor */
        g_params.brightness = 1;
        g_params.contrast = 0;
        g_params.night_mode = 0;
        break;

    case DRISTY_CAM_PRESET_OUTDOOR:
        g_params.aec_enable = 1;
        g_params.agc_enable = 1;
        g_params.awb_enable = 1;
        g_params.gain_ceiling = 1; /* low gain, sun is bright */
        g_params.brightness = 0;
        g_params.contrast = 1;
        g_params.saturation = 1;
        g_params.night_mode = 0;
        break;

    case DRISTY_CAM_PRESET_NIGHT:
        g_params.aec_enable = 1;
        g_params.agc_enable = 1;
        g_params.awb_enable = 1;
        g_params.gain_ceiling = 7; /* maximum gain */
        g_params.brightness = 2;
        g_params.contrast = 1;
        g_params.night_mode = 1;
        break;

    case DRISTY_CAM_PRESET_FAST:
        g_params.aec_enable = 0;
        g_params.agc_enable = 1;
        g_params.manual_exposure = 64; /* very short exposure for motion */
        g_params.gain_ceiling = 5;
        g_params.night_mode = 0;
        break;

    case DRISTY_CAM_PRESET_TAG_DETECT:
        /* Optimised for high-contrast binary markers:
         * moderate exposure, boost contrast and sharpness */
        g_params.aec_enable = 1;
        g_params.agc_enable = 1;
        g_params.gain_ceiling = 3;
        g_params.brightness = 0;
        g_params.contrast = 2;
        g_params.saturation = -1;
        g_params.sharpness = 3;
        g_params.night_mode = 0;
        break;

    case DRISTY_CAM_PRESET_CUSTOM:
        /* Don't change anything, user controls everything */
        break;
    }

    dristy_camera_ctrl_apply();
}

const dristy_camera_params_t *dristy_camera_ctrl_params(void)
{
    return &g_params;
}

void dristy_camera_ctrl_set_exposure(uint8_t auto_en, uint16_t manual)
{
    g_params.aec_enable = auto_en;
    g_params.manual_exposure = manual;
}

void dristy_camera_ctrl_set_gain(uint8_t auto_en, uint8_t manual, uint8_t ceiling)
{
    g_params.agc_enable = auto_en;
    g_params.manual_gain = manual;
    g_params.gain_ceiling = ceiling > 7 ? 7 : ceiling;
}

void dristy_camera_ctrl_set_awb(uint8_t enable, uint8_t mode)
{
    g_params.awb_enable = enable;
    g_params.wb_mode = mode;
}

void dristy_camera_ctrl_set_brightness(int8_t level)
{
    if(level < -2) level = -2;
    if(level > 2) level = 2;
    g_params.brightness = level;
}

void dristy_camera_ctrl_set_contrast(int8_t level)
{
    if(level < -2) level = -2;
    if(level > 2) level = 2;
    g_params.contrast = level;
}

void dristy_camera_ctrl_set_saturation(int8_t level)
{
    if(level < -2) level = -2;
    if(level > 2) level = 2;
    g_params.saturation = level;
}

void dristy_camera_ctrl_set_sharpness(int8_t level)
{
    if(level < 0) level = 0;
    if(level > 5) level = 5;
    g_params.sharpness = level;
}

void dristy_camera_ctrl_set_mirror_flip(uint8_t hmirror, uint8_t vflip)
{
    g_params.hmirror = hmirror ? 1 : 0;
    g_params.vflip = vflip ? 1 : 0;
}

void dristy_camera_ctrl_apply(void)
{
    uint8_t com8;

    /* AEC / AGC / AWB control */
    com8 = sensor_read(OV2640_COM8);
    com8 &= ~(OV2640_COM8_AEC | OV2640_COM8_AGC | OV2640_COM8_AWB);
    if(g_params.aec_enable) com8 |= OV2640_COM8_AEC;
    if(g_params.agc_enable) com8 |= OV2640_COM8_AGC;
    if(g_params.awb_enable) com8 |= OV2640_COM8_AWB;
    sensor_write(OV2640_COM8, com8);

    /* Manual exposure (only applied when AEC is off) */
    if(!g_params.aec_enable)
    {
        sensor_write(OV2640_AEC_LO, g_params.manual_exposure & 0xFF);
        sensor_write(OV2640_AEC_HI, (g_params.manual_exposure >> 8) & 0xFF);
    }

    /* Manual gain (only applied when AGC is off) */
    if(!g_params.agc_enable)
    {
        sensor_write(OV2640_GAIN, g_params.manual_gain);
    }

    /* Gain ceiling */
    {
        uint8_t com9 = sensor_read(OV2640_COM9);
        com9 = (com9 & 0x1F) | ((g_params.gain_ceiling & 0x07) << 5);
        sensor_write(OV2640_COM9, com9);
    }

    /* Night mode */
    {
        uint8_t com11 = sensor_read(OV2640_COM11);
        if(g_params.night_mode)
            com11 |= OV2640_COM11_NIGHT;
        else
            com11 &= ~OV2640_COM11_NIGHT;
        sensor_write(OV2640_COM11, com11);
    }

    /* Mirror / Flip */
    {
        uint8_t com3 = sensor_read(OV2640_COM3);
        com3 &= ~0xC0;
        if(g_params.hmirror) com3 |= 0x40;
        if(g_params.vflip)   com3 |= 0x80;
        sensor_write(OV2640_COM3, com3);
    }

    /* Brightness (via SDE registers) */
    {
        dsp_write(OV2640_SDE, 0x04); /* brightness control */
        /* Map -2..+2 to register values: sign + magnitude */
        uint8_t mag = (uint8_t)((g_params.brightness < 0 ?
                                 -g_params.brightness : g_params.brightness) * 0x20);
        uint8_t sign = g_params.brightness < 0 ? 0x08 : 0x00;
        dsp_write(OV2640_SDE_DATA, mag | sign);
    }

    /* Contrast (via SDE registers) */
    {
        static const uint8_t contrast_regs[][2] = {
            {0x20, 0x00}, /* -2 */
            {0x24, 0x00}, /* -1 */
            {0x28, 0x00}, /*  0 */
            {0x2C, 0x00}, /* +1 */
            {0x30, 0x04}, /* +2 */
        };
        uint8_t idx = (uint8_t)(g_params.contrast + 2);
        if(idx > 4) idx = 4;
        dsp_write(OV2640_SDE, 0x04);
        dsp_write(OV2640_SDE_DATA, contrast_regs[idx][0]);
    }

    /* Saturation (via SDE registers) */
    {
        static const uint8_t sat_vals[] = { 0x10, 0x20, 0x40, 0x60, 0x80 };
        uint8_t idx = (uint8_t)(g_params.saturation + 2);
        if(idx > 4) idx = 4;
        dsp_write(OV2640_SDE, 0x02); /* saturation control */
        dsp_write(OV2640_SDE_DATA, sat_vals[idx]);
        dsp_write(OV2640_SDE_DATA, sat_vals[idx]);
    }

    /* Colorbar */
    if(g_params.colorbar)
        ov2640_apply_colorbar(1);
    else
        ov2640_apply_colorbar(0);

    printf("[DRISTY] camera apply preset=%d exp=%s/%u gain=%s/%u/%u\r\n",
           g_params.preset,
           g_params.aec_enable ? "auto" : "manual", g_params.manual_exposure,
           g_params.agc_enable ? "auto" : "manual", g_params.manual_gain,
           g_params.gain_ceiling);
}

#include "dristy_config.h"

#include <string.h>
#include <stdio.h>

#include "../../drivers/boot_flash.h"
#include "dristy_pipeline.h"
#include "dristy_camera_ctrl.h"
#include "dristy_aruco.h"
#include "dristy_distance.h"
#include "dristy_motion.h"

static dristy_config_data_t g_cfg;

/* CRC-32 (same polynomial as zlib) */
static uint32_t crc32_calc(const uint8_t *data, uint32_t len)
{
    uint32_t crc = 0xFFFFFFFF;
    for(uint32_t i = 0; i < len; i++)
    {
        crc ^= data[i];
        for(int j = 0; j < 8; j++)
        {
            if(crc & 1)
                crc = (crc >> 1) ^ 0xEDB88320;
            else
                crc >>= 1;
        }
    }
    return ~crc;
}

static void set_defaults(void)
{
    memset(&g_cfg, 0, sizeof(g_cfg));
    g_cfg.magic = DRISTY_CONFIG_MAGIC;
    g_cfg.version = DRISTY_CONFIG_VERSION;
    g_cfg.size = sizeof(g_cfg);

    g_cfg.mode = DRISTY_MODE_DETECT_TRACK;
    g_cfg.confidence_pct = 30;
    g_cfg.nms_pct = 30;

    g_cfg.cam_preset = DRISTY_CAM_PRESET_AUTO;
    g_cfg.brightness = 0;
    g_cfg.contrast = 0;
    g_cfg.saturation = 0;
    g_cfg.sharpness = 0;

    g_cfg.target_track_id = 0;
    g_cfg.target_class = 0xFF;

    g_cfg.aruco_dict = 0; /* DICT_4X4_50 */
    g_cfg.aruco_size_mm_x10 = 1000; /* 100mm default */

    g_cfg.motion_diff_thresh = 25;
    g_cfg.motion_area_thresh_x10 = 20; /* 2.0% */

    g_cfg.uart_baud = 115200;

    g_cfg.led_mode = 1; /* status */
    g_cfg.led_brightness = 50;

    g_cfg.focal_length_px_x10 = 2300; /* 230.0 px */
    g_cfg.num_size_refs = 0;
}

void dristy_config_init(void)
{
    uint8_t buf[sizeof(dristy_config_data_t)];

    /* Try to load from flash */
    boot_flash_read(DRISTY_CONFIG_FLASH_ADDR, buf, sizeof(buf));

    dristy_config_data_t *loaded = (dristy_config_data_t *)buf;

    if(loaded->magic == DRISTY_CONFIG_MAGIC &&
       loaded->version == DRISTY_CONFIG_VERSION &&
       loaded->size == sizeof(dristy_config_data_t))
    {
        /* Validate CRC (over everything except the CRC field itself) */
        uint32_t expected_crc = crc32_calc(buf,
            sizeof(dristy_config_data_t) - sizeof(uint32_t));
        if(loaded->crc32 == expected_crc)
        {
            g_cfg = *loaded;
            printf("[DRISTY] config loaded from flash\r\n");
            return;
        }
        printf("[DRISTY] config CRC mismatch, using defaults\r\n");
    }
    else
    {
        printf("[DRISTY] no saved config, using defaults\r\n");
    }

    set_defaults();
}

const dristy_config_data_t *dristy_config_get(void)
{
    return &g_cfg;
}

uint8_t dristy_config_save(void)
{
    /* Compute CRC over everything except the CRC field */
    g_cfg.crc32 = crc32_calc((const uint8_t *)&g_cfg,
        sizeof(g_cfg) - sizeof(uint32_t));

    /* Erase the sector (4 KB) and write */
    if(boot_flash_sector_erase(DRISTY_CONFIG_FLASH_ADDR) != BOOT_FLASH_OK)
        return 0U;
    if(boot_flash_program(DRISTY_CONFIG_FLASH_ADDR,
                          (const uint8_t *)&g_cfg, sizeof(g_cfg)) != BOOT_FLASH_OK)
        return 0U;

    printf("[DRISTY] config saved to flash\r\n");
    return 1;
}

void dristy_config_reset(void)
{
    set_defaults();
    printf("[DRISTY] config reset to defaults\r\n");
}

void dristy_config_set_mode(dristy_mode_t mode)
{
    g_cfg.mode = (uint8_t)mode;
}

void dristy_config_set_confidence(uint8_t pct)
{
    g_cfg.confidence_pct = pct > 100 ? 100 : pct;
}

void dristy_config_set_nms(uint8_t pct)
{
    g_cfg.nms_pct = pct > 100 ? 100 : pct;
}

void dristy_config_set_uart_baud(uint32_t baud)
{
    g_cfg.uart_baud = baud;
}

void dristy_config_set_led(uint8_t mode, uint8_t brightness)
{
    g_cfg.led_mode = mode;
    g_cfg.led_brightness = brightness > 100 ? 100 : brightness;
}

void dristy_config_apply(void)
{
    /* Apply mode */
    dristy_pipeline_set_mode((dristy_mode_t)g_cfg.mode);

    /* Apply thresholds */
    dristy_pipeline_set_confidence(g_cfg.confidence_pct);
    dristy_pipeline_set_nms(g_cfg.nms_pct);

    /* Apply camera settings */
    dristy_camera_ctrl_set_preset((dristy_cam_preset_t)g_cfg.cam_preset);
    dristy_camera_ctrl_set_brightness(g_cfg.brightness);
    dristy_camera_ctrl_set_contrast(g_cfg.contrast);
    dristy_camera_ctrl_set_saturation(g_cfg.saturation);
    dristy_camera_ctrl_set_sharpness(g_cfg.sharpness);
    dristy_camera_ctrl_set_mirror_flip(g_cfg.hmirror, g_cfg.vflip);
    dristy_camera_ctrl_apply();

    /* Apply target selection */
    dristy_pipeline_set_target_track(g_cfg.target_track_id);
    dristy_pipeline_set_target_class(g_cfg.target_class);

    printf("[DRISTY] config applied: mode=%d conf=%d%% baud=%lu\r\n",
           g_cfg.mode, g_cfg.confidence_pct, (unsigned long)g_cfg.uart_baud);
}

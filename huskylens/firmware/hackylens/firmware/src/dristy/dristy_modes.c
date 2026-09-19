#include "dristy_modes.h"

#include <stddef.h>

static const dristy_mode_info_t g_mode_table[] = {
    /* Neural inference modes */
    {
        .mode = DRISTY_MODE_DETECT,
        .name = "Object Detection",
        .short_name = "DETECT",
        .capabilities = DRISTY_CAP_KPU,
        .model_slot = 0,
        .priority = 0,
    },
    {
        .mode = DRISTY_MODE_DETECT_CUSTOM,
        .name = "Custom Model",
        .short_name = "CUSTOM",
        .capabilities = DRISTY_CAP_KPU,
        .model_slot = 3,
        .priority = 1,
    },
    {
        .mode = DRISTY_MODE_CLASSIFY,
        .name = "Classification",
        .short_name = "CLASSIFY",
        .capabilities = DRISTY_CAP_KPU,
        .model_slot = 2,
        .priority = 1,
    },
    {
        .mode = DRISTY_MODE_FACE_DETECT,
        .name = "Face Detection",
        .short_name = "FACE",
        .capabilities = DRISTY_CAP_KPU,
        .model_slot = 1,
        .priority = 1,
    },
    {
        .mode = DRISTY_MODE_FACE_RECOGNISE,
        .name = "Face Recognition",
        .short_name = "FACE ID",
        .capabilities = DRISTY_CAP_KPU,
        .model_slot = 1,
        .priority = 2,
    },

    /* Classical CV modes */
    {
        .mode = DRISTY_MODE_COLOUR_TRACK,
        .name = "Colour Tracking",
        .short_name = "COLOUR",
        .capabilities = DRISTY_CAP_CLASSICAL | DRISTY_CAP_TRACKER,
        .model_slot = 0xFF,
        .priority = 0,
    },
    {
        .mode = DRISTY_MODE_LINE_FOLLOW,
        .name = "Line Following",
        .short_name = "LINE",
        .capabilities = DRISTY_CAP_CLASSICAL,
        .model_slot = 0xFF,
        .priority = 0,
    },
    {
        .mode = DRISTY_MODE_APRILTAG,
        .name = "AprilTag",
        .short_name = "ATAG",
        .capabilities = DRISTY_CAP_CLASSICAL | DRISTY_CAP_TAGS,
        .model_slot = 0xFF,
        .priority = 0,
    },
    {
        .mode = DRISTY_MODE_QR_CODE,
        .name = "QR Code",
        .short_name = "QR",
        .capabilities = DRISTY_CAP_CLASSICAL | DRISTY_CAP_QR,
        .model_slot = 0xFF,
        .priority = 0,
    },
    {
        .mode = DRISTY_MODE_OPTICAL_FLOW,
        .name = "Optical Flow",
        .short_name = "FLOW",
        .capabilities = DRISTY_CAP_CLASSICAL | DRISTY_CAP_FLOW,
        .model_slot = 0xFF,
        .priority = 0,
    },
    {
        .mode = DRISTY_MODE_COLOUR_SORT,
        .name = "Colour Sort",
        .short_name = "CSORT",
        .capabilities = DRISTY_CAP_CLASSICAL,
        .model_slot = 0xFF,
        .priority = 0,
    },
    {
        .mode = DRISTY_MODE_ARUCO,
        .name = "ArUco Marker",
        .short_name = "ARUCO",
        .capabilities = DRISTY_CAP_CLASSICAL | DRISTY_CAP_TAGS,
        .model_slot = 0xFF,
        .priority = 0,
    },
    {
        .mode = DRISTY_MODE_MOTION_DETECT,
        .name = "Motion Detection",
        .short_name = "MOTION",
        .capabilities = DRISTY_CAP_CLASSICAL,
        .model_slot = 0xFF,
        .priority = 0,
    },

    /* Hybrid modes */
    {
        .mode = DRISTY_MODE_DETECT_TRACK,
        .name = "Detect + Track",
        .short_name = "DET+TRK",
        .capabilities = DRISTY_CAP_KPU | DRISTY_CAP_TRACKER,
        .model_slot = 0,
        .priority = 0,
    },
    {
        .mode = DRISTY_MODE_DETECT_TAG,
        .name = "Detect + AprilTag",
        .short_name = "DET+TAG",
        .capabilities = DRISTY_CAP_KPU | DRISTY_CAP_CLASSICAL | DRISTY_CAP_TAGS,
        .model_slot = 0,
        .priority = 1,
    },
    {
        .mode = DRISTY_MODE_LANDING_TARGET,
        .name = "Landing Target",
        .short_name = "LANDING",
        .capabilities = DRISTY_CAP_CLASSICAL | DRISTY_CAP_TAGS | DRISTY_CAP_FLOW,
        .model_slot = 0xFF,
        .priority = 0,
    },
    {
        .mode = DRISTY_MODE_DETECT_ARUCO,
        .name = "Detect + ArUco",
        .short_name = "DET+ARC",
        .capabilities = DRISTY_CAP_KPU | DRISTY_CAP_CLASSICAL | DRISTY_CAP_TAGS,
        .model_slot = 0,
        .priority = 1,
    },
    {
        .mode = DRISTY_MODE_DETECT_MOTION,
        .name = "Motion-Gated Detect",
        .short_name = "MOT+DET",
        .capabilities = DRISTY_CAP_KPU | DRISTY_CAP_CLASSICAL,
        .model_slot = 0,
        .priority = 1,
    },
};

#define MODE_TABLE_COUNT (sizeof(g_mode_table) / sizeof(g_mode_table[0]))

const dristy_mode_info_t *dristy_mode_info(dristy_mode_t mode)
{
    for(uint32_t i = 0; i < MODE_TABLE_COUNT; i++)
    {
        if(g_mode_table[i].mode == mode)
            return &g_mode_table[i];
    }
    return NULL;
}

const char *dristy_mode_name(dristy_mode_t mode)
{
    const dristy_mode_info_t *info = dristy_mode_info(mode);
    return info ? info->name : "Unknown";
}

uint8_t dristy_mode_table_count(void)
{
    return (uint8_t)MODE_TABLE_COUNT;
}

const dristy_mode_info_t *dristy_mode_table_at(uint8_t index)
{
    if(index >= MODE_TABLE_COUNT)
        return NULL;
    return &g_mode_table[index];
}

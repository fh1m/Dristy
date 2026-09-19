#ifndef DRISTY_MODES_H
#define DRISTY_MODES_H

#include <stdint.h>

/* ---------------------------------------------------------------------------
 * Dristy vision modes
 *
 * Each mode configures the pipeline differently — some use the KPU for neural
 * inference, some use CPU-only classical CV, and hybrid modes interleave both.
 * The mode ID is the single byte sent over the Dristy Link Protocol (DLP) to
 * switch between them.
 * ------------------------------------------------------------------------- */

typedef enum
{
    /* Neural inference modes (use KPU on core 0) */
    DRISTY_MODE_DETECT          = 0x00, /* MobileNet-YOLOv2 object detection */
    DRISTY_MODE_DETECT_CUSTOM   = 0x01, /* User kmodel from SD/flash slot */
    DRISTY_MODE_CLASSIFY        = 0x02, /* Image classification, top-K */
    DRISTY_MODE_FACE_DETECT     = 0x03, /* Face detection with landmarks */
    DRISTY_MODE_FACE_RECOGNISE  = 0x04, /* Face recognition (detect+embed+match) */

    /* Classical CV modes (CPU only, no KPU) */
    DRISTY_MODE_COLOUR_TRACK    = 0x10, /* LAB threshold blob tracking */
    DRISTY_MODE_LINE_FOLLOW     = 0x11, /* Line regression */
    DRISTY_MODE_APRILTAG        = 0x12, /* AprilTag detection + 6DOF pose */
    DRISTY_MODE_QR_CODE         = 0x13, /* QR code decode */
    DRISTY_MODE_OPTICAL_FLOW    = 0x14, /* FFT phase correlation */
    DRISTY_MODE_COLOUR_SORT     = 0x15, /* Multi-threshold blob sorting */
    DRISTY_MODE_ARUCO           = 0x16, /* ArUco marker detection + 6DOF pose */
    DRISTY_MODE_MOTION_DETECT   = 0x17, /* Frame-diff motion detection */

    /* Hybrid modes (KPU + classical, pipelined across cores) */
    DRISTY_MODE_DETECT_TRACK    = 0x20, /* Detection + IoU/Kalman tracking */
    DRISTY_MODE_DETECT_TAG      = 0x21, /* Detection + AprilTag interleaved */
    DRISTY_MODE_LANDING_TARGET  = 0x22, /* AprilTag pose + optical flow drift */
    DRISTY_MODE_DETECT_ARUCO    = 0x23, /* Detection + ArUco interleaved */
    DRISTY_MODE_DETECT_MOTION   = 0x24, /* Detection only when motion detected */

    DRISTY_MODE_COUNT,
    DRISTY_MODE_INVALID         = 0xFF,
} dristy_mode_t;

/* Mode capability flags */
#define DRISTY_CAP_KPU       (1U << 0) /* Mode uses KPU inference */
#define DRISTY_CAP_CLASSICAL (1U << 1) /* Mode uses classical CV */
#define DRISTY_CAP_TRACKER   (1U << 2) /* Mode produces tracked objects */
#define DRISTY_CAP_FLOW      (1U << 3) /* Mode produces flow vectors */
#define DRISTY_CAP_TAGS      (1U << 4) /* Mode produces fiducial markers */
#define DRISTY_CAP_QR        (1U << 5) /* Mode produces decoded strings */

typedef struct
{
    dristy_mode_t mode;
    const char *name;
    const char *short_name; /* for LCD status bar, max 8 chars */
    uint8_t capabilities;
    uint8_t model_slot;     /* 0xFF = no model needed */
    uint8_t priority;       /* lower = higher priority for scheduler */
} dristy_mode_info_t;

/* Look up mode info, returns NULL if mode is invalid */
const dristy_mode_info_t *dristy_mode_info(dristy_mode_t mode);

/* Get the human-readable name for a mode */
const char *dristy_mode_name(dristy_mode_t mode);

uint8_t dristy_mode_table_count(void);
const dristy_mode_info_t *dristy_mode_table_at(uint8_t index);

#endif /* DRISTY_MODES_H */

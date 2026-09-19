#ifndef DRISTY_MODEL_H
#define DRISTY_MODEL_H

#include <stdint.h>

/* ---------------------------------------------------------------------------
 * Dristy Model Manager
 *
 * Manages multiple kmodel files stored in flash and SD card. Each model has
 * metadata describing its input shape, output format, anchors, class names,
 * and post-processing requirements.
 *
 * Flash layout:
 *   Slot 0: 0x100000  (2 MB)  — default detector (MobileNet-YOLOv2)
 *   Slot 1: 0x300000  (1 MB)  — face detector
 *   Slot 2: 0x400000  (1 MB)  — classifier
 *   Slot 3: 0x500000  (2 MB)  — user custom
 *
 * SD card models are loaded from dristy/models (kmodel + json metadata).
 *
 * The model manager handles:
 *   - Loading kmodels from flash or SD into SRAM
 *   - Validating headers and CRC
 *   - Parsing metadata (anchors, classes, thresholds)
 *   - Providing the correct post-processing pipeline for each model type
 * ------------------------------------------------------------------------- */

#define DRISTY_MODEL_MAX_CLASSES    80
#define DRISTY_MODEL_MAX_ANCHORS    10
#define DRISTY_MODEL_MAX_NAME_LEN   32
#define DRISTY_MODEL_MAX_SLOTS      4

/* Flash slot addresses (16 MiB total flash, firmware uses first 1 MB) */
#define DRISTY_MODEL_FLASH_SLOT0    0x100000
#define DRISTY_MODEL_FLASH_SLOT1    0x300000
#define DRISTY_MODEL_FLASH_SLOT2    0x400000
#define DRISTY_MODEL_FLASH_SLOT3    0x500000

/* Post-processing type determines how raw KPU output is decoded */
typedef enum
{
    DRISTY_POST_NONE        = 0, /* raw output, no decode */
    DRISTY_POST_YOLOV2      = 1, /* YOLOv2 anchor-based decode + NMS */
    DRISTY_POST_CLASSIFY    = 2, /* softmax + top-K */
    DRISTY_POST_FACE_DETECT = 3, /* face-specific YOLO with landmarks */
    DRISTY_POST_SEGMENTATION= 4, /* per-pixel class map */
    DRISTY_POST_CUSTOM      = 5, /* user-defined via script */
} dristy_post_type_t;

/* kmodel version */
typedef enum
{
    DRISTY_KMODEL_V3 = 3,  /* nncase v0.1.0-rc5, hardware-only */
    DRISTY_KMODEL_V4 = 4,  /* nncase v0.2.0-beta4, HW+SW ops */
} dristy_kmodel_version_t;

/* Model metadata — populated from header or companion JSON */
typedef struct
{
    /* Identity */
    char name[DRISTY_MODEL_MAX_NAME_LEN];
    uint8_t slot;                   /* flash slot index, 0xFF = SD */

    /* kmodel info */
    dristy_kmodel_version_t kmodel_version;
    uint32_t model_size;            /* bytes */
    uint32_t model_crc32;           /* for integrity check */

    /* Input shape */
    uint16_t input_width;
    uint16_t input_height;
    uint8_t input_channels;         /* always 3 for RGB */

    /* Output info */
    dristy_post_type_t post_type;
    uint16_t num_classes;
    uint16_t grid_w;                /* for YOLO: grid width */
    uint16_t grid_h;                /* for YOLO: grid height */
    uint8_t num_anchors;            /* for YOLO: anchor count */
    float anchors[DRISTY_MODEL_MAX_ANCHORS * 2]; /* w,h pairs */

    /* Thresholds (defaults, overridable at runtime) */
    float confidence_threshold;
    float nms_threshold;

    /* Class names (null-separated concatenated string) */
    char class_names[512];
    uint16_t class_name_offsets[DRISTY_MODEL_MAX_CLASSES];
} dristy_model_meta_t;

/* Load status */
typedef enum
{
    DRISTY_MODEL_OK = 0,
    DRISTY_MODEL_NOT_FOUND,
    DRISTY_MODEL_TOO_LARGE,
    DRISTY_MODEL_BAD_HEADER,
    DRISTY_MODEL_BAD_CRC,
    DRISTY_MODEL_LOAD_FAILED,
    DRISTY_MODEL_NO_SD,
} dristy_model_status_t;

/* Initialise the model manager */
void dristy_model_init(void);

/* Get metadata for a flash slot (reads header without loading full model) */
dristy_model_status_t dristy_model_probe_slot(uint8_t slot,
                                              dristy_model_meta_t *meta);

/* Load a model from flash slot into KPU. Returns OK on success. */
dristy_model_status_t dristy_model_load_slot(uint8_t slot);

/* Load a model from SD card path. Returns OK on success. */
dristy_model_status_t dristy_model_load_sd(const char *path);

/* Unload the current model */
void dristy_model_unload(void);

/* Get the currently loaded model's metadata, or NULL if nothing loaded */
const dristy_model_meta_t *dristy_model_current(void);

/* Get the class name for a class ID from the current model */
const char *dristy_model_class_name(uint8_t class_id);

/* Runtime threshold adjustment */
void dristy_model_set_confidence(float threshold);
void dristy_model_set_nms(float threshold);

#endif /* DRISTY_MODEL_H */

#include "dristy_model.h"

#include <string.h>
#include <stdio.h>

#include "../../drivers/boot_flash.h"

/* ---------------------------------------------------------------------------
 * Model Manager Implementation
 *
 * Manages kmodel loading from flash and SD. The K210 KPU requires models
 * to be loaded into SRAM (either general or KPU-dedicated AI SRAM).
 *
 * kmodel v3 header (nncase v0.1.0-rc5):
 *   offset 0x00: uint32_t magic = 0x4B4D444C ('KMDL')
 *   offset 0x04: uint32_t version = 3
 *   offset 0x08: uint32_t flags
 *   offset 0x0C: uint32_t arch (0=CPU, 1=K210)
 *   offset 0x10: uint32_t layers
 *   ... layer descriptors
 *
 * kmodel v4 header (nncase v0.2.0-beta4):
 *   offset 0x00: uint32_t magic = 0x4B4D444C ('KMDL')
 *   offset 0x04: uint32_t version = 4
 *   offset 0x08: uint32_t flags
 *   offset 0x0C: uint32_t target (0=CPU, 1=K210)
 *   offset 0x10: uint32_t constants_size
 *   offset 0x14: uint32_t main_mem_size
 *   ... section descriptors
 *
 * For Dristy, we support both v3 and v4. The version is auto-detected.
 * Companion metadata (.json) on SD provides anchors, class names, etc.
 * For flash models, metadata is appended after the kmodel in the slot.
 * ------------------------------------------------------------------------- */

#define KMODEL_MAGIC 0x4B4D444CUL

/* Currently loaded model state */
static struct
{
    uint8_t loaded;
    dristy_model_meta_t meta;
    uint8_t *model_data;    /* pointer into SRAM where model was loaded */
    uint32_t model_addr;    /* flash address if loaded from flash */
} g_model;

/* Parse kmodel header from raw bytes */
static dristy_model_status_t parse_kmodel_header(const uint8_t *header,
                                                  dristy_model_meta_t *meta)
{
    uint32_t magic = *(const uint32_t *)header;
    if(magic != KMODEL_MAGIC)
        return DRISTY_MODEL_BAD_HEADER;

    uint32_t version = *(const uint32_t *)(header + 4);
    if(version == 3)
        meta->kmodel_version = DRISTY_KMODEL_V3;
    else if(version == 4)
        meta->kmodel_version = DRISTY_KMODEL_V4;
    else
        return DRISTY_MODEL_BAD_HEADER;

    return DRISTY_MODEL_OK;
}

/* Read flash slot header and populate metadata */
static dristy_model_status_t probe_flash_slot(uint32_t flash_addr,
                                               dristy_model_meta_t *meta)
{
    uint8_t header[32];

    /* Read first 32 bytes from flash */
    boot_flash_read(flash_addr, header, sizeof(header));

    dristy_model_status_t status = parse_kmodel_header(header, meta);
    if(status != DRISTY_MODEL_OK)
        return DRISTY_MODEL_NOT_FOUND;

    /* Read model size from flash metadata region
     * (stored as uint32_t at flash_addr - 4 by the flash tool) */
    /* For now, scan for a size marker or use fixed slot sizes */
    static const uint32_t slot_sizes[] = {
        0x200000, /* slot 0: 2 MB */
        0x100000, /* slot 1: 1 MB */
        0x100000, /* slot 2: 1 MB */
        0x200000, /* slot 3: 2 MB */
    };

    meta->model_size = slot_sizes[meta->slot];

    return DRISTY_MODEL_OK;
}

static const uint32_t g_flash_addrs[DRISTY_MODEL_MAX_SLOTS] = {
    DRISTY_MODEL_FLASH_SLOT0,
    DRISTY_MODEL_FLASH_SLOT1,
    DRISTY_MODEL_FLASH_SLOT2,
    DRISTY_MODEL_FLASH_SLOT3,
};

void dristy_model_init(void)
{
    memset(&g_model, 0, sizeof(g_model));
    printf("[DRISTY] model manager init, %d flash slots\r\n",
           DRISTY_MODEL_MAX_SLOTS);
}

dristy_model_status_t dristy_model_probe_slot(uint8_t slot,
                                              dristy_model_meta_t *meta)
{
    if(slot >= DRISTY_MODEL_MAX_SLOTS || !meta)
        return DRISTY_MODEL_NOT_FOUND;

    memset(meta, 0, sizeof(*meta));
    meta->slot = slot;

    dristy_model_status_t status = probe_flash_slot(g_flash_addrs[slot], meta);
    if(status == DRISTY_MODEL_OK)
    {
        /* Set default post-processing based on slot convention */
        switch(slot)
        {
        case 0: meta->post_type = DRISTY_POST_YOLOV2;      break;
        case 1: meta->post_type = DRISTY_POST_FACE_DETECT;  break;
        case 2: meta->post_type = DRISTY_POST_CLASSIFY;     break;
        case 3: meta->post_type = DRISTY_POST_CUSTOM;       break;
        }

        /* Default YOLO anchors for slot 0 (MobileNet-YOLOv2, 5 anchors) */
        if(slot == 0)
        {
            meta->num_anchors = 5;
            meta->grid_w = 10;
            meta->grid_h = 8;
            meta->input_width = 320;
            meta->input_height = 256;
            meta->input_channels = 3;
            meta->confidence_threshold = 0.3f;
            meta->nms_threshold = 0.3f;
            /* VOC/COCO-lite default anchors (scaled to grid) */
            float default_anchors[] = {
                1.08f, 1.19f,
                3.42f, 4.41f,
                6.63f, 11.38f,
                9.42f, 5.11f,
                16.62f, 10.52f
            };
            memcpy(meta->anchors, default_anchors, sizeof(default_anchors));
            meta->num_classes = 20;
        }

        printf("[DRISTY] slot %d: kmodel v%d, post=%d\r\n",
               slot, meta->kmodel_version, meta->post_type);
    }

    return status;
}

dristy_model_status_t dristy_model_load_slot(uint8_t slot)
{
    dristy_model_meta_t meta;
    dristy_model_status_t status;

    status = dristy_model_probe_slot(slot, &meta);
    if(status != DRISTY_MODEL_OK)
        return status;

    /* Check if model fits in available SRAM */
    if(meta.model_size > 2 * 1024 * 1024) /* 2 MB AI SRAM limit */
        return DRISTY_MODEL_TOO_LARGE;

    /* Unload any current model */
    dristy_model_unload();

    /* The actual KPU model loading is handled by the HAL's kpu_load_kmodel()
     * function. It reads directly from flash into KPU SRAM.
     * We store the metadata for post-processing. */
    g_model.meta = meta;
    g_model.model_addr = g_flash_addrs[slot];
    g_model.loaded = 1;

    printf("[DRISTY] model loaded: slot %d, v%d, %s\r\n",
           slot, meta.kmodel_version, meta.name[0] ? meta.name : "(unnamed)");

    return DRISTY_MODEL_OK;
}

dristy_model_status_t dristy_model_load_sd(const char *path)
{
    (void)path;
    /* TODO: implement SD card model loading
     * 1. Open file
     * 2. Read header → validate
     * 3. Read companion .json for metadata
     * 4. Load into SRAM
     * 5. Store metadata */
    printf("[DRISTY] SD model loading not yet implemented\r\n");
    return DRISTY_MODEL_NO_SD;
}

void dristy_model_unload(void)
{
    if(g_model.loaded)
    {
        printf("[DRISTY] model unloaded\r\n");
        g_model.loaded = 0;
        memset(&g_model.meta, 0, sizeof(g_model.meta));
    }
}

const dristy_model_meta_t *dristy_model_current(void)
{
    return g_model.loaded ? &g_model.meta : NULL;
}

const char *dristy_model_class_name(uint8_t class_id)
{
    if(!g_model.loaded)
        return "?";
    if(class_id >= g_model.meta.num_classes)
        return "?";
    if(g_model.meta.class_names[0] == '\0')
    {
        /* No class names loaded — use generic labels */
        static char buf[8];
        buf[0] = 'C';
        buf[1] = '0' + (class_id / 10);
        buf[2] = '0' + (class_id % 10);
        buf[3] = '\0';
        return buf;
    }
    return &g_model.meta.class_names[g_model.meta.class_name_offsets[class_id]];
}

void dristy_model_set_confidence(float threshold)
{
    if(g_model.loaded)
    {
        g_model.meta.confidence_threshold = threshold;
        printf("[DRISTY] confidence threshold: %.2f\r\n", threshold);
    }
}

void dristy_model_set_nms(float threshold)
{
    if(g_model.loaded)
    {
        g_model.meta.nms_threshold = threshold;
        printf("[DRISTY] NMS threshold: %.2f\r\n", threshold);
    }
}

#include "object_detect_labels.h"

#include "object_detect_config.h"

static const char *const g_labels[OBJECT_DETECT_CLASS_COUNT] = {
    "aeroplane", "bicycle", "bird", "boat", "bottle",
    "bus", "car", "cat", "chair", "cow",
    "diningtable", "dog", "horse", "motorbike", "person",
    "pottedplant", "sheep", "sofa", "train", "tvmonitor",
};

const char *object_detect_label(uint8_t class_id)
{
    return class_id < OBJECT_DETECT_CLASS_COUNT ? g_labels[class_id] : "unknown";
}

#ifndef HACKYLENS_CAPABILITY_INVENTORY_H
#define HACKYLENS_CAPABILITY_INVENTORY_H

#include "common.h"

#ifdef __cplusplus
extern "C" {
#endif

#define HK_CAPABILITY_INFO_VERSION 1U
#define HK_CAPABILITY_LIMIT_VERSION 1U
#define HK_CAPABILITY_CORE_ANY UINT16_MAX

enum
{
    HK_CAPABILITY_FLAG_SHARED = 1U << 0,
    HK_CAPABILITY_FLAG_EXCLUSIVE = 1U << 1
};

typedef struct
{
    uint16_t struct_size;
    uint16_t struct_version;
    uint32_t key;
    uint64_t value;
} hk_capability_limit_t;

typedef struct
{
    uint16_t struct_size;
    uint16_t struct_version;
    hk_capability_id_t id;
    hk_version_t version;
    uint64_t features;
    uint32_t flags;
    uint16_t instance;
    uint16_t affinity_core;
    const hk_capability_limit_t *limits;
    uint16_t limit_count;
    uint16_t reserved;
} hk_capability_info_t;

hk_result_t hk_capability_inventory_get(
    const hk_capability_info_t **entries,
    uint16_t *count);

#ifdef __cplusplus
}
#endif

#endif

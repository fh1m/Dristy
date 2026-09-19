#ifndef HK_AI_MODEL_TYPES_H
#define HK_AI_MODEL_TYPES_H

#include <stdint.h>

#define AI_MODEL_MAX_OUTPUTS 4U
#define AI_MODEL_MAX_TENSOR_DIMS 4U

typedef enum
{
    AI_MODEL_ELEMENT_U8 = 0,
    AI_MODEL_ELEMENT_I8,
    AI_MODEL_ELEMENT_F32,
} ai_model_element_t;

typedef enum
{
    AI_MODEL_LAYOUT_CHW = 0,
    AI_MODEL_LAYOUT_HWC,
    AI_MODEL_LAYOUT_FLAT,
} ai_model_layout_t;

typedef enum
{
    AI_MODEL_NORMALIZATION_NONE = 0,
    AI_MODEL_NORMALIZATION_ZERO_TO_ONE,
    AI_MODEL_NORMALIZATION_NEGATIVE_ONE_TO_ONE,
    AI_MODEL_NORMALIZATION_AFFINE,
} ai_model_normalization_t;

typedef enum
{
    AI_MODEL_POSTPROCESS_RAW = 0,
    AI_MODEL_POSTPROCESS_CLASSIFICATION,
    AI_MODEL_POSTPROCESS_YOLO2,
    AI_MODEL_POSTPROCESS_EMBEDDING,
} ai_model_postprocess_t;

typedef struct
{
    const char *name;
    uint32_t bytes;
    uint16_t shape[AI_MODEL_MAX_TENSOR_DIMS];
    uint8_t rank;
    ai_model_element_t element;
    ai_model_layout_t layout;
} ai_model_tensor_t;

typedef struct
{
    ai_model_normalization_t kind;
    float scale[3];
    float bias[3];
} ai_model_normalization_spec_t;

typedef struct
{
    uint32_t version;
    uint32_t flags;
    uint32_t arch;
    uint32_t layers_length;
    uint32_t max_start_address;
    uint32_t main_mem_usage;
    uint32_t output_count;
    uint32_t input_bytes;
} ai_model_kmodel_contract_t;

typedef struct
{
    const char *id;
    const char *directory_path;
    const char *model_path;
    const char *manifest_path;
    const char *labels_path;
    uint32_t max_model_bytes;
    uint32_t expected_model_crc32;
    uint32_t unload_timeout_us;
    ai_model_kmodel_contract_t kmodel;
    ai_model_tensor_t input;
    ai_model_tensor_t outputs[AI_MODEL_MAX_OUTPUTS];
    ai_model_normalization_spec_t normalization;
    ai_model_postprocess_t postprocess;
    uint16_t label_count;
    uint8_t output_count;
    uint8_t manifest_required;
} ai_model_descriptor_t;

typedef struct
{
    char id[33];
    char model_file[65];
    char labels_file[65];
    uint32_t model_size;
    uint32_t model_crc32;
    ai_model_kmodel_contract_t kmodel;
    uint32_t output_bytes[AI_MODEL_MAX_OUTPUTS];
    uint16_t input_shape[AI_MODEL_MAX_TENSOR_DIMS];
    uint16_t label_count;
    uint8_t input_rank;
    ai_model_element_t input_element;
    ai_model_layout_t input_layout;
    ai_model_normalization_t normalization;
    ai_model_postprocess_t postprocess;
    uint8_t output_count;
} ai_model_manifest_t;

#endif

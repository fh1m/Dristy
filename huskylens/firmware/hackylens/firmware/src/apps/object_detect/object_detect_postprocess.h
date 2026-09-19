#ifndef HK_OBJECT_DETECT_POSTPROCESS_H
#define HK_OBJECT_DETECT_POSTPROCESS_H

#include <stddef.h>
#include <stdint.h>

#include "object_detect_types.h"

uint8_t object_detect_postprocess(const float *output,
                                  size_t output_bytes,
                                  uint8_t confidence_percent,
                                  uint8_t nms_percent,
                                  object_detect_postprocess_workspace_t *workspace,
                                  object_detect_result_t results[OBJECT_DETECT_RESULT_MAX],
                                  uint16_t *candidate_count,
                                  object_detect_postprocess_stats_t *stats);

#endif

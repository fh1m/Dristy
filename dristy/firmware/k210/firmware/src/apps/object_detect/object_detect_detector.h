#ifndef HK_OBJECT_DETECT_DETECTOR_H
#define HK_OBJECT_DETECT_DETECTOR_H

#include <stddef.h>
#include <stdint.h>

#include "object_detect_types.h"

object_detect_load_result_t object_detect_detector_load(void);
void object_detect_detector_unload(void);
void object_detect_detector_service_tick(void);
uint8_t object_detect_detector_ready(void);
object_detect_load_result_t object_detect_detector_result(void);
void object_detect_detector_attach_camera(void);
void object_detect_detector_process_frame(uint32_t camera_sequence);
void object_detect_detector_invalidate_results(void);
void object_detect_detector_pause_capture(void);
void object_detect_detector_resume_capture(void);
void object_detect_detector_set_thresholds(uint8_t confidence, uint8_t nms);
const object_detect_result_t *object_detect_detector_results(uint8_t *count);
uint32_t object_detect_detector_result_sequence(void);
void object_detect_detector_note_present(uint32_t camera_sequence);
const char *object_detect_detector_error_label(object_detect_load_result_t result);
void object_detect_detector_format_info(char *line, size_t line_size);

#endif

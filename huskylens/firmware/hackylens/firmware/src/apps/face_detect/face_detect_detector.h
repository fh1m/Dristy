#ifndef HK_FACE_DETECT_DETECTOR_H
#define HK_FACE_DETECT_DETECTOR_H

#include <stddef.h>
#include <stdint.h>

#include "face_detect_types.h"

face_detect_load_result_t face_detect_detector_load(void);
void face_detect_detector_unload(void);
void face_detect_detector_service_tick(void);
uint8_t face_detect_detector_ready(void);
face_detect_load_result_t face_detect_detector_result(void);
void face_detect_detector_attach_camera(void);
void face_detect_detector_process_frame(void);
const face_detect_box_t *face_detect_detector_boxes(uint8_t *count);
const char *face_detect_detector_error_label(face_detect_load_result_t result);
void face_detect_detector_format_info(char *line, size_t line_size);

#endif

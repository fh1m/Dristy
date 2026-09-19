#ifndef PHOTO_ENCODE_H
#define PHOTO_ENCODE_H

#include "../../core/photo_types.h"

#include "camera_photo_types.h"

void photo_encode_begin(const photo_source_t *source);
void photo_encode_end(void);
void photo_encode_fill_file_bytes(photo_format_t format,
                                  uint32_t offset,
                                  uint8_t *dst,
                                  uint16_t len,
                                  uint16_t width,
                                  uint16_t height);

#endif

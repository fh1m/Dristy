#ifndef PHOTO_WRITER_H
#define PHOTO_WRITER_H

#include <stddef.h>
#include <stdint.h>
#include "camera_photo_types.h"

uint8_t photo_save_frame(const photo_source_t *source, char *saved_name, size_t saved_name_size);

#endif

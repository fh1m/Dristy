#ifndef PHOTO_SERVICE_H
#define PHOTO_SERVICE_H

#include <stddef.h>
#include <stdint.h>

uint8_t photo_service_save_current_frame(char *saved_name, size_t saved_name_size);

#endif

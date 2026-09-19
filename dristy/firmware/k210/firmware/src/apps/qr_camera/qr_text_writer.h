#ifndef QR_TEXT_WRITER_H
#define QR_TEXT_WRITER_H

#include <stddef.h>
#include <stdint.h>

uint8_t qr_text_save_payload_text(const char *payload, char *saved_name, size_t saved_name_size);

#endif

#ifndef SETTINGS_PAYLOAD_CODEC_H
#define SETTINGS_PAYLOAD_CODEC_H

#include "settings_snapshot.h"
#include "../storage/settings_store_types.h"

settings_payload_t settings_payload_encode(const settings_snapshot_t *snapshot);
void settings_payload_decode(const settings_payload_t *payload, settings_snapshot_t *snapshot);

#endif

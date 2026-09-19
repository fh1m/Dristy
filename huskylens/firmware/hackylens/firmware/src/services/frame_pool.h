#ifndef HK_FRAME_POOL_H
#define HK_FRAME_POOL_H

#include <stdint.h>

#define FRAME_POOL_CAMERA_SLOT_COUNT 2U

/* Camera reservation owns both slots until release. Scratch borrowers and the
 * camera reservation are mutually exclusive. These calls are allocation-free
 * and are used only from the main loop; camera frame leases remain the camera
 * stream's responsibility. */
uint8_t frame_pool_camera_reserve(void);
void frame_pool_camera_release(void);
uint16_t *frame_pool_camera_slot(uint8_t index);
uint32_t frame_pool_camera_frame_bytes(void);

#endif

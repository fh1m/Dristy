#ifndef CAMERA_SESSION_H
#define CAMERA_SESSION_H


#include <stdint.h>

#include "../core/camera_types.h"

typedef enum
{
    CAMERA_SETTINGS_RETURN_CAMERA = 0,
    CAMERA_SETTINGS_RETURN_QR_CAMERA,
    CAMERA_SETTINGS_RETURN_REINIT,
} camera_settings_return_t;

void camera_active_size_set(camera_size_t size);
uint8_t camera_service_start(void);
void camera_stop(void);
const char *camera_service_fail_reason(void);
void camera_service_enter_begin(uint8_t qr_mode, uint8_t ok_is_down);
uint8_t camera_service_is_qr_mode(void);
void camera_service_set_qvga_mode(uint8_t enabled);
void camera_service_clear_mode(void);
uint8_t camera_service_capture_ready(void);
void camera_service_freeze(uint8_t frozen);
void camera_service_resume_from_settings(uint8_t ok_is_down);
camera_settings_return_t camera_service_prepare_settings_return(uint8_t qr_mode,
                                                                 uint8_t ok_is_down);

#endif

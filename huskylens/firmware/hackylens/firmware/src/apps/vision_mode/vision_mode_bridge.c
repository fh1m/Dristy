#include "vision_mode_bridge.h"

#include <stddef.h>

#include "hk_config.h"
#include "vision_mode_shell.h"
#include "../../core/hk_app.h"
#include "../../core/hk_screen.h"

#if HK_ENABLE_APP_OBJECT_DETECT
#include "../object_detect/object_detect_controller.h"
#endif
#if HK_ENABLE_APP_FACE_DETECT
#include "../face_detect/face_detect_controller.h"
#endif
#if HK_ENABLE_APP_APRILTAG
#include "../apriltag/apriltag_controller.h"
#endif
#if HK_ENABLE_APP_QR_CAMERA
#include "../qr_camera/qr_camera_controller.h"
#endif

typedef enum
{
    BRIDGE_NONE = 0,
    BRIDGE_OBJECT,
    BRIDGE_FACE,
    BRIDGE_APRILTAG,
    BRIDGE_QR,
    BRIDGE_OBJECT_APRILTAG,
} vision_mode_bridge_kind_t;

static vision_mode_bridge_kind_t g_kind;
static dristy_mode_t g_mode;

static vision_mode_bridge_kind_t kind_for_mode(dristy_mode_t mode)
{
    switch(mode)
    {
    case DRISTY_MODE_DETECT:
    case DRISTY_MODE_DETECT_TRACK:
    case DRISTY_MODE_DETECT_CUSTOM:
    case DRISTY_MODE_CLASSIFY:
    case DRISTY_MODE_DETECT_MOTION:
        return BRIDGE_OBJECT;
    case DRISTY_MODE_FACE_DETECT:
    case DRISTY_MODE_FACE_RECOGNISE:
        return BRIDGE_FACE;
    case DRISTY_MODE_APRILTAG:
    case DRISTY_MODE_LANDING_TARGET:
        return BRIDGE_APRILTAG;
    case DRISTY_MODE_QR_CODE:
        return BRIDGE_QR;
    case DRISTY_MODE_DETECT_TAG:
    case DRISTY_MODE_DETECT_ARUCO:
        return BRIDGE_OBJECT_APRILTAG;
    default:
        return BRIDGE_NONE;
    }
}

void vision_mode_bridge_start(dristy_mode_t mode)
{
    g_mode = mode;
    g_kind = kind_for_mode(mode);
    vision_mode_shell_set_active(1U);
#if HK_ENABLE_APP_OBJECT_DETECT
    if(g_kind == BRIDGE_OBJECT || g_kind == BRIDGE_OBJECT_APRILTAG)
        object_detect_controller_enter(NULL);
#endif
#if HK_ENABLE_APP_APRILTAG
    if(g_kind == BRIDGE_APRILTAG || g_kind == BRIDGE_OBJECT_APRILTAG)
        apriltag_controller_enter(NULL);
#endif
#if HK_ENABLE_APP_FACE_DETECT
    if(g_kind == BRIDGE_FACE)
        face_detect_controller_enter(NULL);
#endif
#if HK_ENABLE_APP_QR_CAMERA
    if(g_kind == BRIDGE_QR)
        qr_camera_controller_enter(NULL);
#endif
    hk_screen_set(SCREEN_VISION_MODE);
    (void)g_mode;
}

void vision_mode_bridge_stop(void)
{
#if HK_ENABLE_APP_QR_CAMERA
    if(g_kind == BRIDGE_QR)
        qr_camera_controller_exit();
#endif
#if HK_ENABLE_APP_FACE_DETECT
    if(g_kind == BRIDGE_FACE)
        face_detect_controller_exit();
#endif
#if HK_ENABLE_APP_APRILTAG
    if(g_kind == BRIDGE_APRILTAG || g_kind == BRIDGE_OBJECT_APRILTAG)
        apriltag_controller_exit();
#endif
#if HK_ENABLE_APP_OBJECT_DETECT
    if(g_kind == BRIDGE_OBJECT || g_kind == BRIDGE_OBJECT_APRILTAG)
        object_detect_controller_exit();
#endif
    g_kind = BRIDGE_NONE;
    vision_mode_shell_set_active(0U);
}

uint8_t vision_mode_bridge_active(void)
{
    return g_kind != BRIDGE_NONE;
}

void vision_mode_bridge_tick(const hk_input_snapshot_t *input)
{
#if HK_ENABLE_APP_OBJECT_DETECT
    if(g_kind == BRIDGE_OBJECT || g_kind == BRIDGE_OBJECT_APRILTAG)
        object_detect_controller_tick(input);
#endif
#if HK_ENABLE_APP_APRILTAG
    if(g_kind == BRIDGE_APRILTAG || g_kind == BRIDGE_OBJECT_APRILTAG)
        apriltag_controller_tick(input);
#endif
#if HK_ENABLE_APP_FACE_DETECT
    if(g_kind == BRIDGE_FACE)
        face_detect_controller_tick(input);
#endif
#if HK_ENABLE_APP_QR_CAMERA
    if(g_kind == BRIDGE_QR)
        qr_camera_controller_tick(input);
#endif
}

void vision_mode_bridge_handle_buttons(const hk_input_snapshot_t *input)
{
#if HK_ENABLE_APP_OBJECT_DETECT
    if(g_kind == BRIDGE_OBJECT || g_kind == BRIDGE_OBJECT_APRILTAG)
        object_detect_controller_handle_buttons(input);
#endif
#if HK_ENABLE_APP_APRILTAG
    if(g_kind == BRIDGE_APRILTAG || g_kind == BRIDGE_OBJECT_APRILTAG)
        apriltag_controller_handle_buttons(input);
#endif
#if HK_ENABLE_APP_FACE_DETECT
    if(g_kind == BRIDGE_FACE)
        face_detect_controller_handle_buttons(input);
#endif
#if HK_ENABLE_APP_QR_CAMERA
    if(g_kind == BRIDGE_QR)
        qr_camera_controller_handle_input(input);
#endif
}

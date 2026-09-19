# Architecture

HackyLens 0.4.0 is modular firmware built from common `firmware/src` layers, the selected `boards/<id>` BSP, and the K210 HAL/startup under `platforms/k210`.

> HackyLens v0.4 is a layered K210 reference firmware and MicroPython technology
> preview.

This document describes the implemented v0.4 architecture. The normative
direction is defined in [ARCHITECTURE_VISION.md](ARCHITECTURE_VISION.md), and
the remaining gaps are recorded in [CURRENT_STATE.md](CURRENT_STATE.md).

`firmware/targets/full.c` is a small composition root. It configures the runtime loop in `runtime/hk_main.c`, whose input polling and sleep timing remain platform-dependent. `core` owns app contracts, screen model, dispatch contracts, and neutral data contracts such as `core/pixel_source.h`; it does not access `hk_input` or `hal_time` directly.

## Startup

`runtime/firmware_startup.c` owns startup orchestration. It initializes the platform clocks and hardware through `platforms/k210/startup/platform_bootstrap.c`, loads persisted settings, applies brightness and illumination/RGB settings, initializes the boot controller, shows the boot screen, and mounts storage. The neutral autostart controller then opens the persisted registry target or falls back to the menu; it never includes feature headers.

`platform_bootstrap` is limited to board, HAL, LCD, and hardware-driver initialization. `controllers/boot_controller.c` registers shell callbacks, prepares the menu view, and writes the boot banner; feature view initialization belongs to each app's `enter` callback.

## Layer boundaries

- `drivers` contain board-independent device APIs. The selected `boards/<id>` BSP owns board wiring and `platforms/k210/hal` owns K210 SDK access.
- `runtime` adapts core lifecycle and startup to platform facilities.
- `controllers` coordinate scenarios and pass state to services and UI views.
- `services` own runtime operations such as camera sessions, settings application, debug console I/O, LCD screenshot sourcing, and screenshot UART streaming.
- `storage` encodes and persists data. `storage/screenshot_bmp.c` only encodes BMP using a supplied `screenshot_pixel_source_t`; it never accesses the LCD or UART.
- `ui` renders views and is the only application-facing layer that draws through the LCD driver.

Screenshot UART output keeps the `HKSHOT BEGIN BMP24` / `HKSHOT END` protocol and CRC. `services/screenshot_source.c` supplies the LCD shadow source, `storage/screenshot_bmp.c` encodes bytes, and `services/debug_screenshot_stream.c` sends them through `services/debug_console_service.c`.

`tools/check_arch.py` guards include boundaries, forbidden compatibility headers, SDK-token placement, include cycles, and feature-module ownership.

The shared AI platform is split across the normal layers. `core/ai_model_types.h`
describes tensors, normalization, post-processing, and exact KModel contracts.
`storage/ai_model_storage.*` owns aligned FAT32 loading plus the optional
CRC-protected SD manifest. `services/ai_model_runtime.*` owns the single-KPU
lease, descriptor/manifest/model-identity/output validation, asynchronous run timing, and
deferred stop/unload state machine. `platforms/k210/hal/hal_kpu.*` remains limited to SDK and
peripheral operations. None of these shared files knows about a camera, DVP
capture policy, a feature app, or a particular post-processor.

Camera KPU consumers share one aligned 320x240 planar RGB input through
`services/camera_ai_input.*`. Neutral conversion hooks in `camera_stream`
arm the DVP AI output at frame start and freeze it at frame finish. KPU
therefore receives a complete immutable frame with the exact camera sequence,
instead of racing the next display capture. `services/core1_executor.*`
similarly owns the one reusable core-1 loop and accepts one bounded job at a
time; feature modules do not replace each other's core-1 entry point.

The shared `settings_menu` controller is an instance-based UI state machine driven by a constant item descriptor table and owner callbacks. Its passive view owns only LCD rendering. It has no camera, application, storage, persistence, or screen-navigation dependencies; owner controllers retain opening/closing lifecycle and all settings side effects. CAMERA, QR, APRILTAG, OBJECT DETECT, and the system SETTINGS app supply separate descriptor adapters. Cycle-on-OK and edit-on-OK rows share navigation, partial redraw, hold-repeat, commit lifecycle, and optional dynamic choice providers without sharing their data models.

Backlight, illumination, and RGB writes cross the public Lights capability. Its
fixed K210 provider owns exclusive overlapping channel masks, validates
cancellation/deadlines before register writes, and performs safe-off cleanup.
Settings and temporary camera/MicroPython policy remain above that provider;
only the K210 adapter includes the lights driver interface.

Display 0.1 is implemented by the production K210 provider and the deterministic
fixed-capacity host fake. The provider owns BASE/OVERLAY plane leases, bounded
batch/surface staging, clipped dirty regions, borrowed-buffer lifetime, present
retry, repair, and cleanup. A private UI binding holds the typed BASE handle;
MicroPython holds OVERLAY. Both reuse the one ST7789 shadow framebuffer over a
raw transport that has no app, run-ID, or Python policy. The provider reports
composition-specific bounded batch limits: the full profile reserves the
retained MicroPython canvas, while a MicroPython-disabled composition keeps a
minimal truthful batch implementation and does not reserve that absent
consumer's command/text budget.

## Feature modules

All twelve menu applications are self-contained modules: `apps/terminal/`,
`apps/camera/`, `apps/qr_camera/`, `apps/face_detect/`, `apps/apriltag/`,
`apps/object_detect/`, `apps/micropython/`, `apps/files/`, `apps/buttons/`,
`apps/pong/`, `apps/settings/`, and `apps/sleep/`. Each owns its app entry point,
controller, view, icon, feature configuration, and feature-specific
state/services. The only public header of a module is its `*_app.h`, and only
`apps/app_registry.c` may include it.

The build manifest maps each app ID to its whole directory. `--disable-app`
therefore removes every source and private header of that feature. Shared camera
sources remain while CAMERA, QR-CAMERA, FACE DETECT, APRILTAG, or OBJECT DETECT
is enabled; `quirc` is staged only for QR-CAMERA. The planar AI input is staged
only for FACE/OBJECT. The shared core-1 executor is retained while APRILTAG or
MICROPYTHON needs it. With no camera consumer, sensor/DVP/camera runtime sources
are omitted while the general KPU HAL remains available.

The registry dispatches primary and secondary screen ownership, lifecycle callbacks, background ticks, SD events, menu icons, and debug commands. Shared screen, SD, debug, boot, and system-tick controllers do not include feature headers or select features with conditionals.

CAMERA owns photo capture orchestration, encoders/writers, photo paths, settings adapter, and its view. QR-CAMERA owns quirc integration, luma conversion, result state/view, text persistence, settings, and its view. Both reuse the shared camera session, sensor/frame pipeline, camera preview renderer, and settings persistence.

FILES owns browser navigation/state, previews and deletion, all BMP/PNG/PPM/RAW/GIF decoders, and its view. Shared FAT32 provides neutral mount, directory scan, file, allocation, and stream contracts and has no dependency on FILES browser state. BUTTONS, system SETTINGS, and SLEEP likewise own their controllers and views; SLEEP receives the input snapshot through the registry background lifecycle for auto-sleep.

FACE DETECT owns its YOLO detector adapter, view, icon, and debug command. The feature supplies a constant model descriptor and keeps all face-specific output decoding, while generic aligned storage, KPU ownership, DVP frame handoff, output validation, timing, and deferred unload belong to shared services. Its model remains `/hackylens.kmodels/detect.kmodel` on the SD card and is not embedded in firmware flash.

APRILTAG owns its TAG36H11 detector adapter, grayscale downsampler, settings/selection model and descriptor adapter, stabilized in-frame result overlay, icon, and `HKTAG`/`HKTAGINFO` commands. Its descriptor adapter uses the shared camera-independent `settings_menu`, while APRILTAG retains persistence and camera pause/resume policy. It reuses the shared 320x240 camera runtime and publishes native tag IDs through `vision_result_service` as BLOCK results. Core 0 downsamples a leased camera frame only when the single uncached 160x120 luma handoff and shared core-1 executor are free, then immediately releases the frame. Frames seen while detection is busy are discarded instead of queued, preventing stale-result latency. Core-1 create/detect/destroy jobs atomically publish completed result banks and leave the executor available to other apps. `refine_edges` is a persisted runtime request applied before the next detection rather than a detector restart. The app uses camera session overrides for its independent FPS and LED/RGB profile, leaving CAMERA and QR settings unchanged. Its `ALL/SELECTED` filter is applied before the shared result snapshot, so the UART/I2C wire format remains unchanged. Preview overlays are composed into the LCD shadow before the full-frame transfer, so rectangles are not temporarily erased by the following camera frame. The detector does not use a KPU model. The BSD-licensed OpenMV AprilTag core is staged from `firmware/third_party/apriltag` only when this feature is enabled.

OBJECT DETECT owns the VOC20 labels, YOLOv2 decoder/NMS, settings adapter,
overlay, icon, and `HKOBJECT`/`HKOBJECTINFO` commands. Its pinned KModel v3
accepts planar `1x3x240x320` bytes and returns `1x125x7x10` floats. KPU
inference is asynchronous; completed output is decoded on core 0 before a new
DVP input is armed, avoiding K210's non-coherent cross-core cache boundary.
Newer camera frames are discarded rather than queued. Double result banks and
session epochs prevent partial or
pre-settings results from becoming visible. Boxes are composed before the
single LCD present and published as the existing transport-neutral BLOCK
format with class IDs `0..19`.

Terminal owns its bounded line ring, viewport, scrolling, font geometry, and log-sink lifecycle. The shared logging service exposes only a generic optional sink and remains independent of Terminal. Font selection remains in reserved feature bits inherited from the legacy settings payload, so old records and erased flash continue to decode as `TERMINAL_FONT_NORMAL`.

Settings record v4 retains the v3 prefix and autostart ID, and extends opaque
app data from 80 to 88 bytes. Bytes `0..79` remain APRILTAG's preferences and
587-bit selected-ID map; OBJECT DETECT owns bytes `80..87`. The storage layer
validates and migrates v1, v2, and v3 records without changing CAMERA,
external-link, APRILTAG, or autostart data.

## Architecture guard

`tools/check_arch.py` uses one declarative table for all twelve feature directories. It rejects legacy paths, flat app implementations, external inclusion of private feature headers, private settings-menu view access, layer inversions, include cycles, a mismatch between feature directories and the build manifest, app access to AI storage/HAL internals, and camera/feature dependencies in the shared AI platform.

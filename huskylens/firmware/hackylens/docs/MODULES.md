# Modules

> HackyLens v0.4 is a layered K210 reference firmware and MicroPython technology
> preview.

## External link and vision results

`services/external_link_protocol.*` owns the versioned transport-neutral wire
codec. `services/external_link_service.*` dispatches it over external UART1 or
I2C0 without touching the UART3 debug console. `services/vision_result_service.*`
is the shared producer/consumer boundary for fixed BLOCK/ARROW results. Physical
pin and peripheral operations stay in board/HAL. See
`docs/EXTERNAL_LINK_PROTOCOL.md` for the public wire contract.

Default enabled apps: TERMINAL, CAMERA, QR-CAMERA, FACE DETECT, APRILTAG,
OBJECT DETECT, MICROPYTHON, FILES, BUTTONS, PONG, SETTINGS, SLEEP.

## Capability core

`firmware/include/hackylens/capability/` contains the experimental Capability
API 0.1 common ABI. `firmware/src/capabilities/capability_core.c` owns fixed
owner/lease tables, grant and generation validation, affinity checks, provider
quarantine/recovery, and bounded owner-wide cleanup. Provider callbacks and
mutable core state are private in `capability_provider.h`; public typed handles
contain only an `hk_lease_t`.

Phase 2 is complete for the SEN0305 runtime profile. The generated immutable
inventory contains exactly Time, Input, Display, External Link, and Lights;
native and MicroPython consumers share the same providers and lifecycle core.
`runtime/capability_owner_runtime.c` privately binds generation-checked owners
around menu entry/exit callbacks without changing `hk_app_t`. Deterministic
fakes and K210 providers run the same normative contract suites. Public storage,
camera, vision, AI, app-context, and manifest capabilities remain Phase 3+.

Compile-time app flags are generated into `hk_config.h` by
`tools/build_firmware.py`. The app registry lives in `apps/app_registry.c`.
Every `--disable-app <name>` omits the corresponding complete
`apps/<feature>/` directory. Disabling QR-CAMERA also omits `quirc`; disabling
APRILTAG omits its vendored detector core and TAG36H11 table. The shared core-1
executor remains when APRILTAG or MICROPYTHON requires it. Shared camera sources
are retained only while at least one camera consumer is enabled; the shared
planar AI input requires FACE or OBJECT.

Key public interfaces:

- `hackylens/capability/common.h`, `inventory.h`, and `owner.h` for the public
  Capability API ABI, immutable discovery shape, and typed-handle convention.
- `core/ai_model_types.h` and `services/ai_model_runtime.h` for model metadata
  and the instance-based load/run/stop/unload API. `storage/ai_model_storage.h`
  and `platforms/k210/hal/hal_kpu.h` are implementation boundaries used by the runtime, not
  feature APIs. See `docs/AI_MODELS.md` for the SD manifest and conversion lab.
- `core/hk_app.h`, `core/hk_app_registry.h`, and `core/hk_screen.h` for app metadata, stable autostart IDs, lookup, and screen model. Registry enumeration is the only source of enabled autostart choices; SETTINGS and SLEEP have no autostart ID.
- `apps/camera/camera_app.h`, `apps/qr_camera/qr_camera_app.h`, `apps/files/files_app.h`, `apps/buttons/buttons_app.h`, `apps/settings/settings_app.h`, and `apps/sleep/sleep_app.h` are the sole public contracts for the newly isolated modules. Their private controllers, adapters, decoders, views, and configuration are not shared APIs.
- `controllers/settings_menu_controller.h` for reusable instance-based settings menus. Owners supply item descriptors and callbacks; the component owns navigation, edit/cycle interaction, static or dynamic choices, partial redraw, repeat, and commit notification but never persistence or application lifecycle. CAMERA, QR, APRILTAG, OBJECT DETECT, and system SETTINGS are current consumers.
- `core/pixel_source.h` for a neutral pixel-reader contract.
- `runtime/hk_main.h` and `runtime/firmware_startup.h` for the platform loop and startup composition.
- `services/settings_persistence.h` and `services/settings_lights.h` for settings load and application.
- `services/debug_console_service.h` for narrow debug UART read/write access.
- `services/debug_screenshot_stream.h` and `services/screenshot_source.h` for screenshot UART transport and LCD shadow sourcing.
- `drivers/camera_stream.h` for the IRQ-driven two-slot camera stream and its explicit frame-lease contract; SDK interrupt details remain private to `platforms/k210/hal/hal_dvp.c`.
- `apps/face_detect/face_detect_app.h` is the sole public FACE DETECT interface; its private YOLO decoder, controller, DVP adapter, view, configuration, and types remain inside the module. The KPU model is read from `/hackylens.kmodels/detect.kmodel` through the shared AI runtime.
- `apps/apriltag/apriltag_app.h` is the sole public APRILTAG interface. The module detects TAG36H11 markers on a core-1 worker, reports native IDs `0..586`, and owns its hold-OK settings lifecycle and descriptor adapter, central selection crosshair, persistent selected-ID bitmap, and `ALL/SELECTED` publication filter. Unselected blocks are green and selected IDs are yellow; the numeric ID stays inside each block.
- `apps/object_detect/object_detect_app.h` is the sole public OBJECT DETECT interface. The module owns VOC20 decoding, class labels, overlays, settings, persistence, and diagnostics. Its SD package is `/hackylens.kmodels/object20/`.
- `services/camera_ai_input.h` owns the single aligned planar DVP/KPU input and exact frame-boundary handoff shared by FACE and OBJECT. `services/core1_executor.h` owns APRILTAG's reusable core-1 job slot.
- `services/camera_session_preferences.h` supplies optional per-session FPS and LED/RGB overrides. APRILTAG and OBJECT use independent values; CAMERA and QR continue to read their normal persisted profile after the override is cleared.
- Settings storage v4 keeps APRILTAG's original 80-byte app block, appends eight OBJECT bytes, and retains the autostart ID. The loader accepts v1/v2/v3 and preserves all previous settings.
- `ui/display_binding.h` for the private native-view binding to a typed Display
  BASE handle. `capabilities/display.c` owns the public dispatch boundary,
  `platforms/k210/capabilities/display_adapter.c` owns plane state and bounded
  composition, and `drivers/lcd_st7789_transport.h` is raw panel transport only.
- `storage/screenshot_bmp.h` for BMP encoding, `storage/screenshot_writer.h` for persistence, and focused FAT32/file headers for storage operations.
- `storage/file_mount.h` and `storage/file_dir_scan.h` for neutral FAT mount and directory queries. Browser lists and image viewing are private FILES APIs.
- `storage/sd_card.h` is the permanent private raw-block boundary; no feature
  app includes an SD driver header. `services/frame_pool.*` owns the two fixed
  camera slots and the mutually exclusive generation-checked scratch workspace;
  `services/frame_workspace.h` exposes only its internal borrow/release view.
- `apps/files/image_viewer.h` is private to FILES and supports BMP/PNG/PPM/RAW plus streaming animated GIF87a/GIF89a. GIF playback supports palettes, transparency, interlace, disposal, pause/resume, bulk sub-block reads, and a 1600x1200 logical-canvas limit without loading the complete file into RAM. FILES decodes FAT long names from UTF-16 to UTF-8, renders Russian Cyrillic, and sorts entries by FAT modification time with newest entries first.
- `ui/hk_ui.h` and per-screen view headers for UI rendering.

Private headers are allowed only within their subsystem boundary; they are not compatibility facades or new global contracts.

`tools/architecture_layers.toml` is the explicit layer classification consumed
by architecture guard v2. The guard checks repository sources, forwarding and
resolved symlink targets, generated compiler dependency files, undefined object
symbols, generated-only provider inventory, and identical provider objects in
full and MicroPython-disabled profiles. The `public-capability` layer contains
the public headers and the five common lifecycle frontends; provider bindings,
private state machines, and K210 adapters remain `capability-implementation`.

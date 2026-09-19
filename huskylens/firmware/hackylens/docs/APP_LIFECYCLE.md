---
contract-id: hackylens.legacy-app-lifecycle
owner: firmware-runtime
version: 0.2.0
stability: experimental
---

# App Lifecycle

This document describes the lifecycle shipped by firmware `0.2.0`. The matching
version is historical and does not permanently couple firmware releases to the
future App SDK or runtime contract.

Apps are described by `hk_app_t`: `id`, `title`, `screen`, stable `autostart_id`, `enter`, optional `exit`, optional `tick`, optional `handle_input`, optional `owns_screen`, optional `draw_icon`, optional `background_tick(input)`, optional `handle_sd_event`, optional SD-poll blocking, optional tick interval, optional `handle_debug_command`, and optional `debug_help`. Registry entries use designated initializers so optional fields are independent of structure order. SETTINGS and SLEEP retain the zero/OFF autostart ID and cannot be selected as boot targets.

`runtime/hk_main.c` runs the firmware superloop: it processes debug input, polls buttons, dispatches shell input, ticks the menu and active app, runs the system tick, then sleeps for the configured camera or non-camera interval. `SCREEN_MENU` opens the selected registry entry through its `enter` callback. `SCREEN_CAMERA_SETTINGS` remains a service screen owned by CAMERA/QR-CAMERA rather than a top-level app.

After settings load, boot UI, and SD mount, startup resolves the persisted autostart ID through the registry and calls the target's normal `enter` with an empty input snapshot. The menu index is set to the target before entry, so BACK returns with that item selected. OFF or a target omitted by the current build opens the menu; an omitted target remains persisted and becomes active again when a later build restores it. `HKMENU` and ordinary exits never retrigger autostart.

Screen state is exposed through `core/hk_screen.h`. Apps and controllers receive
the existing `hk_input_snapshot_t` ABI from the runtime loop. The loop adapts
sequenced `hackylens.cap.input` events; apps and controllers must not access the
raw button sampler or capability provider state directly.

Light policy services use `hackylens.cap.lights` channel-mask leases. Persisted
settings keep separate backlight, illumination, and RGB leases; a camera session
or MicroPython releases only the overlapping settings lease, owns that channel
through the same K210 provider, reaches safe-off on cleanup, and then lets the
settings service reacquire and apply its latest values. Sleep switches the
settings-owned backlight through the same capability rather than the driver.

Reusable settings menus are neutral child sessions rather than screens or applications. `settings_menu` reports a close request to its owner; the owner decides whether BACK resumes a camera, returns to a game, or changes screens. Values, persistence, and immediate hardware effects are provided through owner callbacks. Ending edit mode through OK, BACK, or forced session close emits the same commit callback.

CAMERA and QR each own a settings child session and descriptor table. `owns_screen` maps their shared `SCREEN_CAMERA_SETTINGS` secondary screen back to the correct active app. BACK closes the child session and preserves the existing camera resume, QR resume, or size-reinitialization path. System SETTINGS uses edit-on-OK rows; BACK first commits and leaves edit mode, while a second BACK exits. Its app `exit` callback closes the session so `HKMENU` also commits an active edit.

Every app lives in a self-contained `apps/<feature>/` directory; `apps/app_registry.c` includes only public app entry points. The registry dispatches secondary screens, background ticks with the current input snapshot, SD events, debug commands, and `HKHELP` tokens generically. FILES closes preview/GIF state through `exit` and owns SD insertion/removal handling. CAMERA, QR-CAMERA, FACE DETECT, APRILTAG, and OBJECT DETECT stop their own camera sessions in `exit`, so BACK and `HKMENU` share one cleanup path. SLEEP owns both explicit sleep/wake and menu auto-sleep through its background callback.

FACE DETECT starts the shared camera session and asks its `ai_model_runtime_t`
instance to load `/hackylens.kmodels/detect.kmodel` on entry. The runtime
atomically acquires the single KPU owner slot, validates the descriptor and
output tensors, and runs inference asynchronously. The shared camera AI input
captures one complete planar frame at DVP boundaries and remains immutable
until KPU completion. Its `exit` callback requests model unload exactly once.
If inference is active, the module's `background_tick` services the generic
deferred unload after navigation has returned to the menu. A rapid transition
from another AI app shows `AI BUSY` and retries without misreporting allocation
failure.

APRILTAG acquires the shared core-1 executor before entering the camera in fixed 320x240 analysis mode. Its frame consumer downsamples only when the private luma handoff is free; frames arriving during detection are discarded so no stale queue forms. Create, detect, and destroy are bounded jobs, leaving core 1 reusable by other features. Completed native IDs are atomically published as BLOCK results. The previous stable overlay is composed into the next LCD frame and survives one isolated detection miss, avoiding frame-transfer flicker.

Short OK toggles the ID whose block contains the fixed center crosshair; a 700 ms hold freezes capture and opens a shared `settings_menu` session configured by APRILTAG descriptors while remaining under the app lifecycle. Entering settings clears the external result snapshot. BACK reports a neutral close request; APRILTAG then resumes the camera stream, ignores the held-OK release, skips pre-settings detector results, and waits for a new frame. Live BACK remains on core 0 and returns immediately; `exit` stops any paused camera path, invalidates pending results, clears camera session overrides, and requests detector destruction without waiting for an in-flight detection. Selected IDs are native TAG36H11 IDs rather than original HUSKYLENS learned slots.

OBJECT DETECT loads the required model/manifest pair before starting its QVGA
camera session. DVP captures the newest complete planar AI frame, KPU runs
asynchronously, and full per-class YOLOv2 decode/NMS runs on core 0 after KPU
completion. There is no frame queue: while either stage is busy, incoming
analysis frames are dropped. A session epoch
rejects any result begun before settings, exit, or re-entry. Holding OK freezes
capture and opens OBJECT settings; BACK resumes only from a new frame. Exit
clears external BLOCK results immediately and completes model cleanup
through background ticks, so navigation stays responsive.

# Dristy runtime invariants (SEN0305)

1. **Build**: `dist/dristy-full-sen0305.bin` must be newer than any change under `firmware/src/`.
2. **Camera owner**: Enter vision modes only through `camera_runtime_enter`; exit with `camera_stop` before switching bridge vs classical preview.
3. **Classical pipeline**: `process_classical` requires `camera_stream_acquire_latest`; pipeline tick for classical-only modes runs once per new `camera_stream` sequence.
4. **LCD overlays**: KPU modes draw labels via compose overlays (`object_compose_overlay`, etc.); classical modes use `vision_mode_view_compose_overlays` on the preview buffer.
5. **Host API**: DLP `SET_MODE` updates pipeline mode and deferred-opens vision shell when LCD is on.

Violations to watch in serial logs: `CAMERA FAIL`, `NO FRAME`, `VISION FAIL`.

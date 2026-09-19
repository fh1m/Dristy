# Dristy mode readiness (SEN0305)

All modes in `dristy_modes.c` are **LIVE**: menu, LCD vision shell, pipeline, and DLP `SET_MODE`.

Source of truth: [`dristy_mode_readiness.c`](../firmware/dristy/firmware/src/dristy/dristy_mode_readiness.c).

| Mode | LCD path | Host API |
|------|----------|----------|
| Neural (detect, custom, classify, face, face ID) | Legacy bridge + result bus | `read`, `read_tracks` |
| Classical (colour, line, flow, sort, aruco, motion) | Classical camera + pipeline | `read_blobs`, `read`, `read_flow` |
| Tags / QR | Legacy bridge | `read_tags`, `read_qr` |
| Hybrid (det+trk, det+tag, landing, det+aruco, mot+det) | Bridge + pipeline fusion | combined calls |

See [`DRISTY_CAPABILITY_PARITY.md`](DRISTY_CAPABILITY_PARITY.md).

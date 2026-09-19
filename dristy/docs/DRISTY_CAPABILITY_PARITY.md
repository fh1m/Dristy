# Dristy capability parity matrix (SEN0305)

Single table for menu, LCD, pipeline, DLP, Python API, and HW smoke.  
**Design decision (v1):** Neural and tag/QR LCD modes use **legacy app bridge + legacy export → result bus**. Classical ArUco/Motion use **pipeline `process_classical` + live camera stream**. KPU YOLO decode inside `process_kpu_result` remains optional; detections/tracks come from legacy sync until full decode lands.

Sources: [`DRISTY_ARCHITECTURE.md`](DRISTY_ARCHITECTURE.md) §4–§10, [`dristy_modes.c`](../firmware/dristy/firmware/src/dristy/dristy_modes.c), [`dristy_mode_readiness.c`](../firmware/dristy/firmware/src/dristy/dristy_mode_readiness.c).

## Legend

| Column | Meaning |
|--------|---------|
| Readiness | `LIVE` = menu subtitle is mode name; `STUB` = "Coming soon" |
| LCD | `bridge` / `classical` / `stub` |
| Host smoke | `dristy_mode_smoke.py` expectation |

## Mode matrix

| Mode | ID | Readiness | LCD | Pipeline caps | Result bus (when scene present) | DLP | Python | HW smoke |
|------|-----|-----------|-----|---------------|----------------------------------|-----|--------|----------|
| Object Detection | 0x00 | LIVE | bridge→object | KPU | detections, tracks | GET_RESULT, GET_TRACKS | set_mode, read, read_tracks | SMK-DET |
| Custom Model | 0x01 | STUB | stub | KPU | — | SET_MODE only | set_mode | SMK-STUB |
| Classification | 0x02 | STUB | stub | KPU | — | SET_MODE only | set_mode | SMK-STUB |
| Face Detection | 0x03 | LIVE | bridge→face | KPU | detections | GET_RESULT | read | SMK-FACE |
| Face Recognition | 0x04 | STUB | stub | KPU | — | SET_MODE only | set_mode | SMK-STUB |
| Colour Tracking | 0x10 | STUB* | stub | CLASSICAL+TRACKER | blobs (when implemented) | GET_BLOBS | read_blobs | SMK-STUB |
| Line Following | 0x11 | STUB* | stub | CLASSICAL | line (when implemented) | GET_RESULT | read | SMK-STUB |
| AprilTag | 0x12 | LIVE | bridge→apriltag | CLASSICAL+TAGS | tags | GET_TAGS, GET_RESULT | read_tags | SMK-ATAG |
| QR Code | 0x13 | LIVE | bridge→qr | CLASSICAL+QR | qr | GET_QR, GET_RESULT | read_qr | SMK-QR |
| Optical Flow | 0x14 | STUB* | stub | CLASSICAL+FLOW | flow (when implemented) | GET_FLOW | read_flow | SMK-STUB |
| Colour Sort | 0x15 | STUB* | stub | CLASSICAL | blobs | GET_BLOBS | read_blobs | SMK-STUB |
| ArUco Marker | 0x16 | LIVE | classical camera | CLASSICAL+TAGS | tags | GET_TAGS | read_tags | SMK-ARUCO |
| Motion Detection | 0x17 | LIVE | classical camera | CLASSICAL | detections | GET_RESULT | read | SMK-MOTION |
| Detect + Track | 0x20 | LIVE | bridge→object | KPU+TRACKER | detections, tracks | GET_TRACKS | read_tracks | SMK-DTRK |
| Detect + AprilTag | 0x21 | LIVE | bridge→object+tag | KPU+CLASSICAL+TAGS | detections, tags | GET_TAGS, GET_RESULT | read_tags | SMK-DTAG |
| Landing Target | 0x22 | LIVE | bridge→apriltag | CLASSICAL+TAGS+FLOW | tags | GET_TAGS | read_tags | SMK-LAND |
| Detect + ArUco | 0x23 | STUB* | stub | KPU+CLASSICAL+TAGS | hybrid TBD | GET_TAGS | read_tags | SMK-STUB |
| Motion-Gated Detect | 0x24 | STUB | stub | KPU+CLASSICAL | — | SET_MODE only | set_mode | SMK-STUB |

\* P2 research spikes documented in §Research backlog; flip to LIVE only after HW smoke passes.

## DLP command coverage

| Command | Status |
|---------|--------|
| 0x40–0x42, 0x44, 0x47, 0x49, 0x4E, 0x67 | Implemented |
| 0x43 GET_FLOW | Returns result bus flow (empty until FFT mode) |
| 0x45 GET_QR | Returns result bus QR entries |
| 0x4B GET_RAW_FRAME | Not implemented (documented) |

## Host vs menu

`DLP_SET_MODE` updates pipeline mode only; LCD stays on current screen unless user opens a mode from the menu. Documented for headless robotics.

## Research backlog (P2/P3)

| Mode | ARCH | Spike notes |
|------|------|-------------|
| COLOUR_TRACK / COLOUR_SORT | §4.2 | LAB threshold + connected components on grayscale; wire `SET_COLOUR_THRESH` |
| LINE_FOLLOW | §4.2 | Regression on thresholded line pixels |
| OPTICAL_FLOW | §7 | K210 FFT phase correlation — greenfield |
| DETECT_ARUCO | §4.3 | KPU detect + `dristy_aruco_detect` on same frame |
| DETECT_MOTION | §4.3 | Motion mask gates KPU inference |
| CLASSIFY | §4.1 | slot2 kmodel + top-K postprocess |
| DETECT_CUSTOM | §5 | slot3 + SD load |
| FACE_RECOGNISE | §4.1 | detect → embed → match |

## Evidence

After flash, record sha256 and `docs/evidence/dristy-sen0305-smoke.json` from `dristy_hw_verify.py` + `dristy_mode_smoke.py`.

# Full mode audit (host SET_MODE + LCD, 2026-09-19)

`mode_ok` is protocol SET/GET. Counts are live `GET_RESULT`. Scene was a desk (no printed tags/QR/faces required).

Flashed after this table: SHA `ba99adb6…` (FPS gated on camera sequence). Rows with ~948 FPS were empty pipeline commits; that is gated in the flashed image.

| Mode | mode_ok | fps | n_det | blobs | flow | line | motion% | Notes |
|------|---------|-----|-------|-------|------|------|---------|-------|
| DETECT | True | 6.8 | 0 | 0 | 0 | False | 0.0 | VOC20; 0 boxes if scene empty |
| DETECT_CUSTOM | True | 22.3 | 0 | 0 | 0 | False | 0.0 | No user kmodel on device |
| CLASSIFY | True | 22.3 | 0 | 0 | 0 | False | 0.0 | Still routed through object detector |
| FACE_DETECT | True | 5.0 | 0 | 0 | 0 | False | 0.0 | No face in view |
| FACE_RECOGNISE | True | 24.9 | 0 | 0 | 0 | False | 0.0 | Same as face detect |
| COLOUR_TRACK | True | 18.8 | 0 | 16 | 0 | False | 0.0 | Live blobs |
| LINE_FOLLOW | True | 19.9 | 0 | 0 | 0 | True | 0.0 | Line valid |
| APRILTAG | True | 23.8 | 0 | 0 | 0 | False | 0.0 | No tag in view |
| QR_CODE | True | 14.3 | 0 | 0 | 0 | False | 0.0 | No QR in view |
| OPTICAL_FLOW | True | 19.9 | 0 | 0 | 1 | False | 0.0 | Live flow cell |
| COLOUR_SORT | True | 19.0 | 0 | 16 | 0 | False | 0.0 | Live blobs |
| ARUCO | True | 5.9 | 0 | 0 | 0 | False | 0.0 | No marker in view |
| MOTION_DETECT | True | 16.5 | 1 | 0 | 0 | False | 55.0 | Live motion region |
| DETECT_TRACK | True | 949.1 | 0 | 0 | 0 | False | 15.2 | Ghost FPS; gated in ba99adb6 |
| DETECT_TAG | True | 948.1 | 0 | 0 | 0 | False | 15.2 | Shared camera (no dual DVP) |
| LANDING_TARGET | True | 18.2 | 0 | 0 | 1 | False | 15.2 | Flow live; tags need a marker |
| DETECT_ARUCO | True | 948.4 | 0 | 0 | 0 | False | 15.2 | Ghost FPS; gated in ba99adb6 |
| DETECT_MOTION | True | 948.1 | 0 | 0 | 0 | False | 15.2 | Ghost FPS; gated in ba99adb6 |

Raw JSON: `dristy/docs/evidence/dristy-full-audit.json`

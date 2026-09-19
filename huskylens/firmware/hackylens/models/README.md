# HackyLens AI model lab

This directory contains machine-readable model contracts. The SD-ready assets
live in `sdcard/hackylens.kmodels/`; generated build artifacts stay under
ignored `dist/`.

`tools/ai_model.py` supports five main workflows:

- `inspect` identifies native KModel v3, word-swapped images, nncase
  containers, and the original HUSKYLENS legacy KPU object model;
- `fetch` downloads a spec-pinned HTTPS asset, verifies byte size and SHA-256,
  and packages it;
- `package` validates a local native KModel v3 against a spec;
- `verify` checks the binary and JSON manifests, model CRC/SHA, outputs, and
  labels;
- `convert` invokes the pinned legacy compiler and packages its result.

The binary `manifest.hkai` is exactly 256 bytes. It records the model ID and
CRC, KModel v3 contract, input metadata, output byte sizes, post-processing
type, and label count. `manifest.json` retains richer audit metadata including
SHA-256, upstream provenance, output shapes, and post-processing parameters.
Feature apps own preprocessing/decoding; the shared runtime owns validation,
KPU execution, and lifecycle.

## Included FACE model

`sdcard/hackylens.kmodels/detect.kmodel` is pinned to Kendryte's standalone
`face_detect` example at commit
`e89c35465fadb5524c892d2a1c7a76dc76e219ed`. The expected file is 388,776
bytes with SHA-256
`916e679defa91ad76f9feed18b6b37d26328ec9a2c0c8ab0d1ca5983e105b7c0`.
Its exact URL and the limits of the available upstream provenance are recorded
in `sdcard/hackylens.kmodels/detect.UPSTREAM.txt`.

The similarly named 386,608-byte asset extracted from the original HUSKYLENS
firmware is a different/private flash container and must not be copied into the
SD staging directory.

## Reproduce the VOC20 package

```powershell
python tools\ai_model.py fetch `
  --spec models\object_detect_voc20.json `
  --out-dir sdcard\hackylens.kmodels\object20

python tools\ai_model.py verify sdcard\hackylens.kmodels\object20 `
  --spec models\object_detect_voc20.json
```

The spec pins the official Kendryte nncase rc5 example by URL, commit, size,
and SHA-256. It also records the `10x7` grid, five anchors, VOC20 label order,
and default thresholds used by firmware.

## Conversion

The locked compiler is Kendryte nncase `v0.1.0-rc5`. Its downloadable binary is
Linux x86-64, so run bootstrap/conversion in Linux or WSL:

```text
python tools/ai_model.py bootstrap

python tools/ai_model.py convert \
  --source detector.tflite \
  --format tflite \
  --calibration calibration-images \
  --spec models/spec.example.json \
  --out-dir out/detector
```

The wrapper uses the real flat rc5 CLI and requests legacy `k210model` output.
Rc5 directly supports TFLite and Caffe, not ONNX. Modern nncase versions emit
KModel v4 and are not interchangeable with the current firmware runtime.

## Recovered original assets

`unpacked/object_detect.bin` is a custom 16-layer legacy KPU task container,
not a generic data store and not a native KModel v3. Its exact known profile is
320x240 planar input and a quantized `125x7x10` VOC20 output. It requires the
original private loader/dequantizer or a separately verified conversion.

`unpacked/mobilenetv1_1.0.kmodel` is a 224x224 classifier with 1,000 float
scores; it cannot supply object bounding boxes.

See `docs/AI_MODELS.md` for the complete runtime, SD, and provenance contract.

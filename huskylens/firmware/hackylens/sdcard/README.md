# Dristy SD card

Copy this directory to the **root** of a **FAT32** card (512-byte sectors). The Files app shows **FAT32 ONLY** if the format is wrong.

## Primary layout (Dristy firmware)

```text
/dristy.kmodels/detect.kmodel
/dristy.kmodels/detect.UPSTREAM.txt
/dristy.kmodels/object20/model.kmodel
/dristy.kmodels/object20/manifest.hkai
/dristy.kmodels/object20/manifest.json
/dristy.kmodels/object20/labels.txt
```

`manifest.json` is for audit; firmware validates `manifest.hkai`, model CRC, and the KModel contract.

## Legacy mirror (optional)

Older builds also accept `/hackylens.kmodels/...` with the same files. Run the setup tool to refresh both trees:

```bash
python tools/setup_dristy_sdcard.py
python tools/setup_dristy_sdcard.py --mount /media/your-sd
python tools/setup_dristy_sdcard.py --fetch   # download object20 if missing
```

## Face model pin

Kendryte standalone `face_detect` example:

```text
bytes            388776
SHA-256          916e679defa91ad76f9feed18b6b37d26328ec9a2c0c8ab0d1ca5983e105b7c0
```

See `dristy.kmodels/detect.UPSTREAM.txt` for URL and provenance.

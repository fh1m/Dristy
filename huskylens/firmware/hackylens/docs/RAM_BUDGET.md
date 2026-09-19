# RAM / Flash Budget

The firmware uses statically allocated camera frame buffers, an LCD shadow, SD sectors, PNG inflate buffers, quirc storage, and an optional AprilTag working set.

Current ownership:

- LCD shadow and row buffer: `drivers/lcd_st7789.c`; the 153,600-byte shadow is also the leased RGB565-BE full-frame staging surface, so LCD present does not allocate another framebuffer.
- Two camera stream slots: `services/frame_pool.c`; preview, photo, QR, and debug snapshots lease these slots through `drivers/camera_stream.c` without allocating a third frame. When the camera reservation is inactive, files decoding or the Display batch provider may take the single generation-checked scratch workspace borrow; copied or stale borrow tokens cannot release a later borrower.
- SD/FAT sector buffers: `storage/fat32_sd.c`.
- PNG inflate and image row buffers: `apps/files/image_decode_png_inflate.c` and `apps/files/image_decode_common.c`; the complete allocation disappears with `--disable-app files`.
- GIF decoding uses fixed 12-bit LZW tables, one 255-byte sub-block buffer, the shared image row buffer, and small precomputed viewport maps. Frames are composited directly into the 320x240 LCD shadow; disposal mode 3 temporarily reuses one existing camera frame-pool slot, so no GIF-sized logical canvas or additional framebuffer is allocated.
- Terminal history ring and viewport: `apps/terminal/terminal_buffer.c`; they are omitted when Terminal is disabled.
- APRILTAG uses one fixed uncached 19,200-byte 160x120 luma handoff, two small result banks, a small stabilized display-result copy, a 74-byte selected-ID bitmap, and temporary heap allocations owned exclusively by its core-1 worker. Camera RGB565 slots are never retained while detection runs. A 16-bit union-find halves its main threshold working set because the bounded 19,200-pixel input cannot exceed its 65,535-node limit. Only TAG36H11 is built; image file I/O, profiling images, pthreads, and all other tag families are disabled. The complete detector, selection UI, and runtime buffers are omitted with `--disable-app apriltag`.
- Settings menus keep one small instance state per owner and format only the currently drawn row into a 24-byte stack buffer. Descriptor tables live in read-only flash. The previous CAMERA/QR and system SETTINGS arrays containing formatted copies of every row were removed.
- Feature views and controllers add no duplicate framebuffer. CAMERA/QR/FACE/APRILTAG/OBJECT share the camera frame pool and preview surface; menu icons live in feature views as code/constant data. FILES disposal mode 3 continues to reuse the existing camera scratch slot.
- The shared AI runtime allocates only the active model plus the KPU SDK's
  declared main buffer; a global owner prevents two models from coexisting.
  The optional SD manifest is read through one 256-byte stack buffer.
- FACE and OBJECT share one 230,720-byte aligned planar input including the DVP
  guard row; only one AI app can own it. OBJECT additionally keeps a fixed
  350-box geometry table, the full 350x20 per-class probability workspace, and
  two small result banks. Its SD model is 1,352,588 bytes and declares 43,752
  bytes of KModel main memory. The model allocation exists only while OBJECT
  is loaded; no second copy of its 35,000-byte output tensor is retained.
- The shared core-1 executor adds only control words and a permanent idle loop
  when APRILTAG is enabled. KPU output post-processing remains on core 0.

The settings record grows from the legacy 16-byte payload through the 96-byte
v2 payload and 97-byte v3 payload to a 105-byte v4 payload. V4 contains 88
opaque app bytes plus the existing autostart ID; its aligned 124-byte record
still fits in one flash page. Build guard: `hackylens.bin` must remain below
`0x007FE000`, preserving the two 4 KB settings slots at `0x007FE000` and
`0x007FF000`.

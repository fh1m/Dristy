---
contract-id: hackylens.capability.display
owner: platform-architecture
version: 0.1.0
stability: experimental
---

# Display Capability

## Identity and features

- Numeric ID: `0x00010003`.
- Canonical name: `hackylens.cap.display`.

Feature bits:

| Bit | Name | Meaning |
|---:|---|---|
| `1 << 0` | `HK_DISPLAY_FEATURE_BASE_PLANE` | Native base plane |
| `1 << 1` | `HK_DISPLAY_FEATURE_OVERLAY_PLANE` | Independent overlay plane |
| `1 << 2` | `HK_DISPLAY_FEATURE_BATCH` | Bounded retained batch |
| `1 << 3` | `HK_DISPLAY_FEATURE_DIRTY_REGIONS` | Partial present |
| `1 << 4` | `HK_DISPLAY_FEATURE_RGB565` | RGB565 pixel views |
| `1 << 5` | `HK_DISPLAY_FEATURE_BORROWED_SURFACE` | Writable borrowed surface |
| `1 << 6` | `HK_DISPLAY_FEATURE_TEXT` | Bounded text command |

The initial pixel-view format is `HK_DISPLAY_FORMAT_RGB565_BE`. Colors passed
as scalar `uint16_t` values use the usual `rrrrrggggggbbbbb` bit layout. Pixel
buffers use RGB565 big-endian byte order, independent of host endianness.

## Coordinates, rectangles, and clipping

The logical origin is top-left. X grows right and Y grows down. Rectangles are
half-open: `[x, x + width)` and `[y, y + height)`. Public coordinates are signed
32-bit values; dimensions are unsigned 32-bit values.

All arithmetic MUST be checked before conversion to driver coordinates. An
overflow returns `HK_ERR_INVALID_ARGUMENT`. A valid rectangle wholly outside
the active clip is a successful no-op. Partly visible primitives and pixel
views are clipped to the intersection. `x + width` and `y + height` MUST fit in
the signed 32-bit coordinate domain. A zero-area rectangle is a successful
no-op and consumes no command, text, borrowed-view, or dirty-list capacity.

For `blit`, the pixel buffer describes the original, unclipped destination
rectangle. After destination clipping, the source origin is
`source_x = clipped.x - destination.x` and
`source_y = clipped.y - destination.y`. The first transferred source byte is
`pixels.data + source_y * pixels.stride_bytes + source_x * bytes_per_pixel`.
Clipping MUST preserve the original stride and MUST NOT reinterpret the visible
intersection as starting at the first byte of the buffer.

`set_clip(NULL)` selects the full logical display. A non-null clip replaces the
current clip and is intersected with the display bounds; it is not relative to
the previous clip. The fake command log records post-clip rectangles.

## Info, planes, and ownership

`hk_display_info_t` reports width, height, pixel formats, planes, alignment,
maximum commands, text bytes, dirty rectangles, borrowed surfaces, transfer
slice, and maximum present duration.

Inventory limit key `HK_DISPLAY_LIMIT_WIDTH` (`1`) publishes the logical width
in pixels, and `HK_DISPLAY_LIMIT_HEIGHT` (`2`) publishes the logical height in
pixels. Runtime consumers still validate `hk_display_info_t`; the immutable
limits allow build composition to reject an undersized provider.

`buffer_alignment_bytes` applies to the first byte of a borrowed pixel view;
`row_alignment_bytes` applies to every stride. `transfer_slice_bytes` bounds
one cancellation/deadline-free hardware transfer interval.

`BASE` and `OVERLAY` are separate exclusive resource planes. At most one owner
leases each plane. Base and overlay MAY be leased concurrently when the overlay
feature is present. Version `0.1.0` does not define arbitrary layers, z-order,
alpha blending, or a general compositor.

## Batch operations

The public batch lifecycle is:

1. acquire a display plane;
2. `begin_batch`;
3. add bounded clear, fill-rectangle, rectangle, text, or pixel-blit commands;
4. optionally set a clip or mark additional dirty rectangles;
5. `present` with one absolute deadline and cancellation token;
6. retry the same staged batch after a terminal transfer error/cancel, or
   `abort` it;
7. release the plane.

Primitive parameters and bounded text are copied into declared static batch
storage. Large pixel views are borrowed until successful present, abort, or release.
A cancelled, deadline-exceeded, or failed present retains the borrow because
the same staged batch remains retryable. The caller MUST keep that storage
unchanged and valid across the retry interval.

`begin_batch` is valid only in the idle lease state. Batch and borrowed-surface
staging are mutually exclusive. For a retained command batch, a successful
present returns the lease to idle and advances committed generation; `abort`
returns it to idle without changing logical committed state. Calling
batch/surface creation while already staged, or present/abort while idle,
returns `HK_ERR_INVALID_STATE`.

Command, text, dirty-list, borrowed-view, format, stride, size, and alignment
limits are checked transactionally. On `HK_ERR_LIMIT` or validation failure the
provider MUST NOT mutate staged state or consume any capacity. Text bytes are
opaque bounded UTF-8 storage in version `0.1.0`; rendering invalid byte
sequences is not defined.

Dirty regions are derived from clipped commands and explicit marks. A provider
may merge overlapping regions to remain within its advertised limit, but when
`HK_DISPLAY_FEATURE_DIRTY_REGIONS` is present it MUST NOT silently promote an
incremental batch to full-screen merely for implementation convenience.

## Borrowed full-frame surface

`surface_acquire` returns the implementation-owned, in-place mutable backing
store for the `BASE` plane as a writable `hk_buffer_view_t` with explicit
format, stride, size, and alignment. Writes change backing pixels immediately;
the borrow is not a pixel-level transaction. The caller may access it only
until successful present, abort, or release. A failed present retains the
borrow for retry. The caller MUST mark every modified region; `mark_dirty`
declares backing regions that require panel synchronization, while surface
acquisition alone does not imply full-screen damage. Returned flags include
both `HK_BUFFER_ACCESS_READABLE` and `HK_BUFFER_ACCESS_WRITABLE`.

A successful surface present synchronizes the declared regions, advances the
committed generation, and ends the borrow. `abort` ends the borrow and cancels
its pending presentation, but does not roll back backing pixel mutations.
Those mutations remain visible through a later borrow. If no transfer made
physical progress, abort creates no repair work and the panel may remain older
than those cancelled backing changes until a caller marks them dirty again.
Retained command batches keep their transactional staged/committed semantics;
their abort still discards commands without changing the committed backing.

The provider MUST NOT allocate a second full framebuffer implicitly. Additional
full-frame storage is a separate advertised resource and is absent from the
initial K210 provider.

## Present, cancellation, and repair

One caller deadline applies to every dirty region, row, transport chunk, and
retry attempt inside that invocation. The provider checks cancellation between
bounded transfer slices. Work whose deterministic bound exceeds
`maximum_present_duration_us` returns `HK_ERR_LIMIT` before transfer. An
already-expired finite deadline returns `HK_ERR_DEADLINE_EXCEEDED` before the
first transfer; `HK_DEADLINE_IMMEDIATE` does not invent a replacement deadline.

A successful present atomically advances the committed generation. Physical
panels may expose partial progress while a transfer is running. If present is
cancelled or fails after physical progress:

- the staged batch remains available for retry or abort;
- for a retained command batch, the previous logical committed state remains
  identified;
- for an in-place borrowed surface, the current backing store remains
  authoritative and is not rolled back;
- the provider records the affected damage as a bounded `needs_repair` region
  list;
- no late panel writes occur after the terminal result;
- the next successful present or bounded release cleanup repairs the affected
  regions before claiming a consistent committed display.

Failure before physical progress leaves `needs_repair` unchanged. Failure after
any physical progress records the complete affected staged dirty-region list,
not merely the last completed slice. Repair retains disjoint regions and may
merge only overlapping regions under the same rules as ordinary dirty damage;
it MUST NOT replace disjoint damage with one bounding rectangle. For a retained
command batch, retry repairs previous committed content first, then replays the
unchanged staged operation. For an in-place surface, retry or release cleanup
makes the physical panel converge to the current backing store over the
complete affected region list. Only complete success advances logical committed
generation.

Overlay release discards its staged/committed overlay and restores the current
base content. Base release repairs any outstanding physical damage before
dropping ownership. Cleanup uses the caller's original release deadline and is
not cancellable. An already-expired cleanup deadline causes no hardware effect
and preserves an ordinary lease for bounded retry. Cleanup failure after
physical progress follows the common lease invalidation and provider quarantine
rules.

## Required resources and consumers

Inventory presence requires a selected display device, supported driver,
complete routing/preparation, and a provider that can report its dimensions and
formats. Apps MUST use runtime info or explicit capability limits rather than
infer 320x240 from a board ID.

Native views use `BASE`. MicroPython display operations use `OVERLAY`. Camera
and files use a borrowed full-frame surface where appropriate. Pong uses
bounded primitive batches and dirty regions.

## Fake and acceptance

The fake logs plane ownership, commands, clips, dirty regions, transferred
bytes, slices, deadlines, cancellation, committed state, and repair state. The
same contract suite runs against the K210 adapter with stubbed panel transport.

Host acceptance covers native screens, full-frame camera/files, Pong dirty
frames, MicroPython overlay, cancellation/retry, cleanup, restoration, and the
advertised 500 ms operation bound. Stubbed transport timing validates the
deadline state machine only; it is not physical latency evidence. Real SEN0305
full-present `<= 500 ms` and matched-workload `<= 10%` regression qualification
is recorded in Phase 2.13 and is not claimed by Phase 2.8.

## References

- [Capability API](../CAPABILITY_API.md)
- [MicroPython API](../../MICROPYTHON_API.md)

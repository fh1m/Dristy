---
contract-id: hackylens.capability.external-link
owner: platform-architecture
version: 0.1.0
stability: experimental
---

# External Link Capability

## Identity and features

- Numeric ID: `0x00010004`.
- Canonical name: `hackylens.cap.external-link`.

Feature bits:

| Bit | Name | Meaning |
|---:|---|---|
| `1 << 0` | `HK_EXTERNAL_LINK_FEATURE_UART` | Raw UART mode |
| `1 << 1` | `HK_EXTERNAL_LINK_FEATURE_I2C_CONTROLLER` | 7-bit I2C controller |
| `1 << 2` | `HK_EXTERNAL_LINK_FEATURE_I2C_TARGET` | I2C target mode |

The public ABI is
`firmware/include/hackylens/capability/external_link.h`. All extensible input
structures use version `1`, require zero reserved input fields, and follow the
common size/version rules in the Capability API.

## Acquisition, connector ownership, and routing

`hk_external_link_acquire` takes `mode_features`, the complete set of modes the
lease intends to use. Those bits are added to `request.required_features`
during capability negotiation. Trying to configure a mode outside that
negotiated set returns `HK_ERR_NOT_DECLARED`; a negotiated feature unsupported
by the selected provider returns `HK_ERR_FEATURE_UNAVAILABLE`.

Release follows the common lifecycle exactly: a null handle pointer is
`HK_ERR_INVALID_ARGUMENT`, an all-zero typed handle is idempotent `HK_OK`, a
partially-zero or otherwise malformed non-zero handle is
`HK_ERR_STALE_HANDLE`, and a valid non-zero handle of another capability type
is `HK_ERR_INVALID_ARGUMENT`. The first successful release zeros the caller's
handle; a copied non-zero handle is stale afterward.

One capability instance represents one physical connector. UART and I2C modes
that share pins, muxes, or peripherals MUST NOT be advertised as independently
ownable capabilities. There is one exclusive connector lease, even when the
provider supports all three modes.

Supported modes and routes are generated from the selected descriptor,
platform mapping, driver support, and provider implementation. Board ID,
connector label, or HMPY metadata MUST NOT be used to guess a mode.

Changing mode while an operation is active returns `HK_ERR_BUSY`. Mode cleanup
MUST quiesce and reset the previous peripheral before applying the new route.
Validation completes before either cleanup or routing side effects. Public
mode values are `UNCONFIGURED`, `UART`, `I2C_CONTROLLER`, and `I2C_TARGET`;
private route IDs and peripheral instances are not public ABI.

## Configuration

UART version `0.1.0` is 8 data bits, no parity, one stop bit. Its baud must be
within the provider-reported inclusive range. I2C controller frequency must be
within its provider-reported inclusive range, and transfers accept 7-bit
addresses only. I2C target configuration also accepts only a 7-bit address.
The info structure reports fixed upper bounds for controller write/read and
target receive/response buffers.

An undersized input structure, malformed configuration, out-of-range
baud/frequency/address, non-zero reserved fields, and unknown feature bits
return `HK_ERR_INVALID_ARGUMENT` before routing or peripheral side effects. A
sufficient input size with an incompatible structure version returns
`HK_ERR_VERSION_INCOMPATIBLE`. A structurally valid mode absent from the
provider returns `HK_ERR_FEATURE_UNAVAILABLE`.

## Operation token and state machine

Potentially blocking transfers use a generation-checked
`hk_external_link_op_t`. The all-zero value is no operation. A successful
begin returns `HK_PENDING` and a non-zero token. `poll` and `cancel` validate
the owner, connector lease, and token generation before observing hardware.
There is one in-flight operation per lease across both operation kinds:

- UART write begin;
- I2C controller transfer begin, including combined write/read.

The normative states are:

| State | Allowed action | Result |
|---|---|---|
| no operation | begin | `HK_PENDING` with a fresh generation |
| in flight | another begin or mode change | `HK_ERR_BUSY`, no side effects |
| in flight | poll | at most 32 bytes of total TX/RX progress, then `HK_PENDING` or terminal |
| in flight | cancel | quiesce/reset, then terminal `HK_ERR_CANCELLED` |
| terminal latched | poll or cancel | the same terminal result and progress |
| terminal observed | next begin | retires the old generation and starts a fresh operation |

A copied old token returns `HK_ERR_STALE_HANDLE` after the next successful
begin. Progress reports accepted TX bytes and the completed RX prefix. A
provider MAY report a smaller `maximum_poll_bytes`, but version 0.1 MUST process
at most 32 bytes across all phases in one `poll` call.

Argument/owner/token errors do not alter the operation. Terminal results are
latched. Release and trusted owner cleanup quiesce any active operation,
invalidate the token and lease, and return borrowed buffers only after hardware
is safe.

## UART semantics

UART write progress counts bytes accepted by the provider. Acceptance of the
last byte is not success: terminal `HK_OK` requires an empty FIFO and an idle
shift register. While those conditions are pending, progress includes
`HK_EXTERNAL_LINK_PROGRESS_UART_DRAINING` and the operation remains borrowed.

`hk_external_link_uart_read` is synchronous and non-blocking. It copies no more
than the provider poll bound and may return `HK_OK` with zero bytes.
The K210 provider uses the UART RX interrupt as the single producer for
fixed-capacity storage while main-loop reads are its single consumer. This
keeps full-duplex or loopback traffic from depending on the main-loop poll
interval. If that bounded storage is exceeded, the next read returns
`HK_ERR_OVERFLOW`, resets the UART receive path, and publishes zero bytes; the
caller then continues from an explicit resynchronized state. Mode changes,
failed/cancelled writes, release, and owner cleanup stop the RX interrupt and
clear its staged data before the storage can be reused by I2C target mode.

## I2C controller and target semantics

An I2C controller operation is one transaction consisting of its optional
write phase, optional repeated start, and optional read phase. At least one
phase must be non-empty. The 32-byte poll bound applies to the sum of write and
read progress. NACK maps to `HK_ERR_IO`; NACK, timeout, and cancellation stop
and reset the controller before returning terminal.

The K210 controller provider keeps that single-transaction guarantee across
the controller's eight-entry command FIFO with a fixed-state I2C0 interrupt.
The interrupt refills the command FIFO before it becomes empty and drains the
receive FIFO into the still-borrowed caller buffer; it allocates no storage and
publishes no more than the normal 32-byte TX/RX progress budget from a main-loop
poll. Stop, cancellation, deadline failure, mode change, and cleanup mask and
unregister the interrupt before the borrowed buffers can be returned.

I2C target uses a preload model; it does not wait for application code after a
master READ has begun. Polling is synchronous and non-blocking and returns
`HK_PENDING` when no completed event is queued. A WRITE event is consumed only
when the caller's writable buffer can hold the complete bounded payload.

`hk_external_link_i2c_target_preload_response` synchronously copies a bounded
readable buffer and arms it as the one-shot response for the next master READ
that has not begun. A later preload atomically replaces an unread preload; a
zero-length preload clears it. A READ snapshots the then-current preload. It
serves `min(preload_size, requested_bytes)` bytes, pads a longer request with
`0x00` (`HK_EXTERNAL_LINK_TARGET_FILL_BYTE`), and discards an unused preload
tail when the transaction ends. A READ with no preload returns only `0x00`.
The preload is consumed by exactly one READ.

A READ event is queued after that master transaction completes and reports its
actual `requested_bytes`; it is notification, not a pending request that
`preload_response` can complete. Preloading after a WRITE is therefore the
normal request/next-read flow. If a preload call races with a READ already in
progress, the active READ retains its snapshot and the new preload belongs to
the following READ. Mode reconfiguration, mode change, release, and owner
cleanup discard unread preload and queued target events.

The K210 handoff has two bounded completed-event slots backed by two reusable
payload buffers; READ notifications do not reserve a payload buffer. ISR and
main-loop ownership changes occur under the target IRQ lock. If another
completed transaction or required payload snapshot exceeds that fixed
capacity, the provider latches data loss. The next target poll returns
`HK_ERR_OVERFLOW` with a `NONE` event and atomically discards the entire queued
event window; a write already being discarded remains ignored through its
STOP. The next transaction completed after that boundary is the first
observable event. This is the explicit resynchronization state: no prefix or
queued event from before the overflow is replayed. Mode change, release, and
owner cleanup clear both the queued window and the overflow latch. An active
READ keeps its one-shot preload snapshot, and a racing preload still arms the
following READ exactly as above.

## Deadline and buffer ownership

The original absolute deadline supplied at operation begin is stored with the
operation and applies to the whole payload/transaction. It MUST NOT be reset
for a 256-byte language-adapter chunk, FIFO burst, I2C phase, repeated start, or
poll. A finite deadline already expired at begin fails before hardware side
effects. `HK_DEADLINE_IMMEDIATE` permits only progress available without
waiting; if that attempt is not terminal, the operation terminates with
`HK_ERR_DEADLINE_EXCEEDED`. `UINT64_MAX` is invalid.

Validation and terminal precedence follow the common contract: arguments and
ownership, an already-latched terminal result, cancellation, deadline, then
progress/I/O. Thus cancellation wins over deadline expiry when both are first
observed in one poll.

TX buffers are borrowed read-only until terminal completion. RX buffers are
borrowed writable and cannot be read before terminal completion except for the
completed RX prefix reported by `rx_completed_bytes`; that prefix is stable and
readable. The cancel view is also borrowed through terminal. After terminal
cancellation, timeout, release, or owner cleanup, the provider MUST NOT perform
late writes to UART, I2C, or the caller's RX buffer.

## Phase 2.10 implementation boundary

The normal external-link protocol service is a native capability consumer. A
MicroPython run that needs raw connector access asks product policy to pause the
normal service; that service voluntarily releases its lease. After MicroPython
owner cleanup, the normal service reacquires and restores its configured HMPY
mode.

The capability core does not preempt owners or encode this product policy. The
MicroPython cross-core bridge contains transport/ticket/cancel logic only and
calls the same provider as the native service. Those K210 provider and consumer
migrations belong to Phase 2.10; Phase 2.9 does not change production external
service, HAL, routing, or MicroPython runtime behavior.

## Fake and acceptance

The fixed-capacity fake records routing modes, FIFO progress, bytes, I2C phases,
buffer lifetime, original deadline, resets, cancellation, terminal results,
preload replacement, read consumption, and zero fill. Tests cover exclusive conflicts, unsupported
features/modes, active mode switching, UART partial progress/drain and
non-blocking read, combined I2C transfer, NACK/timeout, cancellation before and
during an operation, stale generations, target buffers, release cleanup, and no
late effects. Late-effect absence is proven through unchanged observable byte
counters and terminal state across repeated poll/cancel and stale-handle calls;
there is no write-only metric that can remain trivially zero. The fake allocates
no heap, task, queue, or unbounded event storage.

SEN0305 physical acceptance requires UART TX/RX loopback and a known 7-bit I2C
target, plus restoration of the normal external/HMPY service. Host tests alone
cannot close the electrical gate; that later physical gate is not Phase 2.9
completion evidence.

## References

- [Capability API](../CAPABILITY_API.md)
- [External Link Protocol](../../EXTERNAL_LINK_PROTOCOL.md)
- [MicroPython API](../../MICROPYTHON_API.md)
- [Board Port Contract](../BOARD_PORT.md)

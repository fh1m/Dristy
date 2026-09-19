---
contract-id: hackylens.hmpy
owner: device-protocols
version: 1.1.0
stability: experimental
wire-major: 1
---

# HackyLens MicroPython Protocol (HMPY) v1

HMPY is the binary protocol between the HackyLens firmware and a desktop IDE.
It shares the USB/UART3 byte stream with the existing text debug console but is
never active at the same time as that console. The framing and codec described
here are independent of the filesystem and MicroPython runtime.

## Entering and leaving framed mode

After opening the serial port, the host starts in the existing line mode and
sends this complete ASCII line:

```text
HKMPROTO 1\n
```

The line parser may also accept `\r\n`. A v1-capable device responds with:

```text
HKMPROTO 1 READY\n
```

The byte after that response newline is the beginning of exclusive HMPY framed
mode. From that point, firmware diagnostics, Python stdout/stderr, responses,
and events must all pass through the framed transport; an unframed `printf()`
would corrupt the stream. Existing `HK...` commands retain their current
meaning while the connection is in line mode.

The host leaves framed mode with a `SESSION_CLOSE` request. The device sends
the framed response first and returns to line mode only after the complete
response delimiter has been transmitted. A reboot or expired session lease
also returns to line mode. Hosts should send `PING` every 2--3 seconds; the
session layer may expire an idle lease after 10 seconds. Lease expiry is a
transport ownership decision and does not by itself promise to stop a running
program.

## Wire frame

One raw frame is COBS-encoded and followed by exactly one `0x00` delimiter.
The encoded packet cannot contain zero, so a receiver can discard damaged or
oversized data through the next delimiter and then resume parsing. Empty
packets (repeated delimiters) are ignored.

The decoded frame is:

| Offset | Size | Field |
|---:|---:|---|
| 0 | 4 | magic ASCII `HMPY` |
| 4 | 1 | protocol version, `1` |
| 5 | 1 | message type |
| 6 | 2 | flags |
| 8 | 4 | request ID |
| 12 | 4 | payload length `N` |
| 16 | N | opaque message payload |
| 16+N | 4 | CRC-32 over bytes 0 through 15+N |

All multibyte integers are little-endian. CRC-32 is the standard reflected
CRC-32/ISO-HDLC used by Ethernet, ZIP, and Python `binascii.crc32`: reflected
polynomial `0xEDB88320`, initial value `0xFFFFFFFF`, and final XOR
`0xFFFFFFFF`.

v1 limits are fixed:

| Limit | Bytes |
|---|---:|
| payload | 1024 |
| decoded frame | 1044 |
| COBS packet, without delimiter | 1049 |
| complete wire frame | 1050 |

A decoder rejects a bad magic, unsupported version, unknown type, reserved
flag, invalid request/event envelope, payload length over 1024, length
mismatch, malformed COBS, and CRC mismatch. Length arithmetic must be checked
before indexing or allocation. Firmware uses fixed buffers and enters a
discard-until-delimiter state as soon as an encoded packet exceeds 1049 bytes.

## Request IDs and flags

Requests and their responses use the same message type. A host chooses a
nonzero `request_id`; the response copies it. Asynchronous events always use
`request_id=0` and `flags=0`.

| Bit | Name | Meaning |
|---:|---|---|
| 0 | `RESPONSE` | frame is a response to the matching request ID |
| 1 | `ERROR` | response payload contains the error envelope below; requires `RESPONSE` |
| 2 | `MORE` | another response chunk for this request follows; requires `RESPONSE` |
| 3--15 | reserved | must be zero in v1 |

Requests therefore have `flags=0`. Event frames cannot carry any flags.

## Message types

Values are stable for protocol v1. `0x05--0x0f`, gaps between the listed
ranges, and `0x85--0xff` are reserved and rejected by the v1 codec.

| Value | Name | Direction and purpose |
|---:|---|---|
| `0x01` | `HELLO` | negotiate capabilities, limits, firmware/board and userfs profile |
| `0x02` | `LIST` | enumerate stored program files |
| `0x03` | `STAT` | retrieve metadata for one file |
| `0x04` | `READ` | read file bytes, with `MORE` for additional chunks |
| `0x10` | `UPLOAD_BEGIN` | begin a size/CRC-bound atomic upload |
| `0x11` | `UPLOAD_CHUNK` | transfer a stop-and-wait upload chunk at an offset |
| `0x12` | `UPLOAD_COMMIT` | validate and atomically publish the upload |
| `0x13` | `UPLOAD_ABORT` | discard a pending upload |
| `0x20` | `DELETE` | delete one stored file |
| `0x21` | `SET_STARTUP` | select or clear the startup program |
| `0x22` | `FORMAT` | destructive userfs format with a separate confirmation token |
| `0x30` | `RUN` | start one stored program |
| `0x31` | `STOP` | request a bounded safe stop |
| `0x32` | `STATUS` | return VM/run state and resource counters |
| `0x40` | `PING` | heartbeat; response echoes the request payload |
| `0x41` | `SESSION_CLOSE` | acknowledge and return UART3 to line mode |
| `0x80` | `STDOUT` | async stdout bytes, including run ID and sequence at the message layer |
| `0x81` | `STDERR` | async stderr bytes, including run ID and sequence at the message layer |
| `0x82` | `STATE` | async VM/run state transition |
| `0x83` | `FILE_CHANGED` | async stored-file change notification |
| `0x84` | `DROPPED` | async count/range of log bytes lost from the bounded ring |

The codec deliberately treats payloads as bytes. The session layer owns their
message-specific schemas and validation. Binary schemas use little-endian
fixed-width integers and length-prefixed UTF-8 strings; JSON is not used on the
wire.

## Payload vocabulary

The notation below is used by every message schema:

| Notation | Encoding |
|---|---|
| `u8`, `u16`, `u32`, `u64` | unsigned little-endian integer of that width |
| `name` | `u8 byte_length` followed by that many UTF-8 bytes |
| `bytes...` | all remaining payload bytes |

Names are at most 63 encoded bytes. The current userfs accepts a single flat
ASCII file name made from letters, digits, `-`, `_`, and `.`, with no leading
`.`; `/name` is normalized to `name` by the filesystem API but hosts should
send the normalized form. Program files are at most 256 KiB. Empty `name` is
valid only where explicitly documented. Files larger than the advertised
`runnable source maximum` may be stored/read but cannot be passed to the VM.

## Request and response payloads

Unless a response schema says otherwise, a successful response has an empty
payload. An unsuccessful request uses the error envelope described below.

### Discovery and files

`HELLO` has an empty request. Its response is:

| Field | Type | Meaning |
|---|---|---|
| protocol version | `u8` | `1` |
| filesystem state | `u8` | userfs state table below |
| runtime state | `u8` | runtime state table below |
| boot flags | `u8` | boot/recovery conditions; unknown bits must be ignored |
| capabilities | `u32` | capability bit mask below |
| maximum payload | `u16` | 1024 in v1 |
| upload chunk maximum | `u16` | 1016 in v1 |
| name maximum | `u16` | 63 in v1 |
| runnable source maximum | `u16` | 65535 bytes in v1 |
| file maximum | `u32` | 262144 in v1 |
| filesystem total | `u32` | usable userfs bytes |
| filesystem used | `u32` | allocated userfs bytes |
| firmware version | `name` | build version string |
| board | `name` | board identifier |

In HMPY contract `1.1.0`, `board` is the exact canonical `id` from the
selected `boards/<id>/board.toml` descriptor (for the qualified runtime port,
`huskylens-sen0305`). The wire-major remains `1`; the field encoding is
unchanged. Clients MUST NOT infer capabilities, wiring, or peripheral support
from this identifier. They use the capabilities and limits explicitly carried
by HMPY instead.

Boot flags are backward-compatible with early v1 clients, which treated this
byte as reserved and ignored it:

| Bit | Name | Meaning |
|---:|---|---|
| 0 | `WDT1_RECOVERY` | this boot followed the MicroPython fatal-stop WDT1 fallback; firmware autostart of the MicroPython app is suppressed for this boot |
| 1--7 | reserved | hosts must ignore unknown bits |

Capability bits are stable for v1:

| Bit | Capability |
|---:|---|
| 0 | file list/stat/read/delete |
| 1 | CRC-bound atomic upload |
| 2 | startup program selection |
| 3 | run and bounded stop |
| 4 | stdout events |
| 5 | stderr/diagnostic events |
| 6 | explicitly confirmed format |
| 7 | HackyLens bindings v1 |
| 8 | boot flags are defined in the formerly reserved HELLO byte |

`LIST` has an empty request. Each entry is a response with `MORE` and payload
`name, u32 size`; a final empty response without `MORE` terminates the stream,
including an empty directory.

`STAT` request is `name`. Response is `u32 size, u8 is_startup`.

`READ` request is `name, u32 offset, u32 length`. A zero `length` means through
end of file. Each response is `u32 chunk_offset, u32 full_file_size, bytes...`.
Data chunks are at most 512 bytes and all but the final response carry `MORE`.

### Atomic upload

`UPLOAD_BEGIN` request is `name, u32 final_size, u32 final_crc32`. Response is
`u32 upload_id, u32 next_offset`; a new upload starts at offset zero.

`UPLOAD_CHUNK` request is `u32 upload_id, u32 offset, bytes...`, with at most
1016 data bytes. Response is `u32 upload_id, u32 next_offset`. Chunks are
stop-and-wait and contiguous. Repeating the immediately previous identical
chunk is safe and returns the same acknowledgement.

`UPLOAD_COMMIT` and `UPLOAD_ABORT` requests are `u32 upload_id`. Commit closes
and syncs the temporary file, verifies exact size and CRC-32, then atomically
renames it over the target. Abort removes the temporary file.

### Mutation and execution

`DELETE` request is `name`.

`SET_STARTUP` request is `name`. An empty name clears startup selection;
otherwise the named file must already exist.

`FORMAT` request is the exact ASCII bytes `ERASE USERFS`. No alternative or
case-insensitive token is accepted.

`RUN` request is `name, u32 time_limit_ms`. An empty name selects the startup
program. A zero limit selects the firmware default (30 seconds); accepted
nonzero limits are at most 300000 ms. Response is `u32 run_id`.

`STOP` and `STATUS` have empty requests. `STOP` acknowledges the stop request;
completion is reported by `STATE` and can also be observed with `STATUS`.
The 52-byte `STATUS` response is:

| Offset | Type | Field |
|---:|---|---|
| 0 | `u8` | runtime state |
| 1 | `u8` | runtime exit reason |
| 2 | `u8` | filesystem state |
| 3 | `u8` | filesystem last error |
| 4 | `u32` | run ID |
| 8 | `u32` | source bytes |
| 12 | `u32` | output bytes pending |
| 16 | `u32` | output bytes dropped |
| 20 | `u64` | start timestamp, microseconds |
| 28 | `u64` | last VM heartbeat, microseconds |
| 36 | `u64` | deadline, microseconds |
| 44 | `u32` | filesystem total bytes |
| 48 | `u32` | filesystem used bytes |

`PING` accepts any payload up to the protocol limit and echoes it exactly.
`SESSION_CLOSE` has an empty request and response.

## Event payloads

| Event | Payload |
|---|---|
| `STDOUT` | `u32 run_id, u32 byte_sequence, bytes...` |
| `STDERR` | `u32 run_id, u32 byte_sequence, bytes...` |
| `STATE` | `u8 state, u8 exit_reason, u32 run_id, u64 heartbeat_us` |
| `FILE_CHANGED` | `u8 operation, name` |
| `DROPPED` | `u8 stream, u32 run_id, u32 byte_count` |

Output events carry at most 504 data bytes. `byte_sequence` is the absolute
stream byte offset, so a host can detect a gap across reconnects or delayed
delivery. Python stdout starts at offset zero for each run. Firmware STDERR is
a boot-global diagnostics stream; the first event observed by a newly opened
session establishes its non-zero absolute baseline, and subsequent offsets
remain gap-checkable. `DROPPED.stream` is 1 for Python stdout and 2 for firmware
diagnostics/stderr. File operations are 1=published upload, 2=deleted,
3=startup selection changed, and 4=userfs formatted.

Runtime states are 0=stopped, 1=starting, 2=running, 3=stopping,
4=finished, and 5=error. Exit reasons are 0=none, 1=complete,
2=requested, 3=timeout, 4=uncaught exception, 5=busy, 6=invalid source,
and 7=core1 failure. Filesystem states are 0=uninitialized,
1=unsupported flash, 2=unformatted, 3=corrupt, 4=I/O error, and 5=mounted.

## Error response envelope

An `ERROR|RESPONSE` payload begins with:

| Offset | Size | Field |
|---:|---:|---|
| 0 | 2 | error code |
| 2 | 2 | UTF-8 detail length `D` |
| 4 | D | optional human-readable detail, not for programmatic decisions |

Stable v1 error codes are:

| Value | Name | Value | Name |
|---:|---|---:|---|
| 0 | `OK` | 10 | `IO` |
| 1 | `INVALID_REQUEST` | 11 | `CRC_MISMATCH` |
| 2 | `UNSUPPORTED_VERSION` | 12 | `OFFSET_MISMATCH` |
| 3 | `UNSUPPORTED_TYPE` | 13 | `LIMIT_EXCEEDED` |
| 4 | `INVALID_PAYLOAD` | 14 | `NOT_RUNNING` |
| 5 | `NOT_FOUND` | 15 | `TIMEOUT` |
| 6 | `ALREADY_EXISTS` | 16 | `INTERNAL` |
| 7 | `BUSY` | 17 | `CONFIRMATION_REQUIRED` |
| 8 | `PERMISSION_DENIED` | 18 | `SESSION_EXPIRED` |
| 9 | `NO_SPACE` | | |

## Upload and event invariants

The message layer must bind every upload to an `upload_id`, expected final
size, and expected CRC-32. Chunks include their absolute offset. A duplicate of
the last identical chunk is idempotent; gaps, overlaps, or a duplicate with
different bytes return `OFFSET_MISMATCH` or `INVALID_PAYLOAD`. Commit succeeds
only after size and CRC validation and an atomic filesystem publish.

stdout/stderr events include a `run_id` and monotonically increasing byte or
chunk sequence. They are backed by a bounded ring. Lost output is reported by
`DROPPED`; it must not disappear silently. These guarantees are session/runtime
responsibilities, not codec state.

## Reference implementations and golden vectors

- Firmware codec: `firmware/src/services/hmpy_codec.h` and `.c`. It has no heap
  allocation; its streaming decoder owns fixed encoded and decoded buffers.
- Host codec: `tools/hmpy_protocol.py`. `encode_frame()` includes the delimiter;
  `decode_packet()` does not. `StreamDecoder.feed()` returns frame/error events
  and resynchronizes after malformed input.
- Cross-language examples: `tests/fixtures/hmpy_golden.json`, verified by
  `tests/test_hmpy_protocol.py` against both implementations.

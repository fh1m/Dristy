---
contract-id: hackylens.capability-api
owner: platform-architecture
version: 0.1.0
stability: experimental
---

# HackyLens Capability API

## Purpose and scope

This contract defines the common native ABI and lifecycle for portable
HackyLens hardware capabilities. Native firmware and language adapters MUST use
the same capability providers, ownership rules, error model, deadlines,
cancellation, cleanup, and memory ownership.

This contract does not define the Phase 3 App Runtime, App SDK, public app
manifest, project format, dynamic plugins, Program Manager, or MicroPython API
v2. Static native-app owner binding and build composition remain private
firmware mechanisms.

The initial capability contracts are:

- [Time](capabilities/TIME.md);
- [Input](capabilities/INPUT.md);
- [Display](capabilities/DISPLAY.md);
- [External Link](capabilities/EXTERNAL_LINK.md);
- [Lights](capabilities/LIGHTS.md).

## Canonical identity

`hk_capability_id_t` is a stable 32-bit identifier. Zero is invalid. Assigned
identifiers MUST NOT be reused after publication.

| Numeric ID | Canonical name | Contract |
|---:|---|---|
| `0x00010001` | `hackylens.cap.time` | `hackylens.capability.time` |
| `0x00010002` | `hackylens.cap.input` | `hackylens.capability.input` |
| `0x00010003` | `hackylens.cap.display` | `hackylens.capability.display` |
| `0x00010004` | `hackylens.cap.external-link` | `hackylens.capability.external-link` |
| `0x00010005` | `hackylens.cap.lights` | `hackylens.capability.lights` |

The numeric ID is the firmware lookup key. The canonical name is the
machine-readable build/evidence name. Board device kinds, descriptor IDs,
peripheral numbers, pins, routes, and HMPY `HELLO.board` are not capability
identities.

## Common C types

The public headers MUST define the following ABI shapes. Public extensible
request, info, option, and configuration structures MUST start with
`struct_size` and `struct_version`. Reserved fields MUST be zero on input and
ignored on output.

For extensible input structures, `struct_size` smaller than the implemented
minimum returns `HK_ERR_INVALID_ARGUMENT`. A sufficient size with an
unsupported `struct_version` returns `HK_ERR_VERSION_INCOMPATIBLE`. Providers
MUST apply size validation before version validation.

```c
typedef int32_t hk_result_t;

enum {
    HK_OK = 0,
    HK_PENDING = 1,
    HK_ERR_INVALID_ARGUMENT = -1,
    HK_ERR_CAPABILITY_ABSENT = -2,
    HK_ERR_VERSION_INCOMPATIBLE = -3,
    HK_ERR_FEATURE_UNAVAILABLE = -4,
    HK_ERR_NOT_DECLARED = -5,
    HK_ERR_BUSY = -6,
    HK_ERR_WRONG_OWNER = -7,
    HK_ERR_STALE_HANDLE = -8,
    HK_ERR_WRONG_CONTEXT = -9,
    HK_ERR_INVALID_STATE = -10,
    HK_ERR_DEADLINE_EXCEEDED = -11,
    HK_ERR_CANCELLED = -12,
    HK_ERR_OVERFLOW = -13,
    HK_ERR_LIMIT = -14,
    HK_ERR_IO = -15,
    HK_ERR_INTERNAL = -16
};

typedef uint32_t hk_capability_id_t;

typedef struct {
    uint16_t major;
    uint16_t minor;
    uint16_t patch;
    uint16_t reserved;
} hk_version_t;

typedef struct {
    uint64_t at_us;
} hk_deadline_t;

typedef uint8_t (*hk_cancel_probe_fn)(const void *context);

typedef struct {
    hk_cancel_probe_fn probe;
    const void *context;
} hk_cancel_t;

typedef struct {
    uint32_t slot;
    uint32_t generation;
} hk_owner_t;

typedef struct {
    uint32_t slot;
    uint32_t generation;
    hk_owner_t owner;
    hk_capability_id_t capability_id;
} hk_lease_t;

typedef struct {
    void *data;
    uint32_t size_bytes;
    uint32_t stride_bytes;
    uint32_t flags;
} hk_buffer_view_t;

typedef struct {
    uint16_t struct_size;
    uint16_t struct_version;
    hk_capability_id_t id;
    hk_version_t minimum;
    hk_version_t maximum_exclusive;
    uint64_t required_features;
    uint16_t instance;
    uint16_t reserved;
} hk_capability_request_t;
```

Typed handles MUST contain an `hk_lease_t` and MUST NOT expose provider vtables,
driver objects, HAL objects, peripheral instances, or board routes.

## Result semantics

- `HK_OK` is terminal success.
- `HK_PENDING` is non-terminal progress for explicitly asynchronous operations.
- `HK_ERR_CAPABILITY_ABSENT` means no inventory entry matches the requested ID
  and instance.
- `HK_ERR_VERSION_INCOMPATIBLE` means the ID exists but no version lies inside
  the requested compatibility range.
- `HK_ERR_FEATURE_UNAVAILABLE` means identity and version match but required
  feature bits do not.
- `HK_ERR_NOT_DECLARED` means the selected build did not grant the owner access
  to that capability.
- `HK_ERR_BUSY` means a valid but conflicting exclusive lease or operation owns
  the resource.
- `HK_ERR_WRONG_OWNER`, `HK_ERR_STALE_HANDLE`, and
  `HK_ERR_WRONG_CONTEXT` MUST be detected before hardware access.
- `HK_ERR_OVERFLOW` reports bounded data loss and MUST be accompanied by the
  capability-specific resynchronization state.
- `HK_ERR_INTERNAL` is reserved for a violated provider invariant or failure to
  reach a safe, quiescent state. It MUST NOT replace a more specific result.

Unknown positive or negative results MUST NOT be treated as success.

## Versions and feature negotiation

A request specifies an inclusive `minimum`, an exclusive
`maximum_exclusive`, and required feature bits. A provider matches only when:

1. identity and instance match;
2. `minimum <= provider_version < maximum_exclusive`;
3. every required feature bit is present.

Because experimental contracts may make breaking changes on MINOR versions,
clients compiled for capability `0.1.x` MUST request `[0.1.0, 0.2.0)`, not
"minor greater than or equal to one". A future stable `1.x` client may request
`[1.m.0, 2.0.0)` when the relevant capability contract permits it.

Feature bits are capability-specific. A feature may be added compatibly only
inside the compatibility rules of that capability contract. Unknown feature
bits MUST be preserved in discovery output and MUST fail a request when they
are required but unavailable.

## Immutable inventory and discovery

The selected board/build composition MUST generate one immutable inventory.
The public discovery function returns a read-only pointer and count whose
lifetime is the entire boot. Entries MUST be sorted by numeric ID and instance;
duplicate pairs are invalid.

An inventory entry exposes only:

- capability ID and instance;
- capability version and feature mask;
- public flags, including affinity and sharing class;
- public limits defined by the capability contract.

It MUST NOT expose board identity, pins, peripheral numbers, routes, driver
names, private device kinds, provider vtables, or mutable ownership state.

There is no public or private runtime registration API. The build MUST fail when
an inventory entry cannot be constructed from the selected descriptor,
platform mapping, driver support, provider source, version, and features.
Runtime MUST NOT infer capability presence from a board ID or HMPY metadata.

The private generated `runtime_supported` field describes only whether the
selected Board Port is eligible for runtime firmware. It MUST NOT be
interpreted as physical qualification of the inventory, an individual
capability, or the produced image. Hardware qualification remains separate
evidence bound to its exact tested artifact.

## Required and optional declarations

Required and optional are build-composition declarations, not different
hardware call semantics.

- A missing required capability excludes the app or consumer. `--require-app`
  turns that exclusion into a build error.
- Every optional declaration MUST name a fallback. The consumer MUST check the
  acquisition result before using the fallback.
- Build diagnostics MUST carry a machine-readable reason such as
  `board-resource-absent`, `driver-unsupported`, `route-unavailable`,
  `provider-excluded`, `version-incompatible`, or `feature-missing`.
- Runtime acquisition repeats ID/version/feature/grant validation so malformed
  or custom composition cannot bypass the contract.

## Owners and grants

An owner is a generation-checked token issued by trusted firmware runtime
wiring. How the current native app or service receives an owner is private and
is not an App Runtime contract.

The generated build grants each owner a bounded set of capability requests.
Acquisition outside that set returns `HK_ERR_NOT_DECLARED`. Owner validation is
bug containment and lifecycle enforcement, not a security boundary between C
objects linked into the same firmware.

Closing an owner MUST attempt bounded cleanup of every active lease, invalidate
all of those leases, and then invalidate the owner generation. Owner-wide
cleanup is not cancellable; abandoning cleanup would leave ownership
ambiguous. When owner close is initiated on a different core from a fixed-
affinity provider, the core uses that provider's private synchronous teardown
dispatcher. The dispatcher MUST target the declared provider core, preserve the
original absolute deadline, and return only after cleanup reaches a safe state
or bounded dispatch/cleanup failure is known. Affinity mismatch alone does not
quarantine the provider.

## Lease and handle lifecycle

Acquisition creates one lease in a fixed-capacity table. There is no heap-backed
handle allocation. A typed handle is valid only while its lease slot,
generation, owner, capability ID, type, and affinity all validate.

- Releasing an all-zero handle is idempotent `HK_OK`.
- A partially zero or otherwise malformed non-zero handle returns
  `HK_ERR_STALE_HANDLE`; it is never treated as the zero handle.
- The first valid release performs capability cleanup, invalidates the lease,
  and zeros the caller's handle.
- A non-zero copied handle after release returns `HK_ERR_STALE_HANDLE`.
- An operation presented with another active owner returns
  `HK_ERR_WRONG_OWNER`.
- An inactive or generation-stale owner returns `HK_ERR_STALE_HANDLE`. Passing
  a valid lease through the wrong typed capability entry point returns
  `HK_ERR_INVALID_ARGUMENT` before provider access.
- Generation values MUST NOT silently wrap into reuse. An exhausted slot is
  retired until reboot.
- A failed cleanup invalidates the logical lease and quarantines the provider.
  It cannot be reacquired (`HK_ERR_INVALID_STATE`) until an explicit bounded
  recovery succeeds or the platform performs a controlled reset. Failed
  recovery leaves the provider quarantined.

Every capability contract states whether leases are shared, exclusive, or
exclusive over a declared resource mask.

## Deadlines

`hk_deadline_t.at_us` is an absolute value in the monotonic time domain.
`at_us == 0` means immediate/no wait. `UINT64_MAX` is invalid and MUST NOT be
used as "forever".

Every operation that may wait stores the caller's original deadline. It MUST
NOT create a new deadline for a chunk, row, FIFO burst, retry, or poll. A
provider publishes or validates a finite maximum operation duration and returns
`HK_ERR_LIMIT` for a request that exceeds it.

An already-expired deadline MUST be reported before hardware side effects. A
successful terminal completion latched before the deadline remains success even
if the caller observes it later.

## Cancellation

`hk_cancel_t` is a borrowed view. A null `probe` means no cancellation. The
probe MUST be bounded, non-blocking, allocation-free, and valid until terminal
completion.

Validation and terminal precedence are:

1. arguments, owner, handle generation, type, and affinity;
2. terminal completion already latched by the provider;
3. cancellation;
4. deadline expiration;
5. provider progress or I/O error.

If cancellation and expiration are first observed in the same poll,
`HK_ERR_CANCELLED` wins. A terminal cancellation result means the provider has
quiesced the operation and no later display, UART, I2C, or other write belonging
to that operation may occur.

Release and owner cleanup accept a deadline but are not cancellable.

## Core affinity and concurrency

Every inventory entry declares `ANY_CORE` or a specific core affinity. Calling a
valid handle from the wrong context returns `HK_ERR_WRONG_CONTEXT` before
hardware access. The trusted owner-close dispatcher is the only lifecycle
exception; it does not weaken affinity validation for acquire, release,
recovery, validation, or provider operations.

Language adapters running on another core MUST use a bounded transport to the
declared provider core. That transport does not become a second hardware
implementation and MUST preserve the original owner, lease, deadline,
cancellation, buffers, and terminal result.

## Buffer ownership

`hk_buffer_view_t` borrows memory; it never transfers ownership. Capability
contracts define whether a view is read-only, writable, or
implementation-owned and when the borrow ends.

- Providers MUST validate pointer, size, stride, alignment, access flags, and
  capability limits before hardware access.
- `HK_BUFFER_ACCESS_READABLE` and `HK_BUFFER_ACCESS_WRITABLE` are the public
  `hk_buffer_view_t.flags` bits; capability contracts state which are required
  or returned.
- A caller MUST NOT mutate a borrowed TX/input buffer before terminal
  completion.
- A caller MUST NOT read an RX/output buffer before terminal completion unless
  the operation explicitly reports a completed prefix.
- Implementation-owned borrowed buffers remain owned by the provider and MUST
  NOT be freed or retained after release/abort.

## Bounded resources

Capability core and providers MUST NOT introduce hidden heap allocations,
background tasks, queues, or cores. Any fixed ring, batch arena, operation slot,
or framebuffer is part of the provider's declared limits and measured static
RAM evidence.

Providers MUST make bounded progress per poll or transfer slice. Contract tests
MUST cover table exhaustion, limit errors, deadline/cancel races, cleanup after
errors, stale handles, and absence of late effects.

## Layering

The required dependency direction is:

```text
apps / language adapters
        -> public capability contracts
        -> capability implementations / portable services
        -> drivers
        -> platform HAL / selected BSP
```

Apps and language adapters MUST NOT include board/BSP, HAL, SDK, driver, or
capability-private headers. Capability implementations MUST NOT depend on apps
or language adapters. Boards MUST NOT depend on apps or product policy. A
Python-gated hardware provider is a contract violation.

## Conformance and evidence

The deterministic fake and every platform adapter MUST run the same capability
contract suite. A compile-conformance board does not acquire runtime or hardware
qualification merely by generating an inventory or compiling an adapter.

Each physical observation is bound to an immutable result and the exact image
hash that was tested. A later build MAY carry that observation forward when a
recorded source/composition impact review shows that the relevant capability,
provider, HAL, routing, resource budget, and test assumptions are unchanged.
The later build MUST pass the full automated gate and a boot sanity check.
Only affected physical checks are repeated. A provider, HAL, routing, board,
toolchain, or uncertain cross-cutting change invalidates the related
observations; it does not automatically invalidate unrelated observations.

## Compatibility and deprecation

Capability API version and stability are independent. While experimental, a
breaking change advances MINOR and requires migration notes; clients use an
exclusive upper bound to reject the new line. Compatible corrections advance
PATCH according to the [Versioning Policy](VERSIONING.md).

A future deprecated contract follows the repository deprecation metadata and
migration requirements. Capability IDs are never recycled even after removal.

## References

- [Architecture Vision](../ARCHITECTURE_VISION.md)
- [Roadmap](../ROADMAP.md)
- [Board Port Contract](BOARD_PORT.md)
- [Glossary](GLOSSARY.md)
- [Versioning Policy](VERSIONING.md)
- [Current App Lifecycle](../APP_LIFECYCLE.md)
- [MicroPython API](../MICROPYTHON_API.md)

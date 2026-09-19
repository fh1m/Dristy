---
contract-id: hackylens.capability.lights
owner: platform-architecture
version: 0.1.0
stability: experimental
---

# Lights Capability

## Identity and features

- Numeric ID: `0x00010005`.
- Canonical name: `hackylens.cap.lights`.

Feature and resource-mask bits:

| Bit | Name | Meaning |
|---:|---|---|
| `1 << 0` | `HK_LIGHTS_CHANNEL_BACKLIGHT` | Display backlight |
| `1 << 1` | `HK_LIGHTS_CHANNEL_ILLUMINATION` | Forward illumination |
| `1 << 2` | `HK_LIGHTS_CHANNEL_RGB` | RGB indicator |

## Ownership and values

Acquisition requests a non-zero channel mask. Ownership is exclusive only for
overlapping channel bits. Two owners MAY hold non-overlapping masks. Acquisition
is all-or-nothing; a conflict on one requested bit returns `HK_ERR_BUSY` without
granting another bit.

Public scalar and RGB channel values use the normalized inclusive range
`0..1000`. Language adapters convert their existing public range without
changing their API contract. Providers clamp neither invalid values nor absent
channels; they return an error before hardware access.

## Public operations

- acquire a channel-mask lease;
- query supported channels and limits;
- set one scalar channel;
- set RGB channels atomically where supported;
- release or owner-wide cleanup.

Writes are synchronous and bounded. They validate owner, lease, channel mask,
value, affinity, cancellation, and deadline before the first register write.
Cancellation after a completed synchronous write does not roll back that write.

## Cleanup and policy

Release drives every owned channel to provider-defined safe-off and invalidates
the lease. A finite release or owner-cleanup deadline is checked before the
first safe-off hardware effect. An already-expired ordinary release preserves
the lease for a bounded retry; an owner-close cleanup failure follows the common
invalidation and provider-quarantine rules. Cleanup does not read or restore
persisted product settings. The settings service may reacquire and reapply the
latest persisted state after a temporary owner such as MicroPython releases its
channels.

This separation keeps safe hardware cleanup in the capability provider and
product preference policy in the service layer.

## Required resources and consumers

Each advertised channel requires descriptor-backed resources, driver support,
and a provider mapping. A board may advertise any subset; no subset is inferred
from board identity.

Initial native consumers are settings/camera light services and Sleep.
MicroPython LED/RGB operations use the same provider. Including lights in Phase
2 is required to remove the current Python-to-driver path.

## Fake and acceptance

The fake records channel ownership, writes, safe-off cleanup, persisted-service
reapplication, cancellation, and deadlines. Tests cover overlapping and
non-overlapping masks, all-or-nothing acquisition, unsupported channels, ranges,
wrong owner, cleanup, and native/MicroPython provider identity.

SEN0305 acceptance observes backlight, illumination, RGB, cleanup, and persisted
state restoration.

## References

- [Capability API](../CAPABILITY_API.md)
- [MicroPython API](../../MICROPYTHON_API.md)

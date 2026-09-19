---
adr: 0005
title: Adopt owner-scoped capability handles
status: accepted
date: 2026-08-14
deciders: platform-architecture
supersedes:
superseded-by:
---

# ADR-0005: Adopt owner-scoped capability handles

## Context

Phase 1 permits apps to call board-independent drivers and services directly.
The current input dispatcher, LCD API, external-link service, light controls,
and MicroPython binding service therefore use different ownership, timeout,
cancellation, buffer, and cleanup models. MicroPython additionally contains a
second hardware implementation for display, lights, UART, and I2C behavior.

Phase 2 needs one portable surface for native firmware and language adapters
without introducing the Phase 3 App Runtime or a temporary public facade. The
firmware must remain bounded: no hidden heap allocation, tasks, queues, or
indefinite waits.

## Decision

Adopt generation-checked, owner-scoped capability handles as defined by the
experimental Capability API `0.1.0`.

Trusted private runtime wiring issues an `hk_owner_t` and generated grants. A
capability acquisition creates a fixed-table `hk_lease_t`; typed public handles
contain that lease and expose no provider/driver/HAL object. Every operation
validates the caller owner, lease generation, capability type, core affinity,
arguments, cancellation, and one absolute deadline before hardware access.

Release is idempotent only for an all-zero handle. A copied non-zero handle
after release is stale. Owner-wide cleanup is bounded and non-cancellable,
invalidates every lease, and quarantines a provider that cannot reach a safe
state. Mixed-affinity owner teardown uses each provider's private synchronous,
bounded cleanup dispatcher with the caller's original absolute deadline;
ordinary operations retain strict affinity checks. Generation counters do not
wrap into reuse during one boot.

Buffers are borrowed with explicit size, stride, flags, and lifetime. Providers
make bounded progress and may use only declared fixed storage. Native and
language-adapter consumers call the same providers; a cross-core adapter is a
transport, not a hardware implementation.

The existing `hk_app_t` callback ABI remains unchanged. Private registry wiring
binds the active static app to its owner and handles until Phase 3 defines a
public runtime context.

## Alternatives

- Publish the existing driver APIs: rejected because they expose display,
  debounce, routing, buffer, and Python-specific implementation assumptions.
- Use global singleton APIs without handles: rejected because ownership,
  conflicting consumers, stale use, and deterministic cleanup cannot be
  validated.
- Allocate polymorphic handle objects on the heap: rejected because resource
  use and cleanup would not remain statically bounded.
- Introduce `hk_app_context_t` and lifecycle v2 now: rejected because it would
  prematurely implement Phase 3 and change the current App Lifecycle contract.
- Give MicroPython separate service facades: rejected because it preserves
  duplicate hardware semantics and violates native/Python symmetry.

## Consequences

Every capability gains explicit identity, compatibility range, features,
owner, lease, affinity, deadline, cancellation, cleanup, and memory rules.
Providers require fixed owner/lease/operation state, adding measured static RAM
and dispatch cost. Clients must retain owner/handle values and handle precise
errors rather than rely on global hardware state.

The model catches stale, wrong-owner, wrong-core, and conflicting access before
hardware side effects. Cleanup becomes testable across normal exit, error,
cancel, deadline, and abnormal owner teardown.

Private native-app binding remains migration wiring, but the public capability
types and operations are the final Phase 2 surface and are reusable by the
future App Runtime.

## Compatibility and Migration

This adds Capability API `0.1.0 experimental` and the first per-capability
`0.1.0` contracts. It does not change Board Port `0.1.0`, HMPY `1.1.0`,
MicroPython API `1.0.0`, External Link Protocol `1.0.0`, or App Lifecycle
`0.2.0`.

Experimental clients request `[0.1.0, 0.2.0)` so a future breaking `0.2.0` is
not accepted silently. Driver and private-facade consumers migrate capability
by capability; each legacy production path is removed when its migration gate
passes.

## Evidence

- One host contract suite will run against deterministic fake and K210 adapters.
- Tests cover acquire/release, ownership conflicts, stale handles, table
  exhaustion, deadlines, cancellation, affinity, buffer lifetime, cleanup, and
  absence of late effects.
- Full and MicroPython-disabled builds measure flash/static RAM and prove no new
  heap, task, queue, core, or framebuffer.
- Native and MicroPython call traces/provider symbols prove shared
  implementation.
- Physical SEN0305 smoke is bound to an immutable closure image.

## References

- [Capability API](../spec/CAPABILITY_API.md)
- [Architecture Vision](../ARCHITECTURE_VISION.md)
- [Roadmap](../ROADMAP.md)
- [Current App Lifecycle](../APP_LIFECYCLE.md)
- [ADR-0004](0004-adopt-descriptor-driven-k210-board-ports.md)

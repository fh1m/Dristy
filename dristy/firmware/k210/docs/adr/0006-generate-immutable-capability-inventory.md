---
adr: 0006
title: Generate immutable capability inventory
status: accepted
date: 2026-08-14
deciders: platform-architecture
supersedes:
superseded-by:
---

# ADR-0006: Generate immutable capability inventory

## Context

Phase 1 board descriptors and the private K210 registry distinguish physical
inventory from driver-supported device kinds. `app_requirements.toml` currently
compares apps with those private kinds and can exclude an app or fail
`--require-app`, but it has no public capability IDs, versions, features,
runtime discovery, owner grants, or optional fallback contract.

Board device kinds cannot become public capability IDs: they encode private
composition details and do not express compatible versions, operations,
ownership, routing conflicts, or provider availability. Runtime inference from
`HELLO.board` would duplicate board knowledge in every client and would allow a
build to claim hardware behavior that was not compiled.

## Decision

Generate one immutable capability inventory from the selected board descriptor,
platform capability mapping, driver support, compiled providers, build profile,
and private consumer requirements.

The generated inventory is the only provider table. It is sorted by canonical
numeric capability ID and instance and stored read-only for the entire boot.
There is no runtime registration API and no board-ID inference.

`app_requirements.toml` advances to a private schema 2 with required and optional
capability requests. Requests use an inclusive minimum, exclusive maximum, and
required feature mask. Every optional request names a fallback. Legacy private
resource requirements remain only for subsystems not yet exposed as public
capabilities.

The generator emits const C inventory, private owner grants, canonical
`capabilities.json`, and composition metadata. It fails when resources,
routing, driver support, provider source, version, or features cannot satisfy an
entry. Required absence excludes an app; `--require-app` makes it a build error.
Optional absence records and selects the declared fallback.

Machine-readable absence reasons distinguish resource absence, unsupported
driver, unavailable route, excluded provider, incompatible version, and missing
feature. A diagnostic capability exclusion is recorded and cannot be labelled
release-qualified.

Required non-app consumers remain a build error in ordinary compositions. The
diagnostic-only `--disable-capability` mode may exclude them so every absent
capability can be compiled as a bounded feature-modified image; each such
exclusion is explicit in `composition.json` under
`required_consumer_exclusions` and no private grant is generated for it.

Board Port schema and responsibilities remain unchanged. The platform mapping
is a build-local projection from Board Port facts to Capability API providers.

## Alternatives

- Reuse board device kinds as public IDs: rejected because they are private and
  lack versioned behavior.
- Infer capabilities at runtime from board ID: rejected because it duplicates
  board tables, ignores build exclusions, and contradicts the Board Port
  contract.
- Allow providers to self-register during boot: rejected because inventory
  ordering, resource bounds, absence reasons, and impossible-composition failure
  would no longer be deterministic.
- Introduce the Phase 3 public app manifest now: rejected because Phase 2 needs
  only private build composition and owner grants.
- Compile every provider and hide unavailable entries at runtime: rejected
  because excluded capabilities would still consume flash/static resources and
  could accidentally be acquired.

## Consequences

Capability presence becomes a property of the exact board/build composition,
not of a product name. Required/optional behavior is testable before flashing.
The runtime discovery table is small, immutable, and cannot fabricate hardware.

Build tooling gains a platform capability catalog, generator, schema validation,
canonical artifacts, and negative-composition tests. Consumer requirements move
incrementally from private device kinds to public capability requests as each
provider migrates.

Cube may generate a conservative conformance inventory and compile adapters, but
that does not grant runtime or hardware qualification.

## Compatibility and Migration

This introduces Capability API and per-capability contracts at `0.1.0
experimental`. Board Port remains `0.1.0` because descriptors, required files,
and public board semantics do not change. HMPY remains `1.1.0` with wire-major
`1`; `HELLO.board` remains identity only.

`app_requirements.toml` schema 2 is private build metadata, not a public app
manifest. Phase 3 may generate equivalent requests from its future manifest
without changing the Capability API inventory or acquisition semantics.

## Evidence

- Generator tests cover deterministic output, duplicate/unknown IDs, version and
  feature mismatches, route/resource/provider absence, required exclusions,
  optional fallbacks, and `--require-app`.
- Architecture checks ensure only generated source defines the inventory and no
  provider is gated solely by MicroPython.
- Feature-disabled and full SEN0305 builds record exact composition and provider
  objects.
- Cube compile conformance explicitly carries no runtime/hardware claim.
- Canonical Phase 2 evidence binds source, toolchain, composition, inventory, and
  image hashes.

## References

- [Capability API](../spec/CAPABILITY_API.md)
- [Board Port Contract](../spec/BOARD_PORT.md)
- [Versioning Policy](../spec/VERSIONING.md)
- [Roadmap](../ROADMAP.md)
- [ADR-0004](0004-adopt-descriptor-driven-k210-board-ports.md)
- [ADR-0005](0005-adopt-owner-scoped-capability-handles.md)

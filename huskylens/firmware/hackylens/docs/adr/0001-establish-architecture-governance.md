---
adr: 0001
title: Establish architecture governance and versioned public contracts
status: accepted
date: 2026-08-11
deciders: platform-architecture
supersedes:
superseded-by:
---

# ADR-0001: Establish architecture governance and versioned public contracts

## Context

HackyLens v0.2 demonstrates a layered K210 reference firmware and an embedded
MicroPython vertical slice, but public boundaries have historically evolved as
implementation decisions. Board portability, a Platform API, an App SDK, and a
Project Format are not yet published contracts.

Future phases depend on stable terminology, explicit ownership, independent
version lines, and review evidence. Governance must be enforceable without
turning routine implementation work into an architectural process.

## Decision

HackyLens adopts the Architecture Vision as its governing direction and places
normative public contracts under `docs/spec/`. The five existing technical
contracts remain at their current paths during Phase 0 and receive the same
metadata.

Every public contract has a unique contract ID, logical owner, semantic version,
and stability label. Version and stability are independent. Firmware, HMPY,
Platform API, App SDK, and Project Format use independent version axes.

Significant architectural changes use numbered ADRs. Pull requests identify
affected layers, capabilities, contracts, compatibility, and evidence. A
deterministic documentation checker enforces repository-level rules; it does
not infer architecture or replace review judgment.

## Alternatives

- Keep documentation informal: rejected because it cannot prevent accidental
  public-boundary changes.
- Couple every contract version to firmware: rejected because the contracts
  evolve independently.
- Build a generic source/document parser: rejected because it would be brittle
  and would make governance tooling more complex than the rules it enforces.
- Require ADRs for every implementation choice: rejected because routine work
  does not need permanent architectural records.

## Consequences

Public contracts become identifiable, reviewable, independently versioned, and
machine checked. Contributors must update metadata and evidence when changing a
contract. A small amount of documentation and CI maintenance is added.

Phase 0 does not make any technical contract stable and does not implement
Board Port, Capability API, App SDK, or Project Format behavior.

## Compatibility and Migration

No firmware, wire, storage, or application behavior changes. Existing public
technical documents are labeled experimental at their current compatibility
versions. Future contract promotion or breaking changes require the versioning
and ADR processes defined here.

## Evidence

- Repository documentation consistency checker and unit fixtures.
- Existing architecture guard and host test suite.
- Full and MicroPython-disabled firmware builds with no firmware-input change.

## References

- [Architecture Vision](../ARCHITECTURE_VISION.md)
- [Current State](../CURRENT_STATE.md)
- [Platform Roadmap](../ROADMAP.md)
- [Normative Specifications](../spec/README.md)
- [Versioning Policy](../spec/VERSIONING.md)

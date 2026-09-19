---
adr: 0003
title: Require machine-readable deprecation migration routes
status: accepted
date: 2026-08-11
deciders: platform-architecture
supersedes:
superseded-by:
---

# ADR-0003: Require machine-readable deprecation migration routes

## Context

The versioning policy already required every deprecated contract to provide a
replacement or migration path. The documentation guard could enforce the
deprecation and removal versions but could not prove that a usable migration
route existed. Free-form prose or an external URL left this requirement
ambiguous and non-deterministic.

## Decision

A deprecated contract must declare at least one of two front-matter fields:

- `migration-guide`, referencing an existing repository-local Markdown target;
- `replacement-contract`, naming another existing, non-deprecated contract.

Both fields may be present. The documentation guard validates local files and
anchors, contract identity, self-reference, and replacement lifecycle state.

## Alternatives

- Keep migration evidence in prose: rejected because presence and link health
  cannot be checked deterministically.
- Require only `replacement-contract`: rejected because some removals need a
  procedural migration without a one-for-one replacement contract.
- Require only `migration-guide`: rejected because a direct replacement ID is
  more precise when one exists.
- Accept external migration URLs: rejected because repository conformance would
  depend on mutable external state.

## Consequences

Deprecation metadata has a deterministic exit path that can be validated in CI.
The experimental versioning policy advances from `0.2.0` to `0.3.0` because the
accepted metadata forms are now normative. Continued compatibility-test
evidence remains a review and test-suite responsibility until a stable test
manifest contract is defined.

## Compatibility and Migration

The repository has no deprecated public contracts, so no existing document
requires migration. A future deprecated contract must add either field when it
adds `deprecated-since` and `removal-version`.

Firmware, wire formats, storage formats, and runtime behavior are unchanged.

## Evidence

- Documentation tests cover missing, valid, broken, and unknown migration
  routes.
- The repository documentation guard validates the current contract set.
- The versioning policy defines the metadata before the checker enforces it.

## References

- [Versioning Policy](../spec/VERSIONING.md)
- [Architecture Governance](0001-establish-architecture-governance.md)
- [Experimental Versioning](0002-define-experimental-versioning.md)

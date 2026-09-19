---
adr: 0002
title: Define experimental contract versioning independently of stability
status: accepted
date: 2026-08-11
deciders: platform-architecture
supersedes:
superseded-by:
---

# ADR-0002: Define experimental contract versioning independently of stability

## Context

The governance baseline declares semantic version and stability to be
independent, and allows an experimental contract to provide no compatibility
promise. The initial versioning text nevertheless applied stable SemVer major
rules to every `1.x` contract, including experimental ones. As a result,
`1.0.0 experimental` implied a stronger compatibility signal than its stability
label.

## Decision

All experimental contracts use MINOR for intentional breaking changes,
regardless of their current major version. PATCH never contains an intentional
breaking change. Stable contracts use the conventional SemVer rule in which an
incompatible change increments MAJOR.

Encoded wire and storage major/schema fields remain representation-level
discriminators. They change when their encoded representation is incompatible
and do not redefine the lifecycle rule for the contract's semantic version.

## Alternatives

- Apply conventional SemVer to experimental `1.x` contracts: rejected because
  it couples the compatibility promise to the version number instead of the
  explicit stability field.
- Keep every experimental contract below `1.0.0`: rejected because existing
  wire and API contracts already have meaningful v1 identities.
- Allow breaking PATCH releases while experimental: rejected because PATCH must
  remain a useful compatible-correction signal.

## Consequences

An experimental `1.0.0` contract may become `1.1.0` after a breaking change,
and consumers must use the stability label when deciding whether to depend on
compatibility. Stable contracts retain normal SemVer expectations.

The versioning policy moves from `0.1.0` to `0.2.0` because this correction
changes a normative version-selection rule.

## Compatibility and Migration

No firmware, wire, storage, or application representation changes. Future
experimental contract changes must follow the corrected MINOR rule. Tooling and
documentation that assumed stability from a `1.x` number must inspect the
explicit stability field instead.

## Evidence

- Documentation contract tests pin the experimental and stable rules.
- The documentation guard continues to validate contract metadata and canonical
  representation-version sources independently.

## References

- [Versioning Policy](../spec/VERSIONING.md)
- [Architecture Governance](0001-establish-architecture-governance.md)

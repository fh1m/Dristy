# HackyLens Normative Specifications

This directory contains the normative governance baseline for HackyLens public
contracts. Normative requirements use **MUST**, **MUST NOT**, **SHOULD**, and
**MAY** in their ordinary RFC-style meanings. Implementation and audit documents
remain informative unless they explicitly say otherwise.

Every normative or grandfathered public contract has Markdown front matter with
four required fields:

```yaml
---
contract-id: hackylens.example
owner: platform-architecture
version: 0.1.0
stability: experimental
---
```

The version identifies a compatibility line. Stability is independent: a
`1.0.0` contract is not stable unless its `stability` field says `stable`.
Contracts with an independently encoded generation also declare its canonical
metadata explicitly: `wire-major`, `schema-major`, or `api-major`. These fields
are not inferred from the semantic-version MAJOR component.

## Normative governance contracts

- [Architecture Vision](../ARCHITECTURE_VISION.md)
- [Glossary](GLOSSARY.md)
- [Versioning Policy](VERSIONING.md)
- [Board Port Contract](BOARD_PORT.md)

## Capability contracts

- [Capability API](CAPABILITY_API.md)
- [Time Capability](capabilities/TIME.md)
- [Input Capability](capabilities/INPUT.md)
- [Display Capability](capabilities/DISPLAY.md)
- [External Link Capability](capabilities/EXTERNAL_LINK.md)
- [Lights Capability](capabilities/LIGHTS.md)

## Existing technical contracts

Phase 0 keeps the following existing documents in their current locations and
applies the same metadata policy to them:

- [HMPY Protocol](../HMPY_PROTOCOL.md)
- [MicroPython API](../MICROPYTHON_API.md)
- [External Link Protocol](../EXTERNAL_LINK_PROTOCOL.md)
- [Current App Lifecycle](../APP_LIFECYCLE.md)
- [AI Model Package](../AI_MODELS.md)

New normative public contracts MUST be added under `docs/spec/` or a
contract-specific subdirectory below it. Moving the five existing technical
documents is intentionally deferred to avoid unrelated link churn during the
governance baseline.

## Logical owners

- `platform-architecture`: architecture direction, terminology, and governance.
- `device-protocols`: device/host and external wire protocols.
- `micropython-runtime`: the public MicroPython programming surface.
- `firmware-runtime`: the current native firmware lifecycle contract.
- `ai-runtime`: AI model package and model-runtime contracts.

Owner identifiers name stable components, not individual maintainers.

## Change process

Contract changes MUST update the document version and stability metadata when
required by the [Versioning Policy](VERSIONING.md). Architectural decisions are
recorded under [`docs/adr/`](../adr/README.md). Pull requests identify affected
layers, capabilities, contracts, compatibility, and evidence.

---
contract-id: hackylens.glossary
owner: platform-architecture
version: 0.1.0
stability: experimental
---

# HackyLens Platform Glossary

This document defines the canonical architectural terms used by HackyLens
specifications, ADRs, source layout, and review workflow.

## Normative language

- **MUST** and **MUST NOT** define requirements needed for conformance.
- **SHOULD** defines the default rule; exceptions require an explicit reason.
- **MAY** identifies an optional behavior.
- **Normative** text defines a contract. **Informative** text explains an
  implementation, audit, example, or future direction.

## Hardware and platform

### Board

A **board** is one supported physical product or hardware revision selected as
a build target. It owns its descriptor, BSP, pins, peripheral wiring, flash
layout, and capability inventory. An application MUST NOT infer hardware from a
board identifier.

### Board Support Package (BSP)

A **BSP** is the board-specific implementation that binds a board descriptor to
platform startup, pins, clocks, flash layout, and concrete devices. Product
policy and feature-app behavior do not belong in a BSP.

### Platform

A **platform** is the processor-family substrate shared by compatible boards.
For the first reference implementation this is Kendryte K210 startup,
toolchain, common HAL behavior, and processor-level facilities. A platform MUST
NOT encode SEN0305 product wiring or application policy.

### Hardware Abstraction Layer (HAL)

The **HAL** exposes bounded processor/peripheral primitives used by BSPs,
drivers, and low-level platform code. It hides SDK and register mechanics but
does not define application-facing capabilities.

### Driver

A **driver** controls one concrete device or peripheral using the HAL and BSP
configuration. A driver may know a sensor or controller model, but MUST NOT
depend on feature apps or define product workflow.

## Runtime and application

### Service

A **service** implements reusable runtime behavior by coordinating drivers,
storage, algorithms, or other services. It owns implementation policy such as
resource arbitration, deadlines, cancellation, and cleanup. A service is not
automatically a public application interface.

### Capability

A **capability** is a versioned, app-facing contract for a platform-provided
function. It defines availability, ownership, lifetime, errors, bounded
operations, cancellation, cleanup, and test behavior. Services may implement a
capability; the terms are not synonyms.

### Runtime

The **runtime** creates and stops application instances, issues declared
capability handles, dispatches events/ticks, and guarantees cleanup. It does not
include every firmware subsystem and MUST NOT expose undeclared hardware access
to an app.

### App

An **app** is a compile-time-composable, board-independent feature module. It
uses only the App SDK and declared capabilities, owns its instance state and
feature policy, and can be excluded from the build without leaving private
feature sources or registry entries.

### Project

A **project** is a developer-owned collection of source files, manifest,
configuration, and assets executed through a supported runtime. It is distinct
from a firmware feature-app directory and from device filesystem state.

### Adapter

An **adapter** maps an existing contract to another client surface, such as
native C or MicroPython. It MUST preserve the underlying ownership, coordinates,
formats, errors, deadlines, and cleanup semantics; it MUST NOT create a second
hardware implementation or independent policy path.

## Contract governance

### Public contract

A **public contract** is behavior that external projects, apps, tools, boards,
or adapters are expected to depend on. Each public contract MUST have a unique
`contract-id`, logical owner, semantic version, stability label, normative
documentation, and compatibility policy.

### Implementation

An **implementation** realizes a contract for a particular platform, board, or
language. Its internal symbols and file layout are not public merely because
they are visible in the source tree.

### Owner

An **owner** is the logical component responsible for a contract's definition,
compatibility, tests, and migration path. It is not resource ownership at
runtime and does not identify a single person.

### Stability

**Stability** is the lifecycle promise attached to a contract:
`experimental`, `stable`, or `deprecated`. Stability is independent of semantic
version and is defined by the Versioning Policy.

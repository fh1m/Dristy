---
adr: 0004
title: Adopt descriptor-driven K210 board ports
status: accepted
date: 2026-08-11
deciders: platform-architecture
supersedes:
superseded-by:
---

# ADR-0004: Adopt descriptor-driven K210 board ports

## Context

Firmware `0.2.0` embedded HUSKYLENS pinmux, display, camera, storage, connector,
and programming assumptions in common source and tools. A second K210 board
could not be represented or checked without copying those assumptions or
adding board-ID branches. Phase 1 needs an honest portability boundary without
prematurely defining the future Capability Platform.

## Decision

K210 ports are descriptor-driven packages selected explicitly at build,
package, and flash time. A private platform registry validates descriptor IDs,
profiles, drivers, routes, and named programming profiles. Generated tracked
headers are the compiled projection of exactly one selected BSP.

HUSKYLENS SEN0305 is the only runtime/release port. Sipeed Maix Cube is a
non-releaseable conformance port that records confirmed inventory and a
conservative layout without claiming runtime qualification. Apps may continue
using board-independent drivers, while board/HAL/SDK dependencies are removed
from apps through private runtime facades.

## Alternatives

- Keep an implicit SEN0305 build: rejected because artifacts and flash safety
  would remain ambiguous.
- Add board-ID conditionals to tools: rejected because descriptors would no
  longer be the source of behavior.
- Introduce the Capability Platform now: rejected because it would combine
  board extraction with the broader Phase 2 app migration.
- Ban app-to-driver dependencies now: rejected because it creates a temporary
  facade and a second migration when capabilities arrive.

## Consequences

Firmware, build directories, images, sidecars, release packages, and flash
commands are board-qualified. The HAL and startup implementation live under
`platforms/k210`; common firmware compiles with exactly one BSP. Descriptor and
canonical-byte validation become mandatory CI gates.

Cube cannot produce full firmware, releases, or hardware flash operations.
Hardware qualification remains separate. No public capability IDs, runtime
discovery, App SDK, acquisition lifecycle, tasks, queues, or heap allocation
are added.

## Compatibility and Migration

Firmware advances from `0.2.0` to `0.3.0`. Build, package, and flash invocations
must add `--board huskylens-sen0305`; old implicit paths are removed. Artifact
names and directories become board-qualified.

HMPY advances from contract `1.0.0` to experimental `1.1.0`, while wire-major
stays `1`. The existing board field now normatively carries the canonical
descriptor ID. Clients must not treat it as capability discovery.

## Evidence

- Both board descriptors, BSP callback requirements, route selections,
  programming metadata, generated files, and canonical layouts are checked.
- Cube receives a host BSP compile-check and is rejected by full, release, and
  flash commands.
- SEN0305 feature-disabled and full cross-builds exercise board-qualified
  staging and ensure exactly one selected BSP.
- The pinned Phase 1 resource baseline is tracked under `docs/evidence/`.

## References

- [Board Port Contract](../spec/BOARD_PORT.md)
- [Versioning Policy](../spec/VERSIONING.md)
- [HMPY Protocol](../HMPY_PROTOCOL.md)
- [Architecture Vision](../ARCHITECTURE_VISION.md)

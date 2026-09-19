# Current Project State

> HackyLens v0.4 is a layered K210 reference firmware and MicroPython technology
> preview.

## Phase 1 status

Firmware 0.4.0 implements the experimental Board Port Contract 0.1.0. The
common firmware is composed with the K210 platform and exactly one selected
board support package. Build, image, package, release, and flash commands
require an explicit `--board`; there is no implicit board.

Full builds emit canonical private build attestations bound to the exact image
hash, version, selected board/platform/runtime profile, target, and complete app
composition. `make_image.py` and `package_release.py` require this evidence;
only the complete unmodified full build is release-qualified. Feature-disabled
or fault-injection images cannot be promoted by adding version/board text or by
renaming the file. The attestation is private build evidence, not a public
contract or runtime-discovery surface.

Two descriptors are tracked:

- `huskylens-sen0305` is the runtime-qualified, releaseable port using the
  `hackylens-full` runtime profile.
- `sipeed-maix-cube` is a descriptor/BSP compile-conformance port. It is not
  runtime-qualified or releaseable, and its full build, package, release, and
  flash operations fail before performing work.

The Cube port proves that a second descriptor, generated-header set, and
minimal BSP satisfy the conformance compile/link harness. It does not prove
that common runtime firmware is composed for Cube, nor is it evidence of Cube
runtime behavior or general hardware independence. Its Grove connector records
only the source-verified physical pins 24 and 25; it deliberately claims no
UART/I2C protocol or routed-peripheral semantics.

## Implemented boundaries

- Board descriptors own identity, inventory, routes, defaults, flash layout,
  and programming metadata.
- `platforms/k210/devices.toml` privately registers recognized hardware,
  drivers, exact route roles, the one legacy runtime-mux exception, base
  runtime devices, programming values, and runtime profiles.
- Generated `pins.h`, `defaults.h`, `inventory.h`, and `flash_layout.h` are
  checked for staleness.
- Applications do not include K210 SDK headers. BSP/HAL code and explicitly
  platform-bound internals may include them; startup lives in
  `platforms/k210/startup`.
- Applications may continue to call board-independent driver/service APIs in
  Phase 1, but cannot include a board BSP, platform HAL, or K210 SDK header.
- Application time needs use the public `hackylens.cap.time` 0.1 capability;
  runtime and MicroPython button reads share `hackylens.cap.input` 0.1; and
  settings, camera sessions, Sleep, and MicroPython light writes share
  `hackylens.cap.lights` 0.1. The native external-link service and MicroPython
  UART/I2C bindings share `hackylens.cap.external-link` 0.1. Private
  boot/recovery wiring remains outside the capability surface.
- Private schema-2 app requirements and the separate runtime/service/adapter
  consumer manifest control board-aware composition, capability grants,
  optional fallbacks, and exclusion. They do not expose runtime hardware
  access. `--require-app` changes an incompatible exclusion into an error.
- Phase 2.2 adds Capability API 0.1 common types and a fixed-capacity host-tested
  owner/lease core. Phase 2.3 generates its immutable provider table and owner
  grants from the selected board/profile and the private K210 capability map,
  without changing `hk_app_t`. Phase 2.4 adds Time with shared native/MicroPython
  deadline and cancellation semantics. Phase 2.5 adds Input with fixed-time
  sampling/debounce, an eight-event ring, independent lease cursors, and one
  state provider for the native dispatcher and MicroPython. Phase 2.6 adds
  non-overlapping channel-mask ownership for backlight, illumination, and RGB,
  safe-off cleanup, and persisted settings restoration after temporary leases.
  Phase 2.7 freezes Display 0.1 semantics with a deterministic fixed-capacity
  host fake, including planes, clipped batches, borrowed surfaces, dirty-region
  transfer accounting, retry, repair, and cleanup. Phase 2.8 adds the K210
  provider, splits the raw ST7789 transport from plane state, migrates native
  views through a private typed BASE binding, and moves MicroPython drawing to
  OVERLAY. Both planes reuse the single shadow framebuffer. Phase 2.9 freezes
  the public External Link 0.1 connector/async contract with a deterministic
  fixed-capacity fake. Phase 2.10 adds the K210 provider, migrates the native
  UART/I2C-target service and MicroPython UART/I2C-controller bindings to that
  single provider, and removes the Python-only hardware implementation.
  Phase 2.11 is complete: raw SD access remains behind the
  private storage boundary, frame storage is a portable service with explicit
  borrow/release, and architecture guard v2 has an explicit layer map plus
  generated dependency and object-symbol evidence. Corrective commit `5eaf827`
  generalizes repository object-symbol edges, makes workspace generation
  exhaustion non-wrapping, and passed
  [Release firmware run 32565126272](https://github.com/aztechell/hackylens/actions/runs/32565126272).
  Phase 2.12 automated qualification is complete: all five capabilities run
  identical provider-independent normative case sets against fake and K210,
  with adapter-specific checks kept supplemental. The complete build,
  diagnostic, resource, latency, and rolling-evidence matrix passed
  [Release firmware run 32569674140](https://github.com/aztechell/hackylens/actions/runs/32569674140).
  Phase 2 is complete. Exact implementation commit `f8b76f4` passed automated
  qualification, and owner acceptance links the tested SEN0305 image to the
  immutable candidate result through the impact-based physical ledger. Buttons,
  Files, MicroPython, and Lights received the final targeted checks; unchanged
  UART, I2C, TIME, HMPY, camera, Pong, menu, and Sleep observations were carried
  forward only after impact review. Evidence limitations remain explicit; Maix
  Cube is compile-conformance-only. See `docs/PHASE2_PHYSICAL_STATUS.md`.

The twelve feature applications remain self-contained source modules with
compile-time exclusion. Existing camera, LCD, input, storage, HMPY, external
link, AI, and MicroPython behavior remains part of the SEN0305 runtime profile.

## Contract and release status

| Area | Status |
| --- | --- |
| Firmware | 0.4.0; Phase 2 Capability Platform completed on SEN0305 |
| Board Port Contract | 0.1.0 experimental |
| HMPY Contract | 1.1.0 experimental; wire-major 1 |
| Versioning Policy | 0.4.0 |
| SEN0305 runtime port | Supported and releaseable |
| Cube port | Compile conformance only |
| Capability build composition | Phase 2 qualified on SEN0305; Time + Input + Display + External Link + Lights runtime providers |
| App SDK | Deferred to Phase 3 |
| General hardware portability | Not claimed |

`HELLO.board` is the canonical `board.toml.id`; clients must not infer
capabilities from it. Capability discovery returns the generated immutable
inventory, currently Time, Input, Display, External Link, and Lights on SEN0305,
and is not an App Runtime or manifest surface. The private generated
`runtime_supported` marker describes
Board Port runtime eligibility only; it is not physical capability or hardware
qualification.

## Verification and evidence

Host checks cover descriptor/profile/programming validation, route selection
and collisions, generated-file freshness, canonical flash-layout bytes,
architecture boundaries, board-aware composition, sidecar safety, and CLI
rejection paths. CI compile-checks the Cube BSP and builds SEN0305 full and
feature-disabled configurations.

The pinned pre-change resource baseline is recorded in
`docs/evidence/phase1-baseline.json`. Its exact canonical bytes are digest-
locked to SHA-256
`75d8300766266c144847d1805541ca2303a1fffd1b4496649e50f38af3bb889f`,
its commit must be an ancestor of HEAD, and the historical/current
bootstrap pins and historical `VERSION` must match the evidence. The baseline
BIN/ELF hashes are explicitly named manually pinned golden evidence, and the
CMake version is explicitly operator-recorded; none is presented as a
reproduced build attestation. Acceptance
requires no static-RAM growth and no more than 8192 bytes of erase-rounded
flash growth for the historical Phase 1 closure. The lexical runtime-object guard
applies translation-phase splicing, removes comments and literals, tracks
direct primitives, cross-translation-unit explicit aliases/macros,
allocation/task/queue wrapper calls, file-scope creation, and C++ creation
forms, then compares site-aware fingerprints against the complete baseline
source snapshot. It is a conservative lexical policy check rather than a full
C/C++ semantic analyser, so alias-name propagation may intentionally reject
ambiguous same-named calls. Result hashes are explicitly
named `local_sha256` under `hash_scope=local-workspace-diagnostic`; they remain
mandatory local evidence but are excluded from CI freshness because Phase 1
does not claim general reproducible builds. Compiler and assembler prefix maps
normalize workspace and SDK roots before compilation; builds from differently
sized workspace paths have identical raw BIN bytes and identical resource
sections, which makes the exact CI resource projection path-independent. The
debug ELF hash remains diagnostic rather than a release identity.

`docs/evidence/phase1-result.json` records the Phase 1 rolling result at Phase 1
closure and is no longer compared byte-for-byte with later Phase 2 binaries.
Phase 2 CI instead checks both current build profiles against the immutable
`phase2-baseline.json` attestation and resource budgets. The immutable Phase 1
closure snapshot is `docs/evidence/phase1-closure-result.json`. The hardware-smoke
document names that closure file, pins its canonical-file SHA-256, and must
match its board, firmware version, image size, and image SHA-256. Consequently,
later rolling results do not inherit the closure image's hardware-tested status.

The physical SEN0305 gate passed on 2026-08-13 and is recorded in
`docs/evidence/phase1-hardware-smoke.json`. The release-qualified package booted
as firmware 0.3.0; display and camera captures were inspected; all four button
masks, illumination, RGB, SD, HMPY, external-link runtime routing/transmit, and
package/flash round trip passed. The ISP stub has no flash-read command, and no
external electrical loopback fixture was attached, so those limitations are
explicit in the evidence rather than being presented as qualifications. Cube
hardware qualification is a separate future gate.

## Remaining architecture work

Display is runtime-composed; its borrowed BASE surface is explicitly an in-place
backing store, while retained command batches remain transactional. Phase 2
records physical display observations and explicitly retains the missing
matched-workload timing dataset as an evidence limitation rather than inventing
measurements. Later packages may extend qualification on new hardware.
Public App Runtime/context, app manifests, App SDK, and public storage, camera,
vision, and AI capabilities remain Phase 3+.

Other product gaps remain unchanged: broader MicroPython hardware APIs,
full project/package management, multi-project IDE workflows, formal
Python-to-native migration, complete original-firmware parity, and long-run
hardware qualification.

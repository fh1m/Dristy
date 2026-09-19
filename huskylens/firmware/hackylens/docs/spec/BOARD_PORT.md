---
contract-id: hackylens.board-port
owner: platform-architecture
version: 0.1.0
stability: experimental
---

# K210 Board Port Contract

This experimental contract defines build-time K210 board selection. It does
not define capabilities, an App SDK, runtime discovery, or runtime resource
arbitration.

## Port package and identity

Every port is stored at `boards/<id>/` and MUST contain `board.toml`,
`board.c`, canonical `flash_layout.json`, and generated `pins.h`, `defaults.h`,
`inventory.h`, and `flash_layout.h` under `generated/`. The descriptor `id`
MUST equal the directory name and is the canonical board identity used in
artifacts and HMPY `HELLO.board`.

The schema-1 support invariants are:

- `support=runtime` requires a known `runtime_profile`;
- `support=conformance` forbids `runtime_profile` and requires
  `releaseable=false`;
- `releaseable=true` requires `support=runtime`;
- every runtime-profile device is present in hardware inventory and has a
  known working driver binding;
- `known-unsupported` hardware remains visible in hardware inventory but MUST
  NOT appear in driver-supported inventory;
- unknown schema fields, device IDs, device kinds, drivers, functions, and
  peripheral instances are errors.

Registry and inventory IDs are private composition metadata. They are not
public capability IDs and MUST NOT be exposed as runtime-discovery surfaces.

## Routes and generated state

Each route independently identifies a physical `pin`, FPIOA `function`, and
`peripheral` instance. The private platform registry maps every logical route
macro to its exact signal function and peripheral, maps each function to its
peripheral and SDK macro, and maps each device ID to exactly one kind and its
allowed driver bindings. Descriptors cannot swap or redefine those
relationships. Every runtime profile is a superset of the registry's
`runtime_base_devices`, which represents hardware used unconditionally by the
common Phase 1 startup path.
Pins and functions MUST be unique. Multiple different functions belonging to
one peripheral instance are valid. Conflicting build-time alternatives are
valid only inside one `exclusive_group`.

`runtime_mux_group` is one narrowly scoped preservation exception for the
existing SEN0305 external connector, whose UART and I2C modes already remap the
same two pins at runtime. The exact group name and complete macro/function/pin/
peripheral set are allowlisted by `legacy_runtime_muxes` in the platform
registry; descriptors cannot invent, move, shrink, or extend an exception.
Every mode is still modeled as explicit routes and collision-checked. This
private metadata is not a general runtime-routing, switching, arbitration,
capability, or discovery API, and new ports MUST NOT use it to infer such a
contract.

A conformance-only descriptor MAY record source-verified physical connector
pins without routes only when `protocols=[]`. Such inventory states only that
the pins reach the connector; it MUST NOT be interpreted as UART, I2C, GPIO,
or other peripheral semantics. Runtime descriptors require explicit routes
for connector pins. The Cube Grove pins 24 and 25 are pinned to Sipeed's
[MaixPy-v1 Cube configuration](https://github.com/sipeed/MaixPy-v1/blob/d8901fd2272f000226f1c1037c1eb7c412b88e66/components/boards/config/cube.config.json);
Phase 1 intentionally does not turn those physical names into invented
UART/I2C devices.

`route_selections` selects exactly one compiled route for each group on a
runtime board. A conformance board may omit a selection only when every route
in that group explicitly has `compile=false`. Unselected and `compile=false`
routes MUST NOT appear in generated pin headers or compile staging. The Board
Port Contract introduces no runtime route switching or arbitration API.

Generated files are tracked. CI regenerates them and performs exact text
comparison so stale generated data fails before compilation.

## Board operations

Exactly one selected BSP defines `const hk_board_ops_t hk_board_ops`.
`early_init` is always mandatory and non-NULL. A `*_prepare` callback is
mandatory for every driver-supported inventory kind mapped by the platform
registry. Preparation callbacks are limited to power, routing, pinmux, and
electrical preparation. Device drivers and services own device initialization,
operation, cancellation, and lifecycle.

## Programming metadata

`[programming]` is the sole source of board-specific reset, reboot, ISP stub,
flash, baud, retry, and USB-detection defaults. Flash tooling MAY implement
general named reset profiles, but MUST NOT select behavior with board-ID
conditionals or separate board behavior tables. Safe command-line overrides
are accepted only when `programming.supported=true` and cannot bypass layout,
size, address, sidecar, or identity validation.

When `programming.supported=false`, flash commands fail before serial access.
Stub, flash, baud, and retry defaults are forbidden, and USB detection is
`explicit-port`.

## Architecture boundaries and composition

Phase 1 forbids apps from including board/BSP, platform HAL, or K210 SDK
headers. Existing board-independent driver and service APIs MAY remain direct
app dependencies when they contain no board identity, pins, routing, or HAL
assumptions. Removing app-to-driver dependencies is deferred to the Capability
Platform phase.

Runtime needs such as time and boot/recovery use private internal C facades.
They are not versioned public contracts, capabilities, SDK APIs, or discovery
surfaces. The separate `firmware/app_requirements.toml` is build-time-only: it
may exclude an incompatible app with a machine-readable reason, but MUST NOT
provide runtime hardware access. `--require-app` turns such an exclusion into
a build error.

## Layout and artifact safety

Every layout file is exact canonical UTF-8 JSON: no BOM, recursively sorted
object keys, preserved array order, two-space indentation, no trailing spaces,
LF line endings, and exactly one final LF. Its SHA-256 is calculated only after
canonical-byte validation.

Every successful firmware build first writes a canonical private schema-2 build
attestation. It binds the exact image size and SHA-256 to the firmware version,
board/platform/runtime profile, target, build profile, complete enabled/disabled
app composition, diagnostic capability exclusions, generated capability-
inventory SHA-256, board-driven exclusions, and fault-injection state. The
attestation is build metadata, not a public contract or runtime discovery
surface. Only an unmodified `full` target with the complete `hackylens-full`
composition is `release_qualified`; feature-disabled and fault-injection builds
cannot be relabelled as full artifacts. Standalone image creation and release
packaging require and revalidate the canonical attestation against the exact
input bytes and requested board/profile.

Board-qualified images carry one schema-1 flasher sidecar with the exact required
fields `schema`, `firmware_version`, `board_id`, `platform_id`,
`board_contract_version`, `build_profile`, `image`, `size`, `sha256`,
`flash_address`, and `flash_layout_sha256`; a typed `runtime_profile` is also
present for runtime ports. Packaging verifies both the source sidecar and build
attestation before copying, then revalidates the copied attestation and writes a
new valid same-stem flasher sidecar for the copied release image; release-only
metadata lives in a separate `-release.json` manifest. A missing sidecar is
accepted only with
`--allow-missing-sidecar`, after selected-board address and size checks. The
override never accepts an existing mismatch or an oversized image.

The `sipeed-maix-cube` layout and BSP are conformance evidence only. They are
not a runtime-qualified storage contract or evidence of hardware independence.

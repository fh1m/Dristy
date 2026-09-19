---
contract-id: hackylens.versioning-policy
owner: platform-architecture
version: 0.4.0
stability: experimental
---

# HackyLens Contract Versioning Policy

This policy defines independent semantic-version lines and the lifecycle of
HackyLens public contracts.

## Version and stability are independent

Semantic version identifies a compatibility line. Stability identifies the
promise made for that line.

> A `1.0.0` contract has a defined compatibility line. It is not stable unless
> `stability` is explicitly set to `stable`.

Phase 0 marks every existing technical contract `experimental`. It does not
promote any firmware, protocol, API, or format to stable.

## Independent version axes

| Axis | Current version | Stability/maturity | Canonical source |
| --- | --- | --- | --- |
| Firmware | `0.4.0` | Technology preview; physical qualification in progress | `VERSION` |
| HMPY | `1.1.0` (wire major `1`) | Experimental | HMPY contract and codec constants |
| Board Port Contract | `0.1.0` | Experimental | `BOARD_PORT.md` and board descriptors |
| Platform API | Unpublished | No public contract | Future Platform API spec |
| App SDK | Unpublished | No public contract | Future App SDK spec |
| Project Format | Unpublished | No public contract | Future Project Format spec |

The first published experimental Platform API, App SDK, or Project Format
contract starts at `0.1.0`. A version change on one axis MUST NOT implicitly
change any other axis.

Additional existing technical contracts retain their own compatibility lines:

| Contract | Version | Stability |
| --- | --- | --- |
| MicroPython API | `1.0.0` | Experimental |
| External Link Protocol | `1.0.0` (wire major `1`) | Experimental |
| AI Model Package | `1.0.0` (schema major `1`) | Experimental |
| Legacy App Lifecycle | `0.2.0` | Experimental |

`hackylens.legacy-app-lifecycle` version `0.2.0` describes the lifecycle shipped
by firmware `0.2.0`. This historical match does not establish permanent version
coupling with firmware or with future App SDK/runtime contracts.

## Semantic version rules

Versions use `MAJOR.MINOR.PATCH`.

- For an experimental contract at any major version, an intentional breaking
  change increments MINOR. PATCH MUST NOT contain an intentional breaking
  change.
- For an experimental contract, a backward-compatible addition increments
  MINOR and a compatible correction increments PATCH.
- For a stable contract, an incompatible change increments MAJOR, a backward-
  compatible addition increments MINOR, and a compatible correction increments
  PATCH.
- A firmware release MAY contain unchanged contract versions.
- Documentation-only clarification MAY increment PATCH when it changes no
  observable requirement. A typo or link correction need not change the
  contract version.
- Wire and storage formats increment their encoded major/schema field for
  incompatible representation changes. That encoded discriminator is separate
  from the contract's semantic version; the semantic version follows the
  experimental or stable lifecycle rule above. Compatible additions use the
  contract's MINOR version and explicit feature discovery where the format
  provides it.

## Lifecycle

### Experimental

An experimental contract is usable and testable but has no backward-
compatibility promise. It still MUST have an owner, version, normative behavior,
change evidence, and migration notes for intentional breaking changes.

### Stable

A stable contract follows the compatibility rules of its semantic-version line.
Breaking it requires a new major version and an ADR. Marking a contract stable
requires conformance evidence and is outside Phase 0.

### Deprecated

Deprecated is a supported migration state for a previously stable contract.
The document MUST include:

```yaml
deprecated-since: 1.3.2
removal-version: 1.4.0
migration-guide: ../MIGRATION.md#old-api
```

Every deprecated contract MUST also declare at least one machine-readable
migration route:

- `migration-guide` points to an existing repository-local Markdown document
  and, when supplied, an existing heading anchor;
- `replacement-contract` names another existing, non-deprecated `contract-id`.

Both fields MAY be present. An external URL or prose outside this metadata does
not satisfy the migration-route requirement.

`deprecated-since` and `removal-version` MUST be release versions without
prerelease or build metadata. The earliest permitted removal version is
calculated as:

```text
minimum_removal =
    (deprecated_since.major, deprecated_since.minor + 1, 0)

removal_version >= minimum_removal
```

Therefore `1.3.2` permits removal no earlier than `1.4.0`, and `0.3.2` permits
removal no earlier than `0.4.0`. Continued compatibility tests are required
until removal.

## Changing a contract

A pull request that changes a public contract MUST:

1. identify the contract ID, owner, previous/new version, and stability;
2. explain compatibility and migration impact;
3. update normative documentation and contract tests;
4. add or reference an ADR when the change is breaking or architectural;
5. record hardware, size, or protocol evidence when relevant.

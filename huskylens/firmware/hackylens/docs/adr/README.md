# Architecture Decision Records

Architecture Decision Records preserve the context and consequences of
significant HackyLens decisions. They complement normative specifications: an
ADR explains why a rule was adopted, while a specification defines the current
rule.

## When an ADR is required

Create an ADR for:

- a breaking public-contract change;
- a new dependency direction between architectural layers;
- resource ownership, lifecycle, cancellation, or cleanup semantics;
- a wire, storage, package, or project-format decision;
- a fundamental board, capability, runtime, app, or adapter decision.

Routine implementation choices, local refactoring, bug fixes that preserve a
contract, and names of private functions do not require an ADR.

## Numbering and status

Copy `template.md` to the next four-digit filename:

```text
NNNN-short-kebab-case-title.md
```

Allowed statuses are `proposed`, `accepted`, `rejected`, and `superseded`.
Accepted ADRs are historical records and MUST NOT be rewritten to change their
meaning. Replace a decision with a new ADR and connect the records with
`supersedes` and `superseded-by` metadata.

Both relationship fields contain one four-digit ADR number. A superseding ADR
MUST be accepted, the replaced ADR MUST have status `superseded`, and both
records MUST name each other. ADR numbers are unique even when filenames have
different titles.

Typographical corrections, repaired links, and clarifications that do not alter
the decision are allowed.

## Required content

Every numbered ADR contains machine-readable front matter and the sections
Context, Decision, Alternatives, Consequences, Compatibility and Migration,
Evidence, and References.

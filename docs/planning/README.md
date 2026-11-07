# Planning & Execution Workflow

This directory maintains the active planning state and project backlog for the `cfire` programming language and toolchain.

## Core Files

1. [`backlog.md`](./backlog.md): Selected and prioritized engineering backlog items.
2. [`active.md`](./active.md): The live workbench tracking the currently active milestone and verification gates.
3. [`SENIOR_ENGINEER_REVIEW_GUIDELINES.md`](../SENIOR_ENGINEER_REVIEW_GUIDELINES.md): Mandatory architectural review checklist, memory/lifetime safety rules, and verification quality gates.

---

## Operating Protocol

Whenever work is performed on this repository, the following protocol **must** be followed:

```
[backlog.md] ──(Promote)──> [active.md] ──(Implement & Verify)
```

### Quality Gates (Mandatory)
- **Gate 1 — Zero Compiler Warnings (Strict Mode)**: Code must compile cleanly with `clang++ -std=c++2b -O2 -Wall -Wextra` (zero warnings).
- **Gate 2 — Formatting Invariance**: `./cfire fmt --check examples/` must pass cleanly.
- **Gate 3 — Public Documentation Invariance**: `./cfire doc --check` must pass 100%.
- **Gate 4 — Full Conformance Suite**: `make review-check` (all unit, stdlib, LSP, and E2E test suites) must pass 100%.

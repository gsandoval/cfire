# Planning & Execution Workflow

This directory maintains the active planning state, upstream engineering feedback, and execution tracking for the `cfire` programming language and toolchain.

## Core Files

1. [`cfire-upstream.md`](./cfire-upstream.md): Upstream field report (`CF-001` through `CF-053`) collected while building **Flint** — a hybrid desktop/CLI SSH & encrypted vault application on `cfire`.
2. [`cfire-upstream-response.md`](./cfire-upstream-response.md): Complete engineering response to the Flint upstream report (`CF-001` – `CF-053`), detailing every compiler fix, runtime overhaul, newly integrated library (`ext:crypto/sodium`, `ext:keychain`, `SQLCipher` via `sqlite.open_encrypted`), and migration snippet so downstream C++ shims can be deleted.
3. [`active.md`](./active.md): The live workbench tracking the active `CF-001` – `CF-053` upstream resolution milestone and verification gates.
4. [`backlog.md`](./backlog.md): Active backlog tracking remaining long-horizon platform items (Windows WebView2 backend, custom URI scheme registration, native system tray/menu APIs, and source-map debugger integration).
5. [`SENIOR_ENGINEER_REVIEW_GUIDELINES.md`](../SENIOR_ENGINEER_REVIEW_GUIDELINES.md): Mandatory architectural review checklist, memory/lifetime safety rules, and verification quality gates.

---

## Operating Protocol

Whenever work is performed on this repository, the following protocol **must** be followed:

```
[backlog.md / cfire-upstream.md] ──(Promote)──> [active.md] ──(Implement & Test)──> [cfire-upstream-response.md]
```

### Quality Gates (Mandatory)
- **Gate 1 — Zero Compiler Warnings (Strict Mode)**: Code must compile cleanly with `clang++ -std=c++2b -O2 -Wall -Wextra` (zero warnings).
- **Gate 2 — Formatting Invariance**: `./cfire fmt --check examples/` must pass cleanly.
- **Gate 3 — Public Documentation Invariance**: `./cfire doc --check` must pass 100%.
- **Gate 4 — Full Conformance Suite**: `make review-check` (all unit, stdlib, LSP, and E2E test suites) must pass 100%.

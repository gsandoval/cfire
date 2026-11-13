# Planning & Execution Workflow

This directory maintains the active planning state and project backlog for the `cfire` programming language and toolchain.

## Core Files

1. [`backlog.md`](./backlog.md): Selected and prioritized engineering backlog items.
2. [`active.md`](./active.md): The live workbench tracking the currently active milestone and verification gates.
3. [`SENIOR_ENGINEER_REVIEW_GUIDELINES.md`](../SENIOR_ENGINEER_REVIEW_GUIDELINES.md): Mandatory architectural review checklist, memory/lifetime safety rules, and verification quality gates.
4. [`audit/PLAYBOOK.md`](./audit/PLAYBOOK.md): Repeatable, clean-context language audit (soundness, uniformity, orthogonality, tooling, security, regression testing). Run it at the end of every backlog wave and before each release. Reports go in [`audit/reports/`](./audit/reports/); the probe corpus and tools live in [`scripts/audit/`](../../scripts/audit/).

---

## Operating Protocol

Whenever work is performed on this repository, the following protocol **must** be followed:

```
[backlog.md] ──(Promote)──> [active.md] ──(Implement & Verify)
```

### Quality Gates (Mandatory)
- **Gate 1 — Zero Compiler Warnings (Strict Mode)**: Code must compile cleanly with the Makefile `CXXFLAGS` (`clang++ -std=c++2b -O2 -Wall -Wextra -Wno-parentheses-equality`), producing zero warnings.
- **Gate 2 — Formatting Invariance**: `./cfire fmt --check examples/` must pass cleanly.
- **Gate 3 — Public Documentation Invariance**: `./cfire doc --check` must pass 100%.
- **Gate 4 — Full Conformance Suite**: `make review-check` (all unit, stdlib, LSP, and E2E test suites, plus the `tests/conformance/` corpus) must pass 100%.
- **Gate 5 — Language Conformance Corpus**: every change to language semantics, diagnostics, or stdlib behavior adds or updates a file in [`tests/conformance/`](../../tests/conformance/README.md). A fix for a tracked hole removes its `xfail` line in the same change.
- **Gate 6 — Linux & Windows (pre-release, and for platform-specific changes)**: `make test-vm-linux` and `make test-vm-windows` run the CI `linux` / `windows` jobs on the working tree in local UTM VMs (see [`scripts/vm/README.md`](../../scripts/vm/README.md)). Run them before a release and after changes to platform-conditional code, the build system, or the runtime.

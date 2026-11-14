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

### Quality Gates (Mandatory): three commands
Every gate is one of three `make` targets. They are split only so that iterating on a change stays fast; nobody has to work out which tests a change needs.

| Command | What it runs | Required |
| :--- | :--- | :--- |
| `make check` | Zero C++ compiler warnings (Makefile `CXXFLAGS`, checked from the build logs even when nothing rebuilt), `cfire fmt --check examples/`, `cfire check` on every example, the C++ unit/e2e/stdlib suites, the stdlib `.cf` suite, `cfire doc --check`, the GUIDE doc-tests and the `tests/conformance/` corpus. | Every commit. The pre-commit hook runs it and blocks the commit on failure (`make hooks` installs the hooks). |
| `make check-deep` | ASan/UBSan over every C++ suite and the conformance programs, TSan over the runtime and the concurrency programs, and the fuzz-corpus replay. | Before a CF item is marked done. |
| `make check-platforms` | `make test-vm-linux` and `make test-vm-windows`: the CI `linux` / `windows` jobs on the working tree in local UTM VMs ([`scripts/vm/README.md`](../../scripts/vm/README.md)). | Before a CF item is marked done, and before a release. |

`make check-all` runs the three in order and prints a summary table. **A CF item is marked `[x]` in `active.md` only with that summary pasted into its results**; a tier that cannot run (for example no VMs on this machine) is recorded as such, never silently skipped. The gates need only clang and make: no Python.

- **Conformance corpus**: every change to language semantics, diagnostics, or stdlib behavior adds or updates a file in [`tests/conformance/`](../../tests/conformance/README.md). A fix for a tracked hole removes its `xfail` line in the same change.

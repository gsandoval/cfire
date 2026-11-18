# Planning & Execution Workflow

This directory maintains the active planning state and project backlog for the `cfire` programming language and toolchain.

## Core Files

1. [`backlog.md`](./backlog.md): Main engineering backlog (`CF-001` – `CF-197`, decisions `Q1` – `Q54`).
2. [`backlog-build-distribution.md`](./backlog-build-distribution.md): Topic-focused backlog for compiler/build performance, progressive compilation, caching, 6-target pre-compiled binary distribution, language/documentation consistency, and release/installer/upgrade automation (`CF-198` – `CF-210` and `CF-240` – `CF-245`, decisions `Q55` – `Q61` and `Q70` – `Q73`).
3. [`flint.md`](./flint.md): Topic-focused downstream audit and backlog (`CF-211` – `CF-239`, decisions `Q62` – `Q69`; future items anywhere in `docs/planning/` start at `CF-246` and `Q74`).
4. [`active.md`](./active.md): The live workbench tracking the currently active milestone and verification gates.
5. [`SENIOR_ENGINEER_REVIEW_GUIDELINES.md`](../SENIOR_ENGINEER_REVIEW_GUIDELINES.md): Mandatory architectural review checklist, memory/lifetime safety rules, and verification quality gates.
6. [`audit/PLAYBOOK.md`](./audit/PLAYBOOK.md): Repeatable, clean-context language audit (soundness, uniformity, orthogonality, tooling, security, regression testing). Run it at the end of every backlog wave and before each release. Reports go in [`audit/reports/`](./audit/reports/); the probe corpus and tools live in [`scripts/audit/`](../../scripts/audit/).

---

## Operating Protocol

Whenever work is performed on this repository, the following protocol **must** be followed:

```
[backlog.md / backlog-build-distribution.md / flint.md] ──(Promote)──> [active.md] ──(Implement & Verify)
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

---

## Release & Auto-Versioning Workflow

1. **Auto-Patch on Green `main` CI**: Pushing commits to `main` triggers [`ci.yml`](../../.github/workflows/ci.yml); once `CI` succeeds, [`release.yml`](../../.github/workflows/release.yml) automatically bumps `patch` (`./scripts/release.sh patch`), pushes `chore(release): vX.Y.(Z+1)` + tag, publishes all 6 pre-compiled bundles with `SHA256SUMS.txt`, and deploys GitHub Pages.
2. **Rebase Before Pushing Again**: Always run `git pull --rebase` locally to pick up automated `chore(release): v...` commits created on `main`.
3. **Minor / Major Releases**: Use `gh workflow run release.yml -f bump=minor` (or `bump=major` / `custom` in the GitHub Actions UI), or run `make release BUMP=minor` locally before pushing (if `VERSION` already leads the latest git tag, CI tags and publishes that exact version without double-bumping).

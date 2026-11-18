---
trigger: always_on
description: Mandatory planning and execution workflow for cfire repository
---

# `cfire` Planning & Active Work Protocol

When working in this repository:
1. **Always start with [`docs/planning/README.md`](docs/planning/README.md)**: Adhere to the authoritative operational protocol, pre-implementation quality gate, and multi-tier validation lifecycle.
2. Consult [`docs/planning/active.md`](docs/planning/active.md) for in-flight tasks and [`docs/planning/backlog.md`](docs/planning/backlog.md) for planned items.
3. Before implementing a feature or complex fix, promote it to `docs/planning/active.md` and ensure:
   - Scope is explicitly defined.
   - Open questions are articulated (prompting the user for input when choices exist).
   - Testing strategy is identified, defined, and executed across all applicable tiers:
     - **Unit Tests** (isolated component logic)
     - **Integration Tests** (compiler subsystems and pipelines)
     - **Conformance Tests** (language execution & LSP streams in `tests/unit/` and `tests/e2e/`)
     - **Manual Validation** (real-world validation in Zed/VS Code or CLI)
4. Every change must compile cleanly with `clang++ -std=c++23 -O2 -Wall -Wextra` (zero warnings via `make check-warnings`), pass `make check` 100%, and pass `./cfire doc --check`.
5. **Release & Auto-Versioning**: Green pushes to `main` trigger `.github/workflows/release.yml` to auto-bump `patch` (`0.Y.Z` -> `0.Y.(Z+1)`), publish 6-target binaries, and deploy GitHub Pages. Run `git pull --rebase` before pushing follow-up commits; use `gh workflow run release.yml -f bump=minor` (or `make release BUMP=minor`) for intentional minor/major versions.

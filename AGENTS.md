# Agent Instructions & Workflow Protocol for `cfire`

Welcome to the `cfire` codebase. These guidelines are permanent repository-level instructions that apply across all sessions, agents, and subagents.

---

## 1. Project Overview & Architecture

`cfire` (pronounced *C-fire / Sapphire*) is an ISO C++23-lowering native systems programming language combining modern Go/Rust ergonomics with zero-overhead C++23 interoperability, memory safety intents (read-only view default, `mut`, `move`), and a unified build system with a native Clang AST Header Bridge.

- `src/` — Complete ISO C++23 compiler and toolchain:
  - `lexer_types.hpp`, `lexer.hpp` — Token definitions and scanner.
  - `ast.hpp`, `parser.hpp` — `AstArena` bump allocator, AST definitions, and recursive-descent parser.
  - `sema.hpp` — Semantic analysis, CFG move/divergence checking, and parameter intent enforcement.
  - `codegen.hpp` — Zero-overhead ISO C++23 emitter.
  - `buildsys.hpp` — Native live multi-driver build engine (C/C++, CMake, Make, Ninja, in-process SHA-256 caching, CMake ejection).
  - `clang_bridge.hpp` — Clang JSON AST header inspection bridge.
  - `lsp.hpp` (`src/lsp/{json_rpc,ide_setup,server}.hpp`) — High-performance JSON-RPC 2.0 language server & multi-IDE setup (`cfire ide`).
  - `formatter.hpp` — Canonical code formatter (`cfire fmt`).
  - `refactor.hpp` — AST-backed cross-workspace symbol rename engine (`cfire refactor`).
  - `docgen.hpp` (`src/docgen/{catalog,styles,markdown,html}.hpp`) — Single-source metadata catalog and HTML/Markdown generator (`cfire doc`).
  - `main.cpp` — Standalone CLI driver entrypoint.
- `runtime/` — Modular ISO C++23 runtime & 36 built-in `core:*` / `ext:*` modules:
  - `cfire_prelude.hpp` — Core primitives (`Error`, `slice`, `async.scope`, `own[T]`, `shared[T]`).
  - `cfire_core_stdlib.hpp` & `runtime/core/*.hpp` (`runtime/core/net/{tcp,url,http,ipc,websocket}.hpp`) — Modular header implementations for all 30 `core:*` and 6 `ext:*` standard library packages.
- `docs/planning/README.md` — The authoritative operational protocol and validation process guidelines.
- `docs/planning/` — The project backlog, active development tasks, and architectural evaluations.
- `docs/planning/audit/PLAYBOOK.md` — Repeatable language audit procedure. When asked to "audit the language" or "re-run the audit", follow it end to end (read-only, no commits). It uses `scripts/audit/` (`run_probes.py`, `mutfuzz.py`, `lsp_smoke.py`, `probes/*.cf`).
- `tests/` — Modular conformance test suite (`tests/unit/`, `tests/e2e/`, `tests/test_main.cpp`, `tests/test_harness.hpp`).
- `examples/` — Canonical verified examples (`01-hello-core` through `17-webui-studio`).

---

## 2. Mandatory Planning & Development Protocol

Before starting any task or modifying code, every session, agent, and subagent **must** read and adhere to the operational rules and validation lifecycle defined in [`docs/planning/README.md`](docs/planning/README.md):

```
[docs/planning/README.md]  (Authoritative Protocol & Validation Process)
         │
         ▼
[docs/planning/backlog.md]
         │
         ▼  (Select & Promote)
[docs/planning/active.md]
         │
         ▼  (Mandatory Pre-Coding Gate: Scope + Questions + Multi-Tier Testing)
[Implementation in src/]
         │
         ▼  (Verify: make check per commit; make check-all before sign-off)
[Mark Done & Sign-Off in active.md]
```

### The Pre-Implementation Quality Gate
Before editing code for any non-trivial task:
1. **Follow [`docs/planning/README.md`](docs/planning/README.md)**: Adhere strictly to the pre-implementation and verification rules.
2. **Check `docs/planning/active.md`**: Ensure the task is recorded with an ID from [`docs/planning/backlog.md`](docs/planning/backlog.md).
3. **Define Scope**: State clearly what is in-scope and what is deferred.
4. **Resolve Open Questions**:
   - Identify tradeoffs, API designs, or edge cases.
   - **Do not guess on subjective design decisions**: Ask the user whenever requirements or preferences are ambiguous.
5. **Identify, Define, and Execute the Multi-Tier Testing Strategy**:
   Every feature or fix must explicitly define and pass tests across its applicable tiers:
   - **Unit Tests**: Granular component/function validation (parser rules, data structures, tokenizers).
   - **Integration Tests**: Inter-module compiler pipelines (e.g., driver CLI flags, eject workflows, vendor builds).
   - **Conformance Tests**: End-to-end language execution, memory safety, and LSP JSON-RPC stream assertions in `tests/unit/` and `tests/e2e/`.
   - **Manual Validation**: Live reproduction in target environments (e.g. Zed editor, VS Code, or interactive shell) confirming interactive UI, tabstops, and timing.
6. **Quality Standards** — three commands cover every gate, plus `make check-fast` for sandbox-safe inner-loop checks (details in [`docs/planning/README.md`](docs/planning/README.md)):
   - `make check-fast` — sandbox-safe inner-loop compiler, formatting, doc-sync, and offline unit/stdlib checks (skips loopback TCP and E2E suites).
   - `make check` — every commit (zero warnings, formatting, examples, unit/e2e/stdlib suites, `doc --check`, GUIDE doc-tests, conformance corpus). The pre-commit hook runs it; never bypass it with `--no-verify`.
   - `make check-deep` — sanitizers and fuzz replay.
   - `make check-platforms` — the Linux and Windows VMs running concurrently.
   - `make check-all` runs all three in order, then a summary table; its summary goes in `active.md`.

---

## 3. Naming & Commit Hygiene Rules

- **Source & header naming**: Every `.cpp` translation unit and `.hpp` header must have a domain-descriptive filename matching the subsystem it implements. Do not introduce `tu_`-prefixed translation units or `.inc` textual fragment headers.
- **Test, fixture & seed naming**: Unit test headers, test functions, conformance fixtures, golden snapshots, formatter inputs/outputs, and fuzz seeds must be named after the language rule, API, or bug behavior under test—never after internal milestone, wave, or ticket identifiers or numeric ordering prefixes outside `docs/planning/`.
- **Plain-English commits**: Commit messages must describe the change directly in plain English without `Co-Authored-By:` / attribution trailers, AI/agent/model names, or internal ticket/wave identifiers. Enable repository hooks with `make hooks` (enforced by `.githooks/commit-msg`).

---

## 4. Release & Auto-Versioning Workflow

- **Automatic Patch Releases (`0.Y.Z` -> `0.Y.(Z+1)`)**: Every push to `main` that passes `.github/workflows/ci.yml` (`CI`) triggers `.github/workflows/release.yml` via `workflow_run`. If `VERSION` on `main` equals or predates the latest `v*.*.*` git tag, the workflow runs `./scripts/release.sh patch`, pushes a `chore(release): v0.Y.(Z+1)` commit + annotated tag, builds and verifies all 6 release bundles + `SHA256SUMS.txt`, publishes the GitHub Release, and deploys `docs/` to GitHub Pages.
- **Keep Local `main` Rebased**: Because CI appends `chore(release): v...` commits after green pushes to `main`, run `git pull --rebase` before pushing subsequent local commits.
- **Intentional Minor (`0.X.0`) & Major (`X.0.0`) Releases**: Trigger `.github/workflows/release.yml` manually (`gh workflow run release.yml -f bump=minor` or GitHub Actions `Run workflow` -> `bump` dropdown: `minor`, `major`, `patch`, `custom`), or pre-bump locally before pushing via `make release BUMP=minor` (`./scripts/release.sh minor`). When `VERSION` on `main` is already strictly greater than the latest `v*.*.*` tag, the `workflow_run` release pipeline honors it without adding an extra patch bump.

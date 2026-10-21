# Agent Instructions & Workflow Protocol for `cfire`

Welcome to the `cfire` codebase. These guidelines are permanent repository-level instructions that apply across all sessions, agents, and subagents.

---

## 1. Project Overview & Architecture

`cfire` (pronounced *C-fire / Sapphire*) is an ISO C++23-lowering native systems programming language combining modern Go/Rust ergonomics with zero-overhead C++23 interoperability, memory safety intents (`in`, `mut`, `move`), and a unified build system with a native Clang AST Header Bridge.

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
- `runtime/` — Modular ISO C++23 runtime & 28 built-in `core:*` modules:
  - `cfire_prelude.hpp` — Core primitives (`Error`, `slice`, `async.scope`, `own[T]`, `shared[T]`).
  - `cfire_core_stdlib.hpp` & `runtime/core/*.hpp` (`runtime/core/net/{tcp,url,http,websocket}.hpp`) — Modular header implementations for all 28 `core:*` standard library packages.
- `docs/planning/README.md` — The authoritative operational protocol and validation process guidelines.
- `docs/planning/` — The project backlog, active development tasks, and architectural evaluations.
- `tests/` — Modular conformance test suite (`tests/unit/`, `tests/e2e/`, `tests/test_main.cpp`, `tests/test_harness.hpp`).
- `examples/` — Canonical verified examples (`01-hello-core` through `11-http-server-router`).

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
         ▼  (Verify: zero compiler warnings + make test + cfire doc --check)
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
6. **Quality Standards**:
   - Compiler builds must compile with **zero warnings** using `clang++ -std=c++23 -O2 -Wall -Wextra`.
   - `make test` must pass 100%.
   - `./cfire doc --check` must confirm documentation is synchronized.

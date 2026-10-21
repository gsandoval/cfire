# Architectural Evolution & Technical Debt Backlog

This backlog catalogs the pure ISO C++23 architectural improvements, file decomposition initiatives, and performance optimizations for the `cfire` compiler and runtime.

Tasks follow the standard operational protocol defined in [`docs/planning/README.md`](./README.md). When starting a task, promote it to [`docs/planning/active.md`](./active.md) and fulfill the pre-implementation scoping gate.

---

## Forward-Looking C++23 Modularization Backlog

### `CPP-ARCH-01`: Decompose `src/lsp.hpp` (4,335 LOC) into Modular `src/lsp/*.hpp`
- **Priority**: P1
- **Category**: Compiler Modularization / IDE Tooling
- **Target Files**: `src/lsp.hpp` $\to$ `src/lsp/protocol.hpp`, `src/lsp/hover.hpp`, `src/lsp/completion.hpp`, `src/lsp/navigation.hpp`, `src/lsp/ide_setup.hpp`
- **Problem Statement**: `src/lsp.hpp` is a 4,335-line single header containing JSON-RPC framing, workspace indexing, hover formatting, auto-completion, go-to-definition, signature help, and IDE editor configuration generators.
- **Acceptance Criteria**:
  - Split `src/lsp.hpp` into focused sub-headers under `src/lsp/` with `src/lsp.hpp` acting as a clean umbrella header.
  - Zero behavior change; 100% pass on `test_formatter_lsp_and_docgen` and `test_ide_configuration_setup`.

### `CPP-ARCH-02`: Decompose `src/docgen.hpp` (2,700+ LOC) into Catalog, Renderer, and Templates
- **Priority**: P1
- **Category**: Compiler Modularization / Documentation Engine
- **Target Files**: `src/docgen.hpp` $\to$ `src/docgen/catalog.hpp`, `src/docgen/markdown_renderer.hpp`, `src/docgen/html_generator.hpp`
- **Problem Statement**: `src/docgen.hpp` combines the 28-module `stdlib_catalog()`, diagnostic catalog, Markdown-to-HTML renderer, and HTML page templates in one large file.
- **Acceptance Criteria**:
  - Separate the structured metadata catalogs (`StdModuleDoc`, `DiagnosticDoc`) from the Markdown/HTML rendering logic.
  - `./cfire doc --check` produces byte-for-byte identical `docs/index.html`, `docs/guide.html`, `docs/reference.html`, and `docs/STDLIB_REFERENCE.md`.

### `CPP-ARCH-03`: Separate Header Declarations & Out-of-Line `.cpp` Translation Units for Incremental Compiler Builds
- **Priority**: P2
- **Category**: Build Speed / C++23 Architecture
- **Target Files**: `src/lexer.hpp`, `src/parser.hpp`, `src/sema.hpp`, `src/codegen.hpp`, `src/buildsys.hpp`, `Makefile`
- **Problem Statement**: The compiler is currently header-only (`src/*.hpp` included into `src/main.cpp`), meaning any edit to a single pass recompiles the entire compiler in one translation unit.
- **Acceptance Criteria**:
  - Extract implementation bodies into `src/*.cpp` compiled with incremental `.o` targets in `Makefile`.
  - Reduce incremental rebuild latency of `./cfire` to sub-second.

---

## Completed Architectural Milestones (Archive)

| Task ID | Component | Summary | Completed |
| :--- | :--- | :--- | :---: |
| `CPP-01` | Repo Hygiene | Removed all `.cf` self-hosting experiments (`src/*.cf`, `lib/*.cf`, `src/bootstrap/`, `bootstrap/stage0/`) and restored 100% pure C++23 `runtime/core/*.hpp` | 2026-10-20 |
| `CPP-02` | Frontend & Tooling | Restored 100% native C++23 Lexer, `AstArena` AST, Parser, Formatter, Refactor, Clang Bridge, LSP, and Buildsys | 2026-10-20 |
| `CPP-03` | Semantic Analyzer | Restored 100% native C++23 `SemaFnChecker` in `src/sema.hpp` (`1,382` $\to$ `630 LOC`), deleting 722 lines of FFI bridge wrappers | 2026-10-20 |
| `CPP-04` | Code Generator | Restored 100% native C++23 `Emitter` in `src/codegen.hpp` (`1,728` $\to$ `1,424 LOC`), deleting 311 lines of FFI bridge wrappers | 2026-10-20 |
| `CPP-05` | Docgen & CLI Driver | Restored 100% native C++23 `src/docgen.hpp` and standalone `src/main.cpp` | 2026-10-20 |
| `CPP-06` | Docs & Verification | Verified 0 diff on `docs/reference.html` and `docs/STDLIB_REFERENCE.md`, cleaned self-hosting docs, and verified full conformance suite | 2026-10-20 |
| `CLEAN-01` | Tests & CI | Intent-driven test diagnostic decomposition (`tests/unit/`, `tests/e2e/`), eliminating opaque ticket names from test output | 2026-10-20 |
| `ARCH-01` | Buildsys / SHA-256 | In-process SHA-256 streaming hash eliminating external shell spawning | 2026-10-20 |
| `ARCH-02` | Runtime / Headers | Modularized monolithic runtime into granular headers in `runtime/core/` | 2026-10-20 |
| `ARCH-03` | Tests / Decompose | Split 2,600 LOC monolithic runner into modular parallel suites (`tests/unit`, `tests/e2e`) | 2026-10-20 |
| `ARCH-04` | Memory / Bump Arena | Implemented monotonic bump `AstArena` eliminating atomic `std::shared_ptr` (+16-48% speedup) | 2026-10-20 |
| `ARCH-06` | Docgen / Pipeline | Inverted guide pipeline to make `docs/GUIDE.md` authoritative, deleting 1,400 LOC of string literals | 2026-10-20 |

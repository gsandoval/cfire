# Active Workbench: In-Flight Implementation Tasks

This document tracks items currently under active design, scoping, or implementation from [`docs/planning/backlog.md`](./backlog.md) and [`docs/planning/architectural-backlog.md`](./architectural-backlog.md).

---

## Up Next: Agent-Friendliness & Language Ergonomics (`AGENT-P0-01` – `AGENT-P2-05`)

> **Instructions for the Implementing Agent**:
> Before writing any code for an `AGENT-*` task below, read its full specification, **Critical Details, Edge Cases & Pitfalls (Do Not Miss)**, and **Section Success Instructions & Sign-Off Gate** in [`docs/planning/backlog.md`](./backlog.md). Populate the Pre-Implementation Quality Gate below for the active item/section before editing `src/` or `runtime/`.

| Task ID | Section / Priority | Component | Summary | Status | Section Gate |
| :--- | :---: | :--- | :--- | :---: | :---: |
| `AGENT-P0-01` | Section 1 (P0) | `src/main.cpp`, `src/sema.hpp`, `src/lsp/server.hpp` | **Multi-Error & Fix-It JSON in `cfire check --json`**: 1-based line/col extraction, specific `cfire/<CODE>`, resilient multi-error sema, local package resolution, and `"fixits"` array | `[DONE]` | Passed (`make review-check`) |
| `AGENT-P0-02` | Section 1 (P0) | `src/codegen.hpp`, `src/buildsys.hpp`, `src/sema.hpp` | **`#line` Source Mapping & `-fsyntax-only` Verification in `cfire check`**: Emit `#line <ln> "<file.cf>"` in C++ codegen, add native sema arity checks (`cfire/ARITY-MISMATCH`), and verify types/symbols via PCH-backed `-fsyntax-only` | `[DONE]` | Passed (`make review-check`) |
| `AGENT-P0-03` | Section 1 (P0) | `src/formatter.hpp`, `src/main.cpp` | **Complete Type & Intent Fidelity in `cfire outline` & `format_type_ref`**: Fix `&`/`&mut` and generic `[T]` in `format_type_ref`; emit full types, intents, receivers, and type params in `cfire outline` | `[DONE]` | Passed (`make review-check`) |
| `AGENT-P0-04` | Section 1 (P0) | `src/docgen/markdown.hpp`, `src/docgen.hpp` | **Catalog-Generated `cfire doc --llm` & `--check` Sync**: Synthesize `--llm` reference from `stdlib_catalog()` (all 28 modules), removing hallucinated `json.decode` / `sort.slice_by` | `[DONE]` | Passed (`make review-check`) |
| `AGENT-P1-01` | Section 2 (P1) | `src/codegen.hpp` | **`println` / `print` String Interpolation Lowering Fix**: Flatten `"Hello {name}"` into `std::println("Hello {}", name)` and single non-literal `println(x)` into `std::println("{}", x)` | `[DONE]` | Passed (`make review-check`) |
| `AGENT-P1-02` | Section 2 (P1) | `src/parser.hpp` | **Context-Aware Struct Literal Disambiguation (`allow_struct_lit_`)**: Prevent uppercase identifiers (`if x < MAX_LIMIT {`) from consuming control-flow block `{` as a struct literal | `[DONE]` | Passed (`make review-check`) |
| `AGENT-P1-03` | Section 2 (P1) | `src/ast.hpp`, `src/parser.hpp`, `src/sema.hpp`, `src/codegen.hpp`, `src/formatter.hpp` | **First-Class Expression-Oriented `match` (`MatchExpr`)**: Parse, check exhaustiveness, format, and lower `x := match v { ... }` for enums and primitives | `[DONE]` | Passed (`make review-check`) |
| `AGENT-P1-04` | Section 2 (P1) | `src/lexer.hpp`, `src/parser.hpp`, `src/sema.hpp`, `src/codegen.hpp`, `src/formatter.hpp` | **Compound Assignment & Bitwise Operators**: Support `*=`, `/=`, `%=`, `&=`, `|=`, `^=`, `<<=`, `>>=`, `&`, `|`, `^`, `~`, `<<`, `>>` with mutability checking | `[DONE]` | Passed (`make review-check`) |
| `AGENT-P2-01` | Section 3 (P2) | `src/main.cpp`, `tests/e2e/test_canonical.cpp` | **CLI `-o` / `--output` Flag & Repo Hygiene**: Parse `-o <path>` in `cfire build` and remove stray `cfire_cross_*` binaries from repo root | `[DONE]` | Passed (`make review-check`) |
| `AGENT-P2-02` | Section 3 (P2) | `src/main.cpp`, `src/sema.hpp`, `src/lsp/server.hpp` | **Headless Auto-Fix Engine (`cfire fix` & `cfire check --fix`)**: Apply reverse-ordered non-overlapping fix-its and missing `core:*` imports | `[DONE]` | Passed (`make review-check`) |
| `AGENT-P2-03` | Section 3 (P2) | `runtime/cfire_prelude.hpp`, `src/codegen.hpp`, `src/main.cpp` | **Structured Test Runner (`cfire test --json`, `--filter`, Rich `assert`)**: Location/expression-aware `assert`, non-aborting test isolation, and JSON output | `[DONE]` | Passed (`make review-check`) |
| `AGENT-P2-04` | Section 3 (P2) | `src/main.cpp`, `src/lsp/server.hpp`, `src/clang_bridge.hpp` | **Stateless CLI Inspector (`cfire inspect`)**: Query `<file>:<line>:<col>`, `cpp <header.h>`, or `core:<mod>` via CLI | `[DONE]` | Passed (`make review-check`) |
| `AGENT-P2-05` | Section 3 (P2) | `src/main.cpp` | **Tokenizer-Backed `cfire eval` & Auto-Imported `core:*` Modules**: Token-level `package`/`{}` detection and automatic `core:*` import injection for all 28 modules | `[DONE]` | Passed (`make review-check`) |

---

## Completed Task Registry (`ARCH-CPP-01` – `ARCH-CPP-05`)

| Task ID | Component | Summary | Status | Review Check Gate |
| :--- | :--- | :--- | :---: | :---: |
| `ARCH-CPP-01` | `src/sema.hpp`, `src/codegen.hpp` | **Receiver Method Name Collision Fix (`LD-NEXT-03`)**: Resolve multi-struct receiver method dispatch so defining the same method name (e.g. `area()`) on different structs works without collision | `[DONE]` | Passed (`make review-check`) |
| `ARCH-CPP-02` | `src/codegen.hpp`, `src/ast.hpp` | **Interface C++20 Concept Lowering (`LD-NEXT-01`)**: Lower `.cf` `type Interface interface { ... }` into C++20 `concept` requirements while preserving canonical e2e test invariants | `[DONE]` | Passed (`make review-check`) |
| `ARCH-CPP-03` | `src/codegen.hpp`, `runtime/cfire_prelude.hpp` | **`scope.spawn` Capture Mode (`LD-NEXT-02`)**: Bounded structured concurrency capture semantics with thread-safe loop variable copying | `[DONE]` | Passed (`make review-check`) |
| `ARCH-CPP-04` | `runtime/core/net/*`, `src/lsp/server.hpp` | **Google C++ `<charconv>` & Zero-Exception Migration (`GCPP-01`)**: Replace `std::stoi`/`std::stoul` + `try/catch` with zero-allocation, exception-free `std::from_chars` | `[DONE]` | Passed (`make review-check`) |
| `ARCH-CPP-05` | `runtime/cfire_prelude.hpp` | **Lifetime Safety Annotations (`GCPP-02`)**: Add `[[clang::lifetimebound]]` to runtime view/slice accessors (`slice`, `c_str`, `to_bytes`, `Error::message`) | `[DONE]` | Passed (`make review-check`) |

---

## Completed Architectural Tasks (`CPP-QA-01` – `CPP-QA-06`)

| Task ID | Component | Summary | Status | Review Check Gate |
| :--- | :--- | :--- | :---: | :---: |
| `CPP-QA-01` | `lexer.hpp`, `docgen/*`, `main.cpp` | DRY & Single-Source-of-Truth Consolidation | `[DONE]` | Passed (`make review-check`) |
| `CPP-QA-02` | `clang_bridge.hpp`, `main.cpp`, `buildsys.hpp` | Subprocess & Shell Argument Hardening (`shell_quote`) | `[DONE]` | Passed (`make review-check`) |
| `CPP-QA-03` | `src/lsp/json_rpc.hpp`, `src/lsp/server.hpp` | Structure-Aware JSON-RPC Field Extraction (`find_json_key`) | `[DONE]` | Passed (`make review-check`) |
| `CPP-QA-04` | `runtime/core/net.hpp`, `runtime/core/net/*` | Decompose `runtime/core/net.hpp` into `runtime/core/net/{tcp,url,http,websocket}.hpp` | `[DONE]` | Passed (`make review-check`) |
| `CPP-QA-05` | `src/docgen.hpp`, `src/docgen/*` | Decompose `src/docgen.hpp` into `src/docgen/{catalog,styles,markdown,html}.hpp` | `[DONE]` | Passed (`make review-check`) |
| `CPP-QA-06` | `src/lsp.hpp`, `src/lsp/*` | Decompose `src/lsp.hpp` into `src/lsp/{json_rpc,ide_setup,server}.hpp` | `[DONE]` | Passed (`make review-check`) |

---

## Validation Checklist (Four Gates)

- [x] **Gate 1 — Zero Compiler Warnings (Strict Mode)**: `clang++ -std=c++2b -Wall -Wextra -Wpedantic -Wshadow -Wno-parentheses-equality -Iruntime -Isrc src/main.cpp` compiles with 0 warnings.
- [x] **Gate 2 — AddressSanitizer & UndefinedBehaviorSanitizer**: Passes cleanly under `-fsanitize=address,undefined`.
- [x] **Gate 3 — Public Documentation Invariance**: `./cfire doc --check` passes and `git diff docs/reference.html docs/STDLIB_REFERENCE.md docs/index.html docs/guide.html` is empty.
- [x] **Gate 4 — Full Conformance Suite**: `make review-check` (all unit, stdlib, and e2e test suites + `cfire fmt --check`) passes 100%.

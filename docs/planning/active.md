# Active Workbench: In-Flight Implementation Tasks

This document tracks items currently under active design, scoping, or implementation from [`docs/planning/architectural-backlog.md`](./architectural-backlog.md).

---

## Active Task Registry & State Machine (`ARCH-CPP-01` – `ARCH-CPP-05`)

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

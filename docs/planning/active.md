# Active Workbench: In-Flight Implementation Tasks

This document tracks items currently under active design, scoping, or implementation from [`docs/planning/architectural-backlog.md`](./architectural-backlog.md).

---

## Task Registry & State Machine (`CPP-QA-01` – `CPP-QA-06`)

| Task ID | Component | Summary | Status | Review Check Gate |
| :--- | :--- | :--- | :---: | :---: |
| `CPP-QA-01` | `lexer.hpp`, `docgen/*`, `main.cpp` | DRY & Single-Source-of-Truth Consolidation (deduplicate keyword map, move `--llm` doc to `docgen/markdown.hpp`, deduplicate landing HTML template) | `[DONE]` | Passed (`make review-check`) |
| `CPP-QA-02` | `clang_bridge.hpp`, `main.cpp`, `buildsys.hpp` | Subprocess & Shell Argument Hardening (`shell_quote` for paths/args with spaces or metacharacters) | `[DONE]` | Passed (`make review-check`) |
| `CPP-QA-03` | `src/lsp/json_rpc.hpp`, `src/lsp/server.hpp` | Structure-Aware JSON-RPC Field Extraction (`find_json_key` skipping string literals when scanning for JSON keys) | `[DONE]` | Passed (`make review-check`) |
| `CPP-QA-04` | `runtime/core/net.hpp`, `runtime/core/net/*` | Decompose `runtime/core/net.hpp` (`1,436 LOC`) into `runtime/core/net/{tcp,url,http,websocket}.hpp` | `[DONE]` | Passed (`make review-check`) |
| `CPP-QA-05` | `src/docgen.hpp`, `src/docgen/*` | Decompose `src/docgen.hpp` (`2,767 LOC`) into `src/docgen/{catalog,styles,markdown,html}.hpp` | `[DONE]` | Passed (`make review-check`) |
| `CPP-QA-06` | `src/lsp.hpp`, `src/lsp/*` | Decompose `src/lsp.hpp` (`4,337 LOC`) into `src/lsp/{json_rpc,ide_setup,server}.hpp` | `[DONE]` | Passed (`make review-check`) |

---

## Final Validation Checklist (Verified 2026-10-20)

- [x] **Gate 1 — Zero Compiler Warnings (Strict Mode)**: `clang++ -std=c++2b -Wall -Wextra -Wpedantic -Wshadow -Wno-parentheses-equality -Iruntime -Isrc src/main.cpp` compiles with 0 warnings.
- [x] **Gate 2 — AddressSanitizer & UndefinedBehaviorSanitizer**: Compiler & test suite pass cleanly under `-fsanitize=address,undefined`.
- [x] **Gate 3 — Public Documentation Invariance**: `./cfire doc --check` passes and `git diff docs/reference.html docs/STDLIB_REFERENCE.md docs/index.html docs/guide.html` is empty.
- [x] **Gate 4 — Full Conformance Suite**: `make review-check` (all 49 unit, stdlib, and e2e test suites + `cfire fmt --check`) passes 100%.

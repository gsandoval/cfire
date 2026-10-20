# Active Workbench: In-Flight Implementation Tasks

This document tracks items currently under active design, scoping, or implementation from the `cfire` Architectural Roadmap.

---

## Task Registry & State Machine

| Task ID | Component | Status | Assignee | Review Check Gate |
| :--- | :--- | :---: | :---: | :---: |
| `CLEAN-01` | Tests & CI / Intent Diagnostics | `[COMPLETED]` | Principal Compiler Engineer | Passed (Commit `74f7c2f`) |
| `CLEAN-02` | Stdlib / Pure `.cf` Migration | `[COMPLETED]` | Principal Compiler Engineer | Passed (All 4 Quality Gates) |
| `CLEAN-03` | Compiler / Host Separation & De-duplication | `[IN PROGRESS]` | Principal Compiler Engineer | In Implementation |

---

## Active Task: `CLEAN-03` Compiler Architecture De-duplication & Clean Host Separation

### 1. Scope & Problem Statement
- **Problem**:
  1. The compiler maintains duplicate logic between handwritten C++ (`src/*.hpp`) and self-hosted `.cf` (`src/*.cf`). For instance, JSON escaping and message routing in `src/lsp.hpp` vs `src/lsp.cf`.
  2. `.cf` compiler code contains leaky `cpp::...` internal type escapes (`cpp::cfire::build::BuildPlan`, `cpp::cfire::frontend::codegen_usize_to_str`) rather than clean native `cfire` type definitions.
  3. C++ headers in `src/bootstrap/` must be standardized as purely generated compiler outputs that are never edited manually.
- **In Scope**:
  - Audit and de-duplicate common utilities across `src/lsp.hpp` and `src/lsp.cf`.
  - Replace raw `cpp::...` type leaks in self-hosted `.cf` files where native types are sufficient or wrap them cleanly behind formal FFI boundaries.
  - Standardize bootstrap generated headers and verify strict one-way generation pipeline.
  - Verify that `make dogfood` and `make review-check` continue to pass cleanly with 100% parity across all test suites.
- **Out of Scope**:
  - Total rewrite of the C++ stage0 bootstrap compiler driver (which remains the authoritative C++23 reference engine).

### 2. Testing Strategy Defined Across Tiers
- **Unit & Conformance**:
  - `./cfire test`: all 40+ unit, semantic, and e2e test suites must pass.
- **Dogfooding**:
  - `make dogfood`: re-transpile all 14 self-hosted and standard library modules (`src/*.cf` and `lib/*.cf`) with zero warnings and zero regressions.
- **Review Check**:
  - `make review-check`: pass all 4 quality gates (formatting, doc sync, dogfooding, test parity).

### 3. Implementation Plan
- [ ] **Step 1**: Audit and clean up leaky `cpp::...` escapes in self-hosted `.cf` files (`src/buildsys.cf`, `src/codegen.cf`, `src/lsp.cf`).
- [ ] **Step 2**: De-duplicate LSP protocol formatting between `src/lsp.cf` and `src/lsp.hpp`.
- [ ] **Step 3**: Re-run `make dogfood` and verify generated bootstrap headers compile cleanly without warnings.
- [ ] **Step 4**: Run full `make review-check` quality gates.
- [ ] **Step 5**: Commit and push changes.

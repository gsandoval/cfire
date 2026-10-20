# Active In-Flight Work

This document tracks items currently under active design, scoping, or implementation.

---

## The Mandatory Pre-Implementation Quality Gate

> [!IMPORTANT]
> **Rule of Engagement**: No code may be written for an active task until the following 4 sections are explicitly filled out and approved. If any design choice, API contract, or user experience is ambiguous, **ask the user directly** before proceeding.

### Required Checklist Before Coding
1. [x] **Scope Defined**: Explicit boundaries of what is included vs. excluded.
2. [x] **Open Questions Answered**: Design decisions and edge cases clarified with the user.
3. [x] **Testing Strategy Defined Across Tiers**:
   - [x] **Unit Tests**: Component/helper level test plan.
   - [x] **Integration Tests**: Inter-module compiler pipeline test plan.
   - [x] **Conformance Tests**: Full end-to-end / protocol regression assertions in `tests/conformance_test.cpp`.
   - [x] **Manual Validation**: Live interactive reproduction and CLI invocation verifying zero subprocess calls.
4. [x] **Implementation Plan Mapped**: Specific functions, files, and data structures identified.

---

## Active Task: `ARCH-03` Decompose Monolithic Conformance Test Runner

### 1. Scope & Problem Statement
- **Problem**: `tests/conformance_test.cpp` compiles all 30 test suites (unit tests, language diagnostics, end-to-end examples, and 12 self-hosting dogfood benchmarks) in a single **4,598-line translation unit**. A clean rebuild takes 15–20 seconds on modern multicore CPUs, bottlenecking developer TDD iteration speed.
- **In Scope**:
  - Decompose `tests/conformance_test.cpp` into modular suites under `tests/`:
    - `tests/test_harness.hpp` (shared `require()`, `RunResult`, `ScopedTempDir`, `run_cfire()`, `run_cfire_source()`, runner prototypes)
    - `tests/unit/test_frontend.cpp` (Lexer, Parser, AST TypeRef, Formatter, Refactor rename)
    - `tests/unit/test_sema.cpp` (Intent & move diagnostics, alias checks, type checker rules)
    - `tests/unit/test_stdlib.cpp` (HTTP, WebSocket, SQLite, Blake3/AES/HMAC/SHA-256, Sync Context, Eval, Vendor)
    - `tests/e2e/test_canonical.cpp` (Examples 01–11 end-to-end execution, Raylib ejection, Cross-compilation)
    - `tests/benchmarks/test_selfhost.cpp` (12 self-hosting dogfood parity & throughput benchmarks)
    - `tests/test_main.cpp` (Main test entrypoint orchestrating the test suites)
  - Update `Makefile` with parallel compilation support (`-j`) and granular test targets (`make test-unit`, `make test-stdlib`, `make test-selfhost`, `make test-e2e`).
  - Keep `tests/conformance_test.cpp` as an umbrella forwarder or alias to guarantee zero breaking changes for existing scripts.
- **Out of Scope**:
  - Rewriting existing test assertion logic.
  - Removing any of the existing 30 test suites.

### 2. Open Questions & Architectural Clarifications
- **Shared Test State**: `g_failures` is declared as `inline std::atomic<int> g_failures{0};` in `tests/test_harness.hpp` to ensure thread safety across suites.
- **Execution Order**: Retain the exact test execution sequence so that failure logs and benchmark outputs remain familiar and reproducible.
- **Granular CLI Targets**: Enable running individual suites (e.g., `make test-unit` compiling only `test_frontend.cpp` and `test_sema.cpp` in $<2$ seconds).

### 3. Testing Strategy Defined Across Tiers
- **Compilation Speed Benchmark**:
  - Measure clean compile time of single TU vs parallel compile of decomposed objects (`make -j8 test`).
- **Regression Parity**:
  - Verify all 30 suites execute and pass with zero failures.
  - Verify `make review-check` passes cleanly.

### 4. Implementation Plan
- **Shared Harness**: Extract `tests/test_harness.hpp`.
- **Directory Structure**: Create `tests/unit/`, `tests/e2e/`, `tests/benchmarks/`.
- **Suite Decomposition**: Extract test suites into modular `.cpp` files.
- **Entrypoint**: Create `tests/test_main.cpp` and update `tests/conformance_test.cpp`.
- **Makefile**: Update `Makefile` rules for parallel object compilation and granular targets.

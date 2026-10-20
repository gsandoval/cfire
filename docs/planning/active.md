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

## Active Task: `ARCH-05` Deprecate Dual-Engine C++ Fallback Headers

### 1. Scope & Problem Statement
- **Problem**:
  Maintaining dual implementations of the compiler in both C++ (`src/*.hpp`, 17,610 lines) and `cfire` (`src/*.cf`) doubles maintenance overhead, introduces cognitive confusion over which file is authoritative, and bloats the production repository.
- **In Scope**:
  - Snapshot the stable Stage 0 C++ implementations into `bootstrap/stage0/` as a permanent bootstrapping artifact.
  - Turn `src/*.hpp` into clean, thin dispatch wrappers forwarding directly to `src/bootstrap/*.generated.hpp` (generated from `src/*.cf`).
  - Add `make bootstrap-from-stage0` to build a working bootstrap compiler purely from `bootstrap/stage0/` without needing pre-existing binaries.
  - Update `test_selfhost.cpp` benchmarks to include `bootstrap/stage0/` as the native C++ baseline for continuous throughput comparisons.
- **Out of Scope**:
  - Changing the behavior or grammar of the compiler.
  - Modifying `runtime/core/`.

### 2. Open Questions & Architectural Clarifications
- **Why do we need .cf and .hpp headers?**:
  The canonical code of the `cfire` compiler is written entirely in `.cf` (`src/*.cf`). The C++ host compiler (`clang++`/`g++`) requires C++ headers to compile `cfire`. `cfire emit-cpp` compiles `.cf` into `src/bootstrap/*.generated.hpp`. The handwritten `src/*.hpp` were legacy bootstrap code from before the compiler was self-hosted. ARCH-05 eliminates this duality.
- **Stage 0 Archive**:
  `bootstrap/stage0/` stores the pure C++ implementation so anyone on a clean machine without `cfire` can compile `cfire` from scratch using standard C++23.

### 3. Testing Strategy Defined Across Tiers
- **Compilation Check**:
  - `make clean && make build` using generated headers.
  - `make bootstrap-from-stage0` builds a functional binary.
- **Regression Parity**:
  - `make review-check` passes all 4 quality gates.
  - All 12 dogfood benchmarks in `tests/benchmarks/test_selfhost.cpp` continue to pass.

### 4. Implementation Plan
- **Step 1**: Create `bootstrap/stage0/` and copy the stable C++ implementations.
- **Step 2**: Add `bootstrap-from-stage0` target in `Makefile`.
- **Step 3**: Simplify `src/*.hpp` to forward directly to self-hosted generated headers.
- **Step 4**: Run full `make review-check` and verify zero regressions.

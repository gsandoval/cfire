# Senior Engineer Review Guidelines & Change Verification Protocol

This document establishes the mandatory architectural and code review standards for the `cfire` programming language compiler, runtime, standard library, and toolchain. 

Every contribution, refactoring, or feature addition **must** satisfy this protocol before being committed to `main`.

---

## 1. Operational Review Tiers

```
┌────────────────────────────────────────────────────────────────────────┐
│                        CHANGE VERIFICATION TIERS                       │
├────────────────────────────────────────────────────────────────────────┤
│ TIER 1: Automated Gate (Every commit / PR)                             │
│   • make test (40+ conformance suites pass, 0 warnings)                │
│   • ./cfire doc --check (Documentation sync check)                     │
│   • ./cfire fmt --check examples/ (Canonical code formatting check)    │
├────────────────────────────────────────────────────────────────────────┤
│ TIER 2: Senior Engineer Code Review (Every PR / Architectural change) │
│   • Section A: ABI & Lowering Correctness                              │
│   • Section B: Memory & Lifetime Safety                                │
│   • Section C: Language Semantics & Intent Soundness                   │
│   • Section D: Error Handling & Performance Invariants                 │
│   • Section E: Toolchain & Standard Library Completeness               │
├────────────────────────────────────────────────────────────────────────┤
│ TIER 3: Milestone Architectural Audit (Periodic / Major milestones)   │
│   • Complexity hotspot sweeps (LOC, nesting depth, coupling)           │
│   • Technical debt backlog review (docs/planning/architectural-backlog)│
│   • Header modularization & compilation speed checks                   │
└────────────────────────────────────────────────────────────────────────┘
```

---

## 2. Senior Engineer Code Review Checklist

Reviewers must inspect and verify the following criteria for all proposed code changes:

### A. ABI & Lowering Correctness
1. **Zero-Overhead Guarantee**:
   - Does emitted C++23 code avoid hidden heap allocations, redundant clones, or runtime dynamic boxing?
   - Structs must lower to plain C++23 aggregates (`struct S { ... };`).
   - Enums must lower to efficient tagged unions (`std::variant` with minimal footprint).
2. **Standard ISO C++23 Compliance**:
   - Does generated code compile cleanly without compiler-specific extensions (`__attribute__`, MSVC-only pragmas)?
   - Must build warning-free under both Clang 17+ and GCC 13+ with:
     ```bash
     -std=c++2b -O2 -Wall -Wextra -Wno-parentheses-equality
     ```
3. **CMake Ejection Fidelity**:
   - Does `cfire eject <file.cf> [out_dir]` produce a standalone build tree that successfully compiles with standard `cmake --build` without requiring `cfire` installed?

### B. Memory & Lifetime Safety
1. **String & Slice View Lifetimes**:
   - `str` in parameter position lowers to `const std::string&` (owning/safe) unless explicitly marked as borrowed `&str` (lowering to `std::string_view`).
   - Verify that any function returning `&str` or `[]T` does **not** return a view into a temporary local object or string concatenation.
2. **Move Insertion & Last-Use Soundness**:
   - When a parameter is marked `move`, codegen must emit `std::move(x)` exclusively at its definite last read site.
   - Slices (`[]T`) passed by value should only be moved if the container is an owning vector; borrowed spans (`std::span`) must be copied by value (2 pointers).
3. **Raw Pointer & Smart Pointer Boundary**:
   - `own[T]` must lower to `std::unique_ptr<T>`.
   - `shared[T]` must lower to `std::shared_ptr<T>`.
   - Raw pointers (`*T`, `*mut T`) are reserved strictly for foreign C/C++ interop; verify that nullability checks or guard assertions exist before dereference.

### C. Language Semantics & Intent Soundness
1. **Parameter Intent Orthogonality**:
   - `in` (default): immutable borrow. Call site: `f(x)`.
   - `mut`: mutable in-place reference. Call site: `f(mut x)`.
   - `out`: output parameter. Call site: `f(out x)` (warn with `cfire/DEPRECATED-OUT-PARAM` recommending tuples).
   - `move`: ownership transfer. Call site: `f(move x)`.
   - Ensure call-site annotation matches signature intent; sema must reject mismatches (`cfire/MUT-CALLSITE`, `cfire/IMMUTABLE-MUT-ARG`).
2. **Mutation Alias Exclusivity (`cfire/MUT-ALIAS`)**:
   - Verify that passing the same variable (or subpath of a variable) to both a `mut` argument and any other argument in the same call is statically rejected.
3. **Use-After-Move Detection (`cfire/USE-AFTER-MOVE`)**:
   - Verify that reading a variable after a `move` expression within the same scope or across loop iterations triggers a compile error.
4. **Definite Divergence for Fallible Fallbacks (`cfire/NON-DIVERGING-OR-BLOCK`)**:
   - In `val := fallible() or err { ... }`, the block **must** terminate via `return`, `panic()`, or process exit.

### D. Error Handling & Performance Invariants
1. **Arena-Backed AST Allocation**:
   -Recursive AST expressions (`Expr`) and statements (`Stmt`) are allocated in `AstArena` inside `FileAST`, avoiding atomic reference-counting overhead.
2. **No Unchecked Exceptions or Panics**:
   - All errors must return `std::expected<T, std::string>`.
   - No bare `throw` statements in `src/` or `runtime/`.
   - No unchecked `.value()` calls on optionals or expected values without preceding guard checks.

### E. Toolchain & Standard Library Completeness
1. **LSP Integration**:
   - Any new syntax construct or AST node must update `src/lsp.hpp`:
     - Hover documentation (`textDocument/hover`)
     - Auto-complete snippets (`textDocument/completion`)
     - Semantic token highlight classes (`textDocument/semanticTokens/full`)
2. **Documentation Invariance**:
   - If language syntax, CLI flags, or standard library APIs change, update the single-source-of-truth in `src/docgen.hpp` and `docs/GUIDE.md` and run:
     ```bash
     ./cfire doc
     ./cfire doc --check
     ```
   - Build **must fail** if docs are out of sync.
3. **Canonical Formatting**:
   - Format all example `.cf` files with `./cfire fmt examples/`.
   - Verify with `./cfire fmt --check examples/`.

---

## 3. Review Cadence & When to Run

| Trigger Event | Required Tier | Action Items |
| :--- | :---: | :--- |
| **Every local code change** | **Tier 1** | Run `make review-check`. |
| **Pull Request / Pre-Merge** | **Tier 1 + 2** | Execute the Senior Engineer Review Checklist; inspect git diff for code smells or unnecessary allocations. |
| **Standard Library Expansion** | **Tier 2** | Verify C++ lowering, error expected return (`T!`), POSIX / platform safety, and add conformance tests in `tests/`. |
| **Milestone Completion** | **Tier 3** | Conduct deep architectural audit against `docs/planning/architectural-backlog.md`; measure compile time deltas. |

---

## 4. Enforcement Script (`make review-check`)

The `review-check` target in `Makefile` automates the verification gate:

```makefile
review-check: build
	@echo "=== [1/3] Checking Canonical Code Formatting ==="
	./cfire fmt --check examples/
	@echo "✓ Canonical code formatting verified"
	@echo "=== [2/3] Checking Documentation Synchronization ==="
	./cfire doc --check
	@echo "✓ Documentation guides and stdlib references synchronized"
	@echo "=== [3/3] Running Full Conformance Test Suite ==="
	$(MAKE) test
	@echo "✓ Conformance test suites passed"
```

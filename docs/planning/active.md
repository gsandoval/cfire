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

## Active Task: `ARCH-04` Arena Memory Allocator for AST Nodes

### 1. Scope & Problem Statement
- **Problem**:
  AST nodes currently use `using ExprPtr = std::shared_ptr<Expr>` and `using StmtPtr = std::shared_ptr<Stmt>`.
  Every AST expression and statement allocation invokes `std::make_shared` (allocating a separate heap control block and atomic ref-counters).
  This causes cache fragmentation across hundreds of dispersed heap allocations, 16-byte vector elements, and atomic reference count overhead during AST cloning and traversal.
- **In Scope**:
  - Implement `AstArena`: a high-performance monotonic bump allocator in `src/ast.hpp` allocating 64KB contiguous memory blocks with scoped destruction and batch reset.
  - Transition `ExprPtr` and `StmtPtr` in `src/ast.hpp` from `std::shared_ptr<T>` to raw non-atomic owning pointer aliases (`Expr*` and `Stmt*`).
  - Introduce `std::shared_ptr<AstArena> arena` in `FileAST` with thread-local `ArenaScope` to tie AST node lifetimes strictly to the owning file AST.
  - Keep `make_expr()` and `make_stmt()` APIs identical so that parser and sema call sites in both C++ (`src/parser.hpp`, `src/sema.hpp`) and self-hosted `.cf` (`src/parser.cf`, `src/sema.cf`) require zero syntax modifications.
  - Measure parser throughput and memory locality improvements in `tests/benchmarks/test_selfhost.cpp`.
- **Out of Scope**:
  - Changing `TypeRef` recursive pointer mechanics (which remains lightweight `std::shared_ptr<TypeRef>`).
  - Modifying grammar or AST node fields.

### 2. Open Questions & Architectural Clarifications
- **Lifetime & Ownership**:
  `FileAST` owns `std::shared_ptr<AstArena> arena`. As `FileAST` is moved into `CheckedFile`, the arena remains alive. When `CheckedFile` drops out of scope, the entire arena releases its 64KB blocks in batch.
- **Node Destruction**:
  Non-trivially-destructible AST nodes (those containing `std::string` or `std::vector`) register a lightweight destructor trampoline in `AstArena`. On arena reset, destructors run in LIFO order before free, guaranteeing zero memory leaks.
- **Fallback Safety**:
  If `make_expr()` is called outside an active `ArenaScope` (e.g. in a unit test), a thread-local fallback arena ensures safe allocation without crashing or leaking.

### 3. Testing Strategy Defined Across Tiers
- **Unit Tests**:
  - `AstArena` allocation alignment, capacity expansion across multiple chunks, and proper destructor execution.
- **Integration Tests**:
  - Lexer -> Parser -> Sema -> Codegen compiler pipeline end-to-end with arena-allocated nodes.
- **Conformance Tests**:
  - All 30 conformance test suites (`test-unit`, `test-stdlib`, `test-e2e`, `test-selfhost`) pass with 100% parity.
- **Performance Benchmark**:
  - Validate parser throughput in `test_selfhost_parser_parity_and_benchmark` and ensure zero memory leaks.

### 4. Implementation Plan
- **Step 1**: Implement `AstArena` and `ArenaScope` in `src/ast.hpp`.
- **Step 2**: Update `ExprPtr` to `Expr*` and `StmtPtr` to `Stmt*` in `src/ast.hpp`.
- **Step 3**: Add `std::shared_ptr<AstArena> arena` to `FileAST` and instrument `parse_source()` in `src/parser.hpp` and `src/parser.cf` with `ArenaScope`.
- **Step 4**: Verify clean compilation, re-transpile self-hosted compiler via `make dogfood`, run benchmarks, and execute `make review-check`.

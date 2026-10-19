# Language Design & Frontend Architecture Evaluation

This document provides a comprehensive, rigorous critique of the `cfire` frontend language design and its lowering to ISO C++23. It applies programming language design theory (orthogonality, regularity, cognitive load, compositionality, sound type systems) alongside lessons learned and community perceptions from modern systems languages (**Go**, **Rust**, **C#**, **Zig**, **Swift**, and **Odin**).

---

## 1. Executive Summary & Design Foundations

`cfire` aims to deliver high-productivity systems programming by pairing a clean, structural skin with transparent, zero-overhead lowering to ISO C++23 (`std::expected`, `std::variant`, `std::span`, `std::string_view`, concepts, and RAII).

A rigorous sweep of the compiler frontend ([`src/parser.hpp`](../../src/parser.hpp), [`src/sema.hpp`](../../src/sema.hpp), [`src/codegen.hpp`](../../src/codegen.hpp), [`src/lexer.hpp`](../../src/lexer.hpp), and [`runtime/cfire_prelude.hpp`](../../runtime/cfire_prelude.hpp)) reveals that while the ergonomic vision is compelling, the current implementation relies heavily on syntactic shortcuts, line-number tracking, and ad-hoc lowering rather than a principled semantic model.

### Key Theoretical Pillars Examined
1. **Orthogonality**: Do language constructs combine without arbitrary restrictions or hidden context-dependent traps?
2. **Regularity & Consistency**: Are declarations, expressions, and type annotations uniform across scopes and call sites?
3. **Cognitive Budget**: Does the language minimize visual noise and redundant syntactic paths?
4. **Semantic Soundness & Leaky Lowering**: Does the high-level skin leak underlying C++23 UB, dangling views, or uninitialized state?
5. **Modern Language Lessons**:
   - **Go**: Multiple return values vs. tuples; error handling ceremony; zero-value surprises vs. uninitialized state.
   - **Rust**: Move-checking via Control Flow Graphs (CFG/NLL) vs. AST traversal; postfix `?` vs. prefix `try`.
   - **C#**: The historical regret of `out` parameters vs. deconstructed tuples; nullability propagation.
   - **Zig**: `catch` divergence enforcement; slice allocation transparency vs. implicit decaying views.
   - **Swift**: In-place mutation (`inout`) vs. multiple intents; Law of Exclusivity enforcement.
   - **Odin**: Minimal keyword sets; clean generic syntax.

---

## 2. Detailed Findings & Architectural Gap Analysis

### 2.1 Orthogonality & Compositional Traps

#### LD-01: Type System Non-Orthogonality — Result/Option Stripping in Containers & Fields
- **The Issue**: In `src/ast.hpp`, `is_fallible` (`!`) and `is_optional` (`?`) are ad-hoc boolean flags on `TypeRef`. In `src/codegen.hpp`, `lower_owning_type()` completely ignores these flags—only `lower_return_type()` inspects them.
- **Impact**: Declaring `Item: str?` or `Results: []i32!` in a struct silently drops the optional/fallible wrapper, emitting raw `std::string` and `std::vector<int32_t>`.
- **PL Principle**: **Uniformity of Types**. A type must retain identical semantics whether placed in a return position, a struct field, a container element, or a local binding.
- **Modern Parallel**: Go's historical lack of first-class tuples (multiple returns only allowed at function boundary; cannot make `chan (int, error)` or `[](int, error)`).
- **Remedy**: Promote `Optional` and `Fallible`/`Result` to recursive, first-class AST type nodes (`TypeRef`) and unify lowering into a single recursive function.

#### LD-02: Sum Type Ownership Entrapment — Match Arms Bind Immutable References
- **The Issue**: `match` arm code generation lowers variants to `std::visit(cfire::overloaded{ ... }, target._v)`, where each visitor lambda takes `const Enum::_Variant&` and binds payload fields as `const auto&`.
- **Impact**: It is impossible to move an `own[T]` or move-only payload out of a matched variant (`return move b` fails to compile because `b` is `const`).
- **PL Principle**: **Move Orthogonality & Value Semantics**. Matching on an rvalue or moved union must permit moving payload fields.
- **Modern Parallel**: Rust distinguishes `ref`, `ref mut`, and value-consuming patterns in `match`. Swift provides `consuming` pattern bindings.
- **Remedy**: Inspect the value category of the match target; emit `[&](Enum::_Variant&& _m)` visitors when matching rvalues or moved variables.

#### LD-03: Scoped Concurrency Escapes & Destruction Inversion in `async.scope`
- **The Issue**: `async.scope |scope| { ... }` lowers to passing a lambda to `cfire::async::run_scope`.
- **Impact**:
  1. An early `return` inside `async.scope` exits *only* the setup lambda, not the enclosing function. Execution resumes past the scope block, violating structured concurrency invariants.
  2. `defer` cleanup inside `async.scope` executes when the setup lambda returns, *before* background threads finish joining in `scope.join_all()`, causing use-after-free data races.
- **PL Principle**: **Lexical Scoping Integrity & Structured Concurrency Invariants**.
- **Modern Parallel**: Kotlin (`coroutineScope`), Swift (`withTaskGroup`), and Rust (`std::thread::scope`) guarantee stack frame longevity and prevent unjoined returns.
- **Remedy**: Reject raw lambda lowering; generate structured scope control code with return propagation and ensure `defer` runs after `scope.join_all()`.

---

### 2.2 Leaky Lowering & Lifetime Hazards

#### LD-04: Invisible Borrow Dangling via Auto-View Lowering
- **The Issue**: `in` parameters typed `str` or `[]T` automatically lower to `std::string_view` and `std::span<const T>`.
- **Impact**: Passing a temporary string (e.g. `f(str.to_upper("test"))`) binds a non-owning view to a temporary that is destroyed at the end of the full expression. If the function returns or stores this view, it dangles immediately.
- **PL Principle**: **Abstraction Soundness**. High-level value types must not implicitly decay into unsafe non-owning pointers without lifetime tracking.
- **Modern Parallel**: C++ committee's well-documented regret regarding implicit conversions from temporary `std::string` to `std::string_view`. Zig explicitly separates slices from allocations.
- **Remedy**: Explicitly distinguish owned types (`str`, `[]T`) from borrowed view references (`&str`, `&[]T`). Only lower to `string_view` / `span` when the programmer explicitly borrows with `&`.

#### LD-05: Soundness Collapse in Move Checker — Line-Number-Based Analysis
- **The Issue**: In `src/sema.hpp`, `moved_at_line_` and `last_use_move` record moves and last uses using AST source line numbers (`int line`).
- **Impact**:
  1. Loops: Moving inside a loop records line $L$; on iteration 2, runtime moves a moved-from object without compiler detection.
  2. Reassignment: Re-initializing `x = make_new()` after `sink(move x)` fails because the assignment target is checked as a read of a moved variable.
  3. Branches: An `if` branch move marks the variable dead even when taking the alternate branch.
- **PL Principle**: **Control-Flow Soundness & Definite Assignment Integrity**.
- **Modern Parallel**: Rust replaced AST-based lexical borrow checking with Mid-level Intermediate Representation (MIR) Non-Lexical Lifetimes (NLL) specifically to solve loop, branch, and reassignment soundness.
- **Remedy**: Construct a basic Control Flow Graph (CFG) and solve standard forward liveness/assignment dataflow equations.

#### LD-06: Syntactic Mutability Aliasing Illusion (`cfire/MUT-ALIAS`)
- **The Issue**: `cfire/MUT-ALIAS` only checks bare `IdentExpr` nodes at call sites.
- **Impact**: Wrapping arguments in field access (`p.data`), indexing (`arr[0]`), or receiver calls completely bypasses exclusivity checking.
- **PL Principle**: **Invariant Integrity**. Checks that provide a false sense of security while trivial projections bypass them invite subtle data corruption.
- **Modern Parallel**: Swift's Law of Exclusivity (SE-0176) verifies path disjointness statically and inserts runtime checks where static proof is undecidable.
- **Remedy**: Implement projection-path disjointness verification (`RootVar + Projections`).

---

### 2.3 Syntax Regularity & Cognitive Budget

#### LD-07: Declaration Sigil Proliferation & Keyword Redundancy
- **The Issue**: `cfire` currently allows 7 competing ways to declare a variable (`:=`, `mut :=`, `x: T = val`, `mut x: T = val`, `var x: T = val`, `var mut x: T = val`, `var x = val`), while package-level scope only permits `var`.
- **Impact**: Needless cognitive load, style debates, and visual fragmentation.
- **PL Principle**: **Cognitive Budget & Minimal Redundancy**. One obvious, consistent way to declare bindings.
- **Modern Parallel**: Rust unified around `let` / `let mut`; Zig unified around `const` / `var`; Odin unified around `:=` and `::`.
- **Remedy**: Drop `var` completely and unify declarations into `x := val`, `mut x := val`, `x: T = val`, and `mut x: T = val` uniformly across local and file scopes.

#### LD-08: Lexical Disconnect in Foreign Escape Hatch (`cpp` vs `cplusplus`)
- **The Issue**: `docs/GUIDE.md` instructs developers to use `cpp { ... }`, but the lexer only tokenizes `cplusplus` as `TokenKind::CppBlock`. Typing `cpp { ... }` causes a syntax error.
- **Impact**: Onboarding friction and broken documentation examples.
- **PL Principle**: **Documentary Fidelity & Principle of Least Astonishment**.
- **Remedy**: Unify tokenization to accept `cpp { ... }` natively in `lexer.hpp`.

#### LD-09: Generic Parsing Ambiguity & Scalability Wall
- **The Issue**: Call-site generic arguments `f[T]()` are distinguished from indexing `arr[i]` using a rigid 4-token lookahead (`[`, `Ident`, `]`, `(`).
- **Impact**: Multi-argument generics (`make_pair[i32, str]()`) and qualified type names (`cast[std::string]()`) trigger syntax errors, while array indexing on function pointers (`table[index]()`) can be misparsed.
- **PL Principle**: **Grammar Regularity & Unambiguous Disambiguation**.
- **Modern Parallel**: Rust's turbofish `f::<T>()` was created to disambiguate expression-level type parameters cleanly without arbitrary lookahead.
- **Remedy**: Introduce unambiguous expression turbofish `f::[T]()` or robust bracketed type parsing.

#### LD-10: Compiler-Coupled Serialization & Monolithic Intrusion
- **The Issue**: Struct tags (`json`, `db`, `cli`, `help`) are parsed via hardcoded regex in the compiler frontend, and `src/codegen.hpp` directly emits hardcoded C++ code referencing third-party libraries (`simdjson`, `sqlite3`).
- **Impact**: Custom tags (`yaml`, `toml`, `env`) are discarded, and the compiler core is tightly coupled to specific database and JSON engines.
- **PL Principle**: **Separation of Concerns & Extensibility**.
- **Modern Parallel**: Go's `reflect.StructTag`, Zig's `comptime` type inspection, and Rust's derive macros.
- **Remedy**: Emit generic struct reflection descriptors or compile-time metadata structures that libraries can consume modularly.

---

### 2.4 Error Handling Ergonomics & Symmetry

#### LD-11: Expression-Statement Asymmetry in `try` Lowering
- **The Issue**: `try` is syntactically an expression, but codegen lowers it as a statement-level macro. When nested inside sub-expressions (e.g. `10 + try f()`), the compiler completely drops `try`, emitting invalid C++.
- **Impact**: Inscrutable C++ compiler errors when attempting idiomatic functional composition.
- **PL Principle**: **Compositionality of Expressions**. Any valid expression must compose at any syntactic depth.
- **Modern Parallel**: Rust's postfix `?` and Zig's `try` compose cleanly inside any expression.
- **Remedy**: Lower `try` via statement-expressions `({ ... })` or normalize expressions prior to emission.

#### LD-12: Undefined Behavior in Non-Diverging `or err { ... }` Blocks
- **The Issue**: If an `or err { ... }` error handling block does not return or diverge, codegen unconditionally emits `auto val = std::move(*tmp);` immediately following the `if (!tmp.has_value())` block.
- **Impact**: Dereferencing an empty `std::expected` results in immediate undefined behavior or runtime crash.
- **PL Principle**: **Control-Flow Soundness & Total Value Guarantee**.
- **Modern Parallel**: Zig requires `catch` blocks to evaluate to a fallback value of type `T` or diverge (`return`, `panic`).
- **Remedy**: Add semantic termination analysis in `sema.hpp` enforcing that `or err { ... }` blocks either diverge or yield a fallback value.

---

### 2.5 Parameter Intents

#### LD-13: The `out` Parameter Anti-Pattern & Soundness Void
- **The Issue**: `out x: T` lowers identically to `mut x: T` (`T&`), but lacks definite assignment verification (early returns leave caller variables uninitialized). Furthermore, callers are forced to pre-initialize variables before passing them with `out`.
- **Impact**: Destroyed functional composition, boilerplate initialization, and risk of reading uninitialized state.
- **PL Principle**: **Modern Idiomatic Functional Composition**.
- **Modern Parallel**: C# spent years mitigating `out` with `out var` before moving toward tuple returns. Swift explicitly avoided `out`, using only `inout`.
- **Remedy**: Eliminate `out` parameters completely in favor of first-class multi-return tuples `fn parse() -> (f64, f64)!`.

#### LD-14: Phantom Parameter Intent `forward`
- **The Issue**: `forward` is accepted in function parameter declarations, but lowers to `const T&` (a plain read-only borrow) and is rejected at call sites.
- **Impact**: Confusing dead language surface.
- **PL Principle**: **Truth in Specification**.
- **Remedy**: Remove `forward` from the grammar until universal forwarding references and templates are formally specified.

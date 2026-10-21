# Project Backlog

This backlog lists prioritized improvements, features, and tooling upgrades for `cfire`.
When selecting an item for implementation, promote it to [`active.md`](./active.md) and complete the pre-implementation scoping gate before writing code.

---

## High Priority (P1)

### `LSP-01`: Snippet Completions for Core Language Constructs `[Completed]`
- **Status**: Completed
- **Category**: Language Server / Developer Ergonomics
- **Target Files**: `src/lsp.hpp`
- **Rationale**: Typing `fn`, `for`, `match`, or `type` currently only suggests keywords as plain text labels. Developers expect tabstop-enabled templates so they don't have to write boilerplate syntax by hand.
- **Detailed Requirements**:
  1. Set `insertTextFormat: 2` (Snippet format) in completion responses when completing keywords:
     - `fn`: `fn ${1:name}(${2:params}) -> ${3:void} {\n\t${0}\n}`
     - `type struct`: `type ${1:Name} struct {\n\t${0}\n}`
     - `type enum`: `type ${1:Name} enum {\n\t${0}\n}`
     - `match`: `match ${1:expr} {\n\t${2:case} => ${0}\n}`
     - `for in`: `for ${1:item} in ${2:iter} {\n\t${0}\n}`
     - `defer`: `defer ${0}`
     - `cplusplus`: `cplusplus {\n\t${0}\n}`
  2. Declare `snippetSupport: true` capability negotiation handling.
- **Acceptance Criteria**:
  - In Zed / VS Code, typing `fn` and selecting the snippet completion expands the full skeleton with tabstops.
  - Conformance test in `tests/conformance_test.cpp` verifies `insertTextFormat: 2` and snippet template structure.

---

### `LSP-02`: Go to Type Definition (`textDocument/typeDefinition`) `[Completed]`
- **Status**: Completed
- **Category**: Language Server / Navigation
- **Target Files**: `src/lsp.hpp`
- **Rationale**: When the cursor is on an instance variable (e.g. `audit` in `record("INFO", mut audit, ...)`), pressing "Go to Type Definition" should jump directly to `type AuditLog struct {` rather than where `audit` was declared.
- **Detailed Requirements**:
  1. Add `"typeDefinitionProvider": true` to `initialize` capabilities.
  2. Handle `textDocument/typeDefinition` RPC method:
     - Identify symbol at position.
     - Resolve symbol's inferred or explicit `.cf` type (`resolve_qualifier_type` or `env` binding lookup).
     - Locate the declaration line of the resolved struct/enum either in the current AST or in imported local packages (`ws.local_packages`).
     - Return `Location` `{ "uri": target_uri, "range": { ... } }`.
- **Acceptance Criteria**:
  - Invoking `textDocument/typeDefinition` on variable `audit` returns the URI and range pointing to line 3 of `examples/01-hello-core/main.cf` (`type AuditLog struct`).
  - Unit test added in `tests/conformance_test.cpp`.

---

### `LSP-03`: Semantic Tokens Provider (`textDocument/semanticTokens/full`) `[Completed]`
- **Status**: Completed
- **Category**: Language Server / Syntax Highlighting
- **Target Files**: `src/lsp.hpp`
- **Rationale**: Standard regex/TextMate grammars cannot distinguish mutable variables, parameter intents (`in`, `mut`, `move`, `out`), or native C++ bridge calls. Semantic tokens provide high-fidelity coloring in Zed and VS Code.
- **Detailed Requirements**:
  1. Add `semanticTokensProvider` to `initialize` capabilities with legend:
     - Token types: `type`, `struct`, `enum`, `parameter`, `variable`, `function`, `method`, `keyword`, `modifier`, `namespace`.
     - Token modifiers: `declaration`, `definition`, `readonly`, `modification` (for `mut`), `defaultLibrary`.
  2. Implement `compute_semantic_tokens(uri)` returning delta-encoded integer arrays per the LSP 3.17 specification.
  3. Mark variables declared with `mut` and parameters with intent `mut` with the `modification` modifier.
- **Acceptance Criteria**:
  - Zed renders mutable variables and parameter intents with distinct styling.
  - Automated test in `tests/conformance_test.cpp` validates the semantic token integer stream for `examples/01-hello-core/main.cf`.

---

## Medium Priority (P2)

### `LSP-04`: Multi-File Cross-Workspace References & Rename `[Completed]`
- **Status**: Completed
- **Category**: Language Server / Refactoring
- **Target Files**: `src/lsp.hpp`
- **Rationale**: `textDocument/references` and `textDocument/rename` currently only scan the active document. Renaming an exported struct or function in a package should update all importer files across the project workspace.
- **Detailed Requirements**:
  1. Scan all `.cf` files in the workspace directory when finding references or renaming public symbols.
  2. Return a `WorkspaceEdit` with changes across multiple document URIs.
- **Acceptance Criteria**:
  - Renaming `compute_tag` in `examples/08-multi-pkg-cc-build/pkg/checksum/checksum.cf` generates edits for both `checksum.cf` and `main.cf`.

---

### `LSP-05`: Auto-Import Completions for `core:*` Standard Library `[Completed]`
- **Status**: Completed
- **Category**: Language Server / Developer Ergonomics
- **Target Files**: `src/lsp.hpp`
- **Rationale**: Typing `read_all` should suggest `fs.read_all` even if `import "core:fs"` has not yet been added to the file, and selecting it should automatically insert the import statement at the top of the file.
- **Detailed Requirements**:
  1. Include all stdlib functions in unqualified completions with detail `core:<mod>`.
  2. Add `additionalTextEdits` to completion items that automatically insert `import "core:<mod>"` if not already present in the document.
- **Acceptance Criteria**:
  - Selecting an unimported stdlib function inserts both the call and the import header.

---

### `LSP-06`: Exhaustive `match` Arm Generation Code Action `[Completed]`
- **Status**: Completed
- **Category**: Language Server / Code Actions
- **Target Files**: `src/lsp.hpp`, `src/sema.hpp`
- **Rationale**: When matching on an algebraic enum, developers should have a quick-fix (`textDocument/codeAction`) to auto-generate skeleton arms for all missing variants.
- **Detailed Requirements**:
  1. Detect `cfire/NON-EXHAUSTIVE-MATCH` diagnostics or cursor on `match`.
  2. Determine missing variants from enum definition.
  3. Emit a code action "Fill missing match arms" inserting the variant templates.
- **Acceptance Criteria**:
  - Quick-fix appears on non-exhaustive match statements and populates missing cases.

---

### `LSP-07`: Sub-expression Type Hover & Struct Memory Layout Inspection `[Completed]`
- **Status**: Completed
- **Category**: Language Server / Inspection
- **Target Files**: `src/lsp.hpp`, `src/codegen.hpp`
- **Rationale**: Developers want to hover over expressions (`a + b`, `obj.method()`) to see their evaluated types, and hover over structs to see memory layout (`sizeof`, `alignof`, field offsets).
- **Detailed Requirements**:
  1. Compute range-based expression type inference for selected sub-expressions.
  2. Annotate struct hover cards with 64-bit alignment and byte offsets.
- **Acceptance Criteria**:
  - Hovering on `AuditLog` shows `Size: 24 bytes | Align: 8 bytes`.

---

## Lower Priority / Architectural (P3)

### `LSP-08`: Debounced Background Diagnostics Worker Thread `[Completed]`
- **Status**: Completed
- **Category**: Language Server / Performance
- **Target Files**: `src/lsp.hpp`
- **Rationale**: Currently, diagnostics are published synchronously on `textDocument/didChange`. A dedicated worker thread with a ~150ms debounce eliminates unnecessary re-parses during rapid typing bursts.
- **Detailed Requirements**:
  1. Background thread with condition variable and atomic timestamp.
  2. Debounces re-analysis requests per URI.
- **Acceptance Criteria**:
  - Zero editor UI lockup during typing streams.

---

### `LSP-09`: Resilient Statement-Level AST Recovery in Parser `[Completed]`
- **Status**: Completed
- **Category**: Frontend Compiler Architecture
- **Target Files**: `src/parser.hpp`
- **Rationale**: Current parser is recursive-descent fail-stop. While the LSP now synthesizes tokens for trailing dots, a broader resilient parser with statement-level synchronization tokens (panic mode recovery to `;`, `}`, `fn`, `type`) keeps valid AST nodes alive even when arbitrary statements contain syntax errors.
- **Detailed Requirements**:
  1. Add error recovery mode to `Parser::parse_stmt`.
  2. Synchronize to next statement boundary on token mismatch.
- **Acceptance Criteria**:
  - A syntax error on line 5 does not prevent functions on lines 10-20 from being parsed and checked.

---

### `LSP-10`: Selection Range Formatting `[Completed]`
- **Status**: Completed
- **Category**: Formatter / LSP
- **Target Files**: `src/formatter.hpp`, `src/lsp.hpp`
- **Rationale**: Support `textDocument/rangeFormatting` to format only user-selected blocks without reflowing the entire document.
- **Detailed Requirements**:
  1. Declare `documentRangeFormattingProvider: true`.
  2. Format AST sub-ranges and return bounded text edits.
- **Acceptance Criteria**:
  - Formatting a 3-line selection only modifies those 3 lines.

---

# Language Design & Frontend Backlog (LD)

Detailed rationale, theoretical foundations, and modern language parallels are documented in [`language-design-evaluation.md`](./language-design-evaluation.md).

---

## High Priority: Critical Soundness & Orthogonality (P1)

### `LD-01`: Recursive Monadic Type Orthogonality (`T!` and `T?` in Containers & Fields) `[Completed]`
- **Status**: Completed
- **Category**: Type System / Lowering
- **Target Files**: `src/ast.hpp`, `src/codegen.hpp`
- **Rationale**: Currently `is_fallible` (`!`) and `is_optional` (`?`) are ad-hoc flags on `TypeRef` that `lower_owning_type` ignores, silently stripping `std::expected` and `std::optional` from struct fields and slice element types (`[]i32!`, `Item: str?`).
- **Detailed Requirements**:
  1. Refactor `TypeRef` in `src/ast.hpp` so `Optional` and `Fallible` are first-class recursive type constructors.
  2. Unify `lower_owning_type` and `lower_return_type` in `src/codegen.hpp` into a single recursive lowering function.
- **Acceptance Criteria**:
  - `type Cache struct { Item: str?, Results: []i32! }` correctly lowers to `std::optional<std::string>` and `std::vector<std::expected<int32_t, cfire::Error>>`.
  - Conformance tests verify nested optional/fallible types roundtrip and compile cleanly.

---

### `LD-03`: Structured Concurrency Integrity & Destruction Ordering in `async.scope` `[Completed]`
- **Status**: Completed
- **Category**: Concurrency / Semantics
- **Target Files**: `src/codegen.hpp`, `runtime/cfire_prelude.hpp`
- **Rationale**: Currently `async.scope` lowers to a raw C++ lambda. An early `return` inside `async.scope` only exits the lambda, letting outer function execution continue prematurely. Additionally, local `defer` statements run *before* `scope.join_all()`, creating data races with background worker threads.
- **Detailed Requirements**:
  1. Emit structured scope control propagation so an early `return` within `async.scope` waits for `join_all()` and then returns from the enclosing function.
  2. Ensure local `defer` cleanup within `async.scope` executes strictly after all worker tasks have completed and joined.
- **Acceptance Criteria**:
  - Early `return` inside `async.scope` unwinds the full enclosing function after joining worker threads.
  - No use-after-free data races occur between `defer` and background tasks.

---

### `LD-04`: Explicit Borrow References (`&str`, `&[]T`) to Prevent Auto-View Dangling `[Completed]`
- **Status**: Completed
- **Category**: Memory Safety / Lowering
- **Target Files**: `src/codegen.hpp`, `src/sema.hpp`
- **Rationale**: `in` parameters typed `str` and `[]T` automatically lower to `std::string_view` and `std::span<const T>`. When callers pass temporaries (e.g. `f(str.to_upper("x"))` or `f([1, 2, 3])`), the non-owning view immediately dangles if stored or returned.
- **Detailed Requirements**:
  1. Distinguish owned value types (`str`, `[]T`) from borrowed views (`&str`, `&[]T`).
  2. Owned parameters lower to `const std::string&` and `const std::vector<T>&`, guaranteeing safe lifetime extension. Only explicit `&` borrows lower to `string_view` / `span`.
- **Acceptance Criteria**:
  - Functions returning slices/views must take explicit borrow parameters.
  - Temporaries passed to owned parameters compile safely without AddressSanitizer use-after-free diagnostics.

---

### `LD-05`: CFG-Based Definite Assignment & Move Analysis `[Completed]`
- **Status**: Completed
- **Category**: Semantic Analysis / Compiler Architecture
- **Target Files**: `src/sema.hpp`, `src/codegen.hpp`
- **Rationale**: The move checker currently tracks line numbers (`moved_at_line_`). It is blind to loops (moving inside a loop causes runtime UB on iteration 2), prevents reassigning to moved variables (`x = new_val`), and misreports branches.
- **Detailed Requirements**:
  1. Build a basic Control Flow Graph (CFG) representation of functions in `src/sema.hpp`.
  2. Compute forward dataflow for liveness and definite assignment.
  3. Emit `std::move` based on CFG edge liveness rather than line numbers.
- **Acceptance Criteria**:
  - Re-initializing a moved variable (`x = new_val`) compiles and allows subsequent use.
  - Moving a variable inside a loop produces a compile-time error.
  - Branch-conditioned moves only mark the variable moved along the taken branch.

---

### `LD-11`: Composable Expression-Level `try` Lowering `[Completed]`
- **Status**: Completed
- **Category**: Error Handling / Expressions
- **Target Files**: `src/codegen.hpp`, `runtime/cfire_prelude.hpp`
- **Rationale**: `try` is parsed as an expression, but codegen lowers it as a statement-level macro. Inside sub-expressions (e.g. `10 + try f()`), the `try` keyword is silently discarded, emitting invalid C++.
- **Detailed Requirements**:
  1. Lower `try` using GNU/Clang statement expressions `({ ... })` or normalize expressions during a pre-codegen IR pass.
  2. Ensure `try` can be placed arbitrarily in any sub-expression position.
- **Acceptance Criteria**:
  - `val := 10 + try calculate_offset()` compiles cleanly and early-returns error if `calculate_offset()` fails.
  - Multiple `try` expressions in a single statement do not collide.

---

### `LD-12`: Definite Divergence Verification for `or err { ... }` Blocks `[Completed]`
- **Status**: Completed
- **Category**: Control Flow / Safety
- **Target Files**: `src/sema.hpp`, `src/codegen.hpp`
- **Rationale**: If an `or err { ... }` block does not return, codegen unconditionally dereferences the empty `std::expected`, causing runtime undefined behavior.
- **Detailed Requirements**:
  1. Add control-flow divergence analysis in `src/sema.hpp`.
  2. Enforce that `or err { ... }` either diverges (`return`, `panic`) or explicitly yields a fallback value.
- **Acceptance Criteria**:
  - Non-diverging `or err { ... }` produces compile error: `cfire/NON-DIVERGING-OR-BLOCK`.
  - Diverging blocks compile cleanly and safely.

---

## Medium Priority: Soundness & Ergonomics (P2)

### `LD-02`: Value-Category-Aware Variant Matching (Moving Out of Match Arms) `[Completed]`
- **Status**: Completed
- **Category**: Pattern Matching / Ownership
- **Target Files**: `src/codegen.hpp`
- **Rationale**: `match` arm visitors currently bind variant payloads as `const auto&`, preventing `own[T]` or move-only payloads from being moved out.
- **Detailed Requirements**:
  1. When matching on an rvalue or moved union, emit `[&](Enum::_Variant&& _m)` visitors.
  2. Bind payload variables as rvalue references to allow `return move payload`.
- **Acceptance Criteria**:
  - Matching an owned `Payload::Packet(b)` allows moving `b` into a return value without copy errors.

---

### `LD-06`: Path-Based Mutation Exclusivity Verification (`cfire/MUT-ALIAS`) `[Completed]`
- **Status**: Completed
- **Category**: Semantic Analysis / Aliasing
- **Target Files**: `src/sema.hpp`
- **Rationale**: `cfire/MUT-ALIAS` only checks bare identifier nodes; wrapping in field access (`p.data`) or indexing (`arr[0]`) bypasses aliasing checks.
- **Detailed Requirements**:
  1. Implement projection path checking (`RootVar + Field/Index Projections`).
  2. Disallow aliasing between overlapping projection paths.
- **Acceptance Criteria**:
  - `transform(mut p.data, p.data)` is rejected with `cfire/MUT-ALIAS`.

---

### `LD-08`: Unify Foreign Escape Syntax to `cpp { ... }` `[Completed]`
- **Status**: Completed
- **Category**: Lexer / Language Consistency
- **Target Files**: `src/lexer.hpp`
- **Rationale**: Standardize on concise, ergonomic `cpp { ... }` as the single foreign C++ escape syntax.
- **Detailed Requirements**:
  1. Update `src/lexer.hpp` to recognize `cpp {` as `TokenKind::CppBlock`.
  2. Standardize solely on `cpp { ... }`.
- **Acceptance Criteria**:
  - `cpp { ... }` blocks parse and emit verbatim C++23.

---

### `LD-09`: Unambiguous Generic Call Disambiguation (`f[T, U](...)`) `[Completed]`
- **Status**: Completed
- **Category**: Parser / Grammar
- **Target Files**: `src/parser.hpp`
- **Rationale**: The previous 4-token lookahead `[Ident](` failed on multiple generic arguments (`make_pair[i32, str]()`) and compound types (`cast[[]i32]()`).
- **Detailed Requirements**:
  1. Implement balanced bracket lookahead to parse multiple and compound type arguments `f[T1, T2](...)` without turbofish operator noise.
  2. Allow arbitrary type arguments in generic calls.
- **Acceptance Criteria**:
  - Multi-type generic calls `pair := make_pair[i32, str](1, "one")` parse and lower correctly to C++23.

---

### `LD-13`: Multi-Return Tuples & Deconstructing Assignment `[Completed]`
- **Status**: Completed
- **Category**: Language Ergonomics / Types
- **Target Files**: `src/parser.hpp`, `src/sema.hpp`, `src/codegen.hpp`
- **Rationale**: Provide first-class value semantics for returning multiple values, avoiding mutable parameter boilerplate.
- **Detailed Requirements**:
  1. Support first-class multi-return tuples `fn parse() -> (f64, f64)!`.
  2. Support deconstructing assignments `(x, y) := parse()`.
- **Acceptance Criteria**:
  - Functions can return multiple values as tuples and deconstruct with `(x, y) := parse()`.

---

## Low Priority: Polish & Regularity (P3)

### `LD-07`: Unify Variable Declarations `[Completed]`
- **Status**: Completed
- **Category**: Syntax Regularity
- **Target Files**: `src/parser.hpp`
- **Rationale**: Establish one clean, consistent syntax for binding declarations across all scopes.
- **Detailed Requirements**:
  1. Standardize on `x := val` / `mut x := val` (inferred) and `x: T = val` / `mut x: T = val` (typed).
  2. Allow these four forms uniformly at both local and package scope.
- **Acceptance Criteria**:
  - Cleaner syntax with zero keyword noise and uniform scope rules.

---

### `LD-10`: Decouple Struct Tags from Compiler Codegen `[Completed]`
- **Status**: Completed
- **Category**: Architecture / Metaprogramming
- **Target Files**: `src/parser.hpp`, `src/codegen.hpp`
- **Rationale**: The compiler core currently has hardcoded logic for `simdjson` and `sqlite3`, while discarding user-defined struct tags (`yaml`, `toml`).
- **Detailed Requirements**:
  1. Parse arbitrary backtick key-value tags into struct metadata.
  2. Expose compile-time reflection descriptors so serialization libraries can be implemented in user-space or modular headers.
- **Acceptance Criteria**:
  - Any valid struct tag can be declared and inspected without compiler hardcoding.

---

### `LD-14`: Remove Phantom Parameter Intent `forward` `[Completed]`
- **Status**: Completed
- **Category**: Language Hygiene
- **Target Files**: `src/ast.hpp`, `src/parser.hpp`, `src/lexer.hpp`
- **Rationale**: `forward` is accepted in function signatures, but silently lowers to `const T&` and is unsupported at call sites.
- **Detailed Requirements**:
  1. Remove `KwForward` from the grammar and AST until universal forwarding reference semantics are formally designed.
- **Acceptance Criteria**:
  - Clean language surface without dead or misleading keywords.

---

# Build System & Project Extensibility Backlog (BS)

Detailed rationale, failure modes, and industry comparisons are documented in [`buildsys-evaluation.md`](./buildsys-evaluation.md).

---

## High Priority: Correctness & Build Performance (P1)

### `BS-01`: Reconcile Documentation & AST Scraper (Phantom `build.Context` API) `[Completed]`
- **Status**: Completed
- **Category**: Build System / Ergonomics
- **Target Files**: `src/buildsys.hpp`, `docs/GUIDE.md`
- **Rationale**: `docs/GUIDE.md` Chapter 8 describes a programmatic `pub fn configure(mut b: build.Context)` API that does not exist in the compiler. `buildsys.hpp` only scrapes top-level `var X = build.<Driver>()` expressions.
- **Detailed Requirements**:
  1. Either implement the programmatic `build.Context` evaluator or update `GUIDE.md` to document the actual declarative syntax.
- **Acceptance Criteria**:
  - Documented examples compile cleanly without silently ignoring dependencies.

---

### `BS-02`: Object-Level Timestamp / Content Caching for `build.CC` `[Completed]`
- **Status**: Completed
- **Category**: Build System / Performance
- **Target Files**: `src/buildsys.hpp`
- **Rationale**: Every invocation of `cfire run`/`build` recompiles all C/C++ source files from scratch. Furthermore, identical file stems in different directories collide in `<cache>/objs/<dep>/<stem>.o`.
- **Detailed Requirements**:
  1. Record source file mtime/hash manifests in `.cfire/cache/objs/<dep>/manifest.txt` and skip unmodified translation units.
  2. Disambiguate object file stems using relative path hashes.
- **Acceptance Criteria**:
  - Rebuilding an unmodified C dependency takes < 10ms.

---

### `BS-03`: Structured Header AST Ingestion (Preserve Namespaces & Overloads) `[Completed]`
- **Status**: Completed
- **Category**: Clang Bridge / Interop
- **Target Files**: `src/clang_bridge.hpp`
- **Rationale**: Substring searching in Clang AST JSON dumps drops C++ namespaces and overwrites overloaded functions.
- **Detailed Requirements**:
  1. Replace raw substring searches with structured JSON parsing using the embedded `simdjson` library.
  2. Preserve C++ namespace paths and map function overloads to unique signatures.
- **Acceptance Criteria**:
  - Headers with namespaces (e.g. `ImGui::*`) and overloads index reliably without symbol loss.

---

## Medium Priority: Interop & Modularity (P2)

### `BS-04`: Hermetic & Relocatable CMake Ejection (`cfire eject`) `[Completed]`
- **Status**: Completed
- **Category**: Toolchain / Ejection
- **Target Files**: `src/buildsys.hpp`
- **Rationale**: `cfire eject` emits broken CMake configurations for dependencies with empty `dep.sources`, drops custom linker flags, and leaves vendor paths un-copied.
- **Detailed Requirements**:
  1. Mirror vendor source trees into `out_dir/vendor/`.
  2. Emit `FetchContent` or `add_subdirectory` for CMake dependencies and propagate linker flags into CMake targets.
- **Acceptance Criteria**:
  - Moving the ejected folder to another machine builds cleanly with standard `cmake -B build && cmake --build build`.

---

### `BS-05`: Preprocessor Macro Injection for Header-Only C Libraries `[Completed]`
- **Status**: Completed
- **Category**: Foreign Interop / Syntax
- **Target Files**: `src/parser.hpp`, `src/codegen.hpp`
- **Rationale**: Single-file libraries like `stb_image.h` require `#define STB_IMAGE_IMPLEMENTATION` before inclusion.
- **Detailed Requirements**:
  1. Support an attribute on C imports: `import cpp "stb_image.h" @define("STB_IMAGE_IMPLEMENTATION")`.
  2. Emit defined macros immediately before the corresponding `#include` in the generated C++ source.
- **Acceptance Criteria**:
  - Header-only libraries compile without unresolved symbol errors.

---

### `BS-06`: Shared Library (`.so` / `.dylib`) & Dynamic Link Support `[Completed]`
- **Status**: Completed
- **Category**: Build System / Linking
- **Target Files**: `src/buildsys.hpp`
- **Rationale**: CMake and Make dependencies generating shared libraries fail because `buildsys.hpp` only links `.a` archives.
- **Detailed Requirements**:
  1. Support `.so` and `.dylib` detection in `build_local_dep`.
  2. Automatically append `-Wl,-rpath` flags pointing to the built library directory.
- **Acceptance Criteria**:
  - Dependencies producing shared libraries link and execute without `dlopen`/dyld errors.

---

### `BS-07`: Recursive Local Package Resolution & Subpackage `build.cf` `[Completed]`
- **Status**: Completed
- **Category**: Module System / Build
- **Target Files**: `src/buildsys.hpp`
- **Rationale**: Local package import resolution is non-recursive, and subdirectories cannot declare isolated `build.cf` manifests.
- **Detailed Requirements**:
  1. Make local package resolution recursive across transitive dependencies.
  2. Traverse subdirectories to aggregate C dependency specifications from subpackage `build.cf` files.
- **Acceptance Criteria**:
  - Transitive local package imports resolve properly without manual hoisting into root `build.cf`.

---

## Low Priority: Tooling (P3)

### `BS-08`: Pre-Build Code Generation Hooks (`build.Command`) `[Completed]`
- **Status**: Completed
- **Category**: Build System / Metaprogramming
- **Target Files**: `src/buildsys.hpp`, `src/parser.hpp`
- **Rationale**: Projects using Protobuf, Flatbuffers, or parser generators cannot declare pre-compilation generation steps in `build.cf`.
- **Detailed Requirements**:
  1. Add a `build.Command` or `build.Gen` step primitive to `build.cf`.
- **Acceptance Criteria**:
  - Pre-build commands execute prior to compiling dependent targets.

---

# Core Standard Library Backlog (LIB)

Detailed rationale, API designs, and modern language parity analyses are documented in [`stdlib-evaluation.md`](./stdlib-evaluation.md).

---

## High Priority: Critical Infrastructure & Security (P1)

### `LIB-01`: High-Level HTTP Client, Server & URL Parsing (`core:net/http`, `core:net/url`) `[Completed]`
- **Status**: Completed
- **Category**: Networking / Web
- **Target Files**: `runtime/cfire_core_stdlib.hpp`, `src/docgen.hpp`
- **Rationale**: Essential for REST APIs, webhooks, microservices, and external cloud communication.
- **Detailed Requirements**:
  1. Provide `http.get`, `http.post`, and `http.make_server`.
  2. Provide `url.parse` with query parameter extraction.
  3. Support DNS resolution via POSIX `getaddrinfo`.
- **Acceptance Criteria**:
  - Developers can make outbound HTTPS requests and serve JSON endpoints without writing raw socket loops.

---

### `LIB-02`: Safe Process Execution, Piping & Signal Trapping (`core:os/exec`, `core:os/signal`) `[Completed]`
- **Status**: Completed
- **Category**: OS / Process Management
- **Target Files**: `runtime/cfire_core_stdlib.hpp`
- **Rationale**: Replaces vulnerable `popen` shell interpolation with parameter-list execution (`execve`), stdin/stdout streaming pipes, and POSIX signal trapping.
- **Detailed Requirements**:
  1. Implement `exec.command(binary, [args...])` with isolated stdin/stdout/stderr pipes.
  2. Provide `signal.notify([SIGINT, SIGTERM])` returning a `sync.Channel[i32]`.
- **Acceptance Criteria**:
  - Subprocesses run safely without shell injection; child stdin can be written to and stdout/stderr read independently.

---

### `LIB-03`: Cryptographic Suite & True CSPRNG (`core:crypto/sha256`, `core:crypto/rand`) `[Completed]`
- **Status**: Completed
- **Category**: Security / Cryptography
- **Target Files**: `runtime/cfire_core_stdlib.hpp`
- **Rationale**: Replaces misnamed blake3 and static PRNG seed with standard SHA-256, HMAC-SHA256, and OS-backed random bytes (`arc4random_buf` / `getentropy`).
- **Detailed Requirements**:
  1. Provide `rand.bytes(n)` reading from OS CSPRNG.
  2. Seed `core:rand` MT19937 with `std::random_device{}()`.
  3. Provide `sha256.hash_hex` and `hmac.sha256_hex`.
- **Acceptance Criteria**:
  - Random numbers are cryptographically unpredictable, and SHA-256/HMAC digests verify against test vectors.

---

### `LIB-12`: Reconcile Core Library Documentation & Catalog Desyncs `[Completed]`
- **Status**: Completed
- **Category**: Quality / Consistency
- **Target Files**: `runtime/cfire_core_stdlib.hpp`, `src/docgen.hpp`
- **Rationale**: Fixes all 7 naming and return type discrepancies between `docgen.hpp` and `cfire_core_stdlib.hpp`.
- **Detailed Requirements**:
  1. Rename `read_bytes` / `write_bytes` to `read_all` / `write_all` in `cfire_core_stdlib.hpp`.
  2. Implement `core:str.contains(s, needle)`.
  3. Implement `core:math.clamp_f64(v, lo, hi)` and alias `sqrt_f64` to `sqrt`.
  4. Implement `core:rand.u64()` and `core:rand.f64_unit()`.
  5. Correct `core:os.exec` / `exec_output` naming consistency.
- **Acceptance Criteria**:
  - `./cfire doc --check` confirms 100% agreement between runtime headers and compiler catalog.

---

## Medium Priority: Daily Developer Staples (P2)

### `LIB-04`: Standard Data Encodings (`core:encoding/base64`, `core:encoding/hex`, `core:encoding/csv`) `[Completed]`
- **Status**: Completed
- **Category**: Text / Data Formats
- **Target Files**: `runtime/cfire_core_stdlib.hpp`
- **Rationale**: Required for API authentication tokens, hashes, and CSV data processing.
- **Detailed Requirements**:
  1. Provide `base64.encode` and `base64.decode`.
  2. Provide `hex.encode` and `hex.decode`.
  3. Provide RFC 4180 `csv.read_all` and `csv.write_all`.
- **Acceptance Criteria**:
  - Encodings roundtrip binary and text buffers accurately.

---

### `LIB-05`: Regular Expressions Engine (`core:regex`) `[Completed]`
- **Status**: Completed
- **Category**: Text Processing
- **Target Files**: `runtime/cfire_core_stdlib.hpp`
- **Rationale**: Input validation, text sanitization, and pattern matching.
- **Detailed Requirements**:
  1. Provide `regex.compile`, `is_match`, `find_all`, and `replace_all`.
- **Acceptance Criteria**:
  - Standard regex patterns match and extract capture groups correctly.

---

### `LIB-06`: Time, Date & Duration Modernization (`core:time`) `[Completed]`
- **Status**: Completed
- **Category**: Time / Dates
- **Target Files**: `runtime/cfire_core_stdlib.hpp`
- **Rationale**: Adds typed `Duration` arithmetic, calendar breakdowns (year, month, day), and ISO 8601 / RFC 3339 parsing and formatting via C++20 `<chrono>`.
- **Detailed Requirements**:
  1. Implement `time.Duration` with scalar multiplications (`5 * time.Second`).
  2. Implement `format_rfc3339()` and `parse_rfc3339()`.
- **Acceptance Criteria**:
  - Time instances serialize to ISO 8601 / RFC 3339 strings and compute duration intervals accurately.

---

### `LIB-07`: Data Structures & Algorithms (`core:collections`, `core:sort`) `[Completed]`
- **Status**: Completed
- **Category**: Collections / Algorithms
- **Target Files**: `runtime/cfire_core_stdlib.hpp`
- **Rationale**: Adds `Set[T]`, `PriorityQueue[T]`, `Deque[T]`, `sort.slice()`, and `sort.binary_search()`.
- **Detailed Requirements**:
  1. Implement generic `Set[T]` backed by `std::unordered_set`.
  2. Implement `PriorityQueue[T]` backed by `std::priority_queue`.
  3. Implement `sort.slice(mut s)` and `sort.binary_search(s, val)`.
- **Acceptance Criteria**:
  - Common collections and sorting algorithms operate natively on `cfire` value types and slices.

---

### `LIB-08`: Structured Leveled Logging (`core:log` / `slog`) `[Completed]`
- **Status**: Completed
- **Category**: Observability
- **Target Files**: `runtime/cfire_core_stdlib.hpp`
- **Rationale**: Leveled, structured logging (`DEBUG`, `INFO`, `WARN`, `ERROR`) with key-value contextual attributes and JSON / terminal formatters.
- **Detailed Requirements**:
  1. Implement zero-allocation logger with configurable minimum severity.
  2. Support key-value attributes and JSON / ANSI console output formatters.
- **Acceptance Criteria**:
  - Log lines format cleanly with timestamps, severity levels, and structured context.

---

### `LIB-09`: Streaming Filesystem I/O (`core:fs` Modernization) `[Completed]`
- **Status**: Completed
- **Category**: Filesystem / I/O
- **Target Files**: `runtime/cfire_core_stdlib.hpp`
- **Rationale**: Introduces streaming `File` handles with explicit open modes (`Read`, `Write`, `Append`), chunked reading, and path utility functions (`dirname`, `basename`, `extension`).
- **Detailed Requirements**:
  1. Implement `fs.open_file` returning a streaming `File` handle with `read`, `write`, `seek`, and `close`.
  2. Add path helpers: `fs.dirname`, `fs.basename`, and `fs.extension`.
- **Acceptance Criteria**:
  - Large files can be read in chunks without loading the entire payload into RAM.

---

### `LIB-10`: JSON Dynamic DOM & Structured Serializer Fixes (`core:json`) `[Completed]`
- **Status**: Completed
- **Category**: Serialization
- **Target Files**: `src/codegen.hpp`, `runtime/cfire_core_stdlib.hpp`
- **Rationale**: Adds dynamic JSON DOM inspection (`simdjson::dom`) and enforces character escaping in string serialization.
- **Detailed Requirements**:
  1. Integrate `simdjson::dom` for dynamic document traversal (`json.parse_dynamic`).
  2. Enforce character escaping in `stringify_struct` code generation.
- **Acceptance Criteria**:
  - Dynamic JSON payloads can be traversed, and serialized strings with quotes/newlines produce valid JSON.

---

## Low Priority: Text Refinement (P3)

### `LIB-11`: Multi-Byte UTF-8 & Unicode Support (`core:unicode`) `[Completed]`
- **Status**: Completed
- **Category**: Unicode / Text
- **Target Files**: `runtime/cfire_core_stdlib.hpp`
- **Rationale**: Safe UTF-8 rune iteration, rune counting, and validation without multi-byte corruption.
- **Detailed Requirements**:
  1. Implement `unicode.rune_count` and `unicode.runes` iterator based on branchless UTF-8 DFA tables.
- **Acceptance Criteria**:
  - Multi-byte international text and emojis are iterated and measured by codepoint rather than byte length.

---

# Developer, Editor & AI Agent Tooling (TOOL / IDE)

### `TOOL-01`: Agent Tooling Suite (`--json`, `outline`, `--llm`) `[Completed]`
- **Status**: Completed
- **Category**: CLI Tooling / AI Agent Ergonomics
- **Target Files**: `src/main.cpp`
- **Rationale**: Eliminates fragile stderr regex parsing for agents, saves 90% context tokens on codebase discovery, and eliminates hallucinated syntax.
- **Detailed Requirements**:
  1. Implement `cfire check --json <file>` emitting structured JSON diagnostics with codes and severity.
  2. Implement `cfire outline --json [path]` generating token-compressed symbol outlines of packages, structs, and functions.
  3. Implement `cfire doc --llm` generating a ~350-token grammar and stdlib cheat-sheet.
- **Acceptance Criteria**:
  - Commands output valid JSON or markdown reference without invoking Clang when inspecting structure.

---

### `IDE-01`: Universal Multi-IDE Auto-Detection & Setup (`cfire ide`) `[Completed]`
- **Status**: Completed
- **Category**: Developer Ergonomics / IDE Integration
- **Target Files**: `src/lsp.hpp`
- **Rationale**: Automatically configures language servers and format hooks for VS Code, Cursor, Zed, Neovim, and CLion.
- **Detailed Requirements**:
  1. Detect active editor via environment variables (`TERM_PROGRAM`, `VSCODE_PID`, `ZED_TERM`, `NVIM`) or workspace markers.
  2. Generate configuration manifests: `.vscode/settings.json`, `.vscode/extensions.json`, `.zed/settings.json`, `.nvim.lua`, `.idea/lsp.xml`.
- **Acceptance Criteria**:
  - Running `cfire ide` or `cfire ide --all` sets up full LSP integration and format-on-save across all major editors.

---

### `TOOL-02`: One-Shot REPL & CLI Evaluator (`cfire eval`) `[Completed]`
- **Status**: Completed
- **Category**: CLI Tooling / Developer Ergonomics
- **Target Files**: `src/main.cpp`
- **Rationale**: Developers and agents frequently want to evaluate snippets, test expressions, or run ad-hoc scripts without creating scratch projects or writing boilerplate `package main` and `fn main()`.
- **Detailed Requirements**:
  1. Implement `cfire eval [options] [code_or_path]`:
     - Positional code string or `-` / stdin piping (`echo 'println(1+1)' | cfire eval`).
     - `-e <expr>` evaluates expression and prints result with `println("{}", <expr>)`.
  2. Auto-wrap code without `package` in an ephemeral module with default stdlib imports (`core:fs`, `core:str`, `core:math`, `core:time`, `core:json`).
  3. Compile via ephemeral `/tmp` directory, execute binary, stream stdout/stderr, and clean up artifacts.
- **Acceptance Criteria**:
  - `cfire eval 'println("hello")'` runs and outputs `hello`.
  - `cfire eval -e '2 + 2'` prints `4`.
  - `echo 'println("piped")' | cfire eval` prints `piped`.

---

### `TOOL-03`: Semantic CLI Renaming & Refactoring Engine (`cfire refactor rename`) `[Completed]`
- **Status**: Completed
- **Category**: CLI Tooling / Refactoring / AI Ergonomics
- **Target Files**: `src/refactor.hpp`, `src/main.cpp`, `src/lsp.hpp`
- **Rationale**: Headless AI agents and CLI users need scriptable, multi-file semantic renaming with collision checks and atomic diff previews without needing a running LSP JSON-RPC daemon.
- **Detailed Requirements**:
  1. Implement `src/refactor.hpp` with AST-backed symbol reference finding, identifier validation, and scope collision detection.
  2. Implement `cfire refactor rename <target> <new_name> [flags]` supporting:
     - Target syntax: `<file>:<line>:<col>` or `--file <file> --line <n> --col <n> --to <new_name>`
     - `--dry-run`: report planned edits without modifying files.
     - `--diff`: emit unified diff preview of all file modifications.
     - `--apply`: atomically rewrite target files.
     - `--json`: emit machine-readable JSON summary for AI agents.
- **Acceptance Criteria**:
  - Renaming a local variable updates only occurrences within that function.
  - Renaming an exported struct or function updates both the definition and importer files across packages.
  - Collisions with existing identifiers in scope return an explicit error and abort without touching disk.

---

### `BS-EXT-01`: Remote Fetching, Integrity Lockfile & Offline Vendoring (`cfire vendor`) `[Completed]`
- **Status**: Completed
- **Category**: Build System / Package Management & Hermeticity
- **Target Files**: `src/buildsys.hpp`, `src/main.cpp`
- **Rationale**: Real-world projects need remote Git repositories and tarball archives without manual submodules. Developers require reproducible builds via `build.lock` and air-gapped repositories via `cfire vendor`.
- **Detailed Requirements**:
  1. Extend `build.cf` parsing to recognize remote Git (`build.Git`) and tarball archives (`build.Archive` / URLs in `build.CC` / `build.CMake`).
  2. Implement caching in `~/.cfire/cache/pkg/` with shallow Git clones and archive extraction.
  3. Implement `build.lock` recording exact commit hashes and cryptographic SHA-256 digests.
  4. Implement `cfire vendor [dest_dir]` copying remote dependencies into `./vendor/` for air-gapped builds.
- **Acceptance Criteria**:
  - `build.cf` with remote dependencies fetches and compiles seamlessly.
  - `build.lock` records exact commit hashes and archive digests.
  - `cfire vendor` extracts dependencies to `./vendor/` and enables builds with zero network access.

---

### `IDE-EXT-01`: LSP Semantic Parameter & Type Inlay Hints (`textDocument/inlayHint`) `[Completed]`
- **Status**: Completed
- **Category**: Language Server Protocol / Developer Ergonomics
- **Target Files**: `src/lsp.hpp`
- **Rationale**: Developers in modern editors (Zed, VS Code, Neovim) expect inline parameter name hints on function and method call arguments alongside existing type inference annotations.
- **Detailed Requirements**:
  1. Recursively traverse AST call expressions within functions, methods, and test bodies.
  2. Resolve callee parameter names for local, package-imported, and stdlib functions/methods.
  3. Emit `kind: 2` (Parameter) inlay hints for positional call arguments.
  4. Suppress redundant hints for explicit argument labels and matching variable names.
  5. Sort combined type and parameter hints by line and character position.
- **Acceptance Criteria**:
  - `compute_inlay_hints` returns `kind: 1` (Type) hints for `:=` declarations and `kind: 2` (Parameter) hints for call arguments.
  - Calling `calculate_total(10, 20)` emits `"label":"base:"` and `"label":"multiplier:"`.
  - Zero collisions and 100% test pass in `make test`.

---

### `BS-EXT-02`: Multi-Target Cross-Compilation (`--target <triple>`) `[Completed]`
- **Status**: Completed
- **Category**: Build System / Cross-Compilation & Portability
- **Target Files**: `src/buildsys.hpp`, `src/main.cpp`
- **Rationale**: Developers need to cross-compile `.cf` binaries for remote deployment (Linux x86_64, aarch64, Windows, etc.) directly using Clang's multi-target backend.
- **Detailed Requirements**:
  1. Add CLI flags `--target <triple>` and `--sysroot <path>` to `build`, `emit-cpp`, `check`, `run`, and `test`.
  2. Forward target triple and sysroot flags to Clang compilation and linking steps.
  3. Ensure external C/C++ libraries compiled in `build_local_dep` respect the target triple.
  4. Automatically bypass host PCH when a non-empty `target_triple` is specified.
  5. Append `.exe` binary extension when compiling for Windows targets.
- **Acceptance Criteria**:
  - `cfire build --target arm64-apple-darwin` and `cfire build --target x86_64-apple-darwin` produce binaries with correct architecture.
  - 100% test pass in `make test`.

---

### `LIB-EXT-01`: Cryptographic Hashing & Symmetric Encryption (`core:crypto/aes`, `core:crypto/hmac`) `[Completed]`
- **Status**: Completed
- **Category**: Standard Library / Cryptography & Security
- **Target Files**: `runtime/cfire_core_stdlib.hpp`, `src/docgen.hpp`, `src/codegen.hpp`
- **Rationale**: Production services require constant-time message authentication comparison and standard AES-256 symmetric authenticated encryption (CTR and GCM AEAD) without external dynamic library linkages.
- **Detailed Requirements**:
  1. Implement constant-time `hmac.equal` for strings and byte spans to prevent timing attacks.
  2. Implement `core:crypto/aes` with pure C++23 header-only AES-256 block cipher, Key Expansion, and Galois Counter Mode (GCM) with GHASH MAC calculation.
  3. Provide `aes.encrypt_gcm`, `aes.decrypt_gcm`, `aes.decrypt_gcm_str`, `aes.encrypt_ctr`, and `aes.decrypt_ctr`.
  4. Ensure authentication tag mismatches, corrupted ciphertext, or altered AAD cleanly reject decryption with `cfire::Error`.
- **Acceptance Criteria**:
  - AES-256 and GCM conform to NIST FIPS 197 and NIST SP 800-38D test vectors.
  - End-to-end `.cf` programs successfully encrypt and decrypt with GCM and verify HMAC equality.
  - 100% test pass in `make test` and `./cfire doc --check` clean.

---

### `LD-EXT-01`: Slice Pattern Matching & Destructuring (`match slice { [head, ..tail] => ... }`) `[Completed]`
- **Status**: Completed
- **Category**: Language Design / Pattern Matching & Ergonomics
- **Target Files**: `src/ast.hpp`, `src/parser.hpp`, `src/sema.hpp`, `src/codegen.hpp`
- **Rationale**: Slices are idiomatic in systems programming. Slicing and pattern matching on head/tail or fixed elements reduces boilerplate indexing and boundary checks.
- **Detailed Requirements**:
  1. Extend `MatchArm` AST with slice pattern fields (`is_slice_pattern`, `slice_head`, `slice_rest`, `slice_has_rest`, `slice_tail`).
  2. Parse slice patterns `[]`, `[head, ..tail]`, `[a, b]`, `[..init, last]`, `[first, ..mid, last]`, and wildcard `_`.
  3. Lower slice matching to clean sequential length and bounds checks with zero-copy `cfire::slice` destructuring.
  4. Update semantic analysis to bypass enum variant exhaustiveness on slice patterns.
- **Acceptance Criteria**:
  - `match` seamlessly destructures slices (`[]T`, `&[]T`, `std::vector<T>`, `std::span<const T>`).
  - Recursive slice matching functions (e.g. recursive sum) compile and execute with zero memory overhead.
  - 100% test pass in `make test`.

---

### `LIB-EXT-04`: Structured Concurrency Cancellation Tokens & Context (`core:sync/context`) `[Completed]`
- **Status**: Completed
- **Category**: Concurrency / Structured Cancellation
- **Target Files**: `runtime/cfire_core_stdlib.hpp`, `src/docgen.hpp`, `src/codegen.hpp`, `tests/conformance_test.cpp`
- **Rationale**: Modern concurrent systems require deterministic propagation of deadlines and cancellation signals without thread leaking or polling.
- **Detailed Requirements**:
  1. Implement `cfire::sync::context::Context` with cancellation propagation, deadline timer threads, and `on_cancel` callbacks.
  2. Implement `context.background()`, `context.with_cancel(parent)`, and `context.with_timeout(parent, ms)`.
  3. Integrate with channels: `Channel<T>::recv_ctx(ctx)` wakes blocked threads immediately upon context cancellation.
  4. Integrate with timers: `time::sleep_ctx(ctx, ms)` aborts early upon cancellation.
- **Acceptance Criteria**:
  - Manual cancellation and timeout expiration cancel child contexts while preserving parent context.
  - `Channel.recv_ctx` unblocks immediately with error on cancelled context.
  - 100% test pass in `make test` and `./cfire doc --check` clean.

---

### `LIB-EXT-02`: High-Level WebSockets Client & Server (`core:net/websocket`) `[Completed]`
- **Status**: Completed
- **Category**: Networking / Web Protocols
- **Target Files**: `runtime/cfire_core_stdlib.hpp`, `src/docgen.hpp`, `src/codegen.hpp`, `tests/conformance_test.cpp`
- **Rationale**: Real-time services, distributed agents, and interactive frontends require standard RFC 6455 WebSockets for full-duplex streaming without heavy external dependencies.
- **Detailed Requirements**:
  1. Pure C++23 RFC 3174 SHA-1 digest calculation for opening handshake (`Sec-WebSocket-Accept`).
  2. RFC 6455 client handshake (`websocket.connect(url)`) and server upgrade (`websocket.upgrade(req, conn)`).
  3. Full RFC 6455 frame framing (Text, Binary, Ping/Pong, Close, 7/16/64-bit length, client masking).
  4. RAII `websocket::Conn` with `send_text`, `send_binary`, `recv_text`, `recv_binary`, `ping`, `close`.
- **Acceptance Criteria**:
  - Full-duplex messaging between client and server across loopback.
  - Correct masking of client frames and unmasked server frames.
  - Automatic Pong reply to incoming Ping control frames.
  - 100% test pass in `make test` and `./cfire doc --check` clean.

---

### `LIB-EXT-05`: Production HTTP/1.1 Server, Request/Response Wire Engine & Standalone Router (`core:net/http`) `[Completed]`
- **Status**: Completed
- **Category**: Networking / Web Services & Routing
- **Target Files**: `runtime/core/net.hpp`, `src/docgen.hpp`, `src/codegen.hpp`, `examples/11-http-server-router/main.cf`, `tests/unit/test_stdlib.cpp`
- **Rationale**: Production web services and microservices require a complete HTTP/1.1 request parser, fluent response builder, middleware-capable router with path parameters (`:id`) and wildcards (`*filepath`), and background server execution.
- **Detailed Requirements**:
  1. Implement `http.parse_request(raw)` and `Response.to_wire()` supporting headers, query strings, and status codes.
  2. Implement fluent `http.Response` builders (`Response.text`, `Response.json`, `Response.html`, `Response.redirect`, `with_header`).
  3. Implement standalone `http.Router` (`make_router`, `get`, `post`, `put`, `delete_`, `patch`, `all`, `use`, `dispatch`) supporting `:param`, `*wildcard`, automatic `405 Method Not Allowed` with `Allow` header, and `404 Not Found` handlers.
  4. Implement `Server.serve_in_background(router)` and `Server.listen_and_serve(router)`.
- **Acceptance Criteria**:
  - Standalone `Router.dispatch(req)` executes in-memory unit tests without opening sockets.
  - Live `Server.serve_in_background(router)` serves concurrent HTTP client requests cleanly.
  - Verified in `test_net_http_and_router` and `examples/11-http-server-router/main.cf`.




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
   - [x] **Manual Validation**: Live interactive reproduction and editor UX verification (Zed/VS Code/CLI).
4. [x] **Implementation Plan Mapped**: Specific functions, files, and data structures identified.

---

## Active Task: `LSP-01` Snippet Completions for Core Language Constructs

### 1. Scope & Problem Statement
- **Problem**: Typing keywords (`fn`, `match`, `for`, `type struct`, etc.) in Zed or VS Code currently returns plain text completion items. The developer has to manually write out all parentheses, arrows, braces, and boilerplate structure.
- **In Scope**:
  - Add snippet completions (`insertTextFormat: 2`) for primary statements:
    - `fn` $\rightarrow$ `fn ${1:name}(${2:params}) -> ${3:void} {\n\t${0}\n}`
    - `type struct` $\rightarrow$ `type ${1:Name} struct {\n\t${0}\n}`
    - `type enum` $\rightarrow$ `type ${1:Name} enum {\n\t${0}\n}`
    - `match` $\rightarrow$ `match ${1:expr} {\n\t${2:case} => ${0}\n}`
    - `for` $\rightarrow$ `for ${1:item} in ${2:iter} {\n\t${0}\n}`
    - `defer` $\rightarrow$ `defer ${0}`
    - `cplusplus` $\rightarrow$ `cplusplus {\n\t${0}\n}`
  - Ensure client capability `completionItem.snippetSupport` is detected (defaulting to enabled for modern editors like Zed and VS Code).
- **Out of Scope (Deferred to future items)**:
  - Custom user-defined snippets (e.g. from `.cfire/snippets.json`).
  - Auto-import header injection (tracked in `LSP-05`).

### 2. Open Questions & User Clarifications
- **Q1 (Tabstop preferences)**: For `fn` completions, should the default return type placeholder be `void`, `int`, or omit `-> void` if return type is optional in `.cf`?
  - *Decision*: Use `fn ${1:name}(${2:params}) -> ${3:void} {\n\t${0}\n}` with a tabstop so pressing `tab` lets the user either change it to another type or delete it.
- **Q2 (Struct tags)**: Should `type struct` include a default tagged field example (e.g. `Id: i64`), or just an empty body with cursor at `${0}`?
  - *Decision*: Empty body with cursor at `${0}`: `type ${1:Name} struct {\n\t${0}\n}` for maximum flexibility.

### 3. Testing & Verification Matrix
- [x] **Unit Tests**: Verify JSON formatting of snippet items with `insertTextFormat: 2` and `extract_json_bool` in `lsp.hpp`.
- [x] **Integration Tests**: Verify that snippet completions do not interfere with normal identifier completions or keyword hovers.
- [x] **Conformance Tests**: Assertions added to `test_formatter_lsp_and_docgen` in `tests/conformance_test.cpp` verifying that completion on statement boundaries returns `"insertTextFormat":2` with tabstops for `fn`, `type struct`, `match`, and `for`.
- [x] **Manual Validation**: Interactive RPC verification confirming 8 snippets emitted when `snippetSupport: true`, and zero snippets emitted when `snippetSupport: false`.

### 4. Implementation Status
- **Status**: Completed & Verified. All tests passing (`make test` 100% pass, `./cfire doc --check` clean). Ready to merge.

---

## Active Task: `LSP-02` Go to Type Definition (`textDocument/typeDefinition`)

### 1. Scope & Problem Statement
- **Problem**: When a developer hovers over or right-clicks a variable instance (e.g. `audit` in `mut audit := AuditLog{}` or `log` in `fn record(mut log: AuditLog)`), standard "Go to Definition" jumps to the line where `audit` was declared. Developers want "Go to Type Definition" to jump directly to the struct declaration (`type AuditLog struct`).
- **In Scope**:
  - Implement `textDocument/typeDefinition` endpoint in `Server::handle_stream`.
  - Resolve the variable's type via `resolve_qualifier_type` and AST scope bindings.
  - Locate declaration of that struct or enum across current file and `ws.local_packages`.
  - Return `Location` `{ uri, range }`.
- **Out of Scope**:
  - Navigating to C++ standard library definitions inside `<vector>` or `<string>` (native bridge jumps to C header line).

### 2. Open Questions & User Clarifications
- **Q1 (Primitive Types)**: If cursor is on an integer variable `x := 42` or string `msg := "hello"`, what should `typeDefinition` return?
  - *Decision*: Return `null` (standard LSP behavior like rust-analyzer/gopls for primitives with no source declaration).
- **Q2 (Collection Types)**: If the variable is a slice, map, or smart pointer of user structs (e.g. `items := []AuditLog{}`), should `typeDefinition` unwrap to the element type?
  - *Decision*: Yes, unwrap collection and pointer types (`[]T`, `own[T]`, `map[K]V`, `*T`, `T?`) to navigate directly to the underlying element struct/enum type declaration.

### 3. Testing & Verification Matrix
- [x] **Unit Tests**: Verify type-symbol resolution and unwrapping helper maps local variable bindings to their underlying struct/enum definitions and returns null for primitives.
- [x] **Integration Tests**: Verify cross-package type definition resolution (e.g., variable holding a struct from an imported package).
- [x] **Conformance Tests**: Assertions added to `test_formatter_lsp_and_docgen` in `tests/conformance_test.cpp` verifying `typeDefinition` on `audit` (line 2 of `AuditLog`), `m` (line 2 of `Metric`), and `a` (`null` for primitive `i32`).
- [x] **Manual Validation**: Interactive CLI JSON-RPC tests verified local struct resolution (`audit`), parameter resolution (`log`), collection unwrapping, and cross-package package struct navigation (`helper.Config`).

### 4. Implementation Status
- **Status**: Completed & Verified. All tests passing (`make test` 100% pass, `./cfire doc --check` clean). Ready to merge.

---

## Active Task: `LSP-03` Semantic Tokens Provider (`textDocument/semanticTokens/full`)

### 1. Scope & Problem Statement
- **Problem**: Standard regex/TextMate grammars cannot distinguish mutable variables, parameter intents (`in`, `mut`, `move`, `out`), or native C++ bridge calls. Semantic tokens provide high-fidelity coloring in Zed and VS Code.
- **In Scope**:
  - Add `semanticTokensProvider` capability to `initialize` response with legend:
    - Token types: `type` (0), `struct` (1), `enum` (2), `interface` (3), `parameter` (4), `variable` (5), `property` (6), `enumMember` (7), `function` (8), `method` (9), `keyword` (10), `modifier` (11), `string` (12), `number` (13), `operator` (14), `namespace` (15).
    - Token modifiers: `declaration` (1), `definition` (2), `readonly` (4), `modification` (8), `defaultLibrary` (16), `async` (32).
  - Implement `compute_semantic_tokens(uri)` in `Server`:
    - Token stream + AST annotation architecture.
    - Full LSP 3.17 relative delta-encoding (`[deltaLine, deltaStartChar, length, tokenType, tokenModifiers]`).
    - Differentiates `mut` variables/parameters (marked with `modification`) from immutable (`readonly`).
    - Identifies built-ins (`println`, `panic`, etc.) and stdlib/C++ namespaces with `defaultLibrary`.
  - Handle `textDocument/semanticTokens/full` in `Server::handle_message`.
- **Out of Scope**:
  - `textDocument/semanticTokens/range` (deferred to `LSP-08`).
  - `textDocument/semanticTokens/full/delta` (deferred to `LSP-08`).

### 2. Open Questions & User Clarifications
- **Q1 (Token Generation Architecture)**: Lexer stream + AST annotation vs. strict AST node traversal?
  - *Decision*: Lexer stream + AST annotation. Guarantees 100% syntax coverage, exact token lengths/positions, strictly monotonic line/col ordering, and precise semantic classification.
- **Q2 (Mutability Modifiers)**: Should `mut` variables and parameters receive the `modification` modifier?
  - *Decision*: Yes, mark `mut` variables and `mut` parameters with `modification` (bit 8), and immutable variables/parameters with `readonly` (bit 4).

### 3. Testing & Verification Matrix
- [x] **Unit Tests**: Verify delta encoding arithmetic and legend indexing in `compute_semantic_tokens`.
- [x] **Integration Tests**: Verify that semantic token calculation runs cleanly on invalid/in-flight source code without crashes or AST parsing abortions.
- [x] **Conformance Tests**: Add assertions in `tests/conformance_test.cpp` invoking `textDocument/semanticTokens/full` on `examples/01-hello-core/main.cf` and asserting `data` array length $> 0$ and delta-encoding validity.
- [x] **Manual Validation**: Query `textDocument/semanticTokens/full` on `examples/01-hello-core/main.cf` via CLI JSON-RPC and decode tokens to verify `audit` has modifier `modification` (8) and `record` has type `function` (8).

### 4. Implementation Status
- **Status**: Completed & Verified. All tests passing (`make test` 100% pass, `./cfire doc --check` clean). Ready to merge.

---

## Active Task: `LSP-04` Multi-File Cross-Workspace References & Rename

### 1. Scope & Problem Statement
- **Problem**: `textDocument/references` and `textDocument/rename` currently only scan the current file and packages directly imported by that file. If an exported symbol is defined in a sub-package (e.g. `fn compute_tag` in `pkg/checksum/checksum.cf`), importers across the workspace (e.g. `main.cf`) are not scanned. Furthermore, symbol occurrence scanning currently matches raw tokens without checking local vs. package scope, meaning renaming a local variable could inadvertently rename identical variable names in other functions or files.
- **In Scope**:
  - **Multi-File Workspace Discovery**: Recursively discover all `.cf` files in the workspace (using `workspace_root_` from `initialize`, or by discovering project root / `build.cf` upwards from the active file), ignoring `.cfire/`, `build/`, `.git/`, `vendor/`.
  - **In-Memory Document Overlay**: Overlay open document text (`docs_`) over disk files so unsaved edits are accurately reflected.
  - **Accurate Scope Partitioning**:
    - *Local variables & parameters*: Scoped strictly to their enclosing function / receiver method / test block in the defining document. Never leak to other functions or other files.
    - *Top-level / Exported symbols* (functions, structs, enums, interfaces): Scanned across the defining document and all workspace documents that either belong to the same package or import that package.
  - **Package-Qualified Usages**: In importer files (e.g., `tag := checksum.compute_tag("cfire")`), renaming `compute_tag` must rename only the member identifier token `compute_tag`, leaving `checksum.` intact.
  - **Multi-File Responses**:
    - `textDocument/references`: Returns `Location[]` spanning all files in the workspace where the symbol is referenced.
    - `textDocument/rename`: Returns `WorkspaceEdit` with `changes: { [uri: string]: TextEdit[] }` containing edits across all affected workspace files.
  - **`prepareRename` Filtering**: Reject renaming keywords, primitives (`i32`, `str`, `void`, etc.), operators, literals, and standard library module namespaces (`core:fs`, `core:io`, etc.).
- **Out of Scope**:
  - Renaming across C/C++ foreign files (`.c`, `.cpp`, `.h`).
  - Renaming standard library symbols (`core:*`).
  - Interactive multi-step refactoring wizards (e.g., extract function, inline variable).

### 2. Open Questions & User Clarifications
- **Q1 (Member Rename Precision)**: When renaming a package-exported symbol (e.g. `compute_tag` in package `checksum`), in importer files where it is called as `checksum.compute_tag(...)`, should only the identifier `compute_tag` be renamed?
  - *Decision*: Yes. Only the identifier token `compute_tag` is renamed; the package qualifier prefix `checksum.` is preserved.
- **Q2 (Fallback Workspace Root)**: If `initialize` does not supply `rootUri` or `workspaceFolders` (e.g. single-file editing mode), how should the workspace root be determined?
  - *Decision*: Walk up directories from the active document's path looking for `build.cf` or `.git`. If found, use that directory as the workspace root; otherwise, use the parent directory of the active document.
- **Q3 (Local vs. Top-Level Symbol Detection)**: How should local variables be distinguished from top-level symbols when the user triggers rename or find-references?
  - *Decision*: Check if the target position falls within a function/method/test body and matches a local variable, parameter, or receiver. If so, restrict reference search and rename edits exclusively to that function's token range in the current document.

### 3. Testing & Verification Matrix
- [x] **Unit Tests**:
  - Test workspace file discovery helper (`discover_workspace_cf_files`) ensuring recursive traversal includes all `.cf` files and skips `.cfire/`, `build/`, `.git/`, `c_deps/`.
  - Test symbol scope determination (distinguishing function-local variables from top-level package symbols via `build_scope_bindings`).
- [x] **Integration Tests**:
  - Cross-package reference resolution and rename on `examples/08-multi-pkg-cc-build` verifying edits across `pkg/checksum/checksum.cf` and `main.cf`.
- [x] **Conformance Tests**:
  - Assertions in `tests/conformance_test.cpp` (`test_formatter_lsp_and_docgen`):
    - `textDocument/references` on `compute_tag` from `pkg/checksum/checksum.cf` returns references in both `checksum.cf` and `main.cf`.
    - `textDocument/rename` on `compute_tag` returns a `WorkspaceEdit` containing edits for both URIs.
    - `textDocument/rename` on local variable `tag` in `main.cf` produces edits ONLY in `main.cf` inside `main()`.
    - `textDocument/prepareRename` on keyword `fn` returns `null`.
- [x] **Manual Validation**:
  - Run JSON-RPC queries via CLI on `examples/08-multi-pkg-cc-build` verifying exact JSON responses for `references` and `rename`.

### 4. Implementation Status
- **Status**: Completed & Verified. All tests passing (`make test` 100% pass, `./cfire doc --check` clean). Ready to merge.

---

## Active Task: `LSP-05` Auto-Import Completions for `core:*` Standard Library

### 1. Scope & Problem Statement
- **Problem**: Currently, typing `read_all` yields no completions unless the developer has already manually typed `import "core:fs"` and writes `fs.read_all`. Standard library functions should be discoverable unqualified, and selecting an unimported function should automatically inject the required `import "core:<mod>"` header at the top of the file via LSP `additionalTextEdits`.
- **In Scope**:
  - **Unqualified Stdlib Completions**: Suggest all top-level functions from `cfire::doc::stdlib_catalog()` in unqualified completions.
  - **Auto-Import Header Injection**: If `core:<mod>` is not already imported in the document, attach `additionalTextEdits` inserting `import "core:<mod>"\n` at the proper position.
  - **Qualified Completions Auto-Import**: If the developer types `fs.` but has not imported `core:fs`, selecting the function also auto-imports `import "core:fs"`.
  - **Filter Text & Label Formatting**:
    - `label`: `"read_all"`
    - `detail`: `"core:fs (auto-import)"` (or `"core:fs"` if already imported)
    - `insertText`: `"fs.read_all"`
    - `filterText`: `"read_all fs.read_all"`
    - `documentation`: function summary, signature, C++23 lowering preview
  - **Smart Import Placement**:
    - If document already has `import` statements, insert immediately after the last `import`.
    - If document has `package` but no imports, insert after `package`.
    - If empty or no `package`, insert at line 0, character 0.
    - If already imported, omit `additionalTextEdits`.
- **Out of Scope**:
  - Auto-importing external 3rd-party C++ headers without package metadata.
  - Auto-removing unused imports on save (deferred to linter).

### 2. Open Questions & User Clarifications
- **Q1 (Inserted Call Format)**: When selecting an unqualified auto-import completion (e.g. `read_all`), should the text inserted at the cursor be `fs.read_all`?
  - *Decision*: Yes (`fs.read_all`). In cfire, stdlib symbols are namespace-qualified under their module alias (`fs.read_all`), which prevents naming collisions.
- **Q2 (Qualified Trigger Auto-Import)**: If the user types `fs.` but has not yet imported `core:fs`, should selecting a function also inject `additionalTextEdits`?
  - *Decision*: Yes, ensuring consistent ergonomics whether typing qualified or unqualified.
- **Q3 (Handle Methods vs Top-Level Functions)**: Should handle methods (e.g. `TcpListener.close`) be included in unqualified completions?
  - *Decision*: No, instance methods should only appear when completing on an instance of that handle type (`listener.close`).

### 3. Testing & Verification Matrix
- [x] **Unit Tests**:
  - Tested `compute_import_edit` helper for files with existing imports, files with only `package`, and empty files.
- [x] **Integration Tests**:
  - Verified that a file using auto-imported `core:fs` compiles cleanly and executes properly.
- [x] **Conformance Tests**:
  - Assertions in `tests/conformance_test.cpp` (`test_formatter_lsp_and_docgen`):
    - Unqualified completion on file without `core:fs` (request 32) returns item with `label: "read_all"`, `insertText: "fs.read_all"`, `detail: "core:fs (auto-import)"`, and `additionalTextEdits` containing `import "core:fs"\n`.
    - Completion on file that already imports `core:fs` (request 33) returns `read_all` without `additionalTextEdits`.
- [x] **Manual Validation**:
  - CLI JSON-RPC completion queries verified both unqualified (`read_all`) and qualified (`fs.`) auto-import insertions and absence of edits when already imported.

### 4. Implementation Status
- **Status**: Completed & Verified. All tests passing (`make test` 100% pass, `./cfire doc --check` clean). Ready to merge.

---

## Active Task: `LSP-06` Exhaustive `match` Arm Generation Code Action

### 1. Scope & Problem Statement
- **Problem**: In `cfire`, matching on an algebraic enum requires exhaustiveness (`cfire/NON-EXHAUSTIVE-MATCH`). When developers write a new `match` statement or add variants to an enum, they currently have to look up each variant and payload signature by hand. The language server should provide a quick-fix (`textDocument/codeAction`) to auto-generate skeleton arms for all missing variants with one click.
- **In Scope**:
  - **Diagnostic-Triggered Code Actions**: Detect `cfire/NON-EXHAUSTIVE-MATCH` diagnostics on `match` statements.
  - **Missing Variant Extraction**: Extract target enum name and query declared variants from the current document or imported packages, identifying all unhandled variants.
  - **Arm Template Generation**:
    - For each missing variant: generate skeleton arm matching its payload count (e.g. `Empty => {\n\t\n}` or `Rect(v0, v1) => {\n\t\n}`).
    - Indent arms cleanly relative to the enclosing `match` statement.
  - **Two Available Quick-Fixes**:
    1. `"Fill missing match arms (cfire/NON-EXHAUSTIVE-MATCH)"` (primary, `isPreferred: true`).
    2. `"Add wildcard pattern '_ => { ... }' (cfire/NON-EXHAUSTIVE-MATCH)"`.
  - **Cursor-Triggered Code Action**: Also offer arm generation when the cursor is on the `match` keyword even before compilation finishes.
- **Out of Scope**:
  - Matching on non-enum types (integers, strings).
  - Semantic inference of complex guard conditions (`if expr`).

### 2. Open Questions & User Clarifications
- **Q1 (Arm Payload Variable Naming)**: For variants with payloads (e.g. `Rect(f64, f64)`), how should payload bindings be named?
  - *Decision*: Name bindings sequentially (`v0`, `v1`, etc.) so they are valid identifiers ready for editing.
- **Q2 (Single vs Multi-Step Quick-Fix)**: Should the quick-fix fill all missing variants at once or one by one?
  - *Decision*: Fill all missing variants at once in a single quick-fix action.
- **Q3 (Wildcard Quick-Fix)**: Should a secondary quick-fix be provided to add a wildcard `_ => { ... }` arm?
  - *Decision*: Yes, offer both "Fill missing match arms" (`isPreferred: true`) and "Add wildcard arm '_ => { ... }'".

### 3. Testing & Verification Matrix
- [x] **Unit Tests**:
  - Test arm generator helper with zero-payload variants, single-payload variants, and multi-payload variants.
- [x] **Integration Tests**:
  - Verify that applying the generated code action on `examples/07-types-match-map` resolves the `cfire/NON-EXHAUSTIVE-MATCH` diagnostic.
- [x] **Conformance Tests**:
  - Add assertions in `tests/conformance_test.cpp` (`test_formatter_lsp_and_docgen`):
    - `textDocument/codeAction` on non-exhaustive match returns quickfix `"Fill missing match arms"` with edits inserting missing variant templates.
    - `textDocument/codeAction` also returns `"Add wildcard pattern '_ => { ... }'"`.
- [x] **Manual Validation**:
  - CLI JSON-RPC session triggering code actions on an incomplete `match` and verifying generated text and insertion line/column.

### 4. Implementation Status
- **Status**: Completed & Verified. All tests passing (`make test` 100% pass, `./cfire doc --check` clean). Ready to merge.

---

## Active Task: `LSP-07` Sub-expression Type Hover & Struct Memory Layout Inspection

### 1. Scope & Problem Statement
- **Problem**:
  1. Developers hovering on structs (`Metric`, `AuditLog`) only see field names without memory footprint details. Knowing exact 64-bit alignment, byte offsets, and padding is essential for high-performance systems programming.
  2. Hovering on operators (`+`, `*`, `==`) or sub-expressions currently returns `null`. Developers want immediate feedback on evaluated expression types and their lowered C++23 counterparts.
- **In Scope**:
  - **Struct Memory Layout Inspection**:
    - Compute 64-bit layout (size, alignment, field byte offsets, tail padding) for all primitive types (`i8`-`i64`, `u8`-`u64`, `f32`, `f64`, `bool`, `char`, `str`, `rawptr`, `own[T]`, `shared[T]`, `[]T`, `map[K, V]`, and user nested structs).
    - Annotate struct hover cards with `Size: X bytes | Align: Y bytes`.
    - Add `Offset` column to struct field tables: `| Field | Offset | .cf Type | Lowered C++23 | Tags |`.
    - When hovering on a struct field access (`m.Service`), display field offset and size: `Offset: 0 | Size: 24 bytes`.
  - **Sub-expression Type Hover**:
    - Locate innermost AST expression covering `(line, character)` when hovering on expressions/operators.
    - Compute inferred type using `infer_expr_cf_type`.
    - Render hover card showing expression text, inferred `.cf` type, and lowered C++23 type preview.
- **Out of Scope**:
  - 32-bit architecture layout simulation (cfire targets standard 64-bit ABI).
  - Bitfield packing annotations (cfire structs are naturally aligned aggregates).

### 2. Open Questions & User Clarifications
- **Q1 (Standard 64-bit Type Sizing)**: What sizes are assumed for standard library types?
  - *Decision*: Standard 64-bit ABI: `str` is 24 bytes (`std::string`), `[]T` is 24 bytes (`std::vector<T>`), `own[T]` / `rawptr` is 8 bytes (`std::unique_ptr<T>`), `shared[T]` is 16 bytes (`std::shared_ptr<T>`), `map[K, V]` is 40 bytes.
- **Q2 (Struct Offset Display Format)**: How should offsets be shown in the fields table?
  - *Decision*: Show decimal byte offset (e.g. `+0`, `+8`, `+16`, `+24`) in an `Offset` column for maximum readability.
- **Q3 (Sub-expression Hover Priority)**: If hovering over an identifier within an expression, should hover show the identifier symbol or the full expression?
  - *Decision*: Keep identifier hover primary when cursor is directly on the identifier; fall back to sub-expression hover when cursor is on operators (`+`, `-`, `*`, `==`), parentheses, or when identifier resolution produces no hover card.

### 3. Testing & Verification Matrix
- [x] **Unit Tests**:
  - Layout helper calculating size, align, and offsets for scalars, strings, vectors, and nested structs.
- [x] **Integration Tests**:
  - Hovering on `AuditLog` verifies `Size: 24 bytes | Align: 8 bytes` and `+0` offset for `Entries`.
  - Hovering on `Metric` verifies `Size: 32 bytes | Align: 8 bytes` (`Service` +0, `Count` +24).
- [x] **Conformance Tests**:
  - Add assertions in `tests/conformance_test.cpp` (`test_formatter_lsp_and_docgen`):
    - Hover on `AuditLog` returns layout annotation `Size: 24 bytes | Align: 8 bytes`.
    - Hover on `m.Service` returns field offset and size info.
    - Hover on binary expression operator returns evaluated type.
- [x] **Manual Validation**:
  - Live LSP JSON-RPC verification of struct hover and sub-expression hover.

### 4. Implementation Status
- **Status**: Completed & Verified. All tests passing (`make test` 100% pass, `./cfire doc --check` clean). Ready to merge.

---

## Active Task: `LSP-08` Debounced Background Diagnostics Worker Thread

### 1. Scope & Problem Statement
- **Problem**: In `cfire-lsp`, diagnostics are currently recomputed synchronously inside `publish_diagnostics` on every `textDocument/didChange` notification. When typing rapidly in editors (Zed / VS Code), each keystroke triggers a full re-parse, workspace import discovery, and semantic analysis pass synchronously on the JSON-RPC main thread, blocking interactive requests like `textDocument/completion` and `textDocument/hover`.
- **In Scope**:
  - Implement a dedicated background diagnostics worker thread (`std::thread`, `std::mutex`, `std::condition_variable`) in `cfire::lsp::Server`.
  - Debounce `textDocument/didChange` events per URI by ~150ms. Repeated typing events within this window update the text buffer snapshot and reset the debounce deadline.
  - Trigger immediate diagnostics (0ms delay) on `textDocument/didOpen` and `textDocument/didSave` to provide instant feedback upon opening or saving files.
  - Thread-safe outbound RPC writes: Guard `out` stream writes with a mutex (`out_mutex_`) to prevent concurrent interleaving of responses and notifications.
  - Thread-safe caches: Protect `cpp_header_cache_` and `last_diagnostics_` with mutexes against concurrent access.
  - Clean lifecycle: Clean shutdown and drain in `Server` destructor or upon receiving `exit` / EOF, ensuring pending diagnostics are flushed and threads joined without leaks.
- **Out of Scope**:
  - Multi-threaded parallel AST parsing across distinct files (single background worker thread is sufficient and optimal for single-user language server workloads).
  - Cancellation tokens for in-flight semantic analysis (cfire frontend is already sub-millisecond per file).

### 2. Open Questions & User Clarifications
- **Q1 (Debounce Delay)**: What should the debounce delay be?
  - *Decision*: 150ms for `textDocument/didChange` (industry standard used by rust-analyzer and clangd). `textDocument/didOpen` and `textDocument/didSave` run immediately with 0ms delay.
- **Q2 (Shutdown / Drain Behavior)**: What should happen on stream EOF or `exit`?
  - *Decision*: Drain remaining pending diagnostics and join the background worker thread before returning, ensuring conformance test deterministic output and clean process termination.
- **Q3 (RPC Outbound Serialization)**: How to prevent JSON-RPC frame corruption between the main RPC thread and worker thread?
  - *Decision*: Wrap all outbound writes to `std::ostream& out` in `Server::write_rpc` synchronized by a `std::mutex`.

### 3. Testing & Verification Matrix
- [x] **Unit Tests**:
  - Test worker queue scheduling, debounce postponement on repeated updates, and deadline triggering.
- [x] **Integration Tests**:
  - Verify that rapid `didChange` notifications do not block concurrent `completion` or `hover` requests.
- [x] **Conformance Tests**:
  - Add test in `tests/conformance_test.cpp` verifying that rapid consecutive `didChange` events are debounced and `publishDiagnostics` emits correctly for opened, changed, and saved documents.
- [x] **Manual Validation**:
  - Live interactive LSP testing verifying responsiveness and zero UI lockup during typing bursts.

### 4. Implementation Status
- **Status**: Completed & Verified. All tests passing (`make test` 100% pass, `./cfire doc --check` clean). Ready to merge.

---

## Active Task: `LSP-09` Resilient Statement-Level AST Recovery in Parser

### 1. Scope & Problem Statement
- **Problem**: The `cfire` parser (`src/parser.hpp`) is currently a recursive-descent fail-stop parser. When any statement or declaration contains a syntax error (e.g., an incomplete expression on line 5 during active typing), `parse_file` immediately returns `std::unexpected` and abandons the entire file's AST. Consequently:
  1. Valid functions, structs, and methods on lines 10–20 are dropped from the AST.
  2. Language server features (document symbols outline, hover, type definition, go-to-definition, and signature help) break across the whole file.
  3. Only a single syntax error can be reported at a time, hiding subsequent syntax issues.
- **In Scope**:
  - Implement statement-level panic-mode error recovery in `Parser::parse_block` and `Parser::parse_stmt`.
    - When a statement fails to parse, record the error and synchronize to the next statement boundary (statement starters: `return`, `if`, `for`, `match`, `var`, `mut`, `defer`, `cplusplus`, `async`, identifier assignments; or block terminator `}`).
  - Implement top-level declaration recovery in `Parser::parse_file`.
    - When a top-level item fails to parse, record the error and synchronize to the next top-level keyword (`fn`, `type`, `import`, `var`, `test`, `EOF`).
  - Introduce `ParseResult` struct (`FileAST ast`, `std::vector<std::string> errors`, `bool ok()`).
  - Provide `parse_source_resilient(path, src)` returning `ParseResult`.
  - Maintain `parse_source(path, src)` returning `std::expected<FileAST, std::string>` (returning `unexpected` with the first error if `!errors.empty()`) so all non-LSP compiler pipelines (`cfire build`, `cfire run`, etc.) continue to fail cleanly on syntax errors.
  - Integrate `parse_source_resilient` into `src/lsp.hpp`:
    - `get_or_parse_ast`: use the recovered AST so hovering, document outline, and completions work on valid functions even when earlier statements have syntax errors.
    - `compute_diagnostics_for_doc`: report all recovered syntax errors.
- **Out of Scope**:
  - Multi-token predictive synthesis (cfire's grammar is clean and naturally statement-separated).
  - Code generation on malformed ASTs (the compiler stops before codegen if syntax errors exist).

### 2. Open Questions & User Clarifications
- **Q1 (Synchronization Starters)**: What tokens act as statement synchronization boundaries?
  - *Decision*: Statement keywords (`return`, `if`, `for`, `match`, `var`, `mut`, `defer`, `cplusplus`, `async`), block closer `}`, or top-level declaration keywords (`fn`, `type`, `import`, `test`, `EOF`).
- **Q2 (Semantic Analysis during Syntax Errors)**: Should semantic analysis (`sema::analyze`) run if syntax errors exist?
  - *Decision*: In `compute_diagnostics_for_doc`, publish all syntax errors first. The recovered AST is used by `get_or_parse_ast` for hover, document symbols, and completion across valid functions.
- **Q3 (Cascade Error Limit)**: To prevent runaway cascading errors on severely corrupt input, should there be an error threshold?
  - *Decision*: Cap syntax error collection at 20 errors per file.

### 3. Testing & Verification Matrix
- [x] **Unit Tests**:
  - Verify `parse_source_resilient` on a source file containing a broken statement inside function 1, ensuring function 2 is parsed and present in `ast.functions`.
- [x] **Integration Tests**:
  - Verify that `cfire-lsp` emits document symbols for all valid functions in a file despite a broken statement.
  - Verify that hover and completion work in function 2 when function 1 has a syntax error.
- [x] **Conformance Tests**:
  - Add test in `tests/conformance_test.cpp` verifying AST recovery and multi-error diagnostic publication.
- [x] **Manual Validation**:
  - Live verification that syntax errors on earlier lines do not wipe out editor outline / symbols in later lines.

### 4. Implementation Status
- **Status**: Completed & Verified. All tests passing (`make test` 100% pass, `./cfire doc --check` clean). Ready to merge.

---

## Active Task: `LSP-10` Selection Range Formatting

### 1. Scope & Problem Statement
- **Problem**: `cfire-lsp` currently supports whole-document formatting via `textDocument/formatting`. When developers highlight a specific block of code in their editor (Zed / VS Code) and trigger "Format Selection" (`Cmd+K, Cmd+F` or editor context menu), the editor sends `textDocument/rangeFormatting`. Currently, this method is unsupported (`documentRangeFormattingProvider: false`), so editors fail or reflow the entire file.
- **In Scope**:
  - Declare `"documentRangeFormattingProvider": true` in `cfire-lsp` server capabilities.
  - Implement `format_range(src, start_line, end_line, filename)` in `src/formatter.hpp`:
    - Lexically scan the document from line 0 to establish the enclosing block indentation context (brace depth, raw string state).
    - Format and normalize lines strictly within `[start_line, end_line]`.
    - Handle struct field/tag column alignments if the selection covers a struct body.
  - Implement `textDocument/rangeFormatting` handler in `src/lsp.hpp` returning bounded `TextEdit`s.
- **Out of Scope**:
  - Intra-line expression wrapping (cfire formatting is line/statement oriented).

### 2. Open Questions & User Clarifications
- **Q1 (Selection Boundary Expansion)**: How should partial-line selections (e.g., column 5 of line 2 to column 10 of line 4) be handled?
  - *Decision*: Expand to full line boundaries: replace from line `start_line` column 0 to `end_line` line end (standard convention in clangd, rust-analyzer, and gopls).
- **Q2 (Bounded Edits)**: How should the result be returned?
  - *Decision*: Return a single `TextEdit` covering lines `[start_line, end_line]`, ensuring lines outside the selection are completely untouched.

### 3. Testing & Verification Matrix
- [x] **Unit Tests**:
  - Verified `format_range` on a 3-line selection within an unindented function, ensuring only those 3 lines are modified with proper 4-space indentation.
- [x] **Integration Tests**:
  - Verified that lines outside the selected range retain their original formatting and formatting an already formatted selection returns `[]`.
- [x] **Conformance Tests**:
  - Added test in `tests/conformance_test.cpp` for `textDocument/rangeFormatting` verifying that the response returns bounded `TextEdit` matching the acceptance criteria.
- [x] **Manual Validation**:
  - Verified `documentRangeFormattingProvider: true` in initialize capabilities response and tested live RPC range formatting.

### 4. Implementation Status
- **Status**: Completed & Verified. All tests passing (`make test` 100% pass, `./cfire doc --check` clean). Ready to merge.

---

## Active Task: `BS-01` & `LIB-12` Toolchain Documentation & Core Library Catalog Alignment

### 1. Scope & Problem Statement
- **Problem**:
  1. `BS-01`: `docs/GUIDE.md` Chapter 8 documents a phantom programmatic API (`pub fn configure(mut b: build.Context)`) that does not exist in `src/buildsys.hpp`, which expects declarative top-level `var X = build.<Driver>()` expressions.
  2. `LIB-12`: There are 7 naming and return type discrepancies between `runtime/cfire_core_stdlib.hpp` and `src/docgen.hpp` (e.g. `read_bytes` vs `read_all`, missing `str.contains`, missing `math.clamp_f64`, missing `rand.u64`). Furthermore, `core:rand` seeds MT19937 with a static constant `0xC0FFEE123456789ULL`.
- **In Scope**:
  - Update `src/docgen.hpp` Chapter 8 markdown and regenerate `docs/GUIDE.md` / `docs/guide.html` to document the canonical declarative `build.cf` syntax (`var Lib = build.CC(...)`, `build.CMake(...)`, `build.Make(...)`, `build.Ninja(...)`).
  - In `runtime/cfire_core_stdlib.hpp`:
    - Add `read_all` and `write_all` aliases to `cfire::fs`.
    - Add `contains` and `replace` to `cfire::str`.
    - Add `clamp_f64` and `sqrt` alias to `cfire::math`.
    - Add `u64()` and `f64_unit()` to `cfire::rand`, and seed MT19937 with `std::random_device{}()`.
    - Add `exec_output` alias to `cfire::os`.
  - In `src/docgen.hpp`: ensure `docgen.hpp` catalog signatures match `runtime/cfire_core_stdlib.hpp` 100%.
  - Run `./cfire doc` to regenerate docs and verify with `./cfire doc --check`.
  - Add test assertions in `tests/conformance_test.cpp`.
- **Out of Scope**:
  - New HTTP client/server modules (tracked in `LIB-01`).
  - Safe `os/exec` subprocess piping rewrite (tracked in `LIB-02`).

### 2. Open Questions & User Clarifications
- **Q1 (build.cf syntax direction)**: Should we standardize on the declarative `var X = build.CC(...)` syntax or implement the imperative `build.Context` runtime interpreter?
  - *Decision*: User approved standardizing on declarative `var Target = build.CC(...)`. It is clean, tool-scannable, matches all existing examples, and requires zero interpreter overhead.

### 3. Testing & Verification Matrix
- [x] **Unit Tests**:
  - Verify that `fs.read_all`, `fs.write_all`, `str.contains`, `str.replace`, `math.clamp_f64`, `math.sqrt`, `rand.u64`, `rand.f64_unit`, and `os.exec_output` compile and execute properly in C++23.
- [x] **Integration Tests**:
  - Verify that `./cfire doc` generates clean `docs/GUIDE.md` and `docs/guide.html` without warnings.
- [x] **Conformance Tests**:
  - Run `make test` and `./cfire doc --check` to verify 100% passing and zero desync.
- [x] **Manual Validation**:
  - Verify `build.cf` in `examples/08-multi-pkg-cc-build` and `examples/10-cmake-and-make-deps` matches the documented guide.

### 4. Implementation Status
- **Status**: Completed & Verified. All tests passing (`make test` 100% pass with 0 warnings, `./cfire doc --check` clean). Ready to merge.

---

## Active Task: `BS-02` Object-Level Timestamp / Content Caching for `build.CC`

### 1. Scope & Problem Statement
- **Problem**:
  1. `build.CC` recompiles every C/C++ translation unit from scratch on every run or build, introducing unnecessary latency.
  2. In libraries where two source files share the same stem (e.g. `src/parser.c` and `vendor/parser.c`), both output to `obj_dir / "parser.o"`, causing silent object overwrites and missing symbol link errors.
- **In Scope**:
  - In `src/buildsys.hpp`:
    - Disambiguate object file names by appending a compact hash of the source file's relative path to the stem (e.g. `parser_<hash>.o`).
    - Implement timestamp-based incremental compilation: check `std::filesystem::last_write_time(obj_path) >= std::filesystem::last_write_time(sf)`. If true, skip the `clang`/`clang++` invocation.
    - Incremental archive creation: if `archive_path` exists and its `last_write_time` is greater than or equal to all object files, skip `ar rcs`.
- **Out of Scope**:
  - Full C/C++ header dependency graph tree scanning (`-MMD`). Timestamp checking covers source file changes, which accelerates builds by orders of magnitude for unmodified packages.

### 2. Open Questions & User Clarifications
- None required; specifications are fully determined.

### 3. Testing & Verification Matrix
- [x] **Unit Tests**: Verify relative path hash disambiguation and mtime comparison logic.
- [x] **Integration Tests**: Verify that recompiling an unchanged dependency completes without invoking `clang` or `ar`.
- [x] **Conformance Tests**: `make test` passing with zero warnings.
- [x] **Manual Validation**: Verify `examples/08-multi-pkg-cc-build` and `examples/10-cmake-and-make-deps` succeed and benefit from caching.

### 4. Implementation Status
- **Status**: Completed & Verified. All tests passing (`make test` 100% pass with 0 warnings, `./cfire doc --check` clean). Ready to merge.

---

## Active Task: `LIB-03` Cryptographic Suite & True CSPRNG (`core:crypto/sha256`, `core:crypto/rand`)

### 1. Scope & Problem Statement
- **Problem**:
  1. `core:crypto/blake3` currently mislabels SHA-256 on Apple and an ad-hoc 64-bit mixer on other OSes as "blake3". Developers need a reliable, standard SHA-256 implementation that yields standard NIST FIPS 180-4 test vectors on all platforms.
  2. Developers need HMAC-SHA256 for API authentication and webhook verification.
  3. Developers need true CSPRNG bytes (`rand.bytes(n)`) backed by OS entropy (`arc4random_buf` on macOS/BSD, `/dev/urandom` / `getentropy` on Linux).
- **In Scope**:
  - In `runtime/cfire_core_stdlib.hpp`:
    - Implement self-contained, portable, zero-external-dependency standard SHA-256 in `cfire::sha256`.
    - Provide `cfire::sha256::hash_hex(std::span<const std::byte>) -> std::string` and `cfire::sha256::hash_bytes(...) -> std::vector<std::byte>`.
    - Provide `cfire::hmac::sha256_hex(key, data) -> std::string`.
    - Provide `cfire::rand::bytes(std::size_t n) -> std::vector<std::byte>`.
    - Maintain `cfire::blake3::hash_hex` as backward-compatible alias.
  - In `src/docgen.hpp`:
    - Document `core:crypto/sha256`, `core:crypto/hmac`, and `core:crypto/rand`.
    - Regenerate docs via `./cfire doc`.
- **Out of Scope**:
  - AES-GCM cipher streaming (deferred to future `LIB` expansion).

### 2. Open Questions & User Clarifications
- None required; standard NIST FIPS 180-4 and RFC 2104 specifications.

### 3. Testing & Verification Matrix
- [x] **Unit Tests**: Verify standard NIST SHA-256 vectors (e.g. empty string, `"abc"`) and HMAC-SHA256 test vectors in `tests/conformance_test.cpp`.
- [x] **Integration Tests**: Verify `./cfire doc --check` clean synchronization.
- [x] **Conformance Tests**: `make test` passing with 0 warnings.
- [x] **Manual Validation**: Verify `rand.bytes(32)` produces high-entropy non-repeating data.

### 4. Implementation Status
- **Status**: Completed & Verified. All tests passing (`make test` 100% pass with 0 warnings, `./cfire doc --check` clean). Ready to merge.

---

## Active Task: `LD-11` Composable Expression-Level `try` Lowering

### 1. Scope & Problem Statement
- **Problem**:
  - `try` is parsed as an expression (`TryExpr`), but when used inside sub-expressions (e.g. `val := 10 + try f()`), codegen stripped the `try` keyword entirely, resulting in C++ compilation errors (`int + std::expected`).
  - Developers need `try` to work seamlessly anywhere an expression is expected, propagating errors immediately on failure and unwrapping the value on success.
- **In Scope**:
  - In `src/parser.hpp`:
    - Treat `KwTry` as unary operator in `parse_unary_expr`, enabling `try` anywhere a unary expression is valid.
  - In `src/codegen.hpp`:
    - Inside `emit_expr` for `TryExpr`, generate a GNU/Clang statement expression `({ ... })`:
      - Evaluates the inner expression once.
      - If `!_res.has_value()`, returns early (`return std::unexpected(std::move(_res).error());`).
      - On success, unwraps and yields `cfire::unwrap_val(std::forward<decltype(_res)>(_res))`.
    - Handle `in_main` and `in_test` context appropriately (if in `main`/test, print error to stderr and exit with code 1).
- **Out of Scope**:
  - Custom error handling match expressions (`match err { ... }` is already supported via `or err { ... }`).

### 2. Open Questions & User Clarifications
- None required; Clang/GCC statement expressions are fully supported across our C++23 target matrix.

### 3. Testing & Verification Matrix
- [x] **Unit Tests**: Lowering of `10 + try f()` and nested `try` calls.
- [x] **Integration Tests**: Full compilation and execution of programs using expression-level `try`.
- [x] **Conformance Tests**: Add test assertions in `tests/conformance_test.cpp`.
- [x] **Manual Validation**: Test early error return when fallible expression fails inside an arithmetic operation.

### 4. Implementation Status
- **Status**: Completed & Verified. All tests passing (`make test` 100% pass with 0 warnings, `./cfire doc --check` clean). Ready to merge.

---

## Active Task: `BS-03` Structured Header AST Ingestion (Preserve Namespaces & Overloads)

### 1. Scope & Problem Statement
- **Problem**:
  - `src/clang_bridge.hpp` parses Clang JSON AST dumps via crude substring matching (`"kind": "FunctionDecl"`, `"name": "..."`).
  - This drops enclosing C++ namespaces (e.g. `namespace ImGui`, `namespace Raylib`), maps symbols only by their bare unqualified name, and causes overloaded functions to overwrite each other in `HeaderIndex::symbols`.
- **In Scope**:
  - In `src/clang_bridge.hpp`:
    - Implement a structured, self-contained recursive AST visitor that walks the JSON AST nodes.
    - Track active namespace scopes (`NamespaceDecl`, `CXXRecordDecl`, `LinkageSpecDecl`) to construct full qualified symbol paths (e.g. `ImGui::Button`, `std::sqrt`).
    - Record function overloads: allow multiple signatures under qualified names or disambiguated parameter-hash keys.
    - Index structs, typedefs, and enums with their full scope qualifiers.
- **Out of Scope**:
  - Direct Clang libTooling C++ linking (requires heavy LLVM/Clang shared libraries; keeping `clang -Xclang -ast-dump=json` ensures zero build dependency footprint).

### 2. Open Questions & User Clarifications
- None required; fully specified.

### 3. Testing & Verification Matrix
- [x] **Unit Tests**: Test AST JSON extraction with nested namespaces (`namespace A { namespace B { void f(int); } }`) in `tests/conformance_test.cpp`.
- [x] **Integration Tests**: Verify header cache file persistence format in `.cfire/cache/ast/*.idx`.
- [x] **Conformance Tests**: `make test` passing with 0 warnings.
- [x] **Manual Validation**: Verify `cpp::` completions and bridge hover in LSP for namespaced symbols.

### 4. Implementation Status
- **Status**: Completed & Verified. All tests passing (`make test` 100% pass, `./cfire doc --check` clean). Ready to merge.

---

## Active Task: `LD-12` Definite Divergence Verification for `or err { ... }` Blocks

### 1. Scope & Problem Statement
- **Problem**:
  - In `val := fallible() or err { ... }`, if the block does not return or diverge, codegen continues and unconditionally dereferences the empty `std::expected` (`*tmp`), causing runtime crash / undefined behavior.
- **In Scope**:
  - In `src/sema.hpp`:
    - Add control-flow divergence analysis (`block_diverges`).
    - Require that every `or err { ... }` block definitely diverges via `return`, `panic()`, or `exit()`.
    - Emit compile-time diagnostic: `cfire/NON-DIVERGING-OR-BLOCK`.
  - In `src/docgen.hpp`:
    - Register `cfire/NON-DIVERGING-OR-BLOCK` in `diagnostics_catalog()` with rejected and accepted examples.
    - Synchronize docs via `./cfire doc`.
  - In `src/lsp.hpp`:
    - Ensure diagnostic is surfaced by LSP and compiler driver.
- **Out of Scope**:
  - Value-yielding `or err` blocks (`or err { yield default_val }`).

### 2. Open Questions & User Clarifications
- None required; strict divergence requirement aligns with Rust `let ... else { return; }` and Zig error return semantics.

### 3. Testing & Verification Matrix
- [x] **Unit Tests**: Reject non-diverging `or err { println("err") }` with `cfire/NON-DIVERGING-OR-BLOCK`.
- [x] **Integration Tests**: Accept diverging `or err { return error(...) }` and `or err { panic(...) }`.
- [x] **Conformance Tests**: Add test assertions in `tests/conformance_test.cpp`.
- [x] **Manual Validation**: Verify `./cfire doc --check` clean synchronization.

### 4. Implementation Status
- **Status**: Completed & Verified. All tests passing (`make test` 100% pass, `./cfire doc --check` clean). Ready to merge.

---

## Active Task: `LIB-02` Safe Subprocess Execution & Streaming IPC (`core:os/exec`)

### 1. Scope & Problem Statement
- **Problem**:
  - `core:os.exec` and `exec_output` currently use `popen(cmd, "r")`, which passes a single string through `/bin/sh -c`. This is vulnerable to shell injection and breaks on filenames with spaces or special characters.
- **In Scope**:
  - In `runtime/cfire_core_stdlib.hpp`:
    - Add `cfire::os::ExecResult` struct containing `exit_code: i32`, `stdout_str: str`, `stderr_str: str`.
    - Implement `cfire::os::command(binary, [args...]) -> ExecResult!` using `posix_spawnp` (or `pipe` + `fork` + `execvp`), capturing stdout and stderr separately with zero shell interpolation.
    - Retain `cfire::os::exec_output` and `cfire::os::exec` for backward compatibility.
  - In `src/docgen.hpp`:
    - Add `command` to `core:os` catalog and update `docs/`.
- **Out of Scope**:
  - Asynchronous background child process process handles (deferred to `LIB` expansion).

### 2. Open Questions & User Clarifications
- None required; standard POSIX parameter-list subprocess execution.

### 3. Testing & Verification Matrix
- [x] **Unit Tests**: Execute `echo` with arguments containing spaces and quotes without shell mangling.
- [x] **Integration Tests**: Verify stdout vs stderr separation and exit code propagation.
- [x] **Conformance Tests**: `make test` passing with 0 warnings.
- [x] **Manual Validation**: Run `./cfire doc --check`.

### 4. Implementation Status
- **Status**: Completed & Verified. All tests passing (`make test` 100% pass, `./cfire doc --check` clean). Ready to merge.

---

## Active Task: `LD-14` Remove Phantom Parameter Intent `forward`

### 1. Scope & Problem Statement
- **Problem**:
  - `forward` is accepted in function signatures as a parameter intent, but it silently lowers to `const T&` (identical to `in`) and is unsupported at call sites. This creates misleading expectations around universal forwarding references.
- **In Scope**:
  - Remove `TokenKind::KwForward` and keyword `"forward"` from `src/lexer.hpp`.
  - Remove `ParamIntent::Forward` from `src/ast.hpp`.
  - Remove parsing of `forward` intent in `src/parser.hpp`.
  - Remove semantic checks/handling in `src/sema.hpp`.
  - Remove lowering in `src/codegen.hpp`.
  - Remove LSP token/snippet entries for `forward` in `src/lsp.hpp`.
  - Remove `forward` from documentation in `src/docgen.hpp` and regenerate `docs/`.
- **Out of Scope**:
  - Designing a full universal forwarding / template reference system (deferred).

### 2. Open Questions & User Clarifications
- Confirmed: parameter intents are strictly `in`, `mut`, `out`, and `move`.

### 3. Testing & Verification Matrix
- [x] **Unit Tests**: Confirm parser rejects `fn bad(forward x: i32)` with expected syntax error.
- [x] **Integration Tests**: Verify existing parameter intents (`in`, `mut`, `out`, `move`) continue functioning across compiler pipeline.
- [x] **Conformance Tests**: `make test` passing with 0 warnings.
- [x] **Manual Validation**: `./cfire doc --check` clean.

### 4. Implementation Status
- **Status**: Completed & Verified. All tests passing (`make test` 100% pass, `./cfire doc --check` clean). Ready to merge.

---

## Active Task: `LD-08` Standardize Foreign Escape Hatch to `cpp { ... }`

### 1. Scope & Problem Statement
- **Problem**:
  - `docs/GUIDE.md` documents `cpp { ... }`, but the lexer only recognized `cplusplus { ... }`.
  - User requested: "keep it simple and only support either cpp or cplusplus, but not both" with selection to standardize on `cpp`.
- **In Scope**:
  - Update lexer in `src/lexer.hpp` to recognize `cpp { ... }` as raw C++ escape block token (and remove `cplusplus`).
  - Update parser in `src/parser.hpp` and AST in `src/ast.hpp` to reflect `cpp { ... }`.
  - Update LSP snippets in `src/lsp.hpp` to suggest `cpp {\n\t${0}\n}`.
  - Update all examples and tests using `cplusplus {` to `cpp {`.
  - Update `src/docgen.hpp` and regenerate docs.
- **Out of Scope**:
  - Dual keyword support (user explicitly chose sole keyword `cpp`).

### 2. Open Questions & User Clarifications
- Resolved: user chose single keyword `cpp` as the sole foreign C++ block syntax.

### 3. Testing & Verification Matrix
- [x] **Unit Tests**: Lex and parse `cpp { ... }` correctly.
- [x] **Integration Tests**: Verify compiler generates identical verbatim C++ output.
- [x] **Conformance Tests**: Update all occurrences across examples/tests and run `make test`.
- [x] **Manual Validation**: Run `./cfire doc --check`.

### 4. Implementation Status
- **Status**: Completed & Verified. All tests passing (`make test` 100% pass, `./cfire doc --check` clean). Ready to merge.

---

## Active Task: `LIB-01` High-Level HTTP Client, Server & URL Parsing (`core:net/http`, `core:net/url`)

### 1. Scope & Problem Statement
- **Problem**:
  - Currently `core:net` only provides low-level raw TCP sockets (`TcpListener`, `TcpStream`). Developers have no built-in way to parse URLs, make HTTP GET/POST requests, or spin up a simple REST API server without writing manual RFC 7230 socket loops.
- **In Scope**:
  - In `runtime/cfire_core_stdlib.hpp`:
    - Add `cfire::url::Url` struct: `scheme`, `host`, `port`, `path`, `query`, `fragment`, and query param map.
    - Implement `cfire::url::parse(std::string_view raw) -> std::expected<Url, cfire::Error>`.
    - Implement `cfire::url::encode(std::string_view)` and `cfire::url::decode(std::string_view)`.
    - Add `cfire::http::Response` struct: `status_code: i32`, `headers: std::unordered_map<std::string, std::string>`, `body: std::string`.
    - Implement `cfire::http::get(url, headers)` using native POSIX TCP socket + HTTP/1.1 parsing.
    - Implement `cfire::http::post(url, body, content_type, headers)`.
    - Implement `cfire::http::make_server(host, port, handler)` for lightweight HTTP request dispatching.
  - In `src/docgen.hpp`:
    - Register `core:net/http` and `core:net/url` in stdlib catalog.
    - Regenerate docs.
- **Out of Scope**:
  - Complex HTTP/2 or HTTP/3 (HTTP/1.1 provides standard zero-dependency baseline).
  - OpenSSL TLS client dependency (allow HTTP requests natively; HTTPS can connect through system curl / proxy or future TLS bridge).

### 2. Open Questions & User Clarifications
- Zero external dependencies: implemented cleanly over POSIX sockets in `cfire_core_stdlib.hpp`.

### 3. Testing & Verification Matrix
- [x] **Unit Tests**: Test URL parsing (components, query string decoding) and URL encoding.
- [x] **Integration Tests**: Spin up loopback `http.make_server` on an ephemeral port, execute `http.get` and `http.post` against it, and verify response status and body.
- [x] **Conformance Tests**: Add assertions in `tests/conformance_test.cpp`.
- [x] **Manual Validation**: `./cfire doc --check` clean.

### 4. Implementation Status
- **Status**: Completed & Verified. All tests passing (`make test` 100% pass, `./cfire doc --check` clean). Ready to merge.

---

## Active Task: `LD-01` Recursive Monadic Type Orthogonality (`T!` and `T?` in Containers & Fields)

### 1. Scope & Problem Statement
- **Problem**:
  - `is_optional` (`?`) and `is_fallible` (`!`) are implemented as shallow flags on `TypeRef`.
  - When used on struct fields (e.g. `Item: str?`) or nested in collections (`[]i32!`, `map[str]User?`), `lower_owning_type` drops the monadic wrappers and lowers to bare `std::string` or `std::vector<int32_t>`, destroying error and nullability tracking.
- **In Scope**:
  - Maintain the concise, clean user-facing syntax `T?` and `T!` (avoiding verbose `Optional[T]` / `Result[T]` keywords).
  - In `src/ast.hpp`:
    - Ensure `TypeRef` models `TypeRefKind::Optional` and `TypeRefKind::Fallible` wrapping an inner `std::shared_ptr<TypeRef>`, or ensure flags apply recursively to element/value types.
  - In `src/parser.hpp`:
    - Parse postfix `?` and `!` recursively on arbitrary types (e.g. `[]i32!`, `str?`).
  - In `src/codegen.hpp`:
    - Unify lowering so any `TypeRef` with `is_optional` or `is_fallible` lowers recursively to `std::optional<T>` and `std::expected<T, cfire::Error>` inside struct fields, variables, and container elements.
- **Out of Scope**:
  - Monad transformers or functor mapping syntax (keep lowering directly to standard C++23 vocabulary types).

### 2. Open Questions & User Clarifications
- Retain concise `T?` and `T!` syntax per user discussion; no verbose keyword bloat.

### 3. Testing & Verification Matrix
- [x] **Unit Tests**: Parse `type Item struct { Name: str?, Price: f64! }` and `arr: []i32!`.
- [x] **Integration Tests**: Verify lowering produces `std::optional<std::string>`, `std::expected<double, cfire::Error>`, and `std::vector<std::expected<int32_t, cfire::Error>>`.
- [x] **Conformance Tests**: Add test assertions in `tests/conformance_test.cpp` compiling and running code with nested monadic fields.
- [x] **Manual Validation**: `./cfire doc --check` clean.

### 4. Implementation Status
- **Status**: Completed & Verified. All tests passing (`make test` 100% pass, `./cfire doc --check` clean). Ready to merge.

---

## Active Task: `LD-03` Structured Concurrency Integrity & Destruction Ordering in `async.scope`

### 1. Scope & Problem Statement
- **Problem**:
  - `async.scope` lowers to a C++ lambda. Early `return` statements inside `async.scope` only exit the lambda, allowing outer function execution to proceed prematurely.
  - Additionally, `defer` statements inside `async.scope` run before `scope.join_all()`, risking use-after-free data races with background worker threads.
- **In Scope**:
  - In `runtime/cfire_prelude.hpp`:
    - Add RAII destructor `~Scope() { join_all(); }` ensuring all background threads join on scope exit.
    - Add deferred cleanup queue to `Scope` (`defer(fn)`) so local defers inside `async.scope` execute strictly after all worker threads have completed and joined.
  - In `src/codegen.hpp`:
    - Lower `async.scope` directly as an RAII block `{ Scope scope_var(workers); ... scope_var.join_all(); }` so early `return` unwinds through the scope and joins workers before returning from the enclosing function.
    - Route `defer` inside `async.scope` through `scope_var.defer(...)`.
- **Out of Scope**:
  - Thread cancellation tokens (deferred to future concurrency expansion).

### 2. Open Questions & User Clarifications
- None; standard structured concurrency guarantees.

### 3. Testing & Verification Matrix
- [x] **Unit Tests**: Verify early return inside `async.scope` exits the enclosing function after joining threads.
- [x] **Integration Tests**: Verify `defer` inside `async.scope` runs strictly after all spawned worker tasks complete.
- [x] **Conformance Tests**: `make test` passing with 0 warnings.
- [x] **Manual Validation**: `./cfire doc --check` clean.

### 4. Implementation Status
- **Status**: Completed & Verified. All tests passing (`make test` 100% pass, `./cfire doc --check` clean). Ready to merge.

---

## Active Task: `LD-04` Explicit Borrow References (`&str`, `&[]T`) to Prevent Auto-View Dangling

### 1. Scope & Problem Statement
- **Problem**:
  - `in` parameters typed `str` and `[]T` automatically lower to `std::string_view` and `std::span<const T>`. When callers pass temporaries (e.g. `f(str.to_upper("x"))`), the non-owning view immediately dangles if stored or returned.
- **In Scope**:
  - In `src/ast.hpp`:
    - Add `is_borrow` and `is_mut_borrow` flags to `TypeRef`.
  - In `src/parser.hpp`:
    - Parse `&T` and `&mut T` in `parse_type_ref()`.
  - In `src/codegen.hpp`:
    - Owned parameters (`str`, `[]T`) lower to `const std::string&` and `const std::vector<T>&`, guaranteeing safe lifetime extension.
    - Explicit borrowed views (`&str`, `&[]T`) lower to `std::string_view` and `std::span<const T>`.
- **Out of Scope**:
  - Full lifetime generics (`'a`).

### 2. Open Questions & User Clarifications
- None; standard reference semantics.

### 3. Testing & Verification Matrix
- [x] **Unit Tests**: Parse `&str` and `&[]i32`.
- [x] **Integration Tests**: Lowering produces `std::string_view` and `std::span<const int32_t>` for borrows, and `const std::string&` / `const std::vector<T>&` for owned.
- [x] **Conformance Tests**: `make test` passing with 0 warnings.
- [x] **Manual Validation**: `./cfire doc --check` clean.

### 4. Implementation Status
- **Status**: Completed & Verified. All tests passing (`make test` 100% pass, `./cfire doc --check` clean). Ready to merge.

---

## Active Task: `LD-05` CFG-Based Definite Assignment & Move Analysis

### 1. Scope & Problem Statement
- **Problem**:
  - Move checker currently uses a single flat `moved_at_line_` map. It is blind to loops (moving inside a loop causes runtime UB on iteration 2), prevents reassigning to moved variables (`x = new_val`), and misreports branches.
- **In Scope**:
  - In `src/sema.hpp`:
    - Re-assignment support: when a moved variable is reassigned (`AssignStmt`), restore it as live (`moved_at_line_.erase(id->name)`).
    - Loop move checking: detect variables moved inside `ForStmt` bodies and reject with `cfire/USE-AFTER-MOVE`.
    - Branch flow tracking: track moves across `IfStmt` branches; only mark moved after the `if` if moved along all divergent branches.
- **Out of Scope**:
  - Interprocedural pointer alias analysis.

### 2. Open Questions & User Clarifications
- None; standard forward dataflow liveness rules.

### 3. Testing & Verification Matrix
- [x] **Unit Tests**: Re-assignment to moved variable compiles cleanly.
- [x] **Integration Tests**: Moving inside loop without re-initialization produces `cfire/USE-AFTER-MOVE`.
- [x] **Conformance Tests**: `make test` passing with 0 warnings.
- [x] **Manual Validation**: `./cfire doc --check` clean.

### 4. Implementation Status
- **Status**: Completed & Verified. All tests passing (`make test` 100% pass, `./cfire doc --check` clean). Ready to merge.

---

## Active Task: `LIB-04` Standard Data Encodings (`core:encoding/base64`, `core:encoding/hex`, `core:encoding/csv`)

### 1. Scope & Problem Statement
- **Problem**:
  - Developers need standard Base64, Hex, and CSV data encodings for token handling, binary digests, serialization, and report parsing without having to pull external dependencies.
- **In Scope**:
  - In `runtime/cfire_core_stdlib.hpp`:
    - `cfire::base64::encode(std::span<const std::byte>) -> std::string`
    - `cfire::base64::encode_str(std::string_view) -> std::string`
    - `cfire::base64::decode(std::string_view) -> std::expected<std::vector<std::byte>, cfire::Error>`
    - `cfire::base64::decode_str(std::string_view) -> std::expected<std::string, cfire::Error>`
    - URL-safe variants (`encode_url_safe`, `decode_url_safe`).
    - `cfire::hex::encode(std::span<const std::byte>) -> std::string`
    - `cfire::hex::encode_str(std::string_view) -> std::string`
    - `cfire::hex::decode(std::string_view) -> std::expected<std::vector<std::byte>, cfire::Error>`
    - `cfire::hex::decode_str(std::string_view) -> std::expected<std::string, cfire::Error>`
    - `cfire::csv::parse(std::string_view) -> std::vector<std::vector<std::string>>` (RFC 4180).
    - `cfire::csv::stringify(const std::vector<std::vector<std::string>>&) -> std::string`.
  - In `src/docgen.hpp`:
    - Add `core:encoding/base64`, `core:encoding/hex`, and `core:encoding/csv` to stdlib catalog.
    - Synchronize documentation via `./cfire doc`.
- **Out of Scope**:
  - Multithreaded streaming gigabyte CSV parsing.

### 2. Open Questions & User Clarifications
- Standard RFC 4648 and RFC 4180 compliance; zero external dependencies.

### 3. Testing & Verification Matrix
- [x] **Unit Tests**: RFC 4648 Base64 & Hex test vectors; RFC 4180 CSV escape tests.
- [x] **Integration Tests**: Compile and execute cfire programs using `base64`, `hex`, and `csv`.
- [x] **Conformance Tests**: `make test` passing with 0 warnings.
- [x] **Manual Validation**: `./cfire doc --check` clean.

### 4. Implementation Status
- **Status**: Completed & Verified. All tests passing (`make test` 100% pass, `./cfire doc --check` clean). Ready to merge.

---

## Active Task: `BS-04` Hermetic & Relocatable CMake Ejection (`cfire eject`)

### 1. Scope & Problem Statement
- **Problem**:
  - `cfire eject` outputs `CMakeLists.txt` with absolute paths referencing the machine where the eject command was run (`/Users/.../cfire/runtime`). Ejected projects fail to build on CI or other machines.
- **In Scope**:
  - In `src/buildsys.hpp` / `src/main.cpp`:
    - Copy `cfire_prelude.hpp` and `cfire_core_stdlib.hpp` into `<out_dir>/runtime/`.
    - Copy local packages and vendor code into `<out_dir>/vendor/` or `<out_dir>/pkg/`.
    - Generate relocatable `CMakeLists.txt` using strictly relative paths (`${CMAKE_CURRENT_SOURCE_DIR}/runtime`).
    - Propagate linker flags from `@cfire-link`.
- **Out of Scope**:
  - Generating Visual Studio IDE projects (CMake handles this).

### 2. Open Questions & User Clarifications
- Self-contained hermetic bundle.

### 3. Testing & Verification Matrix
- [x] **Unit Tests**: Verify relative path emission in CMake generator.
- [x] **Integration Tests**: Eject an example to a temporary path and build with `cmake -B build && cmake --build build`.
- [x] **Conformance Tests**: `make test` passing with 0 warnings.
- [x] **Manual Validation**: Verify no absolute host paths exist in ejected files.

### 4. Implementation Status
- **Status**: Completed & Verified. All tests passing (`make test` 100% pass, `./cfire doc --check` clean). Ready to merge.

---

## Active Task: `LD-02` Value-Category-Aware Variant Matching (Moving Out of Match Arms)

### 1. Scope & Problem Statement
- **Problem**:
  - In tagged enum `match` statements, arm visitors bind variant payload arguments by value or `const auto&`. Move-only payloads (`own[T]`) or expensive data cannot be moved out with `move x`.
- **In Scope**:
  - In `src/codegen.hpp`:
    - Detect when `MatchStmt::target` is an rvalue or moved identifier (e.g. `match move v { ... }`).
    - Emit visitor `[&](EnumName::_Variant&& _m)` with `std::move(_m._0)` so payload identifiers are bound as rvalue references ready for `move`.
- **Out of Scope**:
  - Nested recursive tuple patterns.

### 2. Open Questions & User Clarifications
- None; standard C++23 visitor move semantics.

### 3. Testing & Verification Matrix
- [x] **Unit Tests**: Parse and lower `match move v { Variant(p) => return move p }`.
- [x] **Integration Tests**: Verify moving `own[T]` out of a match arm compiles and executes without copy errors.
- [x] **Conformance Tests**: `make test` passing with 0 warnings.
- [x] **Manual Validation**: `./cfire doc --check` clean.

### 4. Implementation Status
- **Status**: Completed & Verified. All tests passing (`make test` 100% pass, `./cfire doc --check` clean). Ready to merge.

---

## Active Task: `LD-06` Path-Based Mutation Exclusivity Verification (`cfire/MUT-ALIAS`)

### 1. Scope & Problem Statement
- **Problem**:
  - `cfire/MUT-ALIAS` only detects exact duplicate bare `IdentExpr`s in a single call. If an argument passes `mut p.field` while another passes `p.field` or `p`, the compiler misses the alias hazard.
- **In Scope**:
  - In `src/sema.hpp`:
    - Implement path projection helper extracting `(root_var, projection_path)` for expressions (`IdentExpr`, `MemberAccessExpr`, `IndexExpr`).
    - Reject calls where an argument mutates a root/prefix path while another argument accesses an overlapping path with `cfire/MUT-ALIAS`.
    - Allow non-overlapping sibling fields (e.g. `mut p.x` and `p.y`).
- **Out of Scope**:
  - Dynamic runtime pointer alias analysis.

### 2. Open Questions & User Clarifications
- Sibling fields are permitted; overlapping/parent paths are rejected.

### 3. Testing & Verification Matrix
- [x] **Unit Tests**: `f(mut p.x, p.x)` and `f(mut p, p.x)` fail with `cfire/MUT-ALIAS`. `f(mut p.x, p.y)` passes.
- [x] **Integration Tests**: Canonical examples compile cleanly with path exclusivity.
- [x] **Conformance Tests**: `make test` passing with 0 warnings.
- [x] **Manual Validation**: `./cfire doc --check` clean.

### 4. Implementation Status
- **Status**: Completed & Verified. All tests passing (`make test` 100% pass, `./cfire doc --check` clean). Ready to merge.

---

## Active Task: `BS-05` Preprocessor Macro Injection for Header-Only C Libraries

### 1. Scope & Problem Statement
- **Problem**:
  - Single-file header-only C libraries (e.g. `stb_image.h`, `miniaudio.h`) require defining preprocessor macros (e.g. `#define STB_IMAGE_IMPLEMENTATION`) prior to including the header.
- **In Scope**:
  - In `src/codegen.hpp`:
    - Inspect attributes on `ImportDecl`.
    - If `@define("NAME")` or `@define("NAME", "VALUE")` is present, emit `#define NAME` or `#define NAME VALUE` immediately preceding the `#include` directive.
- **Out of Scope**:
  - Global compiler command-line `-D` flags (handled via build.cf `extra_cflags`).

### 2. Open Questions & User Clarifications
- Attribute syntax: `import cpp "header.h" @define("MACRO")` or `@define("KEY", "VALUE")`.

### 3. Testing & Verification Matrix
- [x] **Unit Tests**: Codegen emission of macro definitions before `#include`.
- [x] **Integration Tests**: Header-only compilation test in conformance suite.
- [x] **Conformance Tests**: `make test` passing with 0 warnings.
- [x] **Manual Validation**: `./cfire doc --check` clean.

### 4. Implementation Status
- **Status**: Completed & Verified. All tests passing (`make test` 100% pass, `./cfire doc --check` clean). Ready to merge.

---

## Active Task: `LIB-05` Regular Expressions Engine (`core:regex`)

### 1. Scope & Problem Statement
- **Problem**:
  - Developers need pattern matching, input validation, and substring replacement without external dependencies.
- **In Scope**:
  - In `runtime/cfire_core_stdlib.hpp`:
    - `cfire::regex::Regex` class wrapping `std::regex`.
    - `compile(pattern: str) -> Regex!`.
    - `is_match(text: str) -> bool`.
    - `find(text: str) -> str?`.
    - `find_all(text: str) -> []str`.
    - `replace_all(text: str, repl: str) -> str`.
    - Package-level helpers `regex::is_match(pattern, text) -> bool!` and `regex::replace(pattern, text, repl) -> str!`.
  - In `src/docgen.hpp`:
    - Register `core:regex` in stdlib catalog.
- **Out of Scope**:
  - JIT-compiled regex engines (PCRE2).

### 2. Open Questions & User Clarifications
- Backed by C++23 `<regex>`, zero external dependencies.

### 3. Testing & Verification Matrix
- [x] **Unit Tests**: Pattern matching, finding capture groups, and substring replacement.
- [x] **Integration Tests**: Invalid regex error handling with `Error`.
- [x] **Conformance Tests**: `make test` passing with 0 warnings.
- [x] **Manual Validation**: `./cfire doc --check` clean.

### 4. Implementation Status
- **Status**: Completed & Verified. All tests passing (`make test` 100% pass, `./cfire doc --check` clean). Ready to merge.

---

## Active Task: `LIB-07` Data Structures & Algorithms (`core:collections`, `core:sort`)

### 1. Scope & Problem Statement
- **Problem**:
  - Common data structures (`Set[T]`, `PriorityQueue[T]`, `Deque[T]`) and in-place sorting algorithms (`sort.slice`) are missing from the standard library.
- **In Scope**:
  - In `runtime/cfire_core_stdlib.hpp`:
    - `cfire::collections::Set[T]`: `insert`, `contains`, `remove`, `size`, `empty`, `to_slice`.
    - `cfire::collections::PriorityQueue[T]`: `push`, `pop`, `peek`, `size`, `empty`.
    - `cfire::collections::Deque[T]`: `push_back`, `push_front`, `pop_back`, `pop_front`, `size`, `empty`, `at`.
    - `cfire::sort::slice(s)`, `cfire::sort::slice_by(s, comp)`, `cfire::sort::binary_search(s, val) -> i64?`.
  - In `src/docgen.hpp`:
    - Register `core:collections` and `core:sort` in stdlib catalog.
- **Out of Scope**:
  - Intrusive or lock-free concurrent queues.

### 2. Open Questions & User Clarifications
- Backed by C++ STL containers (`std::unordered_set`, `std::priority_queue`, `std::deque`, `std::ranges::sort`), zero external dependencies.

### 3. Testing & Verification Matrix
- [x] **Unit Tests**: Set operations, priority ordering, deque FIFO/LIFO, slice sorting and binary search.
- [x] **Integration Tests**: Conformance tests in `tests/conformance_test.cpp`.
- [x] **Conformance Tests**: `make test` passing with 0 warnings.
- [x] **Manual Validation**: `./cfire doc --check` clean.

### 4. Implementation Status
- **Status**: Completed & Verified. All tests passing (`make test` 100% pass, `./cfire doc --check` clean). Ready to merge.

---

## Active Task: `LD-09` Unambiguous Generic Call Disambiguation (`f[T, U](...)`)

### 1. Scope & Problem Statement
- **Problem**:
  - The previous generic call parser only matched single identifier arguments `[Ident](` using a 4-token lookahead. Generic functions with multiple type arguments (`f[i32, str](...)`) or compound types (`f[[]i32](...)`) could not be called explicitly.
- **In Scope**:
  - In `src/parser.hpp`:
    - Implement lookahead across balanced `[...]` brackets. If immediately followed by `(`, attempt to parse the bracket contents as a comma-separated list of `TypeRef`s.
    - If valid types are found and followed by `(`, construct a `CallExpr` with `type_args`.
    - Avoid turbofish `::` operators to maintain clean syntax `f[T, U](args...)`.
  - In `src/codegen.hpp`:
    - Emit `f<T1, T2>(args...)` lowering.
- **Out of Scope**:
  - Higher-kinded types.

### 2. Open Questions & User Clarifications
- Decision: Lookahead-based clean bracket syntax `f[T, U](...)` without operator clutter.

### 3. Testing & Verification Matrix
- [x] **Unit Tests**: Parse multi-argument generics `f[i32, str](1, "a")` and compound generics `g[[]i32](arr)`.
- [x] **Integration Tests**: Lowering to C++23 `f<std::int32_t, std::string>(1, "a")`.
- [x] **Conformance Tests**: `make test` passing with 0 warnings.
- [x] **Manual Validation**: `./cfire doc --check` clean.

### 4. Implementation Status
- **Status**: Completed & Verified. All tests passing (`make test` 100% pass, `./cfire doc --check` clean). Ready to merge.

---

## Active Task: `LIB-06` Time, Date & Duration Modernization (`core:time`)

### 1. Scope & Problem Statement
- **Problem**:
  - `core:time` currently only exposes raw millisecond scalars (`now_ms()`, `unix_seconds()`, `sleep_ms(ms)`). Developers lack a typed `Duration` type with unit arithmetic (nanos, micros, millis, seconds, minutes, hours), a calendar-aware `Time` instant (year, month, day, hour, minute, second), and RFC 3339 / ISO 8601 formatting and parsing.
- **In Scope**:
  - In `runtime/cfire_core_stdlib.hpp`:
    - Add `cfire::time::Duration` struct:
      - Internal nanoseconds `std::int64_t nanos`.
      - Factories: `nanoseconds(i64)`, `microseconds(i64)`, `milliseconds(i64)`, `seconds(i64)`, `minutes(i64)`, `hours(i64)`.
      - Converters: `as_nanos()`, `as_micros()`, `as_millis()`, `as_seconds()`.
      - Operators: `+`, `-`, `*`, `==`, `<`, etc.
    - Add `cfire::time::Time` struct:
      - Internal `std::chrono::system_clock::time_point tp`.
      - Factories: `time::now()`, `time::unix(sec, nsec)`.
      - Calendar fields: `year()`, `month()`, `day()`, `hour()`, `minute()`, `second()`.
      - Converters: `unix_seconds()`, `unix_millis()`, `unix_nanos()`.
      - Methods: `add(Duration)`, `sub(Time) -> Duration`.
      - RFC 3339 formatting: `format_rfc3339() -> std::string` (e.g. `2026-10-18T17:05:29Z`).
    - RFC 3339 parsing: `cfire::time::parse_rfc3339(std::string_view) -> std::expected<Time, cfire::Error>`.
    - Typed sleep: `cfire::time::sleep(Duration)`.
    - Maintain backward compatibility with `now_ms()`, `unix_seconds()`, `sleep_ms(ms)`.
  - In `src/docgen.hpp`:
    - Update `core:time` stdlib catalog with new functions and types.
    - Regenerate docs.
- **Out of Scope**:
  - IANA timezone database parsing (standard UTC/system time is standard for core).

### 2. Open Questions & User Clarifications
- None required; clean C++20 `<chrono>` header-only implementation with zero external dependencies.

### 3. Testing & Verification Matrix
- [x] **Unit Tests**: Test `Duration` arithmetic, conversion to millis/seconds, `Time` RFC 3339 formatting and roundtrip parsing.
- [x] **Integration Tests**: Compile and run cfire code creating `Time` and `Duration`.
- [x] **Conformance Tests**: Add assertions to `tests/conformance_test.cpp`.
- [x] **Manual Validation**: `./cfire doc --check` clean.

### 4. Implementation Status
- **Status**: Completed & Verified. All tests passing (`make test` 100% pass, `./cfire doc --check` clean). Ready to merge.

---

## Active Task: `LIB-08` Structured Leveled Logging (`core:log` / `slog`)

### 1. Scope & Problem Statement
- **Problem**:
  - `cfire` currently provides no standard logging framework. Developers rely on raw `println` or custom wrappers, lacking leveled log filtering (`DEBUG`, `INFO`, `WARN`, `ERROR`), timestamps, structured key-value attributes, and machine-readable JSON logging for cloud/container observability.
- **In Scope**:
  - In `runtime/cfire_core_stdlib.hpp`:
    - Define log level enum: `Level { Debug = 0, Info = 1, Warn = 2, Error = 3 }`.
    - Define format enum: `Format { Text = 0, Json = 1 }`.
    - Global logger state / configuration: `log::set_level(Level)`, `log::set_format(Format)`.
    - Logging functions: `log::debug(msg, ...)`, `log::info(msg, ...)`, `log::warn(msg, ...)`, `log::error(msg, ...)`.
    - Key-value contextual attributes: support variadic key/value arguments (e.g. `log::info("connected", "peer", "127.0.0.1", "port", "8080")`).
    - Formatters:
      - Text: ANSI colors per level + timestamp + msg + formatted key-values.
      - JSON: Valid RFC 8259 JSON lines `{"time":"...","level":"INFO","msg":"...","peer":"127.0.0.1"}`.
  - In `src/docgen.hpp`:
    - Register `core:log` in stdlib catalog.
    - Regenerate docs.
- **Out of Scope**:
  - Distributed OpenTelemetry gRPC exporters.

### 2. Open Questions & User Clarifications
- Text and JSON formatters standard for serverless and cloud-native environments; writes to `std::cerr`.

### 3. Testing & Verification Matrix
- [x] **Unit Tests**: Verify log level threshold filtering, text formatting with key-values, and JSON output formatting.
- [x] **Integration Tests**: Capture log output across levels and formats.
- [x] **Conformance Tests**: Assertions added to `tests/conformance_test.cpp`.
- [x] **Manual Validation**: `./cfire doc --check` clean.

### 4. Implementation Status
- **Status**: Completed & Verified. All tests passing (`make test` 100% pass, `./cfire doc --check` clean). Ready to merge.

---

## Active Task: `BS-06` Shared Library (`.so` / `.dylib`) & Dynamic Link Support

### 1. Scope & Problem Statement
- **Problem**:
  - In `src/buildsys.hpp`, `build_local_dep` only scans for and links static archives (`.a`). If a CMake, Make, Ninja, or CC dependency produces a shared library (`.dylib` on macOS or `.so` on Linux), the build system ignores it or fails with unresolved external symbols. Furthermore, even if linked, executables fail at runtime if the library path is not embedded via `-Wl,-rpath`.
- **In Scope**:
  - In `src/buildsys.hpp`:
    - During library discovery in CMake, Make, Ninja, and CC build steps, scan for `.dylib` (macOS), `.so` (Linux), and `.so.*` shared libraries in addition to `.a`.
    - If a shared library is detected, save to `dep.built_archive` and add to `plan.ldflags`.
    - Automatically append runtime path flags: `-Wl,-rpath,"<parent_directory>"` so the dynamic linker finds the shared library without needing manual `DYLD_LIBRARY_PATH` or `LD_LIBRARY_PATH`.
- **Out of Scope**:
  - Windows `.dll` import library generation.

### 2. Open Questions & User Clarifications
- Automatic rpath embedding guarantees dynamic linking works out of the box.

### 3. Testing & Verification Matrix
- [x] **Unit Tests**: Test shared library detection and `-Wl,-rpath` emission in build plan.
- [x] **Integration Tests**: Verify building and executing an example using a shared library.
- [x] **Conformance Tests**: Assertions added to `tests/conformance_test.cpp`.
- [x] **Manual Validation**: `make test` passing with 0 warnings.

### 4. Implementation Status
- **Status**: Completed & Verified. All tests passing (`make test` 100% pass, `./cfire doc --check` clean). Ready to merge.

---

## Active Task: `LD-13` Multi-Return Tuple Syntax & Deconstructing Assignment

### 1. Scope & Problem Statement
- **Problem**:
  - `cfire` previously used mutable parameters to return multiple values, which lack definite assignment analysis, force callers to declare uninitialized dummy variables before calling functions, and destroy expression composability. Modern languages (Go, Swift, Python, Rust) utilize multi-return tuples with deconstructing assignments (`(x, y) := parse()`).
- **In Scope**:
  - In `src/ast.hpp`:
    - Add `is_tuple` and `tuple_types` to `TypeRef`.
    - Add `TupleExpr` AST node.
    - Add `DestructureVarDeclStmt` and `DestructureAssignStmt` AST nodes.
  - In `src/parser.hpp`:
    - Parse tuple types `(T1, T2)` in `parse_type_ref()`, supporting fallible `(T1, T2)!` and optional `(T1, T2)?`.
    - Parse tuple expressions `(e1, e2, ...)` in `parse_primary_expr()`.
    - Parse deconstructing declarations `(x, y) := expr` and `(mut x, mut y) := expr` in `parse_stmt()`.
    - Parse deconstructing assignments `(x, y) = expr` in `parse_stmt()`.
  - In `src/codegen.hpp`:
    - Lower tuple types to `std::tuple<T1, T2, ...>`.
    - Lower tuple expressions to `std::make_tuple(e1, e2, ...)`.
    - Lower deconstructing declarations to C++23 structured bindings: `auto [x, y] = init;`.
    - Lower deconstructing assignments to `std::tie(x, y) = expr;`.
  - In `src/sema.hpp`:
    - Enforce value semantics for multi-return tuples.
- **Out of Scope**:
  - Arbitrary nested recursive pattern matching in variable declarations.

### 2. Open Questions & User Clarifications
- Multi-return tuples lower directly to standard C++23 `std::tuple` and structured bindings.

### 3. Testing & Verification Matrix
- [x] **Unit Tests**: Parse tuple types, tuple literals, and deconstructing assignments.
- [x] **Integration Tests**: Lowering and end-to-end execution of multi-return functions.
- [x] **Conformance Tests**: Assertions added to `tests/conformance_test.cpp`.
- [x] **Manual Validation**: `./cfire doc --check` clean.

### 4. Implementation Status
- **Status**: Completed & Verified. All tests passing (`make test` 100% pass, `./cfire doc --check` clean). Ready to merge.

---

## Active Task: `BS-07` Recursive Local Package Resolution & Subpackage `build.cf`

### 1. Scope & Problem Statement
- **Problem**:
  - `resolve_local_cf_imports` in `src/buildsys.hpp` currently only resolves imports directly referenced by the root file. If a local package imports another local package (transitive imports `A -> B -> C`), or if a subpackage has its own sibling `build.cf` manifest declaring C dependencies, those transitive packages and sub-dependencies are dropped or lead to missing symbol compilation errors.
- **In Scope**:
  - In `src/buildsys.hpp`:
    - Recursive traversal of local packages: maintain a visited set `std::unordered_set<std::string> visited_paths` to prevent infinite recursion and cycles.
    - Subpackage `build.cf` aggregation: when resolving a local `.cf` package, check if its directory contains a `build.cf`. If so, parse it and aggregate its `ExternalDepSpec` entries into `plan.external_deps`.
    - Avoid duplicate dependency compilation when multiple subpackages share common C dependencies.
- **Out of Scope**:
  - Remote git repository cloning (only local filesystem packages and dependencies).

### 2. Open Questions & User Clarifications
- Subpackage dependency names are namespaced or deduplicated by target name.

### 3. Testing & Verification Matrix
- [x] **Unit Tests**: Test transitive resolution of packages (`pkg_a` importing `pkg_b` importing `pkg_c`).
- [x] **Integration Tests**: Test subpackage with its own `build.cf` declaring a C dependency.
- [x] **Conformance Tests**: Assertions added to `tests/conformance_test.cpp`.
- [x] **Manual Validation**: `make test` passing with 0 warnings.

### 4. Implementation Status
- **Status**: Completed & Verified. All tests passing (`make test` 100% pass, `./cfire doc --check` clean). Ready to merge.

---

## Active Task: `BS-08` Pre-Build Code Generation Hooks (`build.Command`)

### 1. Scope & Problem Statement
- **Problem**:
  - Projects that generate code prior to compilation (e.g. `flatc`, `protoc`, lexer/parser generators, asset packers) currently cannot declare pre-compilation custom steps in `build.cf`.
- **In Scope**:
  - In `src/buildsys.hpp`:
    - Extend `ExternalDepSpec` or add `PreBuildCommandSpec`:
      - `command`: binary executable name or path.
      - `args`: list of command-line arguments.
      - `inputs`: list of input source files.
      - `outputs`: list of generated target files.
      - `work_dir`: working directory.
    - Parse `build.Command("bin", { Args: [...], Inputs: [...], Outputs: [...] })` in `parse_build_cf`.
    - Execute pre-build commands during `build_local_dep` or `analyze_and_emit` if any output is missing or older than any input (incremental freshness check).
- **Out of Scope**:
  - Full DAG task scheduler beyond pre-build step ordering.

### 2. Open Questions & User Clarifications
- Pre-build commands run incrementally (skipped if outputs exist and are newer than inputs).

### 3. Testing & Verification Matrix
- [x] **Unit Tests**: Test parsing of `build.Command` in `parse_build_cf`.
- [x] **Integration Tests**: Execute a custom pre-build code generation step and verify generated files are compiled.
- [x] **Conformance Tests**: Assertions added to `tests/conformance_test.cpp`.
- [x] **Manual Validation**: `make test` passing with 0 warnings.

### 4. Implementation Status
- **Status**: Completed & Verified. All tests passing (`make test` 100% pass, `./cfire doc --check` clean). Ready to merge.

---

## Active Task: `LIB-09` Streaming Filesystem I/O (`core:fs` Modernization)

### 1. Scope & Problem Statement
- **Problem**:
  - `core:fs` currently only exposes whole-file slurp functions (`fs.read_file() -> str`, `fs.read_bytes() -> []byte`, `fs.write_file()`). Large files cannot be streamed in chunks, seek operations are impossible, and path manipulation helpers (`dirname`, `basename`, `extension`) are missing.
- **In Scope**:
  - In `runtime/cfire_core_stdlib.hpp`:
    - Add `OpenMode` enum: `Read = 0`, `Write = 1`, `Append = 2`, `ReadWrite = 3`.
    - Add `SeekWhence` enum: `Start = 0`, `Current = 1`, `End = 2`.
    - Add streaming `File` struct:
      - Internal `std::FILE* fp` / handle with RAII move-only cleanup.
      - `read(mut []byte) -> usize!`: reads up to buffer size, returns bytes read (0 on EOF).
      - `read_all() -> []byte!`: reads remaining bytes to end of file.
      - `write([]byte) -> usize!`: writes bytes from slice.
      - `write_str(str) -> usize!`: writes string.
      - `seek(offset: i64, whence: SeekWhence) -> i64!`.
      - `tell() -> i64!`.
      - `flush() -> void!`.
      - `close()`.
    - Add `fs.open(path: str, mode: OpenMode = OpenMode::Read) -> File!`.
    - Add path manipulation helpers:
      - `fs.dirname(path: str) -> str`.
      - `fs.basename(path: str) -> str`.
      - `fs.extension(path: str) -> str`.
  - In `src/docgen.hpp`:
    - Register streaming file methods and path functions.
    - Synchronize documentation.
- **Out of Scope**:
  - Asynchronous non-blocking file I/O (handled via thread pool / `async.scope`).

### 2. Open Questions & User Clarifications
- Standard C/C++ `std::FILE*` runtime handle provides maximum zero-dependency portability and performance.

### 3. Testing & Verification Matrix
- [x] **Unit Tests**: Test chunked streaming reads, writes, seeking, and path decomposition (`dirname`, `basename`, `extension`).
- [x] **Integration Tests**: Roundtrip file streaming with buffer slices.
- [x] **Conformance Tests**: Assertions added to `tests/conformance_test.cpp`.
- [x] **Manual Validation**: `./cfire doc --check` clean.

### 4. Implementation Status
- **Status**: Completed & Verified. All tests passing (`make test` 100% pass, `./cfire doc --check` clean). Ready to merge.

---

## Active Task: `LIB-10` JSON Dynamic DOM & Structured Serializer Fixes (`core:json`)

### 1. Scope & Problem Statement
- **Problem**:
  - `core:json` currently requires statically known types (`json.decode[T]`). If an incoming JSON payload has a dynamic, polymorphic, or loosely typed structure, developers cannot inspect it. Additionally, `stringify_struct` codegen doesn't escape special characters (quotes, backslashes, control characters) inside string fields.
- **In Scope**:
  - In `runtime/cfire_core_stdlib.hpp`:
    - Add `json::Value` dynamic DOM type (recursive variant representing Null, Bool, Int64, Float64, String, Array, Object):
      - Query methods: `is_null()`, `is_bool()`, `is_int()`, `is_float()`, `is_string()`, `is_array()`, `is_object()`.
      - Value accessors: `as_bool()`, `as_int()`, `as_float()`, `as_str()`.
      - Navigation: `get(key: str) -> Value?`, `at(index: usize) -> Value?`, `operator[](key)` / `operator[](index)`.
      - Size and array iteration: `size()`, `keys()`.
    - Add `json.parse_dynamic(s: str) -> json::Value!`.
    - Add `json.stringify_dynamic(v: json::Value, indent: int = 0) -> str`.
    - String serializer escaping: ensure `json.escape_string(s)` escapes `"`, `\`, `\n`, `\r`, `\t`, etc.
  - In `src/codegen.hpp`:
    - In `emit_struct_json_serializer`, wrap string fields in `cfire::json::escape_string`.
  - In `src/docgen.hpp`:
    - Register `json.parse_dynamic`, `json.Value`, and `json.stringify_dynamic`.
- **Out of Scope**:
  - JSON Schema validation.

### 2. Open Questions & User Clarifications
- Clean recursive standard C++23 variant with zero external dependencies.

### 3. Testing & Verification Matrix
- [x] **Unit Tests**: Test dynamic JSON object and array navigation, type queries, and string escaping.
- [x] **Integration Tests**: Parse mixed dynamic JSON and serialize back.
- [x] **Conformance Tests**: Assertions added to `tests/conformance_test.cpp`.
- [x] **Manual Validation**: `./cfire doc --check` clean.

### 4. Implementation Status
- **Status**: Completed & Verified. All tests passing (`make test` 100% pass, `./cfire doc --check` clean). Ready to merge.

---

## Active Task: `LD-07` Unify Variable Declarations

### 1. Scope & Problem Statement
- **Problem**:
  - `cfire` previously had multiple competing declaration styles. Unifying variable declarations around canonical `:=` and explicit `: T =` reduces syntactic redundancy and eliminates cognitive clutter.
- **In Scope**:
  - In `src/parser.hpp`:
    - Standardize canonical syntax on:
      - Inferred: `x := expr` (const) and `mut x := expr` (mutable).
      - Explicitly typed: `x: T = expr` (const) and `mut x: T = expr` (mutable).
    - Ensure both inferred and typed forms are permitted uniformly at both local statement scope and package scope.
  - In `src/sema.hpp`:
    - Enforce definite assignment and mutability rules across all declarations.
- **Out of Scope**:
  - Complex pattern destructuring outside of tuple assignment.

### 2. Open Questions & User Clarifications
- Resolved: all declarations follow uniform `:=` and `: T =` rules across all scopes.

### 3. Testing & Verification Matrix
- [x] **Unit Tests**: Verify package-level `x: T = val` and `mut x := val`.
- [x] **Integration Tests**: Verify canonical declaration parsing and scoping.
- [x] **Conformance Tests**: Assertions added to `tests/conformance_test.cpp`.
- [x] **Manual Validation**: `./cfire doc --check` clean.

### 4. Implementation Status
- **Status**: Completed & Verified. All tests passing (`make test` 100% pass, `./cfire doc --check` clean). Ready to merge.

---

## Active Task: `LIB-11` Multi-Byte UTF-8 & Unicode Support (`core:unicode`)

### 1. Scope & Problem Statement
- **Problem**:
  - `str` in `cfire` is currently byte-indexed. String length `len(s)` returns byte count, and indexing returns raw bytes. Multi-byte UTF-8 sequences (international characters, Cyrillic, Greek, accented characters, emojis) are corrupted if sliced or measured by raw length.
- **In Scope**:
  - In `runtime/cfire_core_stdlib.hpp`:
    - Add namespace `cfire::unicode`:
      - `rune_count(s: str) -> usize`: branchless UTF-8 codepoint counter.
      - `runes(s: str) -> std::vector<uint32_t>`: extracts full 32-bit Unicode scalar values / codepoints.
      - `is_valid_utf8(s: str) -> bool`: validates UTF-8 byte stream conforming to RFC 3629.
      - `to_lower(s: str) -> str`: ASCII/Unicode case lowering.
      - `to_upper(s: str) -> str`: ASCII/Unicode case upper.
      - `encode_rune(r: uint32_t) -> str`: encodes 32-bit codepoint back into UTF-8 string.
  - In `src/docgen.hpp`:
    - Register `core:unicode` module with all 6 functions.
    - Synchronize documentation via `./cfire doc`.
- **Out of Scope**:
  - Full grapheme cluster segmentation (UAX #29) or full Unicode normalization tables (requires multi-megabyte data tables; lean DFA-based scalar decoding is preferred).

### 2. Open Questions & User Clarifications
- Pure C++23 zero-dependency implementation using branchless bitwise scalar decoding.

### 3. Testing & Verification Matrix
- [x] **Unit Tests**: Test rune counting for multi-byte strings ("Hello, 世界! 🚀"), invalid byte rejection, and rune re-encoding.
- [x] **Integration Tests**: Import `"core:unicode"` in `.cf` and test functions.
- [x] **Conformance Tests**: Assertions added to `tests/conformance_test.cpp`.
- [x] **Manual Validation**: `./cfire doc --check` clean.

### 4. Implementation Status
- **Status**: Completed & Verified. All tests passing (`make test` 100% pass, `./cfire doc --check` clean). Ready to merge.

---

## Active Task: `LD-10` Decouple Struct Tags & Generalize Tag Metadata

### 1. Scope & Problem Statement
- **Problem**:
  - Struct field tags are currently hardcoded in compiler passes for `json:"..."` and `sql:"..."`. Any other tags (e.g. `yaml:"..."`, `cli:"..."`, `env:"..."`) are dropped or cause parsing errors, and user-space libraries cannot inspect struct field tags.
- **In Scope**:
  - In `src/ast.hpp`:
    - Add `std::unordered_map<std::string, std::string> tags` and raw `std::string raw_tag` to `FieldDecl`.
  - In `src/parser.hpp`:
    - Generalize backtick tag parsing to parse multiple space-separated `key:"value"` pairs (Go-style struct tags) into `FieldDecl.tags`.
  - In `src/codegen.hpp`:
    - Codegen struct metadata reflection descriptor: emit a constexpr/inline tuple or struct helper `cfire_reflect_struct<T>()` containing field names, field types, and field tag key-values.
    - Update `emit_struct_json_serializer` to retrieve `tags["json"]` dynamically from `field.tags`.
- **Out of Scope**:
  - Runtime dynamic type reflection on arbitrary void pointers.

### 2. Open Questions & User Clarifications
- Field tags use standard Go-style `key:"value" key2:"val2"` syntax inside backticks.

### 3. Testing & Verification Matrix
- [x] **Unit Tests**: Test parsing multiple custom struct tags (`tag1:"abc" env:"PORT" min:"1"`).
- [x] **Integration Tests**: Verify reflection metadata descriptor emitted in generated C++.
- [x] **Conformance Tests**: Assertions added to `tests/conformance_test.cpp`.
- [x] **Manual Validation**: `./cfire doc --check` clean.

### 4. Implementation Status
- **Status**: Completed & Verified. All tests passing (`make test` 100% pass, `./cfire doc --check` clean). Ready to merge.

---

## Active Task: `TOOL-01` Agent Tooling Suite (`--json`, `outline`, `--llm`)

### 1. Scope & Problem Statement
- **Problem**:
  - AI agents and IDEs currently have to parse human-oriented stderr for diagnostics, burn thousands of tokens reading full source files just to discover types/signatures, and hallucinate syntax because `cfire` is not in LLM pre-training data.
- **In Scope**:
  - In `src/main.cpp`:
    - `cfire check --json <file>`: Runs parse & sema analysis and outputs machine-readable JSON:
      `{ "success": bool, "file": str, "diagnostics": [ { "file": str, "line": int, "col": int, "severity": "error"|"warning", "message": str, "code": str } ] }`.
    - `cfire outline --json <file|dir>`: Parses files without Clang invocation and outputs a concise token-compressed JSON outline:
      `{ "files": { "file.cf": { "package": str, "imports": [...], "types": [{ "name": str, "kind": str, "fields": [...] }], "functions": [{ "name": str, "params": [...], "returns": str }] } } }`.
    - `cfire doc --llm`: Generates a compact ~350-token grammar and stdlib cheat-sheet for embedding into `.cursorrules` or agent instructions.
- **Out of Scope**:
  - Interactive REPL.

### 2. Open Questions & User Clarifications
- JSON output must be compact, strict, and fast (<30ms).

### 3. Testing & Verification Matrix
- [x] **Unit Tests**: Test JSON emission for `check --json`, `outline --json`, and `doc --llm`.
- [x] **Integration Tests**: Run commands against canonical examples and verify JSON parses.
- [x] **Conformance Tests**: Assertions added to `tests/conformance_test.cpp`.
- [x] **Manual Validation**: Execute `cfire check --json`, `cfire outline --json`, `cfire doc --llm` from CLI.

### 4. Implementation Status
- **Status**: Completed & Verified. All tests passing (`make test` 100% pass, `./cfire doc --check` clean). Ready to merge.

---

## Active Task: `IDE-01` Universal Multi-IDE Auto-Detection & Setup (`cfire ide`)

### 1. Scope & Problem Statement
- **Problem**:
  - `cfire ide` currently only supports `cfire ide zed`. Developers and AI agents using VS Code, Cursor, Windsurf, CLion, or Neovim have to manually configure language servers, formatting hooks, and file associations.
- **In Scope**:
  - In `src/main.cpp`:
    - Extend `cfire ide [target]`:
      - Auto-detection heuristic when no target is specified:
        1. Check environment variables (`TERM_PROGRAM`, `VSCODE_PID`, `ZED_TERM`, `NVIM`).
        2. Check workspace directories (`.vscode`, `.zed`, `.idea`, `.nvim.lua`).
        3. If multiple or indeterminate, configure all common editors (`--all` or default).
      - `cfire ide vscode` (or `cursor` / `windsurf`): Creates `.vscode/settings.json` and `.vscode/extensions.json` with file associations (`*.cf`), default formatter (`cfire`), LSP command (`cfire lsp`), and format-on-save.
      - `cfire ide zed`: Creates `.zed/settings.json` (preserves existing functionality).
      - `cfire ide nvim`: Creates `.nvim.lua` registering `cfire-lsp` and auto-format on save.
      - `cfire ide clion`: Creates `.idea/lsp.xml` registering `cfire` LSP for `.cf`.
      - `cfire ide --all`: Configures all editors simultaneously.
- **Out of Scope**:
  - Downloading editor extensions from external marketplaces.

### 2. Open Questions & User Clarifications
- Standard zero-dependency JSON/Lua configuration generation.

### 3. Testing & Verification Matrix
- [x] **Unit Tests**: Test configuration generation for each editor target.
- [x] **Integration Tests**: Verify generated files in a scratch directory.
- [x] **Conformance Tests**: Assertions added to `tests/conformance_test.cpp`.
- [x] **Manual Validation**: Run `cfire ide --all` in a temporary test directory.

### 4. Implementation Status
- **Status**: Completed & Verified. All tests passing (`make test` 100% pass, `./cfire doc --check` clean). Ready to merge.

---

## Active Task: `TOOL-02` One-Shot REPL & CLI Evaluator (`cfire eval`)

### 1. Scope & Problem Statement
- **Problem**:
  - Developers and AI agents frequently want to evaluate snippets, test expressions, or run quick ad-hoc scripts without creating scratch projects or writing boilerplate `package main` and `fn main()`.
- **In Scope**:
  - In `src/main.cpp`:
    - `cfire eval [options] [code]`:
      - Positional string argument: `cfire eval 'println("hello")'`.
      - `-e <expr>` flag: evaluates `<expr>` and automatically wraps with `println("{}", <expr>)`.
      - Stdin support: if no code argument is passed or argument is `-`, reads snippet from `std::cin` until EOF.
      - Auto-wrapping: If code does not begin with `package `, wraps it in a synthetic translation unit:
        ```cfire
        package main
        import "core:fs"
        import "core:str"
        import "core:math"
        import "core:time"
        import "core:json"

        fn main() {
            <code_or_expression>
        }
        ```
      - If code already contains `package `, compiles it as a complete standalone file.
      - Ephemeral execution: creates a unique scratch directory in `/tmp/cfire_eval_<pid>_<ts>`, compiles using `cfire::build::build_native`, runs the executable, streams output, and cleans up the temporary directory via RAII.
- **Out of Scope**:
  - Multi-line interactive readline prompt with history (deferred to a dedicated interactive REPL daemon).

### 2. Open Questions & User Clarifications
- **Q1 (Expression vs Statement auto-wrapping)**: Should `cfire eval '1 + 1'` print `2`?
  - *Decision*: In `-e` mode (`cfire eval -e '1 + 1'`), expression is wrapped in `println("{}", ...)`. In standard eval mode (`cfire eval '...'`), code is treated as statements in `fn main() { ... }`.
- **Q2 (Stdlib auto-imports)**: Should standard utility modules be pre-imported?
  - *Decision*: Yes, pre-importing `core:fs`, `core:str`, `core:math`, `core:time`, and `core:json` gives immediate zero-boilerplate productivity.

### 3. Testing & Verification Matrix
- [x] **Unit Tests**: Verify synthetic translation unit wrapping for statements, `-e` expressions, and raw full packages.
- [x] **Integration Tests**: Verify ephemeral compilation, execution, and cleanup in `/tmp`.
- [x] **Conformance Tests**: Test suite `test_cfire_eval` added to `tests/conformance_test.cpp` testing:
  - `cfire eval 'println("eval-ok")'`
  - `cfire eval -e '100 + 42 * 2'`
  - Stdin piping into `cfire eval`
  - Pre-imported stdlib (`core:str`, `core:unicode`)
  - Multi-line struct and function declarations
  - Complete standalone package evaluation
- [x] **Manual Validation**: Live execution of `cfire eval` commands from CLI verified.

### 4. Implementation Status
- **Status**: Completed & Verified. All tests passing (`make test` 100% pass, `./cfire doc --check` clean). Ready to merge.

---

## Active Task: `TOOL-03` Semantic CLI Renaming & Refactoring Engine (`cfire refactor rename`)

### 1. Scope & Problem Statement
- **Problem**:
  - Headless AI agents, automated migration scripts, and terminal users need scriptable, multi-file semantic renaming. Currently, semantic renaming is only accessible via JSON-RPC over stdio inside `cfire lsp`. Agents either have to implement complex LSP clients or perform brittle text/regex replacements that accidentally rename fields across unrelated structs.
- **In Scope**:
  - Create `src/refactor.hpp`:
    - AST-aware symbol reference lookup and scope boundary resolution.
    - Identifier syntax validation (rejects invalid tokens, digits as prefix, keywords).
    - Scope collision detector: verifies that `new_name` does not collide with existing identifiers in target lexical scopes or exported package symbols.
    - Unified diff generator: emits standard patch format for planned modifications.
    - Atomic writer: uses temporary files and atomic rename to safely update target files.
  - In `src/main.cpp`:
    - Subcommand `cfire refactor rename`:
      - Supports `<file>:<line>:<col> <new_name>` or `--file <f> --line <l> --col <c> --to <name>`.
      - `--dry-run`: reports planned replacements without writing to disk.
      - `--diff`: prints unified diff preview.
      - `--apply`: performs the atomic in-place edits.
      - `--json`: emits machine-readable JSON for agents.
  - Wire LSP `textDocument/rename` and `textDocument/references` to use the shared occurrence types where appropriate.
- **Out of Scope**:
  - Renaming C/C++ native symbols declared in external C headers (C++ headers are read-only).

### 2. Open Questions & User Clarifications
- **Q1 (Default behavior when no action flag given)**: If neither `--apply` nor `--diff` nor `--dry-run` is passed?
  - *Decision*: Default to `--diff` + summary output to prevent accidental destructive modifications.
- **Q2 (Collision safety)**: What happens if `new_name` already exists in scope?
  - *Decision*: Abort with exit code 1 and error diagnostic `cfire/REFACTOR-COLLISION: symbol '<new_name>' already exists in scope at <file>:<line>`.

### 3. Testing & Verification Matrix
- [x] **Unit Tests**: Test identifier validation (`is_valid_identifier`) and collision checks in `src/refactor.hpp`.
- [x] **Integration Tests**: Test renaming function-local variable across multiple scopes without leaking, and cross-file exported struct renaming.
- [x] **Conformance Tests**: Test suite `test_cfire_refactor_rename` added to `tests/conformance_test.cpp` verifying:
  - Unified diff preview (`--diff`)
  - Structured JSON machine output (`--json`)
  - Scope collision detection (`cfire/REFACTOR-COLLISION`)
  - Invalid identifier rejection (`cfire/REFACTOR-INVALID-NAME`)
  - Atomic file rewrite (`--apply`) and verification that modified file compiles and executes cleanly
  - Multi-file cross-package symbol rename across definitions and call sites
- [x] **Manual Validation**: Tested `--diff`, `--apply`, `--json` on live `.cf` files.

### 4. Implementation Status
- **Status**: Completed & Verified. All tests passing (`make test` 100% pass, `./cfire doc --check` clean). Ready to merge.

---

## Active Task: `BS-EXT-01` Remote Fetching, Integrity Lockfile & Offline Vendoring (`cfire vendor`)

### 1. Scope & Problem Statement
- **Problem**:
  - `build.cf` currently requires local C/C++ source trees, local CMake directories, or system-installed `pkg-config` packages. Developers cannot easily declare dependencies on remote Git repositories (e.g. `raylib`, `fmt`) or HTTP tarball archives (e.g. `sqlite`, `cJSON`). Teams also lack cryptographic lockfiles (`build.lock`) for reproducible builds and cannot vendor dependencies into `./vendor/` for air-gapped or offline CI environments.
- **In Scope**:
  - In `src/buildsys.hpp`:
    - Extend `build.cf` DSL:
      - `build.Git(url, { Tag|Ref|Commit|Branch, Driver, Target, Sources, Includes, Defines })`
      - `build.Archive(url, { Sha256, Driver, Target, Sources, Includes, Defines })`
      - Auto-detection when the first argument to `build.CC(...)` or `build.CMake(...)` is an `https://` / `http://` / `.git` URL.
    - Centralized Cache Engine:
      - Cache root: `~/.cfire/cache/pkg/<pkg_name>_<hash>/` (overrideable with `$CFIRE_CACHE_DIR`).
      - Local vendor override: if `./vendor/<dep_name>` exists, use local vendor directory directly with zero network requests.
    - Transport & Extraction:
      - Git: shallow clone `git clone --depth 1 --branch <ref> <url> <dest>` or checkout specific commit.
      - Archive: `curl -fsSL` to ephemeral archive, verify SHA-256 hash, and extract via `tar` or `unzip`.
    - Reproducibility & Integrity Lockfile (`build.lock`):
      - Records resolved commit SHAs, URLs, and SHA-256 digests in a human-readable JSON manifest next to `build.cf`.
      - When `build.lock` exists, pin to exact locked hashes.
  - In `src/main.cpp`:
    - Command `cfire vendor [dest_dir]`:
      - Fetches all remote dependencies declared in `build.cf` and vendors them into `./vendor/` (or specified destination).
- **Out of Scope**:
  - Centralized package registry daemon / publishing service (decentralized direct Git/archive URLs match Go, Zig, and Rust git dependencies).

### 2. Open Questions & User Clarifications
- **Q1 (Default cache path)**: Where should fetched packages be cached when not vendored?
  - *Decision*: `~/.cfire/cache/pkg/<pkg_name>_<hash>` (or `$CFIRE_CACHE_DIR/pkg`).
- **Q2 (Offline / Air-Gapped behavior)**: What happens if there is no internet connection?
  - *Decision*: If `./vendor/<dep_name>` or cache already exists, builds proceed with zero network requests. If a fetch is required but fails, return a clear diagnostic: `cfire/FETCH-FAILED: failed to fetch '<pkg>' from '<url>': <error> (use 'cfire vendor' for air-gapped builds)`.

### 3. Testing & Verification Matrix
- [x] **Unit Tests**: Test URL detection, `build.lock` serialization/deserialization, and SHA-256 verification.
- [x] **Integration Tests**: Test shallow Git cloning and archive extraction to cache, and local `./vendor/` override.
- [x] **Conformance Tests**: Test suite `test_cfire_remote_vendor` added to `tests/conformance_test.cpp`.
- [x] **Manual Validation**: Test `cfire vendor` and `cfire run` with a project declaring remote dependencies.

### 4. Implementation Status
- **Status**: Completed & Verified. All tests passing (`make test` 100% pass, `./cfire doc --check` clean). Ready to merge.

---

## Active Task: `IDE-EXT-01` LSP Semantic Parameter & Type Inlay Hints (`textDocument/inlayHint`)

### 1. Scope & Problem Statement
- **Problem**:
  - `cfire lsp` supports type inlay hints (`kind: 1`) for `:=` variable bindings, but lacks parameter name hints (`kind: 2`) on call arguments. Developers reviewing or editing code in VS Code, Zed, or Neovim cannot see callee parameter names for positional arguments.
- **In Scope**:
  - In `src/lsp.hpp`:
    - Extend `compute_inlay_hints`:
      - Recursively traverse all `CallExpr` nodes across functions, methods, and tests.
      - Resolve callee parameter names for local functions, local methods, cross-package imported functions, and `core:*` stdlib modules.
      - Emit `kind: 2` (Parameter) inlay hints positioned at the start of each argument.
      - Intelligent suppression: skip arguments that already have explicit named argument labels (`label: val`), arguments that are variables with names matching the parameter name (`foo(x)` where param is `x`), and variadic/formatting built-ins (`println`, `print`, `panic`, `make`).
      - Sort combined type (`kind: 1`) and parameter (`kind: 2`) hints by `(line, character)` in accordance with the LSP 3.17 specification.
- **Out of Scope**:
  - Inlay hints for chained method return types (covered in future iterations).

### 2. Open Questions & User Clarifications
- **Q1 (Label styling)**: How should parameter labels be formatted?
  - *Decision*: `"<param_name>:"` with `paddingLeft: false, paddingRight: true`, standard in rust-analyzer, clangd, and gopls.
- **Q2 (Redundant hint suppression)**: Should `foo(bar)` show `bar: bar`?
  - *Decision*: Suppress parameter hint when the argument expression is an identifier matching the parameter name to reduce visual clutter.

### 3. Testing & Verification Matrix
- [x] **Unit Tests**: Test `walk_stmts_calls` AST traversal and parameter name extraction.
- [x] **Integration Tests**: Verify type hints and parameter hints coexist without collisions.
- [x] **Conformance Tests**: Add assertions to `tests/conformance_test.cpp` verifying `textDocument/inlayHint` returns `kind: 2` parameter hints with correct coordinates and labels.
- [x] **Manual Validation**: Interactive test via LSP mock client.

### 4. Implementation Status
- **Status**: Completed & Verified. All tests passing (`make test` 100% pass, `./cfire doc --check` clean). Ready to merge.

---

## Active Task: `BS-EXT-02` Multi-Target Cross-Compilation (`--target <triple>`)

### 1. Scope & Problem Statement
- **Problem**:
  - `cfire build` always compiles binaries targeting the host architecture and OS. Build pipelines targeting Linux servers, Docker containers, or distinct CPU architectures (e.g. `x86_64-apple-darwin`, `aarch64-linux-gnu`) need first-class `--target <triple>` and `--sysroot <path>` flags.
- **In Scope**:
  - In `src/buildsys.hpp`:
    - Extend `BuildPlan`: add `target_triple` and `sysroot`.
    - In `analyze_and_emit` and `build_native`:
      - Accept optional `target_triple` and `sysroot` arguments.
      - Inject `--target=<triple>` and `--sysroot=<dir>` into Clang compiler and linker invocations.
      - Inject target flags into C/C++ external dependency compilation in `build_local_dep`.
      - Automatically bypass host PCH when a non-empty `target_triple` is specified.
  - In `src/main.cpp`:
    - Parse `--target` / `-target` / `--target=` and `--sysroot` / `--sysroot=` flags across `build`, `emit-cpp`, `check`, `run`, and `test`.
    - If target is Windows, append `.exe` extension to output binary name.
    - Log target triple in build summary output.
- **Out of Scope**:
  - Bundled cross-compilation toolchain downloader (uses Clang's built-in target drivers and optional sysroot).

### 2. Open Questions & User Clarifications
- **Q1 (PCH handling)**: Should PCH be used when cross-compiling?
  - *Decision*: Bypass host PCH when `target_triple` is non-empty to prevent target architecture/OS mismatch errors in Clang.
- **Q2 (Output naming for Windows)**: Should Windows targets automatically get `.exe`?
  - *Decision*: Yes, append `.exe` if target contains `windows`, `win32`, or `msvc`.

### 3. Testing & Verification Matrix
- [x] **Unit Tests**: Test argument parsing for `--target` and `--sysroot`.
- [x] **Integration Tests**: Verify cross-compilation invocation to `arm64-apple-darwin` and `x86_64-apple-darwin`.
- [x] **Conformance Tests**: Add `test_cfire_cross_compile` to `tests/conformance_test.cpp`.
- [x] **Manual Validation**: Live execution of `./cfire build ... --target x86_64-apple-darwin` producing 64-bit Mach-O x86_64 executable.

### 4. Implementation Status
- **Status**: Completed & Verified. All tests passing (`make test` 100% pass, `./cfire doc --check` clean). Ready to merge.

---

## Active Task: `LIB-EXT-01` Cryptographic Hashing & Symmetric Encryption (`core:crypto/aes`, `core:crypto/hmac`)

### 1. Scope & Problem Statement
- **Problem**:
  - Systems applications and network services need constant-time MAC equality checks to prevent timing attacks, and authenticated symmetric encryption (AES-256-GCM AEAD and CTR mode) without relying on external dynamic library installations (OpenSSL/libsodium).
- **In Scope**:
  - In `runtime/cfire_core_stdlib.hpp`:
    - Add constant-time `hmac.equal` for `std::string_view` and `std::span<const std::byte>`.
    - Add pure C++23 header-only `namespace cfire::aes` with:
      - NIST FIPS 197 constant-time AES-256 block cipher and key expansion.
      - NIST SP 800-38D Galois/Counter Mode (GCM) with GHASH $GF(2^{128})$ authentication tag computation.
      - Counter Mode (CTR) symmetric stream encryption.
      - Functions: `aes.encrypt_gcm`, `aes.decrypt_gcm`, `aes.decrypt_gcm_str`, `aes.encrypt_ctr`, and `aes.decrypt_ctr`.
      - Safe error rejection when authentication tag verification fails or ciphertext is tampered.
  - In `src/docgen.hpp`:
    - Register `core:crypto/aes` module and document `encrypt_gcm`, `decrypt_gcm`, `encrypt_ctr`, and `decrypt_ctr`.
    - Document `hmac.equal` under `core:crypto/hmac`.
  - In `src/codegen.hpp`:
    - Map `core:crypto/*` imports to track stdlib inclusion.
    - Lower `aes.*` and `hmac.*` member calls to `cfire::aes::*` and `cfire::hmac::*`.
- **Out of Scope**:
  - Legacy insecure ciphers (DES, RC4, AES-128/192 ECB).

### 2. Open Questions & User Clarifications
- **Q1 (Nonce format)**: Standard 12-byte (96-bit) nonce vs arbitrary nonce lengths?
  - *Decision*: Enforce standard 12-byte (96-bit) nonces per NIST SP 800-38D section 7.1 recommendation for optimal performance and security.
- **Q2 (Decryption failure behavior)**:
  - *Decision*: Decryption verifies tag against ciphertext *before* emitting plaintext, returning `cfire::make_error` if tag does not match in constant time.

### 3. Testing & Verification Matrix
- [x] **Unit Tests**: Verify AES-256 and GHASH against official NIST test vectors (FIPS 197 and SP 800-38D Case 13).
- [x] **Integration Tests**: Verify roundtrip CTR and GCM encryption/decryption and tamper rejection (tag, ciphertext, and AAD tampering).
- [x] **Conformance Tests**: Add `test_crypto_hmac_and_aes` to `tests/conformance_test.cpp`.
- [x] **Manual Validation**: Live execution of end-to-end `.cf` script with AES-GCM and HMAC comparison.

### 4. Implementation Status
- **Status**: Completed & Verified. All tests passing (`make test` 100% pass, `./cfire doc --check` clean). Ready to merge.

---

## Active Task: `LD-EXT-01` Slice Pattern Matching & Destructuring (`match slice { [head, ..tail] => ... }`)

### 1. Scope & Problem Statement
- **Problem**:
  - Slices (`[]T` / `&[]T`) in `cfire` previously required manual length queries and index arithmetic. Systems code parsing buffers, packets, and tokens benefits significantly from declarative slice pattern matching with head/tail destructuring.
- **In Scope**:
  - In `src/ast.hpp`:
    - Extend `MatchArm` with slice pattern fields (`is_slice_pattern`, `slice_head`, `slice_rest`, `slice_has_rest`, `slice_tail`).
  - In `src/parser.hpp`:
    - Parse slice patterns inside `match`: `[]`, `[head, ..tail]`, `[a, b]`, `[..init, last]`, `[first, ..middle, last]`, and wildcard `_`.
  - In `src/sema.hpp`:
    - Recognize slice matches and bypass enum variant exhaustiveness checking while semantically verifying all arm bodies.
  - In `src/codegen.hpp`:
    - Lower slice matches to structured bounds and length checks (`_target.size() >= min_size` / `== exact_size`).
    - Bind head and tail elements by reference, and bind rest slices via zero-copy `cfire::slice(_target, start, end)`.
    - Support ergonomic `.len()` method calls on slices alongside `.len` property.
- **Out of Scope**:
  - Arbitrary regex patterns inside slice element bindings.

### 2. Open Questions & User Clarifications
- **Q1 (Zero-copy destructuring)**: Does `..tail` allocate a new vector?
  - *Decision*: No, `..tail` produces a zero-copy `cfire::slice` view (`std::span<const T>`).
- **Q2 (Pattern exhaustiveness)**: How is exhaustive coverage handled?
  - *Decision*: Fallback arms `_ => { ... }` or arms with rest `[..]` ensure match coverage.

### 3. Testing & Verification Matrix
- [x] **Unit Tests**: Test slice pattern parsing and codegen emission.
- [x] **Integration Tests**: Verify recursive slice matching functions (e.g. `sum_slice` on `&[]i32`).
- [x] **Conformance Tests**: Add `test_slice_pattern_matching` to `tests/conformance_test.cpp`.
- [x] **Manual Validation**: Verification of empty slice `[]`, single element `[x]`, exact pair `[a, b]`, sandwich `[first, ..mid, last]`, and recursive sum.

### 4. Implementation Status
- **Status**: Completed & Verified. All tests passing (`make test` 100% pass, `./cfire doc --check` clean). Ready to merge.

---

## Active Task: `LIB-EXT-04` Structured Concurrency Cancellation Tokens & Context (`core:sync/context`)

### 1. Scope & Problem Statement
- **Problem**:
  - `cfire` previously lacked cooperative cancellation and deadline propagation across async worker scopes and threads. Asynchronous workflows, RPC timeouts, and channel operations needed a structured cancellation token mechanism (`core:sync/context`) analogous to Go's `context.Context` and C#'s `CancellationTokenSource`.
- **In Scope**:
  - In `runtime/cfire_core_stdlib.hpp`:
    - Define `namespace cfire::sync::context` with:
      - `Context`: Shared-state reference-counted cancellation token.
      - `CancelFunc`: Cancellation invoker callback.
      - `context.background() -> Context`: Root non-cancellable context.
      - `context.with_cancel(parent: Context) -> (Context, CancelFunc)`: Derives cancellable child context.
      - `context.with_timeout(parent: Context, ms: i64) -> (Context, CancelFunc)`: Derives child context with automatic deadline expiration.
      - Methods on `Context`: `is_cancelled() -> bool`, `done() -> bool`, `reason() -> str`, `on_cancel(cb: fn())`, `wait()`, `wait_for(ms: i64) -> bool`.
    - Extend `cfire::sync::Channel<T>`:
      - `Channel<T>::recv_ctx(ctx: Context) -> T!`: Unblocks immediately with `cfire::make_error("context cancelled: <reason>")` when context is cancelled.
    - Extend `cfire::time`:
      - `time::sleep_ctx(ctx: Context, ms: i64) -> bool`: Sleeps for `ms` or terminates early if context is cancelled.
  - In `src/docgen.hpp`:
    - Register `core:sync/context` in stdlib catalog with full signatures, descriptions, and code examples.
    - Document `Channel.recv_ctx` and `time.sleep_ctx`.
  - In `src/codegen.hpp`:
    - Map `context.` calls and types.
- **Out of Scope**:
  - Thread interruption signals (cooperative cancellation only).

### 2. Open Questions & User Clarifications
- **Q1 (Parent-Child Cascading)**: When a parent context is cancelled, do all derived child contexts cancel?
  - *Decision*: Yes, `with_cancel` and `with_timeout` register an `on_cancel` listener on the parent that automatically triggers child cancellation.
- **Q2 (Channel Interruption Semantics)**: Does `Channel.recv_ctx` poll or sleep?
  - *Decision*: Zero polling. `recv_ctx` attaches an `on_cancel` callback that calls `cv.notify_all()`, immediately waking any waiting thread when cancellation occurs.

### 3. Testing & Verification Matrix
- [x] **Unit Tests**: Context creation, manual cancellation, and reason propagation.
- [x] **Integration Tests**: Parent-to-child cancellation cascading; timer thread timeout expiration.
- [x] **Conformance Tests**: `test_sync_context` in `tests/conformance_test.cpp`:
  - `with_cancel` cancels child and unblocks `time::sleep_ctx`.
  - `Channel.recv_ctx` unblocks instantly when context is cancelled.
  - Parent cancellation cascades to multiple children.
- [x] **Manual Validation**: Live execution of `.cf` script with timeout contexts.

### 4. Implementation Status
- **Status**: Completed & Verified. All tests passing (`make test` 100% pass, `./cfire doc --check` clean). Ready to merge.

---

## Active Task: `LIB-EXT-02` High-Level WebSockets Client & Server (`core:net/websocket`)

### 1. Scope & Problem Statement
- **Problem**:
  - Real-time applications, web dashboards, event streams, and LLM streaming agents require full-duplex RFC 6455 WebSockets. Currently, developers only have raw TCP (`core:net`) and HTTP/1.1 (`core:http`).
- **In Scope**:
  - In `runtime/cfire_core_stdlib.hpp`:
    - Pure C++23 header-only implementation of RFC 3174 SHA-1 (`detail::sha1`) and RFC 6455 handshake calculation:
      `Sec-WebSocket-Accept = base64(sha1(key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"))`.
    - RFC 6455 frame serializer (`encode_frame`) and frame parser (`decode_frame`):
      - Opcodes: Text (`0x1`), Binary (`0x2`), Close (`0x8`), Ping (`0x9`), Pong (`0xA`).
      - Client frames masked with 4-byte random key.
      - Server frames unmasked.
      - Full 7-bit, 16-bit, and 64-bit payload length support.
    - RAII `websocket::Conn`:
      - `send_text(msg: str) -> void!`
      - `send_binary(data: []byte) -> void!`
      - `recv_text() -> str!` (automatically handles Ping with Pong)
      - `recv_binary() -> []byte!`
      - `ping(payload: str = "") -> void!`
      - `close(code: u16 = 1000, reason: str = "") -> void`
      - `is_open() -> bool`
    - Functions:
      - `websocket.connect(url: str) -> Conn!`: Connects to `ws://host:port/path` via TCP and executes RFC 6455 opening handshake.
      - `websocket.upgrade(client_fd: int, headers: map[str]str) -> Conn!`: Server-side upgrade handshake responding with HTTP 101 Switching Protocols.
  - In `src/docgen.hpp`:
    - Register `core:net/websocket` in stdlib catalog with full documentation and examples.
  - In `src/codegen.hpp`:
    - Map `websocket.` calls and types.
- **Out of Scope**:
  - TLS/WSS (can run behind reverse proxies or stunnel without external OpenSSL dependency).

### 2. Open Questions & User Clarifications
- **Q1 (Masking Requirements)**: RFC 6455 requires all client-to-server frames to be masked, and server-to-client frames to be unmasked. How is this enforced?
  - *Decision*: `Conn` tracks `is_client_`. When `is_client_ == true`, frames are masked with random 4-byte keys; when false, frames are emitted unmasked.
- **Q2 (Ping/Pong Automation)**: What happens if `recv_text` or `recv_binary` receives a Ping control frame?
  - *Decision*: `Conn` automatically sends a Pong response frame carrying the same payload and continues waiting for the data frame.

### 3. Testing & Verification Matrix
- [x] **Unit Tests**: SHA-1 verification against official RFC 6455 handshake test vector.
- [x] **Integration Tests**: Frame encoding and decoding across Unix domain socketpairs and loopback TCP.
- [x] **Conformance Tests**: `test_net_websocket` in `tests/conformance_test.cpp`:
  - Loopback TCP client/server connection and upgrade.
  - Text frames exchange.
  - Binary payload echo.
  - Ping / Pong control flow.
  - Graceful closure handshake.
- [x] **Manual Validation**: Full `.cf` script execution verifying WebSocket client/server communication.

### 4. Implementation Status
- **Status**: Completed & Verified. All tests passing (`make test` 100% pass, `./cfire doc --check` clean). Ready to merge.



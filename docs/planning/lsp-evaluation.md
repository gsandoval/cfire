# Production-Grade LSP Capability Sweep & Gap Analysis

This document provides a comprehensive evaluation of the Language Server Protocol (LSP) capabilities implemented in `cfire-lsp` compared against industry-standard production language servers (`rust-analyzer`, `clangd`, `gopls`, and `zls`).

---

## 1. Executive Summary

A modern production-grade LSP is not merely an AST-to-text bridge; it is a **fault-tolerant, real-time semantic compiler** that must operate continuously over incomplete, syntactically malformed, and rapidly changing code buffers.

### Key Finding on `audit.` Auto-Completion Defect
- **Failure Mode**: When typing `audit.` in an editor, the line contains a trailing dot without a member identifier. Because the parser enforced strict syntax (`expected member name after '.'`), the full-file parse aborted, returning `std::unexpected`. This wiped out the entire AST (struct declarations, function scopes, type bindings) at the exact keystroke auto-completion was requested.
- **Resolution**:
  1. Implemented **resilient parsing recovery** in `get_or_parse_ast`: dynamically sanitizes trailing dots/colons across in-flight lines using synthetic tokens (`__cfire_placeholder__`).
  2. Implemented **AST caching**: retains `last_good_ast_` and `last_good_ws_` per document URI as a fallback.
  3. Added **slice / vector method completions**: dynamic methods (`len`, `empty`, `push`, `pop`, `capacity`, `clear`) now appear when chaining slice access (e.g. `audit.Entries.`).
  4. Added `"@"` to trigger characters for immediate `@pkg` attribute suggestions.

---

## 2. Capability Matrix: Production Standard vs. `cfire-lsp`

| Pillar | Capability / Action | Industry Standard (`rust-analyzer` / `gopls` / `clangd`) | Current `cfire-lsp` Status | Priority Gap |
|---|---|---|---|---|
| **1. Fault-Tolerant Parsing** | Resilient Parser Recovery | Lossless resilient Concrete Syntax Tree (CST) with panic-mode recovery nodes (parses around errors) | Full-file recursive descent; trailing-dot multi-line sanitizer + `last_good_ast_` cache | **P2**: Node-level statement recovery in `parser.hpp` so a broken statement does not invalidate subsequent declarations. |
| | Document Synchronization | Incremental sync (`textDocumentSync: 2`) with range-based diffs | Full document synchronization (`textDocumentSync: 1`) | **P3**: Full sync takes <10ms for current project scales; incremental can be deferred. |
| **2. Auto-Completion** | Dot / Scope Member Access | Deep chained member access (`a.b.c.`) with struct fields, methods, vector primitives | **Supported** (Struct fields, local packages, stdlib handles, `cpp::` bridge, slice vectors) | Complete. |
| | Unqualified Identifiers | Scope-aware local bindings, parameters, functions, structs, enums, stdlib modules | **Supported** (Filtered by declaration line to prevent forward leakage) | Complete. |
| | Snippet Completions | Typing `fn`, `for`, `match`, `type` expands parameterized templates with tabstops | Primitives/keywords offered as plain text items | **P1**: Add Snippet completion format (`InsertTextFormat: 2`) for functions, loops, and structs. |
| | Auto-Import Completion | Suggests unimported stdlib/package functions and auto-inserts `import` at file header | Only shows already-imported packages or stdlib module aliases | **P2**: Auto-insert `import "core:..."` when completing unimported stdlib functions. |
| **3. Hover & Type Inspection** | Hover on Identifiers | Markdown hover with doc-comments, `.cf` signature, lowered C++23 code, parameter intents | **Supported** (Leading multiline `//` comments, signatures, C++23 previews) | Complete. |
| | Struct Tag Hover | Explains `json:`, `cli:`, `db:` reflection and serialization mechanics | **Supported** (Rich documentation for struct tags and inline field comments) | Complete. |
| | Sub-expression Type Hover | Hovering over arbitrary sub-expressions (`x + 1`, chained calls) shows evaluated type | Only hovers on recognized word tokens/symbols | **P2**: Support range-based sub-expression type inference on hover. |
| | Memory Layout Inspection | Shows `sizeof`, `alignof`, and field byte offsets for struct types | Shows fields table and lowered C++ aggregate | **P3**: Add layout offsets and byte size info to struct hover tables. |
| **4. Navigation** | Go to Definition | Jumps to declaration of variables, parameters, functions, structs, and cross-package files | **Supported** (`textDocument/definition` across files and packages) | Complete. |
| | Go to Type Definition | On variable `audit`, jumps to `type AuditLog struct` declaration | Falls back to variable declaration line | **P1**: Implement dedicated `textDocument/typeDefinition`. |
| | Find References | Locates all call/read/write sites across open workspace files | **Supported** (Single-file references) | **P2**: Multi-file cross-workspace reference collection. |
| | Document Symbols / Outline | Structured hierarchy of structs, fields, functions, methods, enums, variants, tests | **Supported** (`textDocument/documentSymbol`) | Complete. |
| | Workspace Symbols | Fuzzy search across all symbols in workspace | **Supported** (`workspace/symbol`) | Complete. |
| **5. Live Diagnostics** | Syntax & SEMA Error Reporting | Live line/column diagnostics with severity, error codes, and source tagging | **Supported** (`cfire/SYNTAX`, `cfire/SEMA`, `cfire/MUT-CALLSITE`, etc.) | Complete. |
| | Debounced Diagnostics | Background worker thread with ~150ms debounce to prevent editor typing stutter | Synchronous on `didChange` / `didOpen` | **P3**: Background thread queue with ~150ms debounce. |
| | Unused Variable / Import Warnings | Greyed-out faded text (`DiagnosticTag: 1`) for unused variables or imports | Currently emits compiler warnings; doesn't tag unused variables | **P2**: Add linter pass tagging unused imports/variables. |
| **6. Code Actions & Quick-Fixes** | Quick-fixes (`textDocument/codeAction`) | Injects `mut ` / `out ` at call site or auto-declares variable mutable | **Supported** (`cfire/MUT-CALLSITE`, `cfire/IMMUTABLE-MUT-ARG`) | Complete. |
| | Rename Symbol (`textDocument/rename`) | Renames variable, function, or field across all usages with prepare check | **Supported** (Single-file rename + `prepareRename`) | **P2**: Cross-file renaming for exported package symbols. |
| | Exhaustive `match` Arm Generation | Quick-fix to automatically populate missing enum variants in `match` statement | Not implemented | **P2**: Code action to populate missing enum cases. |
| **7. Signature Help** | Parameter Hints (`textDocument/signatureHelp`) | Parameter active highlight, markdown doc, supports multiple commas and type args | **Supported** (User functions, methods, stdlib catalog, and Clang AST C/C++ bridge) | Complete. |
| **8. Inlay Hints** | Inferred Types & Parameter Names | Displays inferred `: Type` next to `:=` and parameter names at call sites | **Supported** (`textDocument/inlayHint`) | Complete. |
| **9. Code Formatting** | Full Document Formatting | Deterministic canonical formatting matching compiler standard | **Supported** (`cfire fmt` via `documentFormattingProvider`) | Complete. |
| | Selection Range Formatting | Formats only lines within selected range (`documentRangeFormattingProvider`) | Full document formatting only | **P3**: Format AST subtree matching line range. |
| **10. Semantic Highlighting** | Semantic Tokens (`textDocument/semanticTokens`) | Editor receives fine-grained token types: mutable vs immutable vars, intents, C++ interop | Currently relies on editor TextMate/Tree-Sitter syntax highlighting | **P1**: Implement `textDocument/semanticTokens/full` for distinct coloring of `mut`, `move`, and native C++ blocks. |

---

## 3. Recommended Action Roadmap

1. **Sprint 1 (High Priority / Immediate Developer Ergonomics)**:
   - **LSP-01**: Snippet completions for language constructs (`fn`, `type struct`, `type enum`, `match`, `for`).
   - **LSP-02**: Go to Type Definition (`textDocument/typeDefinition`).
   - **LSP-03**: Semantic Tokens Provider (`textDocument/semanticTokens/full`) for intent and mutability highlighting.

2. **Sprint 2 (Cross-File & Productivity Power Tools)**:
   - **LSP-04**: Multi-file cross-workspace reference collection and symbol renaming.
   - **LSP-05**: Auto-import suggestions for unimported stdlib functions.
   - **LSP-06**: Exhaustive `match` arm generation code action.

3. **Sprint 3 (Compiler Architecture & Polish)**:
   - **LSP-07**: Sub-expression type hover and struct memory layout inspector (`sizeof`/`alignof`).
   - **LSP-08**: Debounced background diagnostics worker thread.
   - **LSP-09**: Resilient statement-level recovery nodes in `src/parser.hpp`.

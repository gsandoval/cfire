# Planning & Execution Workflow

This directory maintains the project backlog, active development tasks, and architectural evaluations for the `cfire` programming language and toolchain.

## Core Files

1. [`language-design-evaluation.md`](./language-design-evaluation.md): Historical architectural critique and language design analysis across orthogonality, regularity, cognitive budget, lifetime safety, and modern PL lessons (all findings `LD-01`–`LD-14` resolved).
2. [`buildsys-evaluation.md`](./buildsys-evaluation.md): Historical adversarial audit of `build.cf`, foreign C/C++ project integration, multi-driver compilation, header inspection, and ejection fidelity (all findings `BS-01`–`BS-08` resolved).
3. [`stdlib-evaluation.md`](./stdlib-evaluation.md): Historical gap analysis of the initial 13 `core:*` modules benchmarked against Go, Python, Rust, Zig, and Odin that drove the expansion to 28 production `core:*` modules (all findings `LIB-01`–`LIB-12` resolved).
4. [`lsp-evaluation.md`](./lsp-evaluation.md): Historical capability sweep and gap analysis comparing `cfire-lsp` against `rust-analyzer`, `clangd`, `gopls`, and `zls` (all findings `LSP-01`–`LSP-10` resolved).
5. [`backlog.md`](./backlog.md): Complete catalog of implemented improvements, features, and fixes across LSP, Language Design (LD), Build System (BS), Standard Library (LIB), and Developer/Agent Tooling (`TOOL-*`, `IDE-*`, `*-EXT-*`).
6. [`architectural-backlog.md`](./architectural-backlog.md): Prioritized C++23 technical debt, header decomposition initiatives (`CPP-ARCH-01` through `CPP-ARCH-03`), and completed architectural milestones (`ARCH-*`, `CPP-01`–`CPP-06`).
7. [`active.md`](./active.md): The live in-flight workbench. Tasks currently being designed or coded live here with detailed scoping and verification criteria.
8. [`post-backlog-ideas.md`](./post-backlog-ideas.md): Archive of strategic explorations (`cfire vendor`, `cfire check --json`, `cfire outline`, `cfire doc --llm`, `cfire refactor`, `cfire eval`, `cfire ide`) that have since been promoted and implemented.
9. [`SENIOR_ENGINEER_REVIEW_GUIDELINES.md`](../SENIOR_ENGINEER_REVIEW_GUIDELINES.md): Mandatory architectural review checklist, memory/lifetime safety rules, and verification quality gates.

---

## Operating Protocol

Whenever work is performed on this repository, the following protocol **must** be followed:

```
[backlog.md] ──(Promote item)──> [active.md] ──(Clarify Scope & Verify)──> [Implement & Test] ──(Sign-off)──> [Done]
```

### 1. Item Promotion
- Items are selected from [`backlog.md`](./backlog.md) based on user priority and moved to [`active.md`](./active.md).

### 2. Pre-Implementation Quality Gate (Mandatory)
Before writing any implementation code for an item in [`active.md`](./active.md), the following sections **must** be populated:
- **Scope & Problem Statement**: Explicit definition of what is inside scope and what is deferred.
- **Open Questions & User Clarifications**: Identify design alternatives, API tradeoffs, or ambiguous behaviors. **Stop and ask the user** when decisions require user input or product preference.
- **Testing & Verification Matrix**: Explicit definition of required tests across applicable tiers:
  - **Unit Tests**: Isolated validation of parsers, helper algorithms, or utility functions.
  - **Integration Tests**: Inter-module compiler pipelines (e.g., frontend $\rightarrow$ sema $\rightarrow$ codegen $\rightarrow$ build system).
  - **Conformance Tests**: End-to-end runtime behavior, ISO C++23 lowered code emission, and LSP JSON-RPC protocol assertions in `tests/unit/` and `tests/e2e/`.
  - **Manual Validation**: Real-world validation in target editors (Zed, VS Code) or CLI invocation verifying live UX, trigger characters, and timing.
- **Implementation Design**: The exact files, classes, methods, and protocols to modify.

### 3. Implementation & Verification
- Code must compile cleanly with `clang++ -std=c++23 -O2 -Wall -Wextra` (zero warnings).
- **All identified tests must be implemented and executed**:
  - Conformance and regression suite (`make test`) must pass 100%.
  - Any new CLI or editor-level behaviors must undergo recorded manual or scripted validation.
  - Documentation checks (`./cfire doc --check`) must pass 100%.

### 4. Completion
- Once all testing tiers pass and results are documented in [`active.md`](./active.md), the item is marked as complete, and the next backlog item can be promoted.

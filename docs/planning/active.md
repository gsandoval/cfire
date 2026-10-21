# Active Workbench: In-Flight Implementation Tasks

This document tracks items currently under active design, scoping, or implementation from the `cfire` Architectural Roadmap.

---

## Task Registry & State Machine

| Task ID | Component | Status | Assignee | Review Check Gate |
| :--- | :--- | :---: | :---: | :---: |
| `CPP-01` | Repo Hygiene & Pure C++23 Runtime (`runtime/core/*.hpp`) | `[COMPLETED]` | Principal Compiler Engineer | Passed |
| `CPP-02` | Native C++23 Frontend, Formatter, Refactor, LSP, Buildsys | `[COMPLETED]` | Principal Compiler Engineer | Passed |
| `CPP-03` | Native C++23 Semantic Analyzer (`src/sema.hpp`) | `[COMPLETED]` | Principal Compiler Engineer | Passed |
| `CPP-04` | Native C++23 Code Generator (`src/codegen.hpp`) | `[COMPLETED]` | Principal Compiler Engineer | Passed |
| `CPP-05` | Native C++23 Docgen (`src/docgen.hpp`) & CLI Driver (`src/main.cpp`) | `[COMPLETED]` | Principal Compiler Engineer | Passed |
| `CPP-06` | Public Docs Invariance (`docs/reference.html`) & Full Test Verification | `[COMPLETED]` | Principal Compiler Engineer | Passed |

---

## Active Milestone Status: Complete

All tasks in the Pure C++23 Restoration & Technical Debt Backlog (`CPP-01` through `CPP-06`) have been completed and verified across all quality gates. Next candidates for promotion live in [`docs/planning/architectural-backlog.md`](./architectural-backlog.md) and [`docs/planning/backlog.md`](./backlog.md).

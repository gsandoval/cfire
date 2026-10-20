# Active Workbench: In-Flight Implementation Tasks

This document tracks items currently under active design, scoping, or implementation from the `cfire` Architectural Roadmap.

---

## Task Registry & State Machine

| Task ID | Component | Status | Assignee | Review Check Gate |
| :--- | :--- | :---: | :---: | :---: |
| `CLEAN-01` | Tests & CI / Intent Diagnostics | `[COMPLETED]` | Principal Compiler Engineer | Passed |
| `CLEAN-02` | Stdlib / Pure `.cf` Migration | `[PLANNED]` | Principal Compiler Engineer | Queued |
| `CLEAN-03` | Compiler / Host Separation & De-duplication | `[PLANNED]` | Principal Compiler Engineer | Queued |

---

## Active Task: `CLEAN-01` Intent-Driven Test Diagnostics & Verification Reporting

### 1. Scope & Problem Statement
- **Problem**:
  1. Conformance test suites in `tests/unit/test_stdlib.cpp` are grouped into opaque ticket batch names (`test_p1_features`, `test_p2_features`, `test_p2_batch2_features`, `test_p2_batch3_features`, `test_p2_batch4_features`, `test_p2_batch5_features`). The test output displays ticket numbers rather than the actual software capabilities or invariants being verified.
  2. The `Makefile` outputs cosmetic artificial agent banners (`@echo "✓ All Senior Engineer Review Quality Gates PASSED"`), rather than concrete, descriptive status lines for each quality gate.
- **In Scope**:
  - Decompose the 6 batch test functions into granular, intent-revealing functions named after their domain behaviors:
    - `test_crypto_sha256_hmac_and_csprng`
    - `test_composable_expression_try`
    - `test_transitive_header_and_link_flags`
    - `test_data_encodings_base64_hex_csv`
    - `test_relocatable_cmake_ejection`
    - `test_c_preprocessor_macro_injection`
    - `test_regex_matching_and_captures`
    - `test_datetime_rfc3339_and_durations`
    - `test_structured_logging_json_and_text`
    - `test_custom_build_steps_codegen`
    - `test_filesystem_streaming_and_paths`
    - `test_subprocess_piping_and_streaming`
    - `test_unicode_utf8_runes_and_case`
    - `test_variadic_generic_tuples`
  - Update `tests/test_harness.hpp` and `tests/test_main.cpp` to register and run each test individually.
  - Refactor `Makefile` to output clear, professional verification messages describing each quality gate's exact status.
- **Out of Scope**:
  - Changing test assertions or expected behaviors (100% test parity must be preserved).

### 2. Testing Strategy Defined Across Tiers
- **Unit & Conformance**:
  - Run `make test` to verify every test executes and passes.
  - Verify every test outputs `=== RUN test_<behavior>` and `--- PASS: test_<behavior>`.
- **Quality Gates**:
  - `make review-check` passes all 4 gates without warnings or artificial banners.

### 3. Implementation Plan
- [x] **Step 1**: Decompose `tests/unit/test_stdlib.cpp` into granular, intent-named functions.
- [x] **Step 2**: Update declarations in `tests/test_harness.hpp` and calls in `tests/test_main.cpp`.
- [x] **Step 3**: Update `Makefile` verification outputs to provide concrete functional feedback.
- [x] **Step 4**: Run `make test` and `make review-check` to verify all tests pass with clean intent outputs.

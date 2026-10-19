# `cfire` Spec 04 — Conformance Suite & Canonical Examples

## 1. Two-Tier Conformance Discipline
1. **Tier A — Golden Transpiler & Diagnostic Tests (`testdata/golden/`):**
   - Verifies exact `.cf` $\rightarrow$ `.gen.cpp` transpilation and exact `file:line:col: message (cfire/RULE)` semantic error reporting (`cfire/MUT-CALLSITE`, `cfire/USE-AFTER-MOVE`).
2. **Tier B — End-to-End Native C++23 Canonical Examples (`examples/`):**
   - Compiles each generated `.cpp` file with `clang++ -std=c++23 -O2`, links native libraries (`libzstd`, `libsimdjson`, `libsqlite3`, etc.), executes the resulting native binary, and verifies its runtime output.

## 2. Canonical Examples Matrix
- `examples/01-hello-core` — `str`, `in`/`mut`/`move` parameter intents, string interpolation, `defer`, `cplusplus` escape hatch.
- `examples/02-log-compressor` — Struct `cli:` tags, `core:fs`, `core:compress/zstd` auto-linking, `T!` (`std::expected`), `try`, and `or err`.
- `examples/03-pkg-cache` — Multi-tag struct codegen (`json:` + `db:`), `core:json` (`simdjson`), `core:db/sqlite` (`sqlite3`), `core:crypto/blake3`.
- `examples/04-async-workers` — Structured concurrency (`async.scope`), parallel worker tasks, error fallback (`or 503`).
- `examples/05-cmake-interop` — `build.cf` + `import cpp` with native C/C++ library headers (`<zlib.h>` / `<sqlite3.h>`) and Clang AST symbol verification.
- `examples/06-raylib-viz` — `vendor:raylib` codegen, `defer` scope pairing, and `build_plan.json` verification.

# Build System & Project Extensibility Architecture Evaluation

> [!NOTE]
> **Historical Baseline Audit (All Findings Resolved)**: This document records the initial adversarial audit of `cfire`'s build system and C/C++ interop. Every finding identified below (`BS-01` through `BS-08`, `BS-EXT-01`, and `BS-EXT-02`) has been **100% resolved and verified** in [`src/buildsys.hpp`](../../src/buildsys.hpp) and [`src/clang_bridge.hpp`](../../src/clang_bridge.hpp).

This document provides a comprehensive, rigorous adversarial audit of the `cfire` build system, foreign project integration, and C/C++ compiler drivers. It benchmarks `cfire` against modern language build systems (**Cargo**, **Zig build**, **Meson**, **CMake**, and **Go/cgo**) and identifies friction points when integrating real-world custom libraries.

---

## 1. Executive Summary & Architecture Overview

`cfire` integrates a built-in multi-driver build engine ([`src/buildsys.hpp`](../../src/buildsys.hpp)) that compiles native C/C++ source trees, executes external build tools (`CMake`, `Make`, `Ninja`), indexes C headers via a Clang AST Bridge ([`src/clang_bridge.hpp`](../../src/clang_bridge.hpp)), caches static archives (`.cfire/cache/lib/*.a`), links system libraries via `pkg-config`, and provides an ejection mechanism to standalone CMake projects (`cfire eject`).

### The Critical API Divergence
A primary finding of this audit is an immediate architectural schism between the documented interface and the actual compiler implementation:
- **`docs/GUIDE.md` (Chapter 8)** specifies a programmatic, imperative configuration API:
  ```cf
  package build
  import "core:build"
  pub fn configure(mut b: build.Context) {
      fast_hash := b.add_cc_library(build.CC{ ... })
      b.link(fast_hash)
  }
  ```
- **`src/buildsys.hpp` (`parse_build_cf`)** implements a static AST scraper that only looks for top-level global variable declarations:
  ```cf
  package build
  import "core:build"
  var FastCRC = build.CC("./c_deps/fastcrc", {
      Sources:  ["fastcrc.c"],
      Includes: ["."],
      Defines:  {"FASTCRC_SEED": 42},
  })
  ```
Following the official guide results in `cfire` finding zero dependencies and silently dropping all foreign libraries.

---

## 2. Adversarial Extensibility Analysis

### 2.1 Integrating Real-World C/C++ Dependencies

| Dependency | Intended Pattern | Real-World Failure Mode in `cfire` |
|---|---|---|
| **OpenSSL** | `import cpp "openssl/ssl.h" @pkg("openssl")` | Clang AST JSON dump is tens of megabytes; naive substring search chokes. Crucial APIs are preprocessor `#define` macros, which Clang AST ignores. Lacks raw pointer navigation (`->`) for `SSL*`. |
| **SQLite (Amalgamation)** | `build.CC("./vendor/sqlite", { Sources: ["sqlite3.c"] })` | Zero incremental compilation: recompiles 8.5MB `sqlite3.c` on every run. Lacks custom linker flags on Linux (`-lpthread`, `-ldl`). |
| **cJSON** | `build.CC("./vendor/cjson", { Sources: ["cJSON.c"] })` | Compiles `.a`, but accessing `cJSON->valuestring` or unions is blocked by `cfire` syntax limitations. |
| **Dear ImGui** | `build.CC("./vendor/imgui", { ... })` | Namespaces are stripped by `clang_bridge`. Overloaded C++ functions overwrite each other in the index map. Lacks platform frameworks (`Cocoa`, `OpenGL`). |
| **libuv** | `build.CMake("./vendor/libuv")` | Produces shared libraries or archives needing `-lrt -lpthread -lnsl -ldl`; `build.CMake` cannot pass transitive link flags to the main executable. |
| **Protobuf / Flatbuffers** | `protoc --cpp_out=. schema.proto` | Impossible. Zero pre-build code generation hooks or custom command steps. |

### 2.2 Compilation Mechanics & Caching
1. **Zero Incremental C Compilation**: `build_local_dep` unconditionally deletes `.cfire/cache/lib/lib<name>.a` on every single invocation and recompiles every `.c`/`.cpp` file from scratch via `clang`, with no file modification timestamp or content hash verification.
2. **Object Stem Collisions**: Object files are compiled to `<cache>/objs/<dep>/<stem>.o`. If a library contains `src/parser.c` and `src/lexer/parser.c`, both overwrite `parser.o`.
3. **Header AST Indexing**: `src/clang_bridge.hpp` parses the Clang AST JSON dump using raw substring searching (`find("\"kind\": \"FunctionDecl\"")`) bounded arbitrarily by `pos + 2048`. C++ namespaces are stripped, and overloaded functions overwrite each other.

### 2.3 Include Directory & Transitive Scoping
- All include directories across all dependencies are merged into a single global include list (`plan.cflags`). There is no target-level visibility (`PRIVATE` vs `PUBLIC` vs `INTERFACE`), inviting global header namespace collisions.
- Subpackage directories cannot maintain isolated `build.cf` manifests. Local `.cf` imports are resolved non-recursively, failing on transitive multi-package dependencies.

### 2.4 Header-Only & Shared Library Limitations
- **Header-Only Macro Injection**: Single-file libraries (e.g. `stb_image.h`, `miniaudio.h`) require `#define STB_IMAGE_IMPLEMENTATION` before inclusion. `cfire` provides no mechanism to inject macros into `main.cpp` prior to header inclusion.
- **Shared Libraries (`.so`, `.dylib`)**: The build system strictly checks for `.a` archives; shared libraries produced by CMake or Make are completely ignored, and no `-rpath` or dynamic linker flags are configured.

---

## 3. Prioritized Engineering Backlog: `BS-01` through `BS-08`

### `BS-01`: Reconcile Documentation & AST Scraper (Phantom `build.Context` API)
- **Status**: Planned (P1)
- **Category**: Build System / Ergonomics
- **Target Files**: `src/buildsys.hpp`, `docs/GUIDE.md`
- **Rationale**: `docs/GUIDE.md` specifies `pub fn configure(mut b: build.Context)`, but `buildsys.hpp` only scrapes top-level `var X = build.<Driver>()` expressions.
- **Remedy**: Either implement the programmatic `build.Context` evaluator or update `GUIDE.md` to document the actual declarative syntax.

### `BS-02`: Object-Level Timestamp / Content Caching for `build.CC`
- **Status**: Planned (P1)
- **Category**: Build System / Performance
- **Target Files**: `src/buildsys.hpp`
- **Rationale**: Every invocation recompiles all C/C++ source files from scratch. Furthermore, identical file stems in different directories collide in `<cache>/objs/<dep>/<stem>.o`.
- **Remedy**: Incorporate source file mtime/hash manifests and path-hash disambiguation for object files to enable true incremental compilation.

### `BS-03`: Structured Header AST Ingestion via `simdjson`
- **Status**: Planned (P1)
- **Category**: Clang Bridge / Interop
- **Target Files**: `src/clang_bridge.hpp`
- **Rationale**: Naive substring searching in JSON dumps drops C++ namespaces and overwrites overloaded functions.
- **Remedy**: Replace substring searches with structured JSON parsing using the embedded `simdjson` library, preserving namespaces and function overload sets.

### `BS-04`: Hermetic & Relocatable CMake Ejection (`cfire eject`)
- **Status**: Planned (P2)
- **Category**: Toolchain / Ejection
- **Target Files**: `src/buildsys.hpp`
- **Rationale**: `cfire eject` emits broken CMake configurations for dependencies with empty `dep.sources`, drops linker flags, and leaves vendor paths un-copied.
- **Remedy**: Mirror vendor trees into `out_dir/vendor/`, emit `add_subdirectory` or `FetchContent` for CMake dependencies, and propagate linker flags into CMake targets.

### `BS-05`: Preprocessor Macro Injection for Header-Only C Libraries
- **Status**: Planned (P2)
- **Category**: Foreign Interop / Syntax
- **Target Files**: `src/parser.hpp`, `src/codegen.hpp`
- **Rationale**: Single-file libraries like `stb_image.h` require `#define STB_IMAGE_IMPLEMENTATION` before inclusion.
- **Remedy**: Support an attribute on C imports: `import cpp "stb_image.h" @define("STB_IMAGE_IMPLEMENTATION")`.

### `BS-06`: Shared Library (`.so` / `.dylib`) & Dynamic Link Support
- **Status**: Planned (P2)
- **Category**: Build System / Linking
- **Target Files**: `src/buildsys.hpp`
- **Rationale**: CMake and Make dependencies generating shared libraries fail because `buildsys.hpp` only looks for `.a` archives.
- **Remedy**: Detect `.so` and `.dylib` artifacts, append them to `plan.ldflags`, and inject `-Wl,-rpath` flags.

### `BS-07`: Recursive Local Package Resolution & Subpackage `build.cf`
- **Status**: Planned (P2)
- **Category**: Module System / Build
- **Target Files**: `src/buildsys.hpp`
- **Rationale**: Local package import resolution is non-recursive, and subdirectories cannot declare isolated `build.cf` manifests.
- **Remedy**: Make local package resolution recursive and aggregate C dependency specifications from subdirectories.

### `BS-08`: Pre-Build Code Generation Hooks (`build.Command`)
- **Status**: Planned (P3)
- **Category**: Build System / Metaprogramming
- **Target Files**: `src/buildsys.hpp`, `src/parser.hpp`
- **Rationale**: Projects using Protobuf, Flatbuffers, or parser generators cannot declare pre-compilation generation steps in `build.cf`.
- **Remedy**: Add a `build.Command` or `build.Gen` step primitive to `build.cf`.

# The `cfire` Language & Systems Programming Guide

> **Sapphire on Fire (`cfire`)** is a statically typed systems programming language that combines Go's readability and fast onboarding, Odin's batteries-included `core:*` library catalog, and Rust-inspired explicit mutation/move safety—compiling 1-to-1 into human-readable **ISO C++23**.

---

## Chapter 1: Philosophy & Architecture

### Why Transpile 1-to-1 to ISO C++23?
Most modern C++ successors fail in production for one of two reasons:
1. **The Interop Wall**: Languages with custom object layouts or opaque runtimes require expensive FFI marshaling to talk to C and C++ libraries.
2. **The Lock-In Risk**: Adopting a new language is risky if your team cannot easily debug, profile, or migrate the codebase using standard tools.

`cfire` solves both by treating **modern ISO C++23 as an ergonomic, zero-wrapper target**:
- Every `type S struct` lowers to a standard C++23 aggregate `struct S`.
- Every `str` is `std::string` (owned) or `std::string_view` (`in` parameter borrow).
- Every `[]T` slice is `std::vector<T>` (owned) or `std::span<const T>` (`in` parameter borrow).
- Every fallible return `T!` is `std::expected<T, cfire::Error>`.
- At any moment, `cfire eject` exports your project into a standalone C++23 + `CMakeLists.txt` repository with zero lock-in.

---

## Chapter 2: Tour of the Toolchain

`cfire` ships as a single, self-contained C++23 binary containing the compiler, build system, test runner, formatter, language server, and documentation generator:

```zsh
# 1. Run a program directly (auto-links core/vendor/C++ dependencies)
./cfire run examples/01-hello-core/main.cf

# 2. Build an optimized native binary
./cfire build examples/07-types-match-map/main.cf

# 3. Run built-in `test "..." { ... }` blocks
./cfire test examples/09-stdlib-test-pointers/main.cf

# 4. Canonically format `.cf` files (or verify formatting in CI with --check)
./cfire fmt examples/
./cfire fmt --check examples/

# 5. Start the Language Server Protocol (LSP) server for your editor
./cfire lsp

# 6. Generate or verify always-up-to-date docs (or inspect a single .cf file)
./cfire doc
./cfire doc --check
./cfire doc examples/09-stdlib-test-pointers/main.cf

# 7. Eject to a standalone C++23 + CMake project
./cfire eject examples/08-multi-pkg-cc-build/main.cf ./standalone_cpp
```

---

## Chapter 3: Values, Ownership, Parameter Intents & Smart Pointers

### 3.1 Immutable by Default
Local variables created with `:=` or explicit types (`x: i32 = 10`) are **immutable (`const`) by default**. Use `mut` when you intend to reassign or mutate a binding:

```cf
limit := 100              // immutable (const)
fixed: [4]i32 = [1,2,3,4] // immutable std::array<int32_t, 4>
mut counter: i32 = 0      // mutable
counter += 1
```

### 3.2 Explicit Parameter Intents (`in`, `mut`, `out`, `move`)
In C++, a function call `process(data)` hides whether `data` is read-only, mutated in-place, or moved from. `cfire` makes parameter intent explicit at **both** the declaration and the call site:

| Intent | Declaration | Call Site | C++23 Lowering | Semantic Guarantee |
|---|---|---|---|---|
| **`in` (default)** | `x: T` | `f(x)` | `const T&` / `std::string_view` / `std::span<const E>` | Read-only borrow; zero copy |
| **`mut`** | `mut x: T` | `f(mut x)` | `T&` | Requires `mut` binding & `mut` at call site (`cfire/MUT-CALLSITE`, `cfire/IMMUTABLE-MUT-ARG`) |
| **`out`** | `out x: T` | `f(out x)` | `T&` | Requires `mut` binding & `out` at call site (`cfire/OUT-CALLSITE`) |
| **`move`** | `move x: T` | `f(move x)` | `T&&` + `std::move(x)` | Rejects any subsequent use of `x` (`cfire/USE-AFTER-MOVE`) |

### 3.3 Smart Pointers (`own[T]` & `shared[T]`)
Value types and slices cover 95% of systems code. When heap allocation or shared ownership is needed, use `own[T]` (`std::unique_ptr<T>`) and `shared[T]` (`std::shared_ptr<T>`):

```cf
owned_box := make(own[Box], Box{Value: 42})
shared_box := make(shared[Box], Box{Value: 58})
total := (*owned_box).Value + (*shared_box).Value
```

---

## Chapter 4: Algebraic Data Types, Struct Tags & Exhaustive `match`

### 4.1 Structs & Compile-Time Field Tags
Struct fields support backtick metadata tags (`json:"..."`, `sql:"..."`, `cli:"..."`) inspected at compile time by `cfire` to generate zero-overhead parsers, serializers, and database row mappers:

```cf
type Metric struct {
    Service: str  `json:"service" sql:"service"`
    Count:   i64  `json:"count"   sql:"count"`
    Latency: f64  `json:"latency"`
    Healthy: bool `json:"healthy"`
}
```

### 4.2 Algebraic Enums & Exhaustive `match`
`type ... enum` declares a tagged union lowered to `std::variant`. The semantic analyzer enforces `cfire/NON-EXHAUSTIVE-MATCH` if any variant is omitted without a `_ => { ... }` fallback arm:

```cf
type Shape enum {
    Circle(radius: f64),
    Rect(width: f64, height: f64),
    Empty,
}

fn describe(s: Shape) -> str {
    mut label := "unknown"
    match s {
        Circle(r)  => { label = "circle" }
        Rect(w, h) => { label = "rect" }
        Empty      => { label = "empty" }
    }
    return label
}
```

---

## Chapter 5: Fearless Error Handling & `defer`

`cfire` eliminates C++ exceptions in favor of value-based `T!` (`std::expected<T, cfire::Error>`):

- **`try expr`**: Propagates an error immediately to the caller (or prints & exits with code `1` in `main`).
- **`expr or fallback`**: Unwraps the value or evaluates `fallback` (`std::expected::value_or`).
- **`expr or err { ... }`**: Handles the error `err` inline with a block that returns or recovers.
- **`defer expr`**: Schedules deterministic scope cleanup via `cfire::scope_exit` RAII guards.

```cf
fn load_payload(path: str) -> []byte! {
    raw := try fs.read_all(path)
    return raw
}

fn main() {
    data := load_payload("missing.bin") or err {
        println("recovered from: {}", err.message())
        return
    }
}
```

---

## Chapter 6: Structured Concurrency & Typed Channels

### 6.1 `async.scope` Structured Task Pools
`async.scope(workers: N) |scope| { ... }` creates a bounded thread-pool scope that automatically joins all spawned tasks (`scope.spawn || { ... }`) before exiting the block—preventing dangling background threads:

```cf
mut counter := atomic(0u64)
async.scope(workers: 4) |scope| {
    for _, item in items {
        scope.spawn || {
            counter.fetch_add(1)
        }
    }
}
```

### 6.2 `core:sync` Typed Channels (`Channel[T]`)
Combine `async.scope` with `sync.make_channel[T]()` for message-passing pipelines:

```cf
import "core:sync"

ch := sync.make_channel[i64]()
async.scope(workers: 2) |scope| {
    scope.spawn || {
        ch.send(777)
    }
}
val := ch.recv() or 0
```

---

## Chapter 7: Zero-Friction C & C++ Interop

1. **Direct Header Imports (`import "cpp:..."` and `import "c:..."`)**:
   `cfire` invokes `clang++ -Xclang -ast-dump=json` on imported C/C++ headers, caches the extracted function signatures in `.cfire/cache/ast/<hash>.idx`, and automatically inserts `cfire::c_str(arg)` when passing a `cfire` `str` to a C `const char*` parameter.
2. **Inline `cpp { ... }` Escape Hatch**:
   Drop raw C++23 statements directly inside any `.cf` function when using intrinsics or template metaprogramming.

---

## Chapter 8: Unified Build System (`build.cf`) & CMake Ejection

When a project needs custom C/C++ static libraries, CMake subprojects, or `pkg-config` system libraries, add a declarative `build.cf` alongside `main.cf`:

```cf
package build

import "core:build"

// Pure C/C++ static library compilation driver:
var FastHash = build.CC("./native/fast_hash", {
    Sources:  ["fast_hash.c"],
    Includes: ["."],
    CFlags:   ["-O3"],
})

// CMake subproject dependency driver:
var KVStore = build.CMake("./vendor/libkv_cmake", {
    Target:   "kvstore",
    Includes: ["include"],
    Defines:  {"KV_SALT": 42},
})
```

Supported drivers include `build.CC`, `build.CMake`, `build.Make`, and `build.Ninja`. Run `./cfire build main.cf` to compile all dependencies into cached static archives (`.cfire/cache/lib/*.a`) automatically, or run `./cfire eject main.cf ./exported` to generate a standalone `CMakeLists.txt`!

---

## Chapter 9: How `cfire` Compares & Honest Design Trade-Offs

### 9.1 `cfire` vs. Cppfront (Cpp2) and Carbon
- **Cppfront (Cpp2)** is a syntax reskin of all of C++: it requires declaration-site intents (`inout`, `out`), but a call `f(x)` still hides whether `x` is borrowed or mutated, and you still write raw CMake and STL boilerplate.
- **Carbon** builds a massive custom compiler frontend to support full C++ class-hierarchy inheritance and template specialization.
- **`cfire`** takes the **TypeScript + Go/Odin approach to C++23**: it keeps the language deliberately small, enforces call-site intents (`mut`, `out`, `move`) + call-site mutable aliasing exclusivity (`cfire/MUT-ALIAS`), ships 13 `core:*` modules + `build.cf` + `fmt` + `lsp` + `doc`, and compiles 1-to-1 to standard C++23 types with a zero-lock-in `cfire eject` escape hatch.

### 9.2 `cfire` vs. Rust, Zig, and Odin
- **Vs. Zig & Odin:** Zig and Odin are outstanding C replacements, but hit a wall when consuming C++ STL/templates/classes (`std::string`, `std::vector`, `<simdjson.h>`, RocksDB, PyTorch C++). `cfire` natively shares the C++23 ABI.
- **Vs. Rust:** `cfire` captures ~85% of day-to-day bug prevention (immutable by default, explicit `mut`/`out`/`move` at call sites, `cfire/MUT-ALIAS` Law of Exclusivity at call sites, `cfire/USE-AFTER-MOVE`, `cfire/NON-EXHAUSTIVE-MATCH`, `T!` error handling) without lifetime annotations (`'a`).

### 9.3 Honest Trade-Offs & Built-In Mitigations
1. **No Full Borrow-Checker Lifetimes Across Heap Graphs:** `cfire` checks call-site mutation exclusivity (`cfire/MUT-ALIAS`) and local use-after-move (`cfire/USE-AFTER-MOVE`), but does not track reference lifetimes stored inside structs. **Mitigation:** Run `./cfire test --sanitize` or `./cfire run --sanitize` to enable Clang AddressSanitizer + UndefinedBehaviorSanitizer (`-fsanitize=address,undefined`) out of the box.
2. **Backend Compile Speed Bounded by `clang++ -std=c++23`:** Because `cfire` emits standard C++23, final binary compilation invokes `clang++`. **Mitigation:** `cfire` automatically precompiles `cfire_prelude.hpp` into a cached `.pch` precompiled header and caches Clang AST header dumps in `.cfire/cache/ast/`.

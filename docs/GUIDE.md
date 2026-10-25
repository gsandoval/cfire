# The `cfire` Language & Systems Programming Guide

> **`cfire`** (pronounced like **"sapphire"** `/ˈsæf.aɪ.ər/`, or **"C-fire"**) is an ergonomic, memory-safe systems programming language that lowers 1-to-1 into modern **ISO C++23**. The name pairs **"C"** (the bedrock of systems engineering) with **"fire"** (blazing modern velocity, or developer slang for *top-tier*). It combines the clean syntax and rapid onboarding of Go, the batteries-included `core:*` catalog of Odin, and the explicit mutation and move safety of Rust—with zero garbage collection, zero runtime lock-in, and full mechanical sympathy.

---

## Chapter 1: Philosophy & Architecture

### 1.1 The Systems Programming Landscape & The `cfire` Name

Where does the name **`cfire`** come from?
- **`C`**: The programming language heritage of systems software—deterministic memory control, zero-overhead abstractions, and mechanical sympathy.
- **`fire`**: In modern engineering slang, *"it's fire"*—blazing fast, lightweight, and genuinely exciting to work with.
- **Pronounced like "sapphire"**: The name rolls off the tongue as *sapphire* (`/ˈsæf.aɪ.ər/`), matching its visual identity—the crystalline structure and precision of a sapphire gem ignited by the speed and focus of fire.

Modern systems software demands both high developer velocity and uncompromising runtime performance. For decades, engineers have navigated difficult compromises:
- **C++** offers unmatched performance, vast ecosystem reach, and zero-cost abstractions, but suffers from forty years of accumulated syntactic complexity, implicit mutable references, and silent use-after-move bugs.
- **Rust** provides ironclad memory safety guarantees via compile-time lifetime analysis (`'a`), but introduces steep cognitive overhead and challenging FFI barriers when integrating with existing C++ codebases.
- **Go** offers outstanding developer ergonomics and networking libraries, but mandates a garbage collector and non-deterministic pause times unsuitable for real-time, game engine, or embedded kernels.
- **Zig and Odin** provide brilliant C replacements with manual allocators, but cannot directly consume C++ templates, classes, or standard library types without brittle C ABI wrappers.

`cfire` takes a pragmatic path designed specifically for teams whose target environment is **ISO C++23**:
1. **Ergonomic, Modern Frontend**: Clean syntax, immutability by default, explicit parameter intents (`in`, `mut`, `move`), multi-return tuples, algebraic enums, and monadic error handling (`T!`).
2. **1-to-1 ISO C++23 Lowering**: Every `cfire` type, function, and expression lowers directly to standard C++23 concepts (`std::string_view`, `std::span`, `std::variant`, `std::expected`, `std::unique_ptr`).
3. **Zero FFI Marshaling**: Call any C or C++ header directly without writing manual wrappers.
4. **Zero Lock-In**: Run `cfire eject` at any time to export your entire project as standard C++23 source code with a standard `CMakeLists.txt`.

### 1.2 Predictable 1:1 Lowering Table

The following table summarizes how core `cfire` types lower into standard ISO C++23:

| `cfire` Type | ISO C++23 Lowering | Ownership & Semantics |
|---|---|---|
| `i8`, `i16`, `i32`, `i64` | `int8_t`, `int16_t`, `int32_t`, `int64_t` | Signed fixed-width integers |
| `u8`, `u16`, `u32`, `u64` | `uint8_t`, `uint16_t`, `uint32_t`, `uint64_t` | Unsigned fixed-width integers |
| `f32`, `f64` | `float`, `double` | IEEE 754 floating point |
| `bool`, `char` | `bool`, `char` | Boolean flag, ASCII/UTF-8 byte |
| `str` (value) | `std::string` | Owned, heap-backed UTF-8 string |
| `str` (`in` param) | `std::string_view` | Non-allocating read-only string view |
| `path` | `std::filesystem::path` | Platform-native filesystem path |
| `[N]T` | `std::array<T, N>` | Contiguous stack array, zero overhead |
| `[]T` (value) | `std::vector<T>` | Owned, dynamic contiguous buffer |
| `[]T` (`in` param) | `std::span<const T>` | Non-allocating contiguous slice borrow |
| `map[K]V` | `std::unordered_map<K, V>` | Hash table with O(1) average lookup |
| `(T1, T2)` | `std::tuple<T1, T2>` | Heterogeneous value tuple |
| `T?` | `std::optional<T>` | Nullable value container |
| `T!` | `std::expected<T, cfire::Error>` | Value-or-error monadic result |
| `own[T]` | `std::unique_ptr<T>` | Single heap ownership with RAII cleanup |
| `shared[T]` | `std::shared_ptr<T>` | Atomic reference-counted shared ownership |
| `type S struct` | `struct S { ... }` | Standard C++ aggregate struct |
| `type E enum` | `std::variant<...>` | Tagged union with exhaustive `match` |

### 1.3 Hello, Systems! (A First Look)

Here is a complete, working `cfire` program showing basic syntax, imports, error handling, and deterministic deferral:

```cf
package main

import "core:fs"
import "core:time"
import "core:str"

fn main() {
    t0 := time.now_ms()

    // Immutability by default: 'msg' cannot be reassigned
    msg := "Hello from cfire (pronounced sapphire)!"
    println("{}", msg)

    // Fallible file operation with early-exit 'try'
    try fs.write_text("build_info.txt", "runtime=cfire\nstandard=c++23\n")
    
    // Deterministic scope cleanup: runs when main exits
    defer {
        println("Cleanup: execution finished in {} ms", time.now_ms() - t0)
    }

    content := try fs.read_text("build_info.txt")
    lines := str.split(content, "\n")
    for idx, line in lines {
        if line.len() > 0 {
            println("  [{}] {}", idx + 1, line)
        }
    }
}
```

---

## Chapter 2: Tour of the Toolchain

`cfire` ships as a single, self-contained binary. There are no secondary package managers, formatters, or test runners to install.

### 2.1 Toolchain Subcommands Reference

| Subcommand | Usage | Purpose & Details |
|---|---|---|
| `run` | `cfire run main.cf` | Compiles and executes the program. Auto-links required `core:*` and C/C++ libraries. |
| `run --web` | `cfire run --web [build.cf]` | Builds and serves the WebAssembly app via the built-in development HTTP server. |
| `run --sanitize` | `cfire run --sanitize main.cf` | Enables Clang AddressSanitizer and UndefinedBehaviorSanitizer (`-fsanitize=address,undefined`). |
| `build` | `cfire build main.cf -o app` | Produces an optimized native executable (`-O3`) in the current directory. |
| `build --subsystem gui` | `cfire build --subsystem gui main.cf -o app` | Packages a native GUI desktop app (signed `.app` bundle on macOS; `-mwindows` on Windows). |
| `build --target wasm` | `cfire build --target wasm main.cf -o app.html` | Cross-compiles to WebAssembly (`.html`, `.js`, `.wasm`) with responsive WebGL canvas or CLI profile. |
| `serve` | `cfire serve [--port 8080] [path]` | Built-in zero-dependency static HTTP/1.1 server with `application/wasm` MIME and COOP/COEP isolation headers. |
| `test` | `cfire test main.cf` | Discovers and executes all embedded `test "..." { ... }` suites in parallel. |
| `test --sanitize` | `cfire test --sanitize main.cf` | Executes all unit tests with ASan and UBSan active to catch memory leaks and bounds errors. |
| `fmt` | `cfire fmt src/` | Canonically formats `.cf` files in-place according to strict formatting rules. |
| `fmt --check` | `cfire fmt --check src/` | CI validation mode. Exits with non-zero status if any file is unformatted. |
| `check` | `cfire check main.cf` | Fast frontend validation: parses and runs semantic analysis without invoking the C++ backend. |
| `eval` | `cfire eval '2 + 2'` | Evaluates a single `cfire` expression or statement directly from the command line. |
| `lsp` | `cfire lsp` | Starts the standard Language Server Protocol daemon for VSCode, Neovim, and Helix. |
| `doc` | `cfire doc` | Generates verified HTML and Markdown documentation from the compiler catalog. |
| `doc --check` | `cfire doc --check` | Verifies that documentation files match the compiler catalog character-for-character. |
| `eject` | `cfire eject main.cf ./export` | Exports the program and all dependencies into standalone ISO C++23 files + `CMakeLists.txt`. |

### 2.2 Embedded Unit Testing

`cfire` includes testing as a core language primitive. You can write isolated test suites directly inside any `.cf` file:

```cf
package math_util

fn add(a: i64, b: i64) -> i64 {
    return a + b
}

fn clamp(val: f64, low: f64, high: f64) -> f64 {
    if val < low { return low }
    if val > high { return high }
    return val
}

test "integer arithmetic addition" {
    assert(add(10, 20) == 30)
    assert(add(-5, 5) == 0)
}

test "floating-point clamp bounds" {
    assert(clamp(5.0, 0.0, 10.0) == 5.0)
    assert(clamp(-2.0, 0.0, 10.0) == 0.0)
    assert(clamp(15.0, 0.0, 10.0) == 10.0)
}
```

Run tests with a single command:
```zsh
./cfire test math_util.cf
```

### 2.3 WebAssembly & Built-In HTTP Development Server

WebAssembly applications built for browsers require execution over HTTP with strict security and MIME configurations. Browsers reject `.wasm` compilation over the `file://` protocol, and modern WebAssembly threads and WebGL backends require Cross-Origin Isolation headers.

`cfire` solves this out of the box with zero external dependencies:

```zsh
# 1. Build your desktop or graphics app for WebAssembly:
cfire build --target=wasm app/main.cf -o app/app-web.html

# 2. Serve the directory or build.cf and launch your default browser:
cfire serve app/app-web.html

# Or build and launch in one command:
cfire run --web
```

The built-in server automatically:
- Serves `.wasm` files with `Content-Type: application/wasm`
- Serves `.js` files with `Content-Type: application/javascript; charset=utf-8`
- Enforces `Cross-Origin-Opener-Policy: same-origin` and `Cross-Origin-Embedder-Policy: require-corp`
- Resolves port collisions automatically by incrementing (+1..+25) if port 8080 is in use
- Prevents directory traversal attacks (`..` resolution)
- Launches your platform default browser (`open`, `xdg-open`, `start`) unless `--no-open` is passed

---

## Chapter 3: Syntax, Primitives, Variables & Control Flow

### 3.1 Immutability by Default
In `cfire`, bindings declared with `:=` or explicit type annotations without `mut` are strictly **immutable (`const`)**. Attempting to reassign an immutable binding triggers a compiler error:

```cf
limit := 100
// limit = 200 // ERROR: cannot assign to immutable binding 'limit'

mut counter := 0 // Mutable binding
counter += 1     // OK
```

Explicit type declarations follow the `name: Type = value` syntax:
```cf
max_payload: u64 = 1048576
mut buffer_size: i32 = 4096
```

### 3.2 Slices (`[]T`) vs Fixed Arrays (`[N]T`)
`cfire` makes a sharp distinction between stack-allocated fixed arrays and dynamically sized slices:
- **Fixed Arrays (`[N]T`)**: Size is known at compile time, memory is allocated on the stack with zero overhead, lowering to `std::array<T, N>`.
- **Dynamic Slices (`[]T`)**: Growable dynamic buffers backed by the heap, lowering to `std::vector<T>` when owned, or `std::span<const T>` when borrowed as an `in` parameter.

```cf
// Fixed array (std::array<int32_t, 4>)
fixed: [4]i32 = [10, 20, 30, 40]
println("fixed length: {}", fixed.len())

// Dynamic slice (std::vector<int32_t>)
mut dynamic: []i32 = [1, 2, 3]
dynamic.push(4)
dynamic.push(5)
println("dynamic length: {}", dynamic.len())

// Slicing views (std::span)
sub := dynamic[1:3] // View of elements at index 1 and 2
```

### 3.3 Hash Maps (`map[K]V`)
Hash maps provide $O(1)$ key-value lookups backed by `std::unordered_map`:

```cf
mut scores: map[str]i64 = {}
scores["Alice"] = 95
scores["Bob"] = 88

// Safe retrieval with 'or' fallback
alice_score := scores["Alice"] or 0
charlie_score := scores["Charlie"] or -1

println("Alice: {}, Charlie: {}", alice_score, charlie_score)

// Check existence
if scores.contains("Alice") {
    println("Alice is enrolled")
}
```

### 3.4 Tuples & Destructuring
Tuples group multiple values together without declaring a formal struct:

```cf
// Constructing a tuple
coord := (12.5, 45.8, "GPS-Locked")

// Destructuring into individual bindings
(lat, lon, status) := coord
println("Lat: {}, Lon: {}, Status: {}", lat, lon, status)
```

### 3.5 Optionals (`T?`)
Values that may be absent are wrapped in `T?`, lowering directly to `std::optional<T>`:

```cf
fn find_user(id: i64) -> str? {
    if id == 42 {
        return "Alice"
    }
    return null
}

user := find_user(42) or "Guest"
println("User: {}", user)
```

### 3.6 Smart Pointers (`own[T]` and `shared[T]`)
Value semantics and slices cover most systems programming needs. For recursive data structures or explicit heap allocations, `cfire` provides `own[T]` (`std::unique_ptr<T>`) and `shared[T]` (`std::shared_ptr<T>`):

```cf
type Node struct {
    Value: i64,
    Next:  own[Node]?,
}

// Allocate an owned heap box
first := make(own[Node], Node{Value: 10, Next: null})
println("Node value: {}", (*first).Value)
```

### 3.7 Control Flow (`if`, `for`, `match`)

```cf
// 1. Conditional branching
if score >= 90 {
    println("Grade: A")
} else if score >= 80 {
    println("Grade: B")
} else {
    println("Grade: C")
}

// 2. Iterating elements
items := ["alpha", "beta", "gamma"]
for item in items {
    println("Item: {}", item)
}

// 3. Iterating with index
for idx, item in items {
    println("[{}] = {}", idx, item)
}

// 4. Iterating map key-value pairs
for key, val in scores {
    println("{} => {}", key, val)
}

// 5. C-style numeric loop
for mut i := 0; i < 5; i += 1 {
    if i == 2 { continue }
    println("i = {}", i)
}
```

---

## Chapter 4: Functions & Call-Site Intent Safety

### 4.1 Function Declarations and Methods
Functions are declared with `fn`. Associated methods specify a receiver before the function name:

```cf
type Account struct {
    Owner:   str,
    Balance: i64,
}

// Function with regular parameters
fn calculate_fee(amount: i64) -> i64 {
    return amount / 100
}

// Read-only receiver method
fn (self: Account) display() {
    println("Account {}: ${}", self.Owner, self.Balance)
}

// Mutable receiver method: requires 'mut self'
fn (mut self: Account) deposit(amount: i64) {
    self.Balance += amount
}
```

### 4.2 Explicit Parameter Intents (`in`, `mut`, `move`)
In raw C++, calling `process(data)` hides whether `data` is read-only, modified in-place, or moved from. `cfire` enforces visibility at **both** declaration and call site:

| Intent | Declaration | Call Site | C++23 Lowering | Guarantee |
|---|---|---|---|---|
| **`in` (default)** | `x: T` | `f(x)` | `const T&` / `std::string_view` / `std::span` | Read-only borrow; zero copy overhead. |
| **`mut`** | `mut x: T` | `f(mut x)` | `T&` | In-place mutation; requires `mut` at call site. |
| **`move`** | `move x: T` | `f(move x)` | `T&&` + `std::move(x)` | Ownership transfer; callee consumes value. |

To return multiple values, `cfire` uses first-class tuples rather than mutable output parameters:
```cf
fn parse_coords(raw: str) -> (f64, f64)! {
    return (12.5, 45.2)
}

(lat, lon) := try parse_coords("12.5,45.2")
```

### 4.3 Semantic Safety Invariants

The `cfire` semantic analyzer verifies parameter intents at compile time:

#### 1. `cfire/MUT-CALLSITE`
Passing a variable to a `mut` parameter without the `mut` marker at the call site is rejected:
```cf
fn increment(mut val: i32) { val += 1 }

mut count := 0
// increment(count)     // ERROR: [cfire/MUT-CALLSITE] missing 'mut' keyword
increment(mut count)    // OK
```

#### 2. `cfire/IMMUTABLE-MUT-ARG`
Passing an immutable binding to a `mut` parameter is rejected:
```cf
val := 42
// increment(mut val)   // ERROR: [cfire/IMMUTABLE-MUT-ARG] 'val' is immutable
```

#### 3. `cfire/MUT-ALIAS` (The Law of Exclusivity)
A variable cannot be passed as a mutable argument while simultaneously aliased as another argument in the same call:
```cf
fn transfer(mut from: i64, mut to: i64, amount: i64) {
    from -= amount
    to += amount
}

mut account_a := 1000
// transfer(mut account_a, mut account_a, 100) // ERROR: [cfire/MUT-ALIAS]
mut account_b := 500
transfer(mut account_a, mut account_b, 100)    // OK: distinct bindings
```

#### 4. `cfire/USE-AFTER-MOVE`
Referencing a variable after it has been transferred via `move` is rejected:
```cf
fn sink(move data: []byte) { /* takes ownership */ }

mut payload: []byte = [1, 2, 3]
sink(move payload)
// println("len = {}", payload.len()) // ERROR: [cfire/USE-AFTER-MOVE]
```

### 4.4 Closures & Lambdas
Closures use pipe syntax `|params| -> ReturnType { ... }`:

```cf
items := [1, 2, 3, 4, 5]

// Inline closure passed to higher-order transform
multiplier := 10
transform := |x: i32| -> i32 {
    return x * multiplier
}

for item in items {
    println("transformed: {}", transform(item))
}
```

---

## Chapter 5: Structs, Compile-Time Tags & Algebraic Enums

### 5.1 Structs & Compile-Time Field Metadata
Structs define composite data records. Field tags (enclosed in backticks) provide zero-cost metadata inspected by the compiler during code generation:

```cf
type DatabaseConfig struct {
    Host:     str  `json:"host"     cli:"--host,-h"    sql:"host"`
    Port:     u16  `json:"port"     cli:"--port,-p"    sql:"port"`
    MaxConns: i32  `json:"max_conns" cli:"--max-conns" sql:"max_connections"`
    SSL:      bool `json:"ssl"      cli:"--ssl"        sql:"use_ssl"`
}
```

Because these tags are processed at compile time, `core:json`, `core:cli`, and `core:db/sqlite` generate direct C++ assignments without runtime reflection overhead:

```cf
import "core:json"
import "core:cli"

fn main() {
    // 1. Automatic CLI flag parsing directly into typed struct
    cfg := try cli.parse[DatabaseConfig]()
    println("Config loaded: host={}, port={}", cfg.Host, cfg.Port)

    // 2. Automatic JSON serialization
    json_str := try json.stringify(cfg)
    println("Serialized: {}", json_str)
}
```

### 5.2 Algebraic Enums (Tagged Unions)
Algebraic enums represent data that can take one of several distinct variants, each carrying its own typed payload:

```cf
type NetworkEvent enum {
    Connected(peer_ip: str, port: u16),
    DataReceived(payload: []byte),
    Disconnected(reason: str),
    Heartbeat,
}
```

This lowers to `std::variant<Connected, DataReceived, Disconnected, Heartbeat>` in ISO C++23.

### 5.3 Exhaustive Pattern Matching (`match`)
Pattern matching inspects algebraic enums and binds variant payloads. The compiler enforces exhaustiveness (`cfire/NON-EXHAUSTIVE-MATCH`):

```cf
fn handle_event(event: NetworkEvent) {
    match event {
        Connected(ip, port) => {
            println("Peer connected: {}:{}", ip, port)
        }
        DataReceived(data) => {
            println("Received payload of {} bytes", data.len())
        }
        Disconnected(reason) => {
            println("Connection closed: {}", reason)
        }
        Heartbeat => {
            // Keep-alive ack
        }
    }
}
```

If a new variant is added to `NetworkEvent`, every `match` statement across your codebase that lacks a wildcard `_ =>` arm will fail compilation at build time.

### 5.4 Slice Destructuring Matching
`match` can also destructure slices into head and tail elements:

```cf
fn summarize_tokens(tokens: []str) {
    match tokens {
        [] => {
            println("No tokens found")
        }
        [command] => {
            println("Single command: {}", command)
        }
        [cmd, ..args] => {
            println("Command: {}, Argument count: {}", cmd, args.len())
        }
    }
}
```

---

## Chapter 6: Fearless Error Handling & Cleanup

### 6.1 The Fallible Return Type `T!`
`cfire` rejects hidden C++ exceptions. Functions that can fail return `T!`, which lowers directly to `std::expected<T, cfire::Error>`. Errors are concrete value objects carrying a code, an error message, and source context:

```cf
import "core:fs"

fn load_api_key(path: str) -> str! {
    if !fs.exists(path) {
        return cfire.error("API key file not found: {}", path)
    }
    raw := try fs.read_text(path)
    return str.trim(raw)
}
```

### 6.2 The Three Ways to Handle Errors

#### 1. Early Return Propagation (`try`)
The `try` operator unwraps the success value or immediately returns the error from the enclosing function:
```cf
fn setup_system() -> void! {
    cfg := try load_api_key("api.key")
    println("Key loaded: {} chars", cfg.len())
}
```
If used inside `fn main()`, an unhandled error printed via `try` outputs a formatted diagnostic and exits the process with status code `1`.

#### 2. Default Fallbacks (`or value`)
When a reasonable default exists, use `or fallback` (lowers to `std::expected::value_or`):
```cf
port := try parse_port(os.getenv("PORT")) or 8080
println("Binding to port {}", port)
```

#### 3. Inline Recovery Blocks (`or err { ... }`)
When you need to log an error, clean up local state, or alter control flow, use an `or err { ... }` block. To prevent unwrapping an uninitialized value, the compiler enforces that value-binding recovery blocks must definitely diverge (`cfire/NON-DIVERGING-OR-BLOCK`):

```cf
token := load_api_key("secret.key") or err {
    println("[WARN] Failed to load key: {}. Falling back to sandbox mode.", err.message())
    return // Diverges cleanly
}
```

### 6.3 Deterministic Scope Cleanup (`defer`)
The `defer` statement registers an action to be executed in Last-In, First-Out (LIFO) order when the surrounding scope exits. It is implemented via RAII scope guards, guaranteeing execution regardless of whether the scope exits via normal return, `try` error propagation, or a `break` statement:

```cf
import "core:fs"
import "core:sync"

mut mtx: sync.Mutex = {}

fn process_transaction() -> void! {
    mtx.lock()
    defer mtx.unlock() // Guaranteed to unlock even on error return

    mut file := try fs.open("audit.log", fs.OpenMode::Append)
    defer file.close() // File descriptor closed cleanly

    try file.write_str("Transaction started\n")
    // If an error happens below, both file.close() and mtx.unlock() execute deterministically
    try execute_db_write()
    try file.write_str("Transaction committed\n")
}
```

---

## Chapter 7: Structured Concurrency & Contexts

### 7.1 Structured Concurrency with `async.scope`
Unlike raw threads or Go's unconstrained `go func()`, `cfire` uses structured task pools. An `async.scope` block defines a lexical boundary: the scope **cannot exit** until every spawned child task has completed:

```cf
import "core:time"

fn process_batch(items: []str) {
    // Spawns a pool of 4 worker threads
    async.scope(workers: 4) |scope| {
        for idx, item in items {
            scope.spawn || {
                println("Worker processing item [{}]: {}", idx, item)
                time.sleep_ms(50)
            }
        }
    }
    // All 4 worker tasks are 100% joined before this line executes!
    println("All batch items successfully processed.")
}
```

Structured concurrency eliminates orphaned background threads, use-after-free references to stack frames, and race conditions during application shutdown.

### 7.2 Message Passing via Typed Channels (`core:sync`)
For decoupled producer-consumer pipelines, `core:sync` provides Go-style typed channels:

```cf
import "core:sync"

fn main() {
    ch := sync.make_channel[i64](capacity: 10)

    async.scope(workers: 2) |scope| {
        // Producer task
        scope.spawn || {
            for mut i := 1; i <= 5; i += 1 {
                ch.send(i * 10)
            }
            ch.close()
        }

        // Consumer task
        scope.spawn || {
            for {
                val := ch.recv() or break // Exits loop when channel is closed and empty
                println("Received: {}", val)
            }
        }
    }
}
```

### 7.3 Cancellation Contexts & Deadlines (`core:sync/context`)
Long-running background operations support cooperative cancellation via `Context`:

```cf
import "core:sync/context"
import "core:time"

fn worker(ctx: context.Context, id: i32) {
    for {
        if ctx.is_cancelled() {
            println("Worker [{}] received cancellation notice", id)
            return
        }
        time.sleep_ms(100)
    }
}

fn main() {
    // Derive a context that cancels automatically after 250 milliseconds
    (ctx, cancel) := context.with_timeout(context.background(), 250)
    defer cancel()

    async.scope(workers: 2) |scope| {
        scope.spawn || { worker(ctx, 1) }
        scope.spawn || { worker(ctx, 2) }
    }
    println("Workers shut down cleanly.")
}
```

---

## Chapter 8: Zero-Friction C & C++ Interop

### 8.1 Direct C Header Ingestion
`cfire` compiles natively to C++23, which means communicating with C libraries requires no manual binding generation. When you import a C header with `import "c:..."`, the compiler automatically invokes Clang to extract type definitions and function prototypes, caching the result in `.cfire/cache/ast/`:

```cf
package main

// Directly imports the system zlib header
import "c:zlib.h" as zlib

fn main() {
    version_str := zlib.zlibVersion()
    println("Zlib library version: {}", version_str)
}
```

### 8.2 Automatic String Boundary Coercion
When calling a C function expecting a null-terminated `const char*`, `cfire` automatically inserts `cfire::c_str(arg)` at compile time. You can pass a native `cfire` `str` directly without handwritten conversion:

```cf
import "c:stdio.h" as c_io

fn main() {
    filename := "output.txt"
    mode := "w"
    
    // Automatically coerces 'filename' and 'mode' to const char*
    mut f := c_io.fopen(filename, mode)
    if f != null {
        c_io.fputs("Written via direct C interop\n", f)
        c_io.fclose(f)
    }
}
```

### 8.3 Direct C++ Header Imports
You can import modern C++ standard and third-party headers directly:

```cf
import "cpp:vector"
import "cpp:algorithm"

fn main() {
    // Direct access to C++ standard library algorithms
    mut values: []i32 = [5, 2, 8, 1, 9]
    // Under the hood, values is std::vector<int32_t>
    println("Sorted elements:")
    // ...
}
```

### 8.4 Inline C++ Escape Hatch (`cpp { ... }`)
For platform intrinsics, inline assembly, or specialized template metaprogramming, `cfire` allows raw C++ statements inside any function:

```cf
fn get_cpu_cycle_count() -> u64 {
    mut cycles: u64 = 0
    cpp {
        #if defined(__x86_64__)
        cycles = __rdtsc();
        #elif defined(__aarch64__)
        asm volatile("mrs %0, cntvct_el0" : "=r"(cycles));
        #endif
    }
    return cycles
}
```

---

## Chapter 9: The Cfire Build System: Native, Cross-Target & WebAssembly

A great systems language must solve the compilation, dependency, and packaging problem without dragging developers into build configuration nightmare. `cfire` features a unified build system that ranges from **instant zero-config single-command builds** to **declarative multi-language dependency manifests (`build.cf`)**, **WebAssembly compilation**, and **native desktop application bundling**.

### 9.1 Zero-Config Core & Incremental PCH Caching
For the majority of applications, `cfire` requires no project files, Makefiles, or build scripts. Point `cfire` at an entry file:

```zsh
cfire run main.cf          # Transpile to C++23, compile, and execute immediately
cfire build main.cf        # Compile an optimized native binary (-O2)
cfire build -o my_app main.cf
```

Under the hood:
1. **Semantic Analysis & Codegen**: `cfire` analyzes types, checks borrow invariants, and lowers `.cf` code to clean, human-readable ISO C++23.
2. **Automated Header & Library Discovery**: Any standard library (`core:*`) or vendor library (`vendor:*`) imported is automatically discovered and mapped to its appropriate compiler include paths and linker flags.
3. **Precompiled Header (PCH) Cache**: The runtime prelude (`cfire_prelude.hpp`) is automatically compiled into an optimized precompiled header (`.pch`) stored in your system temporary cache. When rebuilding, compilation completes in tens of milliseconds because Clang skips reparsing standard library headers.
4. **Memory Sanitizers (`--sanitize`)**: Add `--sanitize` to compile with LLVM AddressSanitizer (ASan) and UndefinedBehaviorSanitizer (UBSan), catching memory errors, buffer overflows, and null pointer dereferences at runtime.

### 9.2 Declarative `build.cf` Manifest
When your project integrates external C or C++ vendor libraries, legacy Makefiles, CMake sub-projects, or remote archives, place a `build.cf` file in your project root. `cfire` automatically detects `build.cf` whenever building or running any sibling `.cf` file.

```cf
package build

import "core:build"

// 1. Compile raw local C/C++ source trees
FastCRC := build.CC("./native/crc32", {
    Sources:  ["crc32.c", "table.c"],
    Includes: ["include", "."],
    Defines:  {"CRC_TABLE_SIZE": 256, "CRC_SEED": 42},
    CFlags:   ["-O3", "-march=native"],
})

// 2. Build a CMake-based vendor subproject into a static archive
KVStore := build.CMake("./vendor/libkv_cmake", {
    Target:   "kvstore",
    Includes: ["include"],
    Defines:  {"KV_SALT": 99},
})

// 3. Build a legacy Makefile or Autotools library
LegacyCodec := build.Make("./vendor/legacy_codec", {
    Target:   "libcodec.a",
    Includes: ["include"],
})

// 4. Fetch and compile a remote Git dependency pinned to a tag or commit
TreeSitter := build.Git("https://github.com/tree-sitter/tree-sitter.git", {
    Tag:      "v0.22.6",
    Sources:  ["lib/src/lib.c"],
    Includes: ["lib/include", "lib/src"],
})

// 5. Download and extract a remote source archive with SHA-256 integrity verification
LibUV := build.Archive("https://dist.libuv.org/dist/v1.48.0/libuv-v1.48.0.tar.gz", {
    Sha256:   "3a34f46ff36a2b8...221",
    Driver:   "CMake",
    Includes: ["include"],
})

// 6. Link system-installed pkg-config dependencies
Zstd := build.SystemLib("libzstd")
```

When you invoke `cfire build main.cf`:
1. `cfire` scans `build.cf` and resolves all dependencies.
2. Local and remote C/C++ sources are compiled into static `.a` archives located in `.cfire/cache/lib/`.
3. Build artifacts are tracked incrementally: unchanged dependencies are never recompiled.
4. All include paths and static archives are linked into your executable in a single pass.

### 9.3 Pre-Build Command Hooks & Code Generators
Modern systems often require code generators (protobuf, flatbuffers, sqlc) or asset packaging steps before compiling language sources. `build.cf` supports arbitrary pre-build commands:

```cf
package build

import "core:build"

// Run code generation before building .cf sources
GenProtobuf := build.Command("protoc", {
    Args:    ["--cpp_out=.cfire/gen", "messages.proto"],
    Inputs:  ["messages.proto"],
    Outputs: [".cfire/gen/messages.pb.h", ".cfire/gen/messages.pb.cc"],
})
```

`cfire` evaluates file timestamps: if `messages.proto` has not been modified since the outputs were produced, the command is skipped.

### 9.4 Cross-Compilation & Target Triples (`--target`)
`cfire` supports cross-compiling to alternative CPU architectures and operating systems using Clang target triples and sysroots:

```zsh
# Compile for macOS Apple Silicon from Intel (or vice-versa)
cfire build --target arm64-apple-darwin main.cf
cfire build --target x86_64-apple-darwin main.cf

# Cross-compile for Linux x86_64 or AArch64
cfire build --target x86_64-unknown-linux-gnu --sysroot /opt/linux-sysroot main.cf
cfire build --target aarch64-unknown-linux-gnu --sysroot /opt/linux-arm64-sysroot main.cf

# Cross-compile for Windows
cfire build --target x86_64-w64-mingw32 main.cf
```

### 9.5 WebAssembly Target & Web Profiles (`--target=wasm`)
`cfire` features first-class WebAssembly support by wrapping the Emscripten (`em++`) toolchain behind standard Cfire CLI flags:

```zsh
# Compile to WebAssembly (.wasm + .js + .html)
cfire build --target=wasm main.cf

# Compile, launch local HTTP server, and open default browser automatically
cfire run --target=wasm main.cf
```

#### Compilation Profiles (`--web-profile`)
1. **`cli` Profile** (Default for pure Cfire): Generates lean WebAssembly without canvas overhead. Can be executed directly with Node.js (`node app.js`) or embedded in serverless workers.
2. **`canvas` Profile** (Auto-detected if `vendor:raylib` or `vendor:raygui` is imported):
   - Injects `-DPLATFORM_WEB` and links against the WebAssembly build of Raylib.
   - Enables WebGL 2.0 and GLFW3 emulation (`-sUSE_GLFW=3`).
   - Automatically enables `-sASYNCIFY=1` so that synchronous desktop blocking loops (`while (!WindowShouldClose())`) yield control transparently to the browser's `requestAnimationFrame` without tab freezes.
   - Generates an interactive HTML5 harness with automatic high-DPI canvas scaling and audio context unlock.
3. **Manual Override**: Pass `--web-profile=cli` or `--web-profile=canvas` to explicitly force a profile.

### 9.6 Native Desktop GUI Packaging & No-Terminal Execution (`--subsystem`)
When compiling graphical desktop applications (such as Raylib or Raygui tools), double-clicking a binary should not spawn an unwanted background terminal window:

- **Windows (`-mwindows`)**: When `cfire` detects `vendor:raylib` or `vendor:raygui`, it automatically links with `-mwindows` (or `-Wl,/subsystem:windows`). Double-clicking the `.exe` launches the Raylib window directly without spawning a `cmd.exe` console window.
- **macOS (`.app` Bundle Packaging)**: On macOS, `cfire build` automatically structures the binary into a native Application Bundle:
  ```text
  MyApp.app/
  └── Contents/
      ├── Info.plist   (CFBundlePackageType: APPL, CFBundleExecutable: MyApp)
      └── MacOS/
          └── MyApp   (Mach-O binary)
  ```
  Double-clicking `MyApp.app` in Finder executes the application without launching `Terminal.app`.
- **Console Debugging Override (`--subsystem=console`)**: When debugging with `println()`, pass `--subsystem=console` to keep the console window active so stdout logs remain visible in the terminal.

### 9.7 Package Caching, Lockfiles & Offline Vendoring
To guarantee reproducible, hermetic builds across development machines and CI/CD runners:
- **Global Package Cache (`~/.cfire/cache/pkg`)**: Remote repositories and archives are fetched once and cached globally by cryptographic content hash.
- **Lockfile Pinning (`build.lock`)**: When dependencies are fetched, `cfire` records exact Git commit SHAs and archive checksums in `build.lock`. Subsequent builds verify that local archives match the recorded checksum.
- **Air-Gapped Offline Vendoring (`cfire vendor`)**: Run `cfire vendor [dest_dir]` to download and vendor all external C/C++ dependencies into a local `vendor/` directory. Your project can then be built completely offline without network access.

### 9.8 Zero-Lock-In CMake Ejection (`cfire eject`)
`cfire` guarantees **zero framework lock-in**. You can export your entire project to standard ISO C++23 with a production CMake build file at any time:

```zsh
cfire eject main.cf ./standalone_cpp_project
```

This generates:
- Standalone C++23 translation units.
- The zero-overhead `cfire_prelude.hpp` runtime header.
- A clean `CMakeLists.txt` configuring all include directories, dependencies, and compile options.

Any developer can immediately clone and build the ejected project using standard CMake and Clang without installing `cfire`.

---

## Chapter 10: Building Web Services, HTTP Servers & Independent Routers (`core:net/http`)

Modern systems software frequently needs to expose telemetry endpoints, REST APIs, or lightweight RPC microservices. Unlike traditional frameworks that force heavy external dependencies or invasive event-loop architectures, `cfire` provides a zero-dependency, native HTTP/1.1 protocol engine and independent router built directly on top of POSIX TCP sockets and ISO C++23.

### 10.1 Philosophy & Zero External Dependencies
The `core:net/http` module is designed with two fundamental principles:
1. **Zero Third-Party Libraries:** The HTTP wire parser, response serializer, server accept loop, and route matcher are implemented in standard ISO C++23 without requiring `libmicrohttpd`, `boost::beast`, or external C libraries.
2. **Decoupled & Independent Features:** Routing and dispatch are strictly decoupled from network sockets. You can create an independent `Router`, register parameterized routes, and unit-test request dispatching purely in memory without opening sockets or listening on network ports.

### 10.2 Request Parsing & Response Writing
The `http.Request` struct represents an incoming HTTP/1.1 request:
- `req.method`: Normalized uppercase HTTP verb (`"GET"`, `"POST"`, `"PUT"`, `"DELETE"`, etc.).
- `req.path`: Clean decoded URI path (e.g. `"/api/users/42"`).
- `req.query(key, fallback)`: Decoded query parameters (e.g. `req.query("limit", "10")`).
- `req.header(name, fallback)`: Case-insensitive header lookup (e.g. `req.header("Authorization")`).
- `req.param(name, fallback)`: Named path parameters extracted by the router (e.g. `req.param("id")`).
- `req.body`: Complete payload body.

The `http.Response` struct provides fluent static constructors for standard HTTP responses:
```cf
// 200 OK with plain text
res1 := http.Response.text(200, "operation successful")

// 200 OK with JSON payload
res2 := http.Response.json(200, "{\"status\":\"ok\"}")

// 201 Created with JSON
res3 := http.Response.json(201, "{\"id\":42}")

// 302 Found redirect
res4 := http.Response.redirect("/login")

// Standard error responses
res5 := http.Response.not_found("user not found")
res6 := http.Response.bad_request("invalid payload")
```

### 10.3 The Independent HTTP Request Router
The router matches incoming requests based on path patterns and HTTP verbs:
- **Exact Path Matching:** `/health`, `/api/v1/metrics`
- **Named Path Parameters (`:param`):** `/users/:id` matches `/users/42` with `req.param("id") == "42"`.
- **Greedy Suffix Wildcards (`*filepath`):** `/static/*filepath` matches `/static/css/theme.css` with `req.param("filepath") == "css/theme.css"`.
- **Method Filtering:** `router.get`, `router.post`, `router.put`, `router.delete_`, `router.patch`, `router.all`.
- **RFC-Compliant Status Handlers:** Automatically returns `405 Method Not Allowed` with an `Allow` header if the path matches a route registered for different verbs, or `404 Not Found` if unmatched.
- **Middleware Pipeline:** `router.use(middleware)` allows adding pre-routing authentication, audit logging, or header validation.

### 10.4 In-Memory Standalone Unit Testing
Because the `Router` is completely independent of network sockets, request handlers can be tested in-memory in pure unit tests without flakiness or port collisions:

```cf
package main

import "core:net/http"

fn main() {
    mut router := http.make_router()
    router.get("/users/:id", |req| {
        return http.Response.text(200, "User ID: " + req.param("id"))
    })

    // Pure in-memory unit test — NO network sockets opened!
    req := http.Request{ method: "GET", path: "/users/100" }
    res := router.dispatch(req)
    assert(res.status_code == 200)
    assert(res.body == "User ID: 100")
}
```

### 10.5 Running the Live HTTP Server
When you are ready to serve live network traffic, instantiate `http.make_server` and pass the router directly to `server.serve_in_background(router)` or `server.listen_and_serve(router)`:

```cf
package main

import "core:net/http"
import "core:json"

type Item struct {
    id: i64 `json:"id"`
    name: str `json:"name"`
}

fn main() {
    mut router := http.make_router()

    router.get("/health", |req| {
        return http.Response.json(200, "{\"status\":\"healthy\"}")
    })

    router.get("/items/:id", |req| {
        return http.Response.text(200, "Lookup id=" + req.param("id"))
    })

    router.post("/items", |req| {
        return http.Response.json(201, "{\"created\":true,\"data\":" + req.body + "}")
    })

    // Start server on port 8080 (or 0 for an ephemeral port)
    mut server := try http.make_server(port: 8080)
    println("Server listening on http://127.0.0.1:{}", server.port())

    // Run non-blocking background event loop
    server.serve_in_background(router)

    // Execute client requests
    resp := try http.get("http://127.0.0.1:8080/health")
    println("GET /health -> status: {}, body: {}", resp.status_code, resp.body)

    // Gracefully stop the server when done
    server.stop()
}
```

---

## Chapter 11: 2D/3D Graphics & GUI Applications with Raylib and Raygui

Modern systems software often requires interactive visualization, hardware-accelerated 2D/3D graphics, or desktop UI controls for internal tooling, game engines, and scientific visualization. `cfire` provides first-class, zero-friction integration with **Raylib 5.0** (`vendor:raylib`) and **Raygui 4.0** (`vendor:raygui`).

### 11.1 Zero-Setup Import & Linking
Simply importing `vendor:raylib` or `vendor:raygui` triggers automatic header ingestion, include path resolution, struct lowering, and linking across macOS (`Cocoa`, `IOKit`, `OpenGL`), Linux (`X11`, `GL`), Windows, and WebAssembly:

```cf
package main

import "vendor:raylib" as rl
import "vendor:raygui" as rg
```

### 11.2 Windowing & The Render Loop
Raylib follows a simple, deterministic frame-by-frame polling loop:

```cf
package main

import "vendor:raylib" as rl

fn main() {
    // 1. Initialize window and OpenGL context (width, height, title)
    rl.InitWindow(800, 450, "cfire + Raylib")
    defer rl.CloseWindow() // Closes window when main returns

    // 2. Lock framerate to display refresh rate
    rl.SetTargetFPS(60)

    // 3. Main render loop
    for !rl.WindowShouldClose() {
        dt := rl.GetFrameTime() // Delta time in seconds

        // Frame rendering with double buffering
        rl.BeginDrawing()
        defer rl.EndDrawing()

        rl.ClearBackground(rl.RAYWHITE)
        rl.DrawText("Hello from Native cfire Raylib!", 180, 200, 28, rl.DARKGRAY)
    }
}
```

### 11.3 2D Shapes, Vectors & Camera Systems
`cfire` automatically lowers struct literals (`rl.Vector2`, `rl.Rectangle`, `rl.Color`, `rl.Camera2D`) to their native C99 Raylib types with zero memory overhead:

```cf
package main

import "vendor:raylib" as rl

fn main() {
    rl.InitWindow(800, 450, "Shapes & Camera")
    defer rl.CloseWindow()

    mut player_pos := rl.Vector2{x: 400.0, y: 225.0}
    speed := 250.0

    cam := rl.Camera2D{
        offset: rl.Vector2{x: 400.0, y: 225.0},
        target: player_pos,
        rotation: 0.0,
        zoom: 1.0,
    }

    rl.SetTargetFPS(60)
    for !rl.WindowShouldClose() {
        dt := rl.GetFrameTime()

        // Input polling
        if rl.IsKeyDown(rl.KEY_RIGHT) { player_pos.x += speed * dt }
        if rl.IsKeyDown(rl.KEY_LEFT)  { player_pos.x -= speed * dt }
        if rl.IsKeyDown(rl.KEY_UP)    { player_pos.y -= speed * dt }
        if rl.IsKeyDown(rl.KEY_DOWN)  { player_pos.y += speed * dt }

        rl.BeginDrawing()
        defer rl.EndDrawing()

        rl.ClearBackground(rl.RAYWHITE)

        // Draw within 2D camera view
        rl.BeginMode2D(cam)
        rl.DrawRectangleRec(rl.Rectangle{x: 100.0, y: 100.0, width: 80.0, height: 80.0}, rl.SKYBLUE)
        rl.DrawCircleV(player_pos, 20.0, rl.MAROON)
        rl.EndMode2D()

        rl.DrawText("Use Arrow Keys to Move", 20, 20, 20, rl.DARKGRAY)
    }
}
```

### 11.4 Raymath Linear Algebra Primitives
Raymath functions (`Vector2Add`, `Vector2Scale`, `Vector2Distance`, `Clamp`) are included out of the box under the `rl` namespace:

```cf
mut velocity := rl.Vector2{x: 10.0, y: 5.0}
step := rl.Vector2Scale(velocity, dt)
player_pos = rl.Vector2Add(player_pos, step)
player_pos.x = rl.Clamp(player_pos.x, 0.0, 800.0)
```

### 11.5 Immediate-Mode GUI Controls (`vendor:raygui`)
`vendor:raygui` provides clean, high-performance immediate-mode UI widgets designed for tools, debug overlays, and game menus:

```cf
package main

import "vendor:raylib" as rl
import "vendor:raygui" as rg

fn main() {
    rl.InitWindow(640, 480, "Raygui Desktop Controls")
    defer rl.CloseWindow()

    mut vsync := true
    mut volume: f32 = 70.0
    mut user_name := "PlayerOne"
    mut text_edit_mode := false
    mut clicks := 0

    rl.SetTargetFPS(60)
    for !rl.WindowShouldClose() {
        rl.BeginDrawing()
        defer rl.EndDrawing()

        rl.ClearBackground(rl.RAYWHITE)

        // Window Box Container
        rg.GuiWindowBox(rl.Rectangle{x: 40.0, y: 30.0, width: 340.0, height: 320.0}, "Audio & Display Settings")

        // Interactive CheckBox (mutates `vsync` in-place)
        rg.GuiCheckBox(rl.Rectangle{x: 60.0, y: 80.0, width: 20.0, height: 20.0}, "Enable VSync", &mut vsync)

        // Slider Bar (mutates `volume` within [0.0, 100.0])
        rg.GuiSliderBar(rl.Rectangle{x: 120.0, y: 120.0, width: 160.0, height: 20.0}, "Volume", "{volume}", &mut volume, 0.0, 100.0)

        // Editable Text Box safely backed by a `mut str`
        if rg.GuiTextBox(rl.Rectangle{x: 60.0, y: 160.0, width: 220.0, height: 28.0}, &mut user_name, 32, text_edit_mode) != 0 {
            text_edit_mode = !text_edit_mode
        }

        // Action Button
        if rg.GuiButton(rl.Rectangle{x: 60.0, y: 210.0, width: 120.0, height: 32.0}, "Save Settings") != 0 {
            clicks += 1
        }

        rl.DrawText("Saved {clicks} times", 200, 218, 16, rl.DARKGRAY)
    }
}
```

### 11.6 Multi-Platform Deployment: Desktop & WebAssembly
The same `.cf` application runs natively across desktop operating systems and modern web browsers without modifying a single line of code:

- **Run on Desktop**:
  ```zsh
  cfire run app.cf
  cfire build app.cf   # macOS: produces MyApp.app; Windows: produces windowless .exe (-mwindows)
  ```
- **Run in the Web Browser**:
  ```zsh
  cfire run --target=wasm app.cf
  ```
  `cfire` compiles to WebAssembly using Emscripten, activates WebGL 2.0 and `-sASYNCIFY=1`, starts a local HTTP server, and opens your browser directly to the interactive Canvas viewport.


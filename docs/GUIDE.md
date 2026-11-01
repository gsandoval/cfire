# The `cfire` Language & Systems Programming Guide

Welcome to **cfire** (pronounced *sapphire* • `/ˈsæf.aɪ.ər/`). This comprehensive engineering guide takes you from platform installation and toolchain setup to authoring high-performance systems services, hardware-accelerated 2D/3D graphics, real-time audio DSP, native iOS applications, and AI-native agent workflows.

Every section follows a **Progressive Disclosure** philosophy:
- **Level 1 — Fundamentals**: A concise, runnable 5–10 line example introducing syntax and primitives.
- **Level 2 — Idiomatic Safety & Composition**: Combining error handling, resource cleanup (`defer`), and type safety.
- **Level 3 — Production Architecture & Lowering**: Real-world services alongside the exact, human-readable ISO C++23 emitted by the compiler with zero hidden runtime overhead.

---

# Part I: Getting Started & Tooling

## Chapter 1: Installation & Platform Setup

### Overview & System Requirements
cfire is a single self-contained binary written in modern C++23. It transpiles `.cf` code directly into clean standard ISO C++23 and invokes an underlying C++23-capable backend compiler (LLVM Clang 18+ or GCC 14+).

> [!NOTE]
> **Zero Runtime Dependencies**: Executables produced by `cfire build` are native ELF, Mach-O, or PE binaries. They do not depend on a garbage collector, virtual machine, or external runtime libraries.

### Quick Installation Script (macOS & Linux)
To install the latest stable `cfire` release and toolchain into `~/.cfire/bin`:

```bash
curl -fsSL https://cfire.memo.engineer/install.sh | sh
```

Verify your installation:
```bash
cfire version
# Output: cfire 0.9.4 (clang-18 target x86_64-apple-darwin23 / arm64)
```

### Manual Installation & Platform Toolchains

#### macOS (Apple Silicon & Intel)
1. Install Xcode Command Line Tools (provides Apple Clang and macOS SDKs):
   ```bash
   xcode-select --install
   ```
2. Install dependencies via Homebrew:
   ```bash
   brew install llvm simdjson zstd sqlite3
   ```
3. Build `cfire` from source:
   ```bash
   git clone https://github.com/gsandoval/cfire.git
   cd cfire && make -j
   sudo make install  # Installs to /usr/local/bin/cfire
   ```

#### Linux (Ubuntu / Debian / Fedora / Arch)
- **Ubuntu 24.04+ / Debian 12+**:
   ```bash
   sudo apt-get update
   sudo apt-get install -y clang-18 libclang-18-dev libsimdjson-dev libzstd-dev libsqlite3-dev libasound2-dev
   cd cfire && make -j
   ```
- **Arch Linux**:
   ```bash
   sudo pacman -S clang llvm simdjson zstd sqlite alsa-lib
   cd cfire && make -j
   ```

#### Windows (WSL2)
cfire supports Windows through WSL2 (Windows Subsystem for Linux) with Ubuntu:
```bash
wsl --install -d Ubuntu-24.04
wsl
# Inside WSL Ubuntu shell:
sudo apt-get update && sudo apt-get install -y clang-18 libsimdjson-dev libzstd-dev libsqlite3-dev
```

### Cross-Compilation Target Setup

#### WebAssembly (`--web` / `--target=wasm`)
cfire compiles any project (including Raylib graphics and audio) directly to WebAssembly and WebGL canvas using Emscripten:
```bash
# Install Emscripten SDK:
git clone https://github.com/emscripten-core/emsdk.git
cd emsdk && ./emsdk install latest && ./emsdk activate latest
source ./emsdk_env.sh
```

#### Native iOS Simulator & Device (`--ios`)
cfire builds native iOS applications (`UIKit` + `OpenGLES3` + `Metal`) without Xcode projects or Storyboards. All that is required is macOS with Xcode installed:
```bash
xcode-select -p  # Verifies Developer directory is active
xcrun --sdk iphonesimulator --show-sdk-path
```

### AI Agent & IDE Configuration

#### Model Context Protocol (`cfire mcp`)
For AI coding assistants (Claude Code, Cursor, Antigravity), cfire includes a native Model Context Protocol server that exposes AST inspection, semantic validation, and documentation tools:
```bash
cfire mcp
# Or configure in ~/.cursor/mcp.json or Claude Desktop config:
# { "command": "cfire", "args": ["mcp"] }
```

#### Editor Syntax & LSP Setup (`cfire ide`)
Configure auto-completion, hover docs, and diagnostics across your preferred editor:
```bash
cfire ide --all      # Configures VS Code, Cursor, Zed, and Neovim
cfire lsp            # Starts standard Language Server Protocol daemon on stdio
```

---

## Chapter 2: 5-Minute Quickstart & Tour of the Toolchain

### Level 1 — Instant Evaluation with `cfire eval`
Test arithmetic expressions or small statements without creating files:

```bash
cfire eval -e "2.0 * math.pi * 10.0"
# Output: 62.83185307179586

cfire eval "println(\"Hello, deterministic C++23!\")"
# Output: Hello, deterministic C++23!
```

### Level 2 — Your First Script (`hello.cf`)
Create a single-file program with standard imports:

```cfire
package main

import "core:str"
import "core:time"

fn main() {
    now := time.now()
    greeting := str.format("Welcome to cfire at {}", now.unix_sec)
    println(greeting)
}
```

Run directly with JIT-like turnaround via precompiled headers:
```bash
cfire run hello.cf
```

### Level 3 — Production Build, Sanitizers & C++23 Ejection

Compile an optimized standalone native binary:
```bash
cfire build hello.cf -o hello
./hello
```

Run with AddressSanitizer and UndefinedBehaviorSanitizer enabled:
```bash
cfire run --sanitize hello.cf
```

Inspect the exact ISO C++23 code generated by the compiler:
```bash
cfire emit-cpp hello.cf
```

Eject a self-contained, standalone CMake project with zero cfire dependencies:
```bash
cfire eject hello.cf --out-dir dist/
# Emits dist/CMakeLists.txt, dist/main.cpp, and runtime headers
```

---

## Chapter 3: Project Layout, CLI & Build System (`build.cf`)

### Complete CLI Command Reference
The unified `cfire` executable provides all engineering tooling:

| Command | Purpose |
| :--- | :--- |
| `cfire run <file.cf> [args...]` | Compiles and executes in temporary cache directory |
| `cfire run --sanitize <file.cf>` | Executes with ASan and UBSan instrumentation |
| `cfire build <file.cf> -o <bin>` | Compiles an optimized native binary |
| `cfire build --web <file.cf> -o <app.html>` | Compiles to WebAssembly + WebGL Canvas |
| `cfire run --ios <file.cf>` | Compiles and launches in the iOS Simulator |
| `cfire eval -e "<expr>"` | Evaluates one-shot expression |
| `cfire check <file.cf>` | Performs syntax and semantic type verification |
| `cfire test [path]` | Discovers and executes all `test "..."` blocks |
| `cfire test --sanitize [path]` | Executes tests under ASan/UBSan memory checks |
| `cfire fmt [--check] [path]` | Formats source files according to language rules |
| `cfire doc [--check]` | Generates or audits documentation HTML & markdown |
| `cfire lsp` | Starts the Language Server Protocol daemon |
| `cfire mcp` | Starts the Model Context Protocol server for AI agents |
| `cfire ide --all` | Installs syntax & LSP settings for VS Code, Zed, Neovim |
| `cfire clean` | Cleans `.cfire/` build and PCH cache |

### Declarative Project Configuration (`build.cf`)
In multi-file projects, `build.cf` coordinates executables, static libraries, and external build systems:

```cfire
package build

fn configure(mut b: Build) {
    // 1. Native CLI executable with C/C++ compilation and archive steps
    CSupport := build.CC("vendor/fast_hash.c", flags: ["-O3"])
    ArchiveLib := build.Archive([CSupport], output: "lib/libfast_hash.a")
    KVStore := build.CMake("third_party/kvstore", options: ["-DBUILD_SHARED_LIBS=OFF"])
    LegacyMake := build.Make("third_party/legacy_math", target: "libmath.a")
    GitDep := build.Git("https://github.com/example/engine", tag: "v1.2.0")
    CustomGen := build.Command("python3", args: ["scripts/gen_tables.py"])

    exe := b.add_executable("service", "src/main.cf")
    exe.link_system("zstd")
    exe.link_system("sqlite3")
    exe.link_library(ArchiveLib)
    exe.link_library(KVStore)

    // 2. WebAssembly deployment (--target=wasm)
    web_app := b.add_executable("web_viewer", "apps/viewer/main.cf")
    web_app.set_target("wasm")
    web_app.set_output("web/viewer.html")

    // 3. Native iOS application
    ios_app := b.add_executable("mobile_app", "apps/mobile/main.cf")
    ios_app.set_target("ios-sim")
}
```

### Precompiled Header (PCH) Acceleration
cfire maintains an automated precompiled header cache in `.cfire/cache/pch/`. Standard library headers (`<vector>`, `<string>`, `<expected>`, `<variant>`, `<format>`, `<chrono>`, `<simdjson.h>`) are compiled once per toolchain update. Incremental rebuilds complete in under 15 milliseconds.

---

# Part II: Core Language & Intent Safety

## Chapter 4: Syntax, Primitives, Variables & Control Flow

### Overview: Modern Ergonomics Lowered to C++23
cfire pairs clean syntax with deterministic systems semantics:
- Variables are **immutable by default**. Mutable state requires explicit `mut`.
- Memory is managed via RAII and value semantics. There is no garbage collector.
- Primitive types have fixed, deterministic bit widths.

### Level 1 — Variables & Primitive Types
```cfire
fn main() {
    // Immutable variable bindings
    count := 42
    ratio := 3.14159
    enabled := true
    label := "cfire systems"

    // Explicit mutable variable
    mut accumulator := 0
    accumulator += count
    println("Total: {}", accumulator)
}
```

Primitive bit-width types:
- Integers: `i8`, `i16`, `i32`, `i64`, `u8`, `u16`, `u32`, `u64`, `usize`
- Floats: `f32`, `f64`
- Booleans: `bool` (`true`, `false`)
- Bytes and Text: `byte`, `str` (UTF-8 immutable view)

### Level 2 — Composite Data Structures
```cfire
import "core:collections"

fn main() {
    // Slices ([]T) - Dynamic views backed by std::vector
    mut scores: []i32
    scores.push(98)
    scores.push(84)
    scores.push(91)

    // Fixed arrays ([N]T) - Stack-allocated std::array
    weights := [3]f64{ 1.0, 2.5, 0.75 }

    // Slicing operations
    sub := scores[0..2]
    println("Sub-scores len: {}", sub.len)

    // Hash Maps - cfire::Map[K, V]
    mut env: Map[str, str]
    env.set("PORT", "8080")
    env.set("HOST", "127.0.0.1")
    if env.contains("PORT") {
        println("Listening on port {}", env.get("PORT"))
    }
}
```

### Level 3 — Pointer Semantics & Lowering
cfire provides clear pointer types:
- `&T`: Immutable borrow (`const T&`)
- `&mut T`: Mutable borrow (`T&`)
- `*T`: Optional raw pointer (`const T*`)
- `^T`: Owned heap handle (`std::unique_ptr<T>`)

```cfire
type Node struct {
    id: i64
    next: ^Node? // Unique ownership
}

fn append_child(mut parent: Node, child_id: i64) {
    parent.next = make_own(Node{ id: child_id, next: nil })
}
```

---

## Chapter 5: Functions, Call-Site Intent Safety & Memory Semantics

### Overview: Call-Site Exclusivity
In traditional C and C++, reviewing call sites like `update(record)` obscures whether `record` is read-only or mutated in-place. cfire enforces **Call-Site Mutability Visibility**: if a parameter is declared `mut`, callers must explicitly write `mut` at the call site.

### Level 1 — Function Signatures & Multiple Returns
```cfire
fn compute_bounds(width: f64, height: f64) -> (f64, f64) {
    area := width * height
    perimeter := 2.0 * (width + height)
    return (area, perimeter)
}

fn main() {
    (a, p) := compute_bounds(10.0, 5.0)
    println("Area: {}, Perimeter: {}", a, p)
}
```

### Level 2 — Parameter Intent (`in`, `mut`, `move`)
```cfire
type Config struct {
    timeout_ms: i32
    retries: i32
}

// 'in' intent: read-only borrow (const T&)
fn validate(in cfg: Config) -> bool {
    return cfg.timeout_ms > 0 && cfg.retries >= 0
}

// 'mut' intent: in-place modification (T&)
fn apply_defaults(mut cfg: Config) {
    if cfg.timeout_ms <= 0 { cfg.timeout_ms = 5000 }
}

fn main() {
    mut c := Config{ timeout_ms: 0, retries: 3 }
    apply_defaults(mut c) // Mandatory 'mut' marker at call site!
    println("Timeout: {}", c.timeout_ms)
}
```

### Level 3 — Compile-Time Borrow & Escape Analysis
cfire's semantic analyzer checks for dangling stack references:
- Returning references to local variables triggers diagnostic `CF3002`.
- Passing the same mutable variable twice (`update(mut x, mut x)`) triggers `cfire/MUT-ALIAS`.

---

## Chapter 6: Structs, Compile-Time Tags, Algebraic Enums & Concepts

### Level 1 — Structs with Default Field Values
```cfire
type Endpoint struct {
    host: str = "127.0.0.1"
    port: u16 = 8080
    ssl: bool = false
}

fn main() {
    ep1 := Endpoint{}
    ep2 := Endpoint{ host: "api.example.com", port: 443, ssl: true }
    println("Connect to {}:{}", ep2.host, ep2.port)
}
```

### Level 2 — Compile-Time Struct Reflection Tags
Tags inform standard library modules (JSON, SQLite, CLI) with zero runtime reflection:

```cfire
type UserRecord struct {
    Id: i64    `json:"id"    sql:"id"    desc:"Unique user identifier"`
    Email: str `json:"email" sql:"email" desc:"Verified email address"`
    Active: bool `json:"active" sql:"active" desc:"Account status"`
}
```

### Level 3 — Algebraic Enums & Exhaustive Pattern Matching
Enums can hold data payloads, lowering directly to `std::variant`:

```cfire
enum Event {
    Connected(str)
    Data([]byte)
    Disconnected(i32)
}

fn handle_event(e: Event) {
    match e {
        Event::Connected(addr) => {
            println("Peer connected from {}", addr)
        }
        Event::Data(payload) => {
            println("Received {} bytes", payload.len)
        }
        Event::Disconnected(code) => {
            println("Connection closed with code {}", code)
        }
    }
}
```

---

## Chapter 7: Fearless Error Handling, Optionals & RAII Cleanup

### Level 1 — Fallible Return Types (`T!`) and `try`
Functions returning `T!` produce a value or propagate an error without exceptions:

```cfire
import "core:fs"

fn read_version_number(path: str) -> i32! {
    content := try fs.read_text(path)
    return 1
}
```

### Level 2 — Fallback Divergence & Optional Unwrapping
```cfire
import "core:os"

fn main() {
    // Provide inline fallback default with 'or'
    port := os.getenv("PORT") or "3000"

    // Optional types (T?)
    mut user_id: i64? = nil
    if user_id != nil {
        println("User: {}", user_id?)
    }
}
```

### Level 3 — Deterministic RAII Cleanup (`defer`)
`defer` statements execute in reverse order (LIFO) whenever a scope exits, whether by normal return or error propagation:

```cfire
import "core:fs"
import "core:db/sqlite"

fn export_dataset(db_path: str, out_path: str) -> void! {
    mut db := try sqlite.open(db_path)
    defer db.close()

    mut out := try fs.open(out_path, fs.OpenMode::Write)
    defer out.close()

    // Both handles are guaranteed to close on return or error!
    try out.write_str("Export completed successfully.\n")
}
```

---

# Part III: Concurrency, C++ Interop & Systems

## Chapter 8: Structured Concurrency, Channels & Contexts

### Level 1 — Scoped Concurrency
`async.scope` creates a lexically bounded thread pool. The scope cannot exit until all spawned tasks complete:

```cfire
fn main() {
    async.scope(workers: 4) |scope| {
        scope.spawn || {
            println("Task A executing")
        }
        scope.spawn || {
            println("Task B executing")
        }
    }
    println("All tasks completed.")
}
```

### Level 2 — Bounded MPMC Channels (`core:sync`)
```cfire
import "core:sync"

fn main() {
    ch := sync.make_channel[i64](capacity: 10)

    async.scope(workers: 2) |scope| {
        // Producer
        scope.spawn || {
            for i in 1..=5 {
                ch.send(i)
            }
            ch.close()
        }

        // Consumer
        scope.spawn || {
            for val in ch {
                println("Received item: {}", val)
            }
        }
    }
}
```

### Level 3 — Cancellation Tokens & Deadlines (`core:sync/context`)
```cfire
import "core:sync/context"
import "core:time"

fn worker(ctx: context.Context) {
    for !ctx.is_cancelled() {
        time.sleep_ms(50)
    }
    println("Worker cleanly cancelled.")
}
```

---

## Chapter 9: Zero-Friction C & C++ Interop & CMake Orchestration

### Level 1 — Inline C++ Escape Blocks
Embed inline C++23 directly within `.cf` source:

```cfire
fn get_system_arch() -> str {
    cpp {
        #if defined(__aarch64__)
            return "arm64";
        #else
            return "x86_64";
        #endif
    }
}
```

### Level 2 — Direct Clang JSON AST Header Ingestion
Import existing C and C++ library headers with automatic type conversion:

```cfire
import cpp "zstd.h"

fn get_zstd_version() -> u32 {
    return cpp.ZSTD_versionNumber()
}
```

### Level 3 — Multi-Build Orchestration in `build.cf`
Orchestrate legacy CMake, Make, and C archives without shell scripts:

```cfire
package build

fn configure(mut b: Build) {
    // Ingest existing CMake dependency
    cmake_lib := b.add_cmake("vendor/sqlite", "libsqlite3.a")
    
    app := b.add_executable("server", "src/main.cf")
    app.link_artifact(cmake_lib)
}
```

---

## Chapter 10: Integrated Testing, Sanitizers & Diagnostics

### Level 1 — Built-In Unit Tests
Write `test "..."` blocks directly alongside implementation code:

```cfire
fn add(a: i32, b: i32) -> i32 { return a + b }

test "addition properties" {
    assert(add(2, 2) == 4, "2 + 2 must equal 4")
    assert(add(-1, 1) == 0, "Inverse sum must equal 0")
}
```

Run tests with `cfire test`.

### Level 2 — Memory Verification under Sanitizers
Run test suites with hardware memory safety checks:
```bash
cfire test --sanitize
# Runs AddressSanitizer (ASan) + UndefinedBehaviorSanitizer (UBSan)
```

### Level 3 — Diagnostic Error Codes
Every semantic compiler check carries an actionable code:
- `CF1xxx`: Lexical tokenization errors
- `CF2xxx`: Grammar parsing and syntax errors
- `CF3xxx`: Semantic types, borrow analysis, and call-site intent checks
- `CF4xxx`: Code generation and C++23 lowering diagnostics
- `CF5xxx`: Build system and linker errors

---

# Part IV: Batteries-Included AI, Data, Networking & Multi-Target

## Chapter 11: Web Services, HTTP/SSE, WebSockets & TLS Networking

### Level 1 — HTTP Client Requests
```cfire
import "core:net/http"

fn main() {
    res := try http.get("https://api.github.com/zen")
    println("Status: {}, Body: {}", res.status_code, res.text)
}
```

### Level 2 — HTTP Server with Server-Sent Events (SSE)
```cfire
import "core:net/http"

fn main() {
    mut router := http.make_router()
    router.get("/health") |req| {
        return http.Response.ok("OK\n")
    }
    router.get("/stream") |req| {
        return http.post_sse(req, |mut sse| {
            sse.send_event("ping", "{}")
        })
    }
    server := try http.make_server("0.0.0.0", 8080, router)
    println("Server running on http://127.0.0.1:8080")
    server.listen()
}
```

### Level 3 — RFC 6455 WebSockets & TLS 1.3
```cfire
import "core:net/websocket"

fn main() {
    mut ws := try websocket.connect("wss://echo.websocket.org")
    defer ws.close()
    try ws.send_text("Hello over TLS 1.3 WebSocket!")
    msg := try ws.read_text()
    println("Echo received: {}", msg)
}
```

---

## Chapter 12: Serialization, SQLite WAL, Vector Search & Zstandard Compression

### Level 1 — Compile-Time SIMD JSON
```cfire
import "core:json"

type Metric struct {
    Name: str `json:"name"`
    Value: f64 `json:"value"`
}

fn main() {
    raw := "{\"name\": \"cpu\", \"value\": 92.5}"
    m := try json.parse[Metric](raw)
    println("Metric: {} = {:.1f}", m.Name, m.Value)
}
```

### Level 2 — Embedded SQLite with Vector Search
```cfire
import "core:db/sqlite"
import "core:ai"

fn main() {
    mut db := try sqlite.open(":memory:")
    try db.exec("CREATE TABLE docs (id INT, text TEXT, vec BLOB)")

    v := try ai.embed("local", "Systems programming with cfire")
    try db.exec("INSERT INTO docs VALUES (1, 'cfire intro', ?1)", v)
}
```

### Level 3 — Zstandard Frame Compression (`core:compress/zstd`)
```cfire
import "core:compress/zstd"

fn main() {
    data := "Highly repetitive systems log payload data..."
    compressed := try zstd.compress(data, level: 3)
    decompressed := try zstd.decompress(compressed)
    println("Ratio: {} -> {} bytes", data.len, compressed.len)
}
```

---

## Chapter 13: 2D/3D Graphics & Native GUI Applications

### Level 1 — Hardware-Accelerated 2D Rendering
```cfire
import "vendor:raylib" as rl

fn main() {
    rl.InitWindow(800, 450, "cfire graphics")
    defer rl.CloseWindow()
    rl.SetTargetFPS(60)

    for !rl.WindowShouldClose() {
        rl.BeginDrawing()
        rl.ClearBackground(rl.DARKGRAY)
        rl.DrawCircle(400, 225, 50.0, rl.GOLD)
        rl.DrawText("Hardware 60 FPS", 20, 20, 20, rl.RAYWHITE)
        rl.EndDrawing()
    }
}
```

### Level 2 — Immediate-Mode Controls (`vendor:raygui`)
```cfire
import "vendor:raylib" as rl
import "vendor:raygui" as rg

fn main() {
    rl.InitWindow(640, 360, "GUI Controls")
    defer rl.CloseWindow()
    mut toggle := false

    for !rl.WindowShouldClose() {
        rl.BeginDrawing()
        rl.ClearBackground(rl.GetColor(0x18181b))
        rg.GuiCheckBox(rl.Rectangle{x: 50.0, y: 50.0, width: 24.0, height: 24.0}, "Enable Feature", &mut toggle)
        rl.EndDrawing()
    }
}
```

### Level 3 — Multi-Platform: macOS App, WebAssembly & iOS UIKit
- **macOS Native .app**: `cfire build --subsystem=gui app.cf -o myapp` (creates signed bundle)
- **WebAssembly HTML5**: `cfire build --target=wasm app.cf -o app.html`
- **Native iOS Simulator**: `cfire run --ios app.cf` (native auto-layout with `GuiIos*`)

---

## Chapter 14: Multi-Target Audio, Polyphonic Synthesis & Real-Time DSP (`core:audio`)

### Level 1 — Zero-Asset Procedural Sound Effects
```cfire
import "core:audio"

fn main() {
    try audio.init(48000, 2)
    defer audio.close()

    coin := audio.load_sound_from_wave(audio.gen_sfx(audio.SfxPreset::Coin))
    coin.play()
}
```

### Level 2 — ADSR Envelope Synthesis & RIFF/WAV Encoding
```cfire
import "core:audio"

fn main() {
    tone := audio.gen_tone(audio.Waveform::Triangle, 440.0, 0.5, 0.7)
    try audio.save_wav(tone, "output_a440.wav")
    println("WAV saved successfully.")
}
```

### Level 3 — Real-Time 64-Voice Mixer & Float32 Audio Stream
```cfire
import "core:audio"

fn main() {
    try audio.init(48000, 2)
    defer audio.close()

    stream := try audio.open_stream(48000, 2)
    tone := audio.gen_tone(audio.Waveform::Sine, 523.25, 0.1, 0.5) // C5
    if stream.needs_data(2048) {
        stream.push_samples(tone.samples)
    }
    println("Active audio master RMS: {:.2f}", audio.rms_level())
}
```

---

## Chapter 15: AI-First LLMs, Multimodal Documents, Local Vectors, RAG & MCP (`core:ai`)

### Level 1 — Multi-Provider Chat, Streaming & Compile-Time Structured Outputs
```cfire
import "core:ai" as ai

type BugReport struct {
    Component: str `json:"component" desc:"Subsystem name"`
    Severity:  i32 `json:"severity"  desc:"Severity from 1 to 5"`
}

fn main() {
    client := ai.openai() // Or ai.anthropic(), ai.gemini(), ai.ollama(), ai.openai_compatible(url)

    // 1. Real-time SSE token streaming
    _ := try client.stream("gpt-4o-mini", [ai.user("Explain zero-cost abstractions.")], |delta| {
        print("{}", delta)
    })

    // 2. Compile-time JSON Schema constrained generation directly into a struct T
    report := try client.generate_struct[BugReport](
        "gpt-4o-mini",
        "Audio buffer underrun when switching sample rates at 96kHz"
    )
    println("Component: {}, Severity: {}", report.Component, report.Severity)
}
```

### Level 2 — Multimodal File Attachments & Structured Extraction from Files
Attach images (`.png`, `.jpg`, `.webp`, `.bmp`), PDFs (`.pdf`), audio (`.wav`, `.mp3`), or Office/OpenDocument files directly to prompts or extract typed structs from any file:
```cfire
import "core:ai" as ai

type Invoice struct {
    Vendor: str   `json:"vendor" desc:"Merchant or vendor name"`
    Total:  f64   `json:"total"  desc:"Total invoice amount"`
    Items:  []str `json:"items"  desc:"Line item descriptions"`
}

fn main() {
    client := ai.gemini()

    // Ask questions about an image, PDF, or slide deck
    summary := try client.ask_with_file("gemini-2.5-flash", "Summarize the architecture diagram", "arch.png")
    println("Summary: {}", summary)

    // Extract a strongly-typed struct directly from a receipt image, PDF, or presentation
    inv := try client.generate_struct_from_file[Invoice](
        "gemini-2.5-flash",
        "Extract all invoice fields",
        "receipt.pdf"
    )
    println("Vendor: {} — Total: ${:.2f}", inv.Vendor, inv.Total)
}
```

### Level 3 — Zero-Dependency Document & Slide Ingestion for Vector RAG
`ai.load_document(path)` and `ai.parse_document(bytes, filename)` extract clean Markdown and per-page/per-slide `sections` (`[]ai.DocumentSection`) with zero external library dependencies across **PDF (`.pdf`)**, **PowerPoint (`.pptx`)**, **Word (`.docx`)**, **Excel (`.xlsx`)**, **LibreOffice (`.odt`, `.odp`, `.ods`)**, **EPUB (`.epub`)**, **HTML (`.html`)**, **CSV (`.csv`)**, **Markdown (`.md`)**, and **Images**:
```cfire
import "core:ai" as ai
import "core:db/sqlite" as sqlite

type SlideHit struct {
    SlideNum: i32 `sql:"slide_num"`
    Title:    str `sql:"title"`
    Snippet:  str `sql:"snippet"`
}

fn main() {
    // 1. Load a multi-slide presentation (.pptx / .odp), PDF (.pdf), or document (.docx / .odt / .epub)
    deck := try ai.load_document("quarterly_review.pptx")
    chunks := ai.chunk_document(deck, 512, 64)

    // 2. Index chunks into SQLite with 1-based slide/page provenance and normalized local embeddings
    mut db := try sqlite.open(":memory:")
    defer db.close()
    try db.exec("CREATE TABLE slides (slide_num INT, title TEXT, snippet TEXT, embedding BLOB)")

    for ch in chunks {
        vec := try ai.embed("local", ch.text)
        try db.exec("INSERT INTO slides VALUES (?1, ?2, ?3, ?4)", ch.index, ch.title, ch.text, vec)
    }

    // 3. Query using SIMD-accelerated vec_cosine_similarity
    qvec := try ai.embed("local", "latency p99 metrics")
    hits := try db.query[SlideHit](
        "SELECT slide_num, title, snippet FROM slides ORDER BY vec_cosine_similarity(embedding, ?1) DESC LIMIT 1",
        qvec
    )
    if hits.len() > 0 {
        println("[Slide {}: {}] {}", hits[0].SlideNum, hits[0].Title, hits[0].Snippet)
    }
}
```

### Level 4 — Cross-Platform OCR, Vision Embeddings & Linux / macOS / WASM Parity
```cfire
import "core:ai" as ai

fn main() {
    // 1. On-device OCR (Apple Vision.framework on macOS/iOS, libtesseract/tesseract on Linux, + PNG/JPEG metadata)
    text := try ai.ocr("invoice.png")
    println("OCR text: {}", text)

    // 2. Normalized 384-dim visual + semantic image embedding (4x4 RGB/Luma moments + Sobel HOG + 8x8 dHash)
    img_vec := try ai.embed_image("local", "invoice.png")
    println("Visual embedding dims: {}", img_vec.len())
}
```

| Capability | macOS / iOS (`__APPLE__`) | Linux (`__linux__`) | WebAssembly (`__EMSCRIPTEN__`) |
| :--- | :--- | :--- | :--- |
| **Office & E-Book (`.docx`, `.pptx`, `.xlsx`, `.odt`, `.odp`, `.ods`, `.epub`)** | Pure C++23 RFC 1951 `DEFLATE` + ZIP/XML (iOS `.app` bundle + `Documents/` sandbox aware) | Pure C++23 RFC 1951 `DEFLATE` + ZIP/XML | Pure C++23 RFC 1951 `DEFLATE` + ZIP/XML + browser drag-and-drop `MEMFS` staging (`window.cfireStageFile`) |
| **PDF (`.pdf`) Extraction** | `PDFKit.framework` + C++23 `/FlateDecode` & `/ToUnicode` CMap | Auto-detected `pdftotext -layout` + C++23 `/FlateDecode`, `/ToUnicode` CMap & `TJ` kerning | Pure C++23 `/FlateDecode`, `/ToUnicode` CMap & `TJ` kerning |
| **Image OCR (`ai.ocr`)** | `Vision.framework` (`VNRecognizeTextRequest`) + metadata | Dynamic `dlopen("libtesseract.so.5")` C-API $\rightarrow$ `tesseract` CLI $\rightarrow$ `CFIRE_OCR_MODEL` + metadata | PNG `tEXt`/`iTXt` & JPEG `COM` metadata |
| **Local Image Embeddings (`ai.embed_image("local", ...)`)** | Pure C++23 PNG/BMP pixel decoder (4x4 RGB/Luma + Sobel HOG + dHash) + OCR | Pure C++23 PNG/BMP pixel decoder (4x4 RGB/Luma + Sobel HOG + dHash) + OCR | Pure C++23 PNG/BMP pixel decoder (4x4 RGB/Luma + Sobel HOG + dHash) |
| **Local Text Embeddings (`ai.embed("local", ...)`)** | `NaturalLanguage.framework` (`NLEmbedding`) + semantic cluster trigram fallback | `CFIRE_LOCAL_EMBED_MODEL` (Ollama) + semantic concept cluster & trigram projections | Semantic concept cluster & trigram projections |
| **Cloud AI HTTPS & Auth (`ai.Client`, `ai.embed`)** | `libssl` (`macOS`) / Foundation `NSURLConnection` native TLS (`iOS` & fallback) | `libssl` (`OpenSSL`) | Browser `XMLHttpRequest` with custom auth headers + Anthropic direct browser CORS header |
| **Vector Search Math (`sqlite` & `core:ai`)** | `ARM NEON` / `AVX2` hardware SIMD | `AVX2` (`_mm256_fmadd_ps`) / `ARM NEON` hardware SIMD | `WASM 128-bit SIMD` (`-msimd128`, `wasm_f32x4_*`) |

### Level 5 — Authoring Native Model Context Protocol (`MCP`) Servers
```cfire
import "core:ai" as ai

type SearchParams struct {
    Query: str `json:"query" desc:"Search query phrase"`
    Limit: i64 `json:"limit" desc:"Maximum matches"`
}

fn main() {
    mut server := ai.make_mcp_server("docs-mcp", "1.0.0")
    server.register_tool[SearchParams]("search_docs", "Searches standard library") |params| {
        println("Query received: {}", params.Query)
        return "[{\"title\": \"core:ai\"}]"
    }
    
    // Process JSON-RPC request from AI coding agent (or call server.serve_stdio())
    req := "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"tools/list\"}"
    res := server.handle_jsonrpc(req)
    println("MCP Response: {}", res)
}
```

---

## Chapter 16: Full-Stack Production Patterns & Canonical Projects Index

### Architectural Tour: Composing the Standard Library
Every module in cfire is designed to interoperate without glue code:
- Networking (`core:net/http`, `core:net/websocket`, `core:net/tls`) seamlessly streams JSON (`core:json`)
- Data records map directly to SQLite tables (`core:db/sqlite`) with SIMD vector similarity (`core:ai`)
- Graphical user interfaces (`vendor:raylib`, `vendor:raygui`) render smoothly alongside real-time audio threads (`core:audio`)
- All code lowers 1-to-1 to clean, auditable ISO C++23.

### Canonical Projects Index (`examples/`)
The `examples/` directory contains verified, production-grade applications:

| Directory | Title | Core Technologies Demonstrated |
| :--- | :--- | :--- |
| `examples/01-hello-core` | Hello Core & Defer | Mutability, parameter intent, scope exit, format |
| `examples/02-log-compressor` | Log Compressor | Command-line parsing (`core:cli`), Zstandard (`core:compress/zstd`) |
| `examples/03-pkg-cache` | Package Cache | SQLite3 WAL transactions, SIMD JSON struct mapping |
| `examples/04-async-workers` | Async Workers | Scoped thread pool concurrency (`async.scope`), cluster health checks |
| `examples/05-cmake-interop` | CMake Interop | Clang JSON AST ingestion of native `<zstd.h>` |
| `examples/06-raylib-viz` | Raylib Visualization | Hardware 2D graphics loop, windowing, color rendering |
| `examples/07-types-match-map` | Types & Match | Algebraic data types, map collections, concept constraints |
| `examples/08-multi-pkg-cc-build` | Multi-Package Build | `build.cf` multi-package static archive linking (`build.CC`) |
| `examples/09-stdlib-test-pointers` | Stdlib & Tests | Smart pointers (`*T`, `^T`), `test "..."` runner |
| `examples/10-cmake-and-make-deps` | Multi-Build Deps | Simultaneous CMake and Make external dependency orchestration |
| `examples/11-http-server-router` | HTTP Web Server | Multi-threaded HTTP/1.1 server, fluent request router, path parameters |
| `examples/12-raygui-controls` | Raygui GUI Kit | Checkboxes, sliders, progress bars, text input controls |
| `examples/13-ios-native-ui` | Native iOS UI | Zero-coordinate `GuiIos*` auto-layout kit, UIKit integration |
| `examples/14-ai-rag-sqlite` | AI Vector RAG | Local normalized embeddings, SQLite `vec_cosine_similarity`, schema |
| `examples/15-mcp-server` | MCP Agent Server | Model Context Protocol server, JSON-RPC 2.0 tool registration |
| `examples/16-audio-synth` | Audio Synthesis | Procedural SFX, polyphonic ADSR synthesizer, RIFF/WAV codec |

Each example can be executed immediately:
```bash
cfire run examples/14-ai-rag-sqlite/main.cf
cfire run examples/15-mcp-server/main.cf
cfire run examples/16-audio-synth/main.cf
```

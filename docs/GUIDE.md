# The `cfire` Language & Systems Programming Guide

**cfire** (pronounced *sapphire* • `/ˈsæf.aɪ.ər/`) is a systems programming language that compiles 1-to-1 to human-readable ISO C++23. It gives you the ergonomics of a modern language—read-only bindings by default, explicit call-site mutation and move markers (`mut`, `move`), value-based error handling (`T!`), lexical structured concurrency (`async.scope`), and zero-wrapper C/C++ header ingestion (`import cpp`)—while compiling down to clean, standard C++23 across native desktop/server (`x86_64` / `arm64`), WebAssembly (`--web`), and native iOS (`--ios`).

This guide is written as a hands-on builder's tour rather than a dry language specification. Each chapter walks through real engineering problems step by step—starting with a working example, showing what happens under the hood in emitted ISO C++23, and highlighting how the compiler's 18 static safety checks catch subtle bugs before they ever reach a debugger.

---

# Part I: Getting Started & Tooling

## Chapter 1: Installation & Platform Setup

### How the `cfire` Toolchain Works
`cfire` ships as a single self-contained native binary with zero external runtime dependencies. When you run or build a `.cf` file, the compiler steps through five deterministic stages:

1. **Lexical & Resilient Syntax Analysis (`lexer` / `parser`)**: Tokenizes `.cf` source files and constructs a typed Abstract Syntax Tree (AST) with statement-level error recovery so your editor and LSP stay responsive while you type.
2. **Clang AST Bridge (`clang_bridge`)**: Whenever it sees `import cpp "header.h"`, `cfire` invokes the system Clang frontend (`-Xclang -ast-dump=json`) to ingest C/C++ function signatures, structs, enums, and macros directly—no manual FFI glue required.
3. **Semantic, Intent & Loan Verification (`sema`)**: Resolves package imports, infers types, checks `match` exhaustiveness and reachability, verifies call-site `mut`/`move` markers, tracks local pointer/slice loans against container mutations (`cfire/USE-AFTER-FREE`, `cfire/ITERATOR-INVALIDATION`), and enforces flow-sensitive null checks (`cfire/NULL-DEREF`).
4. **ISO C++23 Code Generation (`codegen`)**: Emits standard ISO C++23 source code annotated with `#line` directives so `lldb`, `gdb`, and AddressSanitizer stack traces point directly to your `.cf` filenames and line numbers.
5. **PCH-Accelerated Backend Compilation (`buildsys`)**: Compiles the generated C++23 translation unit against a precompiled header (`cfire_prelude.hpp.pch`) using your host C++23 compiler (`clang++` 18+ or `g++` 14+) and links the required system or vendor libraries.

> [!NOTE]
> **Backend Compiler Discovery Order**: `cfire` locates a C++23-capable compiler by checking: (1) `CFIRE_CXX`, (2) `CXX`, (3) `clang++-18` / `clang++` (macOS Xcode Command Line Tools or LLVM 18+), and (4) `g++-14` / `g++`. Executables produced by `cfire build` are standalone native Mach-O, ELF, WASM, or iOS `.app` binaries with no garbage collector or virtual machine.

### Quick Installation Script (macOS & Linux)
Install the `cfire` binary and standard `runtime/` headers (`~/.local/bin/cfire` and `~/.cfire/runtime`, or `/usr/local/bin/cfire` and `/usr/local/share/cfire/runtime` when run as root):

```bash
curl -fsSL https://cfire.memo.engineer/install.sh | sh
```

To bootstrap all optional native acceleration libraries (`simdjson`, `libzstd`, `sqlite3`, `openssl`, `cmake`) and warm the C++23 runtime precompiled header (`.pch`) in one shot, pass `--setup`:

```bash
curl -fsSL https://cfire.memo.engineer/install.sh | sh -s -- --setup
```

Verify the active compiler binary:
```bash
cfire --version
# Output: cfire 0.11.0 (5c06d4a, ISO C++23 systems toolchain)
```

### Checking Toolchain Health (`cfire doctor` & `cfire setup`)
Whenever you set up a new machine or CI runner, run `cfire doctor` to verify your C++23 compiler, `runtime/` header discovery, `.pch` cache, `pkg-config` tools, native `ext:*` libraries, and cross-compilation SDKs (`em++`, iOS Simulator):

```bash
cfire doctor
cfire doctor --json   # Machine-readable JSON report for CI & coding agents
```

If any optional native libraries or build tools are missing, `cfire setup` installs them automatically via your host package manager (`brew` on macOS; `apt-get`, `dnf`, `pacman`, or `apk` on Linux), syncs `runtime/` headers to `~/.cfire/runtime`, and precompiles the C++23 prelude `.pch`:

```bash
cfire setup --yes             # Install missing native libraries + warm C++23 PCH
cfire setup --yes --raylib    # Also pre-fetch vendor:raylib into global cache
cfire setup --yes --wasm      # Also install Emscripten (em++) for --target=wasm
```

### Building from Source & Platform Setup

#### macOS (Apple Silicon `arm64` & Intel `x86_64`)
1. Install Xcode Command Line Tools (provides `clang++`, macOS SDK headers, and system frameworks including `WebKit.framework` and `AudioToolbox.framework`):
   ```bash
   xcode-select --install
   ```
2. Clone, build, and install `cfire` from source:
   ```bash
   git clone https://github.com/gsandoval/cfire.git
   cd cfire && make -j4
   ./cfire install --user   # Installs ~/.local/bin/cfire + ~/.local/share/cfire/runtime (or sudo make install)
   cfire setup --yes        # Installs simdjson, zstd, sqlite, openssl, cmake via Homebrew
   ```
   Need a self-relocating toolchain folder you can copy onto another machine or CI image? Run `./cfire install --portable <dir>` (or `./cfire install --prefix <dir>`).

#### Linux (Ubuntu / Debian / Arch)
- **Ubuntu 24.04+ / Debian 12+**:
   ```bash
   sudo apt-get update && sudo apt-get install -y clang make
   git clone https://github.com/gsandoval/cfire.git
   cd cfire && sudo make install
   cfire setup --yes
   ```
- **Arch Linux**:
   ```bash
   sudo pacman -S clang make pkgconf simdjson zstd sqlite openssl alsa-lib webkit2gtk-4.1
   cd cfire && sudo make install
   ```

#### Docker & OrbStack Containers
Build and run the official Ubuntu 24.04 container image with all `core:*` and `ext:*` native libraries pre-installed:
```bash
docker build -t cfire:latest .
docker run --rm cfire:latest doctor
docker run --rm cfire:latest run examples/01-hello-core/main.cf
```

#### Windows (WSL2)
Run `cfire` inside WSL2 with an Ubuntu 24.04 distribution:
```bash
wsl --install -d Ubuntu-24.04
wsl
sudo apt-get update && sudo apt-get install -y clang-18 pkg-config libsimdjson-dev libzstd-dev libsqlite3-dev libssl-dev
```

### Understanding the Three Stdlib Namespaces (`core:*`, `ext:*`, `vendor:*`)
One of the biggest pain points in systems programming is pulling in a seemingly innocent utility module only to discover it requires five system C libraries at link time. `cfire` keeps dependency boundaries honest by organizing its 38 built-in modules into three explicit namespaces based on external link requirements:
- **`core:*` (Foundational & Platform Core — 30 Modules)**: Everything that compiles and runs out-of-the-box with built-in ISO C++23 or baseline OS APIs/fallbacks (`core:str`, `core:unicode`, `core:regex`, `core:collections`, `core:sort`, `core:math`, `core:rand`, `core:json`, `core:encoding/*`, `core:fs`, `core:os`, `core:time`, `core:sync`, `core:sync/context`, `core:cli`, `core:log`, `core:keychain`, `core:build`, `core:net`, `core:net/url`, `core:net/tls`, `core:net/http`, `core:net/websocket`, `core:audio`, `core:crypto/*`) with zero external C library setup required.
- **`ext:*` (Native C Libraries, WebUI & AI Extensions — 6 Opt-In Modules)**: Specialized domain engines (`ext:ai`, `ext:compress/zstd`, `ext:crypto/sodium`, `ext:db/sqlite`, `ext:net/ssh`, `ext:webui`) that link external C libraries, native WebView GUI toolkits, or AI/OCR engines only when imported—and include built-in portable fallbacks when an optional system library isn't installed.
- **`vendor:*` (Bundled Third-Party C/C++ Engines — 2 Modules)**: `vendor:raylib` and `vendor:raygui`.

| Core / Extension / Vendor Module | Optional System Library | `pkg-config` Name | Built-In Portable Fallback When Absent |
| :--- | :--- | :--- | :--- |
| `core:json` | `libsimdjson` | `simdjson` | Bundled single-header `simdjson` implementation in `runtime/vendor/` |
| `core:net/tls` & `core:net/http` | `libssl` / `libcrypto` | `openssl` | Dynamic `dlopen("libssl")` on Desktop/Linux; Apple `Foundation` `NSURLConnection` on macOS/iOS; browser `XMLHttpRequest` on WASM |
| `core:audio` | `CoreAudio` / `libasound2` | `alsa` | `AudioToolbox.framework` on macOS/iOS; dynamic `dlopen("libasound.so.2")` on Linux; `WebAudio` on WASM; deterministic software mixer in headless CI |
| `core:keychain` | `Security.framework` / `libsecret` | `libsecret-1` | macOS `Security.framework`, Linux `secret-tool`, or AES-256-GCM fallback vault |
| `ext:db/sqlite` | `libsqlite3` | `sqlite3` | System `libsqlite3` (pre-installed on macOS/iOS; in-memory/WASM shim on `--web`) |
| `ext:compress/zstd` | `libzstd` | `libzstd` | Native `libcompression` (`COMPRESSION_LZFSE` / `ZLIB`) on Apple/iOS; RFC 1951 codec on WASM |
| `ext:crypto/sodium` | `libsodium` | `libsodium` | Pure C++23 ChaCha20-Poly1305 / Ed25519 / Argon2 fallback |
| `ext:net/ssh` | `libssh` | `libssh` | OpenSSH `ssh` / `sftp` / `scp` subprocess bridge when `libssh` is absent |
| `ext:ai` (OCR & PDF) | `Vision` / `libtesseract` / `pdftotext` | `tesseract` | `Vision.framework` & `PDFKit` on macOS/iOS; `dlopen("libtesseract.so.5")` & `pdftotext` on Linux; pure C++23 `/FlateDecode` PDF parser & PNG/JPEG metadata reader on all targets |
| `ext:webui` | `WebKit.framework` / `webkit2gtk-4.1` | `webkit2gtk-4.1` | `WKWebView` + `Cocoa` on macOS, `WKWebView` + `UIKit` on iOS, `WebKitGTK` + `GTK3` on Linux, and deterministic headless RPC/Event dispatcher in CI |
| `vendor:raylib` & `vendor:raygui` | Bundled in `runtime/` | None required | Self-contained Cocoa/OpenGL desktop backend, X11/OpenGL Linux backend, WebGL/HTML5 Canvas on WASM, and `UIKit` + `OpenGLES3` on iOS |

### Setting Up WebAssembly & iOS Cross-Compilation

#### WebAssembly (`--web` / `--target=wasm`)
Compiling `.cf` applications to WebAssembly (`.html` + `.js` + `.wasm`) uses the Emscripten SDK (`em++` in `PATH`):
```bash
git clone https://github.com/emscripten-core/emsdk.git
cd emsdk && ./emsdk install latest && ./emsdk activate latest
source ./emsdk_env.sh
```
- **Running in the Browser**: `cfire run --web app.cf` compiles with `-std=c++23 -O2 -msimd128` and starts a local HTTP server (or runs via `node` for CLI targets).
- **Web Profiles**: GUI applications importing `vendor:raylib` or `core:audio` automatically select the `canvas` profile (HTML5 WebGL canvas + drag-and-drop `MEMFS` file staging); console applications select the `cli` profile.

#### Native iOS Simulator & Device (`--ios`)
Building for iOS requires macOS with full Xcode installed so `xcrun` can resolve the `iphonesimulator` and `iphoneos` SDK sysroots:
```bash
xcode-select -p
xcrun --sdk iphonesimulator --show-sdk-path
```
- **App Bundle Generation**: `cfire build --target=ios-sim app.cf -o build/MyApp` generates a complete `build/MyApp.app/` bundle containing the Mach-O executable (`arm64-apple-ios16.0-simulator`), `Info.plist`, `PkgInfo`, and bundled assets/icons, then signs it with `codesign --force --sign -`.
- **One-Command Simulator Launch**: `cfire run --ios app.cf` boots the active iOS Simulator via `xcrun simctl`, installs the `.app` bundle, and streams stdout/stderr right back to your terminal.

### Connecting Your Editor (LSP) & AI Agents (MCP)

#### Model Context Protocol Server (`cfire mcp`)
If you pair-program with AI coding agents (Claude Code, Cursor, Windsurf, Antigravity), `cfire mcp` runs a built-in JSON-RPC 2.0 Model Context Protocol server over `stdio`. Instead of guessing stdlib signatures, your agent can query the compiler and run checks directly:

```bash
cfire mcp --setup          # Automatically registers cfire MCP in detected AI clients
cfire mcp --print-config   # Prints the JSON configuration block
```

```json
{
  "mcpServers": {
    "cfire": {
      "command": "cfire",
      "args": ["mcp"]
    }
  }
}
```

Tools exposed by `cfire mcp`:

| MCP Tool Name | Input Schema | Behavior & Return Payload |
| :--- | :--- | :--- |
| `cfire_stdlib_list` | `{}` | Lists every `core:*`, `ext:*`, and `vendor:*` module with its import path, namespace alias, and summary. |
| `cfire_stdlib_lookup` | `{"query": "str"}` | Searches modules, function signatures, and types matching `query` (e.g., `"ext:webui"` or `"bind"`). |
| `cfire_check` | `{"code"?: "str", "path"?: "str"}` | Parses, type-checks, and verifies loan/intent rules on inline `.cf` `code` or a workspace `path`; returns `"OK (0 errors)"` or exact diagnostics. |
| `cfire_test` | `{"path"?: "str", "filter"?: "str", "bench"?: bool, "fuzz_runs"?: int, "seed"?: int, "bench_time"?: int}` | Executes unit tests (`test`), property-based fuzz tests (`fuzz` with shrinking), `///` doc-tests, automatic JSON round-trip invariants, or statistical benchmarks (`bench`). |
| `cfire_eval` | `{"code": "str"}` | Compiles and executes a `.cf` snippet in a temporary sandbox and returns combined stdout/stderr. |
| `cfire_outline` | `{"path"?: "str"}` | Returns a token-compressed structural AST outline of packages, structs, enums, interfaces, functions, and test blocks. |
| `cfire_inspect` | `{"target": "str"}` | Inspects a stdlib module/symbol, compiler diagnostic rule (`cfire/MUT-CALLSITE`), or foreign C/C++ header (`cpp <header.h>`). |
| `cfire_doc_llm` | `{}` | Returns the complete LLM & AI Agent language reference sheet (`LLMS.txt`). |

#### Editor Syntax & LSP Setup (`cfire ide` & `cfire lsp`)
Install TextMate syntax highlighting, filetype associations, and LSP client configurations with a single command:
```bash
cfire ide --all       # Configures VS Code, Antigravity, Cursor, Windsurf, Zed, Neovim, and CLion
cfire ide --status    # Checks which editors and AI agents are configured on your machine
cfire lsp             # Starts the JSON-RPC 2.0 Language Server on stdio
```
The built-in `cfire lsp` daemon provides live diagnostics (`textDocument/publishDiagnostics`), type/memory-layout hover, auto-import completion, signature help, inlay hints, go-to-definition, cross-package rename (`textDocument/rename`), quick-fix code actions (`cfire/MUT-CALLSITE` and exhaustive `match` arm synthesis), and formatting.

---

## Chapter 2: Quickstart & Toolchain Tour

### Trying Ideas Instantly with `cfire eval`
Before creating a single file, you can test expressions, math, or standard library functions right from your terminal using `cfire eval`. It wraps your snippet in `package main`, detects module prefixes (`math.`, `json.`, `fs.`, `time.`), and injects the matching `import` headers automatically:

```bash
# 1. Expression mode (-e): evaluates the expression and prints its result
cfire eval -e "2.0 * math.PI * 10.0"
# Output: 62.83185307179586

# 2. Statement mode: executes semicolon- or newline-separated statements
cfire eval 'nums := [10, 20, 30]; println("count={} first={}", nums.len(), nums[0])'
# Output: count=3 first=10
```

Because `cfire` caches a precompiled C++23 runtime header (`.cfire/cache/pch/`), `cfire eval` compiles and runs native machine code in milliseconds.

### Writing Your First `.cf` Program (`hello.cf`)
Let's create a small standalone program in `hello.cf` that trims a node name and logs a startup timestamp:

```cfire
package main

import "core:time"

fn main() {
    started_ms := time.now_ms()
    host := "  cfire-node-01  ".trim()
    println("[{}] node={} ready", started_ms, host)
}
```

Run it directly with `cfire run`:
```bash
cfire run hello.cf
# Output: [1730462400123] node=cfire-node-01 ready
```

Notice a few details that shape daily code in `cfire`:
- Every `.cf` file starts with a `package` declaration (`package main` for executable entry points).
- Standard library imports always carry their tier prefix (`import "core:fs"`, `import "core:time"`). Their last path segment (`fs`, `time`) becomes the namespace alias unless you rename it with `as`.
- `started_ms := time.now_ms()` declares an **immutable** binding (`const std::int64_t` in C++23). If you try to reassign `started_ms` later, the compiler stops you with `CF3003`. When you actually want mutation, you opt in with `mut`.

### Checking, Building, Inspecting C++23 & Ejecting to CMake
As your single-file script grows into a real service or desktop app, the same CLI scales with you:

```bash
# 1. Type-check and verify intent & loan rules without invoking the linker
cfire check hello.cf

# 2. Run under AddressSanitizer (ASan) + UndefinedBehaviorSanitizer (UBSan)
cfire run --sanitize hello.cf

# 3. Compile an optimized (-O2) standalone native binary
cfire build hello.cf -o ./bin/hello
./bin/hello

# 4. Inspect the exact ISO C++23 code generated by the compiler
cfire emit-cpp hello.cf

# 5. Export a zero-lock-in C++23 + CMake project tree
cfire eject hello.cf dist/
```

#### What `cfire emit-cpp` Produces
There is no black box between `.cf` and your CPU. Running `cfire emit-cpp hello.cf` prints the exact ISO C++23 source code compiled by the backend:

```cpp
// Generated by cfire 0.11.0 — ISO C++23
#include "cfire_prelude.hpp"
#include "core/str.hpp"
#include "core/time.hpp"

auto main(int argc, char** argv) -> int {
    cfire::runtime::init_args(argc, argv);
#line 7 "hello.cf"
    const std::int64_t started_ms = cfire::time::now_ms();
#line 8 "hello.cf"
    const std::string host = cfire::str::trim(std::string("  cfire-node-01  "));
#line 9 "hello.cf"
    std::println("[{}] node={} ready", started_ms, host);
    return 0;
}
```

Every statement is preceded by a `#line` directive mapping back to `hello.cf`. If you attach `lldb` or trigger an AddressSanitizer report, the stack trace points straight to `hello.cf:9`.

#### Zero Lock-In with `cfire eject`
Adopting a new systems language is much easier when you know you can walk away at any time. Running `cfire eject hello.cf dist/` exports a self-contained CMake project:

```text
dist/
├── CMakeLists.txt          # Standalone CMake 3.25+ build configuration
├── main.cpp                # Clean, human-readable ISO C++23 source
└── runtime/
    ├── cfire_prelude.hpp   # Core slice, map, error, and RAII defer primitives
    └── core/               # Only the standard library headers your project uses
```

Anyone on your team—or any downstream platform—can build `dist/` with vanilla `cmake` and `clang++`/`g++` without `cfire` installed.

---

## Chapter 3: Workspaces, CLI & Build System

### Scaffolding New Projects (`cfire new` & `cfire init`)
Instead of wiring up directories and build files by hand, use `cfire new <dir>` (or `cfire init` inside an existing directory) with one of the four built-in project templates:

```bash
cfire new my-cli               # Default: CLI / backend service template (--template cli)
cfire new --template webui app # Hybrid desktop/mobile WebView app (ext:webui + ./ui/index.html)
cfire new --template gui game  # Hardware-accelerated 2D/3D raylib window (vendor:raylib)
cfire new --template tako tool # Immediate-mode / retained GUI app (pkg:tako)
```

Every scaffolded workspace includes a ready-to-run `build.cf`, `main.cf`, a unit test in `main_test.cf`, and a `.gitignore` so you can immediately run `cfire run` and `cfire test`:

| Template (`--template`) | Generated Files | Target Matrix & Default Subsystem | Best For |
| :--- | :--- | :--- | :--- |
| `cli` (default) | `build.cf`, `main.cf`, `main_test.cf`, `.gitignore` | `["native"]` (`console`) | CLI tools, microservices, AI agents, systems daemons. |
| `webui` | `build.cf`, `main.cf`, `main_test.cf`, `ui/index.html`, `.gitignore` | `["native", "ios-sim"]` (`gui`) | Wails/Tauri-style desktop & mobile apps using HTML/CSS/JS over `ext:webui`. |
| `gui` | `build.cf`, `main.cf`, `main_test.cf`, `.gitignore` | `["native", "wasm", "ios-sim"]` (`gui`, `canvas`) | 60 FPS 2D/3D canvases, games, and visualizers using `vendor:raylib` & `vendor:raygui`. |
| `tako` | `build.cf`, `main.cf`, `main_test.cf`, `.gitignore` | `["native", "wasm", "ios-sim"]` (`gui`, `canvas`) | Ergonomic desktop/web/iOS tools using `pkg:tako` / `core:tako`. |

### Multi-Package Workspace Layout
As a codebase grows beyond a single directory, you can split domain logic into internal packages under `pkg/` and third-party C/C++ libraries under `vendor/`:

```text
my-service/
├── build.cf                    # Declarative build graph (executables, C/CMake deps, pre-build hooks)
├── cfire.lock                  # Deterministic SHA-256 & commit lockfile for remote dependencies
├── main.cf                     # Entry package (`package main`)
├── main_test.cf                # Entry package tests (automatically excluded from production builds)
├── pkg/
│   └── checksum/
│       └── checksum.cf         # Internal package (`package checksum`, imported as `import "./pkg/checksum"`)
└── vendor/
    └── fastcrc/                # Local or vendored C/C++/CMake library sources
```

How `cfire` resolves files and packages across your workspace:
1. **Same-Directory Package Merging**: All `.cf` files in the same folder declaring the same `package <name>` (for example, `main.cf`, `routes.cf`, and `store.cf` all declaring `package main`) automatically share their types, functions, methods, and constants—no relative imports needed between sibling files. Test files ending in `_test.cf` are compiled during `cfire test` and excluded during `cfire build` / `cfire run`.
2. **Local Workspace Packages (`import "./pkg/checksum"`)**: Scans all non-test `.cf` files in `./pkg/checksum/` belonging to `package checksum` and exposes their declarations under the `checksum.` namespace.
3. **Standard Library (`import "core:..."`, `"ext:..."`, `"vendor:..."`)**: Resolves headers from the `cfire` runtime directory and auto-links required system frameworks.
4. **Companion Toolchain Packages (`import "pkg:tako"` / `import "core:tako"`)**: Resolves companion SDK headers across workspace parents, `~/.local/share`, and `CFIRE_PKG_PATH`.

### Declarative Build Files (`build.cf`)
For single-package projects, `cfire run main.cf` works with zero configuration. When you need multi-target app bundles, pre-build code generation, or C/C++/CMake dependencies, declare them in `build.cf`:

```cfire
package build

import "core:build"

// 1. Pre-build code generation hook (runs only when Inputs are newer than Outputs)
GenTables := build.Command("python3", {
    Args:    ["scripts/gen_tables.py"],
    Inputs:  ["scripts/schema.json"],
    Outputs: ["src/generated_tables.cf"],
})

// 2. Native C/C++ sources compiled into a cached static archive (.a)
CSupport := build.CC("./vendor/fastcrc", {
    Sources:  ["fastcrc.c"],
    Includes: ["."],
    Defines:  {"FASTCRC_SEED": 42},
})

// 3. External CMake subproject built and linked automatically
KVStore := build.CMake("./vendor/libkv_cmake", {
    Target:   "kvstore",
    Includes: ["include"],
    Defines:  {"KV_SALT": 42},
})

// 4. Remote Git repository dependency (pinned in cfire.lock)
RemoteEngine := build.Git("https://github.com/example/engine.git", {
    Tag:      "v1.2.0",
    Driver:   "CMake",
    Target:   "engine_core",
    Includes: ["include"],
})

// 5. Multi-target application executable definition
App := build.Executable("service-app", {
    Entry:      "src/main.cf",
    Subsystem:  "console",
    Name:       "ServiceApp",
    BundleId:   "com.example.serviceapp",
    Icon:       "assets/icon.png",
    Targets:    ["native", "wasm", "ios-sim"],
    OutDir:     "build",
    WebProfile: "auto",
})
```

#### `build.Executable` & Dependency Builder Reference

| Field / Builder | Type / Signature | Default / Config | Purpose |
| :--- | :--- | :--- | :--- |
| `Entry` | `str` | `"main.cf"` | Entry `.cf` file containing `fn main()`. |
| `Subsystem` | `str` | `"auto"` | `"console"` (CLI binary) or `"gui"` (creates a macOS `.app` bundle and suppresses terminal console). Auto-switches to `"gui"` when `vendor:raylib` or `ext:webui` is used. |
| `Name` & `BundleId` | `str` | `<name>`, `"com.cfire.<name>"` | Display name and reverse-DNS `CFBundleIdentifier` for macOS/iOS signing and Simulator launch. |
| `Icon` | `str` | `""` | Path to a `.png` icon; auto-generates `.icns` on macOS, `AppIcon60x60@2x.png` on iOS, and window icon pixels on Linux. |
| `Targets` & `WebProfile` | `[]str`, `str` | `["native"]`, `"auto"` | Target triples (`"native"`, `"wasm"`, `"ios-sim"`, `"ios"`) and WASM shell profile (`"canvas"` or `"cli"`). |
| `build.CC(dir, cfg)` | Builder | `Sources`, `Includes`, `Defines` | Compiles C/C++ sources into a SHA-256-cached static archive (`lib<name>.a`) and feeds `Includes` to the Clang AST bridge. |
| `build.CMake(dir, cfg)` | Builder | `Target`, `Includes`, `Defines` | Configures and builds a CMake subproject in `.cfire/cache/deps/` and links the target archive. |
| `build.Make(dir, cfg)` | Builder | `Target`, `Includes`, `Defines` | Runs `make -C <dir>` and links `lib<Target>.a`. |
| `build.Git(url, cfg)` / `build.Archive` | Builder | `Tag`/`Commit`/`Sha256`, `Driver` | Fetches remote dependency, pins cryptographic hashes in `cfire.lock`, and builds via `Driver` (vendor offline with `cfire vendor`). |

### Complete CLI Command Reference
Every tool in the `cfire` workflow lives inside the single `cfire` binary:

| Command & Syntax | Supported Flags | Description |
| :--- | :--- | :--- |
| `cfire init` / `cfire new <dir>` | `--template <cli\|tako\|gui\|webui>`, `--force` | Scaffolds a new `cfire` project (`build.cf`, `main.cf`, `main_test.cf`, `.gitignore`, plus `ui/index.html` for `webui`). |
| `cfire run [target\|file.cf] [-- args...]` | `--web`, `--ios`, `--device <name>`, `--port <n>`, `--no-open`, `--sanitize`, `--subsystem <gui\|console>` | Compiles and executes a native binary, local WebAssembly server (`--web`), or iOS Simulator app (`--ios`). |
| `cfire build [target\|file.cf]` | `-o <path>`, `--target <wasm\|ios-sim\|ios\|triple>`, `--web`, `--ios`, `--subsystem <gui\|console>`, `--name <app>`, `--bundle-id <id>`, `--icon <png>`, `--sanitize` | Compiles an optimized binary, macOS `.app` bundle, WebAssembly `.html`/`.wasm` bundle, or signed iOS `.app` bundle. |
| `cfire check [file.cf\|dir]` | `--json`, `--fix` | Workspace-aware lexer, parser, Clang header bridge, and semantic/loan/intent analyzer (with optional auto-fix via `--fix`). |
| `cfire fix [file.cf\|dir]` | `--dry-run`, `--json` | Automatically inserts missing `core:*` imports, missing call-site `mut` markers, and missing `match` arms. |
| `cfire test [file.cf\|dir]` | `--sanitize`, `--json`, `--filter <pattern>`, `--bench`, `--fuzz-runs <N>`, `--seed <S>`, `--bench-time <ms>` | Compiles and executes `test`, `fuzz` (with counterexample shrinking), `///` doc-tests, and `[auto] json round-trip` checks. |
| `cfire bench [file.cf\|dir]` | `--json`, `--filter <pattern>`, `--bench-time <ms>` | Runs parameterized statistical micro-benchmarks (`bench "name" \|size in [...]\| { ... }`). |
| `cfire eval <code\|-e expr>` | `-e <expr>` | Evaluates a one-shot expression or statement block from CLI arguments or `stdin`. |
| `cfire serve [target\|dir]` | `--port <n>`, `--no-open` | Builds WebAssembly targets and serves static/WASM assets with COOP/COEP headers. |
| `cfire fmt [path]` | `--check` | Canonically formats `.cf` files in place (or exits non-zero on formatting drift with `--check`). |
| `cfire refactor rename <loc> <new>` | `--diff`, `--apply`, `--json` | Performs scope- and package-aware semantic symbol renaming across the workspace. |
| `cfire inspect <target>` / `outline` | `--json` | Queries stdlib docs, diagnostic rules, or C headers (`inspect`), or prints a token-compressed AST summary (`outline`). |
| `cfire emit-cpp` / `eject` / `vendor` | `[out_dir]` | Prints emitted ISO C++23 (`emit-cpp`), exports a standalone CMake tree (`eject`), or vendors remote deps (`vendor`). |
| `cfire doc [file.cf]` | `--check`, `--llm`, `--root <dir>` | Generates the `docs/` HTML site and `LLMS.txt` or verifies zero documentation drift (`--check`). |

---

# Part II: Core Language & Safety

## Chapter 4: Primitives, Collections & Control Flow

### Bindings, Scalar Types & Numeric Conversions
In C++, local variables are mutable by default (`int x = 10;`) and numeric types silently convert—often narrowing a 64-bit integer or signed index without a warning until runtime data corrupts.

`cfire` flips both defaults:
1. **Immutable by Default (`:=`)**: Declaring `count := 42` emits `const std::int64_t count = 42;`. You only mark a variable `mut` when its state genuinely changes over time.
2. **Explicit Numeric Conversions (`i32(x)`, `f64(x)`)**: Mixed-width arithmetic never narrows behind your back. You can convert between scalar types using clean constructor-style calls (`f64(count)`, `f32(x)`, `i32(y)`, `u8(b)`, `byte(b)`, `usize(n)`).
3. **Native String Interpolation**: Every double-quoted string (`"..."`) supports `{expr}` interpolation directly—including arbitrary expressions and format specifiers like `"{scaled:.2f}"` or `"{mask:04x}"`—without needing a `f"..."` prefix.

```cfire
package main

MAX_RETRIES: i32 = 5

fn main() {
    // 1. Immutable inferred bindings (lower to 'const' in C++23)
    count := 42                  // inferred as i64
    ratio := 3.14159             // inferred as f64
    mask := 0xFF_00              // hex literal with digit separator
    raw_sql := `SELECT id, email FROM users WHERE active = 1`

    // 2. Mutable bindings require 'mut'
    mut accumulator: i64 = 0
    accumulator += count
    accumulator <<= 1

    // 3. Constructor-style numeric conversions & native string interpolation
    scaled := f64(accumulator) * ratio
    pixel_x := f32(count) * 1.5
    low_byte := u8(mask & 0xFF)

    summary := "retries={MAX_RETRIES} acc={accumulator}"
    println("{} scaled={:.2f} px={:.1f} byte={} mask=0x{:x} sql_len={}",
        summary, scaled, pixel_x, low_byte, mask, raw_sql.len())
}
```
```text
# Output:
retries=5 acc=84 scaled=263.89 px=63.0 byte=0 mask=0xff00 sql_len=44
```

#### Scalar Types & Conversion Constructors
All scalar types have explicit bit widths, deterministic zero values (`mut x: i32` initializes to `0`), and built-in conversion constructors:

| `cfire` Type | Constructor / Cast | Bit Width & Representation | Default Zero Value | Emitted ISO C++23 Type |
| :--- | :--- | :--- | :---: | :--- |
| `i8`, `i16`, `i32`, `i64` | `i8(x)`, `i16(x)`, `i32(x)`, `i64(x)` | Signed integers (`i64` is default integer literal type) | `0` | `std::int8_t` .. `std::int64_t` |
| `u8` / `byte` | `u8(x)`, `byte(x)` | 8-bit unsigned (`0..255`) | `0` | `std::uint8_t` |
| `u16`, `u32`, `u64` | `u16(x)`, `u32(x)`, `u64(x)` | Unsigned 16/32/64-bit integers | `0` | `std::uint16_t` .. `std::uint64_t` |
| `usize`, `isize` | `usize(x)`, `isize(x)` | Pointer-sized unsigned / signed integers | `0` | `std::size_t`, `std::ptrdiff_t` |
| `f32`, `f64` | `f32(x)`, `f64(x)` | IEEE-754 single & double precision (`f64` default) | `0.0` | `float`, `double` |
| `bool` | `bool(x)` | Logical boolean (`true` / `false`) | `false` | `bool` |
| `str` | `"{x}"` / `str(x)` | Owned UTF-8 text buffer; slices `s[a..b]` are zero-copy views | `""` | `std::string` |

### Working with Slices (`[]T`), Fixed Arrays (`[N]T`) & Maps (`map[K]V`)
Three collection types are built directly into the language prelude (with `Set[T]`, `PriorityQueue[T]`, and `Deque[T]` available in `core:collections`):

```cfire
package main

fn main() {
    // 1. Dynamic Slice ([]i32 -> std::vector<int32_t>)
    mut scores: []i32 = [98, 84]
    scores.push(91)
    top_two := scores[0..2]

    // 2. Fixed-Size Stack Array ([3]f64 -> std::array<double, 3>)
    weights: [3]f64 = [1.0, 2.5, 0.75]

    // 3. Hash Map (map[str]i32 -> cfire::Map<std::string, int32_t>)
    mut ports: map[str]i32
    ports["http"] = 8080
    ports["https"] = 8443

    // Safe lookup: .get(key) returns V? without inserting a default key!
    http_port := ports.get("http") or 80
    smtp_port := ports.get("smtp") or 25
    println("top_two={} weight0={} http={} smtp={} has_tls={}",
        top_two.len(), weights[0], http_port, smtp_port, ports.contains("https"))
}
```
```text
# Output:
top_two=2 weight0=1 http=8080 smtp=25 has_tls=true
```

> [!TIP]
> **Why `ports.get(key) or default` Beats C++ `std::map::operator[]`**: In C++, reading `ports["smtp"]` silently mutates the map by inserting a default-constructed entry. In `cfire`, `ports.get("smtp")` is read-only and returns an optional `i32?`, which pairs naturally with `or 25`.

Additionally, all slice and array indexing (`scores[i]`) is checked at compile time for negative or constant out-of-bounds indices (`cfire/INDEX-OUT-OF-BOUNDS`) and verified at runtime via `cfire::index_at`. Any integer type can index (`grid[r][c]` with `r: i64` needs no cast); a negative index panics like an out-of-range one. Comparisons between signed and unsigned integers compare values, so `r < grid.len()` is `true` for `r = -1`.

Tuples and structs can be map keys and `Set`/`MinQueue` elements: tuples compare and hash element by element (`map[(i64, i64)]bool`), and a struct gets `==`, ordering and hashing automatically when every field has them.

### Unified `for` Loops, Ranges & Conditionals
Rather than juggling `for (;;)`, `while`, `do-while`, and `std::ranges::views::iota`, `cfire` uses a single `for` keyword across ranges, collections, and conditional loops:

```cfire
package main

fn main() {
    // 1. Half-open range (0..5 iterates 0, 1, 2, 3, 4)
    mut sum_exclusive: i64 = 0
    for i in 0..5 {
        sum_exclusive += i
    }

    // 2. Inclusive range (1..=5 iterates 1, 2, 3, 4, 5)
    mut sum_inclusive: i64 = 0
    for i in 1..=5 {
        if i == 3 {
            continue
        }
        sum_inclusive += i
    }

    // 3. Collection iteration over []T, [N]T, str, or Channel[T]
    items: []str = ["alpha", "beta", "gamma"]
    mut joined := ""
    for item in items {
        if !joined.is_empty() {
            joined = joined + ","
        }
        joined = joined + item
    }

    // 4. Conditional (while-style) loop
    mut countdown := 3
    for countdown > 0 {
        if countdown == 1 {
            break
        }
        countdown -= 1
    }

    println("excl={} incl={} items={} countdown={}",
        sum_exclusive, sum_inclusive, joined, countdown)
}
```
```text
# Output:
excl=10 incl=12 items=alpha,beta,gamma countdown=1
```

#### `if` as a Value, Indexed Loops & Map Entries
`if c { a } else { b }` is also an expression. The `else` branch is required, and both branches must have the same type (a bare number adapts to the other branch's numeric type). `for i, x in xs` visits each index with its element, and `for k, v in m` visits each map entry once. Map iteration order is unspecified.

```cfire
package main

fn main() {
    temps: []i64 = [12, -3, 25]
    mut labels: []str = []
    for i, t in temps {
        kind := if t < 0 { "freezing" } else if t < 20 { "mild" } else { "hot" }
        labels.push("{i}:{kind}")
    }

    mut stock: map[str]i64 = {}
    stock["apples"] = 3
    stock["pears"] = 5
    mut total: i64 = 0
    for name, count in stock {
        total += if name.starts_with("a") { count * 2 } else { count }
    }
    println("{} total={}", labels.join(" "), total)
}
```
```text
# Output:
0:mild 1:freezing 2:hot total=11
```

### Pointers (`*T`, `*mut T`) & RAII Smart Ownership (`^T` / `own[T]`, `shared[T]`)
When you need indirection or heap allocation, `cfire` separates **owning smart pointers** from **non-owning pointers and borrows** right in the syntax:

| Syntax | Creation Expression | Ownership & Mutation Semantics | Emitted ISO C++23 Type |
| :--- | :--- | :--- | :--- |
| `own[T]` | `make(own[T], val)` | Unique RAII heap ownership; movable, never implicitly copied | `std::unique_ptr<T>` |
| `shared[T]` | `make(shared[T], val)` | Atomic reference-counted shared heap ownership | `std::shared_ptr<T>` |
| `&T` / `&mut T` | `&x` / `&mut x` | Non-owning immutable or mutable borrow (`&str` -> `cfire::str_ref`, a `std::string_view`) | `const T&` / `T*` |
| `*T` | `&x` or foreign C pointer | Non-owning pointer; requires null check (`if p != none`) before deref | `T*` |
| `*mut T` | `&mut x` | Rebindable pointer to a mutable target; supports `*p = val` and `*p += 1` | `T*` |

Let's see how unique (`own[T]`), shared (`shared[T]`), and rebindable mutable pointers (`*mut T`) work together:

```cfire
package main

type Packet struct {
    sequence: i64
    payload:  str
}

fn main() {
    // 1. Unique heap ownership (std::unique_ptr<Packet>)
    unique_pkt := make(own[Packet], Packet{sequence: 1, payload: "SYN"})

    // 2. Shared reference-counted ownership (std::shared_ptr<Packet>)
    shared_pkt := make(shared[Packet], Packet{sequence: 2, payload: "ACK"})
    shared_clone := shared_pkt

    // 3. Rebindable mutable pointer (*mut i32) with compound deref assignment
    mut a: i32 = 10
    mut b: i32 = 20
    mut cursor: *mut i32 = &mut a
    *cursor += 5
    cursor = &mut b
    *cursor = 99

    println("seq1={} seq2={} a={} b={}",
        unique_pkt.sequence, shared_clone.sequence, a, b)
}
```
```text
# Output:
seq1=1 seq2=2 a=15 b=99
```

---

## Chapter 5: Functions, Call-Site Intent & Loan Safety

### Seeing Every Mutation & Move at the Call Site
If you have ever hunted down a bug in C++ where a helper function quietly modified a struct via a non-`const` reference (`void process(Config& cfg)`), you know the frustration: the function signature is buried in another file, and the call site (`process(cfg)`) looks completely read-only.

In `cfire`, parameters are immutable references by default. Whenever a function mutates an argument in place (`mut`) or takes ownership (`move`), **both the function declaration and the call site must state that intent**:

| Intent Modifier | Default? | Callee Access | Mandatory Call-Site Marker | Emitted ISO C++23 Lowering |
| :--- | :---: | :--- | :--- | :--- |
| *(omitted)* | **Yes** | Read-only borrow | None (`f(x)`) | Scalars by `const T`; structs/slices/maps by `const T&`; `str` by a zero-copy view (`cfire::str_ref`) |
| `mut` | No | Read-write in place | `mut` (`f(mut x)`) | `T&` parameter; caller variable must be declared `mut` |
| `move` | No | Takes ownership | `move` (`f(move x)`) | `T` parameter with `std::move(x)` at call site; invalidates `x` in caller scope |

```cfire
package main

type Buffer struct {
    Entries: []str
}

// 1. Immutable reference (default): read-only borrow (const Buffer&)
fn entry_count(buf: Buffer) -> usize {
    return buf.Entries.len()
}

// 2. 'mut' and 'move' intents: mutates 'buf' in place and consumes 'msg'
fn append_entry(prefix: str, mut buf: Buffer, move msg: str) -> usize {
    formatted := "[{prefix}] {msg}"
    buf.Entries.push(move formatted)
    return buf.Entries.len()
}

// 3. Multi-return tuple: returns count, success flag, and first entry
fn summarize(buf: Buffer) -> (usize, bool, str) {
    if buf.Entries.len() > 0 {
        return (buf.Entries.len(), true, buf.Entries[0])
    }
    return (0, false, "<empty>")
}

fn main() {
    mut log := Buffer{}
    payload := "handshake verified"

    // Call site documents that 'log' is mutated and 'payload' is moved:
    _ = append_entry("NET", mut log, move payload)

    (total, ok, head) := summarize(log)
    println("count={} total={} ok={} head={}", entry_count(log), total, ok, head)
}
```
```text
# Output:
count=1 total=1 ok=true head=[NET] handshake verified
```

> [!TIP]
> **Let `cfire fix` Add Missing Call-Site Markers**: If you write `append_entry("NET", log, move payload)` and forget `mut` at the call site, `cfire check` flags `cfire/MUT-CALLSITE` with the exact column—and running `cfire fix` (or triggering the LSP code action in your editor) inserts `mut` automatically.

### Closures, Trailing Blocks & Zero-Overhead Generics
When a function's last parameter is a callback (`fn(...)`), you can pass a pipe-delimited closure (`|arg| { ... }`) after the closing parenthesis using **Trailing Closure Syntax**. This is the same mechanism used by `app.bind[T]("route") |req| { ... }` in `ext:webui` and `scope.spawn() || { ... }` in `core:sync`:

```cfire
package main

fn for_each_above(items: []i32, threshold: i32, visit: fn(i32)) {
    for x in items {
        if x >= threshold {
            visit(x)
        }
    }
}

fn main() {
    mut total: i32 = 0
    scores: []i32 = [10, 50, 25, 80]

    // Generic function + trailing closure syntax
    for_each_above(scores, 30) |score| {
        total += score
    }

    println("filtered_sum={}", total)
}
```
```text
# Output:
filtered_sum=130
```

Generic type parameters (`[T]`) lower 1-to-1 to C++23 `template <typename T>` functions—giving you full monomorphization with zero heap boxing or virtual dispatch.

### Loan Tracking: How `cfire` Prevents Dangling Pointers & Iterator Invalidation
One of the easiest ways to corrupt memory in C++ is taking a pointer, slice view, or iterator into a `std::vector` or `std::string`, and then calling `.push_back()` or `.clear()` while that view is still live. If the vector reallocates, the pointer dangles silently.

`cfire` solves this without requiring lifetime annotations (`'a`) on your types or functions. Its lexical loan tracker (`src/sema.hpp`) watches local borrows (`&x`, `s[a..b]`, `.data()`, `.byte_ptr()`) and active `for` loops:

```cfire
package main

fn main() {
    mut nums: []i32 = [10, 20, 30]

    // 1. Taking a view or pointer records an active loan on 'nums'
    view := nums[0..2]
    println("first_in_view={}", view[0])

    // 2. Because 'view' is never read again after line 9, its loan ends!
    //    Mutating 'nums' here is 100% safe and accepted:
    nums.push(40)
    println("new_len={}", nums.len())
}
```

If you tried to read `view[0]` *after* `nums.push(40)`, the compiler immediately halts with `cfire/USE-AFTER-FREE`. Similarly, if you try to call `nums.push(...)` or `nums.clear()` *inside* `for x in nums { ... }`, the compiler rejects it with `cfire/ITERATOR-INVALIDATION`.

#### Summary of Intent & Loan Safety Rules in Chapter 5

| Diagnostic Rule | What It Catches at Compile Time | How to Fix It |
| :--- | :--- | :--- |
| `cfire/MUT-CALLSITE` | Calling a `mut` parameter without writing `mut` at the call site. | Write `f(mut x)` (or run `cfire fix`). |
| `cfire/MOVE-CALLSITE` | Calling a `move` parameter without writing `move` at the call site. | Write `f(move x)` to confirm ownership transfer. |
| `cfire/IMMUTABLE-MUT-ARG` | Passing an immutable binding (`x := ...`) to a `mut` parameter. | Declare `mut x := ...`. |
| `cfire/MUT-RECEIVER` | Calling a `fn (mut s: T)` method on an immutable struct variable. | Declare `mut s := ...`. |
| `cfire/MUT-ALIAS` | Passing the same variable twice in one call when at least one arg is `mut`. | Pass distinct variables (`transfer(mut a, mut b)`). |
| `cfire/USE-AFTER-MOVE` | Reading a variable after `move x` transferred ownership on an earlier path. | Read before `move x`, or pass by `mut` borrow. |
| `cfire/USE-AFTER-FREE` | Reading a pointer/view (`&x`, `.data()`, `.byte_ptr()`, `s[a..b]`) after `.push()`, `.clear()`, `move`, or reassignment mutated the owner. | Finish reading the view before mutating the container, or re-slice after `.push()`. |
| `cfire/DANGLING-POINTER` | Assigning the address of an inner-scope variable (`ptr = &local`) to an outer pointer. | Keep the pointee in the outer scope or allocate with `make(own[T], ...)`. |
| `cfire/DANGLING-RETURN` | Returning a pointer or view (`&local`, `local.data()`) to stack-local storage. | Return an owned value (`T`, `str`, `[]T`) or `own[T]` instead. |
| `cfire/ITERATOR-INVALIDATION` | Mutating a collection (`.push()`, `.clear()`, `.remove()`, `move`) inside `for x in col`. | Collect mutations in a separate slice and apply them after the loop. |

---

## Chapter 6: Structs, Tags, Enums & Interfaces

### Structs, Default Field Values & Receiver Methods
Structs in `cfire` map directly to standard-layout C++23 `struct` types. You can give fields default values right in the declaration, construct instances with named fields in any order, and attach methods using value (`fn (s: T)`) or mutable (`fn (mut s: T)`) receivers:

```cfire
package main

type Endpoint struct {
    Host:       str  = "127.0.0.1"
    Port:       u16  = 8080
    TlsEnabled: bool = false
    Requests:   i64  = 0
}

// 1. Read-only receiver: borrows 'e' as 'const Endpoint&'
fn (e: Endpoint) scheme() -> str {
    if e.TlsEnabled {
        return "https"
    }
    return "http"
}

// 2. Mutable receiver: mutates 'e' in place ('Endpoint&')
fn (mut e: Endpoint) record_hit(count: i64) {
    e.Requests += count
}

fn main() {
    default_ep := Endpoint{}
    mut prod_ep := Endpoint{Host: "api.cfire.dev", Port: 443, TlsEnabled: true}
    prod_ep.record_hit(42)

    println("default={}://{}:{} prod={}://{}:{} hits={}",
        default_ep.scheme(), default_ep.Host, default_ep.Port,
        prod_ep.scheme(), prod_ep.Host, prod_ep.Port, prod_ep.Requests)
}
```
```text
# Output:
default=http://127.0.0.1:8080 prod=https://api.cfire.dev:443 hits=42
```

### Compile-Time Struct Tags (`json`, `sql`, `cli`, `desc`)
In C++, serializing a struct to JSON, mapping it from a SQLite row, or generating an OpenAI/MCP tool schema usually requires external code generators or heavy macro boilerplate.

In `cfire`, you attach backtick tags directly to your struct fields (or even leave them untagged—`cfire` automatically reflects all struct field names at compile time via `cfire::reflect_struct<T>`!):

| Tag Key | Consuming Modules | Example | What It Configures at Compile Time |
| :--- | :--- | :--- | :--- |
| `json` | `core:json`, `ext:webui`, `ext:ai` | `` `json:"user_id"` `` | Maps the field to JSON key `"user_id"` in `json.parse[T]`, `json.stringify`, `app.bind[T]`, and `json.schema[T]()`. |
| `sql` | `ext:db/sqlite` | `` `sql:"user_id"` `` | Maps SQLite result column `"user_id"` into the struct field during `db.query[T](sql, ...)`. |
| `cli`, `env`, `default` | `core:cli` | `` `cli:"--port,-p" env:"PORT" default:"8080"` `` | Binds CLI flags, environment variable fallbacks, and default values in `cli.parse[T]()`. |
| `desc` | `core:cli`, `ext:ai` | `` `desc:"Listen port"` `` | Populates `--help` text in `core:cli` and the `"description"` property in `json.schema[T]()` for LLM tool schemas. |
| `validate` | `core:json`, `ext:ai` | `` `validate:"min=1,max=5"` `` | Emits JSON Schema constraints (`minimum`, `maximum`, `minLength`, `maxLength`, `minItems`, `maxItems`, `enum`) in `json.schema[T]()` and enforces runtime bounds in `json.parse[T]()` (`min`, `max`, `min_len`, `max_len`, `non_empty`, `oneof=a|b|c`). |

```cfire
package main

import "core:json"

type UserRecord struct {
    Id:     i64  `json:"id"     sql:"id"     desc:"Unique user identifier"`
    Email:  str  `json:"email"  sql:"email"  desc:"Verified email address"`
    Active: bool `json:"active" sql:"active" desc:"Account status"`
}

fn main() {
    u := UserRecord{Id: 7, Email: "dev@cfire.io", Active: true}
    payload := json.stringify(u)
    schema := json.schema[UserRecord]()
    println("json={}", payload)
    println("has_desc={}", schema.contains("Unique user identifier"))
}
```
```text
# Output:
json={"id":7,"email":"dev@cfire.io","active":true}
has_desc=true
```

### Generic Structs & Algebraic Enums
In addition to generic functions, `cfire` allows you to declare generic structs and enums with type parameters and optional interface constraints (`[T]` or `[T: Constraint]`):

```cfire
package main

type Box[T] struct {
    Item: T
}

type Outcome[T, E] enum {
    Success(T),
    Failure(E),
}

fn main() {
    b := Box[str]{Item: "hello generic world"}
    res := Outcome[i32, str]::Success(42)
    println("boxed={} ok={}", b.Item, match res {
        Outcome::Success(val) => val,
        Outcome::Failure(err) => -1,
    })
}
```

### Algebraic Enums, Or-Patterns, Slice Patterns & Exhaustive `match`
Enums in `cfire` can be simple C-style tags or full algebraic sum types carrying typed payloads (lowered to `std::variant<...>`). When you `match` on an enum, string, or slice, the compiler verifies:
1. **Exhaustiveness (`cfire/NON-EXHAUSTIVE-MATCH`)**: Every variant of an enum must be handled (or covered by a `_ =>` wildcard arm).
2. **Reachability (`cfire/UNREACHABLE-MATCH-ARM`)**: No arm can be shadowed by an earlier duplicate variant, wildcard, or broader slice rest pattern (`[head, ..tail]`).
3. **Or-Patterns (`|`)**: You can match multiple alternative values or variants in a single arm (`"GET" | "HEAD" =>`, `Event::Connected | Event::Disconnected =>`).

#### Best Practices: Prefer Enums Over Integer Constants
A common anti-pattern in systems programming is defining clusters of integer constants for state machines or modes (e.g. `VIEW_LOGIN = 0`, `VIEW_DASHBOARD = 1`, `VIEW_SETTINGS = 2`). This sacrifices compile-time exhaustiveness checking and makes refactoring error-prone.

The `cfire` semantic analyzer actively detects clusters of 3+ identically prefixed integer constants and emits a `cfire/PREFER-ENUM` diagnostic with an automatic LSP quick-fix code action to refactor them into an `enum`. Prefer using enums or strings directly with `match`:

```cfire
package main

type HttpMethod enum {
    Get,
    Post,
    Put,
    Delete,
}

fn is_idempotent(m: HttpMethod) -> bool {
    return match m {
        HttpMethod::Get | HttpMethod::Put | HttpMethod::Delete => true,
        HttpMethod::Post                                       => false,
    }
}

fn route(path: str) -> str {
    return match path {
        "/" | "/index" | "/home" => "landing",
        "/about"                 => "about",
        _                        => "404",
    }
}
```

```cfire
package main

type Event enum {
    Connected(str),
    Data([]byte),
    Resize(i32, i32),
    Disconnected(i32),
}

fn describe_event(e: Event) -> str {
    return match e {
        Event::Connected(addr)     => "connected:" + addr,
        Event::Data(payload)       => "bytes:{payload.len()}",
        Event::Resize(w, h)        => "resize:{w}x{h}",
        Event::Disconnected(code)  => "closed:{code}",
    }
}

fn classify_path(segments: []str) -> str {
    match segments {
        []                   => { return "root" }
        [only]               => { return "single:" + only }
        [first, ..mid, last] => { return "{first}->..({mid.len()})..->{last}" }
    }
    return "unreachable"
}

fn main() {
    ev := Event::Resize(1920, 1080)
    parts: []str = ["api", "v1", "users", "profile"]
    println("event={} route={}", describe_event(ev), classify_path(parts))
}
```
```text
# Output:
event=resize:1920x1080 route=api->..(2)..->profile
```

### Structural Interfaces & C++20 `concept` Lowering
Interfaces in `cfire` are structural: any `struct` that implements the required methods satisfies the interface automatically without an `implements` keyword. When you constrain a generic function with `[T: Measurable]`, `cfire` lowers the interface directly to a zero-overhead C++20 `concept`—no `vtable` pointer indirection or heap allocation required:

```cfire
package main

type Measurable interface {
    fn weight_kg() -> f64
}

type Crate struct {
    Items:    i32
    UnitMass: f64
}

fn (c: Crate) weight_kg() -> f64 {
    return f64(c.Items) * c.UnitMass
}

fn shipping_cost[T: Measurable](parcel: T, rate_per_kg: f64) -> f64 {
    return parcel.weight_kg() * rate_per_kg
}

fn main() {
    box := Crate{Items: 8, UnitMass: 2.5}
    println("total_cost=${:.2f}", shipping_cost(box, 1.75))
}
```
```text
# Output:
total_cost=$35.00
```

---

## Chapter 7: Error Handling, Optionals, Null Safety & Defer

### Separating Errors from Absence (`T!` vs `T?`)
We avoid C++ exceptions (`try`/`catch` stack unwinding) for a straightforward reason: hidden control flow makes systems code harder to reason about and adds unpredictable latency on the failure path. At the same time, returning `-1` or `nullptr` and hoping the caller remembers to check `errno` is how bugs slip into production.

`cfire` separates **recoverable failures** (`T!`) from **valid absence** (`T?`) directly in the return type, and lowers both to flat value types on the stack:

| Type Syntax | What It Represents | Emitted ISO C++23 Type | Construction | Unwrapping & Recovery |
| :--- | :--- | :--- | :--- | :--- |
| `T!` | Fallible result (either a valid `T` or a `cfire::Error` message/code) | `std::expected<T, cfire::Error>` | Return `val` or `return error("msg")` | `try expr`, `expr or default_val`, `expr or err { ... return }` |
| `void!` | Fallible procedure (succeeds or returns `cfire::Error`) | `std::expected<void, cfire::Error>` | Implicit success at end of fn or `return error("msg")` | `try expr`, `expr or err { ... }` |
| `T?` | Optional value (either a present `T` or `none` / `nil`) | `std::optional<T>` | Assign `val` or `none` (`nil`) | `expr or default_val`, or guard with `if x != none` |

### Propagating Errors with `try` and Recovering with `or`
When a function can fail, mark its return type with `!`. Inside any fallible function, `try expr` unwraps the happy path or immediately returns the error to your caller—running any active `defer` blocks in reverse order on the way out.

When you want to handle an error or provide a fallback right at the call site, use `or`:

```cfire
package main


fn parse_port(raw: str) -> u16! {
    cleaned := raw.trim()
    if cleaned.is_empty() {
        return error("port string is empty")
    }
    if cleaned == "8080" {
        return u16(8080)
    }
    if cleaned == "443" {
        return u16(443)
    }
    return error("unsupported port: " + cleaned)
}

fn lookup_limit(tier: str) -> i64? {
    if tier == "pro" { return 10000 }
    if tier == "free" { return 100 }
    return none
}

fn build_endpoint(host: str, raw_port: str) -> str! {
    // 'try' unwraps u16 on success or immediately returns the Error to the caller
    port := try parse_port(raw_port)
    return "{host}:{port}"
}

fn compute_quota(tier: str, raw_port: str) -> i64 {
    // Form 1: Inline default value on an optional (T?)
    base := lookup_limit(tier) or 50

    // Form 2: Diverging recovery block on a fallible (T!) with bound 'err'
    port := parse_port(raw_port) or err {
        println("warn: recovering from invalid port ({})", err.message())
        return base
    }

    return base + i64(port)
}

fn main() {
    ep := build_endpoint("127.0.0.1", " 8080 ") or "0.0.0.0:80"
    q1 := compute_quota("pro", "443")
    q2 := compute_quota("guest", "bad")
    println("ep={} q1={} q2={}", ep, q1, q2)
}
```
```text
# Output:
warn: recovering from invalid port (unsupported port: bad)
ep=127.0.0.1:8080 q1=10443 q2=50
```

> [!IMPORTANT]
> **Divergence Rule (`cfire/NON-DIVERGING-OR-BLOCK`)**: When you bind a variable with `x := fallible() or err { ... }`, the `{ ... }` recovery block **must** exit the current control path via `return`, `break`, `continue`, `panic(...)`, or `exit(...)`. That invariant guarantees `x` can never be read in an uninitialized state after the block.

### Flow-Sensitive Null Pointer Safety (`cfire/NULL-DEREF`)
When you work with heap pointers (`*T` or rebindable `*mut T`), a pointer initialized to `none` (or returned from a function that might yield `none`) cannot be dereferenced blindly.

The semantic analyzer tracks pointer nullability across branches (`cfire/NULL-DEREF`). If a pointer might be `none`, dereferencing `*p` or accessing `p.field` is a compile-time error until you prove it is non-null with `if p != none` (or `assert(p != none)`):

```cfire
package main

type Session struct {
    id: i32
}

fn inspect_session(maybe_ptr: *Session) -> i32 {
    // Error if uncommented: [cfire/NULL-DEREF] dereference of potentially null pointer 'maybe_ptr'
    // return maybe_ptr.id

    if maybe_ptr != none {
        // Inside this guarded branch, the compiler knows maybe_ptr is non-null
        return maybe_ptr.id
    }
    return -1
}

fn main() {
    s := Session{ id: 42 }
    println("active={} empty={}", inspect_session(&s), inspect_session(none))
}
```
```text
# Output:
active=42 empty=-1
```

### Deterministic Scope Cleanup with `defer` (`cfire/RETURN-IN-DEFER`)
Whenever you open a file, lock a resource, or spin up a temporary database handle, write its `defer` cleanup on the very next line. `defer <stmt>` and `defer { ... }` schedule cleanup statements to run in last-in, first-out (LIFO) order when the lexical block exits—whether at the closing `}`, an early `return`, or an error propagated via `try`:

```cfire
package main

import "core:fs"
import "ext:db/sqlite"

fn export_snapshot(db_path: str, log_path: str) -> void! {
    mut db := try sqlite.open(db_path)
    defer db.close() // Registered 1st -> Executes 2nd (LIFO)

    mut log_file := try fs.open(log_path, fs.OpenMode::Write)
    defer {
        _ = log_file.flush()
        log_file.close() // Registered 2nd -> Executes 1st (LIFO)
    }

    try db.exec("CREATE TABLE IF NOT EXISTS events (id INT, name TEXT)")
    try db.exec("INSERT INTO events VALUES (1, 'boot')")
    _ = try log_file.write_text("snapshot_ok\n")
}

fn main() {
    export_snapshot(":memory:", "/tmp/cfire_guide_defer.log") or err {
        println("export failed: {}", err.message())
        return
    }
    text := try fs.read_text("/tmp/cfire_guide_defer.log")
    _ = fs.remove("/tmp/cfire_guide_defer.log")
    print("{}", text)
}
```
```text
# Output:
snapshot_ok
```

Each `defer` lowers to a stack-allocated `cfire::ScopeGuard` lambda whose destructor runs deterministically during C++ stack unwinding. Note that placing a `return` statement inside a `defer { ... }` block is rejected at compile time (`cfire/RETURN-IN-DEFER`), because silently hijacking a function's return value during cleanup masks upstream errors.

---

# Part III: Core Stdlib & Concurrency

## Chapter 8: Strings, Buffers, Unicode, Regex & Containers

### UTF-8 Strings, Unicode Scalars & Regular Expressions
In `cfire`, `str` is an owned UTF-8 buffer (`std::string`). Slicing (`s[a..b]`, `s.trim()`) gives a zero-copy view of it, and you rarely need to name that view type (`&str`): **views go in, copies come out**. A read-only `str` parameter accepts an owned string, a slice, or a literal without copying, and a view is copied automatically wherever owned text is required (a `move` parameter, a struct field, a `str` variable, a return value). Spell `&str` only for a field or return value that deliberately keeps a view. `s[i]` is a `byte`, and `'L'` is a `byte` literal (one ASCII character or an escape such as `'\n'`, `'\''`, `'\x41'`), so `line[0] == 'L'` needs no conversion. Because UTF-8 codepoints vary from 1 to 4 bytes, `s.len()` always reports the exact byte count in $O(1)$ time, while `core:unicode` decodes 32-bit Unicode scalar values (`runes`) when you need character-level inspection:

| Module | Function / Method | Signature | What It Does |
| :--- | :--- | :--- | :--- |
| builtin | `s.split(sep)` / `s.split_n(sep, n)` / `s.lines()` / `parts.join(sep)` | `fn(sep: &str) -> []str` / `fn(sep: &str) -> str` | Splits `s` on `sep` (or on newlines) or joins a `[]str` with `sep`. |
| builtin | `s.trim()` / `trim_start()` / `trim_end()` | `fn() -> &str` | View without leading and/or trailing ASCII whitespace (`' '`, `'\t'`, `'\n'`, `'\r'`); loan-tracked like `s[a..b]`. |
| builtin | `s.strip_prefix(p)` / `strip_suffix(p)` | `fn(p: &str) -> &str?` | View after the prefix / before the suffix, or `none` when it is absent. |
| builtin | `s.replace(from, to)` / `replace_all(from, to)` | `fn(from: &str, to: &str) -> str` | Copy with the first / every occurrence of `from` replaced. |
| builtin | `s.starts_with(p)` / `ends_with(p)` / `contains(p)` | `fn(p: &str) -> bool` | Prefix, suffix, and substring containment checks. |
| builtin | `s.index_of(sub)` / `last_index_of(sub)` | `fn(sub: &str) -> usize?` | The 0-based byte offset of `sub`, or `none` when it is absent (Q8: no `-1` sentinels). |
| builtin | `s.to_upper()` / `to_lower()` / `repeat(n)` | `fn() -> str` / `fn(n: usize) -> str` | ASCII case conversion and `n`-fold repetition. |
| builtin | `s.runes()` / `rune_count()` | `fn() -> []u32` / `fn() -> usize` | Unicode code points (invalid UTF-8 decodes as U+FFFD). |
| builtin | `i32.parse(s)` / `f64.parse(s)` / `bool.parse(s)` | `fn(s: str) -> T?` | Parses a whole literal of the target type; `none` on invalid or out-of-range input. |
| `core:unicode` | `unicode.is_valid_utf8` / `rune_count` | `fn(s: str) -> bool` / `fn(s: str) -> usize` | Validates RFC 3629 UTF-8 byte sequences and counts Unicode codepoints. |
| `core:unicode` | `unicode.runes` / `encode_rune` | `fn(s: str) -> []u32` / `fn(r: u32) -> str` | Decodes UTF-8 into 32-bit scalar values (`u32`) or encodes a codepoint to UTF-8. |
| `core:unicode` | `unicode.to_lower` / `to_upper` | `fn(s: str) -> str` | Full Unicode case folding across Latin, Greek, and Cyrillic scripts. |
| `core:unicode` | `unicode.is_letter` / `is_digit` / `is_space` | `fn(r: u32) -> bool` | Unicode general category classification predicates on a 32-bit rune. |
| `core:regex` | `regex.compile` | `fn(pattern: str) -> Regex!` | Compiles an ECMAScript regular expression (`std::regex`), returning an error on invalid syntax. |
| `core:regex` | `regex.is_match` / `find` / `find_all` | `fn(pat: str, text: str) -> bool!` / `str?!` / `[]str!` | One-shot forms: tests for a match, or extracts the first or all non-overlapping matches. |
| `core:regex` | `re.is_match(text)` / `re.find(text)` / `re.find_all(text)` | `-> bool` / `-> str?` / `-> []str` | The same on a compiled `Regex` from `regex.compile`, for patterns used in a loop. |
| `core:regex` | `regex.replace_all` | `fn(pat: str, text: str, replacement: str) -> str!` | Replaces every match of `pat` in `text` with `replacement`. |

```cfire
package main

import "core:regex"
import "core:unicode"

fn main() -> void! {
    raw := "  node_01, node_02, node_03  "
    parts := raw.trim().split(", ")
    joined := parts.join("|")

    banner := "cfire 🔥"
    runes := unicode.runes(banner)

    cleaned := try regex.replace_all(`[0-9]+`, joined, "#")
    println("joined={} runes={} bytes={} regex={}",
        joined, unicode.rune_count(banner), banner.len(), cleaned)
    println("last_rune=U+{:X} valid_utf8={}",
        runes[runes.len() - 1], unicode.is_valid_utf8(banner))
}
```
```text
# Output:
joined=node_01|node_02|node_03 runes=7 bytes=10 regex=node_#|node_#|node_#
last_rune=U+1F525 valid_utf8=true
```

Here is a small line parser in that style: slices go straight into `parse`, a helper takes `str`, and the byte literal compares with `line[0]`:

```cfire
package main

fn amount(s: str) -> i64 {
    return i64.parse(s.trim()) or 0
}

fn main() {
    lines := ["L68", "R48", " L5"]
    mut dial: i64 = 50
    for raw in lines {
        line := raw.trim()
        if line[0] == 'L' {
            dial = (dial - amount(line[1..])).rem_euclid(100)
        } else {
            dial = (dial + amount(line[1..])).rem_euclid(100)
        }
    }
    println("dial={}", dial)
}
```
```text
# Output:
dial=25
```

### Zero-Copy Buffer Pointers & Compile-Time Bounds Checks
When you pass buffers to C APIs, socket layers, or SIMD loops, copying a `str` into a new `[]byte` vector just to read its bytes wastes memory bandwidth. Strings and slices expose built-in zero-copy pointer and byte-slice methods:

| Method | Receiver | Return Type | Behavior |
| :--- | :--- | :--- | :--- |
| `s.bytes()` | `str` / `&str` | `[]byte` | Copies the UTF-8 bytes of `s` into a `std::vector<uint8_t>` (`[]byte`). |
| `s.data()` / `s.byte_ptr()` | `str` | `const char*` / `char*` | Direct pointer to the underlying contiguous UTF-8 byte buffer (`s.data()`). |
| `s.byte_ptr_at(offset)` | `str` | `char*` | Pointer to byte `offset` (`s.data() + offset`), Useful when advancing through a read/write buffer in chunks. |
| `slice.data()` / `slice.data_ptr()` | `[]T` / `mut []T` | `const T*` / `T*` | Direct pointer to the contiguous backing storage of a slice (`std::vector<T>::data()`). |

At the same time, the compiler guards slice/array indexing and arithmetic division against constant-folded bugs before your binary ever runs:
- **`cfire/INDEX-OUT-OF-BOUNDS`**: Rejects negative index expressions (`items[-1]`) or constant indices that exceed a known array/slice length (`arr[5]` on `[4]i32`).
- **`cfire/DIVIDE-BY-ZERO`**: Rejects integer division (`/`) or remainder (`%`) where the divisor evaluates to constant `0`.

```cfire
package main

fn main() {
    mut packet := "PING:1024"
    // Zero-copy pointer offset into the payload after "PING:"
    payload_ptr := packet.byte_ptr_at(5)
    raw_bytes := packet.bytes()

    mut samples: []f32 = [0.25, 0.5, 0.75, 1.0]
    buf_ptr := samples.data_ptr()

    println("bytes={} first_payload_byte={} first_sample={:.2f}",
        raw_bytes.len(), i32(*payload_ptr), *buf_ptr)
}
```
```text
# Output:
bytes=9 first_payload_byte=49 first_sample=0.25
```

### Generic Sets, Priority Queues, Deques & Sorting
Beyond built-in slices (`[]T`) and hash maps (`map[K]V`), `core:collections` gives you generic hash sets, max-first and min-first priority queues, and double-ended ring queues; sorting and searching are methods on the slice itself:

| Module | Type / Function | Operations / Signature | C++23 Backing |
| :--- | :--- | :--- | :--- |
| `core:collections` | `collections.Set[T]` | `.insert(v)`, `.contains(v) -> bool`, `.remove(v)`, `.len() -> usize`, `.clear()` | `std::unordered_set<T>` |
| `core:collections` | `collections.PriorityQueue[T]` | `.push(v)`, `.pop() -> T?`, `.peek() -> T?`, `.top() -> T`, `.empty() -> bool`, `.len() -> usize` | `std::priority_queue<T>` (largest first) |
| `core:collections` | `collections.MinQueue[T]` | same as `PriorityQueue[T]`, smallest first (`MinQueue[(i64, i64)]` for Dijkstra `(cost, node)` states) | `std::priority_queue<T, std::vector<T>, std::greater<T>>` |
| `core:collections` | `collections.Deque[T]` | `.push_front(v)`, `.push_back(v)`, `.pop_front() -> T`, `.pop_back() -> T`, `.len() -> usize` | `std::deque<T>` |
| builtin | `xs.sort()` / `xs.reverse()` | sorts `xs` ascending or reverses it in place (needs a `mut` binding) | `std::sort` / `std::reverse` |
| builtin | `xs.sort_by(less)` | `fn(less: fn(T, T) -> bool)` — custom comparator sort | `std::sort(xs.begin(), xs.end(), less)` |
| builtin | `xs.is_sorted()` / `xs.binary_search(v)` | `fn() -> bool` / `fn(v: T) -> usize?` | `std::is_sorted` / `std::lower_bound` |
| builtin | `xs.contains(v)` / `index_of(v)` / `first()` / `last()` | `-> bool` / `-> usize?` / `-> T?` | linear search and safe ends |
| builtin | `xs.insert(i, v)` / `remove(i)` / `swap_remove(i)` / `pop()` / `truncate(n)` / `extend(other)` | mutators; out-of-range indices panic; every one invalidates loans | `std::vector` |

```cfire
package main

import "core:collections"

fn main() {
    mut latencies: []i64 = [42, 7, 19, 88, 12]
    latencies.sort()
    idx := latencies.binary_search(19) or -1

    mut pq: collections.PriorityQueue[i64]
    for v in latencies { pq.push(v) }
    mut open: collections.MinQueue[(i64, str)]
    open.push((5, "far"))
    open.push((1, "near"))
    (cost, name) := open.pop() or (0, "")

    mut dq: collections.Deque[str]
    dq.push_back("worker-1")
    dq.push_front("leader-0")

    mut seen: collections.Set[str]
    seen.insert("us-east")
    seen.insert("eu-west")

    println("min={} idx_of_19={} max_pq={} nearest={}:{} front={} regions={}",
        latencies[0], idx, pq.top(), name, cost, dq.pop_front(), seen.len())
}
```
```text
# Output:
min=7 idx_of_19=2 max_pq=88 nearest=near:1 front=leader-0 regions=2
```

---

## Chapter 9: Filesystem, OS, Time, CLI & Logging

### Streaming File I/O, Clocks & OS Entropy (`core:fs`, `core:time`, `core:rand`)
Whether your code runs on a Linux server, macOS desktop, or inside the iOS Simulator, `core:fs`, `core:time`, `core:math`, and `core:rand` give you a single system interface (automatically resolving writable `Documents/` sandbox paths and read-only bundle assets when compiled with `--ios`):

| Module | Function / Method | Signature | Description |
| :--- | :--- | :--- | :--- |
| `core:fs` | `fs.read_text` / `write_text` / `append_text` | `fn(path: str) -> str!` / `fn(path: str, text: str) -> void!` | One-shot UTF-8 text file read, write, and append. |
| `core:fs` | `fs.read_bytes` / `write_bytes` / `append_bytes` | `fn(path: str) -> []byte!` / `fn(path: str, data: []byte) -> void!` | One-shot binary file read, write, and append. |
| `core:fs` | `fs.open(path, mode)` | `fn(path: str, mode: OpenMode) -> File!` | Opens a streaming `File` handle (`OpenMode::Read`, `Write`, `Append`, `ReadWrite`) supporting `.read_text()`, `.read_bytes()`, `.read_into()`, `.read_exact()`, `.read_text_until()`, `.read_bytes_until()`, `.read_line()`, `.read_lines()`, `.write_text()`, `.write_bytes()`, `.seek()`, `.tell()`, `.flush()`, `.close()`. |
| `core:fs` | `fs.exists` / `is_dir` / `is_file` / `file_size` / `copy_file` / `mkdir_all` / `remove` / `list_dir` | `fn(path: str) -> ...` | Path inspection, file copying, recursive directory creation, removal, and directory listing. |
| `core:os` | `os.stdin` / `os.stdout` / `os.stderr` / `os.pipe` | `fn() -> fs.File` / `fn() -> (fs.File, fs.File)!` | Non-owning `fs.File` stream handles for stdio (`0`, `1`, `2`) and owning `(reader, writer)` OS pipe pairs. |
| `core:os` | `os.getenv` / `os.setenv` / `os.cwd` | `fn(key: str, fallback: str = "") -> str` | Reads or sets process environment variables and queries the working directory. |
| `core:time` | `time.now_ms` / `now` / `parse_rfc3339` / `sleep_ms` | `fn() -> i64` / `Time` / `Time!` | Monotonic millisecond clock, wall-clock `Time` with `.format_rfc3339()`, and thread sleep. |
| `core:rand` | `rand.u64` / `range_i64` / `f64_unit` / `bytes` | `fn(n: usize) -> []byte` | Thread-local `MT19937_64` draws and OS-entropy CSPRNG byte slices (`rand.bytes`). |
| `core:math` | `math.PI` / `E` / `TAU` / `SQRT_2` and `math.atan2` / `hypot` / `gcd` / `lcm` | `f64` constants / multi-input functions | Constants and functions with no natural receiver; single-value math is a method (`x.sqrt()`, `x.sin()`, `x.clamp(lo, hi)`, `a.checked_add(b)`). |

```cfire
package main

import "core:fs"
import "core:math"
import "core:os"
import "core:rand"
import "core:time"

fn main() -> void! {
    t0 := time.now_ms()
    tmp := "/tmp/cfire_sys_demo.txt"
    defer { _ = fs.remove(tmp) }

    mut f := try fs.open(tmp, fs.OpenMode::Write)
    _ = try f.write_text("host=" + os.getenv("USER", "ci") + "\n")
    f.close()

    content := try fs.read_text(tmp)
    token := rand.bytes(16)
    elapsed := time.now_ms() - t0
    println("has_host={} token_bytes={} hypot={:.1f} elapsed>={}",
        content.contains("host="), token.len(), (9.0 + 16.0).sqrt(), elapsed >= 0)
}
```
```text
# Output:
has_host=true token_bytes=16 hypot=5.0 elapsed>=true
```

### Safe Subprocess Execution vs Shell Injection (`cfire/SHELL-INJECTION-RISK`)
Shelling out via `/bin/sh -c` with string-concatenated arguments (`os.exec("tar -xf " + user_file)`) is one of the oldest remote-code-execution vectors in systems programming: if `user_file` contains `; rm -rf /`, the shell happily runs both commands.

`cfire` enforces subprocess safety at two levels:
1. **Compile-Time Guard (`cfire/SHELL-INJECTION-RISK`)**: Passing a dynamically concatenated or interpolated string to `os.exec(...)` or `os.exec_output(...)` triggers `cfire/SHELL-INJECTION-RISK` during `cfire check` and `cfire build`.
2. **Direct `posix_spawnp` Execution (`os.command(binary, args)`)**: Always prefer `os.command(bin, args)`. It spawns the binary directly via `posix_spawnp` without invoking `/bin/sh`, passing each element of `args: []str` as a verbatim `argv` entry and returning `ExecResult { exit_code, stdout, stderr }`:

```cfire
package main

import "core:os"

fn main() -> void! {
    // Spawns /bin/echo directly via posix_spawnp — shell metacharacters are inert literals
    res := try os.command("echo", ["hello; uname -a", "safe-argv"])
    println("exit={} output={}", res.exit_code, res.stdout.trim())
}
```
```text
# Output:
exit=0 output=hello; uname -a safe-argv
```

### Compile-Time CLI Flag Parsing & Structured Logging (`core:cli`, `core:log`)
Instead of writing imperative flag-parsing loops for every tool, tag a struct with `` `cli:"--flag,-f" env:"VAR" default:"..." desc:"..."` `` and call `cli.parse[T]()`. Pair it with `core:log` to emit leveled logs in human-readable text during local development or structured JSON in production:

| Module | Function / Method | Signature | Description |
| :--- | :--- | :--- | :--- |
| `core:cli` | `cli.parse[T](args)` | `fn[T](args: []str? = none) -> T!` | Parses `argc`/`argv` (or an explicit `[]str` slice) into struct `T` using `cli`, `env`, and `default` tags. |
| `core:cli` | `cli.help[T](prog_name)` | `fn[T](prog_name: str = "") -> str` | Generates a formatted `--help` usage screen from struct `T`'s compile-time reflection tags. |
| `core:log` | `log.set_level(lvl)` / `set_format(fmt)` | `fn(Level)` / `fn(Format)` | Configures minimum severity (`Level::Debug`, `Info`, `Warn`, `Error`) and output format (`Format::Text` or `Format::Json`). |
| `core:log` | `log.debug` / `info` / `warn` / `error` | `fn(msg: str, ...kv_pairs)` | Emits a timestamped log record with variadic `"key", "value"` structured attributes. |

```cfire
package main

import "core:cli"
import "core:log"

type ServerFlags struct {
    Host:    str `cli:"--host,-h" default:"127.0.0.1" desc:"Bind address"`
    Port:    u16 `cli:"--port,-p" default:"9090"      desc:"Listen port"`
    Workers: i32 `cli:"--workers" default:"4"         desc:"Worker thread count"`
}

fn main() {
    flags := cli.parse[ServerFlags](["--port", "8088", "--workers", "8"])
    log.set_format(log.Format::Json)
    log.info("server configured", "host", flags.Host, "port", "{flags.Port}", "workers", "{flags.Workers}")
}
```
```text
# Output:
"level":"INFO","msg":"server configured","host":"127.0.0.1","port":"8088","workers":"8"
```

---

## Chapter 10: Structured Concurrency & Channels

### Why Unstructured Threads Fail & How `async.scope` Fixes It
Detached threads (`std::thread::detach`) and fire-and-forget tasks break stack memory safety: if a background thread outlives the function that spawned it, any borrowed stack reference becomes a use-after-free race. To work around that, most C++ codebases end up wrapping everything in `std::shared_ptr` and `std::mutex`.

`cfire` enforces **Lexical Structured Concurrency** via `async.scope(workers: N) |scope| { ... }`:
1. **Bounded Worker Pool**: Allocates `N` hardware worker threads (`std::jthread`) scoped to the lexical block.
2. **Mandatory Join Barrier**: The closing `}` of `async.scope` blocks until every task spawned via `scope.spawn || { ... }` finishes.
3. **Zero-Heap Stack Borrowing**: Because no worker can outlive `async.scope`, tasks can read outer immutable bindings (`in`) or write to disjoint pre-allocated slice slots (`&mut results[i]`) without `shared_ptr` overhead:

```cfire
package main

fn compute_shard_checksum(shard_id: i32, scale: i32) -> i32 {
    return (shard_id + 1) * scale
}

fn run_parallel_shards(shard_count: usize, scale: i32) -> []i32 {
    // Pre-allocate one output slot per shard before entering the parallel scope
    mut results := make([]i32, shard_count)

    async.scope(workers: 4) |scope| {
        for i in 0..shard_count {
            slot := &mut results[i]
            shard_idx := i32(i)
            scope.spawn || {
                *slot = compute_shard_checksum(shard_idx, scale)
            }
        }
    } // Implicit barrier: all 4 worker threads join before returning 'results'

    return results
}

fn main() {
    shards := run_parallel_shards(4, 10)
    println("shards=[{}, {}, {}, {}]", shards[0], shards[1], shards[2], shards[3])
}
```
```text
# Output:
shards=[10, 20, 30, 40]
```

### Communicating Across Workers with MPMC Channels (`core:sync`)
When workers need to stream items through a producer/consumer pipeline rather than write to fixed slice slots, use `make(sync.Channel[T], capacity)`. It provides a typed, thread-safe multi-producer / multi-consumer (`MPMC`) queue that integrates directly with `for item in ch` loops:

| Function / Method | Signature | Behavior |
| :--- | :--- | :--- |
| `make(sync.Channel[T], cap)` | `-> Channel[T]` | Creates a thread-safe channel handle (copying the handle shares the underlying queue); `cap = 0` is unbuffered. |
| `ch.send(val)` | `fn (ch: Channel[T]) send(val: T)` | Enqueues `val` and wakes one blocked receiver thread. |
| `ch.recv()` | `fn (ch: Channel[T]) recv() -> T!` | Blocks until an item is available, or returns an error when closed and drained. |
| `ch.recv_ctx(ctx)` | `fn (ch: Channel[T]) recv_ctx(ctx: Context) -> T!` | Blocks until an item arrives, the channel closes, or `ctx` is cancelled/times out. |
| `ch.close()` | `fn (ch: Channel[T]) close()` | Marks the channel closed and wakes all blocked `recv` and `for item in ch` consumers. |

```cfire
package main

import "core:sync"

fn main() {
    ch := make(sync.Channel[i64])
    mut total: i64 = 0

    async.scope(workers: 1) |scope| {
        scope.spawn || {
            for i in 1..=4 {
                ch.send(i * 10)
            }
            ch.close()
        }
    }

    for val in ch {
        total += val
    }

    println("channel_sum={}", total)
}
```
```text
# Output:
channel_sum=100
```

### Shared State with `Mutex[T]` and `Atomic[T]` (`core:sync`)
A spawned task may not write through a captured variable or pointer that other tasks can also reach (`cfire/SPAWN-SHARED-MUT`, `cfire/ESCAPING-CAPTURE-MUT`). When workers really do need one shared value, put it behind a `sync.Mutex[T]` or `sync.Atomic[T]`. Copies of either handle share the same state, so each task captures the handle and goes through its methods:

| Function / Method | Signature | Behavior |
| :--- | :--- | :--- |
| `make(sync.Mutex[T], v)` | `-> Mutex[T]` | A mutex protecting the value `v`. `make(sync.Mutex[void])` is a pure lock with `lock()` / `unlock()`. |
| `mu.lock()` | `fn (m: Mutex[T]) lock() -> MutexGuard[T]` | Blocks until the lock is free. Read or write the value through the guard (`*g += 1`); the lock is released when `g` goes out of scope. |
| `mu.try_lock()` | `fn (m: Mutex[T]) try_lock() -> MutexGuard[T]?` | Like `lock()`, but `none` instead of blocking. |
| `mu.with_lock(f)` | `fn (m: Mutex[T]) with_lock(f: fn(mut T) -> R) -> R` | Runs `f` on the value while holding the lock: `mu.with_lock(\|mut v\| { v += 1 })`. |
| `mu.get()` / `mu.set(v)` | `-> T` / `fn(v: T)` | Copies the value out, or replaces it, under the lock. |
| `make(sync.Atomic[T], v)` | `-> Atomic[T]` | A lock-free integer counter: `load()`, `store(v)`, `add(d)`, `sub(d)`. |

```cfire
package main

import "core:sync"

fn main() {
    total := make(sync.Mutex[i64], 0)
    hits := make(sync.Atomic[i64], 0)

    async.scope |s| {
        for w in 0..4 {
            s.spawn || {
                for i in 0..250 {
                    mut g := total.lock()
                    *g += 2
                    hits.add(1)
                }
            }
        }
    }

    total.with_lock(|mut v| { v += 1 })
    println("total={} hits={}", total.get(), hits.load())
}
```
```text
# Output:
total=2001 hits=1000
```

### Hierarchical Cancellation & Deadlines (`core:sync/context`)
Long-running network calls and worker pipelines need cooperative timeouts and cancellation. `core:sync/context` implements hierarchical cancellation tokens where cancelling a parent `Context` automatically cancels all of its child contexts and wakes any blocked `recv_ctx` or `sleep_ctx` calls:

| Function / Method | Signature | Behavior |
| :--- | :--- | :--- |
| `context.background()` | `fn background() -> Context` | Returns a root, non-cancellable `Context`. |
| `context.with_cancel(parent)` | `fn with_cancel(parent: Context) -> (Context, fn(reason: str = ""))` | Creates a child `Context` and a `cancel(reason)` closure. |
| `context.with_timeout(parent, ms)` | `fn with_timeout(parent: Context, ms: i64) -> (Context, fn(reason: str = ""))` | Creates a child `Context` that automatically cancels after `ms` milliseconds. |
| `ctx.is_cancelled()` / `ctx.done()` | `fn (ctx: Context) is_cancelled() -> bool` | Non-blocking atomic check returning `true` once cancelled or expired. |
| `ctx.reason()` | `fn (ctx: Context) reason() -> str` | Returns the string reason passed to `cancel(reason)` (or `"timeout"`). |
| `ctx.wait()` / `ctx.wait_for(ms)` | `fn (ctx: Context) wait_for(ms: i64) -> bool` | Blocks the calling thread until `ctx` cancels or `ms` elapses (`true` if cancelled). |
| `time.sleep_ctx(ctx, ms)` | `fn sleep_ctx(ctx: Context, ms: i64) -> bool` | Sleeps for `ms` milliseconds or wakes immediately if `ctx` cancels (`false` if cancelled). |

```cfire
package main

import "core:sync"
import "core:sync/context"
import "core:time"

fn main() {
    root := context.background()
    (ctx, cancel) := context.with_timeout(root, 50)
    defer cancel("cleanup")

    ch := make(sync.Channel[str])

    async.scope(workers: 2) |scope| {
        scope.spawn || {
            msg := ch.recv_ctx(ctx) or " aborted:{ctx.reason()}"
            ch.send(msg)
        }
    }

    result := ch.recv() or "none"
    println("cancelled={} result={}", ctx.is_cancelled(), result)
}
```
```text
# Output:
cancelled=true result= aborted:deadline exceeded
```

---

## Chapter 11: C/C++ Interop & Native Dependencies

### Zero-Wrapper Interop: Inline `cpp` Blocks & ABI Coercion
Because `cfire` compiles to ISO C++23, calling C or C++ libraries requires **zero dynamic FFI marshaling, zero wrapper generators (SWIG/bindgen), and zero ABI thunks**. Every `.cf` type lowers to a standard C++23 layout:

| `.cf` Type / Expression | C / C++23 Parameter Type | Boundary Conversion Performed by `cfire` |
| :--- | :--- | :--- |
| `i8`..`i64`, `u8`..`u64`, `f32`, `f64`, `bool` | `int8_t`..`int64_t`, `uint8_t`..`uint64_t`, `float`, `double`, `bool` | Identical C++23 scalar type (0-cost pass-through) |
| `str` passed to `const char*` | `const char*` | Automatic `cfire::c_arg(s)` (`.c_str()`, 0 allocation) |
| read-only `str` parameter, `&str`, `s[a..b]` | `cfire::str_ref` (derives from `std::string_view`) | 0-copy pointer + length view; converts to `std::string` where an owned `str` is needed |
| `s.data()` / `s.byte_ptr()` / `s.byte_ptr_at(i)` | `const char*` / `char*` | Direct raw pointer to contiguous UTF-8 buffer |
| `slice.data()` / `slice.data_ptr()` | `const T*` / `T*` | Direct pointer to contiguous `std::vector<T>` buffer |
| `&T` / `&mut T` | `const T&` or `const T*` / `T&` or `T*` | Direct reference or address-of `&x` for C out-pointers |

When you need platform `#ifdef` intrinsics or direct C++ statements inside a `.cf` function, open a `cpp { ... }` block—all local `.cf` parameters and variables are directly in scope:

```cfire
package main

fn hardware_concurrency_and_arch(multiplier: i32) -> (str, i32) {
    mut threads: i32 = 1
    mut arch := "unknown"

    cpp {
        #if defined(__aarch64__) || defined(_M_ARM64)
            arch = "arm64";
        #elif defined(__x86_64__) || defined(_M_X64)
            arch = "x86_64";
        #elif defined(__wasm__)
            arch = "wasm32";
        #endif
        threads = static_cast<std::int32_t>(std::thread::hardware_concurrency()) * multiplier;
        if (threads <= 0) threads = multiplier;
    }

    return (arch, threads)
}

fn main() {
    (arch, slots) := hardware_concurrency_and_arch(2)
    println("arch_detected={} worker_slots>={}", arch.len() > 0, slots >= 2)
}
```
```text
# Output:
arch_detected=true worker_slots>=true
```

### Importing C Headers & Using Bitwise Flag Enums (`import cpp`)
Use `import cpp "<header.h>"` (with optional `@pkg("<pkg-config-name>")` and `@define("<KEY>", "<VAL>")` attributes) to parse a system or vendor C/C++ header via Clang's JSON AST bridge and expose its functions, structs, enums, and constants under `cpp.` (or an aliased qualifier `import cpp "..." as <alias>`):

```cfire
package main

import cpp "zstd.h" @pkg("libzstd")

fn query_zstd_runtime() -> (u32, usize) {
    ver := cpp.ZSTD_versionNumber()
    bound := cpp.ZSTD_compressBound(1024)
    return (ver, bound)
}

fn main() {
    (ver, max_bytes) := query_zstd_runtime()
    println("zstd_version={} bound_for_1kb={}", ver >= 10000, max_bytes > 1024)
}
```
```text
# Output:
zstd_version=true bound_for_1kb=true
```

**Bitwise Flag Operators & Formatting on C / Vendor Enums:**
C libraries (like Raylib's `ConfigFlags` or POSIX file flags) frequently use `enum` types as bitmasks. `cfire` supports bitwise operators (`|`, `&`, `^`, `~`, `|=`, `&=`) and `"{flag}"` string formatting directly on C and `vendor:*` enums without requiring manual integer casts:
```cfire
// Combine C enum bitflags directly with | and &=
mut flags := rl.ConfigFlags::FLAG_WINDOW_RESIZABLE | rl.ConfigFlags::FLAG_VSYNC_HINT
flags |= rl.ConfigFlags::FLAG_MSAA_4X_HINT
```

Before writing code against a foreign header, run `cfire inspect "cpp zstd.h"` from the terminal to list every discovered symbol and its mapped `.cf` signature.

### Orchestrating C, CMake & Make Libraries in `build.cf`
When your project combines `.cf` code with local C files (`build.CC`), a CMake subproject (`build.CMake`), or a Makefile archive (`build.Make`), declare them in `build.cf`. The build driver compiles each native dependency into `.cfire/cache/deps/`, forwards its `Includes` and `Defines` to the Clang AST bridge, and links the resulting static archives automatically:

```cfire
package build

import "core:build"

FastCRC := build.CC("./c_deps/fastcrc", {
    Sources:  ["fastcrc.c"],
    Includes: ["."],
    Defines:  {"FASTCRC_SEED": 42},
})

KVStore := build.CMake("./vendor/libkv_cmake", {
    Target:   "kvstore",
    Includes: ["include"],
    Defines:  {"KV_SALT": 42},
})

VecMath := build.Make("./vendor/libmath_make", {
    Target:   "vecmath",
    Includes: ["include"],
    Defines:  {"MATH_SCALE_BIAS": 1.0},
})

App := build.Executable("hybrid-build-demo", {
    Entry: "main.cf",
})
```

In `main.cf`, import all three headers directly via `import cpp`:
```cfire
package main

import cpp "fastcrc.h"
import cpp "kvstore.h"
import cpp "vecmath.h"

fn main() {
    payload := "cfire-hybrid"
    crc := cpp.fastcrc32(payload, u32(payload.len()))
    hash := cpp.kv_hash_key(payload)
    dot := cpp.vm_dot2d(2.0, 3.0, 4.0, 5.0)
    println("crc=0x{:x} kv_hash={} dot={:.1f}", crc, hash, dot)
}
```

---

## Chapter 12: Testing, Fuzzing, Sanitizers & Diagnostics

### Unit Tests, Doc-Tests & Resource-Leak Canaries (`cfire test`)
Testing shouldn't require installing a third-party test framework or maintaining separate CMake test targets. `cfire` builds unit tests, executable doc-tests, automatic JSON round-trip invariants, and OS file-descriptor leak detection directly into `cfire test`:

- **Co-Located `test "name" { ... }` Blocks**: Stripped from production `cfire build` binaries; compiled into a fast test runner during `cfire test`.
- **Executable Doc-Tests (`///`)**: Any fenced ```` ```cf ```` block inside a `///` doc-comment that calls `assert` or `assert_eq` is automatically extracted and run as part of `cfire test`.
- **Automatic JSON Round-Trip Invariants**: Importing `core:json` alongside JSON-tagged structs automatically synthesizes `[auto] json round-trip: <Struct>` tests across 16 arbitrary instances.
- **File-Descriptor Leak Canary**: Every `test` and `fuzz` block snapshots open OS file descriptors before and after execution, failing immediately if a test leaks an unclosed file, socket, or SQLite handle (`missing 'defer file.close()'?`).

```cfire
package main

/// Computes the N-th Fibonacci number.
/// ```cf
/// assert_eq(fibonacci(0), 0)
/// assert_eq(fibonacci(10), 55)
/// ```
fn fibonacci(n: i32) -> i64 {
    if n <= 1 {
        return i64(n)
    }
    mut prev: i64 = 0
    mut curr: i64 = 1
    for _ in 2..=n {
        next := prev + curr
        prev = curr
        curr = next
    }
    return curr
}

test "fibonacci base cases" {
    assert_eq(fibonacci(0), 0, "F(0) must equal 0")
    assert_eq(fibonacci(1), 1, "F(1) must equal 1")
}

test "fibonacci sequence progression" {
    assert_eq(fibonacci(10), 55, "F(10) must equal 55")
    assert(fibonacci(12) > fibonacci(11), "sequence must be strictly increasing")
}
```

```bash
# Run all test, fuzz, doc-test, and [auto] invariant blocks in the workspace
cfire test
cfire test --filter "base cases"
cfire test --json
```

### Property-Based Fuzzing & Statistical Benchmarks (`fuzz` & `bench`)
Hand-written unit tests check the inputs you thought of; property-based `fuzz` blocks find the edge cases you missed. Each `fuzz "name" |...|` block runs 16 deterministic adversarial boundary cases (`0`, `-1`, `INT64_MIN`, `INT64_MAX`, `NaN`, empty/UTF-8/NUL/SQL-quote strings) followed by seeded SplitMix64 PRNG draws across primitives, slices, and user structs, shrinking any failure to a minimal counterexample.

For performance work, `bench "name" |param in [...]|` performs warmup, dynamic batch calibration, and multi-sample statistical timing (`min`, `p50`, `mean ± stddev%`, `p99`, `ops/s`) with `black_box(x)` dead-code-elimination barriers:

```cfire
package main

import "core:encoding/hex"

fuzz "hex round-trip invariant" |raw: []byte| {
    encoded := hex.encode(raw)
    decoded := try hex.decode(encoded)
    assert_eq(decoded.len(), raw.len())
}

bench "hex encode throughput" |size in [64, 1024, 16384]| {
    payload := make([]byte, usize(size))
    black_box(hex.encode(payload))
}
```

```bash
# Run fuzz tests with custom iteration count and deterministic seed:
cfire test --fuzz-runs 500 --seed 1337

# Run statistical benchmarks:
cfire bench
cfire bench --bench-time 250 --filter "hex encode"
```

### Source-Mapped Sanitizers (`--sanitize`) & Auto-Remediation (`cfire fix`)
When your project mixes `.cf` code with raw `import cpp` headers or `cpp { ... }` blocks, pass `--sanitize` to `cfire test` or `cfire run`. The compiler links AddressSanitizer (ASan) and UndefinedBehaviorSanitizer (UBSan) with `#line` directives so any C buffer overflow, use-after-free, or signed integer overflow points directly to the offending `.cf` line:

```bash
cfire test --sanitize
cfire run --sanitize src/main.cf
```

And when the compiler flags missing stdlib imports, missing call-site `mut` / `move` markers, or non-exhaustive `match` arms, run `cfire fix` to rewrite your source file automatically:
```bash
cfire fix --dry-run src/main.cf
cfire fix src/main.cf
```

### Complete `cfire/*` Diagnostics Reference
Every diagnostic has a stable `cfire/*` code, shown after the message and emitted as structured JSON by `cfire check --json <file.cf>`. The detailed rules, with rejected and accepted examples, are in [`reference.html`](reference.html). This table lists every code the compiler can report:

<!-- cfire:diagnostics-table:begin (generated by `cfire doc`; do not edit) -->
| Code | Category | Meaning |
| :--- | :--- | :--- |
| `cfire/AMBIGUOUS-INTERPOLATION` | Syntax | An interpolation `{...}` that starts with a number literal must be parenthesized. |
| `cfire/ARITY-MISMATCH` | Types | A call passes the wrong number of arguments. |
| `cfire/ASSIGN-IMMUTABLE` | Mutability | Assignment to a binding declared without `mut`. |
| `cfire/BREAK-OUTSIDE-LOOP` | Detailed rule | `break` or `continue` Outside of a `for` Loop |
| `cfire/DANGLING-POINTER` | Detailed rule | Inner-Scope Borrow Escaping to Outer-Scope Pointer or View |
| `cfire/DANGLING-RETURN` | Detailed rule | Returning Pointer or View Rooted in Function-Local Storage |
| `cfire/DANGLING-TEMPORARY` | Memory safety | A view or pointer borrows from a temporary that is destroyed at the end of the statement. |
| `cfire/DEPRECATED-API` | Migration | A stdlib function that was renamed or replaced before 0.16 (`fs.read_all`, `os.read_stdin`, ...). |
| `cfire/DEPRECATED-DISCARD` | Migration | `_ := expr` is not a discard form; write `_ = expr`. |
| `cfire/DEPRECATED-IN-PARAM` | Migration | Explicit `in` intent; parameters are read-only by default. |
| `cfire/DEPRECATED-INTENT-POS` | Migration | `x: mut T` intent placement; write `mut x: T`. |
| `cfire/DEPRECATED-SEPARATOR` | Syntax | `::` used where `.` is required (only enum variants use `::`). |
| `cfire/DIVIDE-BY-ZERO` | Detailed rule | Compile-Time Division or Modulo by Constant Zero |
| `cfire/DUPLICATE-DECLARATION` | Declarations | Two declarations of the same type, function or method. |
| `cfire/DYNAMIC-INTERFACE-UNSUPPORTED` | Types | A slice or array of interface values; interfaces lower to C++ concepts. |
| `cfire/ERROR` | Tooling | An internal error with no more specific code. |
| `cfire/ESCAPING-CAPTURE-MUT` | Concurrency | An escaping closure mutates a captured outer variable. |
| `cfire/FIELD-NAME-CASE` | Detailed rule | Struct Field Is Not snake_case |
| `cfire/FILE-NOT-FOUND` | Tooling | A file named on the command line or imported does not exist. |
| `cfire/IMMUTABLE-MUT-ARG` | Detailed rule | Passing Immutable Binding as `mut` Argument |
| `cfire/INDEX-OUT-OF-BOUNDS` | Detailed rule | Compile-Time Out-of-Bounds or Negative Constant Sequence Index |
| `cfire/INVALID-CONSTRUCTOR` | Detailed rule | Associated Function Other Than `make` |
| `cfire/INVALID-IMPORT` | Imports | A module imported under the wrong tier prefix (`core:` vs `ext:`). |
| `cfire/INVALID-INDEX` | Types | Indexing a value that is not a string, slice, array or map. |
| `cfire/INVALID-INTERPOLATION` | Syntax | An interpolation `{...}` that is not a valid expression. |
| `cfire/INVALID-MAKE` | Detailed rule | Invalid `make(T, ...)` Arguments |
| `cfire/INVALID-MEMBER` | Types | A member access on a primitive value that has no such member. |
| `cfire/INVALID-MOVE` | Detailed rule | No-Op `move` on Rvalue or Scalar `Copy` Variable |
| `cfire/INVALID-OR-BLOCK-CONTEXT` | Detailed rule | Block `or { ... }` Inside Short-Circuit Operand or `for` Condition |
| `cfire/INVALID-RELEASE` | Detailed rule | `release` on a Shared Pointer |
| `cfire/INVALID-SMART-PTR-CTOR` | Construction | `own(v)` / `shared(v)` called as functions; write `make(own[T], v)`. |
| `cfire/INVALID-STRUCT-TAG` | Detailed rule | Duplicate or Malformed Struct Reflection Tag |
| `cfire/INVALID-TRY-CONTEXT` | Detailed rule | `try` Inside Short-Circuit Operand or `for` Condition |
| `cfire/ITERATOR-INVALIDATION` | Detailed rule | Structural Mutation or Move of Collection During `for` Iteration |
| `cfire/LITERAL-OUT-OF-RANGE` | Numbers | An integer literal that does not fit its target type (Q9). |
| `cfire/MISSING-IMPORT` | Imports | A stdlib module is used without being imported. |
| `cfire/MISSING-RETURN` | Control flow | A function with a return type can fall off its end. |
| `cfire/MOVE-CALLSITE` | Detailed rule | Missing Call-Site `move` Marker |
| `cfire/MUT-ALIAS` | Detailed rule | Mutable Aliasing Across Call-Site Arguments (Law of Exclusivity) |
| `cfire/MUT-CALLSITE` | Detailed rule | Missing Call-Site `mut` Marker |
| `cfire/MUT-RECEIVER` | Detailed rule | Calling `mut` Method on Immutable Receiver |
| `cfire/NO-EFFECT-STMT` | Detailed rule | Pure Expression Statement With No Side Effects |
| `cfire/NON-BOOL-CONDITION` | Types | A condition (`if`, `for`, `&&`, `||`, `!`) that is not `bool`. |
| `cfire/NON-DIVERGING-OR-BLOCK` | Detailed rule | Non-Diverging Error Recovery Block on Value Binding |
| `cfire/NON-EXHAUSTIVE-MATCH` | Detailed rule | Non-Exhaustive `match` Statement Over Algebraic `enum` |
| `cfire/NULL-DEREF` | Detailed rule | Dereference or Member Access on Null or Potentially Null Pointer |
| `cfire/OPTIONAL-OR-ERR` | Detailed rule | Binding Error Variable `or err` on Optional `T?` |
| `cfire/OUT-CALLSITE` | Migration | A call-site `out` marker (retired with the `out` intent). |
| `cfire/PREFER-ENUM` | Style | A cluster of related integer constants that should be an enum (warning). |
| `cfire/REDUNDANT-CALLSITE-INTENT` | Detailed rule | Redundant `mut` Annotation on a Read-Only Parameter |
| `cfire/REDUNDANT-DEREF` | Style | `(*p).field` on a smart pointer; write `p.field`. |
| `cfire/REDUNDANT-OR` | Detailed rule | Redundant `or` on Non-Fallible, Non-Optional Expression |
| `cfire/REDUNDANT-TRY` | Detailed rule | Redundant `try` on Non-Fallible Expression |
| `cfire/RETIRED-API` | Detailed rule | API Retired in the 0.16 Uniform-API Migration |
| `cfire/RETURN-IN-DEFER` | Detailed rule | `return` Statement Inside `defer { ... }` Block |
| `cfire/SEMA-ERROR` | Tooling | A semantic error with no more specific code. |
| `cfire/SHELL-INJECTION-RISK` | Detailed rule | Dynamic String Concatenation or Interpolation in Shell Command |
| `cfire/SHIFT-OUT-OF-RANGE` | Numbers | A constant shift count outside the operand's bit width (Q11). |
| `cfire/SPAWN-SHARED-MUT` | Concurrency | A spawned task writes through a pointer other tasks can reach (Q20). |
| `cfire/SQL-INJECTION-RISK` | Detailed rule | Dynamic String Concatenation or Interpolation in SQL Query |
| `cfire/SYNTAX` | Syntax | The source does not parse. |
| `cfire/TRY-IN-DEFER` | Detailed rule | `try` Inside `defer` Block |
| `cfire/TRY-IN-NON-FALLIBLE-FN` | Detailed rule | `try` Inside Non-Fallible Function |
| `cfire/TRY-ON-OPTIONAL` | Detailed rule | Using `try` on Optional `T?` Instead of `or` |
| `cfire/TYPE-ERROR` | Tooling | The C++ backend rejected generated code; Sema should have caught it (please report). |
| `cfire/TYPE-MISMATCH` | Types | A value of one type is used where another is required. |
| `cfire/UNDECLARED-IDENT` | Names | A name that is not declared, or a module used under the wrong alias. |
| `cfire/UNHANDLED-RESULT` | Errors | A fallible `T!` value used without `try` or `or`. |
| `cfire/UNKNOWN-ARG-LABEL` | Detailed rule | Unknown Labeled Argument |
| `cfire/UNKNOWN-ENUM-VARIANT` | Names | A variant that the enum does not declare. |
| `cfire/UNKNOWN-FIELD` | Names | A field or associated item that the type does not have. |
| `cfire/UNKNOWN-METHOD` | Names | A method that the receiver type does not have (Q14 closed builtin surface). |
| `cfire/UNKNOWN-MODULE` | Imports | An import path that names no known module. |
| `cfire/UNKNOWN-STDLIB-FN` | Names | A function that the stdlib module does not export. |
| `cfire/UNKNOWN-TYPE` | Types | An expression whose type cannot be inferred (warning). |
| `cfire/UNREACHABLE-MATCH-ARM` | Detailed rule | Unreachable `match` Arm Shadowed by Earlier Pattern |
| `cfire/UNRESOLVED-PACKAGE` | Imports | A local package import that cannot be found. |
| `cfire/UNSATISFIED-CONSTRAINT` | Generics | A type argument that does not satisfy the interface constraint. |
| `cfire/UNWRAPPED-OPTIONAL` | Errors | A member access on a `T?` value without unwrapping. |
| `cfire/USE-AFTER-FREE` | Detailed rule | Use of Dangling Pointer or Slice View After Owner Invalidation |
| `cfire/USE-AFTER-MOVE` | Detailed rule | Use of Value After Explicit `move` |
| `cfire/WRONG-ARG-COUNT` | Types | A builtin called with the wrong number of arguments. |
<!-- cfire:diagnostics-table:end -->

---

# Part IV: Networking, Data, UI & AI

## Chapter 13: HTTP, SSE, WebSockets & TLS

### One Networking Stack Across Desktop, iOS & WebAssembly
Writing networking code in C++ usually forces you to choose between a heavyweight library that won't cross-compile to WebAssembly or iOS, and low-level POSIX sockets that don't speak HTTP, TLS 1.3, or WebSockets.

The `core:net` family gives you one API from raw TCP sockets up to HTTP routers, Server-Sent Events (SSE), and RFC 6455 WebSockets—negotiating the native OS transport automatically on each target:

| Module | Primary Types | Desktop (macOS / Linux) | iOS (`--ios`) | Web (`--web`) | Restricted Sandbox Fallback |
| :--- | :--- | :--- | :--- | :--- | :--- |
| `core:net` | `TcpListener`, `TcpStream`, `SocketPair` | POSIX `socket` / `bind` / `connect` (`AF_INET`) | POSIX `socket` (`AF_INET`) | Unsupported (use HTTP/WS) | In-process shared-memory ring buffer for loopback (`127.0.0.1` / `localhost`) when `EPERM`/`EACCES` occurs |
| `core:net/url` | `Url` | Native zero-allocation RFC 3986 parser | Native RFC 3986 parser | Native RFC 3986 parser | Native RFC 3986 parser |
| `core:net/tls` | `TlsStream`, `TlsListener`, `PemPair` | Runtime `dlopen` of `libssl` / `libcrypto` (OpenSSL 1.3+) | Apple `Foundation` / `Network.framework` | Browser TLS stack | In-process TLS stream framing over loopback |
| `core:net/http` | `Client`, `Server`, `Router`, `Request`, `Response`, `SseEvent` | Native `core:net` + `core:net/tls` (with `curl` fallback for external HTTPS if OpenSSL is absent) | Native `NSURLSession` synchronous bridge for external HTTPS; native `core:net` for local loopback | Browser `emscripten_fetch` synchronous XHR bridge | In-process router registry for `127.0.0.1` / `localhost` servers |
| `core:net/websocket` | `WebSocket`, `WsPair`, `WsServer`, `Frame` | RFC 6455 frame engine over `TcpStream` / `TlsStream` | RFC 6455 frame engine | RFC 6455 frame engine | In-memory `websocket.pair()` full-duplex channel |

### Building an HTTP Router & Streaming SSE (`core:net/http`)
Let's build an HTTP service with pre-dispatch middleware, parameterized routes (`/users/:id`), and a real-time Server-Sent Events (`/v1/stream`) endpoint. Notice how `router.dispatch(...)` lets you unit-test your entire routing table in memory before binding a live port via `make(http.Server, 0)`:

| Function / Method | Signature | Description |
| :--- | :--- | :--- |
| `http.get` | `fn(url: str, headers = {}, verify_peer: bool = true) -> Response!` | Sends an `HTTP GET` request over `http://` or `https://` (`verify_peer: false` accepts self-signed certificates). |
| `http.post` / `http.request` | `fn(url: str, body: str, ...) -> Response!` | Sends an `HTTP POST` or custom HTTP verb (`PUT`, `PATCH`, `DELETE`) with headers. |
| `http.post_sse` | `fn(url: str, body: str, headers: map[str]str, on_event: fn(SseEvent) -> bool) -> void!` | Streams `text/event-stream` chunks in real time, invoking `on_event` for each frame until the stream ends or `on_event` returns `false`. |
| `make(http.Router)` | `-> Router` | Constructs a pattern-matching router supporting static paths, `:param` segments, and `*` wildcards. |
| `router.get` / `post` / `put` / `delete_` | `fn(pattern: str, handler: fn(Request) -> Response)` | Registers a route handler for the specified HTTP method and path pattern. |
| `router.use` | `fn(middleware: fn(Request) -> int)` | Runs before route dispatch (`0` continues; non-zero HTTP status code short-circuits). |
| `router.dispatch` | `fn(req: Request) -> Response` | Executes middleware and route matching in memory without opening a network socket. |
| `make(http.Server, port)` / `make(http.Server, port, tls: http.TlsConfig{...})` | `-> Server!` | Binds a multi-threaded HTTP/1.1 or HTTPS (TLS 1.3) server (`0` picks an ephemeral port via `server.port()`); `server.set_limits(http.ServerLimits{...})` tunes timeouts and sizes. |

```cfire
package main

import "core:net/http"

fn main() -> void! {
    mut router := make(http.Router)

    // Middleware: return 0 to continue or an HTTP status code to halt dispatch
    router.use(|req: http.Request| -> int {
        if req.path == "/forbidden" { return 403 }
        return 0
    })

    // Parameterized GET route: /users/:id?format=compact
    router.get("/users/:id", |req: http.Request| -> http.Response {
        uid := req.param("id")
        fmt := req.query("format")
        return http.Response.json(200, "{\"id\":\"" + uid + "\",\"format\":\"" + fmt + "\"}")
    })

    // Server-Sent Events (SSE) endpoint returning text/event-stream frames
    router.post("/v1/stream", |req: http.Request| -> http.Response {
        mut res := http.Response.text(200, "event: token\ndata: {\"chunk\":\"hello\"}\n\nevent: done\ndata: [DONE]\n\n")
        res.set_header("Content-Type", "text/event-stream")
        return res
    })

    // 1. Zero-socket in-memory dispatch for fast unit tests
    synth := router.dispatch(http.Request{ method: "GET", path: "/users/42?format=full" })
    println("In-memory status={}, body={}", synth.status, synth.body)

    // 2. Live loopback server on an ephemeral port
    mut server := try make(http.Server, 0)
    defer server.stop()
    server.serve_in_background(router)

    base := "http://127.0.0.1:{server.port()}"
    live := try http.get(base + "/users/99?format=compact")
    println("Live GET status={}, body={}", live.status, live.body)

    // 3. Consume SSE stream in real time
    mut chunks: []str = []
    try http.post_sse(base + "/v1/stream", "{}", |ev: http.SseEvent| -> bool {
        chunks.push(ev.event + ":" + ev.data)
        return ev.data != "[DONE]"
    })
    println("SSE events ({}): {}", chunks.len(), chunks[0])
}
```
```text
# Output:
In-memory status=200, body={"id":"42","format":"full"}
Live GET status=200, body={"id":"99","format":"compact"}
SSE events (2): token:{"chunk":"hello"}
```

### Upgrading to WebSockets & Self-Signed TLS 1.3 (`core:net/websocket`, `core:net/tls`)
Need full-duplex framing or an encrypted local HTTPS server? Register a WebSocket route directly on your HTTP router with `router.ws("/path", ...)`, and synthesize an in-memory RSA-2048 X.509 certificate with `tls.generate_self_signed_pem("localhost")`—no external `openssl` shell scripts or temporary `.pem` files on disk required:

| Function / Method | Signature | Description |
| :--- | :--- | :--- |
| `tls.available()` | `fn() -> bool` | Returns `true` if the OpenSSL 1.3 dynamic bridge (`libssl` / `libcrypto`) is available on the host. |
| `tls.generate_self_signed_pem(cn)` | `fn(cn: str = "localhost") -> PemPair!` | Generates an ephemeral RSA-2048 X.509 v3 certificate and private key in memory (`.cert_pem`, `.key_pem`). |
| `websocket.pair()` | `fn() -> WsPair` | Returns a connected in-memory `WsPair { client, server }` for zero-socket RFC 6455 frame testing. |
| `websocket.connect(url)` | `fn(url: str) -> WebSocket!` | Performs an RFC 6455 HTTP `Upgrade: websocket` handshake over `ws://` or `wss://`. |
| `ws.send_text(msg)` / `ws.recv_text()` | `fn(str) -> void!` / `fn() -> str!` | Sends or receives a UTF-8 text WebSocket frame (`0x1`) with automatic RFC 6455 §5.3 client masking. |
| `ws.send_binary(b)` / `ws.recv_binary()` | `fn([]byte) -> void!` / `fn() -> []byte!` | Sends or receives a binary WebSocket frame (`0x2`). |

```cfire
package main

import "core:net/http"
import "core:net/tls"
import "core:net/websocket"

fn main() -> void! {
    mut router := make(http.Router)
    router.ws("/ws/echo", |req: http.Request, mut ws: websocket.WebSocket| {
        msg := ws.recv_text() or ""
        if msg != "" {
            _ = ws.send_text("echo:" + msg)
        }
        _ = ws.close()
    })

    mut srv := try make(http.Server, 0)
    defer srv.stop()
    srv.serve_in_background(router)

    mut client := try websocket.connect("ws://127.0.0.1:{srv.port()}/ws/echo")
    defer { _ = client.close() }
    try client.send_text("cfire-frame")
    reply := try client.recv_text()
    println("WS reply: {}", reply)

    if tls.available() {
        pem := try tls.generate_self_signed_pem("localhost")
        mut tls_router := make(http.Router)
        tls_router.get("/secure", |req: http.Request| -> http.Response {
            return http.Response.text(200, "TLS-1.3-OK")
        })
        mut tls_srv := try make(http.Server, 0, tls: http.TlsConfig{cert_pem: pem.cert_pem, key_pem: pem.key_pem})
        defer tls_srv.stop()
        tls_srv.serve_in_background(tls_router)

        sec := try http.get("https://127.0.0.1:{tls_srv.port()}/secure", verify_peer: false)
        println("HTTPS status={}, body={}", sec.status, sec.body)
    }
}
```
```text
# Output:
WS reply: echo:cfire-frame
HTTPS status=200, body=TLS-1.3-OK
```

---

## Chapter 14: JSON, SQLite, Zstd & Cryptography

### Compile-Time JSON Serialization & Schema Synthesis (`core:json`)
Using the `cfire::reflect<T>` tables generated for every struct (Chapter 6), `core:json` parses and serializes structs, slices, maps, and optional (`T?`) fields directly without building an intermediate DOM—unless you explicitly ask for `json.parse_dynamic(raw)`:

| Function | Signature | Description |
| :--- | :--- | :--- |
| `json.parse[T]` | `fn[T](raw: str) -> T!` | Parses a UTF-8 JSON string into struct, slice, map, or primitive `T`. Honors `json` and `validate` field tags. |
| `json.stringify[T]` | `fn[T](val: T, pretty: bool = false) -> str` | Serializes `val` to compact (`pretty = false`) or 2-space indented (`pretty = true`) JSON. |
| `json.schema[T]` | `fn[T]() -> str` | Generates a Draft 2020-12 JSON Schema object describing `T`'s fields and `required` constraints. |
| `json.parse_dynamic` | `fn(raw: str) -> json.Value!` | Parses untyped JSON into a navigable `json.Value` DOM tree (`.get(k)`, `.at(i)`, `.as_str()`, `.as_f64()`). |

```cfire
package main

import "core:json"

type Metric struct {
    Name:  str   `json:"name"`
    Value: f64   `json:"value"`
    Tags:  []str `json:"tags"`
    Note:  str?  `json:"note"`
}

fn main() -> void! {
    raw := "{\"name\":\"cpu.load\",\"value\":92.5,\"tags\":[\"prod\",\"us-east\"],\"note\":null}"
    m := try json.parse[Metric](raw)
    println("Parsed: {} = {:.1f} (tags={}, has_note={})", m.Name, m.Value, m.Tags.len(), m.Note != none)

    encoded := json.stringify(m)
    schema  := json.schema[Metric]()
    println("Encoded: {}", encoded)
    println("Schema has properties: {}", schema.contains("\"properties\""))

    dom := try json.parse_dynamic(raw)
    println("Dynamic lookup: name={}, tag[0]={}", dom["name"].as_str(), dom["tags"][0].as_str())
}
```
```text
# Output:
Parsed: cpu.load = 92.5 (tags=2, has_note=false)
Encoded: {"name":"cpu.load","value":92.5,"tags":["prod","us-east"],"note":null}
Schema has properties: true
Dynamic lookup: name=cpu.load, tag[0]=prod
```

### Embedded SQLite WAL, Vector Search & SQL Injection Safety (`ext:db/sqlite`)
`ext:db/sqlite` embeds SQLite 3 configured with Write-Ahead Logging (`PRAGMA journal_mode=WAL;`), compile-time row-to-struct mapping (`db.query[T]` using `` `sql:"..."` `` or `` `json:"..."` `` tags), atomic transaction scopes (`db.tx`), and SIMD-accelerated vector similarity SQL functions (`vec_cosine_similarity` and `vec_l2_distance`).

> [!IMPORTANT]
> **Compile-Time SQL Injection Prevention (`cfire/SQL-INJECTION-RISK`)**: Never concatenate or interpolate runtime variables into a SQL string (`db.exec("DELETE FROM users WHERE name = '" + user + "'")`). The compiler rejects dynamic string construction inside `db.exec`, `db.query`, and `db.query_one` with `cfire/SQL-INJECTION-RISK`. Always pass runtime values as positional `?1`, `?2` parameters.

| Function / Method | Signature | Description |
| :--- | :--- | :--- |
| `sqlite.open(path)` | `fn(path: str) -> DB!` | Opens a SQLite 3 database at `path` (or `":memory:"`), enables `WAL`, and registers `vec_cosine_similarity` & `vec_l2_distance`. |
| `db.exec(sql, ...args)` | `fn(sql: str, ...args) -> int!` | Binds variadic arguments (`int`, `i64`, `f64`, `bool`, `str`, `[]byte`, `[]f32`) to `?1`, `?2`, ... and returns affected rows. |
| `db.query[T](sql, ...args)` | `fn[T](sql: str, ...args) -> []T!` | Executes a `SELECT` query and maps each row into struct `T` via `` `sql:"..."` `` / `` `json:"..."` `` tags. |
| `db.query_one[T](sql, ...args)` | `fn[T](sql: str, ...args) -> T?!` | Returns the first matching row as `T?` (`none` if the result set is empty). |
| `db.tx(fn_body)` | `fn(body: fn() -> void!) -> void!` | Wraps `body()` in `BEGIN IMMEDIATE` ... `COMMIT`, automatically issuing `ROLLBACK` on error. |

```cfire
package main

import "ext:db/sqlite"

type DocHit struct {
    Id:    i64 `sql:"id"`
    Title: str `sql:"title"`
    Score: f64 `sql:"score"`
}

fn main() -> void! {
    mut db := try sqlite.open(":memory:")
    defer db.close()

    try db.exec("CREATE TABLE docs (id INTEGER PRIMARY KEY, title TEXT, embedding BLOB)")

    v1: []f32 = [0.95, 0.05, 0.0]
    v2: []f32 = [0.10, 0.90, 0.0]
    v3: []f32 = [0.85, 0.15, 0.0]
    // Atomic transaction inserting 3 normalized 3D embeddings as IEEE-754 f32 BLOBs
    try db.tx(|| -> void! {
        try db.exec("INSERT INTO docs (title, embedding) VALUES (?1, ?2)", "Kernel Scheduler", v1)
        try db.exec("INSERT INTO docs (title, embedding) VALUES (?1, ?2)", "HTTP Router",      v2)
        try db.exec("INSERT INTO docs (title, embedding) VALUES (?1, ?2)", "SQLite WAL Engine", v3)
    })

    // Query top-2 nearest documents using the built-in SIMD vec_cosine_similarity SQL function
    query_vec: []f32 = [1.0, 0.0, 0.0]
    hits := try db.query[DocHit](
        "SELECT id, title, vec_cosine_similarity(embedding, ?1) AS score FROM docs ORDER BY score DESC LIMIT 2",
        query_vec
    )

    for hit in hits {
        println("#{} {} (cosine={:.4f})", hit.Id, hit.Title, hit.Score)
    }
}
```
```text
# Output:
#1 Kernel Scheduler (cosine=0.9986)
#3 SQLite WAL Engine (cosine=0.9848)
```

### Zstandard Compression, AES-256-GCM & Cryptographic Hashing
When you need to compress telemetry frames, verify webhook signatures in constant time, or encrypt payloads at rest, `ext:compress/zstd`, `core:crypto/*`, and `core:encoding/*` work out of the box with zero external dependencies:

| Module | Function | Signature | Description |
| :--- | :--- | :--- | :--- |
| `ext:compress/zstd` | `zstd.compress_str` / `decompress_str` | `fn(data: str, level: int = 3) -> []byte!` / `fn(frame: []byte) -> str!` | Compresses/decompresses RFC 8878 Zstandard frames with XXH64 checksum verification. |
| `core:crypto/sha256` | `sha256.hash` / `sha256.hash_hex` | `fn(data: str) -> [32]byte` / `str` | FIPS 180-4 SHA-256 digest as a 32-byte array or 64-char hex string. |
| `core:crypto/blake3` | `blake3.hash` / `blake3.hash_hex` | `fn(data: str) -> [32]byte` / `str` | Cryptographic BLAKE3 hash (plus `keyed_hash_hex` for 32-byte keyed MACs). |
| `core:crypto/hmac` | `hmac.sha256_hex` / `hmac.verify` | `fn(key: str, msg: str) -> str` / `bool` | RFC 2104 `HMAC-SHA256` with constant-time `O(N)` tag comparison. |
| `core:crypto/aes` | `aes.encrypt_gcm` / `aes.decrypt_gcm` | `fn(key: []byte, nonce: []byte, plaintext: []byte, aad: []byte = {}) -> []byte!` | NIST SP 800-38D `AES-256-GCM` authenticated encryption (32-byte key, 12-byte IV, 16-byte tag). |
| `core:encoding/*` | `base64` / `hex` / `csv` | `encode`, `decode`, `csv.parse` | RFC 4648 Base64, lowercase hex, and RFC 4180 CSV parsing/serialization. |

```cfire
package main

import "ext:compress/zstd"
import "core:crypto/aes"
import "core:crypto/hmac"
import "core:crypto/sha256"
import "core:encoding/hex"

fn main() -> void! {
    payload := "ts=1730419200 level=INFO msg=\"request completed\" latency_ms=4.2\n"
    compressed := try zstd.compress_str(payload, 3)
    restored   := try zstd.decompress_str(compressed)
    println("Zstd round-trip match: {} (frame={} bytes)", restored == payload, compressed.len())

    key := sha256.hash_bytes("master-secret-passphrase")
    nonce := try hex.decode("000102030405060708090a0b")
    aad   := try hex.decode("feedface")

    ciphertext := try aes.encrypt_gcm(key, nonce, compressed, aad)
    decrypted  := try aes.decrypt_gcm(key, nonce, ciphertext.ciphertext, ciphertext.tag, aad)
    final_str  := try zstd.decompress_str(decrypted)

    mac := hmac.sha256_hex("webhook-secret", final_str)
    println("AES-GCM + Zstd verified: {}, HMAC valid: {}", final_str == payload, hmac.verify("webhook-secret", final_str, mac))
}
```
```text
# Output:
Zstd round-trip match: true (frame=73 bytes)
AES-GCM + Zstd verified: true, HMAC valid: true
```

---

## Chapter 15: 2D/3D Graphics, Raygui Widgets & Native iOS UI

### Hardware-Accelerated Canvas & Immediate-Mode Controls (`vendor:raylib`, `vendor:raygui`)
`cfire` embeds Raylib 5.0 (`vendor:raylib`) and Raygui (`vendor:raygui`) with a custom high-DPI font rasterizer (`CoreText` on macOS, `UIFont` on iOS, system TrueType on Linux). A single `.cf` file compiles to a native macOS Metal/OpenGL `.app`, a Linux desktop binary, an HTML5 WebGL2 canvas (`cfire run --web`), or a 60/120 Hz iOS Simulator app (`cfire run --ios`):

| Subsystem | macOS (`darwin`) | Linux (`linux`) | iOS (`--ios`) | WebAssembly (`--web`) |
| :--- | :--- | :--- | :--- | :--- |
| **Window & Context** | Cocoa (`NSWindow`) + high-DPI Retina OpenGL/Metal | X11 / Wayland + OpenGL 3.3 + auto-discovered `icon.png` | Native `UIWindow` + `UIViewController` + `CADisplayLink` | HTML5 `<canvas>` + WebGL2 + `emscripten_set_main_loop` |
| **Font Rasterizer** | `CoreText.framework` 2x Retina subpixel rasterizer | System TrueType discovery (`fc-match` / DejaVu / Noto) | Native `UIFont` (`SF Pro`) glyph atlas | Embedded 2x high-DPI font atlas |
| **Safe Area & Haptics** | Full viewport | Full viewport | `GetSafeAreaRect()` Dynamic Island inset + `TriggerHaptic` Taptic Engine | Full canvas + `navigator.vibrate` |

```cfire
package main

import "vendor:raylib" as rl
import "vendor:raygui" as rg

fn main() {
    rl.InitWindow(800, 480, "cfire Telemetry Studio")
    defer rl.CloseWindow()
    rl.SetTargetFPS(60)

    mut vsync    := true
    mut gain_db  : f32 = 0.0
    mut profile  : i32 = 1
    mut hostname := "edge-node-01"
    mut editing  := false

    for !rl.WindowShouldClose() {
        rl.BeginDrawing()
        defer rl.EndDrawing()
        rl.ClearBackground(rl.GetColor(0x18181bff))

        _ = rg.GuiWindowBox(rl.Rectangle{x: 24.0, y: 24.0, width: 340.0, height: 260.0}, "Node Configuration")
        _ = rg.GuiCheckBox(rl.Rectangle{x: 44.0, y: 68.0, width: 20.0, height: 20.0}, "Enable VSync Lock", &mut vsync)
        _ = rg.GuiSliderBar(rl.Rectangle{x: 90.0, y: 108.0, width: 200.0, height: 20.0}, "Gain", "{gain_db:.1f} dB", &mut gain_db, -24.0, 12.0)
        _ = rg.GuiComboBox(rl.Rectangle{x: 44.0, y: 148.0, width: 246.0, height: 28.0}, "Eco;Balanced;Turbo", &mut profile)

        if rg.GuiTextBox(rl.Rectangle{x: 44.0, y: 192.0, width: 246.0, height: 28.0}, &mut hostname, 64, editing) != 0 {
            editing = !editing
        }
        if rg.GuiButton(rl.Rectangle{x: 44.0, y: 234.0, width: 246.0, height: 32.0}, "Apply Settings") != 0 {
            rl.TriggerHaptic(1)
        }

        rl.DrawText("Host: {hostname} | Gain: {gain_db:.1f} dB", 400, 40, 20, rl.RAYWHITE)
    }
}
```

### Zero-Coordinate iOS Auto-Layout (`GuiIos*`) & Workspace Widgets (`pkg/raygui_ext`)
Positioning every mobile button by manual `(x, y)` coordinates gets tedious fast when adapting to iPhone safe areas, Dynamic Island notches, and software keyboards. `vendor:raygui` includes a built-in **Zero-Coordinate iOS Layout Engine** (`rg.GuiIos*`) modeled after SwiftUI's `NavigationStack` and `Form`:

| `vendor:raygui` iOS Control | Signature | What It Renders & Manages Automatically |
| :--- | :--- | :--- |
| `rg.GuiIosBegin` / `GuiIosEnd` | `fn(title: str)` / `fn()` | Reads `GetSafeAreaRect()`, renders the translucent iOS navigation bar, manages vertical touch-drag momentum scrolling, and clips content inside the safe area. |
| `rg.GuiIosSection` | `fn(label: str)` | Starts an inset grouped section header (like SwiftUI `Section("AUDIO ENGINE")`). |
| `rg.GuiIosToggle` | `fn(label: str, active: &mut bool) -> bool` | Full-width grouped row with an animated pill switch and automatic Taptic feedback on toggle. |
| `rg.GuiIosSlider` | `fn(label: str, val: &mut f32, min: f32, max: f32, unit: str)` | Grouped card row with value readout badge and touch-friendly thumb slider. |
| `rg.GuiIosSegmented` | `fn(options: str, active: &mut i32)` | Semicolon-delimited (`"Sine;Saw;Square"`) sliding pill segmented control. |
| `rg.GuiIosTextField` | `fn(label: str, text: &mut str, placeholder: str)` | Auto-focusing text field that summons the iOS software keyboard (`RequestKeyboard()`) on tap without manual focus state variables. |
| `rg.GuiIosButton` | `fn(label: str, accent: bool = true) -> bool` | Full-width rounded action button with press highlight and haptic feedback. |
| `rg.GuiIosTabBar` | `fn(tabs: str, active: &mut i32)` | Bottom dock bar pinned above the iOS Home Indicator (`"Synth;Presets;Settings"`). |

For desktop IDE and studio tools, the workspace also includes `pkg/raygui_ext` (`code_editor.cf`, `splitter.cf`, `text_buffer.cf`), providing resizable split-pane layouts, line-numbered code editors, and gap-buffer text editing out of the box.

```bash
# Scaffold a graphical app or launch directly on Desktop, WebAssembly, or iOS Simulator:
cfire new my-studio --template gui
cfire run examples/13-ios-native-ui
cfire run --web examples/13-ios-native-ui
cfire run --ios examples/13-ios-native-ui
```

---

## Chapter 16: Audio Synthesis & Real-Time DSP

### Lock-Free 64-Voice Mixer & Procedural Waveforms (`core:audio`)
`core:audio` gives you a zero-dependency, 64-voice lock-free stereo `f32` audio mixer, procedural waveform and SFX generators, ADSR envelope shaping, real-time SPSC ring-buffer streams (`Stream`), master RMS/peak metering, and a pure-C++23 RIFF/WAV file codec:

| Target Platform | Hardware Backend | Mixer Architecture | Headless / CI Fallback |
| :--- | :--- | :--- | :--- |
| **macOS & iOS (`__APPLE__`)** | `AudioToolbox.framework` (`AudioQueueNewOutput`) | 64-voice lock-free `f32` stereo mixer + 32-stream ring buffers with linear pitch resampling and constant-power stereo panning | Automatic software mixer when `CFIRE_HEADLESS_TEST=1` or no audio output device is present |
| **Linux (`__linux__`)** | Runtime `dlopen("libasound.so.2")` (ALSA PCM non-blocking thread) | Identical 64-voice lock-free `f32` stereo mixer | Automatic fallback if `libasound.so.2` or `/dev/snd` is unavailable |
| **WebAssembly (`__EMSCRIPTEN__`)** | Browser `AudioContext` + `ScriptProcessorNode` (auto-resumed on gesture) | Identical 64-voice `f32` stereo mixer | Software buffer advancement in headless Node/WASM |

| Category | Function / Method | Signature | Description |
| :--- | :--- | :--- | :--- |
| **Lifecycle & Metering** | `audio.init` / `audio.close` | `fn(sample_rate: i32 = 48000, channels: i32 = 2) -> void!` / `fn()` | Starts and stops the hardware audio device and mixer thread. |
| **Lifecycle & Metering** | `audio.rms_level` / `audio.peak_level` | `fn() -> f32` / `fn() -> f32` | Returns live master RMS (`rms_level`) or Peak (`peak_level`) amplitude in `[0.0, 1.0]`. |
| **Procedural Synthesis** | `audio.gen_tone` / `gen_chirp` / `gen_sfx` | `fn(...) -> Wave` | Synthesizes waveforms (`Sine`, `Square`, `Saw`, `Triangle`, `Noise`), frequency sweeps, or game SFX (`Tap`, `Coin`, `Laser`, `Explosion`, `PowerUp`, `Jump`, `Alert`). |
| **RIFF/WAV Codec** | `audio.encode_wav` / `decode_wav` / `save_wav` / `load_wav` | `fn(...)` | Encodes/decodes 8/16/24-bit PCM and 32-bit float RIFF/WAV in memory or on disk. |
| **Polyphonic `Sound`** | `audio.load_sound_from_wave` / `sound.play` | `fn(wave: Wave) -> Sound` | Uploads PCM frames into the mixer and triggers polyphonic playback with `.set_volume`, `.set_pitch`, `.set_pan`, `.set_looping`. |
| **Real-Time `Stream`** | `audio.open_stream` / `needs_data` / `push_samples` | `Stream` methods | Lock-free SPSC ring buffer for real-time custom DSP synthesis. |

### Streaming Real-Time DSP & Encoding RIFF/WAV Files
```cfire
package main

import "core:audio"
import "core:fs"

fn main() -> void! {
    try audio.init(48000, 2)
    defer audio.close()
    audio.set_master_volume(0.85)

    // 1. Procedural SFX with polyphonic pitch and stereo pan parameters
    coin_wave := audio.gen_sfx(audio.SfxPreset::Coin)
    coin_sfx  := audio.load_sound_from_wave(coin_wave)
    coin_sfx.set_volume(0.9)
    coin_sfx.set_pitch(1.15)
    coin_sfx.set_pan(-0.25) // Slightly left of center
    coin_sfx.play()

    // 2. Real-time push DSP stream (48 kHz stereo interleaved f32)
    stream := try audio.open_stream(48000, 2)
    chirp  := audio.gen_chirp(220.0, 880.0, 0.05, audio.Waveform::Triangle, 0.5)
    if stream.needs_data(2048) {
        stream.push_samples(chirp.samples)
    }

    // 3. In-memory RIFF/WAV encoding and disk round-trip verification
    wav_bytes := audio.encode_wav(chirp)
    wav_path  := "chirp_test.wav"
    defer { _ = fs.remove(wav_path) }
    try audio.save_wav(chirp, wav_path)

    decoded := try audio.load_wav(wav_path)
    println("WAV bytes: {}, frames: {}, channels: {}, rms>=0: {}",
        wav_bytes.len(), decoded.frame_count(), decoded.channels, audio.rms_level() >= 0.0)
}
```
```text
# Output:
WAV bytes: 9644, frames: 2400, channels: 2, rms>=0: true
```

---

## Chapter 17: LLMs, Multimodal RAG & MCP

### Unified Cloud & On-Device Inference (`ext:ai`)
`ext:ai` gives you one zero-dependency SDK across OpenAI, Anthropic, Gemini, local Ollama, any OpenAI-compatible server (`vLLM`, `llama-server`), and 100% offline Apple on-device (`NaturalLanguage` / `Vision`) inference:

| Constructor | Default Base URL | Auth Env Var | Capabilities |
| :--- | :--- | :--- | :--- |
| `ai.openai(api_key = "")` | `https://api.openai.com/v1` | `OPENAI_API_KEY` | `/chat/completions`, `/embeddings`, SSE streaming, `json_schema`, tools, vision/PDF/audio |
| `ai.anthropic(api_key = "")` | `https://api.anthropic.com/v1` | `ANTHROPIC_API_KEY` | `/v1/messages`, SSE streaming, tool use, native image/PDF blocks |
| `ai.gemini(api_key = "")` | `https://generativelanguage.googleapis.com/v1beta` | `GEMINI_API_KEY` | `:generateContent`, `:streamGenerateContent`, `:embedContent`, `responseSchema`, `inlineData` |
| `ai.ollama(host = "http://127.0.0.1:11434")` | `http://127.0.0.1:11434` | `OLLAMA_HOST` | Local `/v1/chat/completions` & `/api/embed` (vision, tools, structured JSON schema) |
| `ai.local()` / `ai.apple()` | On-device (`NaturalLanguage` / `Vision` / `Tesseract` / SIMD) | None (offline) | Cross-platform on-device `NLEmbedding`/trigram embeddings, `NLLanguageRecognizer`/script classifier, `Vision`/`Tesseract` OCR, local synthesis |
| `ai.mock(handler)` / `ai.mock_sequence(turns)` | In-memory deterministic test double | None (offline) | Offline unit testing for multi-turn `ai.Agent` tool loops and validation retries |

### Schema-Typed Tool Calling, `validate:"..."` & `generate_struct[T]`
Because `cfire` reflects struct tags at compile time, you never hand-write JSON Schema strings for LLM tool definitions or structured output extraction. Attach `validate:"..."` constraints (`min`, `max`, `min_len`, `max_len`, `non_empty`, `oneof=a|b|c`) and pass any struct `T` to `ai.tool_def[T](name, desc)`, `client.generate_struct[T](prompt)`, or `agent.run_struct[T](prompt)`:

```cfire
package main

import "ext:ai" as ai

type WeatherLookup struct {
    City: str `json:"city" desc:"City and state/country, e.g. Austin, TX"`
    Unit: str `json:"unit" desc:"Temperature unit: celsius or fahrenheit"`
}

type IncidentTicket struct {
    Service:  str `json:"service"  desc:"Affected microservice name"`
    Severity: i32 `json:"severity" desc:"Severity from 1 (critical) to 5 (minor)"`
}

fn main() -> void! {
    weather_tool := ai.tool_def[WeatherLookup]("get_weather", "Fetches current weather for a city")
    println("Tool '{}' schema has city: {}", weather_tool.name, weather_tool.parameters_schema.contains("\"city\""))

    client := ai.local()
    ticket := try client.generate_struct[IncidentTicket](
        "Service=auth-gateway experienced p99 latency spike (severity 2)"
    )
    println("Structured ticket: service={}, severity={}", ticket.Service, ticket.Severity)
    println("Lang(FR)={}, Lang(JA)={}", client.detect_language("Bonjour le monde, comment allez-vous"), client.detect_language("これは日本語のテストです"))
}
```
```text
# Output:
Tool 'get_weather' schema has city: true
Structured ticket: service=auth-gateway, severity=2
Lang(FR)=fr, Lang(JA)=ja
```

### Offline Document Ingestion & SIMD Vector RAG
`ai.load_document(path)` and `ai.parse_document(bytes, filename)` extract structured sections (`[]DocumentSection { index, kind, title, text }`) from `.pdf`, `.docx`, `.pptx`, `.xlsx`, `.odt`, `.odp`, `.ods`, `.epub`, `.html`, `.csv`, and `.md` files using a built-in pure-C++23 DEFLATE/ZIP/XML/PDF parser. Combine `ai.chunk_document`, `client.embed(...)` (`ai.local()`), and `ext:db/sqlite` for an offline vector RAG pipeline:

```cfire
package main

import "ext:ai" as ai
import "ext:db/sqlite" as sqlite

type RagHit struct {
    Page:  i32 `sql:"page_num"`
    Title: str `sql:"title"`
    Score: f64 `sql:"score"`
}

fn main() -> void! {
    raw_doc := "# Storage Engine\nWrite-Ahead Logging (WAL) enables concurrent readers.\n\n# Network Layer\nRFC 6455 WebSockets provide full-duplex framing."
    doc     := try ai.parse_document(raw_doc.bytes(), "architecture.md")
    chunks  := ai.chunk_document(doc, 256, 32)

    client := ai.local()
    mut db := try sqlite.open(":memory:")
    defer db.close()
    try db.exec("CREATE TABLE chunks (page_num INT, title TEXT, body TEXT, vec BLOB)")

    for ch in chunks {
        v := try client.embed(ch.text)
        try db.exec("INSERT INTO chunks VALUES (?1, ?2, ?3, ?4)", ch.index, ch.title, ch.text, v)
    }

    qvec := try client.embed("concurrent database WAL readers")
    hits := try db.query[RagHit](
        "SELECT page_num, title, vec_cosine_similarity(vec, ?1) AS score FROM chunks ORDER BY score DESC LIMIT 1",
        qvec
    )
    println("Top RAG hit: section #{} '{}' (score > 0.2: {})", hits[0].Page, hits[0].Title, hits[0].Score > 0.2)
}
```
```text
# Output:
Top RAG hit: section #1 'Storage Engine' (score > 0.2: true)
```

### Building Model Context Protocol (`McpServer`) Tools
Want to expose your `cfire` functions as tools inside Claude Desktop, Cursor, or another MCP host? `make(ai.McpServer, name, version)` and `mcp.register_tool[T](...)` wire up schema validation and JSON-RPC 2.0 dispatch in a few lines:

```cfire
package main

import "ext:ai" as ai

type SearchArgs struct {
    query: str `json:"query" desc:"Search phrase"`
    limit: i64 `json:"limit" desc:"Maximum results to return"`
}

fn main() {
    mut mcp := make(ai.McpServer, "cfire-kb-server", "1.0.0")

    mcp.register_tool[SearchArgs]("search_kb", "Searches the internal knowledge base", |args: SearchArgs| -> str! {
        return "[{{\"query\":\"{args.query}\",\"limit\":{args.limit}}}]"
    })

    // Test JSON-RPC 2.0 dispatch in memory (call mcp.serve_stdio() in production)
    req  := "{\"jsonrpc\":\"2.0\",\"id\":7,\"method\":\"tools/call\",\"params\":{\"name\":\"search_kb\",\"arguments\":{\"query\":\"SIMD\",\"limit\":3}}}"
    resp := mcp.handle_jsonrpc(req)
    println("MCP JSON-RPC response contains SIMD: {}", resp.contains("SIMD"))
}
```
```text
# Output:
MCP JSON-RPC response contains SIMD: true
```

### Multi-Turn Agent Loops & Self-Correcting Validation (`ai.Agent`)
Inspired by Pydantic AI, `make(ai.Agent, client, model, system_prompt)` orchestrates multi-turn tool calls, shares tool registries with `ai.McpServer` via `agent.use_mcp(mcp)`, and automatically feeds `validate:"..."` or custom validator errors back to the model to self-correct (`run_struct[Out]`, `run_struct_validated[Out]`):

```cfire
package main

import "ext:ai" as ai

type MetricArgs struct {
    service: str `json:"service" validate:"non_empty"`
}

type HealthReport struct {
    service:  str `json:"service"  validate:"non_empty"`
    severity: i64 `json:"severity" validate:"min=1,max=5"`
    env:      str `json:"env"      validate:"oneof=dev|staging|prod"`
}

fn main() -> void! {
    // Deterministic offline mock sequence:
    // Turn 1: LLM calls tool `fetch_metrics`
    // Turn 2: LLM returns invalid severity=9 (triggers self-correction retry!)
    // Turn 3: LLM self-corrects to severity=2
    mut step := 0
    client := ai.mock(|msgs: []ai.Message, tools: []ai.Tool| -> ai.ChatResponse! {
        step += 1
        if step == 1 {
            mut res := ai.ChatResponse{}
            res.tool_calls.push(ai.ToolCall{id: "call_1", name: "fetch_metrics", arguments_json: "{\"service\":\"billing\"}"})
            return res
        }
        if step == 2 {
            return ai.ChatResponse{text: "{\"service\":\"billing\",\"severity\":9,\"env\":\"prod\"}"}
        }
        return ai.ChatResponse{text: "{\"service\":\"billing\",\"severity\":2,\"env\":\"prod\"}"}
    })

    mut agent := make(ai.Agent, client, "mock-model", "You are an SRE assistant.")
    agent.register_tool[MetricArgs]("fetch_metrics", "Fetches service telemetry", |args: MetricArgs| -> str! {
        return "p99=420ms for {args.service}"
    })

    report := try agent.run_struct[HealthReport]("Assess billing service health")
    println("Agent report: service={}, severity={}, env={}, steps={}", report.service, report.severity, report.env, step)
}
```
```text
# Output:
Agent report: service=billing, severity=2, env=prod, steps=3
```

---

## Chapter 18: Hybrid Desktop & Mobile Applications (`ext:webui`)

### Why `ext:webui`? Architecture & Zero-Port VFS (`app://localhost/`)
Shipping a desktop or mobile UI shouldn't require bundling a 150 MB Chromium runtime (Electron) or exposing a localhost TCP port that other local processes can probe.

`import "ext:webui"` gives you a Wails/Tauri-style hybrid application engine built directly on the operating system's native WebView (`WKWebView` on macOS and iOS, `WebKitGTK` on Linux) coupled to your `cfire` backend over an in-process JSON-RPC 2.0 and pub/sub event bridge:

- **Zero-Port Custom URI Scheme (`app://localhost/`)**: Frontend assets are served directly from memory or disk via `WKURLSchemeHandler` (macOS/iOS) and `webkit_web_context_register_uri_scheme` (Linux). No TCP socket is ever bound.
- **Three Asset Loading Strategies**:
  1. `app.set_html(html_str)`: Inline a self-contained HTML/CSS/JS document for single-file utilities and tests.
  2. `app.embed_asset(path, content, mime)`: Register individual virtual files (`"/app.js"`, `"/styles.css"`) in memory (`mime = ""` auto-detects `.html`, `.css`, `.js`, `.mjs`, `.json`, `.svg`, `.wasm`, `.png`, `.ico`).
  3. `app.serve_dir("ui")`: Serve a directory tree from disk with automatic path-traversal protection (`..` segments are rejected with `403`). At runtime, `serve_dir` automatically resolves relative paths across your working directory (`./ui`), packaged macOS app bundles (`<App>.app/Contents/Resources/ui`), and iOS Simulator/device bundles (`<App>.app/ui`).

Scaffold a complete hybrid app in one command, or run the canonical studio example on desktop or iOS Simulator:
```bash
cfire new my-studio --template webui
cfire run examples/17-webui-studio/main.cf
cfire run --ios examples/17-webui-studio/main.cf
```

### Typed RPC Bindings (`bind[T]`, `bind_action`, `bind_raw`) & `window.backend`
When you import `ext:webui`, the compiler automatically emits `cfire::json::parse_struct<T>` and `stringify_struct<T>` reflection bridges for every struct in your package. You can expose backend functions to JavaScript in three ways depending on how structured the call is:

| Method | Signature | When to Use It |
| :--- | :--- | :--- |
| `app.bind[Req](name) \|req\| { ... }` | `fn[Req, Resp](name: str, handler: fn(Req) -> Resp)` | **Typed Struct RPC (Recommended)**: Deserializes the JS argument object into `req: Req` via `` `json:"..."` `` tags and serializes the returned `Resp` struct (or `Resp!`) back to JS. |
| `app.bind_action(name) \|\| { ... }` | `fn[Resp](name: str, handler: fn() -> Resp)` | **Zero-Argument Action**: Binds a callback that takes no payload (`await window.backend.reset_peaks()`) and returns a struct, primitive, or JSON string. |
| `app.bind_raw(name) \|raw_json\| { ... }` | `fn(name: str, handler: fn(str) -> str)` | **Dynamic JSON Escape Hatch**: Receives the raw JSON argument string and returns a raw JSON string (`"{...}"`). |

On the frontend, `ext:webui` injects a preload script before any page scripts run. Every bound function is callable as an `async` method on the `window.backend` ES6 `Proxy` (or via `window.invoke(method, args)`):

```javascript
// Inside your frontend UI JavaScript:
const mix = await window.backend.update_track({
    channel: 1,
    name: "Poly Synth",
    gain_db: 3.5,
    muted: false,
});
console.log(mix.peak_pct, mix.status); // 83.75, "ACTIVE"
```

Let's wire up both `app.bind[T]` and `app.bind_action` on the `cfire` side:

```cfire
package main

import "ext:webui"

type TrackMix struct {
    channel: i64  `json:"channel"`
    name:    str  `json:"name"`
    gain_db:  f64  `json:"gain_db"`
    muted:   bool `json:"muted"`
}

type MixResult struct {
    channel: i64  `json:"channel"`
    peak_pct: f64  `json:"peak_pct"`
    clipped: bool `json:"clipped"`
    status:  str  `json:"status"`
}

fn main() -> void! {
    mut app := make(webui.App, "cfire Studio", 1024, 680)

    // 1. Typed struct-in -> struct-out RPC binding
    app.bind[TrackMix]("update_track") |req| {
        mut peak := 75.0 + req.gain_db * 2.5
        mut label := "ACTIVE"
        if req.muted {
            peak = 0.0
            label = "MUTED"
        }
        return MixResult{
            channel: req.channel,
            peak_pct: peak,
            clipped: peak >= 100.0,
            status:  label,
        }
    }

    // 2. Zero-argument action binding
    app.bind_action("ping_engine") || {
        return "{\"engine\":\"ready\"}"
    }

    res := try app.call_rpc("update_track", "{\"channel\":1,\"name\":\"Synth\",\"gain_db\":4.0,\"muted\":false}")
    println("RPC response: {}", res)
}
```
```text
# Output:
RPC response: {"channel":1,"peak_pct":85,"clipped":false,"status":"ACTIVE"}
```

### Bidirectional Event Bus & Reactive State (`on`, `emit`, `set_state`)
RPC is great when the user clicks a button and waits for a reply, but live telemetry, audio meters, and background progress bars need push events and shared state in both directions:

| Backend (`cfire`) Method | Frontend (`window.runtime`) Counterpart | Behavior |
| :--- | :--- | :--- |
| `app.on(topic) \|payload\| { ... }` | `window.runtime.EventsEmit(topic, data)` | Frontend publishes `topic`; all matching `cfire` `app.on` listeners receive the serialized JSON payload. |
| `app.emit(topic, payload)` | `const off = window.runtime.EventsOn(topic, fn)` | Backend broadcasts `topic` (struct or JSON string) to all subscribed JS listeners (`EventsOnce` auto-unsubscribes after 1 event; `EventsOff(topic)` clears a topic). |
| `app.set_state(key, value)` / `app.get_state(key)` | `window.runtime.EventsOn("state:change", fn)` | Updates the thread-safe reactive key-value store on the backend and automatically broadcasts `"state:change"` with `{"key": ..., "value": ...}` to the WebView. |

```cfire
package main

import "ext:webui"

type TransportEvent struct {
    playing: bool `json:"playing"`
    bpm:     i64  `json:"bpm"`
}

fn main() {
    mut app := make(webui.App, "Event Bus Demo", 800, 600)

    // Listen for events emitted from JavaScript via window.runtime.EventsEmit("transport:toggle", ...)
    app.on("transport:toggle") |payload| {
        app.set_state("transport", payload)
        app.emit("transport:ack", TransportEvent{ playing: true, bpm: 128 })
    }

    // Simulate an incoming frontend event in headless mode
    app.dispatch_event("transport:toggle", "{\"playing\":true}")
    println("state={} emitted_count={} last_topic={}",
        app.get_state("transport"), app.emitted_events_count(), app.last_emitted_topic())
}
```
```text
# Output:
state={"playing":true} emitted_count=2 last_topic=transport:ack
```

### Frameless Window Chrome, Drag Regions & Native OS Dialogs
To make a hybrid app feel like a native macOS or Linux tool rather than a browser tab, `ext:webui` gives both your `.cf` backend and `window.runtime` full control over window chrome, custom titlebar drag regions, and native OS file/message dialogs:

| Window & Dialog API | Signature / Usage | Description |
| :--- | :--- | :--- |
| `app.set_title(title)` / `set_size(w, h)` | `fn(str)` / `fn(i32, i32)` | Sets window title and initial/live logical pixel dimensions. |
| `app.set_min_size(w, h)` / `set_max_size(w, h)` | `fn(i32, i32)` | Constrains user window resizing bounds. |
| `app.set_frameless(true)` / `set_transparent(true)` | `fn(bool)` | Hides the OS titlebar (`NSWindowStyleMaskFullSizeContentView` on macOS) or enables transparent WebView backgrounds. |
| `app.set_dark_mode(true)` / `set_always_on_top(true)` | `fn(bool)` | Forces `NSAppearanceNameDarkAqua` (macOS) / dark GTK theme or floats the window above other apps. |
| `app.center()` / `fullscreen()` / `minimize()` / `maximize()` | `fn()` | Controls window placement (also callable from JS via `window.runtime.WindowCenter()`, `WindowMinimise()`, `WindowMaximise()`, `WindowFullscreen()`, `Quit()`). |
| **CSS / HTML Drag Regions** | `data-webui-drag-region` or `--wails-draggable: drag` | Add `data-webui-drag-region` (or CSS `-webkit-app-region: drag`) to your custom HTML header bar to initiate native OS window dragging (`performWindowDragWithEvent:` on macOS, `begin_move_drag` on GTK); interactive `<button>` and `<input>` children remain clickable automatically. |
| `app.open_file_dialog(title, dir, filter)` | `fn(str, str = "", str = "") -> str` | Opens a native `NSOpenPanel` (macOS) or `zenity` file picker and returns the selected path. |
| `app.save_file_dialog(title, default_name)` | `fn(str, str = "") -> str` | Opens a native `NSSavePanel` save dialog and returns the target path. |
| `app.message_dialog(title, message, level)` | `fn(str, str, str = "info") -> bool` | Displays a native `NSAlert` modal (`"info"`, `"warning"`, `"error"`, or `"confirm"` with Cancel/OK buttons). |

### Headless & Live Offscreen `WKWebView` Testing (`cfire test`)
Most WebView frameworks are painful to test in CI because spinning up a visible GUI window hangs headless runners. `ext:webui` gives you two tiers of automated verification inside standard `cfire test` blocks:

1. **Millisecond Headless Contract Tests**: Use `try app.call_rpc(method, json)`, `app.dispatch_event(topic, json)`, `app.resolve_asset_content(path)`, and `app.resolve_asset_mime(path)` to test your RPC serialization, state mutations, and VFS security in memory on every platform.
2. **Real Offscreen `WKWebView` DOM + JS Integration Tests (`app.run_webview_test(timeout_ms)`)**: On macOS, calling `try app.run_webview_test(3000)` boots a real offscreen `WKWebView`, loads your HTML/CSS/JS over `app://localhost/`, executes your frontend JavaScript against your live `cfire` `app.bind[T]` handlers, and waits up to `timeout_ms` for your script to set `window.__TEST_PASSED = true` (or fails immediately with `window.__TEST_ERROR`):

```cfire
package main

import "ext:webui"

type EchoReq struct {
    text: str `json:"text"`
}

fn main() -> void! {
    mut app := make(webui.App, "WebView Test Runner", 640, 480)
    app.bind[EchoReq]("upper_echo") |req| {
        return "{\"reply\":\"" + req.text + "!\"}"
    }
    app.set_html(`<!doctype html><html><body><script>
        (async () => {
            try {
                const res = await window.backend.upper_echo({ text: "cfire" });
                if (res.reply === "cfire!") {
                    window.__TEST_PASSED = true;
                } else {
                    window.__TEST_ERROR = "unexpected reply: " + JSON.stringify(res);
                }
            } catch (e) {
                window.__TEST_ERROR = String(e);
            }
        })();
    </script></body></html>`)

    // 1. Fast headless RPC & VFS check
    rpc_out := try app.call_rpc("upper_echo", "{\"text\":\"studio\"}")
    mime    := app.resolve_asset_mime("/index.html")

    // 2. Real offscreen WKWebView round-trip test (auto-skips on headless Linux without display)
    try app.run_webview_test(3000)
    println("rpc={} mime={} webview_test=PASSED", rpc_out, mime)
}
```
```text
# Output:
rpc="{\"reply\":\"studio!\"}" mime=text/html; charset=utf-8 webview_test=PASSED
```

---

## Chapter 19: Production Patterns, Cloud Services & Canonical Examples

### Blueprint: RAG + HTTP/JSON Microservice
Let's pull `ext:db/sqlite` (WAL mode + SIMD vector search), `ext:ai` (local embeddings), `core:json` (compile-time struct serialization), and `core:net/http` (pattern-matched HTTP router) together into a single, zero-dependency microservice:

```cfire
package main

import "ext:ai" as ai
import "ext:db/sqlite" as sqlite
import "core:json" as json
import "core:net/http" as http

type SearchRequest struct {
    query: str `json:"query"`
    top_k:  i64 `json:"top_k"`
}

type SearchResult struct {
    title: str `sql:"title" json:"title"`
    score: f64 `sql:"score" json:"score"`
}

fn main() -> void! {
    // 1. Initialize in-memory SQLite WAL database and ingest knowledge base
    mut db := try sqlite.open(":memory:")
    defer db.close()
    try db.exec("CREATE TABLE kb (title TEXT, body TEXT, vec BLOB)")

    docs := [
        "Lock-Free Audio: core:audio mixes 64 stereo f32 voices in real time.",
        "Zero-Cost Safety: cfire lowers T! and T? to std::expected and std::optional.",
        "TLS 1.3 & WebSockets: core:net/http upgrades RFC 6455 connections natively."
    ]
    client := ai.local()
    for d in docs {
        parts := d.split(": ")
        vec   := try client.embed(d)
        try db.exec("INSERT INTO kb VALUES (?1, ?2, ?3)", parts[0], d, vec)
    }

    // 2. Attach an HTTP JSON endpoint querying SQLite SIMD vector similarity
    mut router := make(http.Router)
    router.post("/v1/search", |req: http.Request| -> http.Response {
        q := json.parse[SearchRequest](req.body) or { return http.Response.json(400, "{\"error\":\"invalid JSON\"}") }
        qvec := client.embed(q.query) or { return http.Response.json(500, "{\"error\":\"embed failed\"}") }
        hits := db.query[SearchResult](
            "SELECT title, vec_cosine_similarity(vec, ?1) AS score FROM kb ORDER BY score DESC LIMIT ?2",
            qvec, q.top_k
        ) or { return http.Response.json(500, "{\"error\":\"query failed\"}") }
        return http.Response.json(200, json.stringify(hits))
    })

    // 3. Verify end-to-end execution via router.dispatch
    res := router.dispatch(http.Request{
        method: "POST",
        path:   "/v1/search",
        body:   "{\"query\":\"64 stereo voices mixer\",\"top_k\":1}"
    })
    parsed := try json.parse[[]SearchResult](res.body)
    println("HTTP {} -> top hit: '{}'", res.status, parsed[0].title)
}
```
```text
# Output:
HTTP 200 -> top hit: 'Lock-Free Audio'
```

### Cloud & Local Service Primitives (`core:tako`)
When you want to prototype or test AWS-style cloud workflows—IAM permission evaluation, S3 object buckets, DynamoDB key-value tables, SQS FIFO queues, Secrets Manager, and CloudWatch metrics/alarms—without network credentials or cloud bills, `import "core:tako"` (scaffolded via `cfire new my-cloud --template tako`) provides an in-memory cloud runtime backed by `pkg/tako`:

```cfire
package main

import "core:tako" as tk

fn main() -> void! {
    mut s3  := tk.NewS3Service()
    mut sqs := tk.NewSQSService()

    try s3.CreateBucket("artifacts")
    etag := try s3.PutObject("artifacts", "release/v1.tar.zst", "zstd-frame-bytes", "application/zstd")
    obj  := try s3.GetObject("artifacts", "release/v1.tar.zst")

    q_url  := try sqs.CreateQueue("build-jobs", true)
    msg_id := try sqs.SendMessage(q_url, "{\"target\":\"arm64\"}", "builds", "dedup-01")
    msgs   := try sqs.ReceiveMessages(q_url, 1)

    println("s3_etag_ok={} body={} sqs_msg={} received={}",
        !etag.is_empty(), obj.Body, !msg_id.is_empty(), msgs.len())
}
```
```text
# Output:
s3_etag_ok=true body=zstd-frame-bytes sqs_msg=true received=1
```

### Canonical Projects Index (`examples/01`–`17`)
Every project in [`examples/`](../examples) is continuously verified by `make test-examples` and can be run directly from the repository root:

| Directory | Title | Core Technologies Demonstrated | Run / Build Command |
| :--- | :--- | :--- | :--- |
| [`examples/01-hello-core`](../examples/01-hello-core) | Hello Core & Defer | Mutability (`mut`), parameter intents (`mut`, `move`), `defer` scope guards, string interpolation | `cfire run examples/01-hello-core/main.cf` |
| [`examples/02-log-compressor`](../examples/02-log-compressor) | Log Compressor | Declarative CLI flag parsing (`core:cli`), RFC 8878 Zstandard compression (`ext:compress/zstd`), `core:fs` | `cfire run examples/02-log-compressor/main.cf` |
| [`examples/03-pkg-cache`](../examples/03-pkg-cache) | Package Cache | SQLite3 WAL transactions (`ext:db/sqlite`), compile-time JSON struct mapping (`core:json`), SHA-256 (`core:crypto/sha256`) | `cfire run examples/03-pkg-cache/main.cf` |
| [`examples/04-async-workers`](../examples/04-async-workers) | Async Workers | Scoped thread pool concurrency (`async.scope`), disjoint slice writes, MPMC `Channel[T]` (`core:sync`) | `cfire run examples/04-async-workers/main.cf` |
| [`examples/05-cmake-interop`](../examples/05-cmake-interop) | CMake Interop | Clang JSON AST ingestion (`import cpp "<zstd.h>"`), `build.CMake` native library orchestration | `cfire run examples/05-cmake-interop/main.cf` |
| [`examples/06-raylib-viz`](../examples/06-raylib-viz) | Raylib Visualization | Hardware 2D graphics loop (`vendor:raylib`), 60 FPS particle & telemetry rendering (`--web` & `--ios` ready) | `cfire run examples/06-raylib-viz/main.cf` |
| [`examples/07-types-match-map`](../examples/07-types-match-map) | Types & Match | Algebraic `enum` sum types (`std::variant`), exhaustive `match`, `map[K]V`, `interface` C++20 concepts | `cfire run examples/07-types-match-map/main.cf` |
| [`examples/08-multi-pkg-cc-build`](../examples/08-multi-pkg-cc-build) | Multi-Package Build | Multi-package `build.cf` orchestration, static archive compilation (`build.CC`) | `cfire run examples/08-multi-pkg-cc-build` |
| [`examples/09-stdlib-test-pointers`](../examples/09-stdlib-test-pointers) | Stdlib & Tests | Smart pointers (`*T`, `^T`), `test "..."` blocks, `core:testing` assertions | `cfire test examples/09-stdlib-test-pointers/main.cf` |
| [`examples/10-cmake-and-make-deps`](../examples/10-cmake-and-make-deps) | Multi-Build Deps | Simultaneous `build.CMake` and `build.Make` external dependency orchestration in a single `build.cf` | `cfire run examples/10-cmake-and-make-deps` |
| [`examples/11-http-server-router`](../examples/11-http-server-router) | HTTP Web Server | Multi-threaded HTTP/1.1 server (`core:net/http`), fluent request router, URL `:param` & query extraction | `cfire run examples/11-http-server-router/main.cf` |
| [`examples/12-raygui-controls`](../examples/12-raygui-controls) | Raygui GUI Kit | Immediate-mode controls (`vendor:raygui`): checkboxes, sliders, progress bars, combo boxes, `&mut str` text boxes | `cfire run examples/12-raygui-controls/main.cf` |
| [`examples/13-ios-native-ui`](../examples/13-ios-native-ui) | Native iOS UI | Zero-coordinate `GuiIos*` auto-layout kit (`examples/13-ios-native-ui/ui/kit.cf`), safe-area insets, real-time `core:audio` metering (`--ios`, `--web`, desktop) | `cfire run --ios examples/13-ios-native-ui` |
| [`examples/14-ai-rag-sqlite`](../examples/14-ai-rag-sqlite) | AI Vector RAG | Local normalized embeddings (`ext:ai`), SQLite `vec_cosine_similarity` (`ext:db/sqlite`), compile-time `json.schema[T]()` | `cfire run examples/14-ai-rag-sqlite/main.cf` |
| [`examples/15-mcp-server`](../examples/15-mcp-server) | MCP Agent Server | Model Context Protocol server (`make(ai.McpServer, ...)`), schema-typed `register_tool[T]`, JSON-RPC 2.0 dispatch | `cfire run examples/15-mcp-server/main.cf` |
| [`examples/16-audio-synth`](../examples/16-audio-synth) | Audio Synthesis | Procedural SFX (`audio.gen_sfx`), ADSR tone synthesis (`audio.gen_tone`), polyphonic mixer, RIFF/WAV file codec (`core:audio`) | `cfire run examples/16-audio-synth/main.cf` |
| [`examples/17-webui-studio`](../examples/17-webui-studio) | Hybrid WebUI Studio | Wails/Tauri-style native WebView (`ext:webui`), typed `app.bind[T]` JSON-RPC 2.0, `window.backend` ES6 Proxy, `app://localhost/` VFS, offscreen `WKWebView` test (`--ios` & desktop) | `cfire run examples/17-webui-studio/main.cf` |

### Further Reference & Module Index
- **Standard Library API Reference**: Browse [`reference.html`](./reference.html) (or [`STDLIB_REFERENCE.md`](./STDLIB_REFERENCE.md)) for every function signature, C++23 lowering expression, and diagnostic code in the compiler catalog.
- **LLM Context Snapshot**: Feed [`LLMS.txt`](./LLMS.txt) or start `cfire mcp` to give coding agents full access to the compiler's type checker, formatter, and standard library catalog.

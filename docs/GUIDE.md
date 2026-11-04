# The `cfire` Language & Systems Programming Guide

**cfire** (pronounced *sapphire* • `/ˈsæf.aɪ.ər/`) is a systems programming language that compiles 1-to-1 to human-readable ISO C++23. It provides call-site mutability enforcement (`in`, `mut`, `move`), value-based error handling (`T!`), lexical structured concurrency (`async.scope`), zero-wrapper C/C++ header ingestion (`import cpp`), and a single-binary multi-target toolchain covering native desktop/server (`x86_64` / `arm64`), WebAssembly (`--web` / `--target=wasm`), and native iOS (`--ios`).

Every chapter in this manual is structured for both **sequential reading** and **random-access engineering reference**:
- **Architecture & Invariants**: Exact compile-time guarantees, memory layout, and ISO C++23 lowering rules.
- **Specification Tables**: Complete syntax forms, CLI flags, struct tags, or API parameters.
- **Progressive Mechanics (Levels 1–4)**: Runnable `.cf` source code paired with numbered execution walkthroughs and expected terminal output.
- **Diagnostics & Edge Cases**: Compile-time error codes (`CF1xxx`–`CF5xxx`, `cfire/MUT-ALIAS`) and runtime failure behavior.

---

# Part I: Getting Started & Tooling

## Chapter 1: Installation & Platform Setup

### Compilation Pipeline & Architecture
`cfire` is distributed as a single self-contained native binary with zero external runtime dependencies. Compiling a `.cf` target executes a deterministic 5-stage pipeline:

1. **Lexical & Resilient Syntax Analysis (`lexer` / `parser`)**: Tokenizes `.cf` source files and constructs a typed Abstract Syntax Tree (AST) with statement-level error recovery for IDE/LSP responsiveness.
2. **Clang AST Bridge (`clang_bridge`)**: For any `import cpp "header.h"` declaration, invokes the system Clang frontend (`-Xclang -ast-dump=json`) to extract C/C++ function signatures, structs, enums, and preprocessor macros without writing manual FFI bindings.
3. **Semantic & Borrow Verification (`sema`)**: Resolves package imports, infers types, checks exhaustiveness on `match` statements, validates call-site `mut`/`move` intent markers, performs control-flow-graph (CFG) use-after-move analysis, and rejects escaping stack references.
4. **ISO C++23 Code Generation (`codegen`)**: Emits standard ISO C++23 source code annotated with `#line` directives so debugger frames and sanitizer stack traces point directly to `.cf` filenames and line numbers.
5. **PCH-Accelerated Backend Compilation (`buildsys`)**: Compiles the generated C++23 source against a precompiled header (`cfire_prelude.hpp.pch`) using the host C++23 compiler (`clang++` 18+ or `g++` 14+) and links required system or vendor libraries.

> [!NOTE]
> **Backend Compiler Discovery Order**: `cfire` locates a C++23-capable compiler by checking: (1) the `CFIRE_CXX` environment variable, (2) the `CXX` environment variable, (3) `clang++-18` / `clang++` (macOS Xcode Command Line Tools or LLVM 18+), and (4) `g++-14` / `g++`. Executables produced by `cfire build` are standalone native Mach-O, ELF, WASM, or iOS `.app` binaries with no garbage collector or virtual machine.

### Quick Installation Script (macOS & Linux)
Install the `cfire` binary and standard `runtime/` headers (`~/.local/bin/cfire` and `~/.cfire/runtime`, or `/usr/local/bin/cfire` and `/usr/local/share/cfire/runtime` when run as root):

```bash
curl -fsSL https://cfire.memo.engineer/install.sh | sh
```

To also bootstrap all optional native acceleration libraries (`simdjson`, `libzstd`, `sqlite3`, `openssl`, `cmake`) and pre-compile the C++23 runtime precompiled header (`.pch`) in one command, pass `--setup`:

```bash
curl -fsSL https://cfire.memo.engineer/install.sh | sh -s -- --setup
```

Verify the active compiler binary:
```bash
cfire --version
# Output: cfire 0.9.4 (ISO C++23 systems toolchain)
```

### Toolchain Health & Setup (`cfire doctor`)
Run `cfire doctor` at any time on a new machine to inspect the C++23 compiler, `runtime/` header discovery, `.pch` cache, `pkg-config` tools, native `core:*` engine libraries, and cross-compilation SDKs (`em++`, iOS Simulator):

```bash
cfire doctor
cfire doctor --json   # Machine-readable JSON report for CI & coding agents
```

If any optional native libraries or build tools are missing, run `cfire setup` to install them automatically via the host package manager (`brew` on macOS; `apt-get`, `dnf`, `pacman`, or `apk` on Linux), sync `runtime/` headers to `~/.cfire/runtime`, and warm the C++23 prelude `.pch`:

```bash
cfire setup --yes             # Install missing native libraries + warm C++23 PCH
cfire setup --yes --raylib    # Also pre-fetch vendor:raylib into global cache
cfire setup --yes --wasm      # Also install Emscripten (em++) for --target=wasm
```

### Manual Source Build & Platform Toolchains

#### macOS (Apple Silicon `arm64` & Intel `x86_64`)
1. Install Xcode Command Line Tools (provides `clang++`, macOS SDK headers, and system frameworks):
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
   You can also bundle a self-relocating portable toolchain directory (`bin/cfire` + `share/cfire/runtime`) anywhere on disk via `./cfire install --portable <dir>` or install to a custom prefix via `./cfire install --prefix <dir>`.

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
   sudo pacman -S clang make pkgconf simdjson zstd sqlite openssl alsa-lib
   cd cfire && sudo make install
   ```

#### Docker & OrbStack Containers
Build and run the official Ubuntu 24.04 Linux container image with all `core:*` native engines pre-installed:
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

### Package Namespaces & System Dependency Matrix
`cfire` organizes its 33 built-in modules into three explicit package namespaces:
- **`core:*` (Foundational Core — 21 Modules)**: Pure ISO C++23 header-only primitives (`core:str`, `core:unicode`, `core:regex`, `core:collections`, `core:sort`, `core:fs`, `core:os`, `core:time`, `core:math`, `core:rand`, `core:sync`, `core:sync/context`, `core:cli`, `core:log`, `core:crypto/*`, `core:encoding/*`) with zero external C/OS library dependencies.
- **`ext:*` (Domain & Platform Extensions — 10 Opt-In Modules)**: Domain and platform engines (`ext:json`, `ext:compress/zstd`, `ext:db/sqlite`, `ext:net`, `ext:net/url`, `ext:net/tls`, `ext:net/http`, `ext:net/websocket`, `ext:audio`, `ext:ai`) that query `pkg-config` and standard system paths only when imported, with built-in portable fallbacks when an optional system library is absent.
- **`vendor:*` (Bundled Third-Party C/C++ Engines — 2 Modules)**: `vendor:raylib` and `vendor:raygui`.

| Extension / Vendor Module | Optional System Library | `pkg-config` Name | Built-In Portable Fallback When Absent |
| :--- | :--- | :--- | :--- |
| `ext:json` | `libsimdjson` | `simdjson` | Bundled single-header `simdjson` implementation in `runtime/vendor/` |
| `ext:db/sqlite` | `libsqlite3` | `sqlite3` | System `libsqlite3` (pre-installed on macOS/iOS; in-memory/WASM shim on `--web`) |
| `ext:compress/zstd` | `libzstd` | `libzstd` | Native `libcompression` (`COMPRESSION_LZFSE` / `ZLIB`) on Apple/iOS; RFC 1951 codec on WASM |
| `ext:net/tls` & `ext:net/http` | `libssl` / `libcrypto` | `openssl` | Dynamic `dlopen("libssl")` on Desktop/Linux; Apple `Foundation` `NSURLConnection` on macOS/iOS; browser `XMLHttpRequest` on WASM |
| `ext:audio` | `CoreAudio` / `libasound2` | `alsa` | `AudioToolbox.framework` on macOS/iOS; dynamic `dlopen("libasound.so.2")` on Linux; `WebAudio` on WASM; deterministic software mixer in headless CI |
| `ext:ai` (OCR & PDF) | `Vision` / `libtesseract` / `pdftotext` | `tesseract` | `Vision.framework` & `PDFKit` on macOS/iOS; `dlopen("libtesseract.so.5")` & `pdftotext` on Linux; pure C++23 `/FlateDecode` PDF parser & PNG/JPEG metadata reader on all targets |
| `vendor:raylib` & `vendor:raygui` | Bundled in `runtime/` | None required | Self-contained Cocoa/OpenGL desktop backend, X11/OpenGL Linux backend, WebGL/HTML5 Canvas on WASM, and `UIKit` + `OpenGLES3` on iOS |

### Cross-Compilation Target Setup

#### WebAssembly (`--web` / `--target=wasm`)
Compiling `.cf` applications to WebAssembly (`.html` + `.js` + `.wasm`) requires the Emscripten SDK (`em++` in `PATH`):
```bash
git clone https://github.com/emscripten-core/emsdk.git
cd emsdk && ./emsdk install latest && ./emsdk activate latest
source ./emsdk_env.sh
```
- **Execution**: `cfire run --web app.cf` compiles with `-std=c++23 -O2 -msimd128` and starts a local HTTP server (or executes via `node` for CLI-profile targets).
- **Profiles**: GUI applications importing `vendor:raylib` or `ext:audio` default to the `canvas` profile (HTML5 WebGL canvas + drag-and-drop `MEMFS` file staging); console applications default to the `cli` profile.

#### Native iOS Simulator & Device (`--ios`)
Building for iOS requires macOS with full Xcode installed (not just Command Line Tools) so `xcrun` can resolve the `iphonesimulator` and `iphoneos` SDK sysroots:
```bash
xcode-select -p
xcrun --sdk iphonesimulator --show-sdk-path
```
- **Output Layout**: `cfire build --target=ios-sim app.cf -o build/MyApp` generates a complete `build/MyApp.app/` bundle containing the Mach-O executable (`arm64-apple-ios16.0-simulator`), `Info.plist`, `PkgInfo`, and bundled assets/icons, then signs it with `codesign --force --sign -`.
- **Execution**: `cfire run --ios app.cf` boots the active iOS Simulator via `xcrun simctl`, installs the `.app` bundle, and streams stdout/stderr back to your terminal.

### AI Agent (MCP) & Editor (LSP) Setup

#### Model Context Protocol Server (`cfire mcp`)
`cfire mcp` starts a JSON-RPC 2.0 Model Context Protocol server over `stdio` for AI coding agents (Claude Code, Cursor, Antigravity):

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
| `cfire_stdlib_lookup` | `{"query": "str"}` | Searches modules, function signatures, and types matching `query` (e.g., `"ext:ai"` or `"embed"`). |
| `cfire_check` | `{"code"?: "str", "path"?: "str"}` | Parses, type-checks, and verifies borrow/intent rules on inline `.cf` `code` or a workspace `path` (including same-directory sibling files); returns `"OK (0 errors)"` or exact `CFxxxx` diagnostics. |
| `cfire_eval` | `{"code": "str"}` | Compiles and executes a `.cf` program or snippet in a temporary sandbox and returns combined stdout/stderr. |
| `cfire_outline` | `{"path"?: "str"}` | Returns a token-compressed structural AST outline of packages, structs, enums, interfaces, functions, and test blocks. |
| `cfire_inspect` | `{"target": "str"}` | Inspects a stdlib module/symbol, compiler diagnostic rule (`cfire/MUT-CALLSITE`), or foreign C/C++ header (`cpp <header.h>`). |
| `cfire_doc_llm` | `{}` | Returns the complete LLM & AI Agent language reference sheet (`LLMS.txt`). |

#### Editor Syntax & LSP Setup (`cfire ide` & `cfire lsp`)
Install TextMate grammar, filetype associations, and LSP client configurations automatically:
```bash
cfire ide --all       # Configures VS Code, Cursor, Zed, Neovim, and CLion
cfire ide vscode      # Configures only VS Code / Cursor
cfire lsp             # Starts the JSON-RPC 2.0 Language Server on stdio
```
The built-in `cfire lsp` daemon supports debounced background diagnostics (`textDocument/publishDiagnostics`), type/memory-layout hover (`textDocument/hover`), auto-import completion (`textDocument/completion`), signature help, inlay type/parameter hints, go-to-definition (`textDocument/definition`), cross-package find-references and rename (`textDocument/rename`), quick-fix code actions (`cfire/MUT-CALLSITE` and exhaustive `match` arm generation), and range formatting.

---

## Chapter 2: Quickstart & Toolchain Tour

### Level 1 — One-Shot Evaluation (`cfire eval`)
`cfire eval` compiles and runs expressions or statements from the command line without creating a project file. It automatically wraps snippets in `package main` and imports common standard library modules (`core:fs`, `core:str`, `core:math`, `core:time`, `ext:json`, `ext:ai`):

```bash
# 1. Expression mode (-e): evaluates the expression and prints its result
cfire eval -e "2.0 * math.pi() * 10.0"
# Output: 62.83185307179586

# 2. Statement mode: executes one or more semicolon- or newline-separated statements
cfire eval 'nums := [10, 20, 30]; println("count={} first={}", nums.len(), nums[0])'
# Output: count=3 first=10
```

**How It Works:**
1. When `-e <expr>` is passed, `cfire` wraps the expression in `println("{}", (<expr>))` inside an auto-generated `fn main()`.
2. When raw statements are passed without `-e`, `cfire` scans the token stream for module identifiers (`math.`, `str.`, `json.`, `fs.`) and injects the corresponding `import "core:..."` headers automatically.
3. Because the precompiled header (`.cfire/cache/pch/`) is reused across invocations, `cfire eval` executes native C++23 code with near-instant startup.

### Level 2 — Anatomy of a `.cf` Program (`hello.cf`)
Create a standalone source file named `hello.cf`:

```cfire
package main

import "core:str"
import "core:time"

fn main() {
    started_ms := time.now_ms()
    host := str.trim("  cfire-node-01  ")
    println("[{}] node={} ready", started_ms, host)
}
```

Compile and execute in a single step:
```bash
cfire run hello.cf
# Output: [1730462400123] node=cfire-node-01 ready
```

**Line-by-Line Mechanics:**
1. **`package main` (Line 1)**: Every `.cf` file begins with a `package` declaration. An executable entry file must declare `package main` and define `fn main()`.
2. **`import "core:str"` & `import "core:time"` (Lines 3–4)**: Imports built-in standard library modules under their default namespace aliases (`str` and `time`). You can override the alias with `import "core:str" as s`.
3. **`started_ms := time.now_ms()` (Line 7)**: The `:=` operator declares an **immutable** local variable and infers its type (`i64`) from the right-hand expression. Reassigning `started_ms` later in `main` is a compile-time error (`CF3003`).
4. **`println("[{}] node={} ready", ...)` (Line 9)**: Built-in formatted output lowered directly to C++23 `std::println`. Placeholders `{}` accept any primitive, `str`, struct, slice, or format specifier (such as `{:.2f}` for floats or `{:x}` for hex).

### Level 3 — Builds, C++23 Emit & CMake Eject

```bash
# 1. Type-check and verify borrow/intent rules without linking an executable
cfire check hello.cf

# 2. Run under AddressSanitizer (ASan) + UndefinedBehaviorSanitizer (UBSan)
cfire run --sanitize hello.cf

# 3. Compile an optimized (-O2) standalone native binary
cfire build hello.cf -o ./bin/hello
./bin/hello

# 4. Inspect the exact ISO C++23 code generated by the compiler
cfire emit-cpp hello.cf

# 5. Export a zero-dependency C++23 + CMake project tree
cfire eject hello.cf dist/
```

#### Under the Hood: Output of `cfire emit-cpp hello.cf`
Running `cfire emit-cpp hello.cf` prints the exact ISO C++23 translation unit compiled by the backend:

```cpp
// Generated by cfire 0.2.0 — ISO C++23
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

- Notice that immutable `:=` bindings lower to `const` C++23 variables (`const std::int64_t`, `const std::string`).
- Every statement is preceded by a `#line` directive mapping back to `hello.cf`, ensuring `lldb`, `gdb`, and ASan report `.cf` line numbers.

#### Output of `cfire eject hello.cf dist/`
`cfire eject` packages the emitted C++23 source, the required runtime headers, and a standalone `CMakeLists.txt` into `dist/` so the project can be built on any machine with a C++23 compiler—even without `cfire` installed:

```text
dist/
├── CMakeLists.txt          # Standalone CMake 3.25+ build configuration
├── main.cpp                # Generated ISO C++23 source
└── runtime/
    ├── cfire_prelude.hpp   # Core slice, map, error, and RAII defer primitives
    └── core/               # Self-contained standard library headers
```

---

## Chapter 3: Workspaces, CLI & Build System

### CLI Command & Flag Reference
The `cfire` binary provides all build, verification, formatting, refactoring, and agent tooling:

| Command & Syntax | Supported Flags | Description |
| :--- | :--- | :--- |
| `cfire init` / `cfire new <dir>` | `--template <cli\|tako\|gui>`, `--name <pkg>` | Scaffolds a new `cfire` workspace (`build.cf`, `src/main.cf`, `tests/main_test.cf`, `.gitignore`) in the current directory (`init`) or `<dir>` (`new`). |
| `cfire run [target\|file.cf] [-- args...]` | `--web`, `--ios`, `--device <name>`, `--port <n>`, `--no-open`, `--sanitize`, `--subsystem <gui\|console>` | Compiles and executes a native binary, local WebAssembly server (`--web`), or iOS Simulator app (`--ios`). Arguments after `--` pass to the program. |
| `cfire build [target\|file.cf]` | `-o <path>`, `--target <wasm\|ios-sim\|ios\|triple>`, `--web`, `--ios`, `--subsystem <gui\|console>`, `--name <app>`, `--bundle-id <id>`, `--icon <png>`, `--sanitize` | Compiles an optimized binary, macOS `.app` bundle, WebAssembly `.html`/`.wasm` bundle, or signed iOS `.app` bundle. |
| `cfire check [file.cf\|dir]` | `--json`, `--fix` | Zero-arg workspace-aware lexer, parser, Clang header bridge, and semantic/borrow analyzer without invoking the C++ backend linker. |
| `cfire test [file.cf\|dir]` | `--sanitize`, `--json`, `--filter <pattern>` | Zero-arg workspace test runner that compiles and runs all `test "..." { ... }` blocks in the target file or workspace tree. |
| `cfire eval <code\|-e expr>` | `-e <expr>` | Evaluates a one-shot expression or statement block from CLI arguments or `stdin`. |
| `cfire serve [target\|dir]` | `--port <n>`, `--no-open` | Builds WebAssembly targets (if needed) and serves static/WASM assets with COOP/COEP headers. |
| `cfire fmt [path]` | `--check` | Formats `.cf` files in place according to canonical 4-space alignment rules (or exits non-zero on drift with `--check`). |
| `cfire fix [file.cf\|dir]` | `--dry-run`, `--json` | Zero-arg workspace auto-fixer that inserts missing `core:*` imports, missing call-site `mut` markers, and missing `match` arms. |
| `cfire refactor rename <file:line:col> <new_name>` | `--diff`, `--apply`, `--json` | Performs scope- and package-aware semantic symbol renaming across the workspace. |
| `cfire inspect <symbol\|header.h>` | `--json` | Queries stdlib documentation, symbol signatures, or foreign C/C++ header declarations statelessly. |
| `cfire outline [path]` | `--json` | Prints a token-compressed structural summary of packages, structs, enums, interfaces, functions, and test blocks. |
| `cfire emit-cpp <file.cf>` | None | Prints the generated ISO C++23 source code to `stdout`. |
| `cfire eject <file.cf> [out_dir]` | None | Exports a standalone C++23 source tree and `CMakeLists.txt` into `out_dir` (defaults to `ejected_cpp/`). |
| `cfire vendor [dest_dir]` | None | Fetches and pins all remote `build.Git` and `build.Archive` dependencies from `build.cf` into `vendor/` for offline builds. |
| `cfire install` | `--user`, `--prefix <dir>`, `--portable <dir>` | Installs the self-relocating `cfire` binary and `runtime/` headers to `~/.local`, `<dir>`, or a portable bundle. |
| `cfire doc [file.cf]` | `--check`, `--llm`, `--root <dir>` | Generates `docs/` HTML site (`index.html`, `guide.html`, `reference.html`, `LLMS.txt`) or verifies zero doc drift (`--check`). |
| `cfire lsp` / `cfire mcp` / `cfire ide` | `--all`, `vscode`, `cursor`, `zed`, `neovim`, `clion` | Starts the LSP server (`lsp`), MCP agent server (`mcp`), or installs editor integration files (`ide`). |

#### Environment Variables
- `CFIRE_CXX`: Overrides the backend C++23 compiler executable (e.g., `/opt/homebrew/opt/llvm/bin/clang++`).
- `CFIRE_RUNTIME_DIR`: Overrides the path to the `runtime/` header directory.

### Multi-Package Workspace Layout
A multi-package `cfire` workspace organizes entry executables, internal library packages, and C/C++ dependencies around a root `build.cf` file:

```text
my-service/
├── build.cf                    # Declarative build graph (executables, C/CMake deps, pre-build hooks)
├── cfire.lock                  # Deterministic SHA-256 & commit lockfile for remote dependencies
├── main.cf                     # Entry package (`package main`)
├── pkg/
│   └── checksum/
│       └── checksum.cf         # Internal package (`package checksum`, imported as `import "./pkg/checksum"`)
└── vendor/
    └── fastcrc/                # Local or vendored C/C++/CMake library sources
```

**Package Resolution Rules:**
1. **Same-Directory Package Merging**: All `.cf` files in the same directory declaring the same `package <name>` (e.g., `package main` in `src/main.cf`, `src/state.cf`, and `src/view.cf`) automatically share their structs, enums, functions, methods, and constants without needing `import "./sibling.cf"`. Test-only `*_test.cf` files are automatically excluded from production `cfire build` and `cfire run` invocations.
2. **Standard Library (`import "core:..."` / `import "ext:..."` / `import "vendor:..."`)**: Resolves headers from the `cfire` runtime directory (`ext:net/http`, `ext:db/sqlite`, `ext:ai`, `vendor:raylib`).
3. **Toolchain Packages (`import "pkg:tako"` / `import "core:tako"`)**: Resolves companion toolchain packages (such as the Tako GUI runtime) automatically across workspace parents, `~/.local/share`, and `CFIRE_PKG_PATH`, paired with `var TakoRuntime = build.Tako()` in `build.cf`.
4. **Local Workspace Packages (`import "./pkg/checksum"`)**: Scans all `.cf` files in `./pkg/checksum/` belonging to `package checksum` and merges their declarations under the `checksum.` namespace.

### Declarative Build System (`build.cf`)
In multi-target or hybrid C/C++ projects, `build.cf` declares executables, pre-build code generators, and external C/C++/CMake/Make dependencies using top-level declarations in `package build`:

```cfire
package build

import "core:build"

// 1. Pre-build code generation hook (runs only when Inputs are newer than Outputs)
var GenTables = build.Command("python3", {
    Args:    ["scripts/gen_tables.py"],
    Inputs:  ["scripts/schema.json"],
    Outputs: ["src/generated_tables.cf"],
})

// 2. Native C/C++ sources compiled into a cached static archive (.a)
var CSupport = build.CC("./vendor/fastcrc", {
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

// 4. External GNU Make subproject
var LegacyMath = build.Make("./vendor/libmath_make", {
    Target:   "vecmath",
    Includes: ["include"],
})

// 5. Remote Git repository dependency (pinned in cfire.lock)
var RemoteEngine = build.Git("https://github.com/example/engine.git", {
    Tag:      "v1.2.0",
    Driver:   "CMake",
    Target:   "engine_core",
    Includes: ["include"],
})

// 6. Remote tarball/zip archive dependency with SHA-256 verification
var ArchiveLib = build.Archive("https://example.com/releases/lib codec-1.0.tar.gz", {
    Sha256:   "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
    Sources:  ["src/codec.c"],
    Includes: ["include"],
})

// 7. Multi-target application executable definition
var App = build.Executable("service-app", {
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

#### `build.cf` Specification Tables

#### 1. `build.Executable` & `build.App` Fields
| Field | Type | Default | Description |
| :--- | :--- | :--- | :--- |
| `Entry` | `str` | `"main.cf"` | Path to the entry `.cf` file containing `fn main()`. |
| `Subsystem` | `str` | `"auto"` | `"console"` (standard CLI binary) or `"gui"` (creates a macOS `.app` bundle and suppresses terminal window). Auto-switches to `"gui"` when `vendor:raylib` is imported. |
| `Name` | `str` | `<name>` | Human-readable application display name (`CFBundleDisplayName` on macOS/iOS, `<title>` on WebAssembly). |
| `BundleId` | `str` | `"com.cfire.<name>"` | Reverse-DNS bundle identifier (`CFBundleIdentifier`) used for macOS/iOS code signing and Simulator launching. |
| `Icon` | `str` | `""` | Path to a `.png` icon; automatically generates `.icns` on macOS, `AppIcon60x60@2x.png` on iOS, and loads window icon pixels via `SetWindowIcon` on Linux. |
| `Targets` | `[]str` | `["native"]` | Target list built when invoking `cfire build build.cf` (`"native"`, `"wasm"`, `"ios-sim"`, `"ios"`). |
| `OutDir` | `str` | `"build"` | Output directory root for compiled artifacts (`build/native/`, `build/wasm/`, `build/ios-sim/`). |
| `WebProfile` | `str` | `"auto"` | `"canvas"` (HTML5 WebGL + Audio shell) or `"cli"` (console output runner) when compiling for `--target=wasm`. |

#### 2. External Dependency Builders (`build.CC`, `CMake`, `Git`)
| Builder Function | First Argument | Supported Config Fields | Behavior |
| :--- | :--- | :--- | :--- |
| `build.CC(dir, cfg)` | Local directory path | `Sources`, `Includes`, `Defines` | Compiles listed C/C++ files into a cached static library (`lib<name>.a`) and adds `Includes` to the Clang AST bridge. |
| `build.CMake(dir, cfg)` | CMake project directory | `Target`, `Includes`, `Defines` | Runs `cmake -B <cache> -DCMAKE_BUILD_TYPE=Release` and `cmake --build`, then links the resulting static/shared library. |
| `build.Make(dir, cfg)` | Makefile directory | `Target`, `Includes`, `Defines` | Executes `make -C <dir>` and links the resulting `lib<Target>.a` archive. |
| `build.Git(url, cfg)` | Git repository URL | `Tag` / `Ref` / `Commit`, `Driver` (`"CC"`\|`"CMake"`\|`"Make"`), `Sources`, `Target`, `Includes`, `Policy` | Clones the repository into `.cfire/deps/` (or uses `vendor/` if `Policy: "vendor"`), records the commit hash in `cfire.lock`, and builds via `Driver`. |
| `build.Archive(url, cfg)` | `.tar.gz` / `.zip` URL | `Sha256`, `Driver`, `Sources`, `Target`, `Includes`, `Policy` | Downloads the archive, verifies its SHA-256 digest against `Sha256` and `cfire.lock`, extracts it, and builds via `Driver`. |
| `build.Command(bin, cfg)` | Executable name | `Args`, `Inputs`, `Outputs`, `WorkDir` | Runs pre-build command before `.cf` parsing whenever any file in `Inputs` has a newer mtime than `Outputs`. |

### Precompiled Header & Artifact Cache
`cfire` stores build artifacts in `.cfire/` (or the system temporary cache during `cfire run`):
- **Precompiled Header (`.cfire/cache/pch/`)**: The standard runtime prelude (`cfire_prelude.hpp`, `<vector>`, `<string>`, `<expected>`, `<variant>`, `<format>`, `<chrono>`) is precompiled once per compiler version and target triple (`native`, `wasm`, `ios-sim`), cutting C++23 frontend parse time by ~85%.
- **External Dependency Cache (`.cfire/cache/deps/`)**: Static archives built by `build.CC`, `build.CMake`, and `build.Make` are keyed by a SHA-256 hash of their source files, compiler flags, and target triple, so C/C++ dependencies only recompile when their sources change.

---

# Part II: Core Language & Safety

## Chapter 4: Primitives, Collections & Control Flow

### Architecture & Invariants
`cfire` enforces three foundational language invariants across all bindings, expressions, and control-flow constructs:
1. **Immutability by Default**: Local bindings declared with `:=` or `: T =` are `const` in emitted C++23 unless prefixed with `mut`.
2. **Deterministic Value & RAII Semantics**: All values have deterministic stack or RAII-owned heap lifetimes; there is no garbage collector.
3. **Fixed-Width Scalar Types**: Integer and floating-point types have explicit bit widths and never silently narrow across assignments.

### Primitive Types Specification
| `cfire` Type | Bit Width | Value Range / Representation | Default Zero Value | Emitted ISO C++23 Type |
| :--- | :---: | :--- | :---: | :--- |
| `i8` | 8-bit signed | `-128` to `127` | `0` | `std::int8_t` |
| `i16` | 16-bit signed | `-32,768` to `32,767` | `0` | `std::int16_t` |
| `i32` | 32-bit signed | `-2,147,483,648` to `2,147,483,647` | `0` | `std::int32_t` |
| `i64` | 64-bit signed | `-9.22e18` to `9.22e18` (default integer literal type) | `0` | `std::int64_t` |
| `u8` / `byte` | 8-bit unsigned | `0` to `255` (`byte` aliases `u8` / `std::byte` in I/O slices) | `0` | `std::uint8_t` |
| `u16` | 16-bit unsigned | `0` to `65,535` | `0` | `std::uint16_t` |
| `u32` | 32-bit unsigned | `0` to `4,294,967,295` | `0` | `std::uint32_t` |
| `u64` | 64-bit unsigned | `0` to `1.84e19` | `0` | `std::uint64_t` |
| `usize` | Pointer-sized unsigned | 64-bit on `x86_64`/`arm64`, 32-bit on `wasm32` | `0` | `std::size_t` |
| `f32` | 32-bit IEEE-754 | Single-precision float | `0.0` | `float` |
| `f64` | 64-bit IEEE-754 | Double-precision float (default float literal type) | `0.0` | `double` |
| `bool` | 1-byte logical | `true` or `false` | `false` | `bool` |
| `str` | UTF-8 string | Owned UTF-8 text buffer (`&str` borrows as `std::string_view`) | `""` | `std::string` |
| `void` | 0-bit unit | Return-only unit type for procedures | — | `void` |

### Level 1 — Variables, Constants & Type Casting
```cfire
package main

const MAX_RETRIES: i32 = 5

fn main() {
    // 1. Immutable inferred bindings (lower to 'const')
    count := 42                  // inferred as i64
    ratio := 3.14159             // inferred as f64
    mask := 0xFF_00              // hex literal with digit separator
    raw_sql := `SELECT id, email FROM users WHERE active = 1`

    // 2. Explicit type annotations & mutable bindings
    mut accumulator: i64 = 0
    accumulator += count
    accumulator <<= 1            // compound bitwise shift

    // 3. Explicit numeric conversion and inline string interpolation
    scaled := (accumulator as f64) * ratio
    summary := "retries={MAX_RETRIES} acc={accumulator}"
    println("{} scaled={:.2f} mask=0x{:x} sql_len={}", summary, scaled, mask, raw_sql.len())
}
```
```text
# Output:
retries=5 acc=84 scaled=263.89 mask=0xff00 sql_len=44
```

**Line-by-Line Mechanics:**
1. **Declaration Forms**:
   - `x := expr` declares an immutable variable with inferred type (`const auto x = expr;`).
   - `mut x := expr` declares a mutable variable with inferred type (`auto x = expr;`).
   - `x: T = expr` and `mut x: T = expr` bind an explicit type `T`.
   - `mut x: T` (without `= expr`) zero-initializes `x` (`T x{};`).
   - `const NAME: T = expr` declares a module-scope compile-time constant (`constexpr T NAME = expr;`).
2. **String Literals & Arbitrary Expression Interpolation**: Double-quoted strings (`"..."`) support escape sequences (`\n`, `\t`, `\"`, `\\`) and inline interpolation of any `cfire` expression with optional format specifiers (`"{var_name}"`, `"{a + b}"`, `"{items[0]}"`, `"{ratio:.2f}"`, `"{mask:04x}"`). Backtick strings (`` `...` ``) are raw multi-line literals that do not interpret backslashes or quotes.
3. **Operators & Explicit Casting (`as`)**: `cfire` supports arithmetic (`+`, `-`, `*`, `/`, `%`), bitwise (`&`, `|`, `^`, `~`, `<<`, `>>`), compound assignment (`+=`, `-=`, `*=`, `/=`, `%=`, `<<=`, `>>=`, `&=`, `|=`, `^=`), comparison (`==`, `!=`, `<`, `<=`, `>`, `>=`), and short-circuit logical (`&&`, `||`, `!`) operators. Mixed-width arithmetic requires an explicit `expr as TargetType` cast (`static_cast<TargetType>(expr)`).

### Level 2 — Arrays, Slices & Maps (`[]T`, `map`)
`cfire` provides three built-in collection types in the language prelude (plus `Set[T]`, `PriorityQueue[T]`, and `Deque[T]` in `core:collections`):

| Collection Syntax | Memory Backing & Lowering | Core Operations |
| :--- | :--- | :--- |
| `[N]T` (Fixed Array) | Stack-allocated `std::array<T, N>` | Indexing `arr[i]`, `.len()`, value copy, zero heap allocation. |
| `[]T` (Dynamic Slice) | Contiguous `std::vector<T>` | `.push(val)`, `.push(move val)`, `.len()`, `.capacity()`, `s[i]`, sub-slicing `s[start..end]`. |
| `map[K]V` / `Map[K, V]` | Hash table `cfire::Map<K, V>` | `m[k] = v` / `m.set(k, v)`, `m.get(k) -> V?`, `m.contains(k) -> bool`, `m.remove(k)`, `m.len()`. |

```cfire
package main

fn main() {
    // 1. Dynamic Slice ([]i32)
    mut scores: []i32 = [98, 84]
    scores.push(91)
    top_two := scores[0..2]

    // 2. Fixed-Size Stack Array ([3]f64)
    weights: [3]f64 = [1.0, 2.5, 0.75]

    // 3. Hash Map (map[str]i32)
    mut ports: map[str]i32
    ports["http"] = 8080
    ports["https"] = 8443

    http_port := ports.get("http") or 80
    println("top_two={} weight0={} http_port={} has_tls={}",
        top_two.len(), weights[0], http_port, ports.contains("https"))
}
```
```text
# Output:
top_two=2 weight0=1 http_port=8080 has_tls=true
```

**How It Works:**
- **Sub-slicing (`scores[0..2]`)**: Slices a half-open range `[start, end)` from a slice or string. String slicing (`label[0..4]`) returns a substring over byte offsets `[0, 4)`.
- **Safe Map Lookup (`ports.get(key)`)**: Returns an optional `V?` (`std::optional<V>`) rather than silently default-inserting a missing key like C++ `std::map::operator[]`. Combine `.get(key)` with `or <fallback>` to extract a value with a default in one expression.

### Level 3 — Control Flow (`if`, `for`, Ranges)
`cfire` unifies all iteration constructs under the single `for` keyword and requires braces `{ ... }` around all conditional and loop bodies (parentheses around conditions are optional):

```cfire
package main

fn main() {
    mut sum_exclusive: i64 = 0
    // 1. Half-open range loop: 0, 1, 2, 3, 4
    for i in 0..5 {
        sum_exclusive += i
    }

    mut sum_inclusive: i64 = 0
    // 2. Inclusive range loop: 1, 2, 3, 4, 5
    for i in 1..=5 {
        if i == 3 {
            continue
        }
        sum_inclusive += i
    }

    // 3. Slice iteration (value and indexed forms)
    items: []str = ["alpha", "beta", "gamma"]
    mut joined := ""
    for item in items {
        if !joined.empty() {
            joined = joined + ","
        }
        joined = joined + item
    }

    // 4. Conditional (while-style) loop with break
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

#### Control Flow Specification Table
| Construct | Syntax | Semantics & C++23 Lowering |
| :--- | :--- | :--- |
| Conditional branch | `if cond { ... } else if cond2 { ... } else { ... }` | Evaluates `bool` conditions without parentheses; lowers to C++ `if (...) { ... } else ...`. |
| Exclusive range loop | `for i in start..end { ... }` | Iterates `i` from `start` while `i < end` (`for (auto i = start; i < end; ++i)`). |
| Inclusive range loop | `for i in start..=end { ... }` | Iterates `i` from `start` while `i <= end` (`for (auto i = start; i <= end; ++i)`). |
| Collection loop | `for elem in collection { ... }` | Range-based `for (const auto& elem : collection)` over `[]T`, `[N]T`, `str`, or `Channel[T]`. |
| Conditional loop | `for cond { ... }` | Executes while `cond` is `true` (`while (cond) { ... }`). |
| Unbounded loop | `for { ... }` | Infinite loop exited via `break` or `return` (`while (true) { ... }`). |

### Level 4 — Pointer & Smart Ownership Taxonomy
`cfire` distinguishes non-owning borrows from heap-owning smart pointers at the type level:

| Pointer / Handle Syntax | Creation Expression | Ownership & Nullability | Emitted ISO C++23 Type |
| :--- | :--- | :--- | :--- |
| `&T` | `&x` (or `in` parameter) | Non-null immutable borrow (`&str` lowers to `std::string_view`) | `const T&` / `std::string_view` |
| `&mut T` | `&mut x` | Non-null mutable borrow (used for C/Raygui in-place pointers) | `T&` / `T*` |
| `own[T]` / `^T` | `make(own[T], val)` | Unique RAII heap ownership; cannot be copied, only moved | `std::unique_ptr<T>` |
| `shared[T]` | `make(shared[T], val)` | Reference-counted shared heap ownership (atomic refcount) | `std::shared_ptr<T>` |
| `*T` | Foreign C pointer | Raw pointer for C/C++ FFI boundaries | `T*` |

```cfire
package main

type Packet struct {
    Sequence: i64
    Payload:  str
}

fn main() {
    // 1. Unique heap ownership (std::unique_ptr<Packet>)
    unique_pkt := make(own[Packet], Packet{Sequence: 1, Payload: "SYN"})

    // 2. Shared reference-counted ownership (std::shared_ptr<Packet>)
    shared_pkt := make(shared[Packet], Packet{Sequence: 2, Payload: "ACK"})
    shared_clone := shared_pkt

    println("seq1={} seq2={} payload2={}",
        (*unique_pkt).Sequence, (*shared_clone).Sequence, (*shared_clone).Payload)
}
```
```text
# Output:
seq1=1 seq2=2 payload2=ACK
```

---

## Chapter 5: Functions & Call-Site Intent Safety

### Call-Site Exclusivity Invariants
In C and C++, a call expression such as `process(cfg, buf)` hides whether `cfg` or `buf` is read-only, mutated in place, or moved from. `cfire` enforces **Call-Site Intent Visibility** and the **Law of Exclusivity**:
1. Every function parameter has an explicit or default passing intent (`in`, `mut`, `out`, or `move`).
2. Whenever a parameter is declared `mut`, `out`, or `move`, the caller **must** write `mut`, `out`, or `move` before the argument at the call site.
3. A single call expression cannot pass the same variable as `mut`/`out` more than once, or alias a `mut`/`out` argument with another `in` argument (`cfire/MUT-ALIAS`).

### Parameter Intent Specification Table
| Intent Modifier | Default? | Callee Access | Mandatory Call-Site Marker | Emitted ISO C++23 Parameter & Call Lowering |
| :--- | :---: | :--- | :--- | :--- |
| `in` (or omitted) | **Yes** | Read-only | None (`f(x)`) | Scalars by value (`const T`); structs/slices/maps by `const T&`; `&str` by `std::string_view` |
| `mut` | No | Read-write in place | `mut` (`f(mut x)`) | `T&` parameter; caller variable must be declared `mut` |
| `out` | No | Write-to output | `out` (`f(out x)`) | `T&` parameter; initializes or overwrites `x` in place |
| `move` | No | Takes ownership | `move` (`f(move x)`) | `T` / `T&&` parameter with `std::move(x)` at call site; invalidates `x` in caller scope |

### Level 1 — Parameter Intents & Tuples
```cfire
package main

type Buffer struct {
    Entries: []str
}

// 1. 'in' intent (default): read-only borrow (const Buffer&)
fn entry_count(in buf: Buffer) -> usize {
    return buf.Entries.len
}

// 2. 'mut' and 'move' intents: mutates 'buf' in place and consumes 'msg'
fn append_entry(prefix: &str, mut buf: Buffer, move msg: str) -> usize {
    formatted := "[{prefix}] {msg}"
    buf.Entries.push(move formatted)
    return buf.Entries.len
}

// 3. 'out' intent and multiple return values (tuple)
fn summarize(in buf: Buffer, out first_item: str) -> (usize, bool) {
    if buf.Entries.len > 0 {
        first_item = buf.Entries[0]
        return (buf.Entries.len, true)
    }
    first_item = "<empty>"
    return (0, false)
}

fn main() {
    mut log := Buffer{}
    payload := "handshake verified"

    // Call site explicitly documents that 'log' is mutated and 'payload' is moved:
    _ := append_entry("NET", mut log, move payload)

    mut head := ""
    (total, ok) := summarize(log, out head)
    println("count={} total={} ok={} head={}", entry_count(log), total, ok, head)
}
```
```text
# Output:
count=1 total=1 ok=true head=[NET] handshake verified
```

#### Under the Hood: Emitted ISO C++23
```cpp
auto entry_count(const Buffer& buf) -> std::size_t {
    return buf.Entries.size();
}

auto append_entry(std::string_view prefix, Buffer& buf, std::string msg) -> std::size_t {
    const std::string formatted = std::format("[{}] {}", prefix, msg);
    buf.Entries.push_back(std::move(formatted));
    return buf.Entries.size();
}

auto summarize(const Buffer& buf, std::string& first_item) -> std::tuple<std::size_t, bool> {
    if (buf.Entries.size() > 0) {
        first_item = buf.Entries[0];
        return std::make_tuple(buf.Entries.size(), true);
    }
    first_item = std::string("<empty>");
    return std::make_tuple(0, false);
}
```

### Level 2 — Closures & Trailing Closure Syntax
`cfire` supports anonymous closures with pipe-delimited parameter lists (`|p1, p2| { ... }`). When a function's final parameter is a callable, callers can pass the closure outside the closing parenthesis using **Trailing Closure Syntax**:

```cfire
package main

fn apply_twice(seed: i32, op: fn(i32) -> i32) -> i32 {
    return op(op(seed))
}

fn for_each_Filtered(items: []i32, min_val: i32, visit: fn(i32)) {
    for x in items {
        if x >= min_val {
            visit(x)
        }
    }
}

fn main() {
    factor := 3
    // 1. Inline closure capturing 'factor' from lexical scope
    tripled_twice := apply_twice(2, |v| {
        return v * factor
    })

    // 2. Trailing closure syntax after ')'
    mut acc := 0
    scores: []i32 = [10, 50, 25, 80]
    for_each_Filtered(scores, 30) |score| {
        acc += score
    }

    println("tripled_twice={} filtered_sum={}", tripled_twice, acc)
}
```
```text
# Output:
tripled_twice=18 filtered_sum=130
```

### Level 3 — Generics & Monomorphization
Functions parameterize over types using square brackets `fn name[T, U](...)`. Type parameters lower directly to C++23 `template <typename T, typename U>` functions, generating specialized machine code with zero boxing or virtual dispatch:

```cfire
package main

fn clamp_generic[T](val: T, lo: T, hi: T) -> T {
    if val < lo {
        return lo
    }
    if val > hi {
        return hi
    }
    return val
}

fn main() {
    i_clamped := clamp_generic(150, 0, 100)
    f_clamped := clamp_generic(1.85, 0.0, 1.0)
    println("i={} f={:.2f}", i_clamped, f_clamped)
}
```
```text
# Output:
i=100 f=1.00
```

### Level 4 — Intent, Move & Pattern Diagnostics
The semantic analyzer (`sema`) enforces 8 static intent, move, and pattern-matching rules at compile time before C++23 codegen runs (note: `cfire` lowers to standard C++23 references and smart pointers rather than a full Rust-style lifetime borrow checker):

| Diagnostic Code | Violation Condition | Rejected Code Example | Remediation |
| :--- | :--- | :--- | :--- |
| `cfire/MUT-CALLSITE` | Calling a function with a `mut` parameter without writing `mut` at the call site. | `apply_defaults(c)` | Write `apply_defaults(mut c)` at the call site. |
| `cfire/IMMUTABLE-MUT-ARG` | Passing an immutable binding (`c := ...`) to a `mut` or `out` parameter. | `c := Config{}; apply_defaults(mut c)` | Declare the binding as `mut c := Config{}`. |
| `cfire/MUT-RECEIVER` | Calling a `fn (mut s: T)` method on an immutable struct instance. | `s := Scene{}; s.register(...)` | Declare `mut s := Scene{}`. |
| `cfire/MUT-ALIAS` | Passing the same variable more than once when at least one argument is `mut`/`out`. | `transfer(mut a, mut a)` | Pass distinct variables to each parameter. |
| `cfire/USE-AFTER-MOVE` | Reading or passing a variable after `move x` has transferred ownership on an earlier path. | `consume(move buf); println("{}", buf.len)` | Read `buf.len` before `consume(move buf)` or pass by `in`/`mut` borrow instead. |
| `cfire/NON-EXHAUSTIVE-MATCH` | A `match` over an `enum` omits one or more variants without a `_ =>` wildcard arm. | `match tok { Ident(s) => {} }` | Handle all variants or add `_ => { ... }`. |
| `cfire/NON-DIVERGING-OR-BLOCK` | An `x := fallible() or err { ... }` block does not exit via `return`/`break`/`panic`. | `x := f() or err { println("{}", err) }` | Add `return` or `panic(...)` inside the recovery block. |
| `cfire/UNREACHABLE-MATCH-ARM` | A `match` arm is shadowed by an earlier wildcard, duplicate variant/literal, or broader slice pattern. | `[h, ..t] => {}, [a, ..m, b] => {}` | Order more specific slice patterns (`[a]`, `[a, ..m, b]`) before general prefix patterns, or remove dead arms. |
| `CF3002` | Returning a borrow (`&T`) pointing to a stack-allocated local variable. | `fn bad() -> &i32 { x := 1; return &x }` | Return `i32` by value or allocate with `make(own[T], ...)`. |

---

## Chapter 6: Structs, Tags, Enums & Interfaces

### Architecture & Invariants
`cfire`'s type system maps directly to zero-overhead C++23 layout primitives:
- **`type Name struct`**: Standard-layout C++23 `struct Name` with default member initializers, designated field construction, and value/mutable receiver methods.
- **Backtick Field Tags (`` `json:"..." sql:"..."` ``)**: Parsed into compile-time `constexpr` reflection traits (`cfire::meta::fields<T>`) with zero runtime RTTI or heap metadata.
- **`type Name enum`**: Tagged union lowered to `std::variant<...>`, verified for exhaustiveness in `match` statements and expressions (`cfire/NON-EXHAUSTIVE-MATCH`).
- **`type Name interface`**: Structural type constraint lowered to a C++20 `concept` without `vtable` pointer indirection.

### Level 1 — Structs, Defaults & Receiver Methods
```cfire
package main

type Endpoint struct {
    Host:       str  = "127.0.0.1"
    Port:       u16  = 8080
    TlsEnabled: bool = false
    Requests:   i64  = 0
}

// 1. Immutable receiver method: borrows 'self' as 'const Endpoint&'
fn (e: Endpoint) scheme() -> str {
    if e.TlsEnabled {
        return "https"
    }
    return "http"
}

// 2. Mutable receiver method: borrows 'self' as 'Endpoint&'
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

**How It Works:**
1. Fields with `= <expr>` default values can be omitted when constructing a struct literal (`Endpoint{}`). Any omitted field without an explicit default is zero-initialized (`0`, `false`, `""`, or empty slice/map).
2. Methods declare their receiver before the function name: `fn (e: Endpoint) ...` cannot mutate fields of `e`, whereas `fn (mut e: Endpoint) ...` mutates `e` in place and can only be invoked on a `mut` variable (`cfire/MUT-RECEIVER`).

### Level 2 — Compile-Time Struct Reflection Tags
Backtick string literals attached to struct fields configure serialization, database mapping, CLI flag parsing, and AI JSON Schema synthesis at compile time:

| Tag Key | Consuming Modules | Syntax Example | Compile-Time Behavior |
| :--- | :--- | :--- | :--- |
| `json` | `ext:json`, `ext:ai` | `` `json:"user_id"` `` | Maps struct field to JSON object key `"user_id"` in `json.parse[T]`, `json.stringify`, and `json.schema[T]()`. |
| `sql` | `ext:db/sqlite` | `` `sql:"user_id"` `` | Maps SQLite result column `"user_id"` into the struct field during `db.query[T](sql, ...)`. |
| `cli` | `core:cli` | `` `cli:"--port,-p"` `` | Binds long (`--port`) and short (`-p`) command-line flags to the struct field in `cli.parse[T]()`. |
| `env` | `core:cli` | `` `env:"SERVICE_PORT"` `` | Reads the fallback value from environment variable `SERVICE_PORT` when the CLI flag is omitted. |
| `default` | `core:cli` | `` `default:"8080"` `` | Supplies the default string/numeric/boolean value when neither CLI flag nor `env` variable is set. |
| `desc` | `core:cli`, `ext:ai` | `` `desc:"Listen port"` `` | Populates `--help` usage text in `core:cli` and the `"description"` property in `json.schema[T]()` for LLM tool/structured outputs. |

```cfire
package main

import "ext:json"

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

### Level 3 — Algebraic Enums & Exhaustive `match`
`cfire` enums are full algebraic sum types: each variant can be a unit tag or carry one or more typed payload fields. `match` can be used both as a statement and as a value-producing expression, and also supports structural slice patterns (`[]`, `[head, ..tail]`, `[first, ..mid, last]`):

```cfire
package main

type Event enum {
    Connected(str),
    Data([]byte),
    Resize(i32, i32),
    Disconnected(i32),
}

fn describe_event(e: Event) -> str {
    // Expression-oriented match (compiler verifies all 4 variants are handled)
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

**Exhaustiveness & Reachability Rules (`cfire/NON-EXHAUSTIVE-MATCH` & `cfire/UNREACHABLE-MATCH-ARM`):**
1. Matching over an `enum` must either list every variant of the enum or provide a fallback `_ => ...` wildcard arm. Adding a new variant to `Event` immediately flags every unhandled `match` site at compile time.
2. Matching over primitives (`i64`, `str`) must always include a wildcard `_ => ...` arm when used as a value expression.
3. Every `match` arm must be reachable (`cfire/UNREACHABLE-MATCH-ARM`): duplicate enum/literal arms, arms appearing after `_ => ...`, or slice patterns already subsumed by an earlier rest pattern (such as `[first, ..mid, last]` placed after `[head, ..tail]`) are rejected at compile time. Unused slice pattern bindings are elided from the emitted C++23 code.

### Level 4 — Interfaces & C++20 Concepts (`interface`)
An `interface` defines a structural contract of method signatures. Any `struct` that implements matching methods satisfies the interface automatically without an explicit `implements` keyword, and generic functions constrained by `[T: InterfaceName]` lower directly to C++20 `concept` constraints:

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
    return (c.Items as f64) * c.UnitMass
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

#### Under the Hood: Emitted C++20 `concept`
```cpp
template <typename T>
concept Measurable = requires(const T& self) {
    { self.weight_kg() } -> std::convertible_to<double>;
};

template <Measurable T>
auto shipping_cost(const T& parcel, double rate_per_kg) -> double {
    return parcel.weight_kg() * rate_per_kg;
}
```

---

## Chapter 7: Error Handling, Optionals & Defer

### Architecture & Invariants (`T!` vs `T?`)
`cfire` eliminates C++ exceptions (`try`/`catch` stack unwinding) and null pointer dereferences by separating **recoverable errors** from **optional absence** in the type system:

| Type Syntax | Meaning | Emitted ISO C++23 Type | Construction | Unwrapping & Propagation |
| :--- | :--- | :--- | :--- | :--- |
| `T!` | Fallible result (either a valid `T` or a `cfire::Error` message/code) | `std::expected<T, cfire::Error>` | Return `val` or `return error("msg")` | `try expr`, `expr or default_val`, `expr or err { ... return }` |
| `void!` | Fallible procedure (succeeds or returns `cfire::Error`) | `std::expected<void, cfire::Error>` | Implicit success at end of fn or `return error("msg")` | `try expr`, `expr or err { ... }` |
| `T?` | Optional value (either a present `T` or `nil`) | `std::optional<T>` | Assign `val` or `nil` | `expr or default_val`, or check `if x != nil { use(x?) }` |

### Level 1 — Fallible Returns (`T!`) & `try`
Any function returning `T!` or `void!` can construct an error with `error("description")` or propagate errors from inner fallible calls using `try`:

```cfire
package main

import "core:str"

fn parse_port(raw: str) -> u16! {
    cleaned := str.trim(raw)
    if cleaned.empty() {
        return error("port string is empty")
    }
    if cleaned == "8080" {
        return 8080 as u16
    }
    if cleaned == "443" {
        return 443 as u16
    }
    return error("unsupported port: " + cleaned)
}

fn build_endpoint(host: str, raw_port: str) -> str! {
    // 'try' unwraps u16 on success or immediately returns the Error to caller
    port := try parse_port(raw_port)
    return "{host}:{port}"
}

fn main() {
    ep := build_endpoint("127.0.0.1", " 8080 ") or "0.0.0.0:80"
    fallback := build_endpoint("127.0.0.1", "invalid") or "0.0.0.0:80"
    println("valid={} fallback={}", ep, fallback)
}
```
```text
# Output:
valid=127.0.0.1:8080 fallback=0.0.0.0:80
```

**How `try` Works:**
- Evaluating `port := try parse_port(raw_port)` checks whether the returned `std::expected<uint16_t, cfire::Error>` holds a value.
- If it holds an error, the enclosing function immediately returns `std::unexpected(res.error())`, running any active `defer` statements in LIFO order before exiting.
- If `try` is used inside `fn main()`, an unhandled error prints the diagnostic message to `stderr` and exits with code `1`.

### Level 2 — Error Recovery (`or`) & Optionals (`T?`)
The `or` operator handles both `T!` errors and `T?` (`nil`) optionals at the call site without verbose `if (!res.has_value())` boilerplate:

```cfire
package main

fn lookup_limit(tier: str) -> i64? {
    if tier == "pro" {
        return 10000
    }
    if tier == "free" {
        return 100
    }
    return nil
}

fn require_positive(v: i64) -> i64! {
    if v <= 0 {
        return error("value must be > 0")
    }
    return v * 2
}

fn compute_quota(tier: str, bonus: i64) -> i64 {
    // Form 1: Inline default value on optional (T?)
    base := lookup_limit(tier) or 50

    // Form 2: Diverging recovery block on fallible (T!) with bound 'err'
    extra := require_positive(bonus) or err {
        println("warn: recovering from invalid bonus ({})", err.message())
        return base
    }

    return base + extra
}

fn main() {
    q1 := compute_quota("pro", 25)
    q2 := compute_quota("unknown", -5)
    println("q1={} q2={}", q1, q2)
}
```
```text
# Output:
warn: recovering from invalid bonus (value must be > 0)
q1=10050 q2=50
```

> [!IMPORTANT]
> **Divergence Rule (`cfire/NON-DIVERGING-OR-BLOCK`)**: When binding a variable with `x := fallible() or err { ... }`, the `{ ... }` block **must** exit the current control path via `return`, `break`, `continue`, `panic(...)`, or `exit(...)`. This guarantees `x` can never be read in an uninitialized state after the block.

### Level 3 — RAII Scope Cleanup (`defer`)
`defer <stmt>` and `defer { ... }` schedule cleanup statements to run automatically when the enclosing lexical block exits—whether by reaching the closing `}`, executing an early `return`, or propagating an error via `try` (`return` inside a `defer { ... }` block is rejected at compile time via `cfire/RETURN-IN-DEFER`):

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
        log_file.close() // Multi-statement block defer -> Executes 1st (LIFO)
    }

    try db.exec("CREATE TABLE IF NOT EXISTS events (id INT, name TEXT)")
    try db.exec("INSERT INTO events VALUES (1, 'boot')")
    try log_file.write_str("snapshot_ok\n")
}

fn main() {
    export_snapshot(":memory:", "/tmp/cfire_guide_defer.log") or err {
        println("export failed: {}", err.message())
        return
    }
    text := try fs.read_text("/tmp/cfire_guide_defer.log")
    _ := fs.remove("/tmp/cfire_guide_defer.log")
    print("{}", text)
}
```
```text
# Output:
snapshot_ok
```

#### Under the Hood: C++23 Scope-Guard Lowering for `defer`
Each `defer` statement lowers to an stack-allocated `cfire::ScopeGuard` lambda object whose destructor runs deterministically during C++ stack unwinding at block exit:

```cpp
auto export_snapshot(std::string db_path, std::string log_path) -> std::expected<void, cfire::Error> {
    auto _try_0 = cfire::sqlite::open(db_path);
    if (!_try_0) return std::unexpected(_try_0.error());
    auto db = std::move(*_try_0);
    auto _defer_1 = cfire::make_scope_guard([&]() { db.close(); });

    auto _try_2 = cfire::fs::open(log_path, cfire::fs::OpenMode::Write);
    if (!_try_2) return std::unexpected(_try_2.error());
    auto log_file = std::move(*_try_2);
    auto _defer_3 = cfire::make_scope_guard([&]() { log_file.close(); });
    // When export_snapshot returns (or if any subsequent 'try' fails),
    // _defer_3::~ScopeGuard() runs first, followed by _defer_1::~ScopeGuard().
}
```

---

# Part III: Core Stdlib & Concurrency

## Chapter 8: Strings, Unicode, Regex & Containers

### Architecture & Invariants
`cfire` strings (`str`) are UTF-8 encoded buffers backed by `std::string`, and borrowed string views (`&str`) lower to `std::string_view`. Text manipulation, RFC 3629 Unicode scalar decoding, ECMAScript regular expressions, and specialized generic data structures (`Set[T]`, `PriorityQueue[T]`, `Deque[T]`, and in-place sorting) are provided by five Tier 1A foundational modules: `core:str`, `core:unicode`, `core:regex`, `core:collections`, and `core:sort`.

### Text, Unicode & Regex Specification
| Module | Function / Method | Signature | Description |
| :--- | :--- | :--- | :--- |
| `core:str` | `str.split` / `str.join` | `fn(s: str, sep: str) -> []str` / `fn(items: []str, sep: str) -> str` | Splits `s` on delimiter `sep` or joins a slice of strings with `sep`. |
| `core:str` | `str.trim` / `trim_left` / `trim_right` | `fn(s: str) -> str` | Strips leading and/or trailing ASCII whitespace (`' '`, `'\t'`, `'\n'`, `'\r'`). |
| `core:str` | `str.replace` | `fn(s: str, from: str, to: str) -> str` | Replaces all occurrences of `from` in `s` with `to`. |
| `core:str` | `str.starts_with` / `ends_with` / `contains` | `fn(s: str, pat: str) -> bool` | Prefix, suffix, and substring containment checks. |
| `core:str` | `str.index_of` / `last_index_of` | `fn(s: str, sub: str) -> i64` | Returns the 0-based byte offset of `sub` in `s`, or `-1` if absent. |
| `core:str` | `str.to_upper` / `to_lower` / `repeat` | `fn(s: str) -> str` / `fn(s: str, count: usize) -> str` | ASCII case conversion and `count`-fold string repetition. |
| `core:unicode` | `unicode.is_valid_utf8` / `rune_count` | `fn(s: str) -> bool` / `fn(s: str) -> usize` | Validates RFC 3629 UTF-8 byte sequences and counts Unicode codepoints (scalar values). |
| `core:unicode` | `unicode.runes` / `encode_rune` | `fn(s: str) -> []u32` / `fn(r: u32) -> str` | Decodes a UTF-8 string into 32-bit Unicode scalar values (`u32`) or encodes a codepoint to UTF-8. |
| `core:unicode` | `unicode.to_lower` / `to_upper` | `fn(s: str) -> str` | Full Unicode case folding across Latin, Greek, and Cyrillic scripts. |
| `core:unicode` | `unicode.is_letter` / `is_digit` / `is_space` | `fn(r: u32) -> bool` | Unicode general category classification predicates on a 32-bit rune. |
| `core:regex` | `regex.compile` | `fn(pattern: str) -> Regex!` | Compiles an ECMAScript regular expression (`std::regex`), returning an error on invalid syntax. |
| `core:regex` | `regex.is_match` / `find` / `find_all` | `fn(pat: str, text: str) -> bool!` / `Match?!` / `[]str!` | Tests pattern matches or extracts the first (`Match { text, index }`) or all non-overlapping matches. |
| `core:regex` | `regex.replace_all` | `fn(pat: str, text: str, replacement: str) -> str!` | Replaces every match of `pat` in `text` with `replacement`. |

### Level 1 — Strings, Runes & Regex (`core:str`)
```cfire
package main

import "core:regex"
import "core:str"
import "core:unicode"

fn main() -> void! {
    raw := "  node_01, node_02, node_03  "
    parts := str.split(str.trim(raw), ", ")
    joined := str.join(parts, "|")

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

**Line-by-Line Mechanics:**
1. **`str.split(str.trim(raw), ", ")` (Line 9)**: Strips outer whitespace and splits `raw` into a 3-element `[]str` slice (`["node_01", "node_02", "node_03"]`).
2. **`unicode.runes(banner)` & `unicode.rune_count(banner)` (Lines 13 & 17)**: While `banner.len()` returns the UTF-8 byte length (`10`, because `🔥` `U+1F525` is 4 UTF-8 bytes), `unicode.rune_count(banner)` returns `7` scalar codepoints.
3. **`try regex.replace_all(...)` (Line 15)**: Compiles the raw string pattern `` `[0-9]+` `` and replaces digit runs with `"#"`.

### Level 2 — Generic Containers & Sorting
Beyond built-in slices (`[]T`) and maps (`map[K]V`), `core:collections` and `core:sort` provide generic hash sets, max-heap priority queues, double-ended queues, in-place sorting, and logarithmic binary search:

| Module | Type / Function | Operations / Signature | C++23 Backing |
| :--- | :--- | :--- | :--- |
| `core:collections` | `collections.Set[T]` | `.insert(v)`, `.contains(v) -> bool`, `.remove(v)`, `.len() -> usize`, `.clear()` | `std::unordered_set<T>` |
| `core:collections` | `collections.PriorityQueue[T]` | `.push(v)`, `.top() -> T`, `.pop() -> T`, `.empty() -> bool`, `.len() -> usize` | `std::priority_queue<T>` (max-heap) |
| `core:collections` | `collections.Deque[T]` | `.push_front(v)`, `.push_back(v)`, `.pop_front() -> T`, `.pop_back() -> T`, `.len() -> usize` | `std::deque<T>` |
| `core:sort` | `sort.slice(mut s)` / `sort.reverse(mut s)` | `fn[T](mut s: []T)` — sorts `s` ascending or reverses `s` in place | `std::sort` / `std::reverse` |
| `core:sort` | `sort.slice_by(mut s, less)` | `fn[T](mut s: []T, less: fn(T, T) -> bool)` — custom comparator sort | `std::sort(s.begin(), s.end(), less)` |
| `core:sort` | `sort.is_sorted(s)` / `binary_search(s, val)` | `fn[T](s: []T) -> bool` / `fn[T](s: []T, val: T) -> i64?` | `std::is_sorted` / `std::lower_bound` |

```cfire
package main

import "core:collections"
import "core:sort"

fn main() {
    mut latencies: []i64 = [42, 7, 19, 88, 12]
    sort.slice(mut latencies)
    idx := sort.binary_search(latencies, 19) or -1

    mut pq := collections.PriorityQueue[i64]{}
    for v in latencies { pq.push(v) }

    mut dq := collections.Deque[str]{}
    dq.push_back("worker-1")
    dq.push_front("leader-0")

    mut seen := collections.Set[str]{}
    seen.insert("us-east")
    seen.insert("eu-west")

    println("min={} idx_of_19={} max_pq={} front={} regions={}",
        latencies[0], idx, pq.top(), dq.pop_front(), seen.len())
}
```
```text
# Output:
min=7 idx_of_19=2 max_pq=88 front=leader-0 regions=2
```

---

## Chapter 9: Filesystem, OS, Time, CLI & Logging

### Architecture & Invariants
`cfire`'s Tier 1B system modules provide cross-platform filesystem streaming (`core:fs`), zero-shell-injection subprocess execution (`core:os`), monotonic and RFC 3339 clocks (`core:time`), IEEE-754 math (`core:math`), OS-entropy CSPRNG (`core:rand`), compile-time struct-tag CLI flag parsing (`core:cli`), and structured JSON/Text logging (`core:log`).

### Filesystem, OS, Time & Random Specification
| Module | Function / Method | Signature | Description |
| :--- | :--- | :--- | :--- |
| `core:fs` | `fs.read_text` / `write_text` / `append_text` | `fn(path: str) -> str!` / `fn(path: str, data: str) -> void!` | One-shot file read/write (automatically resolves iOS `Documents/` sandbox & bundle resources). |
| `core:fs` | `fs.open(path, mode)` | `fn(path: str, mode: OpenMode) -> File!` | Opens a streaming `File` handle (`OpenMode::Read`, `Write`, `Append`, `ReadWrite`) supporting `.read_line()`, `.write_str()`, `.seek()`, `.tell()`, `.flush()`, `.close()`. |
| `core:fs` | `fs.exists` / `is_dir` / `mkdir_all` / `remove` / `list_dir` | `fn(path: str) -> ...` | Path inspection, recursive directory creation, removal, and directory listing. |
| `core:os` | `os.getenv` / `os.setenv` / `os.cwd` | `fn(key: str, fallback: str = "") -> str` | Reads or sets process environment variables and queries the working directory. |
| `core:os` | `os.command(binary, args)` | `fn(bin: str, args: []str) -> ExecResult!` | Spawns `bin` directly via `posix_spawnp` (zero shell injection) and returns `ExecResult { exit_code, stdout, stderr }`. |
| `core:time` | `time.now_ms` / `now` / `parse_rfc3339` / `sleep_ms` | `fn() -> i64` / `Time` / `Time!` | Monotonic millisecond clock, wall-clock `Time` with `.format_rfc3339()`, and thread sleep. |
| `core:rand` | `rand.u64` / `range_i64` / `f64_unit` / `bytes` | `fn(n: usize) -> []byte` | Thread-local `MT19937_64` draws and OS-entropy CSPRNG byte slices (`rand.bytes`). |
| `core:math` | `math.sqrt` / `sin` / `cos` / `pow` / `clamp_f64` | `fn(f64) -> f64` | IEEE-754 transcendental functions and numeric clamping. |

### Level 1 — File I/O, Clocks & CSPRNG (`core:fs`)
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

    // 1. Streaming file write and read-back
    mut f := try fs.open(tmp, fs.OpenMode::Write)
    try f.write_str("host=" + os.getenv("USER", "ci") + "\n")
    try f.close()

    content := try fs.read_text(tmp)
    token := rand.bytes(16)
    elapsed := time.now_ms() - t0
    println("has_host={} token_bytes={} hypot={:.1f} elapsed>={}",
        content.contains("host="), token.len(), math.sqrt(9.0 + 16.0), elapsed >= 0)
}
```
```text
# Output:
has_host=true token_bytes=16 hypot=5.0 elapsed>=true
```

### Level 2 — CLI Parsing & Structured Logging
`core:cli` synthesizes command-line parsers and `--help` screens from struct field tags at compile time, while `core:log` emits leveled structured logs in either human-readable text or machine-parsable JSON:

| Module | Function / Method | Signature | Description |
| :--- | :--- | :--- | :--- |
| `core:cli` | `cli.parse[T](args)` | `fn[T](args: []str? = nil) -> T!` | Parses `argc`/`argv` (or an explicit `[]str` slice) into struct `T` using `` `cli:"--flag,-f" env:"VAR" default:"..." desc:"..."` `` tags. |
| `core:cli` | `cli.help[T](prog_name)` | `fn[T](prog_name: str = "") -> str` | Generates a formatted `--help` usage screen from struct `T`'s compile-time reflection tags. |
| `core:log` | `log.set_level(lvl)` / `set_format(fmt)` | `fn(Level)` / `fn(Format)` | Configures minimum severity (`Level::Debug`, `Info`, `Warn`, `Error`) and output format (`Format::Text` or `Format::Json`). |
| `core:log` | `log.debug` / `info` / `warn` / `error` | `fn(msg: str, ...kv_pairs)` | Emits a timestamped log record with variadic `"key", "value"` structured attributes. |

```cfire
package main

import "core:cli"
import "core:log"

type ServerFlags struct {
    Host:    str  `cli:"--host,-h" default:"127.0.0.1" desc:"Bind address"`
    Port:    u16  `cli:"--port,-p" default:"9090"      desc:"Listen port"`
    Workers: i32  `cli:"--workers" default:"4"         desc:"Worker thread count"`
}

fn main() -> void! {
    flags := try cli.parse[ServerFlags](["--port", "8088", "--workers", "8"])
    log.set_format(log.Format::Json)
    log.info("server configured", "host", flags.Host, "port", f"{flags.Port}", "workers", f"{flags.Workers}")
}
```
```text
# Output:
{"time":"2026-11-01T12:00:00Z","level":"INFO","msg":"server configured","host":"127.0.0.1","port":"8088","workers":"8"}
```

---

## Chapter 10: Structured Concurrency & Channels

### Lexical Task Lifetime Invariants
Unstructured concurrency (`std::thread::detach` or fire-and-forget goroutines) permits background threads to outlive the stack frame whose local variables they reference, causing use-after-free races. `cfire` enforces **Lexical Structured Concurrency**:
1. **`async.scope(workers: N) |scope| { ... }`**: Allocates a bounded worker pool of `N` hardware threads (`std::jthread`) scoped to the lexical block.
2. **Mandatory Join Barrier**: The closing `}` of `async.scope` blocks until every task submitted via `scope.spawn || { ... }` finishes executing.
3. **Safe Stack Borrowing**: Because no spawned task can outlive `async.scope`, tasks can safely read outer immutable bindings (`in`) or write to disjoint pre-allocated slice slots (`&mut results[i]`) without heap-allocating `std::shared_ptr` wrappers.

### Level 1 — Parallel Scopes (`async.scope`)
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
            shard_idx := i as i32
            scope.spawn || {
                *slot = compute_shard_checksum(shard_idx, scale)
            }
        }
    } // Implicit barrier: all 4 worker threads join before returning 'results'

    return results
}

fn main() {
    out := run_parallel_shards(4, 10)
    println("shards=[{}, {}, {}, {}]", out[0], out[1], out[2], out[3])
}
```
```text
# Output:
shards=[10, 20, 30, 40]
```

**Line-by-Line Mechanics:**
1. **`mut results := make([]i32, shard_count)` (Line 8)**: Allocates a fixed-length vector of `shard_count` zero-initialized `i32` elements prior to spawning tasks, preventing vector reallocation races during parallel execution.
2. **`slot := &mut results[i]` (Line 12)**: Captures an exclusive mutable pointer/reference to element `i` for each task, while `scale` is captured as an immutable value.
3. **Scope Exit Barrier (Line 17)**: Execution cannot reach `return results` until all spawned tasks complete.

### Level 2 — Thread-Safe MPMC Channels (`core:sync`)
`core:sync` provides typed, reference-counted multi-producer / multi-consumer (`MPMC`) channels backed by `std::mutex`, `std::condition_variable`, and `std::deque<T>`:

| Function / Method | Signature | Behavior |
| :--- | :--- | :--- |
| `sync.make_channel[T]` | `fn make_channel[T]() -> Channel[T]` | Creates a thread-safe channel handle (copying the handle shares the underlying queue). |
| `ch.send(val)` | `fn (ch: Channel[T]) send(val: T)` | Enqueues `val` and wakes one blocked receiver thread. |
| `ch.recv()` | `fn (ch: Channel[T]) recv() -> T!` | Blocks until an item is available, or returns an error when closed and drained. |
| `ch.recv_ctx(ctx)` | `fn (ch: Channel[T]) recv_ctx(ctx: Context) -> T!` | Blocks until an item arrives, the channel closes, or `ctx` is cancelled/times out. |
| `ch.close()` | `fn (ch: Channel[T]) close()` | Marks the channel closed and wakes all blocked `recv` and `for item in ch` consumers. |

```cfire
package main

import "core:sync"

fn main() {
    ch := sync.make_channel[i64]()
    mut total: i64 = 0

    async.scope(workers: 2) |scope| {
        // Producer task
        scope.spawn || {
            for i in 1..=4 {
                ch.send(i * 10)
            }
            ch.close()
        }

        // Consumer task iterating until ch.close() drains the queue
        scope.spawn || {
            for val in ch {
                total += val
            }
        }
    }

    println("channel_sum={}", total)
}
```
```text
# Output:
channel_sum=100
```

### Level 3 — Cancellation & Deadlines (`context`)
`core:sync/context` implements hierarchical cancellation tokens and automatic deadlines modeled after Go's `context.Context`:

| Function / Method | Signature | Behavior |
| :--- | :--- | :--- |
| `context.background()` | `fn background() -> Context` | Returns a root, non-cancellable `Context`. |
| `context.with_cancel(parent)` | `fn with_cancel(parent: Context) -> (Context, fn(reason: str = ""))` | Creates a child `Context` and a `cancel(reason)` closure. Cancelling a parent cancels all derived children. |
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

    ch := sync.make_channel[str]()

    async.scope(workers: 2) |scope| {
        scope.spawn || {
            // Waits on an empty channel until the 50ms context deadline fires
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
cancelled=true result= aborted:timeout
```

---

## Chapter 11: C/C++ Interop & Native Dependencies

### Direct AST Ingestion vs Manual FFI
Because `cfire` lowers to ISO C++23, calling C or C++ libraries requires **zero dynamic FFI marshaling, zero wrapper generators (SWIG/bindgen), and zero ABI thunks**:
1. **Inline `cpp { ... }` Escape Blocks**: Embed raw C++23 statements, preprocessor conditionals (`#if`), or SIMD intrinsics directly inside `.cf` functions with full access to local `.cf` bindings.
2. **Clang AST Header Bridge (`import cpp "header.h"`)**: Invokes `clang -Xclang -ast-dump=json` at compile time, caches the parsed symbol table in `.cfire/cache/`, and exposes C/C++ functions, structs, and constants under the `cpp::` (or `cpp.`) namespace.
3. **Automatic Boundary Coercion**: `cfire` automatically converts `.cf` `str` arguments to `const char*` (via `cfire::c_arg`) when calling C functions expecting null-terminated strings.

### C/C++ ABI Type Coercion Matrix
| `.cf` Type / Expression | C / C++23 Parameter Type | Boundary Conversion Performed by `cfire` |
| :--- | :--- | :--- |
| `i8`..`i64`, `u8`..`u64`, `f32`, `f64`, `bool` | `int8_t`..`int64_t`, `uint8_t`..`uint64_t`, `float`, `double`, `bool` | Identical C++23 scalar type (0-cost pass-through) |
| `str` passed to `const char*` | `const char*` | Automatic `cfire::c_arg(s)` (`.c_str()`, 0 allocation) |
| `&str` | `std::string_view` | 0-copy pointer + length view |
| `s.data()` / `s.byte_ptr()` | `const char*` / `char*` | Direct raw pointer to contiguous UTF-8 buffer |
| `s.byte_ptr_at(offset)` | `char*` | Bounds-checked pointer offset `s.data() + offset` |
| `slice.data()` / `slice.data_ptr()` | `const T*` (immutable) or `T*` (`mut` slice) | Direct pointer to contiguous `std::vector<T>` buffer |
| `&T` / `&mut T` | `const T&` or `const T*` / `T&` or `T*` | Direct reference or address-of `&x` for C out-pointers |

### Level 1 — Inline `cpp { ... }` Escape Blocks
Local `.cf` parameters and variables are directly in scope inside `cpp { ... }` blocks under their exact identifiers:

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
    println("arch={} worker_slots>={}", arch, slots >= 2)
}
```
```text
# Output:
arch=arm64 worker_slots>=true
```

### Level 2 — Clang AST Header Bridge (`import cpp`)
Use `import cpp "<header.h>"` with optional `@pkg("<pkg-config-name>")` and `@define("<KEY>", "<VAL>")` attributes to import system or third-party C/C++ headers directly:

```cfire
package main

import cpp "zstd.h" @pkg("libzstd")

fn query_zstd_runtime() -> (u32, usize) {
    ver := cpp::ZSTD_versionNumber()
    bound := cpp::ZSTD_compressBound(1024)
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

**Inspecting Foreign C/C++ Headers from the CLI:**
Before writing code against a C header, run `cfire inspect` to list every ingested symbol and its mapped `.cf` signature:
```bash
cfire inspect "cpp zstd.h"
# Output:
# Header: <zstd.h> (42 symbols)
#   cpp::ZSTD_compress(void*, size_t, const void*, size_t, int) -> size_t
#   cpp::ZSTD_compressBound(size_t) -> size_t
#   cpp::ZSTD_versionNumber() -> unsigned int
```

### Level 3 — C, CMake & Make in `build.cf`
When a project combines `.cf` packages with local C sources (`build.CC`), a CMake subproject (`build.CMake`), and a Makefile library (`build.Make`), `build.cf` compiles each dependency into `.cfire/cache/deps/`, injects its `Includes` and `Defines` into the Clang AST bridge, and links all resulting archives automatically:

```cfire
package build

import "core:build"

var FastCRC = build.CC("./c_deps/fastcrc", {
    Sources:  ["fastcrc.c"],
    Includes: ["."],
    Defines:  {"FASTCRC_SEED": 42},
})

var KVStore = build.CMake("./vendor/libkv_cmake", {
    Target:   "kvstore",
    Includes: ["include"],
    Defines:  {"KV_SALT": 42},
})

var VecMath = build.Make("./vendor/libmath_make", {
    Target:   "vecmath",
    Includes: ["include"],
    Defines:  {"MATH_SCALE_BIAS": 1.0},
})

var App = build.Executable("hybrid-build-demo", {
    Entry: "main.cf",
})
```

And in `main.cf`, all three native libraries are imported directly via `import cpp`:
```cfire
package main

import cpp "fastcrc.h"
import cpp "kvstore.h"
import cpp "vecmath.h"

fn main() {
    payload := "cfire-hybrid"
    crc := cpp::fastcrc32(payload, payload.len() as u32)
    hash := cpp::kv_hash_key(payload)
    dot := cpp::vm_dot2d(2.0, 3.0, 4.0, 5.0)
    println("crc=0x{:x} kv_hash={} dot={:.1f}", crc, hash, dot)
}
```

---

## Chapter 12: Testing, Sanitizers & Diagnostics

### Architecture & Invariants
`cfire` integrates unit testing, hardware memory sanitizers, and structured machine-readable diagnostics directly into the compiler binary:
- **Zero-Config Test Blocks (`test "name" { ... }`)**: Co-located in implementation or test `.cf` files. Stripped from production `cfire build` binaries; compiled into a dedicated test harness runner during `cfire test`.
- **Source-Mapped Sanitizers (`--sanitize`)**: Compiles with `-fsanitize=address,undefined -fno-omit-frame-pointer -g` and `#line` directives so buffer overflows, use-after-free, or signed integer overflows report exact `.cf` source coordinates.
- **Auto-Remediation (`cfire fix`)**: Deterministic AST/token rewriter that repairs missing standard library imports, missing call-site `mut` annotations, and non-exhaustive `match` blocks automatically.

### Level 1 — Unit Testing (`test` & `assert_eq`)
```cfire
package main

fn fibonacci(n: i32) -> i64 {
    if n <= 1 {
        return n as i64
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

Execute tests from the CLI (each `assert(a == b)` and `assert_eq(a, b)` evaluates its operands once and formats `[left: <val>, right: <val>]` value diffs on failure):
```bash
# 1. Run all test blocks in the current workspace (or pass a specific file/directory)
cfire test
cfire test math_test.cf

# 2. Run only tests matching a substring filter
cfire test --filter "base cases"

# 3. Emit structured JSON test results for CI pipelines
cfire test --json
```

### Level 2 — Memory & UB Sanitizers (`--sanitize`)
Pass `--sanitize` to `cfire test` or `cfire run` to enable AddressSanitizer (ASan) and UndefinedBehaviorSanitizer (UBSan):

```bash
cfire test --sanitize math_test.cf
cfire run --sanitize src/main.cf
```

What `--sanitize` detects at runtime with zero false positives:
- Out-of-bounds heap, stack, or global array accesses (`import cpp` or `cpp { ... }` blocks).
- Heap use-after-free and double-free bugs across foreign C libraries.
- Signed integer overflow, shift-exponent out-of-bounds, and misaligned/null pointer dereferences.

### Level 3 — Diagnostics & Auto-Fix (`cfire fix`)
Every compiler error and semantic rule violation carries a stable identifier that can be queried in `docs/reference.html` or emitted as JSON via `cfire check --json <file.cf>`:

#### 1. Compiler Stage Diagnostic Ranges (`CF1xxx`–`CF5xxx`)
| Code Prefix | Compiler Stage | Representative Error Conditions |
| :--- | :--- | :--- |
| `CF1xxx` | Lexical Analyzer (`lexer`) | Unterminated string or raw literal, invalid numeric/hex token, unrecognized byte sequence. |
| `CF2xxx` | Syntax Parser (`parser`) | Missing closing `}` or `)`, unexpected token in `type`/`fn`/`match` declaration (`cfire/SYNTAX-ERROR`). |
| `CF3xxx` | Semantic & Borrow Checker (`sema`) | Escaping reference to stack local (`CF3002`), type mismatch across assignment/return, unknown identifier or field. |
| `CF4xxx` | ISO C++23 Emitter (`codegen`) | Unsupported target-specific intrinsic or invalid C++23 template instantiation (`cfire/TYPE-ERROR`). |
| `CF5xxx` | Build & Linker Driver (`buildsys`) | Missing `build.cf` entry target, failed external `CMake`/`Make`/`CC` build step, or unresolved native linker symbol. |

#### 2. Semantic Safety & Exclusivity Rule Catalog
| Rule Code | Enforced Invariant | `cfire fix` Auto-Fixable? |
| :--- | :--- | :---: |
| `cfire/MUT-CALLSITE` | Call site must write `mut arg` when passing to a `mut param: T` parameter. | **Yes** (inserts `mut ` at call site) |
| `cfire/IMMUTABLE-MUT-ARG` | Argument passed as `mut` must be declared with `mut` at its binding site. | **Yes** (upgrades binding to `mut`) |
| `cfire/MUT-RECEIVER` | Calling a `fn (mut self: T)` method requires a mutable struct binding. | **Yes** (upgrades receiver binding to `mut`) |
| `cfire/MUT-ALIAS` | A `mut`/`out` argument cannot alias any other argument in the same call expression. | Manual refactor required |
| `cfire/USE-AFTER-MOVE` | A variable moved with `move x` cannot be read or moved again on the same execution path. | Manual refactor required |
| `cfire/NON-EXHAUSTIVE-MATCH` | `match` over an `enum` must cover all variants (or include `_ => { ... }`). | **Yes** (generates missing variant arms) |
| `cfire/NON-DIVERGING-OR-BLOCK` | Error recovery block `val := f() or err { ... }` must diverge (`return`, `break`, `continue`, `panic`). | Manual refactor required |

```bash
# Preview automatic fixes without modifying files on disk:
cfire fix --dry-run src/main.cf

# Apply automatic fixes in place:
cfire fix src/main.cf
```

---

# Part IV: Networking, Data, UI & AI

## Chapter 13: HTTP, SSE, WebSockets & TLS

The `ext:net` family provides a unified networking stack spanning raw TCP sockets, RFC 3986 URL parsing, OpenSSL TLS 1.3 transport, full HTTP/1.1 client/server routing with Server-Sent Events (SSE), and RFC 6455 WebSockets. Every layer automatically negotiates its transport backend across Desktop (macOS/Linux), iOS (`Foundation` `NSURLSession`), WebAssembly (`emscripten_fetch`), and restricted container/sandbox environments where kernel socket binding is disabled.

### Cross-Platform Transport Matrix

| Module | Primary Types | Desktop (macOS / Linux) | iOS (`--ios`) | Web (`--web`) | Restricted Sandbox Fallback |
| :--- | :--- | :--- | :--- | :--- | :--- |
| `ext:net` | `TcpListener`, `TcpStream`, `SocketPair` | POSIX `socket` / `bind` / `connect` (`AF_INET`) | POSIX `socket` (`AF_INET`) | Unsupported (use HTTP/WS) | In-process shared-memory ring buffer for loopback (`127.0.0.1` / `localhost`) when `EPERM`/`EACCES` occurs |
| `ext:net/url` | `Url` | Native zero-allocation RFC 3986 parser | Native RFC 3986 parser | Native RFC 3986 parser | Native RFC 3986 parser |
| `ext:net/tls` | `TlsStream`, `TlsListener`, `PemPair` | Runtime `dlopen` of `libssl` / `libcrypto` (OpenSSL 1.3+) | Apple `Foundation` / `Network.framework` | Browser TLS stack | In-process TLS stream framing over loopback |
| `ext:net/http` | `Client`, `Server`, `Router`, `Request`, `Response`, `SseEvent` | Native `ext:net` + `ext:net/tls` (with `curl` fallback for external HTTPS if OpenSSL is absent) | Native `NSURLSession` synchronous bridge for external HTTPS; native `ext:net` for local loopback | Browser `emscripten_fetch` synchronous XHR bridge | In-process router registry for `127.0.0.1` / `localhost` servers |
| `ext:net/websocket` | `WebSocket`, `WsPair`, `WsServer`, `Frame` | RFC 6455 frame engine over `TcpStream` / `TlsStream` | RFC 6455 frame engine | RFC 6455 frame engine | In-memory `websocket.pair()` full-duplex channel |

### TCP Sockets & URL Parsing (`ext:net`, `url`)

| Signature | Return Type | Semantics |
| :--- | :--- | :--- |
| `net.listen(port: int = 0)` | `TcpListener!` | Binds a listening TCP socket on `127.0.0.1:port`. Passing `0` dynamically allocates an ephemeral port (queryable via `listener.port()`). |
| `listener.accept()` | `TcpStream!` | Blocks until a client connects and returns the bidirectional `TcpStream`. |
| `net.connect(host: str, port: int)` | `TcpStream!` | Establishes a TCP connection to `host:port`. |
| `net.socket_pair()` | `SocketPair!` | Creates a connected pair of local streams (`.client` and `.server`) for deterministic zero-port testing. |
| `stream.write_str(data: str)` | `int!` | Writes UTF-8 bytes to the stream and returns the byte count written. |
| `stream.read_str(max_bytes: usize = 4096)` | `str!` | Reads up to `max_bytes` from the stream. |
| `stream.read_line()` | `str!` | Reads until `\n` (stripping trailing `\r\n`). |
| `url.parse(raw: str)` | `Url!` | Parses `scheme://host:port/path?query#fragment` into `Url { scheme, host, port, path, query, fragment }`. |
| `url.encode(raw: str)` / `url.decode(raw: str)` | `str` / `str!` | Applies or decodes RFC 3986 percent-encoding (`%XX` and `+` space decoding). |

### HTTP Client, SSE & Router (`ext:net/http`)

#### HTTP Client Specification

| Function | Signature | Description |
| :--- | :--- | :--- |
| `http.get` | `fn(url: str) -> Response!` | Sends an `HTTP GET` request over `http://` or `https://` with TLS peer verification enabled. |
| `http.get_insecure` | `fn(url: str) -> Response!` | Sends an `HTTP GET` request bypassing TLS certificate chain verification (for local self-signed dev servers). |
| `http.post` | `fn(url: str, body: str, content_type: str = "application/json") -> Response!` | Sends an `HTTP POST` request with the given payload and `Content-Type` header. |
| `http.request` | `fn(method: str, url: str, body: str = "", headers: map[str]str = {}) -> Response!` | Sends an arbitrary HTTP verb (`PUT`, `PATCH`, `DELETE`, `HEAD`, `OPTIONS`) with custom headers. |
| `http.post_sse` | `fn(url: str, body: str, headers: map[str]str, on_event: fn(SseEvent) -> bool) -> void!` | Streams `text/event-stream` chunks in real time, invoking `on_event(SseEvent { event, data, id })` for each dispatched SSE frame until the stream ends or `on_event` returns `false`. |

#### HTTP Router & Server Specification

| Method / Factory | Signature | Description |
| :--- | :--- | :--- |
| `http.make_router()` | `fn() -> Router` | Constructs a pattern-matching HTTP router supporting static paths (`/health`), named parameters (`/users/:id`), and prefix wildcards (`/static/*`). |
| `router.get` / `post` / `put` / `delete_` | `fn(pattern: str, handler: fn(Request) -> Response)` | Registers a route handler for the specified HTTP method and path pattern. |
| `router.use` | `fn(middleware: fn(Request) -> int)` | Registers a middleware hook executed before route dispatch. Returning `0` continues dispatch; returning a non-zero HTTP status code short-circuits with that status. |
| `router.ws` | `fn(pattern: str, handler: fn(Request, mut WebSocket) -> void)` | Registers an RFC 6455 WebSocket upgrade endpoint on the HTTP router. |
| `router.dispatch` | `fn(req: Request) -> Response` | Executes middleware and route matching in memory without opening a network socket. |
| `req.param(name)` / `req.query(key)` / `req.header(name)` | `fn(str) -> str` | Extracts URL route parameter `:name`, query parameter `?key=val`, or case-insensitive request header. |
| `http.Response.text` / `json` / `html` / `redirect` | `fn(status: int, body: str) -> Response` | Constructs an `http.Response` with the appropriate `Content-Type` (`text/plain`, `application/json`, `text/html`) or `Location` header. |
| `http.make_server` | `fn(port: int = 0) -> Server!` | Binds a multi-threaded HTTP/1.1 server on `port` (`0` picks an ephemeral port via `server.port()`). |
| `http.make_tls_server_pem` | `fn(cert_pem: str, key_pem: str, port: int = 0) -> Server!` | Binds an HTTPS (TLS 1.3) server using in-memory PEM certificate and private key strings. |
| `server.serve_in_background` | `fn(router: Router) -> void` | Spawns the background accept loop dispatching incoming connections through `router`. |
| `server.stop` | `fn() -> void` | Signals the accept loop to terminate and joins worker threads. |

### Level 1 — HTTP Routing & Live SSE Streaming

```cfire
import "ext:net/http"

fn main() -> void! {
    mut router := http.make_router()

    // Middleware: reject requests missing X-Api-Version when targeting /v1/*
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
    mut server := try http.make_server(0)
    defer server.stop()
    server.serve_in_background(router)

    base := f"http://127.0.0.1:{server.port()}"
    live := try http.get(base + "/users/99?format=compact")
    println("Live GET status={}, body={}", live.status, live.body)

    // 3. Consume SSE stream in real time
    mut chunks := []str{}
    try http.post_sse(base + "/v1/stream", "{}", map[str]str{}, |ev: http.SseEvent| -> bool {
        chunks.push(ev.event + ":" + ev.data)
        return ev.data != "[DONE]"
    })
    println("SSE events ({}): {}", chunks.len(), chunks[0])
}
```

**Line-by-Line Mechanics:**

1. `router.use(...)` attaches a pre-dispatch middleware closure. Returning `0` allows execution to proceed to the matched route handler; returning an HTTP status code (`403`) halts dispatch immediately.
2. `router.get("/users/:id", ...)` extracts `:id` via `req.param("id")` and query string parameters (`?format=...`) via `req.query("format")`.
3. `router.dispatch(...)` tests routing and middleware deterministically in memory without allocating a kernel socket.
4. `http.make_server(0)` binds to `127.0.0.1` on an OS-assigned ephemeral port (`server.port()`). `defer server.stop()` guarantees clean socket teardown on scope exit.
5. `http.post_sse(...)` parses `event:` and `data:` lines from a `text/event-stream` response, invoking the callback for each completed SSE frame until the callback returns `false`.

#### Expected Output

```text
In-memory status=200, body={"id":"42","format":"full"}
Live GET status=200, body={"id":"99","format":"compact"}
SSE events (2): token:{"chunk":"hello"}
```

### TLS 1.3 & WebSockets (`tls`, `websocket`)

#### WebSocket & TLS Specification

| Function / Method | Signature | Description |
| :--- | :--- | :--- |
| `tls.available()` | `fn() -> bool` | Returns `true` if the OpenSSL 1.3 dynamic bridge (`libssl` / `libcrypto`) is loaded on the host. |
| `tls.generate_self_signed_pem(cn)` | `fn(cn: str = "localhost") -> PemPair!` | Generates an ephemeral RSA-2048 X.509 v3 certificate and private key in memory (`.cert_pem`, `.key_pem`). |
| `websocket.pair()` | `fn() -> WsPair` | Returns a connected in-memory `WsPair { client, server }` for deterministic RFC 6455 frame testing. |
| `websocket.connect(url)` | `fn(url: str) -> WebSocket!` | Performs an RFC 6455 HTTP `Upgrade: websocket` handshake (`Sec-WebSocket-Key` / `Sec-WebSocket-Accept`) over `ws://` or `wss://`. |
| `websocket.connect_insecure(url)` | `fn(url: str) -> WebSocket!` | Connects to a `wss://` endpoint bypassing TLS certificate verification. |
| `ws.send_text(msg)` / `ws.recv_text()` | `fn(str) -> void!` / `fn() -> str!` | Sends or receives a UTF-8 text WebSocket frame (opcode `0x1`), automatically applying client-to-server masking per RFC 6455 §5.3. |
| `ws.send_binary(bytes)` / `ws.recv_binary()` | `fn([]byte) -> void!` / `fn() -> []byte!` | Sends or receives a binary WebSocket frame (opcode `0x2`). |
| `ws.ping(payload)` / `ws.close()` | `fn(str) -> void!` / `fn() -> void!` | Sends a control Ping frame (`0x9` — `recv_text()` automatically replies with Pong `0xA`) or initiates the Close handshake (`0x8`). |

### Level 2 — TLS 1.3 HTTPS & WebSocket Upgrade

```cfire
import "ext:net/http"
import "ext:net/tls"
import "ext:net/websocket"

fn main() -> void! {
    // 1. Bidirectional RFC 6455 WebSocket upgrade on an HTTP router
    mut router := http.make_router()
    router.ws("/ws/echo", |req: http.Request, mut ws: websocket.WebSocket| {
        if msg := ws.recv_text() {
            _ = ws.send_text("echo:" + msg)
        }
        _ = ws.close()
    })

    mut srv := try http.make_server(0)
    defer srv.stop()
    srv.serve_in_background(router)

    mut client := try websocket.connect(f"ws://127.0.0.1:{srv.port()}/ws/echo")
    defer { _ = client.close() }
    try client.send_text("cfire-frame")
    reply := try client.recv_text()
    println("WS reply: {}", reply)

    // 2. Self-signed TLS 1.3 HTTPS server if OpenSSL is available on the host
    if tls.available() {
        pem := try tls.generate_self_signed_pem("localhost")
        mut tls_router := http.make_router()
        tls_router.get("/secure", |req: http.Request| -> http.Response {
            return http.Response.text(200, "TLS-1.3-OK")
        })
        mut tls_srv := try http.make_tls_server_pem(pem.cert_pem, pem.key_pem, 0)
        defer tls_srv.stop()
        tls_srv.serve_in_background(tls_router)

        sec := try http.get_insecure(f"https://127.0.0.1:{tls_srv.port()}/secure")
        println("HTTPS status={}, body={}", sec.status, sec.body)
    }
}
```

**Line-by-Line Mechanics:**

1. `router.ws("/ws/echo", ...)` registers a WebSocket endpoint on the standard HTTP router. When a client sends `Upgrade: websocket`, the server validates `Sec-WebSocket-Key`, replies with `HTTP/1.1 101 Switching Protocols`, and transfers the underlying stream to the handler's `mut ws: websocket.WebSocket`.
2. `websocket.connect("ws://...")` performs the client handshake, masks outbound frames with a random 32-bit key, and unmasks inbound server frames.
3. `tls.generate_self_signed_pem("localhost")` synthesizes an X.509 certificate and private key in memory without touching disk or shelling out to the `openssl` CLI.
4. `http.make_tls_server_pem(...)` wraps the accept loop in a TLS 1.3 context, and `http.get_insecure(...)` verifies the encrypted round-trip against the self-signed certificate.

#### Expected Output

```text
WS reply: echo:cfire-frame
HTTPS status=200, body=TLS-1.3-OK
```

---

## Chapter 14: JSON, SQLite, Zstd & Cryptography

`cfire` ships zero-boilerplate compile-time JSON serialization (`ext:json`), an embedded SQLite 3 engine with hardware-accelerated SIMD vector similarity functions (`ext:db/sqlite`), RFC 8878 Zstandard compression (`ext:compress/zstd`), and NIST/RFC cryptographic primitives (`core:crypto/*`, `core:encoding/*`).

### Compile-Time JSON & Schema (`ext:json`)

`ext:json` uses the compiler's `cfire::reflect<T>` metadata tables (Chapter 6) to parse and serialize native `cfire` structs, slices, maps, and `Option` fields without runtime type reflection overhead, plus provides a dynamic DOM (`json.Value`) for unstructured payloads.

| Function | Signature | Description |
| :--- | :--- | :--- |
| `json.parse[T]` | `fn[T](raw: str) -> T!` | Parses a UTF-8 JSON string directly into struct, slice, map, or primitive `T`. Honors `` `json:"name"` `` and `` `validate:"..."` `` field tags. |
| `json.stringify[T]` | `fn[T](val: T, pretty: bool = false) -> str` | Serializes `val` to compact (`pretty = false`) or 2-space indented (`pretty = true`) JSON. |
| `json.schema[T]` | `fn[T]() -> str` | Generates a Draft 2020-12 JSON Schema object describing `T`'s fields, types, and `required` constraints for LLM structured outputs (`ext:ai`). |
| `json.parse_dynamic` | `fn(raw: str) -> json.Value!` | Parses arbitrary untyped JSON into a navigable `json.Value` DOM tree. |
| `json.stringify_dynamic` | `fn(val: json.Value, pretty: bool = false) -> str` | Serializes a `json.Value` DOM node back to a JSON string. |

### Level 1 — Struct JSON, Schema & Dynamic DOM

```cfire
import "ext:json"

type Metric struct {
    Name:  str      `json:"name"`
    Value: f64      `json:"value"`
    Tags:  []str    `json:"tags"`
    Note:  Option[str] `json:"note"`
}

fn main() -> void! {
    raw := "{\"name\":\"cpu.load\",\"value\":92.5,\"tags\":[\"prod\",\"us-east\"],\"note\":null}"
    m := try json.parse[Metric](raw)
    println("Parsed: {} = {:.1f} (tags={}, has_note={})", m.Name, m.Value, m.Tags.len(), m.Note.is_some())

    // Round-trip back to compact JSON and emit Draft 2020-12 JSON Schema
    encoded := json.stringify(m)
    schema  := json.schema[Metric]()
    println("Encoded: {}", encoded)
    println("Schema has properties: {}", schema.contains("\"properties\""))

    // Dynamic DOM parsing for unstructured payloads
    dom := try json.parse_dynamic(raw)
    println("Dynamic lookup: name={}, tag[0]={}", dom.get("name").as_str(), dom.get("tags").at(0).as_str())
}
```

**Line-by-Line Mechanics:**

1. `json.parse[Metric](raw)` maps JSON keys to struct fields using the compile-time `` `json:"..."` `` tags. A JSON `null` (or omitted key) targeting an `Option[T]` field deserializes cleanly to `None`.
2. `json.stringify(m)` emits compact JSON without intermediate DOM allocation.
3. `json.schema[Metric]()` inspects `cfire::reflect<Metric>` at compile time to emit the JSON Schema Draft 2020-12 specification used by `ai.generate_struct[T]` (Chapter 17).
4. `json.parse_dynamic(raw)` constructs a `json.Value` tree supporting `.get(key)`, `.at(index)`, `.as_str()`, `.as_f64()`, `.as_i64()`, and `.as_bool()`.

#### Expected Output

```text
Parsed: cpu.load = 92.5 (tags=2, has_note=false)
Encoded: {"name":"cpu.load","value":92.5,"tags":["prod","us-east"],"note":null}
Schema has properties: true
Dynamic lookup: name=cpu.load, tag[0]=prod
```

### SQLite WAL & Vector Search (`ext:db/sqlite`)

`ext:db/sqlite` embeds SQLite 3 with Write-Ahead Logging (`PRAGMA journal_mode=WAL;`), positional parameter binding (`?1`, `?2`, ...), compile-time row-to-struct mapping (`db.query[T]`), RAII transaction scopes (`db.tx`), and custom SQL scalar functions for hardware-accelerated vector similarity search (`vec_cosine_similarity` and `vec_l2_distance`).

> [!NOTE]
> **Cross-Platform Database Paths:** On iOS (`--ios`), passing a relative filename to `sqlite.open("app.db")` automatically resolves writable paths inside the app's `NSHomeDirectory()/Documents` sandbox container (or falls back to a read-only bundle resource if present in the `.app` bundle). On WebAssembly (`--web`), `sqlite.open` operates against Emscripten's virtual MEMFS filesystem.

| Function / Method | Signature | Description |
| :--- | :--- | :--- |
| `sqlite.open(path)` | `fn(path: str) -> DB!` | Opens or creates a SQLite 3 database at `path` (or `":memory:"`), enables `WAL` mode and `FOREIGN_KEYS`, and registers the `vec_cosine_similarity` and `vec_l2_distance` SQL functions. |
| `db.exec(sql, ...args)` | `fn(sql: str, ...args) -> int!` | Prepares `sql`, binds variadic arguments (`int`, `i64`, `f64`, `bool`, `str`, `[]byte`, `[]f32`) to `?1`, `?2`, ..., executes the statement, and returns the number of affected rows. |
| `db.query[T](sql, ...args)` | `fn[T](sql: str, ...args) -> []T!` | Executes a `SELECT` query and maps each result row into struct `T` (matching column names against `` `db:"..."` `` or `` `json:"..."` `` tags, or positional field order) or primitive `T`. |
| `db.query_one[T](sql, ...args)` | `fn[T](sql: str, ...args) -> Option[T]!` | Executes `sql` and returns `Some(row)` for the first row or `None` if the result set is empty. |
| `db.tx(fn_body)` | `fn(body: fn() -> void!) -> void!` | Wraps `body()` in `BEGIN IMMEDIATE` ... `COMMIT`, automatically issuing `ROLLBACK` if `body()` returns an `Err`. |
| `db.last_insert_rowid()` | `fn() -> i64` | Returns the `ROWID` of the most recent successful `INSERT` on the connection. |
| `sqlite.vec_to_blob(vec)` / `sqlite.blob_to_vec(b)` | `fn([]f32) -> []byte` / `fn([]byte) -> []f32` | Packs or unpacks an `[]f32` embedding slice into a contiguous little-endian IEEE-754 `BLOB` (note: `db.exec` and `db.query` also bind `[]f32` slices directly). |

### Level 2 — SQLite Transactions & Vector Search

```cfire
import "ext:db/sqlite"

type DocHit struct {
    Id:    i64 `db:"id"`
    Title: str `db:"title"`
    Score: f64 `db:"score"`
}

fn main() -> void! {
    mut db := try sqlite.open(":memory:")
    defer db.close()

    try db.exec("CREATE TABLE docs (id INTEGER PRIMARY KEY, title TEXT, embedding BLOB)")

    // Atomic transaction inserting 3 normalized 3D embeddings as IEEE-754 f32 BLOBs
    try db.tx(|| -> void! {
        try db.exec("INSERT INTO docs (title, embedding) VALUES (?1, ?2)", "Kernel Scheduler", []f32{0.95, 0.05, 0.0})
        try db.exec("INSERT INTO docs (title, embedding) VALUES (?1, ?2)", "HTTP Router",      []f32{0.10, 0.90, 0.0})
        try db.exec("INSERT INTO docs (title, embedding) VALUES (?1, ?2)", "SQLite WAL Engine", []f32{0.85, 0.15, 0.0})
    })

    // Query top-2 nearest documents using the built-in SIMD vec_cosine_similarity SQL function
    query_vec := []f32{1.0, 0.0, 0.0}
    hits := try db.query[DocHit](
        "SELECT id, title, vec_cosine_similarity(embedding, ?1) AS score FROM docs ORDER BY score DESC LIMIT 2",
        query_vec
    )

    for hit in hits {
        println("#{} {} (cosine={:.4f})", hit.Id, hit.Title, hit.Score)
    }
}
```

**Line-by-Line Mechanics:**

1. `sqlite.open(":memory:")` initializes an in-memory SQLite instance and registers `vec_cosine_similarity(blob_a, blob_b)` and `vec_l2_distance(blob_a, blob_b)` using ARM NEON (`float32x4_t`) on Apple Silicon/iOS and AVX2/SSE on x86_64.
2. `db.tx(|| -> void! { ... })` executes all three `INSERT` statements inside a single atomic transaction. Passing `[]f32{...}` to `?2` automatically binds the slice as a raw `sizeof(float) * N` SQLite `BLOB`.
3. `db.query[DocHit](...)` binds `query_vec` to `?1`, computes cosine similarity in C++ across every row, sorts in descending order, and maps columns `id`, `title`, and `score` directly into `DocHit` structs via the `` `db:"..."` `` tags.

#### Expected Output

```text
#1 Kernel Scheduler (cosine=0.9986)
#3 SQLite WAL Engine (cosine=0.9848)
```

### Zstandard, Cryptography & Encodings

| Module | Function | Signature | Description |
| :--- | :--- | :--- | :--- |
| `ext:compress/zstd` | `zstd.compress` / `zstd.compress_str` | `fn(data: []byte, level: int = 3) -> []byte!` | Compresses bytes or UTF-8 string into an RFC 8878 Zstandard frame (`0xFD2FB528` magic header + XXH64 content checksum). Uses system `libzstd` when available or the built-in pure-C++23 RFC 8878 frame encoder. |
| `ext:compress/zstd` | `zstd.decompress` / `zstd.decompress_str` | `fn(frame: []byte) -> []byte!` / `str!` | Verifies the RFC 8878 Zstandard frame header and XXH64 checksum and restores the original payload. |
| `core:crypto/sha256` | `sha256.hash` / `sha256.hash_hex` | `fn(data: str) -> [32]byte` / `str` | Computes the FIPS 180-4 SHA-256 digest as a 32-byte array or 64-character lowercase hex string. |
| `core:crypto/blake3` | `blake3.hash` / `blake3.hash_hex` / `blake3.keyed_hash_hex` | `fn(data: str) -> [32]byte` / `str` | Computes the cryptographic BLAKE3 hash (or 32-byte keyed MAC). |
| `core:crypto/hmac` | `hmac.sha256_hex` / `hmac.verify` / `hmac.equal` | `fn(key: str, msg: str) -> str` / `bool` | Computes RFC 2104 `HMAC-SHA256` and performs constant-time `O(N)` tag verification immune to timing side-channels. |
| `core:crypto/aes` | `aes.encrypt_gcm` / `aes.decrypt_gcm` | `fn(key: []byte, nonce: []byte, plaintext: []byte, aad: []byte = {}) -> []byte!` | Performs NIST SP 800-38D `AES-256-GCM` authenticated encryption (32-byte key, 12-byte nonce, 16-byte authentication tag appended). |
| `core:encoding/base64` | `base64.encode` / `decode_str` / `encode_url` | `fn(str) -> str` / `fn(str) -> str!` | RFC 4648 Standard and URL-safe Base64 encoding and decoding. |
| `core:encoding/hex` | `hex.encode_str` / `decode_str` | `fn(str) -> str` / `fn(str) -> str!` | Lowercase hexadecimal encoding and decoding. |
| `core:encoding/csv` | `csv.parse` / `csv.parse_Stream[T]` / `csv.stringify` | `fn(raw: str) -> [][]str!` / `[]T!` | RFC 4180 CSV parser (supporting quoted fields and escaped quotes) and struct slice mapper. |

### Level 3 — Zstd Compression & AES-256-GCM

```cfire
import "ext:compress/zstd"
import "core:crypto/aes"
import "core:crypto/hmac"
import "core:crypto/sha256"
import "core:encoding/hex"

fn main() -> void! {
    payload := "ts=1730419200 level=INFO msg=\"request completed\" latency_ms=4.2\n"
    // 1. Compress UTF-8 payload into an RFC 8878 Zstandard frame
    compressed := try zstd.compress_str(payload, 3)
    restored   := try zstd.decompress_str(compressed)
    println("Zstd round-trip match: {} (frame={} bytes)", restored == payload, compressed.len())

    // 2. Derive a 32-byte key with SHA-256 and encrypt with AES-256-GCM
    key_arr := sha256.hash("master-secret-passphrase")
    mut key := []byte{}
    for b in key_arr { key.push(b) }
    nonce := try hex.decode("000102030405060708090a0b") // 12-byte GCM IV
    aad   := try hex.decode("feedface")

    ciphertext := try aes.encrypt_gcm(key, nonce, compressed, aad)
    decrypted  := try aes.decrypt_gcm(key, nonce, ciphertext, aad)
    final_str  := try zstd.decompress_str(decrypted)

    // 3. Constant-time HMAC-SHA256 verification
    mac := hmac.sha256_hex("webhook-secret", final_str)
    println("AES-GCM + Zstd verified: {}, HMAC valid: {}", final_str == payload, hmac.verify("webhook-secret", final_str, mac))
}
```

**Line-by-Line Mechanics:**

1. `zstd.compress_str(payload, 3)` compresses the log line into an RFC 8878 frame, and `zstd.decompress_str(compressed)` verifies the magic number and checksum before returning the original `str`.
2. `aes.encrypt_gcm(key, nonce, compressed, aad)` encrypts the compressed frame using a 256-bit key (`32` bytes), a 96-bit nonce (`12` bytes), and Additional Authenticated Data (`aad`), appending the 128-bit (`16`-byte) authentication tag to `ciphertext`.
3. Modifying a single byte of `ciphertext` or `aad` causes `aes.decrypt_gcm` to return an `Err("aes.decrypt_gcm: authentication tag mismatch")` before any plaintext is released.
4. `hmac.verify` compares the expected and computed HMAC digests in constant time via bitwise XOR accumulation.

#### Expected Output

```text
Zstd round-trip match: true (frame=77 bytes)
AES-GCM + Zstd verified: true, HMAC valid: true
```

---

## Chapter 15: 2D/3D Graphics & Native UI

`cfire` integrates `vendor:raylib` (Raylib 5.0 2D/3D graphics, input, and `<raymath.h>`) and `vendor:raygui` (immediate-mode UI controls) with zero external build configuration. A single `.cf` source file compiles to native macOS Metal/OpenGL `.app` bundles, Linux X11/Wayland executables, HTML5 WebGL2/Canvas WebAssembly pages (`--web`), and native iOS `UIKit`/`OpenGLES` applications (`--ios`).

### Cross-Platform Window & Font Pipeline

| Subsystem | macOS (`darwin`) | Linux (`linux`) | iOS (`--ios`) | WebAssembly (`--web`) |
| :--- | :--- | :--- | :--- | :--- |
| **Window & Context** | Cocoa (`NSWindow`) + OpenGL 3.3 / Metal layer (`FLAG_WINDOW_HIGHDPI`) | X11 / Wayland + OpenGL 3.3 + auto-discovered `icon.png` (`SetWindowIcon`) | Native `UIWindow` + `UIViewController` + `CADisplayLink` 60/120 Hz | HTML5 `<canvas>` + WebGL2 + `emscripten_set_main_loop` |
| **High-DPI Font Rasterizer** | `CoreText.framework` full-line subpixel/2x Retina texture rasterizer | Automatic system TrueType discovery (`fc-match` / DejaVu / Liberation / Noto) at 2x oversampling | Native `UIFont` (`SF Pro`) glyph atlas + UTF-8 punctuation/Latin-1 folding | Embedded 2x high-DPI font atlas + UTF-8 punctuation folding |
| **Safe Area & Keyboard** | Full window viewport + hardware keyboard | Full window viewport + hardware keyboard | `GetSafeAreaRect()` insets past Dynamic Island & Home Indicator; `RequestKeyboard()` invokes `<UIKeyInput>` | Full canvas viewport + DOM keyboard events |
| **Haptic Feedback** | No-op on desktop | No-op on desktop | `TriggerHaptic(style)` invokes `UIImpactFeedbackGenerator` (Taptic Engine) | `TriggerHaptic(style)` invokes `navigator.vibrate([12..35])` |

### `vendor:raylib` & `raygui` Specification

| Module | Function | Signature | Description |
| :--- | :--- | :--- | :--- |
| `vendor:raylib` | `InitWindow` / `CloseWindow` | `fn(width: i32, height: i32, title: str)` / `fn()` | Initializes the window and GPU context (`0, 0` uses full screen on iOS/mobile). Pair with `defer rl.CloseWindow()`. |
| `vendor:raylib` | `SetTargetFPS` / `GetFrameTime` | `fn(fps: i32)` / `fn() -> f32` | Caps frame rate and returns the delta time (`dt`) in seconds since the previous frame. |
| `vendor:raylib` | `WindowShouldClose` | `fn() -> bool` | Returns `true` when `KEY_ESCAPE` or the OS window close button is pressed. |
| `vendor:raylib` | `BeginDrawing` / `EndDrawing` | `fn()` / `fn()` | Opens and flushes the frame's backbuffer swap. |
| `vendor:raylib` | `BeginMode2D` / `EndMode2D` | `fn(camera: rl.Camera2D)` / `fn()` | Applies a 2D affine camera transform (`offset`, `target`, `rotation`, `zoom`). |
| `vendor:raylib` | `BeginScissorMode` / `EndScissorMode` | `fn(x: i32, y: i32, w: i32, h: i32)` / `fn()` | Clips subsequent draw calls to the pixel rectangle `(x, y, w, h)` for scroll views. |
| `vendor:raylib` | `DrawText` / `DrawRectangleRounded` / `DrawCircleV` | `fn(...)` | Hardware-accelerated text and primitive rendering with automatic `cfire` `str` to `const char*` lowering. |
| `vendor:raylib` | `GetSafeAreaRect` | `fn() -> rl.Rectangle` | Returns the usable screen area inset past mobile notches, Dynamic Island, and Home Indicator bars. |
| `vendor:raylib` | `RequestKeyboard` / `TriggerHaptic` | `fn()` / `fn(style: i32)` | Summons the mobile software keyboard or fires a haptic pulse (`0` = Light, `1` = Medium, `2` = Heavy). |
| `vendor:raygui` | `GuiButton` / `GuiWindowBox` | `fn(bounds: rl.Rectangle, text: str) -> i32` | Renders a clickable button or window container; returns `1` on click/close. |
| `vendor:raygui` | `GuiCheckBox` | `fn(bounds: rl.Rectangle, text: str, checked: &mut bool) -> i32` | Renders a checkbox and mutates `checked` in place. |
| `vendor:raygui` | `GuiSliderBar` / `GuiProgressBar` | `fn(bounds: rl.Rectangle, left: str, right: str, value: &mut f32, min: f32, max: f32) -> i32` | Renders a continuous slider or progress bar and clamps `value` within `[min, max]`. |
| `vendor:raygui` | `GuiComboBox` | `fn(bounds: rl.Rectangle, text: str, active: &mut i32) -> i32` | Renders a semicolon-delimited (`"Low;Medium;High"`) dropdown selector updating `active`. |
| `vendor:raygui` | `GuiTextBox` | `fn(bounds: rl.Rectangle, text: &mut str, textSize: i32, editMode: bool) -> i32` | Renders an interactive text input field backed directly by a mutable `cfire` `str`. |

### Level 1 — Raygui Controls & Live Text Input

```cfire
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

        // Immediate-mode control panel
        _ = rg.GuiWindowBox(rl.Rectangle{x: 24.0, y: 24.0, width: 340.0, height: 260.0}, "Node Configuration")
        _ = rg.GuiCheckBox(rl.Rectangle{x: 44.0, y: 68.0, width: 20.0, height: 20.0}, "Enable VSync Lock", &mut vsync)
        _ = rg.GuiSliderBar(rl.Rectangle{x: 90.0, y: 108.0, width: 200.0, height: 20.0}, "Gain", f"{gain_db:.1f} dB", &mut gain_db, -24.0, 12.0)
        _ = rg.GuiComboBox(rl.Rectangle{x: 44.0, y: 148.0, width: 246.0, height: 28.0}, "Eco;Balanced;Turbo", &mut profile)

        if rg.GuiTextBox(rl.Rectangle{x: 44.0, y: 192.0, width: 246.0, height: 28.0}, &mut hostname, 64, editing) != 0 {
            editing = !editing
        }
        if rg.GuiButton(rl.Rectangle{x: 44.0, y: 234.0, width: 246.0, height: 32.0}, "Apply Settings") != 0 {
            rl.TriggerHaptic(1)
        }

        rl.DrawText(f"Host: {hostname} | Gain: {gain_db:.1f} dB", 400, 40, 20, rl.RAYWHITE)
    }
}
```

**Line-by-Line Mechanics:**

1. `rl.InitWindow(800, 480, ...)` creates a high-DPI window and binds `CoreText` (macOS) or Fontconfig TrueType (Linux) so `rl.DrawText` and `rg.Gui*` controls render at native Retina resolution.
2. `rg.GuiTextBox(..., &mut hostname, 64, editing)` passes a mutable reference to a native `cfire` `str` (`std::string`), syncing edits back into `hostname` without manual `char[]` scratch buffers.
3. `f"{gain_db:.1f} dB"` formats a string inline and lowers automatically to `const char*` via `cfire::c_arg(...)`.

### Mobile Safe Areas & Multi-Target Packaging

When targeting mobile (`--ios`) or responsive web (`--web`), combine `rl.GetSafeAreaRect()`, `rl.BeginScissorMode(...)`, `rl.RequestKeyboard()`, and `rl.TriggerHaptic(...)` (or the zero-coordinate layout kit in [`examples/13-ios-native-ui/ui/kit.cf`](file:///Users/memo/Programming/cfire/examples/13-ios-native-ui/ui/kit.cf) using `GuiIos*` patterns) to adapt to any viewport:

```bash
# 1. Native macOS .app bundle (suppresses terminal window and generates Info.plist + Apple Silicon binary)
cfire build --subsystem=gui examples/13-ios-native-ui/main.cf -o PocketStudio

# 2. WebAssembly HTML5 Canvas + WebGL2 + WebAudio bundle (--target=wasm or --web)
cfire build --target=wasm examples/13-ios-native-ui/main.cf -o build/web/index.html
cfire run --web examples/13-ios-native-ui/main.cf

# 3. Native iOS Simulator .app bundle (compiles against iPhoneSimulator.sdk and launches in Simulator.app)
cfire run --ios examples/13-ios-native-ui/main.cf
```

---

## Chapter 16: Audio Synthesis & Real-Time DSP

`ext:audio` provides a zero-dependency, 64-voice lock-free stereo `f32` audio engine, procedural waveform/SFX generators, ADSR envelope shaping, real-time push ring buffers (`Stream`), master RMS/peak metering, and a pure-C++23 RIFF/WAV encoder/decoder.

### Audio Architecture & Backend Matrix

| Target Platform | Hardware Backend | Mixer Architecture | Headless / CI Fallback |
| :--- | :--- | :--- | :--- |
| **macOS & iOS (`__APPLE__`)** | `AudioToolbox.framework` (`AudioQueueNewOutput`) | 64-voice lock-free `f32` stereo mixer + 32-stream ring buffers with linear pitch resampling and constant-power stereo panning | Automatic software mixer when `CFIRE_HEADLESS_TEST=1` or no audio output device is present |
| **Linux (`__linux__`)** | Runtime `dlopen("libasound.so.2")` (ALSA PCM non-blocking thread) | Identical 64-voice lock-free `f32` stereo mixer | Automatic fallback if `libasound.so.2` or `/dev/snd` is unavailable |
| **WebAssembly (`__EMSCRIPTEN__`)** | Browser `AudioContext` + `ScriptProcessorNode` (auto-resumed on pointer/key gesture) | Identical 64-voice `f32` stereo mixer | Software buffer advancement in headless Node/WASM |

### Complete `ext:audio` Specification

| Category | Function / Method | Signature | Description |
| :--- | :--- | :--- | :--- |
| **Lifecycle & Metering** | `audio.init` / `audio.close` | `fn(sample_rate: i32 = 48000, channels: i32 = 2) -> void!` / `fn()` | Starts and stops the hardware audio device and mixer thread. |
| **Lifecycle & Metering** | `audio.set_master_volume` / `master_volume` | `fn(vol: f32)` / `fn() -> f32` | Sets or queries master output gain (`0.0` to `1.0`). |
| **Lifecycle & Metering** | `audio.rms_level` / `audio.peak_level` | `fn() -> f32` / `fn() -> f32` | Returns live master Root-Mean-Square (`rms_level`) or Peak (`peak_level`) amplitude in `[0.0, 1.0]`. |
| **Procedural Synthesis** | `audio.gen_tone` | `fn(kind: Waveform, freq_hz: f32, duration_sec: f32, volume: f32 = 0.5) -> Wave` | Synthesizes a band-controlled `Waveform` (`Sine`, `Square`, `Saw`, `Triangle`, `Noise`) with an attack/release envelope. |
| **Procedural Synthesis** | `audio.gen_chirp` | `fn(start_hz: f32, end_hz: f32, duration_sec: f32, kind: Waveform = Waveform::Sine, volume: f32 = 0.5) -> Wave` | Synthesizes a continuous frequency sweep from `start_hz` to `end_hz`. |
| **Procedural Synthesis** | `audio.gen_sfx` | `fn(preset: SfxPreset) -> Wave` | Synthesizes zero-asset procedural sound effects: `SfxPreset::Tap`, `Coin`, `Laser`, `Explosion`, `PowerUp`, `Jump`, `Alert`. |
| **RIFF/WAV Codec** | `audio.encode_wav` / `decode_wav` | `fn(wave: Wave) -> []byte` / `fn(data: []byte) -> Wave!` | Encodes a `Wave` to a 16-bit PCM RIFF/WAV byte slice or decodes 8/16/24-bit PCM and 32-bit IEEE float WAV data. |
| **RIFF/WAV Codec** | `audio.save_wav` / `load_wav` | `fn(wave: Wave, path: str) -> void!` / `fn(path: str) -> Wave!` | Reads or writes `.wav` files on disk (resolving iOS `.app` bundle and `Documents/` paths automatically). |
| **Polyphonic `Sound`** | `audio.load_sound_from_wave` / `load_sound` | `fn(wave: Wave) -> Sound` / `fn(path: str) -> Sound!` | Uploads PCM frames into a mixer slot and returns a lightweight `Sound` handle. |
| **Polyphonic `Sound`** | `sound.play` / `stop` | `fn()` / `fn()` | Allocates a free voice from the 64-voice pool for immediate polyphonic playback, or stops active voices for this `Sound`. |
| **Polyphonic `Sound`** | `sound.set_volume` / `set_pitch` / `set_pan` / `set_looping` | `fn(f32)` / `fn(f32)` / `fn(f32)` / `fn(bool)` | Adjusts per-sound gain (`0.0..2.0`), pitch playback rate (`0.1..4.0`), stereo pan (`-1.0` left .. `+1.0` right), or seamless looping. |
| **Real-Time `Stream`** | `audio.open_stream` | `fn(sample_rate: i32 = 48000, channels: i32 = 2) -> Stream!` | Allocates a lock-free SPSC ring buffer stream mixed into the master output. |
| **Real-Time `Stream`** | `stream.needs_data` / `push_samples` | `fn(target_frames: i32 = 2048) -> bool` / `fn(samples: []f32)` | Checks if queued frames dropped below `target_frames` and pushes interleaved stereo `f32` samples. |

### Level 1 — Polyphonic SFX, Streams & WAV Codec

```cfire
import "ext:audio"
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

**Line-by-Line Mechanics:**

1. `audio.init(48000, 2)` starts the 48 kHz stereo mixer. In headless test environments without audio hardware, `ext:audio` initializes the software mixer transparently so `rms_level()`, `peak_level()`, and stream buffers remain testable.
2. `coin_sfx.set_pitch(1.15)` and `coin_sfx.set_pan(-0.25)` configure linear-interpolated resampling and constant-power stereo panning before `coin_sfx.play()` triggers a voice in the 64-voice pool.
3. `stream.needs_data(2048)` queries the lock-free ring buffer fill level, and `stream.push_samples(chirp.samples)` enqueues interleaved `f32` frames without blocking the audio callback thread.
4. `audio.save_wav` and `audio.load_wav` serialize and parse standard 44-byte RIFF/WAV headers and 16-bit little-endian PCM frames.

#### Expected Output

```text
WAV bytes: 9644, frames: 2400, channels: 2, rms>=0: true
```

---

## Chapter 17: LLMs, Multimodal RAG & MCP

`ext:ai` provides a unified, zero-dependency SDK for cloud and local LLM inference, compile-time JSON Schema structured extraction (`generate_struct[T]`), multimodal image/audio/document attachments, offline document and slide deck ingestion (`load_document`, `chunk_document`), on-device OCR (`ocr`), local text and visual embeddings (`embed`, `embed_image`), language identification (`detect_language`), and a JSON-RPC 2.0 Model Context Protocol server (`McpServer`).

### Provider Client & Auth Matrix

| Constructor | Default Base URL | Default Auth Environment Variable | Wire Protocol & Capabilities |
| :--- | :--- | :--- | :--- |
| `ai.openai(api_key = "", base_url = "https://api.openai.com/v1")` | `https://api.openai.com/v1` | `OPENAI_API_KEY` | OpenAI `/chat/completions`, `/embeddings`, SSE streaming, `response_format: json_schema`, tool calling, vision & PDF/audio input |
| `ai.anthropic(api_key = "", base_url = "https://api.anthropic.com/v1")` | `https://api.anthropic.com/v1` | `ANTHROPIC_API_KEY` | Anthropic `/v1/messages` (`2023-06-01`), SSE streaming, tool use, native image/PDF document blocks, browser CORS support |
| `ai.gemini(api_key = "", base_url = "https://generativelanguage.googleapis.com/v1beta")` | `https://generativelanguage.googleapis.com/v1beta` | `GEMINI_API_KEY` or `GOOGLE_API_KEY` | Gemini `:generateContent`, `:streamGenerateContent?alt=sse`, `:embedContent`, `responseSchema`, `inlineData` multimodal attachments |
| `ai.ollama(host = "http://127.0.0.1:11434")` | `http://127.0.0.1:11434` | `OLLAMA_HOST` | Local Ollama `/v1/chat/completions` & `/api/embed` (vision, tools, structured JSON schema) |
| `ai.openai_compatible(base_url, api_key = "")` | User-specified URL | Optional `api_key` | Any OpenAI-compatible server (`vLLM`, `llama-server`, `LM Studio`, `Groq`, `Together`, `DeepSeek`) |
| `ai.apple()` | On-device (`NaturalLanguage` / `Vision`) | None (100% offline) | Apple `NLEmbedding`, `NLLanguageRecognizer`, `VNRecognizeTextRequest`, plus deterministic local synthesis |

### Complete `ext:ai` API Specification

| Category | Function / Method | Signature | Description |
| :--- | :--- | :--- | :--- |
| **Messages & Multimodal** | `ai.system` / `ai.user` / `ai.assistant` | `fn(content: str) -> Message` | Constructs a role-tagged chat `Message`. |
| **Messages & Multimodal** | `ai.user_with_file` | `fn(content: str, file_path: str) -> Message!` | Loads `file_path` into a MIME-typed `Attachment` (or extracts Markdown for Office/ODF/EPUB/text files) and attaches it to a user `Message`. |
| **Messages & Multimodal** | `ai.load_attachment` / `detect_mime_type` | `fn(path: str) -> Attachment!` / `fn(path: str, data: []byte = {}) -> str` | Inspects magic bytes (`%PDF-`, `\x89PNG`, `BM`, `\xFF\xD8\xFF`, `RIFF....WEBP`, ZIP/OOXML/ODF/EPUB) and loads a multimodal `Attachment`. |
| **Inference & Streaming** | `client.ask` / `client.ask_with_file` | `fn(model: str, prompt: str, ...) -> str!` | Single-turn text or multimodal file completion returning the assistant's text response. |
| **Inference & Streaming** | `client.chat` / `client.stream` | `fn(model: str, msgs: []Message) -> ChatResponse!` / `fn(model: str, msgs: []Message, on_delta: fn(str)) -> str!` | Multi-turn chat completion or real-time SSE token streaming (preserving full multi-turn history and multimodal attachments across OpenAI, Anthropic, Gemini, and Ollama). |
| **Tools & Structured Output** | `ai.tool_def[T]` / `client.chat_with_tools` | `fn[T](name: str, desc: str) -> Tool` / `fn(model: str, msgs: []Message, tools: []Tool) -> ChatResponse!` | Derives a tool's JSON Schema from struct `T` at compile time and returns model `ToolCall` invocations (`res.tool_calls`). |
| **Tools & Structured Output** | `client.generate_struct[T]` | `fn[T](model: str, prompt: str, sys: str = "") -> T!` | Constrains the LLM output to `json.schema[T]()` and deserializes the response directly into `T!`. |
| **Tools & Structured Output** | `client.generate_struct_from_file[T]` | `fn[T](model: str, prompt: str, file_path: str, sys: str = "") -> T!` | Extracts a strongly-typed struct `T` directly from an image, PDF, slide deck, spreadsheet, or document. |
| **Document Ingestion & RAG** | `ai.load_document` / `parse_document` | `fn(path: str) -> Document!` / `fn(data: []byte, filename: str) -> Document!` | Extracts clean Markdown (`doc.markdown`) and 1-based page/slide/sheet `sections` (`[]DocumentSection { index, kind, title, text }`) from `.pdf`, `.docx`, `.pptx`, `.xlsx`, `.odt`, `.odp`, `.ods`, `.epub`, `.html`, `.csv`, `.md`, or images. |
| **Document Ingestion & RAG** | `ai.chunk_document` / `chunk_text` | `fn(doc: Document, max_chars: i32 = 512, overlap: i32 = 64) -> []DocumentSection` | Splits a `Document` into overlapping chunks while preserving each chunk's originating page/slide `index` and `title`. |
| **Embeddings, OCR & NLP** | `ai.embed` / `client.embed` | `fn(model: str, text: str) -> []f32!` | Computes an L2-normalized `[]f32` text embedding (`model = "local"` runs 100% offline). |
| **Embeddings, OCR & NLP** | `ai.embed_image` / `client.embed_image` | `fn(model: str, path: str) -> []f32!` | Computes a 384-dim L2-normalized visual + semantic image embedding (`model = "local"` decodes PNG/BMP/PPM/JPEG pixels into 4x4 RGB/Luma moments + Sobel HOG + 64-bit dHash + OCR features). |
| **Embeddings, OCR & NLP** | `ai.ocr` / `ai.detect_language` | `fn(path: str) -> str!` / `fn(text: str) -> str` | Extracts text from images and identifies the dominant BCP-47 language tag (`"en"`, `"es"`, `"fr"`, `"de"`, `"pt"`, `"it"`, `"ja"`, `"ko"`, `"zh-Hans"`, `"ru"`, `"ar"`, `"hi"`). |
| **Vector Math & MCP** | `ai.cosine_similarity` / `l2_distance` / `dot_product` / `normalize` | `fn([]f32, []f32) -> f32` / `fn([]f32) -> []f32` | Hardware SIMD-accelerated (`ARM NEON`, `AVX2`, `WASM SIMD128`) vector operations. |
| **Vector Math & MCP** | `ai.make_mcp_server` / `register_tool[T]` / `handle_jsonrpc` / `serve_stdio` | `McpServer` methods | Exposes `cfire` functions as schema-validated Model Context Protocol tools over JSON-RPC 2.0. |

### Cross-Platform On-Device & Cloud Parity Matrix

| Capability | macOS & iOS (`__APPLE__`) | Linux (`__linux__`) | WebAssembly (`__EMSCRIPTEN__`) |
| :--- | :--- | :--- | :--- |
| **Office & E-Book (`.docx`, `.pptx`, `.xlsx`, `.odt`, `.odp`, `.ods`, `.epub`)** | Pure C++23 RFC 1951 `DEFLATE` + ZIP/XML parser (with automatic iOS `.app` bundle & `Documents/` sandbox path resolution) | Pure C++23 RFC 1951 `DEFLATE` + ZIP/XML parser | Pure C++23 RFC 1951 `DEFLATE` + ZIP/XML parser + browser drag-and-drop `MEMFS` staging (`window.cfireStageFile`) |
| **PDF (`.pdf`) Extraction** | `PDFKit.framework` (macOS) + pure C++23 `/FlateDecode`, `/ToUnicode` CMap & `TJ` kerning parser (iOS & fallback) | Auto-detected `pdftotext -layout` + pure C++23 `/FlateDecode`, `/ToUnicode` CMap & `TJ` kerning parser | Pure C++23 `/FlateDecode`, `/ToUnicode` CMap & `TJ` kerning parser |
| **Image OCR (`ai.ocr`)** | `Vision.framework` (`VNRecognizeTextRequest`) + PNG/JPEG metadata | Dynamic `dlopen("libtesseract.so.5")` C-API -> `tesseract` CLI -> `CFIRE_OCR_MODEL` + metadata | PNG `tEXt`/`iTXt` & JPEG `COM` metadata |
| **Local Visual Embeddings (`ai.embed_image("local", ...)`)** | Pure C++23 PNG/BMP/PPM pixel decoder + CoreGraphics JPEG/HEIC fallback -> 4x4 RGB/Luma + Sobel HOG + dHash + OCR | Pure C++23 PNG/BMP/PPM pixel decoder + ImageMagick/ffmpeg fallback -> 4x4 RGB/Luma + Sobel HOG + dHash + OCR | Pure C++23 PNG/BMP/PPM pixel decoder -> 4x4 RGB/Luma + Sobel HOG + dHash |
| **Language Detection (`ai.detect_language`)** | Apple `NLLanguageRecognizer` + Unicode script & stopword profile fallback | Unicode script block scanner (CJK, Hiragana/Katakana, Hangul, Cyrillic, Arabic, Devanagari) + Latin stopword/diacritic scorer | Unicode script block scanner + Latin stopword/diacritic scorer |
| **Cloud AI HTTPS Transport** | `libssl` (macOS) / `Foundation` `NSURLSession` native HTTPS (iOS & fallback) | `libssl` (`OpenSSL`) + `curl` fallback | Browser `emscripten_fetch` XHR with custom auth & Anthropic browser CORS headers |

### Level 1 — Tools & Structured Output (`ext:ai`)

```cfire
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
    // 1. Construct a schema-typed tool definition at compile time
    weather_tool := ai.tool_def[WeatherLookup]("get_weather", "Fetches current weather for a city")
    println("Tool '{}' schema has city: {}", weather_tool.name, weather_tool.parameters_schema.contains("\"city\""))

    // 2. Offline on-device client (swap with ai.openai(), ai.anthropic(), ai.gemini(), or ai.ollama())
    client := ai.apple()
    ticket := try client.generate_struct[IncidentTicket](
        "local",
        "Service=auth-gateway experienced p99 latency spike (severity 2)"
    )
    println("Structured ticket: service={}, severity={}", ticket.Service, ticket.Severity)

    // 3. Language detection across scripts
    println("Lang(FR)={}, Lang(JA)={}", ai.detect_language("Bonjour le monde, comment allez-vous"), ai.detect_language("これは日本語のテストです"))
}
```

**Line-by-Line Mechanics:**

1. `ai.tool_def[WeatherLookup](...)` inspects `WeatherLookup`'s `json` and `desc` field tags at compile time to build the exact JSON Schema required by OpenAI `tools`, Anthropic `input_schema`, and Gemini `functionDeclarations`.
2. `client.generate_struct[IncidentTicket](...)` attaches `json.schema[IncidentTicket]()` to the request and parses the returned JSON payload directly into an `IncidentTicket` struct.
3. `ai.detect_language(...)` returns the BCP-47 language code (`"fr"`, `"ja"`) on macOS, Linux, iOS, and WebAssembly alike.

#### Expected Output

```text
Tool 'get_weather' schema has city: true
Structured ticket: service=auth-gateway, severity=2
Lang(FR)=fr, Lang(JA)=ja
```

### Level 2 — Document Ingestion & Vector RAG

```cfire
import "ext:ai" as ai
import "ext:db/sqlite" as sqlite

type RagHit struct {
    Page:  i32 `db:"page_num"`
    Title: str `db:"title"`
    Score: f64 `db:"score"`
}

fn main() -> void! {
    // 1. Parse an in-memory Markdown/HTML/Office/PDF document into structured sections
    raw_doc := "# Storage Engine\nWrite-Ahead Logging (WAL) enables concurrent readers.\n\n# Network Layer\nRFC 6455 WebSockets provide full-duplex framing."
    doc     := try ai.parse_document(raw_doc.bytes(), "architecture.md")
    chunks  := ai.chunk_document(doc, 256, 32)

    // 2. Index chunks into an in-memory SQLite vector table
    mut db := try sqlite.open(":memory:")
    defer db.close()
    try db.exec("CREATE TABLE chunks (page_num INT, title TEXT, body TEXT, vec BLOB)")

    for ch in chunks {
        v := try ai.embed("local", ch.text)
        try db.exec("INSERT INTO chunks VALUES (?1, ?2, ?3, ?4)", ch.index, ch.title, ch.text, v)
    }

    // 3. Retrieve the closest section using SIMD vec_cosine_similarity
    qvec := try ai.embed("local", "concurrent database WAL readers")
    hits := try db.query[RagHit](
        "SELECT page_num, title, vec_cosine_similarity(vec, ?1) AS score FROM chunks ORDER BY score DESC LIMIT 1",
        qvec
    )
    println("Top RAG hit: section #{} '{}' (score > 0.2: {})", hits[0].Page, hits[0].Title, hits[0].Score > 0.2)
}
```

**Line-by-Line Mechanics:**

1. `ai.parse_document(...)` (and `ai.load_document(path)` for `.pdf`, `.pptx`, `.docx`, `.xlsx`, `.odt`, `.odp`, `.ods`, `.epub`) parses the file into a `Document` with 1-based `sections` (`DocumentSection { index, kind, title, text }`).
2. `ai.chunk_document(doc, 256, 32)` splits long sections into 256-character windows with 32-character overlap while preserving each chunk's `index` and `title` provenance.
3. `ai.embed("local", ...)` generates a 384-dimensional L2-normalized `[]f32` embedding vector that binds directly to SQLite's `?4` `BLOB` parameter and is ranked via `vec_cosine_similarity(vec, ?1)`.

#### Expected Output

```text
Top RAG hit: section #1 'Storage Engine' (score > 0.2: true)
```

### Level 3 — Model Context Protocol (`McpServer`)

```cfire
import "ext:ai" as ai

type SearchArgs struct {
    Query: str `json:"query" desc:"Search phrase"`
    Limit: i64 `json:"limit" desc:"Maximum results to return"`
}

fn main() {
    mut mcp := ai.make_mcp_server("cfire-kb-server", "1.0.0")

    mcp.register_tool[SearchArgs]("search_kb", "Searches the internal knowledge base", |args: SearchArgs| -> str! {
        return f"[{{\"query\":\"{args.Query}\",\"limit\":{args.Limit}}}]"
    })

    // Handle a JSON-RPC 2.0 tools/call request in memory (use mcp.serve_stdio() in production)
    req  := "{\"jsonrpc\":\"2.0\",\"id\":7,\"method\":\"tools/call\",\"params\":{\"name\":\"search_kb\",\"arguments\":{\"query\":\"SIMD\",\"limit\":3}}}"
    resp := mcp.handle_jsonrpc(req)
    println("MCP JSON-RPC response contains SIMD: {}", resp.contains("SIMD"))
}
```

**Line-by-Line Mechanics:**

1. `ai.make_mcp_server("cfire-kb-server", "1.0.0")` initializes a JSON-RPC 2.0 server implementing the Model Context Protocol (`initialize`, `tools/list`, `tools/call`, and `ping`).
2. `mcp.register_tool[SearchArgs](...)` derives the tool's JSON Schema from `SearchArgs` at compile time and automatically deserializes incoming `params.arguments` JSON into `args: SearchArgs` before invoking the handler closure.
3. Calling `mcp.serve_stdio()` runs the blocking `stdin`/`stdout` JSON-RPC loop ready for registration in Claude Desktop, Cursor, or any MCP host.

#### Expected Output

```text
MCP JSON-RPC response contains SIMD: true
```

---

## Chapter 18: Production Patterns & Examples

This chapter demonstrates how `cfire`'s standard library modules compose into a single, zero-dependency binary and provides a complete engineering guide to the 16 canonical projects in [`examples/`](file:///Users/memo/Programming/cfire/examples).

### Blueprint: RAG + HTTP/JSON Microservice

The following self-contained service combines `ext:db/sqlite` (WAL mode + SIMD vector search), `ext:ai` (document chunking + local embeddings), `ext:json` (compile-time struct serialization), and `ext:net/http` (pattern-matched HTTP router) into a single executable:

```cfire
import "ext:ai" as ai
import "ext:db/sqlite" as sqlite
import "ext:json" as json
import "ext:net/http" as http

type SearchRequest struct {
    Query: str `json:"query"`
    TopK:  i64 `json:"top_k"`
}

type SearchResult struct {
    Title: str `db:"title" json:"title"`
    Score: f64 `db:"score" json:"score"`
}

fn main() -> void! {
    // 1. Initialize in-memory SQLite WAL database and ingest knowledge base
    mut db := try sqlite.open(":memory:")
    defer db.close()
    try db.exec("CREATE TABLE kb (title TEXT, body TEXT, vec BLOB)")

    docs := [
        "Lock-Free Audio: ext:audio mixes 64 stereo f32 voices in real time.",
        "Zero-Cost Safety: cfire lowers T! and T? to std::expected and std::optional.",
        "TLS 1.3 & WebSockets: ext:net/http upgrades RFC 6455 connections natively."
    ]
    for d in docs {
        parts := d.split(": ")
        vec   := try ai.embed("local", d)
        try db.exec("INSERT INTO kb VALUES (?1, ?2, ?3)", parts[0], d, vec)
    }

    // 2. Attach an HTTP JSON endpoint querying SQLite SIMD vector similarity
    mut router := http.make_router()
    router.post("/v1/search", |req: http.Request| -> http.Response {
        q := json.parse[SearchRequest](req.body) or return http.Response.json(400, "{\"error\":\"invalid JSON\"}")
        qvec := ai.embed("local", q.Query) or return http.Response.json(500, "{\"error\":\"embed failed\"}")
        hits := db.query[SearchResult](
            "SELECT title, vec_cosine_similarity(vec, ?1) AS score FROM kb ORDER BY score DESC LIMIT ?2",
            qvec, q.TopK
        ) or return http.Response.json(500, "{\"error\":\"query failed\"}")
        return http.Response.json(200, json.stringify(hits))
    })

    // 3. Verify end-to-end execution via router.dispatch
    res := router.dispatch(http.Request{
        method: "POST",
        path:   "/v1/search",
        body:   "{\"query\":\"64 stereo voices mixer\",\"top_k\":1}"
    })
    parsed := try json.parse[[]SearchResult](res.body)
    println("HTTP {} -> top hit: '{}'", res.status, parsed[0].Title)
}
```

**Line-by-Line Mechanics:**

1. `SearchResult` uses dual reflection tags (`` `db:"title" json:"title"` ``) so the exact same struct maps out of SQLite (`db.query[SearchResult]`) and serializes directly to the HTTP response body (`json.stringify(hits)`).
2. Inside the `router.post("/v1/search", ...)` closure, `or return http.Response.json(...)` converts any JSON parse, embedding, or SQLite error into a structured HTTP status response without throwing exceptions.
3. `router.dispatch(...)` verifies the entire JSON -> Embedding -> SQLite SIMD Vector Search -> JSON pipeline in memory.

#### Expected Output

```text
HTTP 200 -> top hit: 'Lock-Free Audio'
```

### Canonical Projects Index (`examples/01`–`16`)

Every project in [`examples/`](file:///Users/memo/Programming/cfire/examples) is continuously verified by `make -C cfire test-examples` and can be run directly from the repository root:

| Directory | Title | Core Technologies Demonstrated | Run / Build Command |
| :--- | :--- | :--- | :--- |
| [`examples/01-hello-core`](file:///Users/memo/Programming/cfire/examples/01-hello-core) | Hello Core & Defer | Mutability (`mut`), parameter intents (`in`, `mut`, `out`, `move`), `defer` scope guards, string interpolation | `cfire run examples/01-hello-core/main.cf` |
| [`examples/02-log-compressor`](file:///Users/memo/Programming/cfire/examples/02-log-compressor) | Log Compressor | Declarative CLI flag parsing (`core:cli`), RFC 8878 Zstandard compression (`ext:compress/zstd`), `core:fs` | `cfire run examples/02-log-compressor/main.cf` |
| [`examples/03-pkg-cache`](file:///Users/memo/Programming/cfire/examples/03-pkg-cache) | Package Cache | SQLite3 WAL transactions (`ext:db/sqlite`), compile-time JSON struct mapping (`ext:json`), SHA-256 (`core:crypto/sha256`) | `cfire run examples/03-pkg-cache/main.cf` |
| [`examples/04-async-workers`](file:///Users/memo/Programming/cfire/examples/04-async-workers) | Async Workers | Scoped thread pool concurrency (`async.scope`), disjoint slice writes, MPMC `Channel[T]` (`core:sync`) | `cfire run examples/04-async-workers/main.cf` |
| [`examples/05-cmake-interop`](file:///Users/memo/Programming/cfire/examples/05-cmake-interop) | CMake Interop | Clang JSON AST ingestion (`import cpp "<zstd.h>"`), `build.CMake` native library orchestration | `cfire run examples/05-cmake-interop/main.cf` |
| [`examples/06-raylib-viz`](file:///Users/memo/Programming/cfire/examples/06-raylib-viz) | Raylib Visualization | Hardware 2D graphics loop (`vendor:raylib`), 60 FPS particle & telemetry rendering (`--web` & `--ios` ready) | `cfire run examples/06-raylib-viz/main.cf` |
| [`examples/07-types-match-map`](file:///Users/memo/Programming/cfire/examples/07-types-match-map) | Types & Match | Algebraic `enum` sum types (`std::variant`), exhaustive `match`, `map[K]V`, `interface` C++20 concepts | `cfire run examples/07-types-match-map/main.cf` |
| [`examples/08-multi-pkg-cc-build`](file:///Users/memo/Programming/cfire/examples/08-multi-pkg-cc-build) | Multi-Package Build | Multi-package `build.cf` orchestration, static archive compilation (`build.CC`) | `cfire run examples/08-multi-pkg-cc-build` |
| [`examples/09-stdlib-test-pointers`](file:///Users/memo/Programming/cfire/examples/09-stdlib-test-pointers) | Stdlib & Tests | Smart pointers (`*T`, `^T`), `test "..."` blocks, `core:testing` assertions | `cfire test examples/09-stdlib-test-pointers/main.cf` |
| [`examples/10-cmake-and-make-deps`](file:///Users/memo/Programming/cfire/examples/10-cmake-and-make-deps) | Multi-Build Deps | Simultaneous `build.CMake` and `build.Make` external dependency orchestration in a single `build.cf` | `cfire run examples/10-cmake-and-make-deps` |
| [`examples/11-http-server-router`](file:///Users/memo/Programming/cfire/examples/11-http-server-router) | HTTP Web Server | Multi-threaded HTTP/1.1 server (`ext:net/http`), fluent request router, URL `:param` & query extraction | `cfire run examples/11-http-server-router/main.cf` |
| [`examples/12-raygui-controls`](file:///Users/memo/Programming/cfire/examples/12-raygui-controls) | Raygui GUI Kit | Immediate-mode controls (`vendor:raygui`): checkboxes, sliders, progress bars, combo boxes, `&mut str` text boxes | `cfire run examples/12-raygui-controls/main.cf` |
| [`examples/13-ios-native-ui`](file:///Users/memo/Programming/cfire/examples/13-ios-native-ui) | Native iOS UI | Zero-coordinate `GuiIos*` auto-layout kit (`examples/13-ios-native-ui/ui/kit.cf`), safe-area insets, real-time `ext:audio` metering (`--ios`, `--web`, desktop) | `cfire run --ios examples/13-ios-native-ui/main.cf` |
| [`examples/14-ai-rag-sqlite`](file:///Users/memo/Programming/cfire/examples/14-ai-rag-sqlite) | AI Vector RAG | Local normalized embeddings (`ext:ai`), SQLite `vec_cosine_similarity` (`ext:db/sqlite`), compile-time `json.schema[T]()` | `cfire run examples/14-ai-rag-sqlite/main.cf` |
| [`examples/15-mcp-server`](file:///Users/memo/Programming/cfire/examples/15-mcp-server) | MCP Agent Server | Model Context Protocol server (`ai.make_mcp_server`), schema-typed `register_tool[T]`, JSON-RPC 2.0 dispatch | `cfire run examples/15-mcp-server/main.cf` |
| [`examples/16-audio-synth`](file:///Users/memo/Programming/cfire/examples/16-audio-synth) | Audio Synthesis | Procedural SFX (`audio.gen_sfx`), ADSR tone synthesis (`audio.gen_tone`), polyphonic mixer, RIFF/WAV file codec (`ext:audio`) | `cfire run examples/16-audio-synth/main.cf` |

### Further Reference & Module Index

- **Standard Library API Reference**: Browse [`reference.html`](file:///Users/memo/Programming/cfire/docs/reference.html) (or [`STDLIB_REFERENCE.md`](file:///Users/memo/Programming/cfire/docs/STDLIB_REFERENCE.md)) for every function signature, C++23 lowering expression, and diagnostic code in the compiler catalog.
- **LLM Context Snapshot**: Feed [`LLMS.txt`](file:///Users/memo/Programming/cfire/docs/LLMS.txt) or start `cfire mcp` to give coding agents full access to the compiler's type checker, formatter, and standard library catalog.

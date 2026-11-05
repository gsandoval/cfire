# Active Workbench: In-Flight Implementation Tasks

This document tracks items currently under active design, scoping, or implementation from [`docs/planning/backlog.md`](./backlog.md), [`docs/planning/architectural-backlog.md`](./architectural-backlog.md), and [`docs/planning/opportunity-tracker.md`](./opportunity-tracker.md).

---

## Completed Milestone: `WEBUI-01` — Native WebView & Two-Way RPC Application Framework (`WEBUI-P0-01` – `WEBUI-P0-06`)

| Task ID | Priority | Component | Summary | Status | Section Gate |
| :--- | :---: | :--- | :--- | :---: | :---: |
| `WEBUI-P0-01` | P0 | `runtime/core/webui.hpp`, `runtime/cfire_core_stdlib.hpp` | **Core RPC Router, Event Bus, Asset VFS (`app://localhost/`) & Wails/Tauri JS Preload Bridge**: Typed `app.bind[T](name) \|req\|`, `app.bind_action`, `app.on`, `app.emit`, `app.set_state`, `app.serve_dir`, `app.set_html`, and injected `window.backend.*` ES6 Proxy + `window.invoke` + `window.runtime.*` | `[DONE]` | Passed (`make review-check`) |
| `WEBUI-P0-02` | P0 | `runtime/core/webui.hpp` | **Native macOS (`Cocoa` + `WKWebView`) & iOS (`UIKit` + `WKWebView`) Backends**: Objective-C++ `WKURLSchemeHandler` (`app://localhost/`), `WKScriptMessageHandler`, `NSWindow` / `UIWindow` lifecycle, native dialogs, and `run_webview_test` offscreen `WKWebView` test runner | `[DONE]` | Passed (`make review-check`) |
| `WEBUI-P0-03` | P0 | `runtime/core/webui.hpp`, `tests/unit/test_stdlib.cpp` | **Native Linux (`GTK3` + `WebKitGTK`) Backend & Mock-Header Verification**: `webkit_web_context_register_uri_scheme("app")`, `WebKitUserContentManager` script message handler, `g_idle_add_full` thread-safe dispatch, headless CI fallback, and mock-header compile test | `[DONE]` | Passed (`make review-check`) |
| `WEBUI-P0-04` | P0 | `src/codegen.hpp`, `src/buildsys.hpp`, `tests/unit/test_sema.cpp` | **Codegen & Build System Integration**: Auto-enable `json` struct reflection for `ext:webui`, `.mm` Objective-C++ compilation (`plan.uses_objc`, skipping C++-only PCH), macOS/iOS/Linux framework & `pkg-config` flags, and `./ui` bundle copying | `[DONE]` | Passed (`make review-check`) |
| `WEBUI-P0-05` | P0 | `src/main.cpp`, `examples/17-webui-studio/`, `Makefile`, `tests/e2e/test_canonical.cpp` | **CLI Scaffolding (`cfire init --webui`) & Canonical Studio Example (`17-webui-studio`)**: Project generator (`--template webui` / `--webui`) and interactive multi-channel studio mixer example with unit, doc, fuzz, bench, and live `WKWebView` tests | `[DONE]` | Passed (`make review-check`) |
| `WEBUI-P0-06` | P0 | `src/docgen/catalog.hpp`, `src/docgen/markdown.hpp`, `src/lsp/json_rpc.hpp`, `src/lsp/server.hpp`, `src/lsp/ide_setup.hpp`, `src/mcp.hpp`, `docs/GUIDE.md`, `tests/unit/test_frontend.cpp` | **Stdlib Catalog, LSP, MCP & Docs Sync + Cross-Platform Validation**: Register `ext:webui` in catalog (`stdlib_catalog()`, `discover_runtime_modules()`, `module_practical_example()`), add LSP `webui.App` & `\|req\|` closure type inference + unit tests, update MCP tools & `GUIDE.md`, and pass `make review-check` + `--target ios-sim` build | `[DONE]` | Passed (`make review-check`) |

### Pre-Implementation Quality Gate (`WEBUI-01`)
1. **Scope & Problem Statement**:
   - Provide a first-class, zero-bloat WebView application framework (`import "ext:webui"`) enabling developers to build desktop (macOS, Linux) and mobile (iOS) applications with HTML5/CSS3/JS UIs backed by typed `cfire` functions, structs, and real-time events.
   - **Hard Constraints**:
     - Do **not** extend `cfire` at the core language level (`src/lexer.hpp`, `src/parser.hpp`, `src/ast.hpp`, and core `src/sema.hpp` rules remain untouched). Use `cfire`'s existing generic method call (`app.bind[ReqStruct]("name") |req| { ... }`) and trailing closure syntax.
     - Do **not** add an `ext:wails` package alias—the package is strictly `import "ext:webui"`.
     - Synchronize **LSP** (`src/lsp/json_rpc.hpp`, `src/lsp/server.hpp`, `src/lsp/ide_setup.hpp`), **MCP** (`src/mcp.hpp`), and **Docs** (`src/docgen/catalog.hpp`, `src/docgen/markdown.hpp`, `docs/GUIDE.md`) completely so `./cfire doc --check` and `make test-lsp` pass 100%.
2. **Resolved Architectural & Ergonomic Decisions**:
   - **Why Not Link Go's Wails Binary Directly**: Wails is written in Go (`cgo` + Go runtime/GC), which adds ~20 MB per binary and does not support iOS. Instead, we implement the native C/Objective-C++ WebView layer (`WKWebView` on macOS/iOS, `WebKitGTK` on Linux, matching `webview/webview` and Tauri's `wry`) with Wails v2/v3 + Tauri v2's exact JS and backend ergonomics.
   - **Frontend JS Ergonomics**: Standard Wails (`await window.backend.<method>(args)` via ES6 `Proxy`, `window.runtime.EventsOn`, `window.runtime.EventsEmit`, `window.runtime.WindowSetTitle`, `window.runtime.Quit`, CSS `--wails-draggable: drag`) **and** Tauri (`await window.invoke("method", args)`).
   - **Custom URI Scheme (`app://localhost/`)**: Served via `WKURLSchemeHandler` (macOS/iOS) and `webkit_web_context_register_uri_scheme` (Linux) for a secure context (`window.isSecureContext === true`) with zero open TCP ports.
3. **Testing & Verification Matrix**:
   - **Unit & Mock-Header Tests (`tests/unit/test_stdlib.cpp`)**: Headless RPC (`app.call_rpc`), event bus (`app.dispatch_event`, `app.emit`), asset VFS (`app://localhost/`), and Linux `WebKitGTK` mock-header compile & callback verification.
   - **LSP Verification (`tests/unit/test_lsp.cpp`)**: Verify `webui.` module completions, `app.` handle method completions (`bind`, `bind_action`, `on`, `emit`, `set_html`, `serve_dir`, `run`), and typed struct field completions on `req.` inside `app.bind[TrackMix]("...") |req| { ... }`.
   - **Live macOS `WKWebView` Integration Test (`examples/17-webui-studio/main.cf`)**: Real `WKWebView` boot via `app.run_webview_test(3000)` executing JS `await window.backend.*`, `await window.invoke(...)`, `window.runtime.EventsEmit(...)`, and `window.runtime.EventsOn(...)` inside WebKit.
   - **iOS Simulator Cross-Compilation (`--target ios-sim`)**: `./cfire build --target ios-sim examples/17-webui-studio/main.cf` verifying `UIKit` + `WKWebView` compilation and `.app` bundle creation.
   - **Regression Gate**: `make review-check` must pass 100% with 0 compiler warnings, 0 formatting diffs, and 0 doc diffs.

---

## Completed Milestone: `SAFE-01` — Memory Safety, Use-After-Free & Bounds Verification (`SAFE-P0-01` – `SAFE-P0-03`)

| Task ID | Priority | Component | Summary | Status | Section Gate |
| :--- | :---: | :--- | :--- | :---: | :---: |
| `SAFE-P0-01` | P0 | `src/sema.hpp` | **Compile-Time Loan/Origin Use-After-Free (`cfire/USE-AFTER-FREE`), Lexical Scope Escape (`cfire/DANGLING-POINTER`, `cfire/DANGLING-RETURN`), and Loop Iterator Invalidation (`cfire/ITERATOR-INVALIDATION`)** | `[DONE]` | Passed (`make review-check`) |
| `SAFE-P0-02` | P0 | `src/sema.hpp` | **Zero-Overhead Flow-Sensitive Null-Pointer Verification (`cfire/NULL-DEREF`) & Constant Index Bounds Check (`cfire/INDEX-OUT-OF-BOUNDS`)** | `[DONE]` | Passed (`make review-check`) |
| `SAFE-P0-03` | P0 | `runtime/cfire_prelude.hpp`, `src/codegen.hpp`, `tests/` | **Inline Dynamic Bounds-Checked Indexing (`cfire::index_at`) & Conformance Suite** | `[DONE]` | Passed (`make review-check`) |

---

## Completed Milestone: `OPP-01` — Native iOS & Simulator Target (`--target ios-sim` / `ios` & `cfire run --ios`)

| Task ID | Priority | Component | Summary | Status | Section Gate |
| :--- | :---: | :--- | :--- | :---: | :---: |
| `IOS-P0-01` | P0 | `src/buildsys.hpp`, `src/main.cpp` | **iOS Toolchain, Flat `.app` Bundle Packager & `cfire run --ios` Runner**: Support `--target ios-sim` (`arm64-apple-ios17.0-simulator`) and `--target ios` (`arm64-apple-ios17.0`), dynamic `xcrun --sdk` sysroot resolution, flat iOS `.app/Info.plist` generation + `codesign`, and `cfire run --ios` (`xcrun simctl boot/install/launch --console-pty`) | `[DONE]` | Passed (`make review-check`) |
| `IOS-P0-02` | P0 | `runtime/core/ios_raylib.hpp`, `runtime/vendor/raygui.h`, `src/codegen.hpp` | **Built-In Objective-C++ `UIKit` + `OpenGLES3` + `CoreAudio` Backend & Zero-Coordinate `GuiIos*` Auto-Layout Kit**: Self-contained iOS Raylib/Raygui engine with edge-to-edge Safe-Area rendering, true rounded-corner geometry, `rg.GuiIos*` auto-layout controls (navigation header, grouped cards, pill switches, segmented controls, auto-focusing text fields, tab bar), and `<UIKeyInput>` software keyboard | `[DONE]` | Passed (`make review-check`) |
| `IOS-P0-03` | P0 | `runtime/core/fs.hpp`, `runtime/core/sqlite.hpp`, `runtime/core/json.hpp`, `runtime/core/compress.hpp`, `src/codegen.hpp` | **Transparent iOS Sandbox File/Database Path Resolution & Header-Only JSON/Compression Fallbacks**: Auto-map relative writable paths (`core:fs`, `core:db/sqlite`) to iOS `~/Documents/`, check `.app` bundle first for relative reads, link iOS SDK `-lsqlite3` and `-lcompression`, and support header-only C++23 `json::parse<T>` on iOS | `[DONE]` | Passed (`make review-check`) |
| `IOS-P0-04` | P0 | `examples/13-ios-native-ui/main.cf`, `tests/e2e/test_canonical.cpp`, `docs/GUIDE.md` | **Canonical iOS UI Showcase (`13-ios-native-ui`), Live Simulator Validation & Documentation Sync**: Verify `cfire run --ios examples/13-ios-native-ui/main.cf` live on iPhone Simulator, add automated e2e tests in `test_canonical.cpp`, and sync `docs/` | `[DONE]` | Passed (`make review-check`) |

### Pre-Implementation Quality Gate (`OPP-01`)
1. **Scope & Problem Statement**:
   - Enable `cfire` to compile and run `.cf` applications (both standard library CLI/database apps and graphical `vendor:raylib` / `vendor:raygui` apps) natively on the iOS Simulator (`--target ios-sim`, `cfire run --ios`) and physical iOS devices (`--target ios`) with zero Xcode project creation or external SDL2 dependencies.
2. **Resolved User Decisions (`/grill-me`)**:
   - **Backend**: Built-in Objective-C++ `UIKit` + `OpenGLES3` + `CoreAudio` backend (`runtime/vendor/raylib_ios.hpp`) compiled with `-x objective-c++ -fobjc-arc`.
   - **Viewport & Orientation**: Smart Adaptive Viewport (`InitWindow(0, 0)` uses native iPhone points; fixed `InitWindow(w, h)` scales/letterboxes inside the iOS Safe Area, remaps touch coordinates into `w x h`, and sets preferred Landscape/Portrait orientation in `Info.plist`).
   - **CLI**: `cfire build --target ios-sim | ios` and `cfire run --ios` (or `--ios-sim`, plus optional `--device <name>`).
   - **Keyboard**: `UIView<UIKeyInput>` automatically shows/hides the iOS software keyboard when `GuiTextBox` enters/exits `editMode`.
   - **iOS Sandbox & Stdlib**: Relative writable paths in `core:fs` and `core:db/sqlite` map to `~/Documents/`; relative reads check the `.app` bundle first then `~/Documents/`; iOS builds link the iOS SDK's `-lsqlite3` and use header-only C++23 JSON struct reflection.
3. **Testing & Verification Matrix**:
   - **Unit/Integration Tests**: Verify `analyze_and_emit` and `build_native` for `ios-sim` target triple, flat `.app/Info.plist` generation, and `core:fs` / `core:json` compilation against `iphonesimulator` SDK.
   - **Live Simulator Validation**: Compile and launch `examples/06-raylib-viz/main.cf` and `examples/12-raygui-controls/main.cf` on the booted iPhone Simulator via `./cfire run --ios`.
   - **Regression Suite**: `make review-check` must pass 100% with 0 compiler warnings.

---

## Completed Milestones: Systems & GUI Application Ergonomics (`LANG-*`, `STDLIB-*`)

| Task ID | Priority | Component | Summary | Status | Section Gate |
| :--- | :---: | :--- | :--- | :---: | :---: |
| `LANG-P0-01` | P0 | `runtime/cfire_prelude.hpp` | **Built-in numeric conversion constructors**: Provide `i8`, `i16`, `i32`, `i64`, `u8`, `byte`, `u16`, `u32`, `u64`, `usize`, `isize`, `f32`, `f64`, `bool_` constructor functions in prelude | `[DONE]` | Passed (`make review-check`) |
| `STDLIB-P1-04` | P1 | `runtime/core/str.hpp`, `src/docgen/catalog.hpp`, `tests/unit/test_stdlib.cpp` | **Substring search primitives in `core:str`**: Implement `index_of`, `last_index_of`, and `rfind` returning `i64` (-1 if not found) with unit & doc tests | `[DONE]` | Passed (`make review-check`) |
| `LANG-P1-03` | P1 | `runtime/cfire_prelude.hpp` | **Bitwise operator overloads for imported vendor/C enums**: Provide generic C++20 concept-constrained bitwise operators (`\|`, `&`, `^`, `~`, `\|=`, `&=`, `^=`) and `std::formatter` for enums | `[DONE]` | Passed (`make review-check`) |
| `LANG-P1-02` | P1 | `runtime/core/str.hpp`, `src/codegen.hpp`, `src/sema.hpp`, `src/docgen/catalog.hpp` | **Pointer offsets, decays, and C FFI interop for `str` & slices**: `str.byte_ptr()`, `str.byte_ptr_at(offset)`, `s.byte_ptr()`, `s.byte_ptr_at(offset)`, raw pointer arithmetic/decay, and `*c_char` / `*mut c_char` type mapping | `[DONE]` | Passed (`make review-check`) |
| `LANG-P2-05` | P2 | `src/parser.hpp`, `src/codegen.hpp`, `src/sema.hpp` | **Rebindable pointer / reference aliases (`*mut T`)**: Disambiguate binary operators from newline-prefixed unary dereference statements (`*ptr = ...`), allow re-assigning pointer variables (`target = &mut ...`), and support dereferenced read/write (`*target += ...`) | `[DONE]` | Passed (`make review-check`) |
| `LANG-P1-06` | P1 | `src/codegen.hpp`, `src/docgen/catalog.hpp`, `tests/e2e/test_canonical.cpp` | **Slice and Vector `.data()` / `.data_ptr()` buffer pointer access**: Support `.data()` and `.data_ptr()` on slices and vectors lowering to `({}).data()`, eliminating `cpp {}` escapes in C FFI calls | `[DONE]` | Passed (`make review-check`) |

---

## Completed Milestones Summary

| Task ID | Section / Priority | Component | Summary | Status | Section Gate |
| :--- | :---: | :--- | :--- | :---: | :---: |
| `NET-P0-01` | Section 1 (P0) | `runtime/core/net/tcp.hpp`, `runtime/core/net/url.hpp`, `runtime/core/net/websocket.hpp` | **Socket Timeout Fix, `INADDR_ANY` Binding & `wss://` URL Port Default**: Clear 2s `SO_RCVTIMEO` on `websocket::Conn` init, add `set_timeout_ms`, bind `listen_tcp` to `INADDR_ANY`, and default `wss://` to port `443` | `[DONE]` | Passed (`make review-check`) |
| `NET-P0-02` | Section 1 (P0) | `runtime/core/net/http.hpp`, `runtime/core/net/websocket.hpp` | **First-Class WebSocket Server (`websocket::Server`) & `http::Router::ws` Route Upgrade**: High-level `websocket.make_server(port)` and `router.ws(pattern, handler)` upgrade dispatch in `HttpServer` | `[DONE]` | Passed (`make review-check`) |
| `NET-P0-03` | Section 1 (P0) | `runtime/core/net/tls.hpp`, `runtime/core/net/tcp.hpp`, `runtime/core/net.hpp` | **TLS / X.509 Certificate Engine (`core:net/tls`) via OpenSSL Dynamic Bridge**: Zero-build-flag `<dlfcn.h>` OpenSSL 3 loader, `tls.wrap_client` SNI + X.509 verification, `tls.ServerContext`, and `tls.generate_self_signed_pem` | `[DONE]` | Passed (`make review-check`) |
| `NET-P0-04` | Section 1 (P0) | `runtime/core/net/http.hpp`, `runtime/core/net/websocket.hpp`, `src/codegen.hpp`, `src/docgen/catalog.hpp` | **Unified `https://` & `wss://` Client/Server Support & Catalog Registration**: Automatic TLS in `http.get`/`post`/`request` and `websocket.connect`/`connect_insecure`, `make_tls_server*`, and `core:net/tls` catalog sync | `[DONE]` | Passed (`make review-check`) |
| `SEC-P0-01` | Section 2 (P0) | `runtime/core/net/*`, `runtime/core/json.hpp`, `runtime/core/compress.hpp`, `src/parser.hpp` | **Untrusted Input Parser Security Hardening**: WebSocket frame size cap + chunked read, HTTP CRLF stripping + header/query caps, `%XX` hex validation, JSON/AST recursion depth limits, and Zstd decompression bomb limit | `[DONE]` | Passed (`make fuzz`) |
| `SEC-P0-02` | Section 2 (P0) | `tests/unit/test_stdlib.cpp`, `tests/test_main.cpp`, `Makefile` | **Deterministic Mutation Fuzzer & Adversarial Security Suite (`make fuzz`)**: 10,000+ mutated/adversarial inputs across WebSocket, HTTP, URL, JSON, Base64/Hex/CSV, UTF-8, and `.cf` Lexer/Parser/LSP | `[DONE]` | Passed (`make fuzz`) |
| `SEC-P0-03` | Section 2 (P0) | `tests/unit/test_stdlib.cpp`, `tests/test_main.cpp`, `Makefile` | **Built-In Performance Benchmark Suite (`make bench`)**: Latency (`ns/op`) and throughput (`ops/sec`, `MB/s`) benchmarks for HTTP, WebSocket, TLS, JSON, Crypto, and Compiler Frontend | `[DONE]` | Passed (`make bench`) |

---

## Validation Checklist (Four Gates)

- [x] **Gate 1 — Zero Compiler Warnings (Strict Mode)**: `clang++ -std=c++2b -Wall -Wextra -Wpedantic -Wshadow -Wno-parentheses-equality -Iruntime -Isrc src/main.cpp` compiles with 0 warnings.
- [x] **Gate 2 — Formatting Invariance**: `./cfire fmt --check examples/` passes cleanly.
- [x] **Gate 3 — Public Documentation Invariance**: `./cfire doc --check` passes and `git diff docs/reference.html docs/STDLIB_REFERENCE.md docs/index.html docs/guide.html` is empty.
- [x] **Gate 4 — Full Conformance Suite**: `make review-check` (all unit, stdlib, and e2e test suites + `cfire fmt --check`) passes 100%.

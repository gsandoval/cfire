# Active Workbench: In-Flight Implementation Tasks

This document tracks items currently under active design, scoping, or implementation from [`docs/planning/backlog.md`](./backlog.md), [`docs/planning/architectural-backlog.md`](./architectural-backlog.md), and [`docs/planning/opportunity-tracker.md`](./opportunity-tracker.md).

---

## Completed Milestone: `OPP-01` — Native iOS & Simulator Target (`--target ios-sim` / `ios` & `cfire run --ios`)

| Task ID | Priority | Component | Summary | Status | Section Gate |
| :--- | :---: | :--- | :--- | :---: | :---: |
| `IOS-P0-01` | P0 | `src/buildsys.hpp`, `src/main.cpp` | **iOS Toolchain, Flat `.app` Bundle Packager & `cfire run --ios` Runner**: Support `--target ios-sim` (`arm64-apple-ios17.0-simulator`) and `--target ios` (`arm64-apple-ios17.0`), dynamic `xcrun --sdk` sysroot resolution, flat iOS `.app/Info.plist` generation + `codesign`, and `cfire run --ios` (`xcrun simctl boot/install/launch --console-pty`) | `[DONE]` | Passed (`make review-check`) |
| `IOS-P0-02` | P0 | `runtime/core/ios_raylib.hpp`, `runtime/vendor/raygui.h`, `src/codegen.hpp` | **Built-In Objective-C++ `UIKit` + `OpenGLES3` + `CoreAudio` Backend for `vendor:raylib` & `vendor:raygui`**: Self-contained iOS Raylib/Raygui engine with blocking `for !rl.WindowShouldClose()` `CFRunLoop` pump, Smart Adaptive Viewport inside Safe Area, touch-to-mouse remapping, and `<UIKeyInput>` software keyboard trigger on `GuiTextBox` | `[DONE]` | Passed (`make review-check`) |
| `IOS-P0-03` | P0 | `runtime/core/fs.hpp`, `runtime/core/sqlite.hpp`, `runtime/core/json.hpp`, `runtime/core/compress.hpp`, `src/codegen.hpp` | **Transparent iOS Sandbox File/Database Path Resolution & Header-Only JSON/Compression Fallbacks**: Auto-map relative writable paths (`core:fs`, `core:db/sqlite`) to iOS `~/Documents/`, check `.app` bundle first for relative reads, link iOS SDK `-lsqlite3` and `-lcompression`, and support header-only C++23 `json::parse<T>` on iOS | `[DONE]` | Passed (`make review-check`) |
| `IOS-P0-04` | P0 | `tests/e2e/test_canonical.cpp`, `docs/GUIDE.md` | **Unit/Integration Verification, Live Simulator Validation & Documentation Sync**: Verify `cfire build --target ios-sim` and `cfire run --ios` on `examples/06-raylib-viz` and `examples/12-raygui-controls`, add unit tests for iOS build plan generation, and sync `docs/` | `[DONE]` | Passed (`make review-check`) |

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

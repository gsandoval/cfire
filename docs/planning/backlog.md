# Project Backlog

This document tracks the interactively triaged and architecturally audited backlog for `cfire` (`CF-001` – `CF-085`).

---

## 1. Selected Backlog (Active Scope)

### Phase 1: P0 Security, Storage Integrity & Uniform Library Diagnostics

| ID | Priority | Component | Summary | Status |
| :--- | :---: | :--- | :--- | :---: |
| `CF-006` / `CF-070` | **P0** | `src/module_registry.hpp`, `src/sema.hpp`, `src/buildsys.hpp`, `runtime/core/sodium.hpp` | **Strict `ext:crypto/sodium` + Hard `libsodium` Requirement**: Make `sodium` strictly `ext:crypto/sodium` (reject `core:crypto/sodium`), expose `sodium.blake2b` & `sodium.derive_subkey`, remove the pure-C++ fallback from `sodium.hpp`, and fail hard with a clear diagnostic at build time if `libsodium` is not installed. | `[DONE]` |
| `CF-007` / `CF-069` | **P0** | `runtime/core/sqlite.hpp`, `src/buildsys.hpp` | **Fail-Closed `sqlite.open_encrypted` & `sqlcipher` Detection**: Auto-detect `sqlcipher` via `pkg-config`/system paths in `buildsys.hpp` and verify `PRAGMA cipher_version;` in `apply_key()` / `open_encrypted()` so it returns an explicit `Err` instead of silently writing plaintext when linked against unencrypted `libsqlite3`. | `[DONE]` |
| `CF-008` / `CF-071` | **P0** | `runtime/core/keychain.hpp` | **Linux `secret-tool lookup`, Error Checking & No Plaintext Cache**: Implement `secret-tool lookup` in `keychain::get` on Linux, check `SecItemAdd` `OSStatus` on macOS and `pclose` status on Linux, and restrict `memory_fallback_store()` strictly to `CFIRE_TEST_MODE`. | `[DONE]` |
| `CF-075` | **P1** | `src/buildsys.hpp`, `src/doctor.hpp` | **Uniform Library Discovery (`~/.local`) & Version Diagnostics (No Network Auto-Download)**: Treat all `core:` / `ext:` C/C++ libraries uniformly — never auto-download during `cfire build`; search `~/.local/{include,lib}` alongside system prefixes and emit clear diagnostics in `cfire build` and `cfire doctor` when a required library is missing or incompatible (e.g., `simdjson < 4.0` with `clang >= 19`). | `[DONE]` |

### Phase 2: P1 Compiler, Sema & Codegen Fixes (Orthogonality-Preserving)

| ID | Priority | Component | Summary | Status |
| :--- | :---: | :--- | :--- | :---: |
| `CF-059` / `CF-063` | **P1** | `src/sema.hpp` | **Fix `os.getenv` False `NULL-DEREF` & Reject `str.find` in Favor of `str.index_of`**: Remove `os.getenv` and `find`/`rfind` from `expr_returns_optional_sema`, and reject `.find()` / `.rfind()` on `str` in Sema with a diagnostic + fix-it pointing to the single canonical `s.index_of(sub)` / `s.last_index_of(sub)`. | `[DONE]` |
| `CF-062` | **P1** | `src/sema.hpp` | **`[]byte` Element Type Diagnostic**: Report a compile-time `cfire/TYPE-MISMATCH` diagnostic (with `byte(...)` fix-it) when integer literals or `u8` values are stored into `[]byte` literals or indexed assignments. | `[DONE]` |
| `CF-017` / `CF-034` / `CF-060` | **P1** | `src/codegen.hpp`, `src/codegen/expr.hpp`, `src/build/package_loader.hpp`, `src/sema.hpp` | **String Interpolation Lowering, Zero-Copy `str` Slice Diagnostics & Cross-Package Mangling**: Keep `a[0..5]` on `str` as zero-copy `std::string_view` (emitting a Sema hint for `&str` or `str(a[0..5])` when passed to an owning `str` param), lower `{...}` interpolation expressions (including `p.helper()`, `a[0..5]`, and nested calls) through `Emitter::emit_expr`, and consistently mangle non-`main` package functions and types (`struct`/`enum`) across packages. | `[DONE]` |
| `CF-057` | **P1** | `src/codegen/reflect.hpp` | **Honor `` `default:"..."` `` Tags in `ext:json`**: Apply field `default_val` tags in generated `cfire::json::parse_struct<T>` when a JSON key is missing or `null`. | `[DONE]` |
| `CF-066` | **P1** | `src/codegen/reflect.hpp` | **Guard JSON Struct Codegen on Field Serializability**: Skip emitting `json::parse_struct` / `stringify_struct` for structs containing non-serializable runtime handles (`webui.App`, `sync.Mutex`, `sqlite.Conn`, etc.). | `[DONE]` |
| `CF-067` | **P1** | `runtime/cfire_prelude.hpp` | **Support Braced Slice Literals in `vec_push` (`rows.push(["a", "b"])`)**: Add `template <typename T> void vec_push(std::vector<T>& v, T val)` overload so braced initializer lists deduce `T`. | `[DONE]` |
| `CF-068` | **P1** | `src/ast.hpp`, `src/parser.hpp`, `src/build/package_loader.hpp`, `src/codegen.hpp` | **Per-Declaration Source File Tracking in `#line` Directives**: Preserve each AST declaration's originating `source_path` across package merges and emit `#line <line> "<source_path>"`. | `[DONE]` |
| `CF-076` | **P1** | `src/clang_bridge.hpp` | **Suppress Partial AST JSON Dump on `import cpp` Error**: Filter out JSON AST lines when `clang++ -Xclang -ast-dump=json` exits non-zero so only Clang diagnostics are displayed. | `[DONE]` |
| `CF-078` | **P1** | `src/parser.hpp` | **Negative Numeric Literals in Struct Field Defaults**: Support unary `-` before integer and float literals in struct field default values (`a: i32 = -1`). | `[DONE]` |
| `CF-085` | **P1** | `src/sema.hpp`, `src/codegen.hpp`, `src/codegen/expr.hpp` | **`shared[T]` / `own[T]` Auto-Deref on `.field` & `.method()` (Keep `make[shared[T]]` Canonical)**: Keep `make[shared[T]](...)` as the sole constructor (emitting a Sema diagnostic with fix-it if `shared(v)` is written), and auto-dereference `shared[T]` / `own[T]` on member accesses and UFCS method calls. | `[DONE]` |

### Phase 3: P1 Build System, Installer, Test Runner & IDE Tooling

| ID | Priority | Component | Summary | Status |
| :--- | :---: | :--- | :--- | :---: |
| `CF-011` / `CF-072` | **P1** | `docs/install.sh`, `Makefile`, `src/doctor.hpp` | **Atomic Binary Install on macOS & Version/Tag/SHA Support**: Install `cfire` via temp file + `mv -f` in `docs/install.sh` (preventing macOS code-signature cache `SIGKILL`), support tags/SHAs in `CFIRE_VERSION`, and pass `GIT_SHA` into `make`. | `[DONE]` |
| `CF-018` / `CF-080` | **P1** | `src/codegen/test_runner.hpp` | **Skip FD-Leak Canary on Foreign `import cpp` FFI & Pre-Warm `libsodium`**: Automatically exempt test suites that use `import cpp` from the FD-leak canary (since foreign C libraries open process-lifetime FDs on first call), and pre-warm `sodium_init()` when `ext:crypto/sodium` is imported. | `[DONE]` |
| `CF-031` | **P1** | `src/buildsys.hpp` | **Inherit `build.cf` `Frameworks` & `Libraries` in `cfire test`**: Discover the nearest ancestor `build.cf` during `cfire test` and apply its `Frameworks`, `Libraries`, and `external_deps`. | `[DONE]` |
| `CF-058` | **P1** | `src/main.cpp`, `src/buildsys.hpp` | **Target Filtering in `cfire build <Name>`**: When `<Name>` matches a declared `build.Executable` in `build.cf`, build only that target (and report an error if `<Name>` matches neither a file nor a declared target). | `[DONE]` |
| `CF-074` | **P1** | `src/lsp/ide_setup.hpp`, `.claude-plugin/marketplace.json` | **Portable `cfire` Command in `.mcp.json` & Claude Code LSP Plugin Setup**: Write `"cfire"` instead of machine-specific absolute paths in `.mcp.json` and `.claude/rules/cfire.md`, ship `.claude-plugin/marketplace.json` + LSP plugin manifest, remove duplicate MCP from `plugin.json`, and update `cfire ide claude` / `cfire ide --status`. | `[DONE]` |

### Phase 4: P1 Desktop `ext:webui` & System Tray Enhancements

| ID | Priority | Component | Summary | Status |
| :--- | :---: | :--- | :--- | :---: |
| `CF-045` | **P1** | `runtime/core/webui/macos_ios.hpp` | **Return CSS Font Family Names from `monospace_fonts()` on macOS**: Resolve `[NSFont fontWithName:size:] familyName` so `app.monospace_fonts()` returns deduplicated CSS family names (`Menlo`) instead of PostScript face names (`Menlo-Regular`). | `[DONE]` |
| `CF-053` | **P1** | `runtime/core/webui.hpp`, `runtime/core/webui/linux_gtk.hpp` | **Hardened `clipboard_set_secret`**: Do not store concealed secrets in `state_->state["__clipboard"]`, set `x-kde-passwordManagerHint` (`"secret"`) on Linux, and dispatch delayed clipboard clearing on the GTK main loop (`g_timeout_add`) only if the clipboard generation matches. | `[DONE]` |
| `CF-077` | **P1** | `runtime/core/webui.hpp`, `runtime/core/webui/linux_gtk.hpp`, `runtime/core/webui/windows_webview2.hpp` | **Native Linux GTK 3 & Win32 File/Message Dialogs**: Implement `open_file_dialog`, `save_file_dialog`, and `message_dialog` on Linux using `GtkFileChooserDialog` / `GtkMessageDialog` (and Win32 dialogs on Windows). | `[DONE]` |
| `CF-082` | **P1** | `runtime/core/webui.hpp`, `runtime/core/webui/bridge.hpp`, `runtime/core/webui/macos_ios.hpp`, `runtime/core/webui/linux_gtk.hpp`, `runtime/core/webui/windows_webview2.hpp`, `src/docgen/catalog_types.hpp` | **System Tray / Menu Bar Status Item & Window Hide/Show**: Add `app.set_tray(icon_path, tooltip)`, `app.add_tray_item(title, event)`, `app.hide_window()`, `app.show_window()`, and `app.set_hide_on_close(enabled: bool)` across macOS (`NSStatusBar` / `NSStatusItem`), Linux, and Windows (`Shell_NotifyIconW`). | `[DONE]` |

### Phase 5: P2 Concurrency, Stdlib & Windows Core Runtime Fixes

| ID | Priority | Component | Summary | Status |
| :--- | :---: | :--- | :--- | :---: |
| `CF-083` | **P2** | `runtime/cfire_prelude.hpp` | **Copyable `cfire::async::Scope` Handle for Nested `scope.spawn`**: Back `cfire::async::Scope` with a shared `State` block so tasks spawned via `scope.spawn` can capture `scope` by value and call `scope.spawn` recursively, while `with_scope` joins all transitively spawned tasks. | `[DONE]` |
| `CF-026` | **P2** | `runtime/core/sync.hpp` | **Fix `ch.len()` Compilation on `sync.Channel[T]`**: Add `size()` alongside `len()` on `cfire::sync::Channel<T>` so `cfire::len(ch)` compiles cleanly. | `[DONE]` |
| `CF-042` | **P2** | `runtime/core/os.hpp` | **Restore `os.RawTerminal` on Process Exit (`std::atexit`)**: Track active raw terminal `termios` state and register an `atexit` hook so `os.exit()` restores the terminal cleanly. | `[DONE]` |
| `CF-054` | **P2** | `src/docgen/catalog_types.hpp`, `src/sema.hpp` | **Fix `fs.write_all` Docs & Document `fs.rename`**: Remove the inaccurate "atomically" claim from `fs.write_all` documentation and add `fs.rename(from: str, to: str) -> void!` to the `core:fs` reference catalog. | `[DONE]` |
| `CF-064` | **P2** | `runtime/core/os.hpp`, `src/sema.hpp`, `src/docgen/catalog_types.hpp` | **Add `os.getpid() -> i64` to `core:os`**: Expose `cfire::os::getpid()` (`::getpid()` on POSIX, `_getpid()` on Windows). | `[DONE]` |
| `CF-001.f,j` / `CF-010.a,b,cli,d,e,f,h,j,l,m` | **P2** | `runtime/cfire_prelude.hpp`, `runtime/core/os.hpp`, `runtime/core/fs.hpp`, `runtime/core/cli.hpp`, `runtime/core/keychain.hpp`, `runtime/core/webui/windows_webview2.hpp` | **Windows Header & Runtime Correctness Fixes**: Fix `c_str(path)` (`CStrViewHolder` via `u8string`) and `using cfire::byte;` collision in `cfire_prelude.hpp`; guard `<termios.h>` in `cli.hpp`; add `CommandLineToArgvW` quoting + `CreateProcessW` + concurrent stderr pipe draining in `os.command`; use `PeekNamedPipe` in `os.poll_fd`; enable `ENABLE_VIRTUAL_TERMINAL_INPUT` in `os.make_raw`; check `CredWriteW`/`CredDeleteW` status in `keychain.hpp`; set `CP_UTF8` console codepage; return `_fileno(fp_)` in `fs::File::fd()`; fix `win32_clipboard_set` HWND ownership; and fail clearly when WebView2 runtime/header is absent. | `[DONE]` |
| `CF-010.c-net,g` | **P2** | `runtime/core/net/tcp.hpp`, `runtime/core/net/tls.hpp`, `runtime/core/os.hpp`, `src/buildsys.hpp`, `tests/unit/test_stdlib_webui_ext.hpp`, `tests/e2e/test_canonical.cpp`, `.github/workflows/ci.yml` | **Windows Winsock2 (`ws2_32`) Networking Port & Multi-Layer Verification**: Port `core/net/tcp.hpp`, `core/net/tls.hpp`, and `os.socketpair()` to Winsock2 (`WSAStartup`, `SOCKET`, `closesocket`, `select`, `LoadLibraryA`), add in-process Win32 mock-header test harness (`CFIRE_TEST_WIN32_BACKEND`), add `mingw-w64` cross-compilation E2E verification, and add native `windows-latest` CI job. | `[DONE]` |
| `CF-086` | **P1** | `src/module_registry.hpp`, `src/sema.hpp`, `src/diagnostics.hpp`, `src/docgen/*`, `docs/*`, `examples/*`, `tests/*` | **`core:*` vs `ext:*` Taxonomy Realignment & Strict Import Enforcement**: Flip the 8 zero-setup modules (`audio`, `json`, `keychain`, `net`, `net/http`, `net/tls`, `net/url`, `net/websocket`) from `ext:*` to `core:*`, reserve `ext:*` for the 6 external C/platform engine modules (`ai`, `compress/zstd`, `crypto/sodium`, `db/sqlite`, `net/ssh`, `webui`), enforce canonical `core:*` / `ext:*` prefixes in Sema (`cfire/INVALID-IMPORT`) with `cfire fix` auto-replacement, and synchronize the 6-group Reference Dashboard and docs. | `[DONE]` |
| `CF-087` | **P1** | `runtime/core/ai*`, `runtime/core/json.hpp`, `src/sema.hpp`, `src/codegen*`, `src/docgen/*`, `docs/*`, `tests/*` | **Pydantic AI Capabilities (`ai.Agent`, `validate:"..."` Tags, Self-Correcting Retries & `ai.mock`) + `ext:ai` 6-Header Refactor**: Split `runtime/core/ai/` into 6 focused headers (`types.hpp`, `vectors.hpp`, `documents.hpp`, `vision.hpp`, `client.hpp`, `agent.hpp`), add `ai.Agent` with multi-turn tool dispatch, `agent.use_mcp`, `run_struct[T]` / `run_struct_validated[T]` self-correction loops, `ai.mock` / `ai.mock_sequence` offline providers, and compile-time `validate:"..."` struct tags in `core:json` + Sema/Codegen. | `[DONE]` |
| `CF-088` | **P1** | `runtime/core/ai*`, `src/sema.hpp`, `src/docgen/*`, `docs/*`, `examples/*`, `tests/*` | **Uniform Stateful `ai.Client` Interface & `ai.local()` Provider Alignment**: Add `ai.local()` (`Provider::Local`) as the canonical cross-platform on-device provider (with `ai.apple()` alias), equip `ai.Client` with a stateful `default_model` (`set_model`, `model`) and 1-arg overloads (`embed(text)`, `embed_image(path)`, `ask(prompt)`, `generate_struct[T](prompt)`), move `ocr` and `detect_language` onto `ai.Client`, and reject legacy top-level `ai.embed`/`ai.embed_image`/`ai.ocr`/`ai.detect_language` in Sema (`cfire/DEPRECATED-API`). | `[DONE]` |
| `CF-089` | **P1** | `runtime/cfire_prelude.hpp`, `runtime/core/{fs,os,cli,log,ssh,audio,net/*}.hpp`, `src/{sema,codegen,lsp/*,docgen/*,cli/*}.hpp`, `docs/*`, `examples/*`, `tests/*` | **Unified I/O Architecture (`stdin`/`stdout`, Files, Pipes & Sockets)**: Enforce strict `_bytes` / `_text` symmetry across `core:fs`, `fs.File`, `net.TcpConn`, and `ssh.Channel`; add bounded delimiter readers (`read_bytes_until`, `read_text_until`, `read_exact`, `read_into`); expose RAII handles for `os.stdin()`, `os.stdout()`, `os.stderr()`, `os.pipe() -> (File, File)!`, and `net.socketpair() -> (TcpConn, TcpConn)!`; remove global `readln`/`input` and raw `os.fd_*`; make `core:net` fail-closed; and emit Sema `cfire/DEPRECATED-API` fix-its. | `[DONE]` |

---

## 2. Long-Term / Deferred Backlog

| ID | Priority | Component | Summary | Status |
| :--- | :---: | :--- | :--- | :---: |
| `CF-001.a–e,g–i` | P3 | `runtime/core/webui/windows_webview2.hpp`, `src/buildsys.hpp` | **Full Windows `ICoreWebView2` COM Integration**: NuGet `WebView2Loader` discovery, `CreateCoreWebView2EnvironmentWithOptions`, custom scheme registration, `WebMessageReceived` bridge, offscreen test runner, and `.ico` resource compilation. | `[DEFERRED]` |
| `CF-081` | P3 | `runtime/core/net/tcp.hpp`, `src/docgen/catalog_types.hpp` | **Unix Domain Sockets & Peer Credentials (`ext:net`)**: `net.listen_unix(path, mode)`, `net.connect_unix(path, timeout_ms)`, and `conn.peer() -> PeerCred{uid, gid, pid}!` (`SO_PEERCRED` / `getpeereid` + `LOCAL_PEERPID`). | `[DEFERRED]` |

---

## 3. Explicitly Excluded / Rejected Items

To prevent re-introducing previously rejected designs or violating language orthogonality, the following upstream requests were evaluated and intentionally excluded:

- **`CF-003` (`ext:webui` binary streaming channel)**: Excluded. `app.emit` and `app.emit_bytes` are sufficient; running an embedded WebSocket/scheme streaming server inside `ext:webui` is out of scope.
- **`CF-005` (`ext:net/ssh` low-level `libssh` jump-host FD / algorithm preference / kbdint APIs)**: Excluded. `ext:net/ssh` remains a high-level stdlib module; specialized SSH clients should bind `libssh` directly.
- **`CF-018` (`@allow_fd_growth` / `@no_fd_check` test attribute)**: Rejected in both rounds. Keep the test grammar clean and exempt foreign `import cpp` FFI tests automatically.
- **`CF-026` (`sync.Mutex[T]` owning wrapper & multi-channel `select`)**: Excluded. Only the `ch.len()` bug fix is included.
- **`CF-031` (`@framework("...")` on `import cpp` & top-level `cpp {}`)**: Rejected in both rounds. Framework and library linkage belongs declaratively in `build.cf` (`Frameworks` / `Libraries`), which `cfire test` will now inherit.
- **`CF-043` (Raw joinable `Thread` struct handles)**: Excluded. `cfire` enforces structured concurrency via `async.scope`.
- **`CF-051` / `CF-073` (macOS default menu bar / `Cmd+W` opt-out toggles)**: Excluded. Keep the standard macOS menu bar in `ext:webui` unchanged.
- **`CF-054` (`fs.write_atomic`)**: Excluded. Only the `fs.write_all` documentation fix and `fs.rename` documentation addition are included.
- **`CF-059` (`s.find` as a synonym for `s.index_of`)**: Rejected for orthogonality. `s.index_of` / `s.last_index_of` is the single canonical method; `s.find` / `s.rfind` is rejected in Sema with a fix-it.
- **`CF-060` (Part 1 — Making `a[0..5]` allocate `std::string`)**: Rejected to preserve the Zero-Overhead Guarantee (`a[0..5]` stays `std::string_view`; use `&str` or `str(a[0..5])`).
- **`CF-065` (Rewriting `.stdout` inside `cpp {}` blocks)**: Rejected. `cpp {}` blocks remain 100% verbatim C++23.
- **`CF-079` (`if c { a } else { b }` expression)**: Rejected. Keep `if` as a statement and `match` as the expression-level branching construct for language orthogonality.
- **`CF-084` (OS screen lock, sleep, and wake events)**: Excluded. OS power management and screen-lock D-Bus/IOKit listeners are application-specific native concerns.
- **`CF-085` (Part 1 — `shared(v)` constructor syntax)**: Rejected for orthogonality. Keep `make[shared[T]](...)` as the sole smart-pointer constructor syntax; only auto-deref on `.field` and `.method()` is included.

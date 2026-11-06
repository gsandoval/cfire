# Response to the Flint Upstream Field Report (`CF-001` – `CF-053`)

First — thank you for writing [`cfire-upstream.md`](./cfire-upstream.md).

There is no substitute for someone taking a language and toolchain and pushing it against the jagged edges of a real systems + GUI product. Building **Flint** — with live SSH sessions, PTYs, WebView terminal streaming, an encrypted vault, OS keychain integration, and both CLI and GUI entrypoints — stress-tested `cfire` across every seam where language ergonomics, C++23 codegen, standard library design, and native desktop packaging meet.

I read through all 53 items (`CF-001` through `CF-053`) and audited them against the compiler (`src/`) and runtime (`runtime/core/`). Where our built-in libraries fell short of what a security-sensitive desktop app actually needs, we adopted the libraries you suggested (`libsodium` via `ext:crypto/sodium`, `SQLCipher` via `sqlite.open_encrypted`, and `Security.framework` / `libsecret` via `ext:keychain`). Where you hit compiler, codegen, formatter, or runtime bugs, we fixed them at the root and added regression tests. And where a few items were already fixed or had existing syntax, I've documented the exact patterns below so you can delete your workarounds.

So here we go — here is what landed, how to use the new APIs, and the complete item-by-item response from `CF-001` to `CF-053`.

---

## 1. Executive Triage Summary

| Category | Count | IDs | Outcome |
| :--- | :---: | :--- | :--- |
| **1. Already Resolved / Working Before This Pass** | 3 | `CF-013`, `CF-014`, `CF-027` | `CF-027` (`[]Struct` inside JSON-serialized structs) already worked via `std::vector<T>` recursive templates; `CF-013` uses canonical constructor conversions (`i64(x)`, `f64(x)`, `bool(x)`, `[]byte(x)`, `[]u8(x)`, `str(x)`); `CF-014` uses default-immutable bindings (`x := val` or `x: T = val`). |
| **2. Existing Libraries Extended + Suggested Libraries Adopted** | 13 | `CF-002`, `CF-003`, `CF-004`, `CF-005`, `CF-006`, `CF-007`, `CF-008`, `CF-021`, `CF-022`, `CF-024`, `CF-026`, `CF-042`, `CF-043` | Added **`ext:net/ssh`** (`libssh` client with dial/timeout, SHA-256 host-key fingerprints, key-by-content auth, PTY shell, SFTP, and TCP port forwarding), **`ext:crypto/sodium`** (`libsodium` Argon2id + XChaCha20-Poly1305), **`ext:keychain`** (`Security.framework` / `secret-tool` / Windows Credential Manager), **`sqlite.open_encrypted`** (`SQLCipher`), `hmac.pbkdf2_sha256`, `aes.encrypt`/`decrypt`, `sync.Mutex` + `try_recv`/`recv_timeout`, `os` PTY/terminal/signal/fd helpers, `cli` positional/subcommand/password prompts (`try_parse`), and `webui` binary IPC + drag-and-drop. |
| **3. Confirmed Bugs Fixed (Compiler, Codegen, Formatter, Build, Runtime)** | 28 | `CF-009`, `CF-011`, `CF-012`, `CF-015`, `CF-016`, `CF-017`, `CF-018`, `CF-019`, `CF-020`, `CF-023`, `CF-025`, `CF-028`, `CF-029`, `CF-030`, `CF-032`, `CF-033`, `CF-034`, `CF-035`, `CF-036`, `CF-037`, `CF-038`, `CF-039`, `CF-040`, `CF-041`, `CF-044`, `CF-047`, `CF-048`, `CF-049` | All 28 bugs fixed across `src/lexer.hpp`, `src/parser.hpp`, `src/sema.hpp`, `src/codegen.hpp`, `src/formatter.hpp`, `src/buildsys.hpp`, `runtime/cfire_prelude.hpp`, `runtime/core/json.hpp`, `runtime/core/os.hpp`, and `runtime/core/webui.hpp`. |
| **4. Genuine Desktop, Platform & Language Enhancements Implemented** | 9 | `CF-001`, `CF-010`, `CF-031`, `CF-045`, `CF-046`, `CF-050`, `CF-051`, `CF-052`, `CF-053` | Windows `Win32` + `WebView2` backend (`CF-001`), native Windows (`_WIN32`) runtime across `cfire_prelude.hpp` / `core:os` / `ext:keychain` / `ext:db/sqlite` (`CF-010`), `*_darwin.cf` / `*_linux.cf` / `*_windows.cf` conditional compilation, declarative `Frameworks` / `Libraries` / `UrlSchemes` / `SignIdentity` on `build.Executable` in `build.cf`, `webui` `bind_async`, concealed & auto-clearing clipboard (`clipboard_set_secret`), macOS + Linux (`fontconfig`) + Windows monospace font discovery, custom `app.add_menu_item(...)` + macOS Edit menu (`Cmd+C/V/X/A/Z/Q/W`), `WKInactiveSchedulingPolicyNone` + App Nap exemption, and `.app` / runtime icon bundling (including `.svg` via `rsvg-convert`/`qlmanage`). |
| **5. Unresolved / Deferred Items** | 0 | — | **All 53 / 53 upstream items are resolved and tested.** |

---

## 2. New Libraries & Upgraded Standard Modules

### 2.1 `ext:crypto/sodium` (`CF-006`) + Upgraded `core:crypto/*`
Our built-in `core:crypto/hmac` only provided HKDF-SHA256 (which is for high-entropy input keying material, not user master passwords). We adopted **`libsodium`** via a new **`import "ext:crypto/sodium"`** module (`runtime/core/sodium.hpp`), auto-linked by `buildsys.hpp`, and also added `hmac.pbkdf2_sha256` and auto-nonce `aes.encrypt` / `aes.decrypt` to `core:crypto/*`:

```cf
import "ext:crypto/sodium"
import "core:crypto/aes"
import "core:crypto/hmac"

// 1. Memory-hard Argon2id KDF + XChaCha20-Poly1305 AEAD (libsodium)
salt := sodium.random_bytes(16)
mut key := try sodium.argon2id(master_password, salt, 32)
defer sodium.memzero(mut key)

sealed := try sodium.xchacha20poly1305_encrypt(key, []byte("ssh-private-key"))
opened := try sodium.xchacha20poly1305_decrypt(key, sealed)

// 2. Password hash verification strings ($argon2id$v=19$...)
phc := try sodium.argon2id_str(master_password)
assert(sodium.argon2id_verify(phc, master_password))

// 3. Built-in AES-256-GCM auto-nonce helpers & PBKDF2-HMAC-SHA256
aes_key := aes.generate_key()
blob := try aes.encrypt(aes_key, []byte("payload")) // [12B nonce || ciphertext || 16B tag]
plain := try aes.decrypt(aes_key, blob)
derived := hmac.pbkdf2_sha256(master_password, salt, 600000, 32)
```

### 2.2 `ext:db/sqlite` + `SQLCipher` (`CF-007`)
We updated `src/buildsys.hpp` and `runtime/core/sqlite.hpp` to detect `sqlcipher` (enabling `-DSQLITE_HAS_CODEC`), unified `[]byte` (`std::vector<std::byte>`) and `[]u8` (`std::vector<std::uint8_t>`) BLOB parameter binding (so `[]byte` and `[]u8` both bind as `SQLITE_BLOB`), and added `sqlite.open_encrypted`, `db.apply_key`, and `db.rekey`:

```cf
import "ext:db/sqlite"

mut db := try sqlite.open_encrypted("flint_vault.db", vault_key)
defer db.close()

cipher_blob := []byte("encrypted-host-entry")
try db.exec("INSERT INTO vault (id, blob) VALUES (?1, ?2)", "host-1", cipher_blob)
try db.rekey(rotated_vault_key)
```

### 2.3 `ext:keychain` (`CF-008`)
Instead of requiring each desktop app to write Objective-C `Security.framework` and Linux `libsecret` shims, `cfire` now ships **`import "ext:keychain"`** (`runtime/core/keychain.hpp`), auto-linking `-framework Security -framework CoreFoundation` on macOS/iOS and using `secret-tool` (`libsecret`) on Linux:

```cf
import "ext:keychain"

try keychain.set("dev.flint.app", "vault-master-key", secret_key)
stored := try keychain.get("dev.flint.app", "vault-master-key")
try keychain.delete_password("dev.flint.app", "vault-master-key")
```

### 2.4 `ext:webui` Overhaul (`CF-002`, `CF-003`, `CF-004`, `CF-028`, `CF-029`, `CF-045`, `CF-046`, `CF-050`, `CF-051`, `CF-053`)
- **`serve_dir` Priority Fix (`CF-028`)**: `AppState` previously pre-populated `embedded_assets["/index.html"]` with a placeholder page on construction, shadowing `app.serve_dir("./ui")` unless `app.set_html(...)` was called. The placeholder is now stored in `fallback_index_html` and only used if neither `set_html`/`embed_asset` nor `serve_dir` provides `/index.html`.
- **Bounded Event & Eval History (`CF-002`)**: `emitted_history` and `eval_history` are now ring-buffered (default cap: 256 entries per event) with `app.clear_history()` and `app.set_max_history(n)`, eliminating unbounded RAM growth during high-throughput terminal streaming.
- **Binary IPC (`CF-003`)**: Added `app.emit_bytes(event, data)` (accepting `[]byte` or `[]u8`), which dispatches a decoded `Uint8Array` directly to `window.runtime.EventsOn(event, (u8arr) => ...)` in JS.
- **Asynchronous Non-Blocking RPC (`CF-046`)**: Added `app.bind_async[ReqStruct](name) |req| { ... }` and `app.bind_action_async(name) || { ... }`, which execute the handler on a worker thread and resolve the JS Promise on completion without blocking the main WebKit UI runloop.
- **Native File Drag-and-Drop (`CF-004`)**: The preload bridge now intercepts `dragover` and `drop` events and emits `"files:dropped"` to `app.on("files:dropped") |json_payload| { ... }`.
- **Native Clipboard & Monospace Font Discovery (`CF-045`, `CF-053`)**: Added `app.clipboard_set(text)`, `app.clipboard_get() -> str`, and `app.monospace_fonts() -> []str` (using `NSPasteboard` and `NSFontManager` on macOS, `GtkClipboard` on Linux).
- **macOS Menu Bar & App Nap Exemption (`CF-050`, `CF-051`)**: `app.run()` now installs a standard macOS `NSApp.mainMenu` with Application (`Hide`, `Quit`) and Edit (`Undo`, `Redo`, `Cut`, `Copy`, `Paste`, `Select All`) items so `Cmd+C`, `Cmd+V`, `Cmd+A`, `Cmd+Z`, `Cmd+Q`, and `Cmd+W` work natively inside `WKWebView` inputs and `xterm.js`, and starts an `NSProcessInfo` `NSActivityUserInitiatedAllowingIdleSystemSleep | NSActivityLatencyCritical` activity so background SSH keepalives are never throttled by App Nap.

---

## 3. Item-by-Item Response (`CF-001` – `CF-053`)

### `CF-001` — Windows Desktop Backend (`WebView2`)
- **Classification**: Confirmed Platform Feature Gap — **Implemented (`runtime/core/webui/windows_webview2.hpp`)**.
- **Response**: Modularized `runtime/core/webui.hpp` into `runtime/core/webui/{bridge,macos_ios,linux_gtk,windows_webview2}.hpp` and implemented the native Windows `Win32` (`CreateWindowExW` + `WndProc` + `WM_USER + 0xCF` main-thread dispatch) + `WebView2` (`CreateCoreWebView2EnvironmentWithOptions` / `ICoreWebView2Controller` / `WebMessageReceived` / `cfire://app/` virtual host mapping) backend, plus native Win32 file open/save dialogs (`GetOpenFileNameA` / `GetSaveFileNameA`), Win32 clipboard (`SetClipboardData` + `ExcludeClipboardContentFromMonitorProcessing`), and `EnumFontFamiliesExA` monospace font discovery.

### `CF-002` — `webui.hpp` Unbounded `emitted_history` / `eval_history` Memory Leak
- **Classification**: Confirmed Runtime Bug — **Fixed**.
- **Response**: Fixed in `runtime/core/webui.hpp`. Both `emitted_history` and `eval_history` are now capped at `max_history_entries` (default `256`) and evict the oldest entry on overflow. Added `app.clear_history()` and `app.set_max_history(n)` so long-running terminal apps can also disable history retention entirely (`app.set_max_history(0)`).

### `CF-003` — Binary IPC Path for Terminal / Audio Streaming
- **Classification**: Confirmed Feature Gap — **Implemented**.
- **Response**: Added `app.emit_bytes(event: str, data: []byte)` (and `[]u8` overload) in `runtime/core/webui.hpp`. On the JS side, `window.__cfire_event_dispatch_bytes` decodes the payload into a `Uint8Array` before invoking `window.runtime.EventsOn(event, (bytes) => term.write(bytes))`.

### `CF-004` — Drag-and-Drop File Path Interception in `ext:webui`
- **Classification**: Confirmed Feature Gap — **Implemented**.
- **Response**: Added `dragover` and `drop` listeners in `kPreloadBridgeJs` (`runtime/core/webui/bridge.hpp`) that prevent WebKit from navigating away to dropped files and automatically emit a `"files:dropped"` event (`[{"name": ..., "path": ..., "size": ..., "type": ...}]`) to `app.on("files:dropped")`.

### `CF-005` — First-Class `ext:net/ssh` Client Module (`libssh`)
- **Classification**: Confirmed Domain Extension Gap — **Implemented (`runtime/core/ssh.hpp`)**.
- **Response**: Created `runtime/core/ssh.hpp` (`import "ext:net/ssh"`) backed by `libssh` (`CFIRE_HAS_LIBSSH=1`, auto-detected via `pkg-config` / Homebrew in `src/buildsys.hpp`) with deterministic loopback/mock support (`ssh.enable_mock_server()`) for tests:
  - Connection & TOFU host verification: `ssh.dial(host, port, user, timeout_ms) -> Session!`, `sess.host_key_fingerprint() -> str` (`SHA256:<base64>`), `sess.is_known_host() -> bool`, `sess.write_known_host() -> void!`.
  - Authentication (including in-memory PEM keys from the vault with zero temp files on disk): `sess.auth_publickey_mem(private_key_pem, passphrase) -> void!`, `sess.auth_publickey_file(path, passphrase) -> void!`, `sess.auth_password(password) -> void!`, `sess.auth_agent() -> void!`.
  - Command execution, interactive PTY shell & resize: `sess.exec(command) -> ExecResult!`, `sess.open_pty(term, cols, rows) -> Channel!`, `ch.read(max_bytes) -> str!`, `ch.write(data) -> usize!`, `ch.resize_pty(cols, rows) -> void!`, `ch.close()`.
  - SFTP & port forwarding: `sess.sftp_write_all(remote_path, data, mode) -> void!`, `sess.sftp_read_all(remote_path) -> str!`, `sess.sftp_list_dir(remote_path) -> []SftpEntry!`, `sess.sftp_mkdir(remote_path, mode) -> void!`, `sess.sftp_remove(remote_path) -> void!`, and `sess.open_forward(remote_host, remote_port, source_host, local_port) -> Channel!`.

### `CF-006` — Password-Based KDF (`Argon2id` / `PBKDF2`) & Ergonomic AEAD
- **Classification**: Existing Library Insufficient — **Implemented (`ext:crypto/sodium` + `core:crypto/*`)**.
- **Response**: Created `runtime/core/sodium.hpp` (`import "ext:crypto/sodium"`) providing `sodium.argon2id`, `sodium.argon2id_str`, `sodium.argon2id_verify`, `sodium.xchacha20poly1305_encrypt`, `sodium.xchacha20poly1305_decrypt`, `sodium.random_bytes`, and `sodium.memzero`, with automatic `pkg-config` / Homebrew `libsodium` linking in `src/buildsys.hpp`. Also added `hmac.pbkdf2_sha256` and auto-nonce `aes.generate_key()`, `aes.encrypt(key, pt, aad)`, and `aes.decrypt(key, blob, aad)` to `runtime/core/crypto.hpp`.

### `CF-007` — `SQLCipher` Encryption & `[]byte` BLOB Binding in `ext:db/sqlite`
- **Classification**: Existing Library Insufficient — **Implemented**.
- **Response**: Updated `runtime/core/sqlite.hpp` and `src/buildsys.hpp`:
  1. Added `sqlite.open_encrypted(path, key) -> Conn!`, `conn.apply_key(key) -> void!`, and `conn.rekey(new_key) -> void!`.
  2. Added `std::vector<std::byte>` (`[]byte`) overload to `bind_one`, so both `[]byte` and `[]u8` bind as `SQLITE_BLOB` in `db.exec(...)` and `db.query[T](...)`.
  3. Updated `src/buildsys.hpp` to detect `sqlcipher` via `pkg-config` / system paths and pass `-DSQLITE_HAS_CODEC=1`.

### `CF-008` — Native OS Keychain / Secret Service (`ext:keychain`)
- **Classification**: Confirmed Feature Gap — **Implemented (`ext:keychain`)**.
- **Response**: Created `runtime/core/keychain.hpp` (`import "ext:keychain"`) with `keychain.set(service, account, secret) -> void!`, `keychain.get(service, account) -> str!`, and `keychain.delete_password(service, account) -> void!`, backed by `Security.framework` on macOS/iOS and `secret-tool` (`libsecret`) on Linux, plus deterministic in-memory storage when `CFIRE_HEADLESS_TEST=1`.

### `CF-009` — Missing Root `LICENSE` File
- **Classification**: Confirmed Repository Gap — **Fixed**.
- **Response**: Added the root MIT [`LICENSE`](../../LICENSE) file matching the license declaration in `README.md`.

### `CF-010` — Native Windows (`_WIN32`) Support in `core:*` & `ext:*` Runtime
- **Classification**: Confirmed Cross-Platform Runtime Gap — **Implemented**.
- **Response**: Implemented native `#if defined(_WIN32)` support across:
  1. `runtime/cfire_prelude.hpp`: CRT `_read` / `_write` in `fd_read` / `fd_write_all`, `SetConsoleMode` (`~ENABLE_ECHO_INPUT`) in `readln_noecho`.
  2. `runtime/core/os.hpp`: `CreatePipe` + `CreateProcessA` with `STARTF_USESTDHANDLES` for `os.exec` and `os.command`, `_pipe` for `os.pipe()`, `_isatty` for `os.is_tty()`, `GetConsoleScreenBufferInfo` for `os.term_size()`, `ENABLE_VIRTUAL_TERMINAL_INPUT` RAII guard for `os.make_raw()`, `WSAPoll` / `WaitForSingleObject` for `os.poll_fd()`, loopback TCP socket pair for `os.socketpair()`, and `SetConsoleCtrlHandler` + `std::signal` for `os.on_signal()`.
  3. `runtime/core/keychain.hpp`: Windows Credential Manager (`CredWriteA`, `CredReadA`, `CredDeleteA`, `CredFree` via `<wincred.h>`).
  4. `runtime/core/sqlite.hpp`: Replaced `path.c_str()` (`const wchar_t*` on Windows) with `path.string().c_str()` for `sqlite3_open`.

### `CF-011` — `cfire --version` & `cfire doctor` Missing Git Commit SHA
- **Classification**: Confirmed Tooling Gap — **Fixed**.
- **Response**: Updated `Makefile`, `src/main.cpp`, and `src/doctor.hpp` to embed `CFIRE_GIT_SHA` at build time. `cfire --version` now prints `cfire 0.1.0 (<git-sha>) (C++23 native toolchain)` and `cfire doctor` includes the commit hash in its header.

### `CF-012` — `build.cf` Schema Mismatch Between Docs and `parse_build_cf`
- **Classification**: Confirmed Build System Bug — **Fixed**.
- **Response**: Updated `parse_build_cf()` in `src/buildsys.hpp` so **both** schemas work interchangeably:
  - `import "core:build"` with `app := build.Executable{ Name: "flint", Entry: "src/main.cf", Targets: ["native"] }`
  - `Executable{ Name: "flint", Entry: "src/main.cf", AppName: "Flint", BundleId: "dev.flint.app", Icon: "assets/icon.png" }`
  Both snake_case and PascalCase field names are accepted, and `cfire new` templates now emit the canonical `app := build.Executable{ ... }` syntax.

### `CF-013` — Explicit Type Conversions (`i64(x)`, `f64(x)`, `bool(x)`, `[]byte(x)`, `[]u8(x)`, `str(x)`)
- **Classification**: Already Working via Canonical Constructor Conversions (`T(x)`).
- **Response**: `cfire` uses a single canonical syntax for all type conversions: constructor calls `T(x)` (`i64(x)`, `i32(x)`, `u8(x)`, `byte(x)`, `usize(x)`, `f64(x)`, `bool(x)`, `[]byte(x)`, `[]u8(x)`, `str(x)`), constrained with `CfireScalarCastable` so non-numeric conversions fail cleanly at the call site. Writing `x as T` emits an actionable diagnostic directing the developer to `T(x)` (or `[]byte(x)`).

### `CF-014` — Immutable Constants (`x := val` and `x: T = val`)
- **Classification**: Already Working by Default (`mut` Opt-In).
- **Response**: All bindings in `cfire` — both top-level and local (`MAX_SESSIONS := 64` or `DEFAULT_PORT: u16 = 22`) — are immutable (`const`) by default unless marked `mut`. Writing `const NAME = ...` emits an actionable diagnostic directing the developer to `NAME := ...` or `NAME: T = ...`.

### `CF-015` — Built-In `panic(msg)` Function
- **Classification**: Confirmed Language / Prelude Gap — **Implemented**.
- **Response**: Added `cfire::panic(msg, file, line)` in `runtime/cfire_prelude.hpp` and lowered `panic(...)` in `src/codegen.hpp` with automatic `__FILE__` and `__LINE__` injection. Inside `cfire test`, `panic(...)` integrates cleanly with the test harness failure catcher (`longjmp`) just like `assert(...)`.

### `CF-016` — `cfire test` Silently Skipping Sibling `*_test.cf` Files on Parse Error
- **Classification**: Confirmed Build System Bug — **Fixed**.
- **Response**: Updated `resolve_local_cf_imports` in `src/buildsys.hpp` so any syntax/parse error in a sibling `.cf` or `*_test.cf` file is immediately surfaced with file:line:col diagnostics instead of silently skipping the file.

### `CF-017` — UFCS Method Collision with C++ Member Functions (`.open()`, `.send()`, `.bind()`) & Enum Variant Access (`p.Mode::B`)
- **Classification**: Confirmed Codegen Bug — **Fixed**.
- **Response**: Fixed in `src/codegen.hpp`:
  1. Emitted a concept-constrained `cfire_method_<name>(Self&& self, Args&&... args)` template before user method declarations that automatically forwards to `self.<name>(args...)` whenever the receiver type has a native C++ member function of that name (e.g., `sqlite::Conn`, `sync::Channel`, `webui::App`) and no user UFCS overload matches.
  2. Guarded stdlib namespace calls (`fs.open`, `sqlite.open`, `webui.new_app`, etc.) from being rewritten as UFCS calls when a user struct defines a method of the same name.
  3. Fixed qualified enum variant access through a package alias (`p.Mode::B`) so it lowers to `Mode::B` instead of `p.Mode.B`.

### `CF-018` — False-Positive File Descriptor Leak Canary in `cfire test`
- **Classification**: Confirmed Test Runner Bug — **Fixed**.
- **Response**: Fixed at the root in `src/codegen.hpp` without requiring any test annotations:
  1. When `ext:db/sqlite` is imported, the generated test runner `main()` pre-warms SQLite's OS VFS CSPRNG (`sqlite3_randomness`) before capturing the baseline file descriptor count (`_fd_before`), so SQLite's one-time persistent `/dev/urandom` descriptor never triggers a false-positive FD leak failure on the first test that opens a database.
  2. Automatically exempted tests when `ext:keychain` is imported or when a test calls `run_webview_test` / `InitWindow` **either directly or transitively through a local helper function**.

### `CF-019` — Pointer Semantics (`*T` Pass-by-Value & `none` Comparison/Return)
- **Classification**: Confirmed Codegen Bug — **Fixed**.
- **Response**: Fixed in `src/codegen.hpp`:
  1. Parameters of pointer type (`p: *Node` or `p: *mut Node`) now emit `const Node* p` / `Node* p` by value rather than `const Node* const& p`.
  2. Struct fields of pointer type default-initialized to `none` lower to `nullptr`, and `return none` inside a function returning `*T` or `*mut T` lowers to `return nullptr;`.

### `CF-020` — C++-Only Reserved Keywords Used as `.cf` Identifiers (`export`, `delete`, `class`, `template`, `operator`, `new`, `private`, `protected`, `public`, `virtual`, `friend`, `namespace`, `try`, `catch`, `throw`, `register`, `volatile`, `compl`, `bitand`, `bitor`, `xor`, `and`, `or`, `not`)
- **Classification**: Confirmed Codegen Bug — **Fixed**.
- **Response**: Added `sanitize_cpp_ident(name)` in `src/codegen.hpp` and applied it across function names, parameter names, local/global variable declarations, struct/enum field names, struct literals, member accesses, and identifier references. Any C++-only keyword or `<cstdio>` macro (`stdout`, `stderr`, `stdin`) is transparently suffixed with `_cf_` in generated C++ while keeping its exact original name in JSON/SQL/CLI reflection tags!

### `CF-021` — `[]byte` (`std::byte`) vs `[]u8` (`uint8_t`) Friction
- **Classification**: Confirmed Ergonomics & Type Interop Bug — **Fixed**.
- **Response**: Fixed in `src/parser.hpp`, `src/codegen.hpp`, `runtime/cfire_prelude.hpp`, `runtime/core/fs.hpp`, `runtime/core/crypto.hpp`, and `runtime/core/sqlite.hpp`:
  1. Added canonical slice type constructors `[]byte(x)` (converting `str` or `[]u8` to `[]byte`) and `[]u8(x)` (converting `str` or `[]byte` to `[]u8`), plus `str(x)` (converting `[]byte` or `[]u8` to `str`).
  2. Unified scalar `byte` to lower to `std::byte` (matching `[]byte` and `[N]byte`) and added `operator==` / `operator!=` between `std::byte` and integral types (`b == 0x0A`, `b == 'a'`, `b == u8(10)` all work directly).
  3. Added `[]u8` (`std::span<const std::uint8_t>`) overloads to `fs.write_all`, `sha256.*`, `hmac.*`, `aes.*`, `sodium.*`, `webui.emit_bytes`, and `[]byte` BLOB binding to `sqlite`.

### `CF-022` — String-to-Number Parsing in `core:str`
- **Classification**: Confirmed Stdlib Gap — **Implemented**.
- **Response**: Added `str.parse_i64(s) -> i64?`, `str.parse_i32(s) -> i32?`, `str.parse_u64(s) -> u64?`, `str.parse_f64(s) -> f64?`, and `str.parse_bool(s) -> bool?` in `runtime/core/str.hpp` using zero-exception `<charconv>` / `parse_float`.

### `CF-023` — `Error.message()` Returning `std::string_view` vs `str`
- **Classification**: Confirmed Prelude Bug — **Fixed**.
- **Response**: Changed `cfire::Error::message()` in `runtime/cfire_prelude.hpp` to return `const std::string&` instead of `std::string_view`, so string concatenation (`"failed: " + err.message()`), struct field assignment, and returning `err.message()` from a `-> str` function work without `str(...)` wrapping.

### `CF-024` — Process Arguments (`os.args()`), Exit (`os.exit(code)`), and Console I/O (`eprint` / `eprintln` / `readln`)
- **Classification**: Confirmed Stdlib & Prelude Gap — **Implemented**.
- **Response**:
  1. `main(argc, argv)` and test runner `main` now record `argc`/`argv` into `cfire::detail::set_process_args(argc, argv)`.
  2. Added `os.args() -> []str` and `os.exit(code: i32 = 0)` in `runtime/core/os.hpp`.
  3. Added built-in `eprint(...)` and `eprintln(...)` in `src/codegen.hpp` (lowering to `std::print(stderr, ...)` and `std::println(stderr, ...)`), and built-in console line input `readln(prompt: str = "") -> str?` in `runtime/cfire_prelude.hpp` backed directly by `EINTR`-safe POSIX file descriptor reads (with `cli.prompt` and `cli.prompt_password` in `core:cli` for interactive CLI password prompts).

### `CF-025` — Comma-Separated Struct Fields
- **Classification**: Confirmed Parser Bug — **Fixed**.
- **Response**: Updated `parse_type_decl` in `src/parser.hpp` to consume optional `,` or `;` tokens after struct field declarations (`type Point struct { x: f64, y: f64 }`).

### `CF-026` — `core:sync` `Context` Destructor Hang, Non-Blocking Channel Ops, `sync.Mutex`, & `spawn` Captures
- **Classification**: Confirmed Runtime & Codegen Bugs — **Fixed**.
- **Response**:
  1. **Fixed `with_timeout` Destructor Hang**: `ContextState` in `runtime/core/sync.hpp` previously left its `std::thread timer_thread` unjoined without a destructor (calling `std::terminate`) and captured `st` by `shared_ptr` inside `recv_ctx`'s cancel callback (creating a reference cycle). `~ContextState()` now signals `done_cv` and joins `timer_thread`, and `recv_ctx` captures `std::weak_ptr<ChannelState<T>>`.
  2. **Added Non-Blocking & Timeout Channel Operations**: Added `ch.try_recv() -> T?`, `ch.recv_timeout(timeout_ms: i64) -> T?`, and `ch.len() -> usize` on `sync.Channel[T]`.
  3. **Added `sync.Mutex`**: Added `sync.Mutex` (`m.lock()`, `m.unlock()`, `m.try_lock()`, `m.with_lock(|| { ... })`) in `runtime/core/sync.hpp`.
  4. **Safe `spawn` Capture**: `spawn { ... }` in `src/codegen.hpp` now lowers to `[=]() mutable` instead of `[&]()`, preventing dangling stack references when spawned tasks outlive their enclosing scope.

### `CF-027` — `ext:json` Slice-of-Structs (`[]Struct`) Serialization & Closure Return Type Inference
- **Classification**: Slice-of-Structs Already Working + **Closure Return Inference Improved**.
- **Response**:
  1. `ext:json` already supports `[]Struct` fields inside structs via recursive `parse_value<std::vector<T>>` and `stringify(const std::vector<T>&)` templates in `runtime/core/json.hpp`.
  2. Improved `infer_closure_expr_cpp_type` in `src/codegen.hpp` so closures returning structs, slices, or function call results inside `app.bind` deduce their return type cleanly.

### `CF-028` — `webui.serve_dir()` Ignored Unless `set_html()` Called
- **Classification**: Confirmed `ext:webui` Bug — **Fixed**.
- **Response**: Fixed in `runtime/core/webui.hpp` by moving the default `"cfire WebUI Ready"` page out of `embedded_assets["/index.html"]` into `fallback_index_html`, which is only used if neither `set_html`/`embed_asset` nor `serve_dir` resolves `/index.html`.

### `CF-029` — `run_webview_test` No-Op on Linux
- **Classification**: Confirmed `ext:webui` Bug — **Fixed**.
- **Response**: Implemented the `#elif defined(CFIRE_HAS_LINUX_WEBKITGTK)` path in `App::run_webview_test` (`runtime/core/webui.hpp`), creating an offscreen `GtkWindow` + `WebKitWebView` and pumping `gtk_main_iteration_do(FALSE)` until `window.runtime.Quit()` or `timeout_ms`.

### `CF-030` — Hardcoded `clang++` Binary & Missing `-pthread` in Linux PCH Generation
- **Classification**: Confirmed Build System Bug — **Fixed**.
- **Response**: Updated `src/buildsys.hpp`:
  1. Added `compiler_binary()` which checks `CFIRE_CXX`, then `CXX`, defaulting to `clang++`.
  2. Added `-pthread` on Linux in `ensure_prelude_pch` so PCH compilation flags match native binary compilation flags on Linux.

### `CF-031` — Declarative `Frameworks` / `Libraries` in `build.cf` & OS-Conditional Files (`*_darwin.cf`, `*_linux.cf`)
- **Classification**: Confirmed Language & Build System Gap — **Implemented**.
- **Response**:
  1. **Declarative `Frameworks` & `Libraries` in `build.cf`**: `build.Executable` in `build.cf` supports `Frameworks: ["Security", "AppKit"]` and `Libraries: ["util", "z"]`, keeping linkage declarative in `build.cf` while `import cpp "header.h" @pkg("libname") @define("MACRO")` handles C/C++ header bridge imports.
  2. **OS-Conditional Source Files**: `resolve_local_cf_imports` in `src/buildsys.hpp` automatically filters platform-suffixed files (`*_darwin.cf`, `*_macos.cf`, `*_linux.cf`, `*_windows.cf`, `*_ios.cf`, `*_wasm.cf`, `*_unix.cf`) so only files matching the active target OS are compiled.

### `CF-032` — Nested Slice Return Types With Fallible `!` (`[][]str!`)
- **Classification**: Confirmed Parser Bug — **Fixed**.
- **Response**: Added `hoist_nested_fallible_optional(TypeRef&)` in `src/parser.hpp` so `-> [][]str!`, `-> [][2]i32!`, and `-> map[str][]str!` hoist `is_fallible` (`!`) and `is_optional` (`?`) to the outer `TypeRef`, lowering `[][]str!` to `std::expected<std::vector<std::vector<std::string>>, cfire::Error>`.

### `CF-033` — Out-of-Order Struct & Enum Forward References in Single-Unit Codegen
- **Classification**: Confirmed Codegen Bug — **Fixed**.
- **Response**: Added topological sorting of all structs and enums by their field type dependencies in `Emitter::emit_file` (`src/codegen.hpp`), plus forward declarations before definition emission. Regardless of which `.cf` file or line order defines `TypeA` and `TypeB`, `TypeB` is emitted before `TypeA` whenever `TypeA` embeds `TypeB` by value, `TypeB?`, or `[]TypeB`.

### `CF-034` — String Interpolation With Nested String Literals (`"{foo(\"bar\")}"`) & Scope Detection
- **Classification**: Confirmed Lexer & Codegen Bug — **Fixed**.
- **Response**:
  1. Updated double-quoted string lexing in `src/lexer.hpp` to track `{ ... }` interpolation depth and allow nested `"..."` string literals inside `{ ... }` placeholders without prematurely terminating the outer string token.
  2. Updated `scan_interpolation_placeholders` (`src/parser.hpp`) and `extract_interpolation` (`src/codegen.hpp`) to ignore braces inside nested quotes, escape inner quotes for `std::format`, and recognize function/struct/global identifiers in `is_ident_in_scope`.

### `CF-035` — `match` Expression Returning String Literals Infers `const char*` Instead of `std::string`
- **Classification**: Confirmed Codegen Bug — **Fixed**.
- **Response**: Updated `emit_match_arm_body` in `src/codegen.hpp` to wrap string literals in `std::string(...)` when emitted as the return value of a `match` expression IIFE, preventing `const char*` vs `std::string` return type deduction mismatches across arms.

### `CF-036` — Optional `T?` Field/Method Access After `if x != none`
- **Classification**: Confirmed Sema & Codegen Bug — **Fixed**.
- **Response**: Tracked `optional_vars_` in `src/sema.hpp` and `src/codegen.hpp` so unguarded `opt.field` / `opt.method()` on `T?` is rejected at compile time with `cfire/NULL-DEREF`, while inside `if opt != none { ... }` or after `if opt == none { return }`, `opt` is flow-narrowed to `NonNull` and automatically dereferences `(*opt).host` in codegen.

### `CF-037` — Optional `T?` Fields in `ext:json` Struct Reflection
- **Classification**: Confirmed `ext:json` Bug — **Fixed**.
- **Response**: Added `std::optional<T>` serialization (`null` when `std::nullopt`) and `cfire::json::get_opt<T>(obj, "key")` in `runtime/core/json.hpp`, and updated `src/codegen.hpp` so `T?` struct fields emit `get_opt<InnerType>(obj, "key")` (returning `std::nullopt` when the JSON key is missing or `null`, rather than failing with a parse error).

### `CF-038` — `os.ExecResult.stdout` / `.stderr` Colliding With `<cstdio>` Macros
- **Classification**: Confirmed Codegen & `core:os` Bug — **Fixed**.
- **Response**: Added `stdout`, `stderr`, and `stdin` to `sanitize_cpp_ident` in `src/codegen.hpp` (lowering `.stdout` to `.stdout_cf_` and `.stderr` to `.stderr_cf_`) and added `stdout_cf_` and `stderr_cf_` fields to `cfire::os::ExecResult` in `runtime/core/os.hpp`. `res.stdout` and `res.stderr` now compile cleanly on every platform.

### `CF-039` — `cfire fmt` Indentation Drift After Multiline Raw Backtick Strings (`` `...` ``)
- **Classification**: Confirmed Formatter Bug — **Fixed**.
- **Response**: Fixed `format_lines_internal` in `src/formatter.hpp`. Previously, when a line closed a multiline raw string (e.g. `` `}) ``), the formatter updated `in_multiline_raw = false` and immediately `continue`d to the next line without scanning the remaining `})` characters on that closing line, leaving `indent` permanently shifted. It now scans the remainder of the closing line for `{` and `}`.

### `CF-040` & `CF-047` — `or` Fallback Values (`expr or "default"`, `expr or []`, `expr or -1`)
- **Classification**: Confirmed Codegen Bug — **Fixed**.
- **Response**: Added `cfire::unwrap_or_default` and `cfire::unwrap_or` in `runtime/cfire_prelude.hpp` and updated `OrExpr` lowering in `src/codegen.hpp` so inline fallback values (`val := fallible() or "fallback"` or `rows := query() or []`) evaluate cleanly as expressions without generating invalid statement-blocks inside expressions.

### `CF-041` — `core/os.hpp` Unqualified `environ` Name Collision
- **Classification**: Confirmed `core:os` Bug — **Fixed**.
- **Response**: Qualified `::environ` at global scope in `runtime/core/os.hpp` and declared `extern "C" char** environ;` at file scope before `namespace cfire::os`, preventing any collision with user variables or parameters named `environ`.

### `CF-042` — `core:cli` Exception Aborts (`std::stoi`), Positional Args, Subcommands & Password Prompts
- **Classification**: Existing Library Insufficient — **Overhauled**.
- **Response**: Overhauled `runtime/core/cli.hpp`:
  1. Replaced `std::stoi` and `std::stod` (which threw uncaught C++ exceptions and called `std::terminate` on invalid user input) with `cfire::detail::parse_int<T>` and `cfire::detail::parse_float<T>`, returning a clean `cfire::Error`.
  2. Added zero-arg `cli.parse[T]()` and `cli.try_parse[T]() -> T?` reading from `os.args()` automatically.
  3. Added `cli.args() -> []str`, `cli.subcommand() -> str`, `cli.positional() -> []str`, `cli.prompt(msg: str) -> str?`, and `cli.prompt_password(msg: str) -> str?` (using `<termios.h>` `ECHO` suppression on POSIX TTYs).

### `CF-043` — Terminal Raw Mode, Window Size, Safe File Descriptors (`fd_read`/`fd_write`/`fd_close`), `pipe`, `socketpair`, & Signal Handling in `core:os`
- **Classification**: Confirmed `core:os` Gap — **Implemented**.
- **Response**: Added to `runtime/core/os.hpp`:
  - `os.STDIN` (`0`), `os.STDOUT` (`1`), `os.STDERR` (`2`) and `os.is_tty(fd: i32 = os.STDIN) -> bool`
  - `os.fd_read(fd: i32, mut buf: []byte) -> usize!`, `os.fd_read_bytes(fd: i32, max_bytes: usize = 4096) -> []byte!`, `os.fd_write(fd: i32, data: []byte | str) -> usize!`, and `os.fd_close(fd: i32) -> void!` (safe, `EINTR`-resilient POSIX file descriptor operations)
  - `os.pipe() -> []i32!` and `os.socketpair() -> []i32!` (`FD_CLOEXEC`-enabled descriptors for subprocess pipes and self-pipe / shutdown signaling)
  - `os.term_size(fd: i32 = 0) -> TermSize` (returns `TermSize{cols, rows}` via `ioctl(TIOCGWINSZ)`)
  - `os.make_raw(fd: i32 = 0) -> RawTerminal!` (RAII guard that puts `fd` into raw mode and restores original `termios` on `.restore()` or scope exit)
  - `os.poll_fd(fd: i32, timeout_ms: i64 = 0) -> bool` (`EINTR`-safe readability check via `poll(2)`)
  - `os.on_signal(signum: i32, handler: fn(i32))` (safe signal callback registration for `SIGWINCH`, `SIGINT`, `SIGTERM`)
  - `f.fd() -> i32` on `fs::File` and `conn.fd() -> i32` + `conn.read_exact(n: usize) -> []byte!` on `net::TcpConn`.

### `CF-044` — `assert(slice == [a, b])` Initializer List Type Deduction Failure
- **Classification**: Confirmed Prelude Bug — **Fixed**.
- **Response**: Added an overload of `cfire::assert_binop_at` in `runtime/cfire_prelude.hpp` accepting `const std::vector<L>& lhs` and `std::initializer_list<Elem> rhs_init`, so `assert(items == ["a", "b"])` and `assert(nums == [1, 2, 3])` deduce types cleanly.

### `CF-045` — Monospace System Font Enumeration
- **Classification**: Confirmed `ext:webui` Feature Gap — **Implemented**.
- **Response**: Added `app.monospace_fonts() -> []str` (and `webui.monospace_fonts() -> []str`) in `runtime/core/webui.hpp`, querying `[NSFontManager sharedFontManager] availableFontNamesWithTraits:NSFixedPitchFontMask` on macOS, `fc-list :spacing=mono family` (`fontconfig`) on Linux, and `EnumFontFamiliesExA` (`FIXED_PITCH`) on Windows, with deduplicated fallbacks.

### `CF-046` — Asynchronous Non-Blocking RPC Handlers in `ext:webui`
- **Classification**: Confirmed `ext:webui` Architecture Gap — **Implemented**.
- **Response**: Added `app.bind_async[ReqStruct](name) |req| { ... }` and `app.bind_action_async(name) || { ... }` in `runtime/core/webui.hpp`. Unlike synchronous `app.bind` (which runs on the WebKit `didReceiveScriptMessage` callback thread), `bind_async` dispatches the handler on a worker thread and resolves `window.__cfire_rpc_resolve(id, ...)` via `dispatch_async(dispatch_get_main_queue(), ...)` when finished, keeping the UI at 60fps during slow SSH connects or vault KDF operations.

### `CF-048` — `or` Recovery Block Inside Reassignment (`x = fallible() or err { ... }`) & Parameterless `or { ... }` on `T?`
- **Classification**: Confirmed Parser, Sema & Codegen Bug — **Fixed**.
- **Response**:
  1. Updated `AssignStmt` in `src/codegen.hpp` to detect when `s.value` is an `OrExpr` with a `handler_body` (previously only `VarDeclStmt` handled `OrExpr` blocks!), emitting the temporary `_res` check, executing `handler_body` on error, and assigning `lhs = std::move(*_res)` on success.
  2. Added parameterless `or { ... }` blocks for both `T?` (`std::optional<T>`) and `T!` (`std::expected<T, cfire::Error>`), and enforced `cfire/OPTIONAL-OR-ERR` when `or err { ... }` is written on an optional `T?`.

### `CF-049` — Untagged Structs in `ext:json` & `ext:webui`
- **Classification**: Confirmed Sema & Codegen Bug — **Fixed**.
- **Response**: Updated `src/sema.hpp` and `src/codegen.hpp` so **all** structs in a program are registered in `cf.struct_tags` and receive generated `json::parse_struct<T>`, `json::stringify_struct<T>`, and `json::schema_struct<T>` specializations whenever `ext:json` or `ext:webui` is imported. Fields without explicit `` `json:"..."` `` tags automatically serialize and deserialize using their `.cf` field name, while fields tagged `` `json:"-"` `` are skipped.

### `CF-050` — WebKit Background Timer & App Nap Throttling
- **Classification**: Confirmed `ext:webui` Desktop Gap — **Fixed**.
- **Response**: In `runtime/core/webui/macos_ios.hpp` (both `run` and `run_webview_test`), `NSProcessInfo` begins an activity with `NSActivityUserInitiatedAllowingIdleSystemSleep | NSActivityLatencyCritical`, and `WKWebViewConfiguration.preferences` sets `inactiveSchedulingPolicy = 0` (`WKInactiveSchedulingPolicyNone` on macOS 14+), preventing macOS App Nap and WebKit from throttling background timers and SSH keepalives when the window is occluded or on another Space.

### `CF-051` — Standard macOS Menu Bar, Keyboard Shortcuts (`Cmd+C/V/X/A/Z/Q/W`) & Custom Menu Items in `ext:webui`
- **Classification**: Confirmed `ext:webui` Desktop Bug — **Fixed**.
- **Response**:
  1. `runtime/core/webui/macos_ios.hpp` populates `[NSApp setMainMenu:...]` with standard Application (`Hide`, `Quit` wired to `performClose:` so `windowShouldClose:` runs before exit), Edit (`Undo`, `Redo`, `Cut`, `Copy`, `Paste`, `Select All`), and Window (`Minimize`, `Close Window`) menus wired to the Cocoa responder chain.
  2. Added `app.add_menu_item(menu: str, title: str, shortcut: str, event: str)` to register custom native menu bar items (e.g. `"Shell" -> "New Tab"` with `"t"`) that emit `event` to `app.on(event)`.

### `CF-052` — macOS `.app` Icon Generation (`.png`/`.svg`), Runtime Window Icon, `UrlSchemes` & `SignIdentity` in `build.cf`
- **Classification**: Confirmed Build System & `ext:webui` Gap — **Fixed**.
- **Response**:
  1. `ensure_macos_icns` in `src/buildsys.hpp` now supports `.svg` source icons (converting via `rsvg-convert` or `qlmanage -t -s 1024` before `sips` + `iconutil`) as well as `.png` and `.icns`.
  2. Added `app.set_icon(path: str)` in `ext:webui` (and automatic `-DCFIRE_APP_ICON_PATH` wiring from `Icon` in `build.cf`) to set `[NSApp setApplicationIconImage:]` on macOS, `gtk_window_set_icon_from_file` on Linux, and `WM_SETICON` on Windows even during unbundled `cfire run`.
  3. Added `UrlSchemes: ["ssh"]` (generating `CFBundleURLTypes` in `Info.plist`) and `SignIdentity: "Developer ID Application: ..."` in `build.cf`.

### `CF-053` — Native OS Clipboard Access & Concealed Auto-Clearing Secrets (`clipboard_set_secret`)
- **Classification**: Confirmed `ext:webui` Feature Gap — **Implemented**.
- **Response**: Added `app.clipboard_set(text: str, concealed: bool = false, clear_after_ms: i64 = 0)`, `app.clipboard_set_secret(text: str, clear_after_ms: i64 = 30000)`, `app.clipboard_get() -> str`, and `app.is_clipboard_concealed() -> bool` in `runtime/core/webui.hpp`. When `concealed` is `true`, `NSPasteboard` sets `org.nspasteboard.ConcealedType` / `org.nspasteboard.TransientType` on macOS (and `ExcludeClipboardContentFromMonitorProcessing` on Windows) so clipboard managers ignore the secret, and when `clear_after_ms > 0`, a background timer clears the clipboard only if `changeCount` / generation is unchanged.

---

## 4. C++ Shims You Can Now Delete in Flint

With these changes in `cfire`, you can delete the following workarounds in the Flint repository:
1. **Delete your custom Objective-C / `libsecret` keychain C++ blocks** $\rightarrow$ replace with `import "ext:keychain"` (`keychain.set`, `keychain.get`, `keychain.delete_password`).
2. **Delete your custom `libsodium` / PBKDF2 / AES nonce-prepending C++ wrappers** $\rightarrow$ replace with `import "ext:crypto/sodium"` (`sodium.argon2id`, `sodium.xchacha20poly1305_encrypt`, `sodium.xchacha20poly1305_decrypt`) or `aes.encrypt` / `aes.decrypt`.
3. **Delete your SQLCipher `sqlite3_key` C++ shim** $\rightarrow$ replace with `sqlite.open_encrypted(path, key)` and pass `[]byte` slices directly to `db.exec` / `db.query`.
4. **Delete your custom `NSPasteboard`, `NSFontManager`, and `NSApp.mainMenu` Objective-C++ blocks** $\rightarrow$ `Cmd+C/V/X/A/Z/Q/W` and App Nap prevention are now automatic in `app.run()`, and clipboard/fonts are exposed via `app.clipboard_set`, `app.clipboard_get`, and `app.monospace_fonts()`.
5. **Delete your `<termios.h>`, `TIOCGWINSZ`, `poll`, and `socketpair` `cpp {}` blocks** $\rightarrow$ replace with `os.make_raw`, `os.term_size`, `os.poll_fd`, `os.socketpair`, and `os.on_signal`.
6. **Delete your custom `libssh` C FFI shims** $\rightarrow$ replace with `import "ext:net/ssh"` (`ssh.dial`, `sess.host_key_fingerprint`, `sess.auth_publickey_mem`, `sess.open_pty`, `sess.exec`, `sess.sftp_*`, `sess.open_forward`).
7. **Delete dummy `` `json:"..."` `` tags on internal RPC structs** $\rightarrow$ untagged structs and `T?` optional fields now serialize and deserialize automatically in `ext:json` and `ext:webui`.

# Flint Downstream Backlog

Topic-focused backlog (like [`backlog-build-distribution.md`](./backlog-build-distribution.md)) for the work `cfire` needs so that Flint (`github.com/samaniegoluis/flint`) can move off its 0.10.0 pin (`c5b1d4b`, which is `2d9daec` in today's history). Every item has been evaluated against `cfire`'s core design pillars, settled `Q1`–`Q61` decisions, and `main` @ `14a1a51` (2026-11-15), with owner design decisions resolved interactively as **`Q62` – `Q69`**.

- **IDs**: this file tracks **`CF-211` – `CF-239`** and ADRs **`Q62` – `Q69`**. **Future items anywhere in `docs/planning/` start at `CF-240` and `Q70`.** Where an item matches an entry in Flint's `docs/cfire-upstream.md`, it says so as *Flint ref*. Flint's own numbers from `CF-086` up collide with `cfire`'s, so cite this file's IDs.
- **Workflow**: as [`README.md`](./README.md) describes. An item moves to [`active.md`](./active.md) before work starts and is signed off there with the `make check-all` summary.
- **Checked at**: `main` @ `14a1a51` (2026-11-15), on Ubuntu 24.04, clang 18, libsodium, libssh 0.10.6, sqlcipher, webkit2gtk-4.1. Evidence tags: **[ran]** a program was built and run; **[read]** read in the source.
- **Hashes**: history was rewritten twice between 2026-11-09 and 2026-11-15. Hashes below are for today's `main`; each commit is also named by date and subject so it can be found again.

## Summary

| ID | P | Wave | Title | Flint ref | Evidence | Status |
| :--- | :---: | :---: | :--- | :--- | :---: | :--- |
| `CF-211` | **P0** | `F1` | `ext:net/ssh` fails open: a mock session without libssh, and the `ext:ssh` alias never links libssh (`Q62`) | — | [ran] | `[DONE]` |
| `CF-212` | **P0** | `F1` | `ext:net/ssh` does not compile against libssh | — | [ran] | `[DONE]` |
| `CF-213` | **P0** | `F1` | `ext:crypto/sodium` does not compile since `byte` became `u8` | — | [ran] | `[DONE]` |
| `CF-214` | **P0** | `F1` | CI gate: every `ext:` module compiles and links with its real library | — | [ran] | `[DONE]` |
| `CF-215` | **P0** | `F1` | `sodium.argon2id` & `argon2id_str`: documented 64 MiB vs runtime 4 MiB; argument order is guessed | `CF-090` | [ran] | `[DONE]` |
| `CF-216` | **P0** | `F1` | `cfire clean --cache` deletes whatever directory `CFIRE_CACHE_DIR` names (`Q63`) | — | [ran] | `[DONE]` |
| `CF-217` | **P1** | `F1` | Remaining shared or predictable temp and cache paths; two user-cache roots (`Q63`) | `CF-153` follow-up | [ran] | `[DONE]` |
| `CF-218` | **P1** | `F4` | webui: inspector and context menu on in release builds (macOS, Windows, Linux); no autofill opt-out (`Q69`) | `CF-122`, `CF-121` | [ran] | `[DONE]` |
| `CF-219` | **P0** | `F2` | A `[]T!` / `[]T?` function cannot return a slice | `CF-115` | [ran] | `[DONE]` |
| `CF-220` | **P0** | `F2` | `REDUNDANT-TRY` / `REDUNDANT-OR` fire on unresolved types; `cfire fix` deletes error handling | `CF-106`, `CF-117` | [ran] | `[DONE]` |
| `CF-221` | **P1** | `F2` | `cfire fix` applies the editor-only `PREFER-ENUM` rewrite | — | [ran] | `[DONE]` |
| `CF-222` | **P1** | `F2` | Qualified cross-package names in `match` arms and generic bounds | `CF-017` follow-up | [ran] | `[DONE]` |
| `CF-223` | **P1** | `F2` | Code the checker accepts that clang rejects or gets wrong (five repros + `0o` octal literals, `Q68`) | `CF-091`, `104`, `130`, `133`, `145` | [ran] | `[DONE]` |
| `CF-224` | **P2** | `F2` | Orthogonal language additions (`[T: A + B]`, `mut`/`move` interfaces, `match` guard bindings; no `.clone()`, `Q64`) | `CF-100`, `124`, `148` | [ran] | `[DONE]` |
| `CF-225` | **P1** | `F3` | `File.flush()` is a no-op; add `File.sync()` (`Q68`) | `CF-143` | [ran] | `[DONE]` |
| `CF-226` | **P1** | `F3` | `core:fs`: exclusive create, stat, realpath, error codes (`Q68`) | `CF-099`, `CF-136` | [ran] | `[DONE]` |
| `CF-227` | **P1** | `F3` | `host` on `http.Server` / `websocket.Server` / `serve_in_background` (`Q68`) | `CF-141` | [ran] | `[DONE]` |
| `CF-228` | **P2** | `F3` | HTTP client single end-to-end `timeout_ms` deadline (`Q68`) | `CF-146` | [ran] | `[DONE]` |
| `CF-229` | **P3** | `F3` | Local wall-clock time formatting (`Time.local()`, `Time.format()`, `Q68`) | `CF-095` | [ran] | `[DONE]` |
| `CF-230` | **P2** | `F3` | Cross-platform local IPC & peer credentials (`core:net/ipc`, `Q65`) | `CF-081` | [ran] | `[DONE]` |
| `CF-231` | **P3** | — | Multi-socket wait inside `async.scope` (`Q66`) | `CF-112` | [read] | `[EXCLUDED — see backlog.md §3]` |
| `CF-232` | **P3** | `F4` | webui general desktop fixes: Dock reopen, per-monitor DPI, full-screen events, drag & drop (`Q69`) | `CF-097`, `127`, `125`, `004` | [ran] | `[DONE]` |
| `CF-233` | **P3** | — | Fd-leak canary for `import cpp` packages (`Q67`) | `CF-089` | [read] | `[EXCLUDED — see backlog.md §3]` |
| `CF-234` | **P1** | `F5` | Documentation corrections that `CF-210` missed (exact list) | — | [ran] | `[DONE]` |
| `CF-235` | **P1** | `F5` | Mark the reversed decisions in `backlog.md` as superseded | — | [ran] | `[DONE]` |
| `CF-236` | **P2** | `F1`/`F5` | Delete dead references and leftover files (`INVALID-IMPORT` in `F1`; root files & Makefile clean in `F5`) | — | [ran] | `[DONE]` |
| `CF-237` | **P1** | `F1` | `cfire clean`: cover every artifact location, with safety rules & automatic legacy sweep (`Q63`) | — | [ran] | `[DONE]` |
| `CF-238` | **P2** | — | Flint as a downstream canary, and tags for downstream pins (`Q69`) | — | [ran] | `[EXCLUDED — see backlog.md §3]` |
| `CF-239` | **P1** | `F1` | MCP parity: `cfire_inspect` workspace `build.cf` flags/cache, `StdTypeDoc` + `builtin_methods` lookup, and canonical prefix note (`Q62`, `Q63`) | — | [ran] | `[DONE]` |

Fixed since the first pass (2026-11-14), no item needed:
- Generic receiver methods (Flint `CF-134`), fixed by `597fd87`.
- `cfire check` writing `.cfire_cache` into `runtime/`, and the shared Windows PCH directory, both now under the user cache (`dea2e46`, `597fd87`).
- The `Executable` field list in the catalog (`CF-210`).
- `install.sh` writing `runtime/VERSION`, now removed on install (`docs/install.sh:234`, `:336`).

---

## Architectural Decision Record (`Q62` – `Q69`, resolved interactively 2026-11-15 / 2026-11-16)

62. **One Canonical Import Path & Fail-Closed `ext:net/ssh` (`Q62`, resolves `D5`, `D7`, `CF-211`, `CF-236`)**:
    - Delete all non-canonical `m.aliases` (`ext:ssh`, `ext:sodium`, `ext:sqlite`, `ext:zstd`, `ext:compress`, `ext:ai/*`) from `src/docgen/stdlib_catalog_entries.hpp` and route them through `canonical_import_for_wrong_prefix` (`src/module_registry.hpp`), so Sema emits the hard error **`cfire/INVALID-IMPORT`** with a machine-applicable `cfire fix` rewrite (`Q16` — no warning or dual-API period). There is strictly one canonical import path per stdlib module across Sema, `buildsys`, LSP, and `docgen`.
    - Outside `CFIRE_TEST_MODE` (`cfire build`, `cfire run`), importing `ext:net/ssh` without `libssh` is a compile/build error (`buildsys.hpp` + `#error` in `runtime/core/ssh.hpp`), matching `ext:crypto/sodium`. Under `CFIRE_TEST_MODE` (`cfire test` and `cfire`'s C++ unit suites), compilation succeeds without `libssh`, **but** `is_mock_mode()` returns `true` *only* when `ssh.enable_mock_server()` or `CFIRE_HEADLESS_TEST=1` was explicitly requested; otherwise `ssh.connect` fails closed with `Err`.

63. **Single OS-Native / XDG User Cache Root & Marker-Guarded `cfire clean` (`Q63`, resolves `D6`, `CF-216`, `CF-217`, `CF-237`)**:
    - Unify `user_cache_root_dir()` (`src/build/cache_paths.hpp`) and `cfire::detail::user_cache_dir()` (`runtime/cfire_prelude.hpp`) on a single precedence chain:
      `CFIRE_CACHE_DIR` > `$XDG_CACHE_HOME/cfire` > OS-native user cache (`~/Library/Caches/cfire` on macOS, `~/.cache/cfire` on Linux, `%LOCALAPPDATA%\cfire\cache` on Windows) > per-process `0700` `SecureTempDir`.
    - `~/.cfire/` holds only durable user configuration and installed toolchain state (`config.json`, `bin/`, `runtime/`). Header-AST caches (`mcp`, `inspect`) write to `<project_root>/.cfire/cache` inside a workspace or the user cache root otherwise—never `<toolchain>/.cfire/cache`.
    - Initializing a cache root writes a `.cfire-cache` marker (`0700` dir). `cfire clean [--cache] [--all] [--dry-run] [--json] [path]` refuses `/`, `$HOME`, symlinks, and project/toolchain ancestors, requires `.cfire-cache` on non-empty `--cache` roots, deletes only known cache subdirectories + `probes.json`, and automatically sweeps owned legacy cache locations (`~/.cfire/cache`, `<runtime>/.cfire_cache`, owned `$TMP/cfire_*` dirs) under `--cache`/`--all` without adding a permanent `--legacy` CLI flag.

64. **Orthogonal Language Extensions — `+` Bounds, `mut`/`move` Interfaces, Guard Bindings; No `.clone()` (`Q64`, resolves `D4`, `CF-224`)**:
    - Extend generics with compound bounds `[T: A + B]` (including qualified `pkg.Bound` from `CF-222`), allow `mut` and `move` parameter/receiver intents in `interface` declarations, and allow pattern bindings inside `match` `if` guards (`Some(x)`, `Ok(x)`, `Err(e)`, `Enum::V(x)`, `[h, ..rest]`, plus bare `x` on **non-enum** scrutinees so misspelled unqualified enum variants stay caught as `UNKNOWN-VARIANT` per `Q13`/`CF-110`).
    - **Exclude `[]T.clone()`**: preserve `Q14`/`Q36`/`Q53` single-way value semantics (`mut copy := xs` creates an owned copy; views copy automatically when stored or returned), and add an idiomatic `cfire/UNKNOWN-METHOD` hint when `.clone()` is called.

65. **Cross-Platform Local IPC (`core:net/ipc`) (`Q65`, resolves `D1`, promotes `CF-081` as `CF-230`)**:
    - Promote `CF-081` into a new zero-setup standard library module `import "core:net/ipc"` (`runtime/core/net/ipc.hpp`), bringing the catalog to **36 modules (`30 core:* + 6 ext:*`)**.
    - Expose pure `Q29` verbs `ipc.listen(endpoint: str) -> ipc.Listener!`, `ipc.connect(endpoint: str, timeout_ms: i32 = 5000) -> ipc.Conn!`, `ipc.endpoint(name: str) -> str`, full `CF-089` `_bytes`/`_text` stream methods on `ipc.Conn`, and `conn.peer_credentials() -> ipc.PeerCred!` (`PeerCred { pid: i64, same_user: bool, uid: i64 }`).
    - Backed by the platform-standard local IPC mechanism on each OS: `0600` `AF_UNIX` `SOCK_STREAM` sockets (`SO_PEERCRED` on Linux, `getpeereid` + `LOCAL_PEERPID` on macOS, socket unlink on `Listener.close()`) on POSIX, and overlapped Win32 Named Pipes (`\\.\pipe\...`, `PIPE_TYPE_BYTE | PIPE_REJECT_REMOTE_CLIENTS`, current-user `TokenUser` SID + `LocalSystem` DACL, `GetNamedPipeClientProcessId` + client process `TokenUser` SID comparison for `same_user`) on Windows.

66. **Exclude Multi-Socket `net.wait_readable` (`Q66`, resolves `D2`, excludes `CF-231`)**:
    - Keep `net::detail::Poller` internal to `http.Server` and record `CF-231` in `backlog.md` §3. `TcpConn` is move-only, TLS streams can buffer decrypted bytes in OpenSSL `SSL_pending`, and `async.scope` structured concurrency (`Q2`, `CF-026`, `CF-043`) handles multi-stream relays cleanly.

67. **Reaffirm `CF-018` `import cpp` FD-Canary Exemption (`Q67`, resolves `D3`, excludes `CF-233`)**:
    - Keep `CF-018` / `CF-080` unchanged: `import cpp` test suites remain automatically exempt from the per-test FD-leak canary with zero test-lifecycle hooks or extra CLI flags. Record `CF-233` in `backlog.md` §3.

68. **Lean Cross-Platform Stdlib Surface & `0o` Octal Literals (`Q68`, refines `CF-223`, `CF-225`–`CF-229`)**:
    - **Octal literals**: Lower `0o...` integer literals (`0o600` → C++ `0600`) in Sema and codegen (`CF-223`).
    - **`core:fs` (`CF-225`, `CF-226`)**: Add `File.sync() -> void!` (`F_FULLFSYNC`/`fsync`/`FlushFileBuffers`), fix stdio `File.flush()`, and add `fs.create_new(path: str, mode: u32 = 0o600) -> File!`, `fs.stat(path: str) -> FileInfo!`, `fs.realpath(path: str) -> str!`, and `fs.ERR_{EXISTS,NOT_FOUND,PERMISSION}` integer `Error.code()` constants (`CF-R02`). **Exclude** `fs.sync_dir`, `fs.symlink`, and `fs.read_link` due to Windows directory-handle and `CreateSymbolicLinkW` privilege divergence.
    - **`core:net/http` & `core:net/websocket` (`CF-227`, `CF-228`)**: Add `host: str = "0.0.0.0"` on `make(http.Server, ...)`, `make(websocket.Server, ...)`, and `http.serve_in_background(...)` (`Q29`/`Q31`); add a single end-to-end `timeout_ms: i64 = 30000` labeled parameter on `http.{get,post,request,post_sse}` (`Q30`, skipping a separate `connect_timeout_ms`).
    - **`core:time` (`CF-229`)**: Add Go-style offset-carrying `t.local() -> Time` and `t.utc() -> Time` (preserving instant-only `==`/`<=>`) plus CRT-safe C++ token formatting `t.format(pattern: str) -> str` (`%Y`, `%m`, `%d`, `%H`, `%M`, `%S`, `%z`, `%%`).

69. **Secure WebUI Release Defaults, Desktop Events & Downstream-Agnostic Releases (`Q69`, resolves `D8`, refines `CF-218` & `CF-232`, excludes `CF-238`)**:
    - **`ext:webui` (`CF-218`, `CF-232`)**: Default DevTools / Web Inspector and browser default context menu to **OFF** in `Release` (`cfire build`, `-DCFIRE_RELEASE_BUILD=1`) and **ON** in `DevDebug` (`cfire run`), with explicit `app.set_devtools(bool)`, `app.set_context_menu(bool)`, and `app.set_autofill(bool)`; ship macOS Dock reopen (`applicationShouldHandleReopen:hasVisibleWindows:`), Windows PerMonitorV2 HiDPI awareness, `"window:fullscreen"`, and `"window:file_drop"` (`CF-004`).
    - **Exclude `CF-238`**: `cfire` tags immutable SemVer releases only (`vX.Y.Z`), keeps commit messages downstream-agnostic (`AGENTS.md` §3), and captures regressions via in-repo `tests/conformance/` + `CF-214` `ext_smoke` gates rather than cloning external downstream repos in CI.

Resolved while checking: the root `Dockerfile` stays. It is `CF-205`'s static musl release builder (`backlog-build-distribution.md:140`).

---

## Wave Sequencing (`Waves F1–F5`)

| Wave | Goal | Items | Exit Gate |
| :---: | :--- | :--- | :--- |
| **F1** | `ext:*` fail-closed soundness, crypto contract, smoke CI, safe unified cache/clean & MCP parity | `CF-211` → `CF-212` → `CF-213` → `CF-215` → `CF-214`; `CF-236` (imports); `CF-216` + `CF-217` + `CF-237`; `CF-239` | Non-canonical imports emit `INVALID-IMPORT` + `cfire fix`; SSH fails closed without `libssh`; `ext_smoke` builds and passes against real libs on CI; `argon2id` / `argon2id_str` match 64 MiB catalog contract; single OS/XDG cache root with `.cfire-cache` guard and safe nested `cfire clean`; MCP `inspect` & `stdlib_lookup` have full parity with `CF-188` types, builtin methods, and `build.cf`; `make check-all` green. |
| **F2** | Compiler, Sema & codegen soundness + `Q64` language extensions | `CF-219` → `CF-220` → `CF-221` → `CF-222` → `CF-223` → `CF-224` | `[]T!` / `[]T?` returns compile; `cfire fix` never strips `try`/`or` on `Unknown` or runs editor-only `PREFER-ENUM`; `pkg.Enum::V` and `pkg.Bound`, `0o600`, NUL strings, nested/empty slices, `const` map lookup, `[T: A + B]`, `mut`/`move` interfaces, and guard bindings pass conformance + `make check-all`. |
| **F3** | Core stdlib (`fs`, `http`/`ws`, `time`) & cross-platform `core:net/ipc` | `CF-225` + `CF-226`; `CF-227` + `CF-228`; `CF-229`; `CF-230` | `File.sync`, `create_new`, `stat`, `realpath`, `ERR_*`, server `host`, HTTP client `timeout_ms`, `Time.local`/`format`, and `core:net/ipc` (`AF_UNIX` on POSIX + Named Pipes on Windows + `PeerCred`) pass unit, TSan, and Linux/Windows VM gates (`make check-all`). |
| **F4** | `ext:webui` release security defaults & desktop lifecycle events | `CF-218` → `CF-232` | Release binaries disable DevTools & context menus unless opted in; `set_autofill(false)` wired; Dock reopen, Windows PerMonitorV2 DPI, `window:fullscreen`, and `window:file_drop` verified across platforms + `make check-all`. |
| **F5** | Docs sync, path integrity gate & leftover repo cleanup | `CF-234` → `CF-236` (cleanup) | All 13 `CF-234` corrections + `30 core + 6 ext` module counts + GUIDE Ch11 C++ RAII wrapper recipe applied; root `CNAME`/`.nojekyll` removed; `Makefile clean` delegates to `cfire clean`; doc-path integrity test & `./cfire doc --check` + `make check-all` green. |

---

## P0: Security & Soundness

#### `CF-211` — `ext:net/ssh` fails open (`Q62`) `[DONE]`
- **Problem & Evidence** [ran]:
  1. **Without libssh, `ssh.connect` succeeds against an in-memory mock server with a made-up host key.**
     - `runtime/core/ssh.hpp:56-66`: `detail::is_mock_mode()` returns `true` whenever `!CFIRE_LIBSSH_ACTIVE`, in production builds too.
     - The header's own comment (`ssh.hpp:3-5`) limits the mock to `ssh.enable_mock_server()` or `CFIRE_HEADLESS_TEST=1`.
     - A probe compiled without `CFIRE_HAS_LIBSSH` connected to `192.0.2.1` (TEST-NET, which cannot be reached) and printed `connect ok=1`.
  2. **The `ext:ssh` alias never links libssh.**
     - `src/docgen/stdlib_catalog_entries.hpp:171` and `src/module_registry.hpp:51,60` accept `ext:ssh`, `core:ssh` and `core:net/ssh`.
     - But `src/buildsys.hpp:683` detects libssh only for `core:net/ssh`, `ext:net/ssh` and `core:ssh`.
     - With libssh-dev installed, `import "ext:ssh"` followed by `ssh.connect("192.0.2.1", user: "memo")` printed `connected?!`.
     - `cfire check` is silent for every alias.
- **Decision (`Q62`)**:
  - Fail closed on missing `libssh` in `cfire build` / `cfire run` (`buildsys.hpp` diagnostic + `#error` in `ssh.hpp` outside `CFIRE_TEST_MODE`), matching `5579cdb` (2026-11-07) for `ext:crypto/sodium`. In `CFIRE_TEST_MODE`, the mock activates **only** when `ssh.enable_mock_server()` or `CFIRE_HEADLESS_TEST=1` is set; otherwise `ssh.connect` returns `Err`.
  - Delete all non-canonical `m.aliases` (`Q16`) and emit `cfire/INVALID-IMPORT` with a `cfire fix` rewrite to the single canonical `ext:net/ssh` (and likewise across all other `ext:*` aliases).
- **Implementation**:
  1. Outside `CFIRE_TEST_MODE`, emit `-DCFIRE_USE_SSH=1` and make missing `libssh` a build/compile error naming the install command (`brew install libssh`, `apt install libssh-dev`, `pacman -S mingw-w64-clang-x86_64-libssh`).
  2. Restrict `is_mock_mode()` strictly to `mock_enabled_flag()` or (`defined(CFIRE_TEST_MODE)` && `CFIRE_HEADLESS_TEST=1`).
  3. Clear `m.aliases` in `src/docgen/stdlib_catalog_entries.hpp:146-180` and map `ext:ssh`, `ext:sodium`, `ext:sqlite`, `ext:zstd`, `ext:compress`, `ext:ai/*`, `ext:tako` in `src/module_registry.hpp:54-63` so Sema emits `cfire/INVALID-IMPORT` with `cfire fix` rewrites.
- **Tests**:
  - A non-test build importing `ext:net/ssh` without libssh fails at compile time.
  - A test-mode build without `libssh` and without `enable_mock_server()` returns `Err` from `ssh.connect`.
  - `import "ext:ssh"` fails `cfire check` with `cfire/INVALID-IMPORT` and rewrites via `cfire fix` to `import "ext:net/ssh"`.
  - `connect` to `192.0.2.1` returns an error within its timeout.
- **Acceptance Criteria**: No spelling of the SSH import yields a session that is not backed by libssh, except through explicit `enable_mock_server()` / `CFIRE_HEADLESS_TEST=1` under `CFIRE_TEST_MODE`.

#### `CF-212` — `ext:net/ssh` does not compile against libssh `[DONE]`
- **Problem & Evidence** [ran]: With libssh 0.10.6, every program importing `ext:net/ssh` fails in clang.
  - At `runtime/core/ssh.hpp:712`, the member `Session::sftp_mkdir` (`:702`) hides `::sftp_mkdir`: "too many arguments … did you mean `::sftp_mkdir`".
  - CI never installs `libssh-dev` (`.github/workflows/ci.yml:34,64`). The libssh path has not been compiled since the module was added (`283344c`, 2026-11-06).
- **Implementation**:
  - Qualify every libssh/sftp C call in `ssh.hpp` with `::`.
  - Install `libssh-dev` in the CI Linux jobs and `mingw-w64-clang-x86_64-libssh` in the Windows job (and ensure `libssh` is installed on the UTM VMs or checked appropriately).
- **Acceptance Criteria**: `CF-214`'s smoke program for `ext:net/ssh` builds and links against libssh on Linux, macOS and Windows CI.

#### `CF-213` — `ext:crypto/sodium` does not compile `[DONE]`
- **Problem & Evidence** [ran]: Any `.cf` program importing `ext:crypto/sodium` fails in clang.
  - Redefinition of `memzero` (`runtime/core/sodium.hpp:48`) and of `argon2id` (`:187`).
  - Redefinition of default arguments at `:200`, `:279` and `:326`.
  - The cause: "unify byte with u8" (`5869497`, 2026-11-12) made the `std::byte` and `uint8_t` overloads identical.
  - No `.cf` test imports the module.
- **Implementation**:
  - Delete the duplicate `std::byte` / `as_u8` overloads.
  - Check the other runtime headers for `std::span<const std::byte>` overloads that sit next to `std::span<const std::uint8_t>` ones.
- **Acceptance Criteria**: `CF-214` smoke programs cover every catalog function of `ext:crypto/sodium` and pass.

#### `CF-214` — CI gate: every `ext:` module with its real library `[DONE]`
- **Evidence / Status** [ran]: `tests/conformance/ext_smoke/{ai,zstd,sodium,sqlite,ssh,webui}.cf` added, covering every catalog entry (`test_ext_soundness_cache_and_clean`); Linux & Windows VM gates pass `190/190` (`0` skipped).
- **Problem & Evidence**: `CF-212` and `CF-213` reached `main` because no `.cf` program per `ext:` module is built with its real library. CI exercises only the mock and fallback paths.
- **Implementation**:
  - Add `tests/conformance/ext_smoke/<module>.cf`, one per canonical `ext:` module (`ext:ai`, `ext:compress/zstd`, `ext:crypto/sodium`, `ext:db/sqlite`, `ext:net/ssh`, `ext:webui`), each calling every catalog function at least once.
  - Run them in `make check` on every CI job that installs the library.
  - A file whose optional system library is absent reports **skipped**, never passed.
- **Acceptance Criteria**: Breaking any `ext:` module's real-library path fails `make check` on at least one CI platform.

#### `CF-215` — `sodium.argon2id` & `argon2id_str`: documented 64 MiB, runtime 4 MiB, guessed argument order `[DONE]`
- *Flint ref*: `CF-090`.
- **Problem & Evidence** [ran]:
  1. The catalog (`src/docgen/stdlib_catalog_entries.hpp:759-760`) documents `argon2id(password, salt, out_len = 32, opslimit = 2, memlimit = 67108864)` and `argon2id_str(password, opslimit = 2, memlimit = 67108864)`, with memory limits in **bytes** (64 MiB).
  2. The C++ overloads take `(…, ops_limit, mem_kib, key_len)` for `argon2id` and `(password, ops_limit = 2, mem_kib = 4096)` for `argon2id_str`, and their defaults pass `2u, 4096u`, which is **4 MiB** (`runtime/core/sodium.hpp:166`, `:184`, `:202`, `:220`, `:223-229`).
  3. `sodium.hpp:132-141` and `:229` "repair" the mismatch with guesses (`key_len > 1024 && 16 <= ops_limit <= 1024`, `if (mem_kib >= 1048576) mem_kib /= 1024`). Any other combination goes through misread. A KDF must never guess its parameters.
  4. Nothing can be observed from `.cf` code until `CF-213` lands.
- **Decision**: The catalog is the contract.
- **Implementation**:
  - Single canonical parameter order and byte units in `argon2id` and `argon2id_str` matching the catalog (`out_len = 32, opslimit = crypto_pwhash_OPSLIMIT_INTERACTIVE, memlimit = crypto_pwhash_MEMLIMIT_INTERACTIVE`). Delete both heuristic guessing branches.
  - Reject values below `crypto_pwhash_MEMLIMIT_MIN` or `crypto_pwhash_OPSLIMIT_MIN` with `Err`; do not clamp them.
- **Tests**: Output of `argon2id` must be byte-identical to direct `crypto_pwhash` calls for the defaults (64 MiB) and for 8, 64, and 256 MiB; `argon2id_str` PHC string header must report `m=65536` (64 MiB) by default.
- **Acceptance Criteria**: Documented calls match libsodium with identical parameters for both `argon2id` and `argon2id_str`.

#### `CF-216` — `cfire clean --cache` deletes whatever `CFIRE_CACHE_DIR` names (`Q63`) `[DONE]`
- **Problem & Evidence** [ran]:
  - `src/cli/build_commands.cpp:57-66` calls `remove_all(user_cache_root_dir())`.
  - `user_cache_root_dir()` (`src/build/cache_paths.hpp:115-121`) returns `CFIRE_CACHE_DIR` verbatim.
  - With `CFIRE_CACHE_DIR` pointing at a directory that held `Documents/thesis.txt`, `cfire clean --cache` printed "Cleaned … global cache … (3 entries removed)" and the directory was gone.
  - A typo such as `CFIRE_CACHE_DIR=$HOME` deletes the user's home directory.
- **Implementation**:
  - Cache root creation writes a `.cfire-cache` marker (`0700` dir).
  - `clean --cache` deletes only known subdirectories (`pch`, `bin`, `libs`, `headers`, `ast`, `pkg`, `doc`, `mcp`, `probes.json`), and only under a non-empty root that carries the `.cfire-cache` marker.
  - Never follows symlinks, and refuses `/`, `$HOME`, and any ancestor of the current project or toolchain.
- **Tests**:
  - A `CFIRE_CACHE_DIR` that holds a foreign file without `.cfire-cache`: exits non-zero (`1`) with the reason and removes nothing.
  - With `CFIRE_CACHE_DIR=$HOME`: refused.
- **Acceptance Criteria**: No value of `CFIRE_CACHE_DIR` can make `cfire clean` delete a file `cfire` did not create.

#### `CF-217` — Remaining shared or predictable temp and cache paths; two user-cache roots (`Q63`) `[DONE]`
- *Flint ref*: `CF-153` follow-up.
- **Problem & Evidence** [ran]: `CF-153`'s acceptance allows no predictable or shared temporary path. Left over:
  1. `src/mcp.hpp:697` and `src/cli/inspect_and_outline.hpp:260`: `cfire mcp` and `cfire inspect cpp …` write `<toolchain parent>/.cfire/cache`. That is the installed toolchain, which may be shared or read-only. The MCP path falls back to the user cache only when the write fails (`:700-702`).
  2. `src/doctor.hpp:137`: the fixed path `$TMP/cfire_doctor_probe.cpp`.
  3. `src/clang_bridge.hpp:721-724`: `$TMP/cfire_clang_ast_<hash>_<pid>_<seq>` is created with `create_directories`, not `mkdtemp`, and has no `0700`. The name is predictable, which leaves a symlink race.
  4. `src/build/cache_paths.hpp:130-135`: the fallback cache root `$TMP/cfire-<user>-cache` is likewise created with `create_directories`, with a predictable name and no permission check.
  5. There are two user-cache roots: `~/.cfire/cache` (`cache_paths.hpp:124`) and XDG / `Library/Caches` (`runtime/cfire_prelude.hpp:426-455`). `cfire clean --cache` clears only the first.
- **Decision (`Q63`)**: Standardize everywhere on the single OS-native / XDG user cache root (`CFIRE_CACHE_DIR` > `$XDG_CACHE_HOME/cfire` > `~/Library/Caches/cfire` on macOS, `~/.cache/cfire` on Linux, `%LOCALAPPDATA%\cfire\cache` on Windows > `0700` `SecureTempDir`), leaving `~/.cfire/` solely for `config.json`, `bin/`, and `runtime/`.
- **Implementation**:
  - Unify `cache_paths.hpp` and `cfire_prelude.hpp`'s `user_cache_dir()` on the single OS-native / XDG cache root with `SecureTempDir` fallback (never a predictable `$TMP/cfire-<user>-cache` name).
  - Header-inspection caches go to `<project>/.cfire/cache` inside a project, or `cache_subdir("headers")` when outside a project—never `<toolchain>/.cfire/cache`.
  - Probes (`doctor.hpp`) and AST work directories (`clang_bridge.hpp`) use `cfire::detail::SecureTempDir` (`mkdtemp`, `0700`).
  - Extend `CF-153`'s CI grep gate to flag `temp_directory_path(…) /` followed by a literal or `std::format`, and any path derived from `runtime_dir` that is written to.
- **Acceptance Criteria**:
  - `check`, `inspect`, `mcp` and `doctor` against a read-only toolchain directory write nothing there.
  - No `cfire_*` entries remain in `$TMP` after a test-suite run.

#### `CF-218` — webui: inspector and context menu on in release builds; no autofill opt-out (`Q69`) `[DONE]`
- *Flint ref*: `CF-122`, `CF-121`.
- **Problem & Evidence** [read]:
  - `runtime/core/webui/macos_ios.hpp:244` sets `developerExtrasEnabled = YES` for every app. It has done so since the first webui commit (`89345ad`, 2026-11-04), so this is a default, not a decision.
  - `windows_webview2.hpp:410-412` declares `put_AreDevToolsEnabled` and `put_AreDefaultContextMenusEnabled` but never calls them, so WebView2's defaults (on) apply.
  - Nothing lets a window opt out of autofill, which matters for secret fields.
- **Decision (`Q69`)**: Secure defaults (`CF-168`): DevTools and default browser context menu are OFF in `Release` (`cfire build`, `-DCFIRE_RELEASE_BUILD=1`) and ON in `DevDebug` (`cfire run`), with explicit `set_devtools`, `set_context_menu`, and `set_autofill` methods.
- **API Example**:
  ```cf
  import "ext:webui"

  mut app := make(webui.App, "Flint", 1080, 720)
  app.set_autofill(false)     // disable password/form autofill on secret fields
  app.set_devtools(false)     // explicit toggle (default: ON in DevDebug, OFF in Release)
  app.set_context_menu(false) // explicit toggle (default: ON in DevDebug, OFF in Release)
  ```
- **Implementation**:
  - Pass `-DCFIRE_RELEASE_BUILD=1` from `buildsys.hpp` when building under `BuildProfile::Release`.
  - Default `devtools_enabled` and `context_menu_enabled` in `AppState` to `!defined(CFIRE_RELEASE_BUILD)` and apply explicitly on WKWebView, WebKitGTK (`enable-developer-extras`, `context-menu` signal), and WebView2 (`put_AreDevToolsEnabled`, `put_AreDefaultContextMenusEnabled`).
  - Add `app.set_devtools(enabled: bool)`, `app.set_context_menu(enabled: bool)`, and `app.set_autofill(enabled: bool)` (WebView2 `ICoreWebView2Settings4::put_IsGeneralAutofillEnabled` / `put_IsPasswordAutosaveEnabled`, plus WebKit equivalents) across runtime, Sema, and catalog.
- **Acceptance Criteria**: A `cfire build` release binary exposes no web inspector and no default context menu on any platform unless it calls `set_devtools(true)` / `set_context_menu(true)`.

---

## P0–P2: Compiler Correctness

#### `CF-219` — A `[]T!` / `[]T?` function cannot return a slice `[DONE]`
- *Flint ref*: `CF-115`. It has reproduced on every commit since the trial at `634c8dd` (2026-11-10).
- **Problem & Evidence** [ran]: All of these fail with "return type mismatch: function 'f' expects '[]T!', got '[]T' (cfire/TYPE-MISMATCH)":
  ```cf
  fn f() -> []i64! { mut out: []i64 = []; out.push(1); return out }
  ```
  - Root cause in `src/sema/types.hpp:184`: `TypeTable::from_name` checks `n.starts_with("[]")` before `n.ends_with('!')` / `n.ends_with('?')`, parsing `"[]i64!"` as `Slice("i64!")` instead of `Fallible(Slice("i64"))` (affecting `[]T?` as well).
  - Fails for `[]i64`, `[]str`, `[]u8` and `[]Struct` alike. Scalar `T!` returns are fine.
  - Flint has about 50 unique sites.
- **Implementation**:
  - Guard `n.starts_with("[]")` in `TypeTable::from_name` (`src/sema/types.hpp:184`) with `&& !n.ends_with('?') && !n.ends_with('!')`, matching `map[K]V` and `[N]T`.
- **Acceptance Criteria**:
  - Conformance covers `[]scalar`, `[]str`, `[]struct`, `[][]T` and a returned sub-slice, from both `T!` and `T?` functions.
  - The repro compiles and runs.

#### `CF-220` — Redundancy diagnostics fire on unresolved types `[DONE]`
- *Flint ref*: `CF-106`, `CF-117`.
- **Problem & Evidence** [ran]:
  - The minimal cross-package case is clean (`ports` declares `Presence`, `app` has `fn confirm[P: Presence](p: P) -> bool! { ok := try p.presence_confirm(…) }`).
  - But when an imported package has type errors, every call with an unresolved type gets `REDUNDANT-TRY` / `REDUNDANT-OR`, and the fix-its remove the token.
  - `cfire check src/application` on Flint (with `src/domain` still failing) reports **396 `REDUNDANT-TRY` and 111 `REDUNDANT-OR`**. Each carries "fix: Remove redundant 'try '".
- **Decision**: Q16 requires every hard error to come with a fix machines can apply safely, and a diagnostic that needs a resolved type cannot be safe on an `Unknown` one.
- **Implementation**:
  - Skip `REDUNDANT-TRY` / `REDUNDANT-OR` in `src/sema/expression_checker_impl.hpp` when the operand's `TypeId` is `Unknown` or the file/callee package failed type/import resolution.
  - `cfire fix` (`src/cli/check_and_fix.hpp`) applies no token-removal fix-it to a file that also has an unresolved-type diagnostic.
- **Acceptance Criteria**: On a project that does not type-check, `cfire fix` never removes `try`, `or` or `!`.

#### `CF-221` — `cfire fix` applies `PREFER-ENUM` `[DONE]`
- **Problem & Evidence** [ran]:
  - `CF-123` defines `PREFER-ENUM` as an optional lint with an LSP code action.
  - Yet `cfire fix --dry-run` on `VAULT_V1/V2/V3: i64` constants applies "Convert 'VAULT_*' constants to enum" (8 edits).
  - On Flint it rewrote 144 constant groups whose values go to disk, onto the wire and into exit codes.
- **Implementation**:
  - Fix-its carry an applicability: `MachineApplicable` (applied by `cfire fix`) or `EditorOnly` (only an LSP code action, or `cfire fix --only <code>`).
  - Mark `PREFER-ENUM` as `EditorOnly` and audit every other fix-it in `src/diagnostics_registry_impl.hpp` against Q16.
- **Acceptance Criteria**: A plain `cfire fix` never changes how a value is represented.

#### `CF-222` — Qualified cross-package names in `match` arms and generic bounds `[DONE]`
- **Problem & Evidence** [ran]:
  1. `match m { p.Mode::A => … }` is rejected with "'.' is not a valid separator in pattern matching; use '::' (e.g. 'p::Mode')" (`src/parser_impl.hpp:1580`). `p::Mode::A` does not parse either. Only the unqualified `Mode::A` works.
  2. `fn f[P: ports.Presence](…)` does not parse ("expected 'IDENT', got '.'"). Only the unqualified bound works.
- **Decision**: Q13 names `pkg.Enum::Variant` and `pkg.Type` as canonical. Patterns may also accept unqualified variants.
- **Implementation**:
  - Accept `Ident.Ident::Ident` in `parse_single_pattern` (`src/parser_impl.hpp:1575`) and `Ident.Ident` in generic bounds (`:436`, `:838`).
  - Suggest `Type::Variant` only for a two-segment `Type.Variant`.
- **Acceptance Criteria**: Conformance covers a cross-package enum in `match` arms and guards, and a cross-package interface bound.

#### `CF-223` — Code the checker accepts that clang rejects or gets wrong (`0o` octal literals, NUL strings, const map lookup, nested/empty slice literals) (`Q68`) `[DONE]`
- *Flint ref*: `CF-091`, `104`, `130`, `133`, `145`.
- **Problem & Evidence** [ran at `14a1a51`]:
  - `need := [["host", "list"], ["exec", "web"]]` fails in clang (`std::vector` construction).
  - `"\0".len()` is `0`, and `"a\u{0}b".len()` is `1`: string literals are truncated at NUL.
  - `fn get(m: map[str]i64, k: str) -> i64 { return m[k] }` fails in clang: `cfire::Map::operator[]` is not `const` (`runtime/cfire_prelude.hpp:640`).
  - `fn f() -> ([]str, i64) { return ([], 0) }` fails in clang (`make_tuple` from an empty literal).
  - `0o600` lexes in `src/lexer_impl.hpp:206-216` as `TokenKind::Int("0o600")`, then emits invalid C++ `0o600` instead of `0600` (`Q68`).
- **Decision**: `CF-108` and `CF-190` promise that code the checker accepts compiles in clang.
- **Implementation**:
  - Lower `0o...` / `0O...` integer tokens to C++ octal `0...` in codegen and parse base-8 values in Sema range checks.
  - Emit string literals containing embedded `\0` with an explicit byte length (`std::string("...", N)` / `std::string_view("...", N)`).
  - Type an empty `[]` from its expected return/tuple context and lower nested slice literals with explicit element types (`std::vector<std::vector<std::string>>`).
  - Add `const` `cfire::Map::operator[]` through `find()`, panicking with location on a missing key just as checked indexing requires.
- **Acceptance Criteria**: All five cases compile, run, and print the right values, with one conformance file each.

#### `CF-224` — Orthogonal language additions (`[T: A + B]`, `mut`/`move` interfaces, `D4` guard bindings; no `.clone()`) (`Q64`) `[DONE]`
- *Flint ref*: `CF-100`, `124`, `148`.
- **Problem & Evidence** [ran]:
  - `[X: A + B]` does not parse.
  - `mut` parameters cannot be written in interface methods ("expected type name, got 'mut'").
  - A guard cannot see its arm's binding (`D4`); only `_ if <cond>` works today (`parse_single_pattern` / `call_and_match_checker_impl.hpp:731-768`).
  - *(Excluded per `Q64`: `[]T.clone()` — `cfire` uses `mut copy := xs` and automatic view-to-owner copies; `.clone()` stays out and gets an idiomatic `UNKNOWN-METHOD` hint.)*
- **Decision (`Q5` & `Q64`)**:
  - Extend `TypeParam` to `std::vector<std::string> constraints` (`A + B`), enforcing all bounds in Sema and lowering to C++20 `requires (A<T> && B<T>)`.
  - Allow `mut` and `move` parameter intents (and `fn mut` receiver methods) on `InterfaceMethodSig` in `ast.hpp`, `parser_impl.hpp`, Sema, and concept lowering.
  - Allow guard bindings on `Some(x)`, `Ok(x)`, `Err(e)`, `Enum::V(x)`, `[h, ..rest]` (checking `arm.guard` on slice arms too), and bare `x` on **non-enum** scrutinees (`match n { x if x > 3 => ... }`), keeping guarded arms non-exhaustive and keeping misspelled enum variants rejected (`Q13`/`CF-110`).
- **Acceptance Criteria**: Each accepted extension has conformance tests and GUIDE docs; `xs.clone()` reports `cfire/UNKNOWN-METHOD` with an idiomatic copy hint.

---

## P1–P3: Standard Library

#### `CF-225` — `File.flush()` is a no-op; add `File.sync()` (`Q68`) `[DONE]`
- *Flint ref*: `CF-143`.
- **Problem & Evidence** [read]: `runtime/core/fs.hpp:405-408` checks `fd_ >= 0` and returns success. Nothing in the API forces data to disk.
- **Implementation**:
  - `File.flush()`: call `std::fflush(stdout)` / `std::fflush(stderr)` when `fd_` is `1` or `2`, and document in the catalog that `fs.File` is an unbuffered OS descriptor (`sync()` flushes OS buffers to storage).
  - Add `File.sync() -> void!` (`fcntl(fd_, F_FULLFSYNC)` falling back to `fsync` on macOS, `fsync` on Linux, `FlushFileBuffers` on Windows). *(Excluded per `Q68`: `fs.sync_dir`.)*
- **Acceptance Criteria**: `sync()` issues a real OS disk sync and surfaces errors cleanly across POSIX and Windows.

#### `CF-226` — `core:fs`: exclusive create, stat, realpath, error codes (`Q68`) `[DONE]`
- *Flint ref*: `CF-099`, `CF-136`.
- **Decision (`Q68` & `CF-R02`)**:
  - The runtime relies on exclusive create internally (`SecureTempDir`, `CF-153`), so `.cf` code gets the cross-platform subset that behaves identically without elevated privileges on macOS, Linux, and Windows.
  - Errors stay the existing `Error` with an integer code (`CF-R02` rejected typed error enums).
  - *(Excluded per `Q68`: `fs.symlink`, `fs.read_link`, `fs.sync_dir`; `write_atomic` remains excluded per `CF-054`.)*
- **API Example**:
  ```cf
  import "core:fs"

  mut lock := fs.create_new("vault.lock", 0o600) or err {
      if err.code() == fs.ERR_EXISTS {
          return error("vault is already locked")
      }
      return err
  }
  defer lock.close()
  _ = try lock.write_text("pid=1234\n")
  try lock.sync()

  real := try fs.realpath("./keys/../keys/id_ed25519")
  info := try fs.stat(real) // FileInfo { size: u64, mtime_ms: i64, mode: u32, is_dir: bool, is_symlink: bool }
  ```
- **Implementation**:
  - `fs.create_new(path: str, mode: u32 = 0o600) -> File!` (`O_CREAT | O_EXCL` / `CREATE_NEW`).
  - `fs.stat(path: str) -> FileInfo!` (`size`, `mtime_ms`, `mode`, `is_dir`, `is_symlink`).
  - `fs.realpath(path: str) -> str!` (`::realpath` / `_wfullpath` + existence check).
  - Integer `Error.code()` constants `fs.ERR_EXISTS`, `fs.ERR_NOT_FOUND`, and `fs.ERR_PERMISSION`.
- **Acceptance Criteria**: All exist on POSIX and Windows with conformance tests and GUIDE rows.

#### `CF-227` — `host` on `http.Server`, `websocket.Server` & `serve_in_background` (`Q68`) `[DONE]`
- *Flint ref*: `CF-141`.
- **Problem & Evidence** [read]:
  - `make(port: u16 = 0, tls: TlsConfig = {})` (`stdlib_catalog_entries.hpp:471`, `:524`) always listens on every interface, dual-stack since `05b7834` (2026-11-12).
  - `net.listen` already takes `host` (Q29).
- **Decision**: Q29 and 0.1 #6 keep `0.0.0.0` as the default with an opt-in restriction:
  ```cf
  mut srv := try make(http.Server, 8080, host: "127.0.0.1")
  mut ws := try make(websocket.Server, 8081, host: "127.0.0.1")
  mut bg := try http.serve_in_background(0, router, host: "127.0.0.1")
  ```
- **Acceptance Criteria**: Binding `127.0.0.1` binds loopback only and refuses non-loopback interface connections (verified in unit & VM gates).

#### `CF-228` — HTTP client end-to-end deadline (`timeout_ms`) (`Q68`) `[DONE]`
- *Flint ref*: `CF-146`.
- **Problem & Evidence** [read]: `http.request` takes headers and returns response headers, but has no timeout.
- **Decision (`Q30` & `Q68`)**: Single end-to-end labeled parameter `timeout_ms: i64 = 30000` (`0` disables) on `http.get`, `http.post`, `http.request`, and `http.post_sse`, applied via `steady_clock` budget across TCP `connect` (`poll`), TLS handshake, send, and body read (skipping a separate `connect_timeout_ms`).
  ```cf
  resp := try http.get("https://vault.example.com/health", timeout_ms: 5000)
  ```
- **Acceptance Criteria**: A request to a blackhole/stalled peer fails with `Err` within `timeout_ms`.

#### `CF-229` — Local wall-clock time formatting (`Q68`) `[DONE]`
- *Flint ref*: `CF-095`.
- **Problem & Evidence** [read]: `core:time` formats only UTC RFC 3339.
- **Decision (`Q7` & `Q68`)**:
  ```cf
  import "core:time"

  t := time.now()
  loc := t.local()
  println(loc.format_rfc3339())            // "2026-11-15T21:04:00-05:00"
  println(loc.format("%Y-%m-%d %H:%M:%S")) // "2026-11-15 21:04:00"
  ```
- **Implementation**: Add `offset_sec_`/`is_local_` to `cfire::time::Time` while keeping `==` / `<=>` instant-only; add `t.local() -> Time` (via `localtime_r` / `localtime_s`), `t.utc() -> Time`, `t.format(pattern: str) -> str` (CRT-safe C++ tokenizer supporting `%Y`, `%m`, `%d`, `%H`, `%M`, `%S`, `%z`, `%%`), and `t.format_rfc3339()` offset rendering.

#### `CF-230` — Cross-platform local IPC & peer credentials (`core:net/ipc`, `Q65`, promotes `CF-081`) `[DONE]`
- *Flint ref*: `CF-081`.
- **Decision (`Q65`)**:
  ```cf
  import "core:net/ipc"

  ep := ipc.endpoint("flint-agent")
  mut ln := try ipc.listen(ep)
  defer ln.close()

  mut conn := try ln.accept()
  defer conn.close()
  cred := try conn.peer_credentials() // PeerCred { pid: i64, same_user: bool, uid: i64 }
  if !cred.same_user {
      return error("unauthorized local IPC peer")
  }
  ```
- **Implementation**:
  - Register `core:net/ipc` (`runtime/core/net/ipc.hpp`) in catalog, module registry, and Sema (`30 core:* + 6 ext:* = 36` modules).
  - Pure `Q29` verbs: `ipc.endpoint(name: str) -> str`, `ipc.listen(endpoint: str) -> ipc.Listener!`, `ipc.connect(endpoint: str, timeout_ms: i32 = 5000) -> ipc.Conn!`, full `CF-089` `_bytes`/`_text` stream methods on `ipc.Conn`, and `conn.peer_credentials() -> ipc.PeerCred!`.
  - **POSIX (macOS & Linux)**: `0600` `AF_UNIX` `SOCK_STREAM` (`$XDG_RUNTIME_DIR/<name>.sock` / macOS `$TMPDIR/<name>.sock`; explicit socket paths like `$SSH_AUTH_SOCK` work verbatim), socket unlink on `Listener.close()`, `SO_PEERCRED` (Linux) / `getpeereid` + `LOCAL_PEERPID` (macOS).
  - **Windows**: Overlapped Win32 Named Pipes (`\\.\pipe\<name>-<user-sid>`, explicit `\\.\pipe\...` paths like `\\.\pipe\openssh-ssh-agent` work verbatim), `PIPE_TYPE_BYTE | PIPE_REJECT_REMOTE_CLIENTS | FILE_FLAG_OVERLAPPED`, `SECURITY_ATTRIBUTES` DACL locked to current user's `TokenUser` SID + `LocalSystem`, `GetNamedPipeClientProcessId` + `TokenUser` SID comparison for `same_user`.

#### `CF-231` — Multi-socket wait inside `async.scope` (`Q66`) `[EXCLUDED — see backlog.md §3]`
- *Flint ref*: `CF-112`.
- **Evidence** [read]: `runtime/core/net/poller.hpp` serves the HTTP event loop only. User code has a per-connection `poll`.
- **Disposition (`Q66`)**: Excluded in `backlog.md` §3. `TcpConn` is move-only, TLS `SSL_pending` can hold unread decrypted bytes in user space, and `async.scope` structured concurrency handles bidirectional stream relays cleanly.

#### `CF-232` — webui general desktop fixes (`Q69`) `[DONE]`
- *Flint ref*: `CF-097`, `127`, `125`, `004`.
- **API Example**:
  ```cf
  app.on("window:fullscreen") |payload| { /* {"fullscreen": true} */ }
  app.on("window:file_drop")  |payload| { /* {"paths": ["/path/to/file"]} */ }
  ```
- **Scope**:
  - Clicking the macOS Dock icon reopens a hidden window (`applicationShouldHandleReopen:hasVisibleWindows:`).
  - Per-monitor DPI awareness v2 on Windows (`SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)`).
  - Enter and exit full-screen events (`"window:fullscreen"`).
  - Real OS file drag-and-drop events (`"window:file_drop"` with native filesystem paths).
- **Acceptance Criteria**: Each is observed in unit/headless tests wherever observable offscreen and across platforms.

#### `CF-233` — fd-leak canary for `import cpp` packages (`Q67`) `[EXCLUDED — see backlog.md §3]`
- *Flint ref*: `CF-089`.
- **Disposition (`Q67`)**: Excluded in `backlog.md` §3, strictly reaffirming `CF-018` / `CF-080` (`import cpp` test suites remain automatically exempt with zero language hooks or extra CLI flags).

---

## P1–P2: Documentation & Hygiene

#### `CF-234` — Documentation corrections that `CF-210` missed `[DONE]`
These are the exact edits at `14a1a51` (plus `core:net/ipc` `30 core:* + 6 ext:* = 36` module count and GUIDE Ch11 C++ RAII move-only handle pattern). Then run `./cfire doc`; `docs/guide.html` and `docs/reference.html` are generated, and `cfire doc --check` must pass.

| # | File:line | Now | Change to |
| :---: | :--- | :--- | :--- |
| 1 | `docs/GUIDE.md:111` (table header) | "Optional System Library … Built-In Portable Fallback When Absent" | Split it: modules that are **vendored first** (`core:json`, `ext:db/sqlite`: `runtime/vendor/` amalgamations; the system library is used only with an explicit `PkgConfig`, link or prefix override, `src/buildsys.hpp:494-532`), and modules that **require** a system library. |
| 2 | `docs/GUIDE.md:116` (`core:keychain`) | pkg-config `libsecret-1`; "…Linux `secret-tool`, or AES-256-GCM fallback vault" | macOS `Security.framework`; Windows Credential Manager; Linux the `secret-tool` CLI (package `libsecret-tools`), with no pkg-config. **No fallback**: any other platform returns an error, and the in-memory store exists only under `CFIRE_TEST_MODE` (`runtime/core/keychain.hpp:27`, `:170-172`). |
| 3 | `docs/GUIDE.md:117` (`ext:db/sqlite`) | "System `libsqlite3`…" | "Vendored SQLite amalgamation (`runtime/vendor/sqlite3/`); system `libsqlite3` only on override; in-memory/WASM shim on `--web`". |
| 4 | `docs/GUIDE.md:119` (`ext:crypto/sodium`) | "Pure C++23 ChaCha20-Poly1305 / Ed25519 / Argon2 fallback" | "None. Without libsodium the build fails (`#error`, `runtime/core/sodium.hpp:19`)". The fallback was removed by `5579cdb` (2026-11-07). |
| 5 | `docs/GUIDE.md:120` (`ext:net/ssh`) | "OpenSSH `ssh` / `sftp` / `scp` subprocess bridge when `libssh` is absent" | After `CF-211`: "None. Without libssh the build fails (`#error`); `ssh.enable_mock_server()` is for tests only (`CFIRE_TEST_MODE`)". |
| 6 | `docs/GUIDE.md:384-397` (`build.Executable` reference) | Lists `Entry`, `Subsystem`, `Name`/`BundleId`, `Icon`, `Targets`/`WebProfile` and the builders; `Subsystem` values given as `"console"`/`"gui"` | Add `OutDir` (defaults to the project root), `Sources`, `Includes`, `Defines`, `Flags`, `PkgConfig`, `Libraries`, `Frameworks` (linked on Apple only, `src/buildsys.hpp:174-180`), `Portable`, `Static`, `Universal`, `UrlSchemes` and `SignIdentity`, matching the catalog (`stdlib_catalog_entries.hpp:222`). Use `"cli"`/`"gui"` consistently with the catalog. |
| 7 | `docs/GUIDE.md`, `build.cf` section (`:384`) | Platform-specific file names are undocumented | Document the suffixes `_darwin`/`_macos`, `_ios`, `_linux`, `_windows`, `_wasm` and `_unix` (`src/build/package_loader.hpp:96-101`, present since `283344c`): such a file compiles only for its platform. Warn that an existing `foo_linux.cf` is silently platform-only. |
| 8 | `src/docgen/stdlib_catalog_entries.hpp:478-482` (`Router`) | Lists `get`, `post`, `ws`, `dispatch`, `use` | Add `Router.put` and `Router.delete_` (`runtime/core/net/http.hpp:1278`, `:1282`; `GUIDE.md:1979` already lists them). |
| 9 | `docs/GUIDE.md:888` (match chapter) | No pattern guards or range patterns | Document `_ if cond =>`, guard bindings (`Q64`), `a..=b` / `a..b` range patterns, and `Some`/`none`/`Ok`/`Err` arms (`634c8dd`). |
| 10 | `README.md:122`, `:129` | `./cfire wasm build …`, `./cfire wasm serve …` | Delete: there is no `wasm` subcommand (`cfire --help`). Keep `cfire build --target=wasm` and `cfire serve`. |
| 11 | `README.md:125` | `./cfire run --ios examples/13-ios-native-ui` | Point at an existing example or drop it. `examples/13-*` was removed in `553c7d9` (2026-11-12). |
| 12 | `docs/SENIOR_ENGINEER_REVIEW_GUIDELINES.md:29`, `:113` | `docs/planning/architectural-backlog(.md)` | `docs/planning/backlog.md`; that file does not exist. |
| 13 | `docs/planning/backlog.md:416` | `conn.peer_addr()` | Fixed in Stage 1 (`conn.remote_addr()`, `stdlib_catalog_entries.hpp:446`). |
| 14 | `src/docgen/stdlib_catalog_entries.hpp:722` (`ai.create_zip`) | Documents `entries: map[str]str` | Update to `entries: []ZipEntry` to match `runtime/core/ai/vision.hpp:545` (`cfire doc` regenerated). |

- **Acceptance Criteria**:
  - Every row is applied, and `./cfire doc --check` passes after `./cfire doc`.
  - A CI/unit check fails when a backtick-quoted repository path in `README.md`, `docs/GUIDE.md`, or `docs/SENIOR_ENGINEER_REVIEW_GUIDELINES.md` does not exist. Finished entries in `backlog*.md` and `active.md` are history and are excluded.

#### `CF-235` — Mark the reversed decisions in `backlog.md` as superseded `[DONE]`
Applied in Stage 1 (`docs/planning/backlog.md` lines `884`, `1824`, `1825`, `1836–1853`), preserving historical text verbatim with trailing `— **Superseded** ...` notes (including `CF-085` `make(shared[T], ...)` and `CF-081` promoted to `CF-230`), plus `CF-176` marked `[DONE]`.

#### `CF-236` — Delete dead references and leftover files `[DONE]`
Things that no longer exist are **deleted**. Before each deletion, confirm with `git grep`, the workflows and the `Makefile` that nothing reads it.

| Item | Evidence | Action |
| :--- | :--- | :--- |
| Root `CNAME` and `.nojekyll` | Pages deploys `docs/` through `actions/upload-pages-artifact` (`.github/workflows/deploy-docs.yml:39`); `docs/CNAME` is identical; both root files date from the initial commit (`cc5bb85`). | Delete both in Wave `F5`. |
| Module alias spellings (`stdlib_catalog_entries.hpp:146-179`, `src/module_registry.hpp:46-62`) | `ext:sodium`, `core:sodium`, `ext:ssh`, `core:ssh`, `ext:sqlite`, `core:keychain`, … compile silently, and some skip library detection (`CF-211`). | Per `Q62` (Wave `F1`): remove `m.aliases` and emit hard error `cfire/INVALID-IMPORT` with a machine-applicable `cfire fix` rewrite. |
| `make clean` (`Makefile` `clean:`) | Removes `examples/*/.cfire` and `tests/.cfire` by hand, but not the per-entry `.cfire` directories of nested examples, nor leftover `runtime/.cfire_cache` directories written by builds before `597fd87`. | After `CF-237` (Wave `F5`), call `./cfire clean --cache` / project clean for `examples/` and `tests/` instead of hand-listing paths. |

**Checked and kept:**
- `Dockerfile` (`CF-205`'s static musl release builder).
- `scripts/audit/` (`PLAYBOOK.md`), `tools/vmtest/` (`Makefile:236`), `scripts/vm/` (VM gates).
- `.claude-plugin/` (plugin manifest) and `docs/index.template.html` (generates `index.html`).
- Mentions of `scripts/run_*.py`, `runtime/VERSION` and `examples/13` inside finished `active.md`/`backlog.md` entries. They are correct as history.

#### `CF-237` — `cfire clean`: cover every artifact location, with safety rules (`Q63`) `[DONE]`
- **Problem & Evidence** [ran]:
  - `cfire clean [--cache] [dir]` (`src/cli/build_commands.cpp:28-71`, from `dea2e46`) removes only `<dir>/.cfire`, plus the user cache root with `--cache` (see `CF-216`).
  - On Flint, `cfire clean .` printed "Cleaned ./.cfire (0 entries removed)". The 48 MB of artifacts live in the entry directories: `src/drivers/gui/.cfire` 34 MB, `cli/.cfire` 12 MB, `vault-server/.cfire` 2.2 MB.
  - Where `cfire` writes:

  | Scope | Path | Written by |
  | :--- | :--- | :--- |
  | project, per entry directory | `<entry dir>/.cfire/{gen,obj}/`, `gen/universal/` | `src/buildsys.hpp:1190`, `:1362`, `:1454`, `:1569` |
  | project | `<project>/.cfire/cache/` (deps, objs, libs, icons) | `buildsys.hpp:377`, `:879`; `src/build/deps.hpp:592-595`; `src/build/apple_bundle.hpp:134` |
  | project, per source directory | `<src dir>/.cfire/cache/` | `src/lsp/server.hpp:813` |
  | project outputs | `<OutDir>/<name>`, `<name>.app`, `<name>.html/.js/.wasm`, iOS bundles. `OutDir` defaults to the **project root**. | `src/build/manifest.hpp` (target resolution) |
  | user (`Q63`) | `~/Library/Caches/cfire` / `~/.cache/cfire` / `%LOCALAPPDATA%\cfire\cache` or `$CFIRE_CACHE_DIR` (`{pch,bin,libs,headers,ast,pkg,doc,mcp,probes.json}`) | Unified `cache_paths.hpp` + `cfire_prelude.hpp` |
  | toolchain (bug, `CF-217`) | `<toolchain parent>/.cfire/cache/` | `src/mcp.hpp:697`; `src/cli/inspect_and_outline.hpp:260` |
  | legacy (auto-swept by `--cache`) | `~/.cfire/cache/`, `<runtime>/.cfire_cache/` (before `597fd87`), `$TMP/cfire_pch_cache/`, `$TMP/cfire-<uid>-pch/`, `$TMP/cfire_doctor_probe.cpp`, owned `$TMP/cfire_clang_ast_*` | older builds; `src/doctor.hpp:137`; `src/clang_bridge.hpp:721` |

- **Command**: `cfire clean [--cache] [--all] [--dry-run] [--json] [path|build.cf]`
  - **Default (project)**: every `.cfire/` under the project root (found from `build.cf`, or the given path), plus each target's **exact** output paths from the manifest resolver. Never remove a whole `OutDir`, because it can be the project root.
  - `--cache`: the unified OS-native / XDG user cache root (`Q63`), subject to `CF-216`'s `.cfire-cache` marker rule, plus an automatic sweep of owned legacy locations (`~/.cfire/cache`, `<runtime>/.cfire_cache`, owned `$TMP/cfire_*` dirs) so no permanent `--legacy` CLI flag is needed.
  - `--all`: project + `--cache`. `--dry-run` lists each path and its size. `--json` emits structured JSON.
- **Safety rules**:
  - Never follow symlinks; remove the link itself.
  - Remove a `.cfire/` only if it holds nothing but `gen/`, `obj/` and `cache/`; otherwise report it.
  - Remove an output path only if it resolves inside the project root and is not a directory that contains `.cf` files.
  - Never touch `~/.cfire/runtime/`, `~/.cfire/bin/`, or `~/.cfire/config.json`.
  - Exit `1` when anything was skipped or a safety guard triggered, with a reason per path.
- **Tests**:
  - A fixture with nested entry directories, `OutDir: "."`, an `.app` bundle and a symlinked `.cfire`; check what is removed and what is kept.
  - A legacy directory owned by another uid is kept; owned legacy cache dirs are swept cleanly by `--cache`.
- **Acceptance Criteria**:
  - After `cfire clean --all`, a cold rebuild works.
  - On multi-driver layouts (e.g., Flint), `cfire clean` removes all driver `.cfire` directories.

#### `CF-238` — Flint as a downstream canary, and tags for downstream pins (`Q69`) `[EXCLUDED — see backlog.md §3]`
- **Disposition (`Q69`)**: Excluded in `backlog.md` §3. `cfire` keeps SemVer release tags only (`vX.Y.Z`), keeps commit messages downstream-agnostic (`AGENTS.md` §3), and guards regressions via self-contained `tests/conformance/` and `CF-214` `ext_smoke` suites rather than cloning external repositories in CI.

#### `CF-239` — MCP parity with Wave B5, `CF-188` & Wave W4 (`Q62`, `Q63`) `[DONE]`
- **Problem & Evidence** [ran]:
  1. `cfire_inspect` on `"cpp <header.h>"` (`src/mcp.hpp:693-710`) writes to `<toolchain parent>/.cfire/cache` (`runtime_dir.parent_path() / ".cfire" / "cache"`) and passes `{}` `cflags` instead of loading workspace `build.cf` (`PkgConfig`, `Libraries`, `Frameworks`, `Includes`, `Defines`) and caching under `<project>/.cfire/cache` or `cache_subdir("headers")`, unlike `src/cli/inspect_and_outline.hpp:240-262`.
  2. `cfire_stdlib_lookup` (`src/mcp.hpp:454-489`) and `cfire_inspect` (`src/mcp.hpp:764-778`) iterate only `mod.functions` plus hardcoded `log.Level`/`log.Format` C++ `uint8_t` descriptions, omitting `CF-188`'s `mod.type_docs` (`StdTypeDoc` structs/fields/enums) and Wave W4's `kBuiltinMethods` (`src/sema/builtin_methods.hpp`).
  3. `cfire_stdlib_lookup` (`src/mcp.hpp:457-458`) silently matches wrong `core:`/`ext:` prefixes without noting the canonical import path (`Q62`).
- **Implementation**:
  - Update `cfire_inspect("cpp <header.h>")` in `src/mcp.hpp` to load workspace/ancestor `build.cf` fields via `parse_build_cf` / `apply_manifest_pkg_fields` / `resolve_plan_pkg_configs` / `resolve_plan_defines` and route header caches through `header_inspect_cache_dir(ws_root)`.
  - Format `StdTypeDoc` (structs, fields, enums, handles, manifests in pure `.cf` terms, removing the legacy hardcoded `log.Level`/`log.Format` blocks) and `kBuiltinMethods` (`str`, `[]T`, `[N]T`, `map[K]V`, `int`, `float`) across both `cfire_stdlib_lookup` and `cfire_inspect`.
  - When `cfire_stdlib_lookup` receives a non-canonical module path (`canonical_import_for_wrong_prefix`), return the canonical module docs with an explicit canonical-import note (`cfire/INVALID-IMPORT`).
- **Acceptance Criteria**: Unit tests cover MCP C++ header inspection with `build.cf` `Includes`/`Defines` + workspace cache path, `StdTypeDoc` and `kBuiltinMethods` lookup/inspect responses, and non-canonical import warning note.

---

## Not for cfire (decided; do not re-propose)

| Need / Proposal | Why it stays out of cfire | Decision |
| :--- | :--- | :--- |
| Threads that outlive a scope (Flint `CF-026`, `138`, `043`) | Structured concurrency is a safety guarantee and only got stricter: `async.Task[T]` belongs to its scope, and `SPAWN-SHARED-MUT` / `ESCAPING-CAPTURE-MUT` enforce it. Flint restructures around one long-lived top-level scope, `sync.context` and `serve_in_background`, and sizes `workers:` for its long-lived tasks. | Q2, Q20, `634c8dd` |
| C handles freed by a custom function (`ssh_free`, `sqlite3_close`) | Safe code has no raw pointers, and `own[T].from_raw` takes only pointers that `release()` produced. The pattern is a move-only RAII class in the C++ shim, held by value. **Doc task (`CF-234`):** add this pattern to GUIDE Chapter 11. | Q25, Q54 |
| Low-level SSH (`005`) | Flint's libssh adapter is its own security boundary. | Section 3 |
| Lock, sleep and idle events (`084`); presence checks with polkit / Touch ID (`093`); process chains (`092`); which process holds a port (`114`); RDP (`119`) | OS integration that is Flint's own policy. | Section 3; scope |
| macOS menu and ⌘W (`051`, `073`); `cpp {}` verbatim (`065`) | Recorded decisions, never revisited. | Section 3 |
| Flint's own crypto, SQLCipher and keychain adapters (`006`–`008`) | Flint's pinned parameters and key formats are Flint policy. `cfire` owes correct, fail-closed modules (`CF-211`–`CF-215`), not Flint's shape. | — |
| Multi-window and a screen list (`126`) | A large API driven by a single app; Flint's shim stays until a second user needs it. | Q5 |
| `@pkg` on `import cpp` (Flint has 33 uses) | Retired in `597fd87` in favour of `build.cf` `PkgConfig`. Flint moves its `flint-native` pkg-config shim into `build.cf`, which also settles Flint `CF-031`. | `CF-207` |
| `[]T.clone()` (`CF-224` part) | `cfire` uses single-way value semantics (`mut copy := xs`) and automatic view-to-owner promotion; `UNKNOWN-METHOD` prints an idiomatic copy hint. | Q14, Q36, Q53, Q64 |
| `fs.sync_dir`, `fs.symlink`, `fs.read_link` (`CF-226` part) | POSIX directory `fsync` and Windows `CreateSymbolicLinkW` privilege semantics diverge across OSes; `core:fs` keeps portable `File.sync`, `create_new`, `stat`, `realpath`, and `ERR_*`. | Q68 |
| Multi-socket `net.wait_readable` (`CF-231`) | `TcpConn` is move-only, TLS `SSL_pending` can hold unread bytes in user space, and `async.scope` structured concurrency handles stream relays. | Q2, Q26, Q43, Q66 |
| `cfire test` FD-canary warm-up hook on `import cpp` (`CF-233`) | Reaffirms `CF-018` / `CF-080` automatic exemption for `import cpp` without adding test lifecycle hooks or CLI flags. | CF-018, Q67 |
| Downstream-coupled Flint CI canary, `legacy-flint-pin` tag & Flint impact in `feat!` commits (`CF-238`) | `cfire` publishes SemVer release tags only, keeps commits downstream-agnostic (`AGENTS.md` §3), and verifies regressions via `tests/conformance/` + `CF-214`. | Q69 |

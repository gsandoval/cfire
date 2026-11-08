# Active Workbench: Audited Upstream Resolution (`CF-001` – `CF-085`)

This workbench tracks the active 5-phase execution promoted from [`backlog.md`](./backlog.md).

## Active Scope Checklist

- [x] **Phase 1: P0 Security, Storage Integrity & Uniform Library Diagnostics**
  - `CF-006` / `CF-070`: Strict `ext:crypto/sodium`, expose `blake2b` & `derive_subkey`, remove pure-C++ fallback, fail hard at build time without `libsodium`.
  - `CF-007` / `CF-069`: Auto-detect `sqlcipher` in `buildsys.hpp` and fail closed in `sqlite.open_encrypted` / `apply_key` via `PRAGMA cipher_version;`.
  - `CF-008` / `CF-071`: Implement Linux `secret-tool lookup` & `clear`, check `SecItemAdd` / `pclose` errors, and restrict `memory_fallback_store()` to `CFIRE_TEST_MODE`.
  - `CF-075`: Uniform system library discovery (`~/.local` priority) & clear diagnostics in `buildsys.hpp` and `doctor.hpp` (no silent network auto-download).
- [x] **Phase 2: P1 Compiler, Sema & Codegen Fixes (Orthogonality-Preserving)**
  - `CF-059` / `CF-063`: Remove `os.getenv` / `find` from `expr_returns_optional_sema` and reject `str.find` / `str.rfind` in Sema with fix-it to `s.index_of` / `s.last_index_of`.
  - `CF-062`: Sema `cfire/TYPE-MISMATCH` diagnostic (with `byte(...)` fix-it) for `int`/`u8` in `[]byte`.
  - `CF-017` / `CF-034` / `CF-060`: Lower `{...}` string interpolation through `Emitter::emit_expr`, keep `a[0..5]` zero-copy `std::string_view` with Sema hint for `&str` / `str(...)`, and consistently mangle non-`main` package functions and types.
  - `CF-057`: Honor `` `default:"..."` `` tags in generated `json::parse_struct<T>`.
  - `CF-066`: Skip automatic JSON reflection for structs holding non-serializable handles (`webui.App`, `sync.Mutex`, `sqlite.Conn`, etc.).
  - `CF-067`: Default `typename U = T` in `vec_push(std::vector<T>&, U&&)` for braced slice literals.
  - `CF-068`: Track originating `source_path` on AST declarations and emit accurate `#line` directives.
  - `CF-076`: Filter out AST JSON dump lines when `clang++ -Xclang -ast-dump=json` fails.
  - `CF-078`: Support unary `-` on numeric literals in struct field defaults (`a: i32 = -1`).
  - `CF-085`: Reject `shared(v)` in Sema with fix-it to `make[shared[T]](v)`, and auto-deref `shared[T]` / `own[T]` on `.field` and `.method()`.
- [x] **Phase 3: P1 Build System, Installer, Test Runner & IDE Tooling**
  - `CF-011` / `CF-072`: Atomic `mv -f` install in `docs/install.sh` & `Makefile`, tag/SHA support in `CFIRE_VERSION`, pass `GIT_SHA` to `make`.
  - `CF-018` / `CF-080`: Exempt test suites using `import cpp` from the FD-leak canary and pre-warm `sodium_init()` when `ext:crypto/sodium` is imported.
  - `CF-031`: Inherit `Frameworks`, `Libraries`, and `external_deps` from ancestor `build.cf` during `cfire test`.
  - `CF-058`: Filter `cfire build <TargetName>` to build only the matching `Executable` in `build.cf`.
  - `CF-074`: Portable `"cfire"` in `.mcp.json` / `.claude/rules/cfire.md`, `.claude-plugin/marketplace.json` + LSP plugin manifest, and updated `cfire ide claude` / `--status`.
- [x] **Phase 4: P1 Desktop `ext:webui` & System Tray**
  - `CF-045`: Return deduplicated CSS font family names from `monospace_fonts()` on macOS.
  - `CF-053`: Do not store concealed secrets in `state_->state["__clipboard"]`, set `x-kde-passwordManagerHint` on Linux, and clear on GTK main thread if generation matches.
  - `CF-077`: Implement GTK 3 (`GtkFileChooserDialog` / `GtkMessageDialog`) and Win32 file/message dialogs.
  - `CF-082`: Add `set_tray`, `add_tray_item`, `hide_window`, `show_window`, and `set_hide_on_close` across macOS, Linux, and Windows.
- [x] **Phase 5: P2 Concurrency, Stdlib & Windows Core Runtime Fixes**
  - `CF-083`: Make `cfire::async::Scope` copyable via `std::shared_ptr<State>` so nested `scope.spawn` compiles and joins all transitively spawned tasks.
  - `CF-026`: Add `size()` on `cfire::sync::Channel<T>`.
  - `CF-042`: Register `std::atexit` raw terminal restoration in `os.make_raw`.
  - `CF-054` & `CF-064`: Fix `fs.write_all` docs, document `fs.rename`, and add `os.getpid() -> i64`.
  - `CF-001.f,j` & `CF-010.a,b,cli,d,e,f,h,j,l,m`: Windows header and runtime correctness fixes.
- [x] **Phase 6: `core:*` vs `ext:*` Taxonomy Realignment (`CF-086`)**
  - `CF-086`: Flip `audio`, `json`, `keychain`, `net`, `net/http`, `net/tls`, `net/url`, and `net/websocket` to `core:*`; enforce strict canonical `core:*` / `ext:*` prefixes across all 38 modules (`cfire/INVALID-IMPORT` + `cfire fix`); update 6-category Reference Dashboard, docs, examples, and tests.
- [x] **Phase 7: Pydantic AI Capabilities & `ext:ai` 6-Header Refactor (`CF-087`)**
  - `CF-087`: Refactor `runtime/core/ai/` into 6 headers (`types.hpp`, `vectors.hpp`, `documents.hpp`, `vision.hpp`, `client.hpp`, `agent.hpp`); implement `ai.Agent` (`register_tool`, `register_tool_action`, `use_mcp`, `run`, `run_struct`, `run_struct_validated`), `ai.mock` / `ai.mock_sequence`, multi-turn tool call/result serialization, and compile-time `validate:"..."` struct tags (`min`, `max`, `min_len`, `max_len`, `non_empty`, `oneof`) in `core:json` + Sema/Codegen.
- [x] **Phase 8: Uniform Stateful `ai.Client` Interface & `ai.local()` Provider Alignment (`CF-088`)**
  - `CF-088`: Add `ai.local()` (`Provider::Local`) with `ai.apple()` alias, stateful `default_model` on `Client` (`set_model`, `model`), 1-arg `Client` methods (`embed(text)`, `embed_image(path)`, `ask(prompt)`, `generate_struct[T](prompt)`), `client.ocr(path)`, `client.detect_language(text)`, and Sema `cfire/DEPRECATED-API` enforcement on top-level `ai.embed`/`ai.embed_image`/`ai.ocr`/`ai.detect_language`.
- [x] **Phase 9: Unified I/O Architecture (`stdin`/`stdout`, Files, Pipes & Sockets) (`CF-089`)**
  - `CF-089`: Enforce strict `_bytes` / `_text` symmetry across `core:fs`, `fs.File`, `net.TcpConn`, and `ssh.Channel`; add `read_into`, `read_exact`, `read_bytes_until`, and `read_text_until`; replace raw `os.fd_*`, `os.read_stdin*`, and global `readln`/`input` with RAII `fs.File` / `net.TcpConn` handles (`os.stdin()`, `os.stdout()`, `os.stderr()`, `os.pipe() -> (File, File)!`, `net.socketpair() -> (TcpConn, TcpConn)!`, `.poll()`, `.is_tty()`); fix `cli.prompt`/`cli.prompt_password` (`-> str?`); make `core:net` fail-closed; and enforce Sema `cfire/DEPRECATED-API` fix-its.

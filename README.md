<p align="center">
  <img src="docs/assets/cfire_logo.jpg" alt="cfire Logo — A Sapphire on Fire" width="240" />
</p>

<h1 align="center"><code>cfire</code> (<code>.cf</code>)</h1>

<p align="center">
  <strong>Crystalline C++23 Precision. Blazing Productivity.</strong><br/>
  <em>Write clean, Go/Tako/Odin-inspired code. Get zero-overhead native C++23.</em>
</p>

---

`cfire` is a high-productivity systems programming language and unified C/C++ build toolchain—**implemented in 100% pure ISO C++23**—that transpiles 1:1 to human-readable, standard **C++23** with zero runtime overhead.

## Core Pillars
1. **TypeScript's Approach to C++23:** A clean, ergonomic structural skin that lowers directly to native C++23 (`std::expected`, `std::variant`, `std::span`, `std::string_view`, `std::format`, C++20 `concept`s) without a garbage collector or runtime reflection overhead.
2. **Pure ISO C++23 Compiler & Runtime:** All compiler and toolchain subsystems (`src/*.hpp`, `src/main.cpp`) and the modular `core:*` runtime (`runtime/core/*.hpp`) are authored in clean, zero-dependency ISO C++23.
3. **Odin-Style Auto-Linked `core:` & `vendor:` Standard Library:** Import `core:compress/zstd`, `core:json`, `core:db/sqlite`, `core:net/http`, `core:sync`, or `vendor:raylib` and `cfire` automatically includes and links the underlying C/C++ engines (`libzstd`, `simdjson`, `sqlite3`, POSIX sockets, `raylib`).
4. **Clang AST C/C++ Interop + Unified `build.cf` Build System:** Import any C/C++ header (`import cpp "<header.h>"`) or build local/external `build.CC` and `build.CMake` dependencies into cached static archives (`.cfire/cache/lib/*.a`) with automatic `str` $\rightarrow$ `const char*` boundary coercion (`cfire::c_str`).
5. **Intent Over Ceremony:** Replace `const T&`, `T&&`, `std::move`, `std::string_view`, and pointer hazards with `in` (default), `mut`, `move` (with definite last-use move insertion), multi-return tuples `(T1, T2)`, unified `str`/`path`/`bytes`, `own[T]`/`shared[T]`, `T!`, `defer`, `match`, and `test "..." { ... }` blocks.

## Built-In `core:` (28 Modules) & `vendor:` Catalog

| Import Path | Native Engine / C++23 Lowering | Auto-Linked Flags |
| :--- | :--- | :--- |
| `core:fs` | `<filesystem>` + `<fstream>` (`read_all`, `write_all`, `read_text`, `write_text`, streaming `open`/`File`, `dirname`, `basename`, `extension`, `exists`, `mkdir_all`, `remove`, `list_dir`) | *(header-only)* |
| `core:cli` | Compile-time `cli:"..."` struct-tag flag parser (`bool`, `i32`, `i64`, `usize`, `f64`, `str`, `path`) | *(header-only)* |
| `core:compress/zstd` | Facebook `libzstd` (`<zstd.h>`) frame compressor & decompressor (`compress`, `decompress`) | `-lzstd` |
| `core:json` | `simdjson` (`<simdjson.h>`) `parse[T]` + `stringify(T)` via `json:"..."` struct tags, plus dynamic DOM (`parse_dynamic`) | `-lsimdjson` |
| `core:db/sqlite` | `sqlite3` (`<sqlite3.h>`) WAL connection (`open`, `exec`, `query[T]`, `tx`) via `sql:"..."` struct tags | `-lsqlite3` |
| `core:crypto/sha256` | Standard FIPS 180-4 SHA-256 digest (`hash_hex`, `hash_bytes`) | *(header-only)* |
| `core:crypto/hmac` | RFC 2104 HMAC-SHA256 message authentication (`sha256_hex`, `sha256_bytes`, constant-time `equal`) | *(header-only)* |
| `core:crypto/aes` | NIST FIPS 197 AES-256-GCM AEAD & CTR symmetric encryption (`encrypt_gcm`, `decrypt_gcm`, `encrypt_ctr`, `decrypt_ctr`) | *(header-only)* |
| `core:crypto/rand` | OS-backed CSPRNG entropy (`bytes`) via `arc4random_buf` / `/dev/urandom` | *(header-only)* |
| `core:crypto/blake3` | Fast cryptographic 256-bit digest (`hash_hex`) | *(CommonCrypto / std)* |
| `core:net` | Real POSIX BSD TCP sockets (`listen_tcp`, `connect_tcp`, `accept`, `read_str`, `write`) | *(POSIX sockets)* |
| `core:net/url` | RFC 3986 URL parsing & percent encoding (`parse`, `encode`, `decode`) | *(header-only)* |
| `core:net/http` | HTTP/1.1 client (`get`, `post`), server (`make_server`, `serve_in_background`), & standalone `Router` (`make_router`, `:param`, `*wildcard`) | *(POSIX / std)* |
| `core:net/websocket` | RFC 6455 WebSockets client, server & loopback pair (`connect`, `upgrade`, `pair`, `send_text`, `recv_text`, `send_binary`, `recv_binary`, `ping`, `close`) | *(POSIX / std)* |
| `core:time` | Monotonic clocks, typed `Duration`, calendar breakdown & RFC 3339 timestamps (`now`, `since`, `sleep_ms`, `format_rfc3339`, `parse_rfc3339`) | *(header-only)* |
| `core:os` | Environment, process args, filesystem navigation (`getenv`, `setenv`, `cwd`, `chdir`, `args`, `pid`, `exit`) | *(POSIX / stdlib)* |
| `core:os/exec` | Safe `fork`/`execvp` subprocess execution & pipe capture (`command`, `run_with_input`) | *(POSIX)* |
| `core:os/signal` | Asynchronous POSIX signal trapping (`notify`) over `sync.Channel[i32]` | *(POSIX)* |
| `core:sync` | Thread-safe Go-style typed `Channel[T]` (`make_channel`, `send`, `recv`, `try_recv`, `close`) for `async.scope` workers | *(header-only)* |
| `core:sync/context` | Hierarchical cancellation tokens & deadlines (`background`, `with_cancel`, `with_timeout`, `recv_ctx`, `sleep_ctx`) | *(header-only)* |
| `core:str` | Zero-fuss string manipulation (`trim`, `split`, `join`, `replace_all`, `starts_with`, `ends_with`, `contains`, `to_upper`, `to_lower`) | *(header-only)* |
| `core:unicode` | RFC 3629 UTF-8 validation, rune iteration & case mapping (`is_valid_utf8`, `rune_count`, `runes`, `encode_rune`, `to_upper`, `to_lower`) | *(header-only)* |
| `core:regex` | ECMAScript regular expressions (`compile`, `is_match`, `find`, `find_all`, `replace_all`) | *(header-only)* |
| `core:collections` | Generic high-performance containers: `Set[T]`, `PriorityQueue[T]`, `Deque[T]` | *(header-only)* |
| `core:sort` | In-place slice sorting & binary search (`slice`, `slice_desc`, `is_sorted`, `binary_search`) | *(header-only)* |
| `core:log` | Structured leveled logging (`set_level`, `set_format`, `debug`, `info`, `warn`, `error`) in Text or JSON | *(header-only)* |
| `core:encoding/base64` | RFC 4648 Base64 encoding & decoding (`encode`, `encode_str`, `decode`, `decode_str`) | *(header-only)* |
| `core:encoding/hex` | Hexadecimal encoding & decoding (`encode`, `decode`, `decode_str`) | *(header-only)* |
| `core:encoding/csv` | RFC 4180 CSV parser & serializer (`parse`, `stringify`) | *(header-only)* |
| `core:math` | Numeric & trigonometric utilities (`abs_f64`, `sqrt`, `pow`, `sin`, `cos`, `floor`, `ceil`, `clamp_i64`, `clamp_f64`) | *(header-only)* |
| `core:rand` | Non-deterministic PRNG & CSPRNG (`u64`, `range_i64`, `f64_unit`, `bytes`) | *(header-only)* |
| `vendor:raylib` | `raylib` (`<raylib.h>`) 2D/3D hardware-accelerated visualization | `-lraylib` + macOS frameworks |

## Quick Start

```bash
# Build the cfire compiler and run the full C++23 conformance suite:
make test

# Run canonical examples directly:
./cfire run examples/01-hello-core/main.cf
./cfire run examples/02-log-compressor/main.cf -- --input README.md --level 3
./cfire run examples/03-pkg-cache/main.cf
./cfire run examples/04-async-workers/main.cf
./cfire run examples/05-cmake-interop/main.cf
./cfire run examples/07-types-match-map/main.cf
./cfire run examples/08-multi-pkg-cc-build/main.cf
./cfire run examples/09-stdlib-test-pointers/main.cf
./cfire run examples/10-cmake-and-make-deps/main.cf
./cfire run examples/11-http-server-router/main.cf

# Evaluate one-shot expressions or statements:
./cfire eval -e "1 + 2 * 3"
./cfire eval "println(\"Hello from cfire eval!\")"

# Semantic rename refactoring across workspace:
./cfire refactor rename examples/01-hello-core/main.cf:6:5 my_new_symbol --diff

# Configure IDE integrations (VS Code, Cursor, Zed, Neovim, CLion):
./cfire ide --all

# Inspect and type-check with diagnostics:
./cfire check examples/01-hello-core/main.cf

# Execute built-in `test "..." { ... }` blocks in any .cf file:
./cfire test examples/09-stdlib-test-pointers/main.cf

# Format .cf source files canonically (or verify with --check):
./cfire fmt examples/
./cfire fmt --check examples/

# Start the built-in Language Server Protocol (LSP) server over JSON-RPC 2.0 stdio:
./cfire lsp

# Generate / verify the always-up-to-date documentation site & guides, or extract package docs:
./cfire doc
./cfire doc --check
./cfire doc examples/09-stdlib-test-pointers/main.cf
```

## Documentation & Guides (Always Kept in Sync via `cfire doc --check`)

- **Interactive HTML Landing Page:** [`docs/index.html`](docs/index.html)
- **Complete Language & Systems Programming Guide:** [`docs/guide.html`](docs/guide.html) / [`docs/GUIDE.md`](docs/GUIDE.md)
- **`core:*` Standard Library & Diagnostics Reference:** [`docs/reference.html`](docs/reference.html) / [`docs/STDLIB_REFERENCE.md`](docs/STDLIB_REFERENCE.md)


<p align="center">
  <img src="docs/assets/cfire_logo.jpg" alt="cfire Logo — A Sapphire on Fire" width="240" />
</p>

<h1 align="center"><code>cfire</code> (<code>.cf</code>)</h1>

<p align="center">
  <strong>Crystalline C++23 Precision. Blazing Productivity.</strong><br/>
  <em>Write clean, Go/Tako/Odin-inspired code. Get zero-overhead native C++23.</em>
</p>

---

`cfire` is a high-productivity systems programming language and unified C/C++ build toolchain—**100% self-hosted in pure `cfire` (`.cf`)**—that transpiles 1:1 to human-readable, standard **C++23** with zero runtime overhead and measurable performance parity ($\ge 70-179\%$ of native C++ speed).

## Core Pillars
1. **TypeScript's Approach to C++23:** A clean, ergonomic structural skin that lowers directly to native C++23 (`std::expected`, `std::variant`, `std::span`, `std::string_view`, `std::format`, C++20 `concept`s) without a garbage collector or runtime reflection overhead.
2. **100% Self-Hosted & Self-Building:** All 12 compiler and toolchain subsystems (Lexer, Parser, AST, Sema, Codegen, Buildsys, Formatter, Docgen, LSP, Refactoring Engine, Main Driver, Clang Bridge) are written in pure `cfire` (`src/*.cf`), dogfooded continuously via `make dogfood`.
3. **Odin-Style Auto-Linked `core:` & `vendor:` Standard Library:** Import `core:compress/zstd`, `core:json`, `core:db/sqlite`, `core:net/http`, `core:sync`, or `vendor:raylib` and `cfire` automatically includes and links the underlying C/C++ engines (`libzstd`, `simdjson`, `sqlite3`, POSIX sockets, `raylib`).
4. **Clang AST C/C++ Interop + Unified `build.cf` Build System:** Import any C/C++ header (`import cpp "<header.h>"`) or build local/external `build.CC` and `build.CMake` dependencies into cached static archives (`.cfire/cache/lib/*.a`) with automatic `str` $\rightarrow$ `const char*` boundary coercion (`cfire::c_str`).
5. **Intent Over Ceremony:** Replace `const T&`, `T&&`, `std::move`, `std::string_view`, and pointer hazards with `in` (default), `mut`, `move` (with definite last-use move insertion), multi-return tuples `(T1, T2)`, unified `str`/`path`/`bytes`, `own[T]`/`shared[T]`, `T!`, `defer`, `match`, and `test "..." { ... }` blocks.

## Built-In `core:` & `vendor:` Catalog

| Import Path | Native Engine / C++23 Lowering | Auto-Linked Flags |
| :--- | :--- | :--- |
| `core:fs` | `<filesystem>` + `<fstream>` (`read_bytes`, `read_text`, `write_bytes`, `write_text`, `exists`, `mkdir_all`, `list_dir`) | *(stdlib)* |
| `core:cli` | Compile-time `cli:"..."` struct-tag flag parser (`bool`, `i32`, `i64`, `usize`, `f64`, `str`, `path`) | *(header-only)* |
| `core:compress/zstd` | Facebook `libzstd` (`<zstd.h>`) frame compressor & decompressor | `-lzstd` |
| `core:json` | `simdjson` (`<simdjson.h>`) `parse[T]` + `stringify(T)` via `json:"..."` struct tags | `-lsimdjson` |
| `core:db/sqlite` | `sqlite3` (`<sqlite3.h>`) WAL connection, `exec`, `query[T]`, `tx` via `db:"..."` struct tags | `-lsqlite3` |
| `core:crypto/sha256` | Standard FIPS 180-4 SHA-256 digest (`hash_hex`, `hash_bytes`) | *(stdlib)* |
| `core:crypto/hmac` | RFC 2104 HMAC-SHA256 message authentication (`digest_hex`, `verify`) | *(stdlib)* |
| `core:crypto/aes` | NIST FIPS 197 AES-256-GCM symmetric authenticated encryption (AEAD) | *(stdlib)* |
| `core:crypto/blake3` | Cryptographic 256-bit digest (`hash_hex`) | *(CommonCrypto / std)* |
| `core:net` | Real POSIX BSD TCP sockets (`listen_tcp`, `connect_tcp`, `accept`, `read_str`, `write`) | *(POSIX sockets)* |
| `core:net/http` | HTTP/1.1 client (`get`, `post`), server (`make_server`), router (`Router`) | *(POSIX / std)* |
| `core:net/url` | RFC 3986 URL parsing & percent encoding (`parse`, `encode`, `decode`) | *(stdlib)* |
| `core:net/websocket` | RFC 6455 WebSockets client & server (`handshake`, `Frame`, `send`, `recv`) | *(POSIX / std)* |
| `core:sync` | Thread-safe Go-style typed `Channel[T]` (`send`, `recv`, `close`) for `async.scope` workers | *(stdlib)* |
| `core:sync/context` | Cancellation tokens & deadlines (`Context`, `with_cancel`, `with_timeout`) | *(stdlib)* |
| `core:time` | Monotonic clocks, durations & RFC 3339 timestamps (`now`, `format_rfc3339`, `parse_rfc3339`) | *(stdlib)* |
| `core:os` | Environment, working directory, command execution (`getenv`, `exec`, `command`) | *(POSIX / stdlib)* |
| `core:str` | Zero-fuss string manipulation (`trim`, `split`, `join`, `to_upper`, `to_lower`, `replace_all`) | *(stdlib)* |
| `core:unicode` | UTF-8 rune decoding, rune counting, uppercase/lowercase conversions | *(stdlib)* |
| `core:regex` | ECMAScript regular expressions (`compile`, `is_match`, `find_first`, `replace_all`) | *(stdlib)* |
| `core:collections` | High-performance containers: `Set[T]`, `PriorityQueue[T]`, `Deque[T]` | *(stdlib)* |
| `core:sort` | Slice sorting & binary search (`slice`, `slice_by`, `binary_search`) | *(stdlib)* |
| `core:log` | Structured leveled logging (`debug`, `info`, `warn`, `error`, `set_level`) | *(stdlib)* |
| `core:encoding/base64` | RFC 4648 Base64 & URL-safe encoding/decoding (`encode`, `decode`) | *(stdlib)* |
| `core:encoding/hex` | Hexadecimal encoding & decoding (`encode`, `decode`) | *(stdlib)* |
| `core:encoding/csv` | RFC 4180 CSV parsing & stringification (`Reader`, `Writer`) | *(stdlib)* |
| `core:math` / `core:rand` | Numeric utilities & thread-local CSPRNG (`range_i64`, `random_bytes`) | *(stdlib)* |
| `vendor:raylib` | `raylib` (`<raylib.h>`) 2D/3D hardware-accelerated visualization | `-lraylib` + macOS frameworks |

## Quick Start

```bash
# Build the cfire compiler and run the full C++23 conformance suite:
make test

# Dogfood the full 12-component self-hosting pipeline (cfire compiles cfire):
make dogfood

# Run canonical examples directly:
./cfire run examples/01-hello-core/main.cf
./cfire run examples/02-log-compressor/main.cf -- --input README.md --level 3
./cfire run examples/03-pkg-cache/main.cf
./cfire run examples/04-async-workers/main.cf
./cfire run examples/05-cmake-interop/main.cf
./cfire run examples/07-types-match-map/main.cf
./cfire run examples/08-multi-pkg-cc-build/main.cf
./cfire run examples/09-stdlib-test-pointers/main.cf

# Evaluate one-shot expressions or statements:
./cfire eval -e "1 + 2 * 3"
./cfire eval "println(\"Hello from cfire eval!\")"

# Semantic rename refactoring across workspace:
./cfire refactor rename src/main.cf:42:5 my_new_symbol --diff

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


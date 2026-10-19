<p align="center">
  <img src="docs/assets/cfire_logo.jpg" alt="cfire Logo — A Sapphire on Fire" width="240" />
</p>

<h1 align="center"><code>cfire</code> (<code>.cf</code>)</h1>

<p align="center">
  <strong>Crystalline C++23 Precision. Blazing Productivity.</strong><br/>
  <em>Write clean, Go/Tako/Odin-inspired code. Get zero-overhead native C++23.</em>
</p>

---

`cfire` is a high-productivity systems programming language and unified C/C++ build toolchain—**written in pure C++23 (`-std=c++23`) with zero external toolchain dependencies**—that transpiles 1:1 to human-readable, standard **C++23**.

## Core Pillars
1. **TypeScript's Approach to C++23:** A clean, ergonomic structural skin that lowers directly to native C++23 (`std::expected`, `std::variant`, `std::span`, `std::string_view`, `std::format`, C++20 `concept`s) without a garbage collector or runtime reflection overhead.
2. **Odin-Style Auto-Linked `core:` & `vendor:` Standard Library:** Import `core:compress/zstd`, `core:json`, `core:db/sqlite`, `core:net`, `core:sync`, or `vendor:raylib` and `cfire` automatically includes and links the underlying C/C++ engines (`libzstd`, `simdjson`, `sqlite3`, POSIX sockets, `raylib`).
3. **Clang AST C/C++ Interop + Unified `build.cf` Build System:** Import any C/C++ header (`import cpp "<header.h>"`) or build local/external `build.CC` and `build.CMake` dependencies into cached static archives (`.cfire/cache/lib/*.a`) with automatic `str` $\rightarrow$ `const char*` boundary coercion (`cfire::c_str`).
4. **Intent Over Ceremony:** Replace `const T&`, `T&&`, `std::move`, `std::string_view`, and pointer hazards with `in` (default), `mut`, `out`, `move` (with definite last-use move insertion), unified `str`/`path`/`bytes`, `own[T]`/`shared[T]`, `T!`, `defer`, `match`, and `test "..." { ... }` blocks.

## Built-In `core:` & `vendor:` Catalog

| Import Path | Native Engine / C++23 Lowering | Auto-Linked Flags |
| :--- | :--- | :--- |
| `core:fs` | `<filesystem>` + `<fstream>` (`read_bytes`, `read_text`, `write_bytes`, `write_text`, `exists`, `mkdir_all`, `list_dir`) | *(stdlib)* |
| `core:cli` | Compile-time `cli:"..."` struct-tag flag parser (`bool`, `i32`, `i64`, `usize`, `f64`, `str`, `path`) | *(header-only)* |
| `core:compress/zstd` | Facebook `libzstd` (`<zstd.h>`) frame compressor & decompressor | `-lzstd` |
| `core:json` | `simdjson` (`<simdjson.h>`) `parse[T]` + `stringify(T)` via `json:"..."` struct tags | `-lsimdjson` |
| `core:db/sqlite` | `sqlite3` (`<sqlite3.h>`) WAL connection, `exec`, `query[T]`, `tx` via `db:"..."` struct tags | `-lsqlite3` |
| `core:crypto/blake3` | Hardware-accelerated 256-bit cryptographic digest (`hash_hex`) | *(CommonCrypto / std)* |
| `core:net` | Real POSIX BSD TCP sockets (`listen_tcp`, `connect_tcp`, `accept`, `read_str`, `write`) | *(POSIX sockets)* |
| `core:sync` | Thread-safe Go-style typed `Channel[T]` (`send`, `recv`, `close`) for `async.scope` workers | *(stdlib)* |
| `core:time` | Monotonic clocks & timers (`now_ms`, `unix_seconds`, `sleep_ms`) | *(stdlib)* |
| `core:os` | Environment, working directory & subprocess capture (`getenv`, `setenv`, `cwd`, `exec`) | *(POSIX / stdlib)* |
| `core:str` | Zero-fuss string manipulation (`trim`, `split`, `join`, `to_upper`, `to_lower`, `replace_all`) | *(stdlib)* |
| `core:math` / `core:rand` | Math helpers (`clamp_i64`, `abs_f64`, `sqrt_f64`) & thread-local RNG (`range_i64`) | *(stdlib)* |
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


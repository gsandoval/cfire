# `cfire` Spec 03 — Unified Build System & `core:` / `vendor:` Catalog

## 1. Odin-Style Auto-Linked `core:` and `vendor:` Packages
Referencing a `core:` or `vendor:` package in an `import` declaration automatically emits the corresponding C++23 runtime header and injects the required `pkg-config` / Homebrew / system linker flags (`-lzstd`, `-lsimdjson`, `-lsqlite3`, etc.).

| Import Path | Underlying Native Engine | Auto-Linked Flags |
| :--- | :--- | :--- |
| `core:fs` | C++23 `<filesystem>` + `<fstream>` | *(stdlib)* |
| `core:cli` | Compile-time struct-tag codegen (`cli:"..."`) | *(header-only)* |
| `core:compress/zstd` | Facebook `libzstd` (`<zstd.h>`) | `-lzstd` (via `pkg-config libzstd`) |
| `core:json` | `simdjson` (`<simdjson.h>`) + static tag codegen | `-lsimdjson` (via `pkg-config simdjson`) |
| `core:db/sqlite` | `sqlite3` (`<sqlite3.h>`) + static tag row mapper | `-lsqlite3` (via `pkg-config sqlite3`) |
| `core:crypto/blake3` | Fast cryptographic hash / OpenSSL EVP | `-lcrypto` (or bundled BLAKE3) |
| `core:net` / `core:http` | POSIX / Asio async sockets + `llhttp` | `-lllhttp` |
| `vendor:raylib` | `raylib` | `-lraylib -framework Cocoa -framework IOKit -framework OpenGL` |

## 2. External C/C++ Dependencies (`@pkg`, `build.cf`)
1. **Inline `@pkg("<name>")`:**
   ```go
   import cpp "zstd.h" @pkg("libzstd")
   ```
   Queries `pkg-config --cflags --libs <name>` and forwards all include directories and library flags to `clang++`.
2. **Declarative `build.cf`:**
   Supports `build.CMake(...)`, `build.CC(...)`, `build.Autotools(...)`, and `build.Executable(...)`.
3. **Zero Lock-In (`cfire eject`):**
   Emits standalone `.cpp` files, `cfire_prelude.hpp`, and a standard `CMakeLists.txt`.

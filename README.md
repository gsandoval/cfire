<p align="center">
  <img src="docs/assets/cfire_logo_horizontal.png" alt="cfire logo" width="280" />
</p>

<h1 align="center"><code>cfire</code> (<code>.cf</code>)</h1>

<p align="center">
  <strong>Pronounced <em>sapphire</em> (/ˈsæf.aɪ.ər/). C Systems Heritage. Blazing Modern Velocity.</strong><br/>
  <em>Write clean, Go/Odin-inspired code. Lower 1:1 to zero-overhead ISO C++23.</em>
</p>

---

`cfire` is a high-productivity systems programming language and unified C/C++ build toolchain—**implemented in 100% pure ISO C++23**—that transpiles 1:1 to human-readable, standard **C++23** with zero runtime overhead.

### What's in the Name?
**`cfire`** (pronounced like **"sapphire"**, or phonetically **"C-fire"**) brings together two core ideas:
- **`C`**: The bedrock of systems programming—direct mechanical sympathy, raw execution speed, and standard ISO C++23 precision.
- **`fire`**: In modern developer parlance, *it's fire*—blazing fast, lightweight, and genuinely fun to write without forty years of ceremonial baggage.

The phonetic homophone to **sapphire** captures the visual identity: crystalline, structured rigor on the inside, fueled by a blazing developer workflow on the outside.

## Core Pillars
1. **TypeScript's Approach to C++23:** A clean, ergonomic structural skin that lowers directly to native C++23 (`std::expected`, `std::variant`, `std::span`, `std::string_view`, `std::format`, C++20 `concept`s) without a garbage collector or runtime reflection overhead.
2. **Pure ISO C++23 Compiler & Runtime:** All compiler and toolchain subsystems (`src/*.hpp`, `src/main.cpp`) and the modular `core:*` runtime (`runtime/core/*.hpp`) are authored in clean, zero-dependency ISO C++23.
3. **Explicit `core:*`, `ext:*` & `vendor:*` Package Namespaces:** Import zero-setup foundational & baseline OS primitives (`core:str`, `core:fs`, `core:sync`, `core:cli`, `core:json`, `core:net/http`, `core:audio`, `core:keychain`), opt-in native C library / platform extensions (`ext:ai`, `ext:compress/zstd`, `ext:crypto/sodium`, `ext:db/sqlite`, `ext:net/ssh`, `ext:webui`), or bundled C/C++ graphics engines (`vendor:raylib`, `vendor:raygui`) and `cfire` automatically includes and links the underlying libraries (`libzstd`, `libsodium`, `sqlite3`, `libssh`, `raylib`).
4. **Clang AST C/C++ Interop + Unified `build.cf` Build System:** Import any C/C++ header (`import cpp "<header.h>"`) or build local/external `build.CC` and `build.CMake` dependencies into cached static archives (`.cfire/cache/lib/*.a`) with automatic `str` $\rightarrow$ `const char*` boundary coercion (`cfire::c_str`).
5. **Intent Over Ceremony:** Replace `const T&`, `T&&`, `std::move`, `std::string_view`, and pointer hazards with `in` (default), `mut`, `move` (with definite last-use move insertion), multi-return tuples `(T1, T2)`, unified `str`/`path`/`bytes`, `own[T]`/`shared[T]`, `T!`, `defer`, `match`, and `test "..." { ... }` blocks.

## 3-Namespace `core:*`, `ext:*` & `vendor:*` Standard Library Catalog (38 Modules)

To keep the core language surface clear while providing batteries-included productivity, the standard library is separated into three explicit package namespaces based on external link requirements:
- **`core:*` — Foundational & Platform Core (30 Modules, Zero External Setup):** Everything that compiles and runs out-of-the-box with built-in ISO C++23 or baseline OS APIs/fallbacks (`core:str`, `core:unicode`, `core:regex`, `core:collections`, `core:sort`, `core:math`, `core:rand`, `core:json`, `core:encoding/*`, `core:fs`, `core:os`, `core:time`, `core:sync`, `core:sync/context`, `core:cli`, `core:log`, `core:keychain`, `core:build`, `core:net`, `core:net/url`, `core:net/tls`, `core:net/http`, `core:net/websocket`, `core:audio`, `core:crypto/*`).
- **`ext:*` — Native C Libraries, WebUI & AI Extensions (6 Opt-In Modules):** Specialized engines requiring external C libraries, native GUI webview toolkits, or AI/OCR engines (`ext:ai`, `ext:compress/zstd`, `ext:crypto/sodium`, `ext:db/sqlite`, `ext:net/ssh`, `ext:webui`) that are only included and linked when explicitly imported.
- **`vendor:*` — Bundled Third-Party C/C++ Engines (2 Modules):** `vendor:raylib` and `vendor:raygui`.

| Import Path | Native Engine / C++23 Lowering | Auto-Linked Flags |
| :--- | :--- | :--- |
| `core:audio` | Cross-target polyphonic audio mixer, procedural SFX (`gen_sfx`), ADSR synth (`gen_tone`, `gen_chirp`), 16-bit RIFF/WAV codec (`load_wav`, `save_wav`), and real-time `f32` `Stream` DSP | *(AudioToolbox / WebAudio / ALSA fallback)* |
| `core:build` | Declarative `build.cf` build-graph API (`Executable`, `CC`, `CMake`, `Cargo`, `Zig`, `Tako`) | *(header-only)* |
| `core:fs` | `<filesystem>` + `<fstream>` (`read_all`, `write_all`, `read_text`, `write_text`, `read_bytes`, `write_bytes`, streaming `open`/`File`, `dirname`, `basename`, `extension`, `exists`, `mkdir_all`, `remove`, `list_dir`) | *(header-only)* |
| `core:cli` | Compile-time `cli:"..."` struct-tag flag parser (`bool`, `i32`, `i64`, `usize`, `f64`, `str`, `path`) | *(header-only)* |
| `core:json` | `simdjson` (`<simdjson.h>`) `parse[T]` + `stringify(T)` + compile-time JSON Schema `schema[T]()` via `json:"..."` & `desc:"..."` struct tags, plus dynamic DOM (`parse_dynamic`) | *(bundled `simdjson` fallback / `-lsimdjson`)* |
| `core:keychain` | Native OS credential store (`set`, `get`, `delete_password`) via macOS `Security.framework`, Linux `libsecret`, or AES-256-GCM fallback vault | *(Security.framework / fallback)* |
| `core:crypto/sha256` | Standard FIPS 180-4 SHA-256 digest (`hash_hex`, `hash_bytes`) | *(header-only)* |
| `core:crypto/hmac` | RFC 2104 HMAC-SHA256 message authentication (`sha256_hex`, `sha256_bytes`, constant-time `equal`) | *(header-only)* |
| `core:crypto/aes` | NIST FIPS 197 AES-256-GCM AEAD & CTR symmetric encryption (`encrypt_gcm`, `decrypt_gcm`, `encrypt_ctr`, `decrypt_ctr`) | *(header-only)* |
| `core:crypto/blake3` | Fast cryptographic 256-bit digest (`hash_hex`) | *(CommonCrypto / std)* |
| `core:net` | Real POSIX BSD TCP sockets (`listen_tcp`, `connect_tcp`, `accept`, `read_str`, `write`) | *(POSIX sockets)* |
| `core:net/tls` | TLS 1.2/1.3 encrypted client & server streams (`connect`, `accept`, `read_str`, `write_str`) | *(OpenSSL `dlopen` / Apple Foundation / loopback)* |
| `core:net/url` | RFC 3986 URL parsing & percent encoding (`parse`, `encode`, `decode`) | *(header-only)* |
| `core:net/http` | HTTP/1.1 & HTTPS client (`get`, `post`, `post_sse`), server (`make_server`, `serve_in_background`), & standalone `Router` (`make_router`, `:param`, `*wildcard`) | *(POSIX / TLS)* |
| `core:net/websocket` | RFC 6455 WebSockets client, server & loopback pair (`connect`, `upgrade`, `pair`, `send_text`, `recv_text`, `send_binary`, `recv_binary`, `ping`, `close`) | *(POSIX / std)* |
| `core:time` | Monotonic clocks, typed `Duration`, calendar breakdown & RFC 3339 timestamps (`now`, `since`, `sleep_ms`, `format_rfc3339`, `parse_rfc3339`) | *(header-only)* |
| `core:os` | Environment, process args, subprocess execution, signals, and filesystem navigation (`getenv`, `setenv`, `cwd`, `chdir`, `args`, `pid`, `exit`, `exec`) | *(POSIX / stdlib)* |
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
| `ext:ai` | Multi-provider LLMs (`openai`, `anthropic`, `gemini`, `ollama`, `openai_compatible`), compile-time `generate_struct[T]` & `generate_struct_from_file[T]`, multimodal attachments (`user_with_file`, `ask_with_file`), zero-dependency document & slide extraction (`load_document`, `chunk_document` for `.pdf`, `.pptx`, `.docx`, `.xlsx`, `.odt`, `.odp`, `.ods`, `.epub`, `.html`, `.csv`, `.md`), on-device & Linux OCR (`ocr`), local/cloud text & image embeddings (`embed`, `embed_image`), and `McpServer` | *(POSIX / TLS / SIMD / Apple Frameworks / Tesseract)* |
| `ext:compress/zstd` | Facebook `libzstd` (`<zstd.h>`) frame compressor & decompressor (`compress`, `decompress`) | `-lzstd` |
| `ext:crypto/sodium` | Modern `libsodium` cryptography (`aead_encrypt`, `sign_detached`, `box_seal`, `pwhash_str`) | `-lsodium` |
| `ext:db/sqlite` | `sqlite3` (`<sqlite3.h>`) WAL connection (`open`, `exec`, `query[T]`, `tx`) via `sql:"..."` struct tags + `AVX2`/`ARM NEON` SIMD vector SQL functions (`vec_cosine_similarity`, `vec_cosine_distance`, `vec_l2_distance`) | `-lsqlite3` |
| `ext:net/ssh` | First-class `libssh` client (`connect`, `exec`, `sftp_read`, `sftp_write`, `forward_local`) | `-lssh` |
| `ext:webui` | Native desktop/mobile hybrid WebView (`WKWebView` / `WebKitGTK`) with `app://localhost/` VFS and typed RPC | *(WebKit / GTK3)* |
| `vendor:raylib` | `raylib` (`<raylib.h>`) 2D/3D hardware-accelerated visualization across macOS, iOS, WASM & Linux | `-lraylib` + platform frameworks |
| `vendor:raygui` | Immediate-mode GUI controls (`GuiButton`, `GuiSliderBar`, `GuiCheckBox`) & native iOS `GuiIos*` auto-layout kit | *(header-only over `vendor:raylib`)* |

## Quick Start

```bash
# 1. One-line install on macOS or Linux (installs binary + runtime headers & runs cfire doctor):
curl -fsSL https://cfire.memo.engineer/install.sh | sh

# 2. Check environment health or auto-install optional native libraries (simdjson, zstd, sqlite3, openssl):
./cfire doctor
./cfire setup --yes

# 3. Build the cfire compiler from source and run the full C++23 conformance suite:
make test
sudo make install

# 4. Run inside Docker / OrbStack:
docker build -t cfire:latest .
docker run --rm cfire:latest doctor
docker run --rm cfire:latest run examples/01-hello-core/main.cf

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
./cfire run examples/14-ai-rag-sqlite/main.cf
./cfire run examples/15-mcp-server/main.cf
./cfire run examples/16-audio-synth/main.cf

# Build optimized native binaries or packaged desktop apps:
./cfire build examples/01-hello-core/main.cf -o hello
./cfire build --subsystem=gui apps/desktop/main.cf -o myapp  # Auto-creates signed .app bundle on macOS

# Cross-compile to WebAssembly (HTML5 + WebGL Canvas or CLI):
./cfire build --target=wasm apps/desktop/main.cf -o web/app.html
./cfire wasm build apps/desktop/main.cf -o web/app.html

# Run native iOS apps directly on simulator:
./cfire run --ios examples/13-ios-native-ui

# Serve WebAssembly applications locally with zero dependencies (application/wasm MIME + COOP/COEP isolation):
./cfire serve web/app.html
./cfire wasm serve web/app.html

# Evaluate one-shot expressions or statements:
./cfire eval -e "1 + 2 * 3"
./cfire eval "println(\"Hello from cfire eval!\")"

# Start the Model Context Protocol (MCP) server for AI coding agents:
./cfire mcp

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


# Core Library Catalog & Daily Developer Utility Evaluation

> [!NOTE]
> **Historical Baseline Audit (All Findings Resolved)**: This document records the initial adversarial audit of `cfire`'s original 13-module prototype. Every gap, security finding, and catalog discrepancy identified below (`LIB-01` through `LIB-12`, plus `LIB-EXT-01` through `LIB-EXT-05`) has been **100% resolved**. `cfire` now ships with **28 production `core:*` modules** in [`runtime/core/`](../../runtime/core/) and [`src/docgen.hpp`](../../src/docgen.hpp).

This document provides a comprehensive, adversarial audit of the initial `cfire` standard library (`core:*`) benchmarked against modern gold-standard standard libraries (**Go**, **Python**, **Rust**, **Zig**, and **Odin**) to identify gaps in day-to-day developer workflows.

---

## 1. Executive Summary & Architecture Scorecard

`cfire` ships with 13 built-in modules (`core:fs`, `core:cli`, `core:compress/zstd`, `core:json`, `core:db/sqlite`, `core:crypto/blake3`, `core:net`, `core:time`, `core:os`, `core:sync`, `core:str`, `core:math`, `core:rand`).

While these modules provide an attractive initial baseline, an adversarial audit reveals that the current catalog is configured as a **demo-grade surface** rather than a production-ready toolkit:
1. **Critical Domain Gaps**: Zero HTTP client or server, zero TLS/SSL, zero URL parsing, no subprocess argument piping, no regex engine, no standard encodings (Base64, Hex, CSV), and no collections beyond maps and slices.
2. **Security & Cryptographic Hazards**:
   - `core:rand` seeds MT19937 with a static constant `0xC0FFEE123456789ULL` (every thread in every run produces identical sequences).
   - `core:crypto/blake3` is actually `CC_SHA256` on macOS and an unauthenticated 128-bit polynomial hash on Linux.
   - `core:os.exec` concatenates strings into `/bin/sh -c`, inviting command injection vulnerabilities.
3. **Documentation & Catalog Desynchronization**: Multiple discrepancies exist between the documented catalog in `src/docgen.hpp` and the runtime header in `runtime/cfire_core_stdlib.hpp`.

---

## 2. Technical Audit of the 13 Modules

| Module | Backing Engine | Fallible (`T!`) | Key Limitations & Adversarial Vulnerabilities |
|---|---|---|---|
| **`core:fs`** | `std::filesystem`, `<fstream>` | Yes | Whole-buffer reading only (OOM risk on large files; no streaming `File` handle). Append mode missing (`ios::trunc` hardcoded). No file permissions or stat timestamps. Name desync (`read_bytes` vs `read_all`). |
| **`core:cli`** | Loop over `argv` | Yes | Only parses space-separated `--flag val` (no `--flag=val`). No positional arguments or subcommands. No help generator. Uncaught `stoi`/`stod` exceptions on invalid inputs. |
| **`core:compress/zstd`** | `libzstd` | Yes | Whole-buffer only (no streaming decompressor). Requires known content size. No Gzip/Deflate support for HTTP workflows. |
| **`core:json`** | `simdjson` + `std::format` | Yes | Flat structs only (no nested structs, struct arrays, or maps). Serializer lacks character escaping (`"foo "bar"` breaks JSON). No dynamic/unstructured DOM (`json::Value`). |
| **`core:db/sqlite`** | `sqlite3.h` | Yes | Maps columns by ordinal index rather than column name (reordered queries corrupt data). No connection pooling (unsafe in `async.scope`). No NULL/optional handling. No binary BLOB support. |
| **`core:crypto/blake3`** | `CommonCrypto` / custom | No | Critical identity crisis: runs `CC_SHA256` on macOS and an insecure 128-bit FNV loop on Linux. Not BLAKE3. No HMAC, SHA-512, or AES. |
| **`core:net`** | POSIX BSD Sockets | Yes | Raw blocking TCP only. No HTTP client/server, no TLS/SSL. Hardcoded 2s timeout. DNS resolution absent: connecting to non-localhost hosts fails silently. |
| **`core:time`** | `std::chrono` | No | Bare integers only (`now_ms`, `unix_seconds`). No `Duration` type. No ISO 8601 or RFC 3339 parsing/formatting. No calendar breakdown (year, month, day). No timezones. |
| **`core:os`** | C stdlib + `popen` | Yes (`exec`) | Shell injection hazard via `/bin/sh -c`. Merges stdout/stderr. No stdin piping. No streaming output. No process signals (`SIGINT`, `SIGTERM`). Missing `args()` and `exit(code)`. |
| **`core:sync`** | `mutex`, `condition_variable` | Optional | Unbounded channel queue causes memory leaks under fast producers. No channel `select`. No receive timeouts. Missing `Mutex`, `RWMutex`, `WaitGroup`, and `Once`. |
| **`core:str`** | `<string>`, `<algorithm>` | No | ASCII-only byte semantics: `to_upper`/`to_lower` corrupt multi-byte UTF-8 sequences. No regex. Missing `contains` (documented). No Base64/Hex/URL encoding. |
| **`core:math`** | `<cmath>`, `<algorithm>` | No | Anemic surface: exactly 3 functions (`abs_f64`, `sqrt_f64`, `clamp_i64`). Documented `clamp_f64` is missing. Missing `PI`, trigonometry, logarithms, rounding (`floor`, `ceil`). |
| **`core:rand`** | `std::mt19937_64` | No | Deterministic PRNG with static constant seed. Documented `u64()` and `f64_unit()` are missing. No cryptographically secure random bytes (`/dev/urandom`). |

---

## 3. Discrepancies: `runtime/cfire_core_stdlib.hpp` vs `src/docgen.hpp`

1. **`core:fs` Read/Write**: `docgen.hpp` documents `read_all` and `write_all`; `cfire_core_stdlib.hpp` implements `read_bytes` and `write_bytes`.
2. **`core:fs.remove` Return**: `docgen.hpp` states `bool`; `cfire_core_stdlib.hpp` returns `std::expected<void, cfire::Error>`.
3. **`core:str.replace`**: `docgen.hpp` lists `replace`; `cfire_core_stdlib.hpp` implements `replace_all`.
4. **`core:str.contains`**: Documented in `docgen.hpp`, but completely missing from `cfire_core_stdlib.hpp`.
5. **`core:math` Float Clamp**: `clamp_f64` is documented, but missing from `cfire_core_stdlib.hpp`.
6. **`core:rand` Scalar Generators**: `u64()` and `f64_unit()` are documented, but missing from `cfire_core_stdlib.hpp`.
7. **`core:os.exec` Naming**: `docgen.hpp` documents `exec_output`; `cfire_core_stdlib.hpp` implements `exec`.

---

## 4. Prioritized Engineering Backlog: `LIB-01` through `LIB-12`

### `LIB-01`: High-Level HTTP Client, Server & URL Parsing (`core:net/http`, `core:net/url`)
- **Status**: Planned (P1)
- **Category**: Networking / Web
- **Target Files**: `runtime/cfire_core_stdlib.hpp`, `src/docgen.hpp`
- **Rationale**: Essential for REST APIs, webhooks, microservices, and external cloud communication.
- **Remedy**: Integrate `llhttp` parser, POSIX socket client with DNS resolution via `getaddrinfo`, and basic HTTP server handler routing.

### `LIB-02`: Safe Process Execution, Piping & Signal Trapping (`core:os/exec`, `core:os/signal`)
- **Status**: Planned (P1)
- **Category**: OS / Process Management
- **Target Files**: `runtime/cfire_core_stdlib.hpp`
- **Rationale**: Replaces vulnerable `popen` shell interpolation with parameter-list execution (`execve`), stdin/stdout streaming pipes, and POSIX signal trapping.

### `LIB-03`: Cryptographic Suite & True CSPRNG (`core:crypto/sha256`, `core:crypto/rand`)
- **Status**: Planned (P1)
- **Category**: Security / Cryptography
- **Target Files**: `runtime/cfire_core_stdlib.hpp`
- **Rationale**: Replaces misnamed blake3 and static PRNG seed with standard SHA-256, HMAC-SHA256, and OS-backed random bytes (`arc4random_buf` / `getentropy`).

### `LIB-04`: Standard Data Encodings (`core:encoding/base64`, `core:encoding/hex`, `core:encoding/csv`)
- **Status**: Planned (P2)
- **Category**: Text / Data Formats
- **Target Files**: `runtime/cfire_core_stdlib.hpp`
- **Rationale**: Header-only Base64, Hex, and RFC 4180 CSV serialization needed for auth tokens, data exchange, and exports.

### `LIB-05`: Regular Expressions Engine (`core:regex`)
- **Status**: Planned (P2)
- **Category**: Text Processing
- **Target Files**: `runtime/cfire_core_stdlib.hpp`
- **Rationale**: Input validation, text sanitization, and pattern matching backed by standard C++23 `<regex>` or embedded RE2.

### `LIB-06`: Time, Date & Duration Modernization (`core:time`)
- **Status**: Planned (P2)
- **Category**: Time / Dates
- **Target Files**: `runtime/cfire_core_stdlib.hpp`
- **Rationale**: Adds typed `Duration` arithmetic, calendar breakdowns (year, month, day), and ISO 8601 / RFC 3339 parsing and formatting via C++20 `<chrono>`.

### `LIB-07`: Data Structures & Algorithms (`core:collections`, `core:sort`)
- **Status**: Planned (P2)
- **Category**: Collections / Algorithms
- **Target Files**: `runtime/cfire_core_stdlib.hpp`
- **Rationale**: Adds `Set[T]`, `PriorityQueue[T]`, `Deque[T]`, `sort.slice()`, and `sort.binary_search()`.

### `LIB-08`: Structured Leveled Logging (`core:log` / `slog`)
- **Status**: Planned (P2)
- **Category**: Observability
- **Target Files**: `runtime/cfire_core_stdlib.hpp`
- **Rationale**: Leveled, structured logging (`DEBUG`, `INFO`, `WARN`, `ERROR`) with key-value contextual attributes and JSON / terminal formatters.

### `LIB-09`: Streaming Filesystem I/O (`core:fs` Modernization)
- **Status**: Planned (P2)
- **Category**: Filesystem / I/O
- **Target Files**: `runtime/cfire_core_stdlib.hpp`
- **Rationale**: Introduces streaming `File` handles with explicit open modes (`Read`, `Write`, `Append`), chunked reading, and path utility functions (`dirname`, `basename`, `extension`).

### `LIB-10`: JSON Dynamic DOM & Structured Serializer Fixes (`core:json`)
- **Status**: Planned (P2)
- **Category**: Serialization
- **Target Files**: `src/codegen.hpp`, `runtime/cfire_core_stdlib.hpp`
- **Rationale**: Adds dynamic JSON DOM inspection (`simdjson::dom`) and enforces character escaping in string serialization.

### `LIB-11`: Multi-Byte UTF-8 & Unicode Support (`core:unicode`)
- **Status**: Planned (P3)
- **Category**: Unicode / Text
- **Target Files**: `runtime/cfire_core_stdlib.hpp`
- **Rationale**: Safe UTF-8 rune iteration, rune counting, and validation without multi-byte corruption.

### `LIB-12`: Reconcile Core Library Documentation Desyncs
- **Status**: Planned (P1)
- **Category**: Quality / Consistency
- **Target Files**: `runtime/cfire_core_stdlib.hpp`, `src/docgen.hpp`
- **Rationale**: Fixes all 7 naming and return type discrepancies between `docgen.hpp` and `cfire_core_stdlib.hpp`.

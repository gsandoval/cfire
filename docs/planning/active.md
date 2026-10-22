# Active Workbench: In-Flight Implementation Tasks

This document tracks items currently under active design, scoping, or implementation from [`docs/planning/backlog.md`](./backlog.md) and [`docs/planning/architectural-backlog.md`](./architectural-backlog.md).

---

## Up Next: WebSocket Server, Socket Transport Fixes & TLS/WSS Support (`NET-P0-01` – `NET-P0-04`)

> **Instructions for the Implementing Agent**:
> Before writing any code for a `NET-*` task below, read its full specification, **Critical Details, Edge Cases & Pitfalls (Do Not Miss)**, and **Section Success Instructions & Sign-Off Gate** in [`docs/planning/backlog.md`](./backlog.md).

| Task ID | Section / Priority | Component | Summary | Status | Section Gate |
| :--- | :---: | :--- | :--- | :---: | :---: |
| `NET-P0-01` | Section 1 (P0) | `runtime/core/net/tcp.hpp`, `runtime/core/net/url.hpp`, `runtime/core/net/websocket.hpp` | **Socket Timeout Fix, `INADDR_ANY` Binding & `wss://` URL Port Default**: Clear 2s `SO_RCVTIMEO` on `websocket::Conn` init, add `set_timeout_ms`, bind `listen_tcp` to `INADDR_ANY`, and default `wss://` to port `443` | `[DONE]` | Passed (`make review-check`) |
| `NET-P0-02` | Section 1 (P0) | `runtime/core/net/http.hpp`, `runtime/core/net/websocket.hpp` | **First-Class WebSocket Server (`websocket::Server`) & `http::Router::ws` Route Upgrade**: High-level `websocket.make_server(port)` and `router.ws(pattern, handler)` upgrade dispatch in `HttpServer` | `[DONE]` | Passed (`make review-check`) |
| `NET-P0-03` | Section 1 (P0) | `runtime/core/net/tls.hpp`, `runtime/core/net/tcp.hpp`, `runtime/core/net.hpp` | **TLS / X.509 Certificate Engine (`core:net/tls`) via OpenSSL Dynamic Bridge**: Zero-build-flag `<dlfcn.h>` OpenSSL 3 loader, `tls.wrap_client` SNI + X.509 verification, `tls.ServerContext`, and `tls.generate_self_signed_pem` | `[DONE]` | Passed (`make review-check`) |
| `NET-P0-04` | Section 1 (P0) | `runtime/core/net/http.hpp`, `runtime/core/net/websocket.hpp`, `src/codegen.hpp`, `src/docgen/catalog.hpp` | **Unified `https://` & `wss://` Client/Server Support & Catalog Registration**: Automatic TLS in `http.get`/`post`/`request` and `websocket.connect`/`connect_insecure`, `make_tls_server*`, and `core:net/tls` catalog sync | `[DONE]` | Passed (`make review-check`) |
| `SEC-P0-01` | Section 2 (P0) | `runtime/core/net/*`, `runtime/core/json.hpp`, `runtime/core/compress.hpp`, `src/parser.hpp` | **Untrusted Input Parser Security Hardening**: WebSocket frame size cap + chunked read, HTTP CRLF stripping + header/query caps, `%XX` hex validation, JSON/AST recursion depth limits, and Zstd decompression bomb limit | `[DONE]` | Passed (`make fuzz`) |
| `SEC-P0-02` | Section 2 (P0) | `tests/unit/test_stdlib.cpp`, `tests/test_main.cpp`, `Makefile` | **Deterministic Mutation Fuzzer & Adversarial Security Suite (`make fuzz`)**: 10,000+ mutated/adversarial inputs across WebSocket, HTTP, URL, JSON, Base64/Hex/CSV, UTF-8, and `.cf` Lexer/Parser/LSP | `[DONE]` | Passed (`make fuzz`) |
| `SEC-P0-03` | Section 2 (P0) | `tests/unit/test_stdlib.cpp`, `tests/test_main.cpp`, `Makefile` | **Built-In Performance Benchmark Suite (`make bench`)**: Latency (`ns/op`) and throughput (`ops/sec`, `MB/s`) benchmarks for HTTP, WebSocket, TLS, JSON, Crypto, and Compiler Frontend | `[DONE]` | Passed (`make bench`) |

---

## Completed Milestones Summary

| Milestone | Task Range | Summary | Status | Gate |
| :--- | :--- | :--- | :---: | :---: |
| **Agent-Friendliness & Language Ergonomics** | `AGENT-P0-01` – `AGENT-P2-05` (13 items) | Multi-error `--json` with fix-its, `#line` source mapping + `-fsyntax-only` check, `cfire outline` type fidelity, catalog-generated `cfire doc --llm`, `println` interpolation flattening, struct-lit disambiguation, expression `match`, compound/bitwise ops, `-o` flag, `cfire fix`, `cfire test --json`/`--filter`, `cfire inspect`, and tokenizer-backed `cfire eval` | `[DONE]` | Passed (`make review-check`) |
| **C++20 Lowering & Google C++ Hardening** | `ARCH-CPP-01` – `ARCH-CPP-05` (5 items) | Receiver method name disambiguation, interface `concept` lowering, `scope.spawn` capture mode, `<charconv>` zero-exception migration, and `[[clang::lifetimebound]]` annotations | `[DONE]` | Passed (`make review-check`) |
| **Modular Runtime & Tooling Decomposition** | `CPP-QA-01` – `CPP-QA-06` (6 items) | DRY consolidation, `shell_quote` hardening, JSON-RPC field extraction, and modularization of `runtime/core/net/*`, `src/docgen/*`, and `src/lsp/*` | `[DONE]` | Passed (`make review-check`) |

---

## Validation Checklist (Four Gates)

- [x] **Gate 1 — Zero Compiler Warnings (Strict Mode)**: `clang++ -std=c++2b -Wall -Wextra -Wpedantic -Wshadow -Wno-parentheses-equality -Iruntime -Isrc src/main.cpp` compiles with 0 warnings.
- [x] **Gate 2 — Formatting Invariance**: `./cfire fmt --check examples/` passes cleanly.
- [x] **Gate 3 — Public Documentation Invariance**: `./cfire doc --check` passes and `git diff docs/reference.html docs/STDLIB_REFERENCE.md docs/index.html docs/guide.html` is empty.
- [x] **Gate 4 — Full Conformance Suite**: `make review-check` (all unit, stdlib, and e2e test suites + `cfire fmt --check`) passes 100%.

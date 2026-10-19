# `cfire` Standard Library (`core:*`) & Diagnostics Reference

> Auto-generated from the compiler's single-source-of-truth catalog via `./cfire doc`.

## 1. Built-In `core:*` Standard Library Modules

### `core:fs` (`fs`) — Filesystem & Zero-Copy Byte I/O

- **Auto-Link Flags:** `(header-only)`
- **Overview:** Provides RAII file and directory utilities backed by `std::filesystem` and `std::vector<std::byte>`.

**Practical Example:**
```cf
package main

import "core:fs"

fn main() {
    // 1. High-level one-shot file read and write
    try fs.write_text("config.ini", "workers=8\nport=9000\n")
    content := try fs.read_text("config.ini")
    println("Read configuration: {}", content)

    // 2. Streaming file operations with deterministic RAII defer
    mut file := try fs.open("service.log", fs.OpenMode::Append)
    defer file.close() // Closed deterministically on return

    try file.write_str("[INFO] Worker subsystem initialized\n")
    try file.flush()
    offset := try file.tell()
    println("Log stream offset: {} bytes", offset)
}
```

| `.cf` Signature | Lowered C++23 | Description | Example |
|---|---|---|---|
| `fn read_all(path: str) -> []byte!` | `cfire::fs::read_all(path)` | Reads an entire binary file into a byte buffer. | `raw := try fs.read_all("input.bin")` |
| `fn write_all(path: str, data: []byte) -> void!` | `cfire::fs::write_all(path, data)` | Writes a byte slice atomically to disk, creating parent directories if needed. | `try fs.write_all("out.bin", raw)` |
| `fn read_text(path: str) -> str!` | `cfire::fs::read_text(path)` | Reads an entire UTF-8 text file into a `str`. | `txt := try fs.read_text("config.json")` |
| `fn write_text(path: str, text: str) -> void!` | `cfire::fs::write_text(path, text)` | Writes a UTF-8 string to disk. | `try fs.write_text("hello.txt", "sapphire")` |
| `fn open(path: str, mode: OpenMode = OpenMode::Read) -> File!` | `cfire::fs::open(path, mode)` | Opens a file with specified mode (Read, Write, Append, ReadWrite) returning a streaming File handle. | `mut f := try fs.open("log.txt", OpenMode::Append)` |
| `fn (mut f: File) read(mut buf: []byte) -> usize!` | `f.read(buf)` | Reads up to `buf.len` bytes from the streaming file handle. | `n := try f.read(mut buf)` |
| `fn (mut f: File) read_all() -> []byte!` | `f.read_all()` | Reads all remaining bytes from current file position to EOF. | `bytes := try f.read_all()` |
| `fn (mut f: File) write(data: []byte) -> usize!` | `f.write(data)` | Writes byte slice to file stream. | `n := try f.write(buf)` |
| `fn (mut f: File) write_str(s: str) -> usize!` | `f.write_str(s)` | Writes UTF-8 string to file stream. | `try f.write_str("header\n")` |
| `fn (mut f: File) seek(offset: i64, whence: SeekWhence = SeekWhence::Start) -> i64!` | `f.seek(offset, whence)` | Seeks within file to offset from Start, Current, or End. | `pos := try f.seek(0, SeekWhence::Start)` |
| `fn (mut f: File) tell() -> i64!` | `f.tell()` | Returns current byte offset within file stream. | `pos := try f.tell()` |
| `fn (mut f: File) flush() -> void!` | `f.flush()` | Flushes buffered stream data to disk. | `try f.flush()` |
| `fn (mut f: File) close() -> void` | `f.close()` | Closes open file handle. | `f.close()` |
| `fn dirname(path: str) -> str` | `cfire::fs::dirname(path)` | Returns directory portion of a filesystem path. | `dir := fs.dirname("/a/b/c.txt")` |
| `fn basename(path: str) -> str` | `cfire::fs::basename(path)` | Returns file name portion of a filesystem path. | `name := fs.basename("/a/b/c.txt")` |
| `fn extension(path: str) -> str` | `cfire::fs::extension(path)` | Returns file extension including leading dot. | `ext := fs.extension("main.cf")` |
| `fn exists(path: str) -> bool` | `cfire::fs::exists(path)` | Returns `true` if the path exists on disk. | `if fs.exists("build.cf") { ... }` |
| `fn mkdir_all(path: str) -> void!` | `cfire::fs::mkdir_all(path)` | Recursively creates a directory and all parent directories. | `try fs.mkdir_all(".cfire/cache")` |
| `fn remove(path: str) -> void!` | `cfire::fs::remove(path)` | Removes a file or directory tree recursively. | `try fs.remove("tmp_dir")` |
| `fn list_dir(path: str) -> []str!` | `cfire::fs::list_dir(path)` | Lists sorted filenames inside a directory. | `entries := try fs.list_dir("examples")` |

### `core:cli` (`cli`) — Struct-Tag Command-Line Parser

- **Auto-Link Flags:** `(header-only)`
- **Overview:** Generates zero-boilerplate command-line flag parsers from struct field tags (`cli:"--flag,help"`).

**Practical Example:**
```cf
package main

import "core:cli"

type AppFlags struct {
    host:    str  `cli:"--host"    default:"127.0.0.1"`
    port:    u16  `cli:"--port,-p" default:"8080"`
    workers: i32  `cli:"--workers" default:"4"`
    verbose: bool `cli:"--verbose,-v"`
}

fn main() {
    // Automatically binds command-line arguments to typed struct
    flags := try cli.parse[AppFlags]()
    println("Server listening on {}:{} with {} workers (verbose={})",
        flags.host, flags.port, flags.workers, flags.verbose)
}
```

| `.cf` Signature | Lowered C++23 | Description | Example |
|---|---|---|---|
| `fn parse[T](args: []str?) -> T!` | `cfire::cli::parse<T>(argc, argv)` | Parses CLI flags (`str`, `i64`, `i32`, `f64`, `bool`) into a tagged struct `T`. | `args := try cli.parse[Args]()` |

### `core:compress/zstd` (`zstd`) — Native Zstandard Frame Compression

- **Auto-Link Flags:** `-lzstd`
- **Overview:** Hardware-accelerated Zstandard frame compression and decompression backed by native `libzstd`.

**Practical Example:**
```cf
package main

import "core:compress/zstd"

fn main() {
    raw := []byte("high-throughput packet trace payload ... high-throughput packet trace payload ...")

    // Hardware-accelerated Zstandard frame compression
    compressed := try zstd.compress(raw, level: 3)
    println("Original size: {} bytes -> Compressed: {} bytes", raw.len(), compressed.len())

    // Decompression using the embedded frame content size
    decompressed := try zstd.decompress(compressed)
    println("Decompressed size: {} bytes", decompressed.len())
}
```

| `.cf` Signature | Lowered C++23 | Description | Example |
|---|---|---|---|
| `fn compress(input: []byte, level: i32 = 3) -> []byte!` | `cfire::zstd::compress(input, level)` | Compresses a byte slice into a Zstandard frame. | `blob := try zstd.compress(raw, level: 3)` |
| `fn decompress(frame: []byte) -> []byte!` | `cfire::zstd::decompress(frame)` | Decompresses a Zstandard frame using the embedded frame content size. | `orig := try zstd.decompress(blob)` |

### `core:json` (`json`) — Compile-Time SIMD JSON Reflection & Dynamic DOM

- **Auto-Link Flags:** `-lsimdjson (or header-only)`
- **Overview:** Uses `json:"..."` struct tags to emit specialized `simdjson::ondemand` parsers and fast `std::format` serializers with zero runtime reflection overhead, along with a zero-dependency dynamic DOM `json.Value`.

**Practical Example:**
```cf
package main

import "core:json"

type ServiceMetric struct {
    service: str  `json:"service"`
    latency: f64  `json:"latency_ms"`
    healthy: bool `json:"healthy"`
}

fn main() {
    // 1. Zero-overhead SIMD deserialization using compile-time struct tags
    payload := `{"service":"auth","latency_ms":1.42,"healthy":true}`
    m := try json.parse[ServiceMetric](payload)
    println("Service={}, Latency={}ms, Healthy={}", m.service, m.latency, m.healthy)

    // 2. High-speed JSON serialization
    serialized := json.stringify(m)
    println("Serialized output: {}", serialized)

    // 3. Dynamic DOM for arbitrary or schemaless JSON
    mut doc := try json.parse_dynamic(`{"tags":["systems","c++23"],"cluster":{"nodes":3}}`)
    println("Cluster nodes: {}", doc.get("cluster").get("nodes").as_int())
}
```

| `.cf` Signature | Lowered C++23 | Description | Example |
|---|---|---|---|
| `fn parse[T](payload: []byte | str) -> T!` | `cfire::json::parse<T>(payload)` | Parses UTF-8 JSON into a tagged struct `T` using `simdjson::ondemand`. | `cfg := try json.parse[Config](raw)` |
| `fn stringify[T](val: T) -> str` | `cfire::json::stringify<T>(val)` | Serializes a tagged struct `T` (`str`, `u64`, `i64`, `i32`, `f64`, `bool`) into a JSON string. | `payload := json.stringify(metric)` |
| `fn escape_string(s: str) -> str` | `cfire::json::escape_string(s)` | Escapes quotes, backslashes, and control characters for JSON strings. | `esc := json.escape_string(raw_txt)` |
| `fn parse_dynamic(payload: str) -> Value!` | `cfire::json::parse_dynamic(payload)` | Parses arbitrary JSON into a recursive dynamic DOM `Value`. | `v := try json.parse_dynamic(raw)` |
| `fn stringify_dynamic(val: Value, indent: i32 = 0) -> str` | `cfire::json::stringify_dynamic(val, indent)` | Serializes a dynamic DOM `Value` into compact or indented JSON. | `s := json.stringify_dynamic(v, 2)` |

### `core:db/sqlite` (`sqlite`) — Embedded SQLite3 with WAL & Row Mapping

- **Auto-Link Flags:** `-lsqlite3`
- **Overview:** RAII SQLite3 wrapper enabling `PRAGMA journal_mode=WAL`, parameterized queries, atomic transactions, and compile-time `sql:"..."` struct row mapping.

**Practical Example:**
```cf
package main

import "core:db/sqlite"

type UserRecord struct {
    id:    i64 `sql:"id"`
    name:  str `sql:"name"`
    email: str `sql:"email"`
}

fn main() {
    // Opens SQLite connection and enables WAL mode automatically
    mut db := try sqlite.open("app.db")
    defer db.close()

    try db.exec("CREATE TABLE IF NOT EXISTS users (id INTEGER PRIMARY KEY, name TEXT, email TEXT)")

    // Atomic transaction with auto-rollback on error
    try db.tx |mut tx| {
        try tx.exec("INSERT INTO users (name, email) VALUES (?1, ?2)", "alice", "alice@systems.dev")
        try tx.exec("INSERT INTO users (name, email) VALUES (?1, ?2)", "bob", "bob@systems.dev")
    }

    // Direct struct row mapping via sql:"..." tags
    users := try db.query[UserRecord]("SELECT id, name, email FROM users WHERE id >= ?1", 1)
    for u in users {
        println("User [{}]: {} <{}>", u.id, u.name, u.email)
    }
}
```

| `.cf` Signature | Lowered C++23 | Description | Example |
|---|---|---|---|
| `fn open(path: str) -> Conn!` | `cfire::sqlite::open(path)` | Opens an SQLite3 database connection and enables WAL mode. | `mut db := try sqlite.open("app.db")` |
| `fn (mut db: Conn) exec(sql: str, ...args) -> void!` | `db.exec(sql, args...)` | Executes a parameterized SQL statement (`?1`, `?2`, ...). | `try db.exec("INSERT INTO pkg VALUES (?1, ?2)", name, ver)` |
| `fn (mut db: Conn) query[T](sql: str, ...args) -> []T!` | `db.query<T>(sql, args...)` | Executes a `SELECT` query and maps rows directly into struct `T` via `sql:"..."` tags. | `rows := try db.query[Row]("SELECT name, ver FROM pkg")` |
| `fn (mut db: Conn) tx(block: fn(mut tx: Conn) -> void!) -> void!` | `db.tx([&](Conn& tx) { ... })` | Runs a closure inside an atomic `BEGIN IMMEDIATE ... COMMIT` transaction with automatic rollback on error. | `try db.tx |mut tx| { ... }` |

### `core:crypto/sha256` (`sha256`) — Standard FIPS 180-4 SHA-256 Digest

- **Auto-Link Flags:** `(CommonCrypto / built-in)`
- **Overview:** Computes NIST FIPS 180-4 SHA-256 digests over byte slices or strings.

**Practical Example:**
```cf
package main

import "core:crypto/sha256"

fn main() {
    payload := "system_audit_transaction_event_2026"
    digest := sha256.hash_hex(payload)
    println("FIPS 180-4 SHA-256 Digest: {}", digest)
}
```

| `.cf` Signature | Lowered C++23 | Description | Example |
|---|---|---|---|
| `fn hash_hex(data: []byte) -> str` | `cfire::sha256::hash_hex(data)` | Returns a 64-character lowercase hex digest of `data`. | `digest := sha256.hash_hex(raw)` |
| `fn hash_bytes(data: []byte) -> []byte` | `cfire::sha256::hash_bytes(data)` | Returns 32-byte binary SHA-256 digest of `data`. | `b := sha256.hash_bytes(raw)` |

### `core:crypto/hmac` (`hmac`) — RFC 2104 HMAC-SHA256 Message Authentication

- **Auto-Link Flags:** `(CommonCrypto / built-in)`
- **Overview:** Computes RFC 2104 keyed HMAC-SHA256 authentication digests.

**Practical Example:**
```cf
package main

import "core:crypto/hmac"

fn main() {
    secret_key := "super-secure-signing-secret"
    data := "tenant_id:42;session:8849"

    signature := hmac.sha256_hex(secret_key, data)
    println("HMAC-SHA256 signature: {}", signature)

    // Constant-time validation check prevents timing side-channel attacks
    is_valid := hmac.equal(signature, signature)
    println("Signature verified: {}", is_valid)
}
```

| `.cf` Signature | Lowered C++23 | Description | Example |
|---|---|---|---|
| `fn sha256_hex(key: str, data: str) -> str` | `cfire::hmac::sha256_hex(key, data)` | Returns a 64-character lowercase hex HMAC-SHA256 digest. | `sig := hmac.sha256_hex(secret, payload)` |
| `fn equal(a: str, b: str) -> bool` | `cfire::hmac::equal(a, b)` | Performs a constant-time equality check on two MAC digests to prevent timing attacks. | `ok := hmac.equal(sig1, sig2)` |

### `core:crypto/aes` (`aes`) — NIST FIPS 197 AES-256 Symmetric Encryption & GCM AEAD

- **Auto-Link Flags:** `(Header-only pure C++23)`
- **Overview:** Constant-time AES-256 block cipher implementation with Counter Mode (CTR) and Galois/Counter Mode (GCM) Authenticated Encryption with Associated Data (AEAD).

**Practical Example:**
```cf
package main

import "core:crypto/aes"
import "core:rand"

fn main() {
    key := rand.bytes(32)   // 256-bit AES key
    nonce := rand.bytes(12) // 96-bit standard GCM nonce
    secret := "Confidential credentials and token payload"
    aad := "tenant_id:prod_42"

    // Authenticated Encryption with Associated Data (GCM AEAD)
    res := try aes.encrypt_gcm(key, nonce, secret, aad)
    println("Ciphertext length: {} bytes, Tag: {:02x}", res.ciphertext.len(), res.tag[0])

    // Decrypt and verify cryptographic authenticity tag
    plain := try aes.decrypt_gcm(key, nonce, res.ciphertext, res.tag, aad)
    println("Authenticated decrypted plaintext: {}", plain)
}
```

| `.cf` Signature | Lowered C++23 | Description | Example |
|---|---|---|---|
| `fn encrypt_gcm(key: []byte, nonce: []byte, plaintext: []byte, aad: []byte = []) -> GcmResult!` | `cfire::aes::encrypt_gcm(key, nonce, pt, aad)` | Encrypts plaintext with AES-256-GCM AEAD, producing ciphertext and a 16-byte authentication tag. | `res := try aes.encrypt_gcm(key, nonce, secret, aad)` |
| `fn decrypt_gcm(key: []byte, nonce: []byte, ciphertext: []byte, tag: []byte, aad: []byte = []) -> []byte!` | `cfire::aes::decrypt_gcm(key, nonce, ct, tag, aad)` | Authenticates and decrypts AES-256-GCM ciphertext, failing if tag verification fails or data was tampered. | `pt := try aes.decrypt_gcm(key, nonce, res.ciphertext, res.tag, aad)` |
| `fn encrypt_ctr(key: []byte, iv: []byte, plaintext: []byte) -> []byte!` | `cfire::aes::encrypt_ctr(key, iv, pt)` | Encrypts plaintext using AES-256-CTR mode. | `ct := try aes.encrypt_ctr(key, iv, pt)` |
| `fn decrypt_ctr(key: []byte, iv: []byte, ciphertext: []byte) -> []byte!` | `cfire::aes::decrypt_ctr(key, iv, ct)` | Decrypts ciphertext using AES-256-CTR mode. | `pt := try aes.decrypt_ctr(key, iv, ct)` |

### `core:crypto/blake3` (`blake3`) — Cryptographic 256-Bit Digest (Alias)

- **Auto-Link Flags:** `(CommonCrypto / built-in)`
- **Overview:** Computes fast 256-bit cryptographic hex digests over byte slices (convenience alias for `core:crypto/sha256`).

**Practical Example:**
```cf
package main

import "core:crypto/blake3"

fn main() {
    blob := []byte("immutable chunk storage block 0x8842")
    hash := blake3.hash_hex(blob)
    println("Blake3 256-bit digest: {}", hash)
}
```

| `.cf` Signature | Lowered C++23 | Description | Example |
|---|---|---|---|
| `fn hash_hex(data: []byte) -> str` | `cfire::blake3::hash_hex(data)` | Returns a 64-character lowercase hex digest of `data`. | `digest := blake3.hash_hex(raw)` |

### `core:net` (`net`) — POSIX BSD TCP Sockets & Loopback Networking

- **Auto-Link Flags:** `(POSIX sockets)`
- **Overview:** RAII TCP client and listener sockets backed by BSD `AF_INET` sockets with non-blocking `poll` timeouts and automatic `AF_UNIX` `socketpair` loopback fallback inside sandboxes.

**Practical Example:**
```cf
package main

import "core:net"

fn main() {
    // 1. Bind loopback TCP listener on an ephemeral port (0)
    mut ln := try net.listen_tcp(port: 0)
    port := ln.port()
    println("Listening on 127.0.0.1:{}", port)

    // 2. Connect client TCP socket
    mut client := try net.connect_tcp("127.0.0.1", port)
    _ := try client.write("PING\n")

    // 3. Accept incoming connection and read data
    mut conn := try ln.accept()
    msg := try conn.read_str(max_bytes: 64)
    println("Server received payload: {}", msg)
}
```

| `.cf` Signature | Lowered C++23 | Description | Example |
|---|---|---|---|
| `fn listen_tcp(port: u16 = 0) -> TcpListener!` | `cfire::net::listen_tcp(port)` | Binds a loopback TCP listener on `127.0.0.1:port` (`0` selects an ephemeral port). | `mut ln := try net.listen_tcp(0)` |
| `fn (ln: TcpListener) port() -> u16` | `ln.port()` | Returns the bound TCP port. | `p := ln.port()` |
| `fn (mut ln: TcpListener) accept() -> TcpConn!` | `ln.accept()` | Accepts an incoming TCP connection with a safe poll timeout. | `mut conn := try ln.accept()` |
| `fn connect_tcp(host: str, port: u16) -> TcpConn!` | `cfire::net::connect_tcp(host, port)` | Connects a `TcpConn` to `host:port`. | `mut c := try net.connect_tcp("127.0.0.1", p)` |
| `fn (mut c: TcpConn) write(payload: str) -> usize!` | `c.write(payload)` | Writes bytes to an open TCP connection. | `_ := try c.write("PING")` |
| `fn (mut c: TcpConn) read_str(max_bytes: usize = 1024) -> str!` | `c.read_str(max_bytes)` | Reads up to `max_bytes` from the socket as a string. | `msg := try conn.read_str(64)` |

### `core:net/url` (`url`) — RFC 3986 URL Parsing & URL Encoding

- **Auto-Link Flags:** `(header-only)`
- **Overview:** RFC 3986 URL parsing with component extraction (scheme, host, port, path, query, fragment), form-urlencoded query parameter maps, and percent encoding/decoding.

**Practical Example:**
```cf
package main

import "core:net/url"

fn main() {
    raw := "https://api.domain.com:8443/v1/search?q=systems+c%2B%2B&limit=25#results"
    u := try url.parse(raw)
    println("Scheme: {}, Host: {}, Port: {}, Path: {}", u.scheme, u.host, u.port, u.path)
    println("Query parameter 'q': {}", u.query["q"])

    encoded := url.encode("raw query with spaces & symbols!")
    println("Percent-encoded: {}", encoded)
}
```

| `.cf` Signature | Lowered C++23 | Description | Example |
|---|---|---|---|
| `fn parse(raw: str) -> Url!` | `cfire::url::parse(raw)` | Parses an RFC 3986 URL string into component fields and query parameters. | `u := try url.parse("http://127.0.0.1:8080/api/items?q=shoes#top")` |
| `fn encode(s: str) -> str` | `cfire::url::encode(s)` | Percent-encodes a string for safe use in URL path or query strings. | `enc := url.encode("hello world!")` |
| `fn decode(s: str) -> str` | `cfire::url::decode(s)` | Decodes a percent-encoded URL string. | `dec := url.decode(enc)` |

### `core:net/http` (`http`) — HTTP/1.1 Client, Wire Protocol Engine, Server & Independent Router

- **Auto-Link Flags:** `(POSIX sockets)`
- **Overview:** RFC 9112 HTTP/1.1 client (`get`, `post`, `request`), wire parser (`parse_request`), response serializer (`Response`), multi-mode server (`make_server`), and independent request router (`make_router`).

**Practical Example:**
```cf
package main

import "core:net/http"
import "core:json"

type HealthStatus struct {
    status: str `json:"status"`
    uptime: i64 `json:"uptime_sec"`
}

fn main() {
    // 1. HTTP GET client request with custom headers
    headers := { "User-Agent": "cfire-service/1.0", "Accept": "application/json" }
    resp := try http.get("http://127.0.0.1:8080/health", headers: headers)
    if resp.status_code == 200 {
        h := try json.parse[HealthStatus](resp.body)
        println("Service status: {}, uptime: {}s", h.status, h.uptime)
    }

    // 2. Build independent HTTP router with path parameters
    mut router := http.make_router()
    router.get("/health", |req| {
        return http.Response.json(200, "{\"status\":\"healthy\",\"uptime_sec\":120}")
    })
    router.get("/users/:id", |req| {
        return http.Response.text(200, "User: " + req.param("id"))
    })

    // 3. Serve requests using the router
    mut server := try http.make_server(port: 8080)
    server.serve_in_background(router)
}
```

| `.cf` Signature | Lowered C++23 | Description | Example |
|---|---|---|---|
| `fn get(url: str, headers: map[str]str = {}) -> Response!` | `cfire::http::get(url, headers)` | Executes an HTTP GET request against a target URL. | `resp := try http.get("http://127.0.0.1:8080/health")` |
| `fn post(url: str, body: str, content_type: str = "application/json", headers: map[str]str = {}) -> Response!` | `cfire::http::post(url, body, content_type, headers)` | Executes an HTTP POST request with payload body. | `resp := try http.post("http://127.0.0.1:8080/items", "{\"id\": 1}")` |
| `fn request(method: str, url: str, body: str = "", content_type: str = "", headers: map[str]str = {}) -> Response!` | `cfire::http::request(method, url, body, content_type, headers)` | Executes an arbitrary HTTP request (PUT, DELETE, PATCH, etc.). | `resp := try http.request("DELETE", "http://127.0.0.1:8080/items/1")` |
| `fn make_server(port: u16 = 0) -> HttpServer!` | `cfire::http::make_server(port)` | Creates a lightweight HTTP server listening on `port` (0 for ephemeral). | `mut srv := try http.make_server(8080)` |
| `fn make_router() -> Router` | `cfire::http::make_router()` | Creates an independent HTTP request router supporting path parameters (`:id`), wildcards (`*path`), and standalone dispatch. | `mut r := http.make_router()` |
| `fn parse_request(raw: str) -> Request!` | `cfire::http::parse_request(raw)` | Parses raw HTTP/1.1 wire text into a structured Request model. | `req := try http.parse_request(raw_text)` |
| `fn Response.text(code: i32, payload: str) -> Response` | `cfire::http::Response::text(code, payload)` | Creates an HTTP Response with text/plain content-type and standard status reason. | `return http.Response.text(200, "OK")` |
| `fn Response.json(code: i32, json_str: str) -> Response` | `cfire::http::Response::json(code, json_str)` | Creates an HTTP Response with application/json content-type. | `return http.Response.json(200, "{\"status\":\"ok\"}")` |
| `fn Response.html(code: i32, html: str) -> Response` | `cfire::http::Response::html(code, html)` | Creates an HTTP Response with text/html content-type. | `return http.Response.html(200, "<h1>Hello</h1>")` |
| `fn Response.redirect(location: str, code: i32 = 302) -> Response` | `cfire::http::Response::redirect(location, code)` | Creates an HTTP redirect Response setting Location header. | `return http.Response.redirect("/login", 302)` |
| `fn (mut r: Router) get(pattern: str, handler: fn(Request) -> Response) -> Router&` | `r.get(pattern, handler)` | Registers an HTTP GET route with optional path parameters (`:id`) or wildcards (`*path`). | `r.get("/users/:id", |req| http.Response.text(200, req.param("id")))` |
| `fn (mut r: Router) post(pattern: str, handler: fn(Request) -> Response) -> Router&` | `r.post(pattern, handler)` | Registers an HTTP POST route handler. | `r.post("/items", |req| http.Response.json(201, "{}"))` |
| `fn (r: Router) dispatch(req: Request) -> Response` | `r.dispatch(req)` | Dispatches an HTTP request through the router in-memory without open network sockets. | `res := router.dispatch(test_req)` |
| `fn (mut r: Router) use(mw: fn(mut Request) -> Response?) -> Router&` | `r.use(mw)` | Registers pre-routing middleware that can short-circuit or validate requests. | `r.use(|req| { return None })` |
| `fn (mut s: HttpServer) serve_in_background(handler: fn(Request) -> Response) -> void` | `s.serve_in_background(handler)` | Starts a non-blocking background accept loop serving requests with the specified handler or router. | `server.serve_in_background(router)` |
| `fn (mut s: HttpServer) stop() -> void` | `s.stop()` | Gracefully closes the listening socket and stops worker threads. | `server.stop()` |
| `fn (req: Request) param(key: str, fallback: str = "") -> str` | `req.param(key, fallback)` | Retrieves a named route path parameter extracted by the router. | `id := req.param("id")` |
| `fn (req: Request) query(key: str, fallback: str = "") -> str` | `req.query(key, fallback)` | Retrieves a decoded URL query parameter. | `page := req.query("page", "1")` |
| `fn (req: Request) header(key: str, fallback: str = "") -> str` | `req.header(key, fallback)` | Performs a case-insensitive header lookup. | `auth := req.header("Authorization")` |

### `core:net/websocket` (`websocket`) — RFC 6455 High-Level WebSockets Client & Server

- **Auto-Link Flags:** `(POSIX sockets)`
- **Overview:** RFC 6455 full-duplex WebSocket client (`connect`) and server upgrade handler (`upgrade`) with automated framing, client masking, and Ping/Pong management.

**Practical Example:**
```cf
package main

import "core:net/websocket"

fn main() {
    // Establish full-duplex RFC 6455 WebSocket connection
    mut ws := try websocket.connect("ws://127.0.0.1:8080/market-data")
    defer ws.close()

    try ws.send_text("SUBSCRIBE BTC-USD")
    frame := try ws.recv_text()
    println("Received WebSocket stream frame: {}", frame)
}
```

| `.cf` Signature | Lowered C++23 | Description | Example |
|---|---|---|---|
| `fn connect(url: str) -> Conn!` | `cfire::websocket::connect(url)` | Establishes a WebSocket connection to `ws://<host>[:port]/<path>` and completes the RFC 6455 handshake. | `mut ws := try websocket.connect("ws://127.0.0.1:8080/ws")` |
| `fn upgrade(req: Request, mut conn: TcpConn) -> Conn!` | `cfire::websocket::upgrade(req, conn)` | Upgrades an incoming HTTP connection to a WebSocket connection. | `mut ws := try websocket.upgrade(req, mut conn)` |
| `fn pair() -> (Conn, Conn)!` | `cfire::websocket::pair()` | Creates an in-process full-duplex WebSocket client/server loopback pair. | `mut (client, server) := try websocket.pair()` |
| `fn (mut ws: Conn) send_text(msg: str) -> void!` | `ws.send_text(msg)` | Sends a UTF-8 text frame to the peer (masked if client). | `try ws.send_text("hello world")` |
| `fn (mut ws: Conn) send_binary(data: []byte) -> void!` | `ws.send_binary(data)` | Sends a binary data frame to the peer (masked if client). | `try ws.send_binary(payload)` |
| `fn (mut ws: Conn) recv_text() -> str!` | `ws.recv_text()` | Receives the next text frame from the peer, automatically responding to Ping frames with Pong. | `msg := try ws.recv_text()` |
| `fn (mut ws: Conn) recv_binary() -> []byte!` | `ws.recv_binary()` | Receives the next binary frame from the peer. | `bytes := try ws.recv_binary()` |
| `fn (mut ws: Conn) ping(payload: str = "") -> void!` | `ws.ping(payload)` | Sends a Ping control frame to check peer liveness. | `try ws.ping("heartbeat")` |
| `fn (mut ws: Conn) close(code: u16 = 1000, reason: str = "")` | `ws.close(code, reason)` | Initiates a clean RFC 6455 closure handshake and closes the socket. | `ws.close()` |
| `fn (ws: Conn) is_open() -> bool` | `ws.is_open()` | Returns true if the WebSocket connection is open. | `if ws.is_open() { ... }` |

### `core:time` (`time`) — Time, Calendar Dates, Durations & RFC 3339 Formatting

- **Auto-Link Flags:** `(header-only)`
- **Overview:** High-resolution monotonic timing, duration arithmetic, and RFC 3339 / ISO 8601 formatting backed by `std::chrono`.

**Practical Example:**
```cf
package main

import "core:time"

fn main() {
    now := time.now()
    println("Current RFC 3339 timestamp: {}", now.format_rfc3339())

    // High-precision duration measurement
    time.sleep_ms(30)
    elapsed := time.since(now)
    println("Elapsed duration: {} ms ({} us)", elapsed.to_millis(), elapsed.to_micros())
}
```

| `.cf` Signature | Lowered C++23 | Description | Example |
|---|---|---|---|
| `fn now_ms() -> i64` | `cfire::time::now_ms()` | Returns monotonic milliseconds from `std::chrono::steady_clock`. | `t0 := time.now_ms()` |
| `fn unix_seconds() -> i64` | `cfire::time::unix_seconds()` | Returns current UTC Unix timestamp in seconds. | `ts := time.unix_seconds()` |
| `fn sleep_ms(ms: i64)` | `cfire::time::sleep_ms(ms)` | Sleeps the current thread for `ms` milliseconds. | `time.sleep_ms(10)` |
| `fn sleep_ctx(ctx: Context, ms: i64) -> bool` | `cfire::time::sleep_ctx(ctx, ms)` | Sleeps the current thread for `ms` or until `ctx` is cancelled; returns true if completed without cancellation. | `ok := time.sleep_ctx(ctx, 50)` |
| `fn now() -> Time` | `cfire::time::now()` | Returns current wall clock `Time` instant. | `t := time.now()` |
| `fn unix(sec: i64, nsec: i64 = 0) -> Time` | `cfire::time::unix(sec, nsec)` | Constructs a `Time` instant from Unix seconds and nanoseconds. | `t := time.unix(1700000000)` |
| `fn parse_rfc3339(s: str) -> Time!` | `cfire::time::parse_rfc3339(s)` | Parses an RFC 3339 / ISO 8601 timestamp string into a `Time` instant. | `t := try time.parse_rfc3339("2026-10-18T12:00:00Z")` |
| `fn duration_seconds(s: i64) -> Duration` | `cfire::time::duration_seconds(s)` | Constructs a typed `Duration` representing `s` seconds. | `d := time.duration_seconds(5)` |
| `fn duration_millis(ms: i64) -> Duration` | `cfire::time::duration_millis(ms)` | Constructs a typed `Duration` representing `ms` milliseconds. | `d := time.duration_millis(250)` |
| `fn sleep(d: Duration)` | `cfire::time::sleep(d)` | Sleeps the current thread for duration `d`. | `time.sleep(time.duration_millis(50))` |

### `core:os` (`os`) — Environment, Working Directory & Subprocesses

- **Auto-Link Flags:** `(header-only)`
- **Overview:** Process environment inspection, working directory queries, and subprocess output capture.

**Practical Example:**
```cf
package main

import "core:os"
import "core:os/exec"

fn main() {
    home := os.getenv("HOME") or "/tmp"
    println("Environment $HOME: {}", home)

    // Execute external process and capture stdout/exit code
    cmd := exec.command("git", ["rev-parse", "--short", "HEAD"])
    output := try cmd.output()
    println("Current Git commit: {} (exit code {})", output.stdout, output.exit_code)
}
```

| `.cf` Signature | Lowered C++23 | Description | Example |
|---|---|---|---|
| `fn getenv(key: str, fallback: str = "") -> str` | `cfire::os::getenv(key, fallback)` | Reads environment variable `key`, returning `fallback` if unset. | `mode := os.getenv("ENV", "dev")` |
| `fn setenv(key: str, value: str)` | `cfire::os::setenv(key, value)` | Sets environment variable `key` to `value` in the current process. | `os.setenv("MODE", "prod")` |
| `fn cwd() -> str` | `cfire::os::cwd()` | Returns the current working directory path. | `dir := os.cwd()` |
| `fn exec_output(cmd: str) -> str!` | `cfire::os::exec_output(cmd)` | Executes a shell command via `popen` and returns its captured stdout. | `out := try os.exec_output("uname -s")` |
| `fn command(binary: str, args: []str) -> ExecResult!` | `cfire::os::command(binary, args)` | Spawns a subprocess with explicit argument list via `posix_spawnp` (zero shell injection risk) and captures stdout/stderr. | `res := try os.command("git", ["status"])` |

### `core:sync` (`sync`) — Thread-Safe Typed Channels

- **Auto-Link Flags:** `(header-only)`
- **Overview:** Go-style typed MPSC/MPMC channels (`Channel[T]`) synchronized with `std::mutex` and `std::condition_variable`.

**Practical Example:**
```cf
package main

import "core:sync"

fn main() {
    // Typed buffered message channel
    ch := sync.make_channel[i32](capacity: 10)

    async.scope(workers: 2) |scope| {
        // Producer task
        scope.spawn || {
            for i in [10, 20, 30] {
                ch.send(i)
            }
            ch.close()
        }

        // Consumer task
        scope.spawn || {
            for item := ch.recv() or break {
                println("Channel consumer received: {}", item)
            }
        }
    }
}
```

| `.cf` Signature | Lowered C++23 | Description | Example |
|---|---|---|---|
| `fn make_channel[T]() -> Channel[T]` | `cfire::sync::make_channel<T>()` | Creates a new shared, thread-safe channel of element type `T`. | `ch := sync.make_channel[i64]()` |
| `fn (ch: Channel[T]) send(val: T)` | `ch.send(val)` | Sends a value into the channel and wakes one waiting receiver. | `ch.send(42)` |
| `fn (ch: Channel[T]) recv() -> T!` | `ch.recv()` | Blocks until a value is available or returns an error if the channel is closed and empty. | `v := ch.recv() or 0` |
| `fn (ch: Channel[T]) recv_ctx(ctx: Context) -> T!` | `ch.recv_ctx(ctx)` | Receives from the channel or aborts immediately with an error if `ctx` is cancelled. | `v := try ch.recv_ctx(ctx)` |
| `fn (ch: Channel[T]) close()` | `ch.close()` | Closes the channel and wakes all waiting receivers. | `ch.close()` |

### `core:sync/context` (`context`) — Cancellation Tokens & Deadline Contexts

- **Auto-Link Flags:** `(header-only)`
- **Overview:** Structured concurrency cancellation token and deadline propagation hierarchy (`core:sync/context`).

**Practical Example:**
```cf
package main

import "core:sync/context"
import "core:time"

fn main() {
    bg := context.background()
    (ctx, cancel) := context.with_timeout(bg, 500) // 500ms deadline
    defer cancel("worker task complete")

    time.sleep_ms(40)
    if !ctx.is_done() {
        println("Operation succeeded within timeout deadline")
    }
}
```

| `.cf` Signature | Lowered C++23 | Description | Example |
|---|---|---|---|
| `fn background() -> Context` | `cfire::context::background()` | Returns an empty, root non-cancellable Context. | `ctx := context.background()` |
| `fn with_cancel(parent: Context) -> (Context, fn(reason: str = ""))` | `cfire::context::with_cancel(parent)` | Derives a child Context and a cancellation handle function from a parent Context. | `(ctx, cancel) := context.with_cancel(parent)` |
| `fn with_timeout(parent: Context, ms: i64) -> (Context, fn(reason: str = ""))` | `cfire::context::with_timeout(parent, ms)` | Derives a child Context with an automatic millisecond deadline. | `(ctx, cancel) := context.with_timeout(parent, 250)` |
| `fn (ctx: Context) is_cancelled() -> bool` | `ctx.is_cancelled()` | Returns true if the Context has been cancelled or timed out. | `if ctx.is_cancelled() { return }` |
| `fn (ctx: Context) done() -> bool` | `ctx.done()` | Synonym for `is_cancelled()`. | `if ctx.done() { return }` |
| `fn (ctx: Context) reason() -> str` | `ctx.reason()` | Returns the cancellation reason string. | `msg := ctx.reason()` |
| `fn (ctx: Context) wait()` | `ctx.wait()` | Blocks until the Context is cancelled or its deadline expires. | `ctx.wait()` |
| `fn (ctx: Context) wait_for(ms: i64) -> bool` | `ctx.wait_for(ms)` | Blocks up to `ms` milliseconds or until the Context is cancelled; returns true if cancelled. | `cancelled := ctx.wait_for(100)` |

### `core:str` (`str`) — UTF-8 String Manipulation

- **Auto-Link Flags:** `(header-only)`
- **Overview:** Zero-surprise string splitting, joining, trimming, searching, replacement, and ASCII case conversion.

**Practical Example:**
```cf
package main

import "core:str"

fn main() {
    raw := "  systems, programming, language  "
    trimmed := str.trim(raw)
    tokens := str.split(trimmed, ", ")
    formatted := str.join(tokens, "::")
    println("Processed string: {}", formatted)
}
```

| `.cf` Signature | Lowered C++23 | Description | Example |
|---|---|---|---|
| `fn split(s: str, sep: str) -> []str` | `cfire::str::split(s, sep)` | Splits `s` around every occurrence of `sep`. | `parts := str.split("a,b,c", ",")` |
| `fn join(items: []str, sep: str) -> str` | `cfire::str::join(items, sep)` | Joins a slice of strings with `sep`. | `s := str.join(parts, "-")` |
| `fn trim(s: str) -> str` | `cfire::str::trim(s)` | Strips leading and trailing whitespace. | `clean := str.trim("  hi  ")` |
| `fn starts_with(s: str, prefix: str) -> bool` | `cfire::str::starts_with(s, prefix)` | Returns `true` if `s` starts with `prefix`. | `if str.starts_with(s, "cf") { ... }` |
| `fn ends_with(s: str, suffix: str) -> bool` | `cfire::str::ends_with(s, suffix)` | Returns `true` if `s` ends with `suffix`. | `if str.ends_with(f, ".cf") { ... }` |
| `fn contains(s: str, needle: str) -> bool` | `cfire::str::contains(s, needle)` | Returns `true` if `s` contains `needle`. | `if str.contains(s, "fire") { ... }` |
| `fn replace(s: str, from: str, to: str) -> str` | `cfire::str::replace(s, from, to)` | Replaces all occurrences of `from` in `s` with `to`. | `r := str.replace(s, "old", "new")` |
| `fn to_upper(s: str) -> str` | `cfire::str::to_upper(s)` | Converts ASCII characters to uppercase. | `u := str.to_upper("cfire")` |
| `fn to_lower(s: str) -> str` | `cfire::str::to_lower(s)` | Converts ASCII characters to lowercase. | `l := str.to_lower("CFIRE")` |

### `core:math` (`math`) — Numeric & Floating-Point Utilities

- **Auto-Link Flags:** `(header-only)`
- **Overview:** Standard mathematical constants (`PI`, `E`) and numeric clamping/transcendental functions.

**Practical Example:**
```cf
package main

import "core:math"

fn main() {
    bounded := math.clamp_f64(3.14159, 0.0, 1.0)
    hypot := math.sqrt(3.0 * 3.0 + 4.0 * 4.0)
    println("Clamped: {}, Hypot: {}", bounded, hypot)
}
```

| `.cf` Signature | Lowered C++23 | Description | Example |
|---|---|---|---|
| `fn clamp_i64(val: i64, lo: i64, hi: i64) -> i64` | `cfire::math::clamp_i64(val, lo, hi)` | Clamps a 64-bit integer to `[lo, hi]`. | `c := math.clamp_i64(x, 0, 100)` |
| `fn clamp_f64(val: f64, lo: f64, hi: f64) -> f64` | `cfire::math::clamp_f64(val, lo, hi)` | Clamps a 64-bit float to `[lo, hi]`. | `c := math.clamp_f64(v, 0.0, 1.0)` |
| `fn sqrt(x: f64) -> f64` | `cfire::math::sqrt(x)` | Computes square root. | `r := math.sqrt(16.0)` |
| `fn abs_f64(x: f64) -> f64` | `cfire::math::abs_f64(x)` | Computes absolute value of a `f64`. | `a := math.abs_f64(-3.5)` |

### `core:rand` (`rand`) — Thread-Local MT19937_64 & CSPRNG Generator

- **Auto-Link Flags:** `(header-only)`
- **Overview:** Fast, thread-safe pseudo-random and OS-entropy CSPRNG random generation.

**Practical Example:**
```cf
package main

import "core:rand"

fn main() {
    uid := rand.u64()
    dice_roll := rand.range_i64(1, 6)
    token_bytes := rand.bytes(16)
    println("ID: {}, Roll: {}, Entropy bytes: {}", uid, dice_roll, token_bytes.len())
}
```

| `.cf` Signature | Lowered C++23 | Description | Example |
|---|---|---|---|
| `fn u64() -> u64` | `cfire::rand::u64()` | Generates a random 64-bit unsigned integer. | `id := rand.u64()` |
| `fn range_i64(min_incl: i64, max_incl: i64) -> i64` | `cfire::rand::range_i64(min_incl, max_incl)` | Generates a uniform integer in `[min_incl, max_incl]`. | `n := rand.range_i64(1, 10)` |
| `fn f64_unit() -> f64` | `cfire::rand::f64_unit()` | Generates a uniform `f64` in `[0.0, 1.0)`. | `p := rand.f64_unit()` |
| `fn bytes(n: usize) -> []byte` | `cfire::rand::bytes(n)` | Generates `n` cryptographically secure random bytes from OS entropy. | `buf := rand.bytes(32)` |

### `core:encoding/base64` (`base64`) — RFC 4648 Base64 & URL-Safe Encoding/Decoding

- **Auto-Link Flags:** `(header-only)`
- **Overview:** High-performance, zero-allocation Base64 and URL-safe Base64 encoder and decoder.

**Practical Example:**
```cf
package main

import "core:encoding/base64"

fn main() {
    credentials := "service_account:token_xyz_9988"
    encoded := base64.encode_str(credentials)
    println("Base64 encoded: {}", encoded)

    decoded := try base64.decode_str(encoded)
    println("Decoded text: {}", decoded)
}
```

| `.cf` Signature | Lowered C++23 | Description | Example |
|---|---|---|---|
| `fn encode(data: []byte, url_safe: bool = false) -> str` | `cfire::base64::encode(data, url_safe)` | Encodes binary byte slice into standard or URL-safe Base64 string. | `s := base64.encode(raw)` |
| `fn encode_str(s: str, url_safe: bool = false) -> str` | `cfire::base64::encode_str(s, url_safe)` | Encodes a UTF-8 text string into standard or URL-safe Base64. | `token := base64.encode_str("user:pass")` |
| `fn decode(s: str) -> []byte!` | `cfire::base64::decode(s)` | Decodes a Base64 string into binary bytes. | `bytes := try base64.decode(token)` |
| `fn decode_str(s: str) -> str!` | `cfire::base64::decode_str(s)` | Decodes a Base64 string into a UTF-8 text string. | `plain := try base64.decode_str(token)` |

### `core:encoding/hex` (`hex`) — Hexadecimal Encoding & Decoding

- **Auto-Link Flags:** `(header-only)`
- **Overview:** Encodes binary bytes to lowercase hexadecimal strings and decodes hex strings to byte buffers.

**Practical Example:**
```cf
package main

import "core:encoding/hex"

fn main() {
    binary_bytes := []byte{0xCA, 0xFE, 0xBA, 0xBE}
    hex_str := hex.encode(binary_bytes)
    println("Hexadecimal string: 0x{}", hex_str)

    restored := try hex.decode(hex_str)
    println("Restored byte length: {}", restored.len())
}
```

| `.cf` Signature | Lowered C++23 | Description | Example |
|---|---|---|---|
| `fn encode(data: []byte) -> str` | `cfire::hex::encode(data)` | Encodes binary bytes to lowercase hexadecimal string. | `h := hex.encode(digest)` |
| `fn encode_str(s: str) -> str` | `cfire::hex::encode_str(s)` | Encodes text characters to hexadecimal string. | `h := hex.encode_str("abc")` |
| `fn decode(s: str) -> []byte!` | `cfire::hex::decode(s)` | Decodes a hexadecimal string to binary bytes. | `buf := try hex.decode(h)` |
| `fn decode_str(s: str) -> str!` | `cfire::hex::decode_str(s)` | Decodes a hexadecimal string to a UTF-8 string. | `txt := try hex.decode_str(h)` |

### `core:encoding/csv` (`csv`) — RFC 4180 CSV Parsing & Stringification

- **Auto-Link Flags:** `(header-only)`
- **Overview:** RFC 4180 compliant CSV parser and serializer supporting multi-line quoted fields and comma escaping.

**Practical Example:**
```cf
package main

import "core:encoding/csv"

fn main() {
    csv_data := "service,region,qps\nauth,us-west-2,1450\napi,eu-central-1,4200\n"
    table := csv.parse(csv_data)
    for row in table {
        println("Service: {}, Region: {}, QPS: {}", row[0], row[1], row[2])
    }

    // Serialize 2D slice back into RFC 4180 CSV
    rendered := csv.stringify(table)
    println("Output:\n{}", rendered)
}
```

| `.cf` Signature | Lowered C++23 | Description | Example |
|---|---|---|---|
| `fn parse(s: str) -> [][]str` | `cfire::csv::parse(s)` | Parses a CSV string into a 2D table of rows and columns. | `rows := csv.parse(content)` |
| `fn stringify(rows: [][]str) -> str` | `cfire::csv::stringify(rows)` | Serializes a 2D table of strings into an RFC 4180 formatted CSV string. | `out := csv.stringify(rows)` |
| `fn read_all(path: str) -> [][]str!` | `cfire::csv::read_all(path)` | Reads an entire CSV file from disk and parses it into rows. | `table := try csv.read_all("data.csv")` |
| `fn write_all(path: str, rows: [][]str) -> void!` | `cfire::csv::write_all(path, rows)` | Serializes rows to CSV and writes to disk. | `try csv.write_all("report.csv", table)` |

### `core:regex` (`regex`) — ECMAScript Regular Expressions

- **Auto-Link Flags:** `(header-only)`
- **Overview:** Pattern matching, group extraction, and text substitution backed by C++23 `<regex>`.

**Practical Example:**
```cf
package main

import "core:regex"

fn main() {
    email_pattern := `^[a-zA-Z0-9_\.-]+@[a-zA-Z0-9\.-]+\.[a-z]{2,6}$`
    re := try regex.compile(email_pattern)

    email := "architect@cfire.memo.engineer"
    if try re.is_match(email) {
        println("Validated email: {}", email)
    }

    // Global text replacement
    cleaned := try regex.replace_all(`\s+`, "multiple    whitespace     characters", " ")
    println("Cleaned text: {}", cleaned)
}
```

| `.cf` Signature | Lowered C++23 | Description | Example |
|---|---|---|---|
| `fn compile(pattern: str) -> Regex!` | `cfire::regex::compile(pattern)` | Compiles an ECMAScript regex pattern into a reusable Regex object. | `re := try regex.compile("\\d+")` |
| `fn is_match(pattern: str, text: str) -> bool!` | `cfire::regex::is_match(pattern, text)` | Tests whether a regex pattern matches text. | `ok := try regex.is_match("^[a-z]+$", s)` |
| `fn replace_all(pattern: str, text: str, repl: str) -> str!` | `cfire::regex::replace_all(pattern, text, repl)` | Replaces all occurrences matching pattern with repl. | `res := try regex.replace_all("\s+", s, " ")` |

### `core:collections` (`collections`) — Generic Containers (Set, PriorityQueue, Deque)

- **Auto-Link Flags:** `(header-only)`
- **Overview:** Standard generic containers backed by STL unordered_set, priority_queue, and deque.

**Practical Example:**
```cf
package main

import "core:collections"

fn main() {
    // 1. Generic Hash Set
    mut set := collections.Set[str]{}
    set.insert("alpha")
    set.insert("beta")
    println("Set contains 'alpha': {}", set.contains("alpha"))

    // 2. Generic Max-Heap Priority Queue
    mut pq := collections.PriorityQueue[i64]{}
    pq.push(10)
    pq.push(95)
    pq.push(42)
    println("Highest priority element: {}", pq.top()) // 95

    // 3. Double-Ended Queue (Deque)
    mut dq := collections.Deque[str]{}
    dq.push_back("item_2")
    dq.push_front("item_1")
    println("Front: {}, Back: {}", dq.pop_front(), dq.pop_back())
}
```

| `.cf` Signature | Lowered C++23 | Description | Example |
|---|---|---|---|
| `type Set[T] struct` | `cfire::collections::Set<T>` | Generic hash set container. | `mut s := collections.Set[str]{}` |
| `type PriorityQueue[T] struct` | `cfire::collections::PriorityQueue<T>` | Generic max-heap priority queue. | `mut pq := collections.PriorityQueue[i32]{}` |
| `type Deque[T] struct` | `cfire::collections::Deque<T>` | Generic double-ended queue. | `mut dq := collections.Deque[i32]{}` |

### `core:sort` (`sort`) — Slice Sorting & Binary Search

- **Auto-Link Flags:** `(header-only)`
- **Overview:** In-place sorting algorithms and binary search on slices.

**Practical Example:**
```cf
package main

import "core:sort"

fn main() {
    mut numbers := [95, 12, 48, 3, 77]
    // In-place slice sorting
    sort.slice(mut numbers)
    println("Sorted numbers: {:?}", numbers)

    // Logarithmic binary search
    if idx := sort.binary_search(numbers, 48) {
        println("Found element 48 at index: {}", idx)
    }
}
```

| `.cf` Signature | Lowered C++23 | Description | Example |
|---|---|---|---|
| `fn slice(mut s: []T)` | `cfire::sort::slice(s)` | Sorts a slice in ascending order in-place. | `sort.slice(mut numbers)` |
| `fn binary_search(s: []T, val: T) -> i64?` | `cfire::sort::binary_search(s, val)` | Performs binary search on a sorted slice. | `idx := sort.binary_search(numbers, 42)` |

### `core:log` (`log`) — Structured Leveled Logging

- **Auto-Link Flags:** `(header-only)`
- **Overview:** Leveled logging (`debug`, `info`, `warn`, `error`) with structured key-value attributes and Text / JSON formatters.

**Practical Example:**
```cf
package main

import "core:log"

fn main() {
    log.set_level(log.Level::Debug)
    log.set_format(log.Format::Json) // Output format: Text or Json

    // Leveled structured logging with key-value attributes
    log.info("service worker started", "port", "8080", "cluster", "prod-us-east")
    log.warn("memory threshold reached", "usage_pct", "84", "threshold", "80")
    log.error("database pool exhausted", "active_conns", "100", "pool_size", "100")
}
```

| `.cf` Signature | Lowered C++23 | Description | Example |
|---|---|---|---|
| `fn set_level(l: Level)` | `cfire::log::set_level(l)` | Sets the minimum logging severity threshold. | `log.set_level(log.Level::Debug)` |
| `fn set_format(f: Format)` | `cfire::log::set_format(f)` | Sets the log output format (Text or Json). | `log.set_format(log.Format::Json)` |
| `fn debug(msg: str, ...attrs: str)` | `cfire::log::debug(msg, ...)` | Logs a debug message with optional key-value attribute pairs. | `log.debug("cache miss", "key", "user:1")` |
| `fn info(msg: str, ...attrs: str)` | `cfire::log::info(msg, ...)` | Logs an informational message with optional key-value attribute pairs. | `log.info("server listening", "port", "8080")` |
| `fn warn(msg: str, ...attrs: str)` | `cfire::log::warn(msg, ...)` | Logs a warning message with optional key-value attribute pairs. | `log.warn("rate limit reached", "client", ip)` |
| `fn error(msg: str, ...attrs: str)` | `cfire::log::error(msg, ...)` | Logs an error message with optional key-value attribute pairs. | `log.error("database connection failed", "db", name)` |

### `core:unicode` (`unicode`) — UTF-8 Decoding, Rune Counting & Unicode Manipulation

- **Auto-Link Flags:** `(header-only)`
- **Overview:** RFC 3629 compliant branchless UTF-8 decoding, codepoint extraction, rune validation, and Unicode case transformation.

**Practical Example:**
```cf
package main

import "core:unicode"

fn main() {
    text := "cfire 🔥 世界"
    // Validate RFC 3629 UTF-8 byte sequences
    if unicode.is_valid_utf8(text) {
        rune_count := unicode.rune_count(text)
        println("Unicode scalar value count: {}", rune_count) // 10 runes
    }

    // Decode into individual 32-bit scalar codepoints
    runes := unicode.runes(text)
    for r in runes {
        println("Rune: U+{:04X}", r)
    }
}
```

| `.cf` Signature | Lowered C++23 | Description | Example |
|---|---|---|---|
| `fn rune_count(s: str) -> usize` | `cfire::unicode::rune_count(s)` | Counts the number of Unicode scalar values (codepoints) in a UTF-8 string. | `count := unicode.rune_count("Hello, 世界! 🚀")` |
| `fn runes(s: str) -> []u32` | `cfire::unicode::runes(s)` | Decodes all Unicode codepoints in `s` into a slice of 32-bit scalar values. | `cps := unicode.runes("cfire 🔥")` |
| `fn is_valid_utf8(s: str) -> bool` | `cfire::unicode::is_valid_utf8(s)` | Validates whether a string consists of strictly valid RFC 3629 UTF-8 byte sequences. | `ok := unicode.is_valid_utf8(payload)` |
| `fn encode_rune(r: u32) -> str` | `cfire::unicode::encode_rune(r)` | Encodes a 32-bit Unicode codepoint into its UTF-8 string representation. | `char := unicode.encode_rune(0x1F525)` |
| `fn to_lower(s: str) -> str` | `cfire::unicode::to_lower(s)` | Converts Latin, Greek, and Cyrillic Unicode characters to lowercase. | `lower := unicode.to_lower("HELLO, СВЕТ!")` |
| `fn to_upper(s: str) -> str` | `cfire::unicode::to_upper(s)` | Converts Latin, Greek, and Cyrillic Unicode characters to uppercase. | `upper := unicode.to_upper("hello, свет!")` |

## 2. Semantic Diagnostic Codes

### `[cfire/MUT-CALLSITE]` — Missing Call-Site `mut` Marker

Functions declaring `mut param: T` mutate the caller's variable in-place (`T&`). Callers must write `mut arg` at the call site so mutation is visible when reading code.

**Rejected (`.cf`):**
```cf
fn bump(mut x: i32) { x += 1 }
fn main() {
    mut a := 10
    bump(a) // ERROR [cfire/MUT-CALLSITE]
}
```

**Accepted (`.cf`):**
```cf
fn bump(mut x: i32) { x += 1 }
fn main() {
    mut a := 10
    bump(mut a) // OK
}
```

### `[cfire/IMMUTABLE-MUT-ARG]` — Passing Immutable Binding as `mut` Argument

Bindings declared with `:=` or `x: T = ...` without `mut` are `const` by default and cannot be passed to `mut` parameters.

**Rejected (`.cf`):**
```cf
fn reset(mut v: i32) { v = 0 }
fn main() {
    val := 5
    reset(mut val) // ERROR [cfire/IMMUTABLE-MUT-ARG]
}
```

**Accepted (`.cf`):**
```cf
fn reset(mut v: i32) { v = 0 }
fn main() {
    mut val := 5
    reset(mut val) // OK
}
```

### `[cfire/MUT-RECEIVER]` — Calling `mut` Method on Immutable Receiver

Methods with a mutable receiver `fn (mut self: Counter) inc()` cannot be invoked on an immutable binding.

**Rejected (`.cf`):**
```cf
c := Counter{Value: 0}
c.inc() // ERROR [cfire/MUT-RECEIVER]
```

**Accepted (`.cf`):**
```cf
mut c := Counter{Value: 0}
c.inc() // OK
```

### `[cfire/MUT-ALIAS]` — Mutable Aliasing Across Call-Site Arguments (Law of Exclusivity)

A variable passed as a `mut` or `out` argument (or used as a `mut` method receiver) cannot also be passed as another argument in the same call.

**Rejected (`.cf`):**
```cf
fn transfer(mut from: i32, mut to: i32) { ... }
fn main() {
    mut acc := 100
    transfer(mut acc, mut acc) // ERROR [cfire/MUT-ALIAS]
}
```

**Accepted (`.cf`):**
```cf
fn transfer(mut from: i32, mut to: i32) { ... }
fn main() {
    mut a := 100
    mut b := 50
    transfer(mut a, mut b) // OK: distinct bindings
}
```

### `[cfire/USE-AFTER-MOVE]` — Use of Value After Explicit `move`

Once a variable is passed with `move x` (which lowers to `std::move(x)`), reading or passing `x` again in the same scope is rejected at compile time.

**Rejected (`.cf`):**
```cf
consume(move buf)
println("{}", buf.len()) // ERROR [cfire/USE-AFTER-MOVE]
```

**Accepted (`.cf`):**
```cf
println("{}", buf.len())
consume(move buf) // OK: moved on last use
```

### `[cfire/NON-EXHAUSTIVE-MATCH]` — Non-Exhaustive `match` Statement Over Algebraic `enum`

Every `match` over a `type ... enum` must either cover all declared variants or include a wildcard `_ => { ... }` arm.

**Rejected (`.cf`):**
```cf
match shape {
    Circle(r) => { ... }
    // ERROR [cfire/NON-EXHAUSTIVE-MATCH]: missing Rect
}
```

**Accepted (`.cf`):**
```cf
match shape {
    Circle(r) => { ... }
    Rect(w, h) => { ... }
}
```

### `[cfire/NON-DIVERGING-OR-BLOCK]` — Non-Diverging Error Recovery Block on Value Binding

When initializing or assigning a variable with `val := fallible() or err { ... }`, the error recovery block must definitely diverge with `return`, `panic(...)`, or `exit(...)` to avoid unwrapping an uninitialized value.

**Rejected (`.cf`):**
```cf
fn compute() -> i32 {
    // ERROR [cfire/NON-DIVERGING-OR-BLOCK]: handler does not diverge
    x := parse_num() or err {
        println("error: {}", err.message())
    }
    return x
}
```

**Accepted (`.cf`):**
```cf
fn compute() -> i32 {
    x := parse_num() or err {
        println("error: {}", err.message())
        return 0
    }
    return x
}
```


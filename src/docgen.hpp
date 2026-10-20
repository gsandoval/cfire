#pragma once
// src/docgen.hpp — Single-Source-Of-Truth Metadata Catalog & Documentation Generator (`cfire doc`)
// Generates:
//   - docs/index.html            (Interactive "Sapphire on Fire" Landing Page)
//   - docs/guide.html & GUIDE.md (8-Chapter Language & Systems Programming Guide)
//   - docs/reference.html & STDLIB_REFERENCE.md (Complete `core:*` Stdlib & Diagnostics Reference)
// Also powers `cfire lsp` hover/autocomplete and `./cfire doc <file.cf>` package doc extraction.

#include "buildsys.hpp"
#include "formatter.hpp"
#include "bootstrap/docgen.generated.hpp"
#include <expected>
#include <filesystem>
#include <format>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace cfire::doc {

using StdFnDoc = ::StdFnDoc;
using StdModuleDoc = ::StdModuleDoc;
using DiagnosticDoc = ::DiagnosticDoc;
using DocStruct = ::DocStruct;
using DocEnum = ::DocEnum;
using DocFunction = ::DocFunction;
using DocTest = ::DocTest;

[[nodiscard]] inline auto stdlib_catalog() -> const std::vector<StdModuleDoc>& {
    static const std::vector<StdModuleDoc> catalog = {
        {
            "core:fs", "fs", "Filesystem & Zero-Copy Byte I/O", "(header-only)",
            "Provides RAII file and directory utilities backed by `std::filesystem` and `std::vector<std::byte>`.",
            {
                {"read_all", "fn read_all(path: str) -> []byte!", "cfire::fs::read_all(path)", "Reads an entire binary file into a byte buffer.", "raw := try fs.read_all(\"input.bin\")"},
                {"write_all", "fn write_all(path: str, data: []byte) -> void!", "cfire::fs::write_all(path, data)", "Writes a byte slice atomically to disk, creating parent directories if needed.", "try fs.write_all(\"out.bin\", raw)"},
                {"read_text", "fn read_text(path: str) -> str!", "cfire::fs::read_text(path)", "Reads an entire UTF-8 text file into a `str`.", "txt := try fs.read_text(\"config.json\")"},
                {"write_text", "fn write_text(path: str, text: str) -> void!", "cfire::fs::write_text(path, text)", "Writes a UTF-8 string to disk.", "try fs.write_text(\"hello.txt\", \"sapphire\")"},
                {"open", "fn open(path: str, mode: OpenMode = OpenMode::Read) -> File!", "cfire::fs::open(path, mode)", "Opens a file with specified mode (Read, Write, Append, ReadWrite) returning a streaming File handle.", "mut f := try fs.open(\"log.txt\", OpenMode::Append)"},
                {"File.read", "fn (mut f: File) read(mut buf: []byte) -> usize!", "f.read(buf)", "Reads up to `buf.len` bytes from the streaming file handle.", "n := try f.read(mut buf)"},
                {"File.read_all", "fn (mut f: File) read_all() -> []byte!", "f.read_all()", "Reads all remaining bytes from current file position to EOF.", "bytes := try f.read_all()"},
                {"File.write", "fn (mut f: File) write(data: []byte) -> usize!", "f.write(data)", "Writes byte slice to file stream.", "n := try f.write(buf)"},
                {"File.write_str", "fn (mut f: File) write_str(s: str) -> usize!", "f.write_str(s)", "Writes UTF-8 string to file stream.", "try f.write_str(\"header\\n\")"},
                {"File.seek", "fn (mut f: File) seek(offset: i64, whence: SeekWhence = SeekWhence::Start) -> i64!", "f.seek(offset, whence)", "Seeks within file to offset from Start, Current, or End.", "pos := try f.seek(0, SeekWhence::Start)"},
                {"File.tell", "fn (mut f: File) tell() -> i64!", "f.tell()", "Returns current byte offset within file stream.", "pos := try f.tell()"},
                {"File.flush", "fn (mut f: File) flush() -> void!", "f.flush()", "Flushes buffered stream data to disk.", "try f.flush()"},
                {"File.close", "fn (mut f: File) close() -> void", "f.close()", "Closes open file handle.", "f.close()"},
                {"dirname", "fn dirname(path: str) -> str", "cfire::fs::dirname(path)", "Returns directory portion of a filesystem path.", "dir := fs.dirname(\"/a/b/c.txt\")"},
                {"basename", "fn basename(path: str) -> str", "cfire::fs::basename(path)", "Returns file name portion of a filesystem path.", "name := fs.basename(\"/a/b/c.txt\")"},
                {"extension", "fn extension(path: str) -> str", "cfire::fs::extension(path)", "Returns file extension including leading dot.", "ext := fs.extension(\"main.cf\")"},
                {"exists", "fn exists(path: str) -> bool", "cfire::fs::exists(path)", "Returns `true` if the path exists on disk.", "if fs.exists(\"build.cf\") { ... }"},
                {"mkdir_all", "fn mkdir_all(path: str) -> void!", "cfire::fs::mkdir_all(path)", "Recursively creates a directory and all parent directories.", "try fs.mkdir_all(\".cfire/cache\")"},
                {"remove", "fn remove(path: str) -> void!", "cfire::fs::remove(path)", "Removes a file or directory tree recursively.", "try fs.remove(\"tmp_dir\")"},
                {"list_dir", "fn list_dir(path: str) -> []str!", "cfire::fs::list_dir(path)", "Lists sorted filenames inside a directory.", "entries := try fs.list_dir(\"examples\")"}
            }
        },
        {
            "core:cli", "cli", "Struct-Tag Command-Line Parser", "(header-only)",
            "Generates zero-boilerplate command-line flag parsers from struct field tags (`cli:\"--flag,help\"`).",
            {
                {"parse", "fn parse[T](args: []str?) -> T!", "cfire::cli::parse<T>(argc, argv)", "Parses CLI flags (`str`, `i64`, `i32`, `f64`, `bool`) into a tagged struct `T`.", "args := try cli.parse[Args]()"}
            }
        },
        {
            "core:compress/zstd", "zstd", "Native Zstandard Frame Compression", "-lzstd",
            "Hardware-accelerated Zstandard frame compression and decompression backed by native `libzstd`.",
            {
                {"compress", "fn compress(input: []byte, level: i32 = 3) -> []byte!", "cfire::zstd::compress(input, level)", "Compresses a byte slice into a Zstandard frame.", "blob := try zstd.compress(raw, level: 3)"},
                {"decompress", "fn decompress(frame: []byte) -> []byte!", "cfire::zstd::decompress(frame)", "Decompresses a Zstandard frame using the embedded frame content size.", "orig := try zstd.decompress(blob)"}
            }
        },
        {
            "core:json", "json", "Compile-Time SIMD JSON Reflection & Dynamic DOM", "-lsimdjson (or header-only)",
            "Uses `json:\"...\"` struct tags to emit specialized `simdjson::ondemand` parsers and fast `std::format` serializers with zero runtime reflection overhead, along with a zero-dependency dynamic DOM `json.Value`.",
            {
                {"parse", "fn parse[T](payload: []byte | str) -> T!", "cfire::json::parse<T>(payload)", "Parses UTF-8 JSON into a tagged struct `T` using `simdjson::ondemand`.", "cfg := try json.parse[Config](raw)"},
                {"stringify", "fn stringify[T](val: T) -> str", "cfire::json::stringify<T>(val)", "Serializes a tagged struct `T` (`str`, `u64`, `i64`, `i32`, `f64`, `bool`) into a JSON string.", "payload := json.stringify(metric)"},
                {"escape_string", "fn escape_string(s: str) -> str", "cfire::json::escape_string(s)", "Escapes quotes, backslashes, and control characters for JSON strings.", "esc := json.escape_string(raw_txt)"},
                {"parse_dynamic", "fn parse_dynamic(payload: str) -> Value!", "cfire::json::parse_dynamic(payload)", "Parses arbitrary JSON into a recursive dynamic DOM `Value`.", "v := try json.parse_dynamic(raw)"},
                {"stringify_dynamic", "fn stringify_dynamic(val: Value, indent: i32 = 0) -> str", "cfire::json::stringify_dynamic(val, indent)", "Serializes a dynamic DOM `Value` into compact or indented JSON.", "s := json.stringify_dynamic(v, 2)"}
            }
        },
        {
            "core:db/sqlite", "sqlite", "Embedded SQLite3 with WAL & Row Mapping", "-lsqlite3",
            "RAII SQLite3 wrapper enabling `PRAGMA journal_mode=WAL`, parameterized queries, atomic transactions, and compile-time `sql:\"...\"` struct row mapping.",
            {
                {"open", "fn open(path: str) -> Conn!", "cfire::sqlite::open(path)", "Opens an SQLite3 database connection and enables WAL mode.", "mut db := try sqlite.open(\"app.db\")"},
                {"Conn.exec", "fn (mut db: Conn) exec(sql: str, ...args) -> void!", "db.exec(sql, args...)", "Executes a parameterized SQL statement (`?1`, `?2`, ...).", "try db.exec(\"INSERT INTO pkg VALUES (?1, ?2)\", name, ver)"},
                {"Conn.query", "fn (mut db: Conn) query[T](sql: str, ...args) -> []T!", "db.query<T>(sql, args...)", "Executes a `SELECT` query and maps rows directly into struct `T` via `sql:\"...\"` tags.", "rows := try db.query[Row](\"SELECT name, ver FROM pkg\")"},
                {"Conn.tx", "fn (mut db: Conn) tx(block: fn(mut tx: Conn) -> void!) -> void!", "db.tx([&](Conn& tx) { ... })", "Runs a closure inside an atomic `BEGIN IMMEDIATE ... COMMIT` transaction with automatic rollback on error.", "try db.tx |mut tx| { ... }"}
            }
        },
        {
            "core:crypto/sha256", "sha256", "Standard FIPS 180-4 SHA-256 Digest", "(CommonCrypto / built-in)",
            "Computes NIST FIPS 180-4 SHA-256 digests over byte slices or strings.",
            {
                {"hash_hex", "fn hash_hex(data: []byte) -> str", "cfire::sha256::hash_hex(data)", "Returns a 64-character lowercase hex digest of `data`.", "digest := sha256.hash_hex(raw)"},
                {"hash_bytes", "fn hash_bytes(data: []byte) -> []byte", "cfire::sha256::hash_bytes(data)", "Returns 32-byte binary SHA-256 digest of `data`.", "b := sha256.hash_bytes(raw)"}
            }
        },
        {
            "core:crypto/hmac", "hmac", "RFC 2104 HMAC-SHA256 Message Authentication", "(CommonCrypto / built-in)",
            "Computes RFC 2104 keyed HMAC-SHA256 authentication digests.",
            {
                {"sha256_hex", "fn sha256_hex(key: str, data: str) -> str", "cfire::hmac::sha256_hex(key, data)", "Returns a 64-character lowercase hex HMAC-SHA256 digest.", "sig := hmac.sha256_hex(secret, payload)"},
                {"equal", "fn equal(a: str, b: str) -> bool", "cfire::hmac::equal(a, b)", "Performs a constant-time equality check on two MAC digests to prevent timing attacks.", "ok := hmac.equal(sig1, sig2)"}
            }
        },
        {
            "core:crypto/aes", "aes", "NIST FIPS 197 AES-256 Symmetric Encryption & GCM AEAD", "(Header-only pure C++23)",
            "Constant-time AES-256 block cipher implementation with Counter Mode (CTR) and Galois/Counter Mode (GCM) Authenticated Encryption with Associated Data (AEAD).",
            {
                {"encrypt_gcm", "fn encrypt_gcm(key: []byte, nonce: []byte, plaintext: []byte, aad: []byte = []) -> GcmResult!", "cfire::aes::encrypt_gcm(key, nonce, pt, aad)", "Encrypts plaintext with AES-256-GCM AEAD, producing ciphertext and a 16-byte authentication tag.", "res := try aes.encrypt_gcm(key, nonce, secret, aad)"},
                {"decrypt_gcm", "fn decrypt_gcm(key: []byte, nonce: []byte, ciphertext: []byte, tag: []byte, aad: []byte = []) -> []byte!", "cfire::aes::decrypt_gcm(key, nonce, ct, tag, aad)", "Authenticates and decrypts AES-256-GCM ciphertext, failing if tag verification fails or data was tampered.", "pt := try aes.decrypt_gcm(key, nonce, res.ciphertext, res.tag, aad)"},
                {"encrypt_ctr", "fn encrypt_ctr(key: []byte, iv: []byte, plaintext: []byte) -> []byte!", "cfire::aes::encrypt_ctr(key, iv, pt)", "Encrypts plaintext using AES-256-CTR mode.", "ct := try aes.encrypt_ctr(key, iv, pt)"},
                {"decrypt_ctr", "fn decrypt_ctr(key: []byte, iv: []byte, ciphertext: []byte) -> []byte!", "cfire::aes::decrypt_ctr(key, iv, ct)", "Decrypts ciphertext using AES-256-CTR mode.", "pt := try aes.decrypt_ctr(key, iv, ct)"}
            }
        },
        {
            "core:crypto/blake3", "blake3", "Cryptographic 256-Bit Digest (Alias)", "(CommonCrypto / built-in)",
            "Computes fast 256-bit cryptographic hex digests over byte slices (convenience alias for `core:crypto/sha256`).",
            {
                {"hash_hex", "fn hash_hex(data: []byte) -> str", "cfire::blake3::hash_hex(data)", "Returns a 64-character lowercase hex digest of `data`.", "digest := blake3.hash_hex(raw)"}
            }
        },
        {
            "core:net", "net", "POSIX BSD TCP Sockets & Loopback Networking", "(POSIX sockets)",
            "RAII TCP client and listener sockets backed by BSD `AF_INET` sockets with non-blocking `poll` timeouts and automatic `AF_UNIX` `socketpair` loopback fallback inside sandboxes.",
            {
                {"listen_tcp", "fn listen_tcp(port: u16 = 0) -> TcpListener!", "cfire::net::listen_tcp(port)", "Binds a loopback TCP listener on `127.0.0.1:port` (`0` selects an ephemeral port).", "mut ln := try net.listen_tcp(0)"},
                {"TcpListener.port", "fn (ln: TcpListener) port() -> u16", "ln.port()", "Returns the bound TCP port.", "p := ln.port()"},
                {"TcpListener.accept", "fn (mut ln: TcpListener) accept() -> TcpConn!", "ln.accept()", "Accepts an incoming TCP connection with a safe poll timeout.", "mut conn := try ln.accept()"},
                {"connect_tcp", "fn connect_tcp(host: str, port: u16) -> TcpConn!", "cfire::net::connect_tcp(host, port)", "Connects a `TcpConn` to `host:port`.", "mut c := try net.connect_tcp(\"127.0.0.1\", p)"},
                {"TcpConn.write", "fn (mut c: TcpConn) write(payload: str) -> usize!", "c.write(payload)", "Writes bytes to an open TCP connection.", "_ := try c.write(\"PING\")"},
                {"TcpConn.read_str", "fn (mut c: TcpConn) read_str(max_bytes: usize = 1024) -> str!", "c.read_str(max_bytes)", "Reads up to `max_bytes` from the socket as a string.", "msg := try conn.read_str(64)"}
            }
        },
        {
            "core:net/url", "url", "RFC 3986 URL Parsing & URL Encoding", "(header-only)",
            "RFC 3986 URL parsing with component extraction (scheme, host, port, path, query, fragment), form-urlencoded query parameter maps, and percent encoding/decoding.",
            {
                {"parse", "fn parse(raw: str) -> Url!", "cfire::url::parse(raw)", "Parses an RFC 3986 URL string into component fields and query parameters.", "u := try url.parse(\"http://127.0.0.1:8080/api/items?q=shoes#top\")"},
                {"encode", "fn encode(s: str) -> str", "cfire::url::encode(s)", "Percent-encodes a string for safe use in URL path or query strings.", "enc := url.encode(\"hello world!\")"},
                {"decode", "fn decode(s: str) -> str", "cfire::url::decode(s)", "Decodes a percent-encoded URL string.", "dec := url.decode(enc)"}
            }
        },
        {
            "core:net/http", "http", "HTTP/1.1 Client, Wire Protocol Engine, Server & Independent Router", "(POSIX sockets)",
            "RFC 9112 HTTP/1.1 client (`get`, `post`, `request`), wire parser (`parse_request`), response serializer (`Response`), multi-mode server (`make_server`), and independent request router (`make_router`).",
            {
                {"get", "fn get(url: str, headers: map[str]str = {}) -> Response!", "cfire::http::get(url, headers)", "Executes an HTTP GET request against a target URL.", "resp := try http.get(\"http://127.0.0.1:8080/health\")"},
                {"post", "fn post(url: str, body: str, content_type: str = \"application/json\", headers: map[str]str = {}) -> Response!", "cfire::http::post(url, body, content_type, headers)", "Executes an HTTP POST request with payload body.", "resp := try http.post(\"http://127.0.0.1:8080/items\", \"{\\\"id\\\": 1}\")"},
                {"request", "fn request(method: str, url: str, body: str = \"\", content_type: str = \"\", headers: map[str]str = {}) -> Response!", "cfire::http::request(method, url, body, content_type, headers)", "Executes an arbitrary HTTP request (PUT, DELETE, PATCH, etc.).", "resp := try http.request(\"DELETE\", \"http://127.0.0.1:8080/items/1\")"},
                {"make_server", "fn make_server(port: u16 = 0) -> HttpServer!", "cfire::http::make_server(port)", "Creates a lightweight HTTP server listening on `port` (0 for ephemeral).", "mut srv := try http.make_server(8080)"},
                {"make_router", "fn make_router() -> Router", "cfire::http::make_router()", "Creates an independent HTTP request router supporting path parameters (`:id`), wildcards (`*path`), and standalone dispatch.", "mut r := http.make_router()"},
                {"parse_request", "fn parse_request(raw: str) -> Request!", "cfire::http::parse_request(raw)", "Parses raw HTTP/1.1 wire text into a structured Request model.", "req := try http.parse_request(raw_text)"},
                {"Response.text", "fn Response.text(code: i32, payload: str) -> Response", "cfire::http::Response::text(code, payload)", "Creates an HTTP Response with text/plain content-type and standard status reason.", "return http.Response.text(200, \"OK\")"},
                {"Response.json", "fn Response.json(code: i32, json_str: str) -> Response", "cfire::http::Response::json(code, json_str)", "Creates an HTTP Response with application/json content-type.", "return http.Response.json(200, \"{\\\"status\\\":\\\"ok\\\"}\")"},
                {"Response.html", "fn Response.html(code: i32, html: str) -> Response", "cfire::http::Response::html(code, html)", "Creates an HTTP Response with text/html content-type.", "return http.Response.html(200, \"<h1>Hello</h1>\")"},
                {"Response.redirect", "fn Response.redirect(location: str, code: i32 = 302) -> Response", "cfire::http::Response::redirect(location, code)", "Creates an HTTP redirect Response setting Location header.", "return http.Response.redirect(\"/login\", 302)"},
                {"Router.get", "fn (mut r: Router) get(pattern: str, handler: fn(Request) -> Response) -> Router&", "r.get(pattern, handler)", "Registers an HTTP GET route with optional path parameters (`:id`) or wildcards (`*path`).", "r.get(\"/users/:id\", |req| http.Response.text(200, req.param(\"id\")))"},
                {"Router.post", "fn (mut r: Router) post(pattern: str, handler: fn(Request) -> Response) -> Router&", "r.post(pattern, handler)", "Registers an HTTP POST route handler.", "r.post(\"/items\", |req| http.Response.json(201, \"{}\"))"},
                {"Router.dispatch", "fn (r: Router) dispatch(req: Request) -> Response", "r.dispatch(req)", "Dispatches an HTTP request through the router in-memory without open network sockets.", "res := router.dispatch(test_req)"},
                {"Router.use", "fn (mut r: Router) use(mw: fn(mut Request) -> Response?) -> Router&", "r.use(mw)", "Registers pre-routing middleware that can short-circuit or validate requests.", "r.use(|req| { return None })"},
                {"HttpServer.serve_in_background", "fn (mut s: HttpServer) serve_in_background(handler: fn(Request) -> Response) -> void", "s.serve_in_background(handler)", "Starts a non-blocking background accept loop serving requests with the specified handler or router.", "server.serve_in_background(router)"},
                {"HttpServer.stop", "fn (mut s: HttpServer) stop() -> void", "s.stop()", "Gracefully closes the listening socket and stops worker threads.", "server.stop()"},
                {"Request.param", "fn (req: Request) param(key: str, fallback: str = \"\") -> str", "req.param(key, fallback)", "Retrieves a named route path parameter extracted by the router.", "id := req.param(\"id\")"},
                {"Request.query", "fn (req: Request) query(key: str, fallback: str = \"\") -> str", "req.query(key, fallback)", "Retrieves a decoded URL query parameter.", "page := req.query(\"page\", \"1\")"},
                {"Request.header", "fn (req: Request) header(key: str, fallback: str = \"\") -> str", "req.header(key, fallback)", "Performs a case-insensitive header lookup.", "auth := req.header(\"Authorization\")"}
            }
        },
        {
            "core:net/websocket", "websocket", "RFC 6455 High-Level WebSockets Client & Server", "(POSIX sockets)",
            "RFC 6455 full-duplex WebSocket client (`connect`) and server upgrade handler (`upgrade`) with automated framing, client masking, and Ping/Pong management.",
            {
                {"connect", "fn connect(url: str) -> Conn!", "cfire::websocket::connect(url)", "Establishes a WebSocket connection to `ws://<host>[:port]/<path>` and completes the RFC 6455 handshake.", "mut ws := try websocket.connect(\"ws://127.0.0.1:8080/ws\")"},
                {"upgrade", "fn upgrade(req: Request, mut conn: TcpConn) -> Conn!", "cfire::websocket::upgrade(req, conn)", "Upgrades an incoming HTTP connection to a WebSocket connection.", "mut ws := try websocket.upgrade(req, mut conn)"},
                {"pair", "fn pair() -> (Conn, Conn)!", "cfire::websocket::pair()", "Creates an in-process full-duplex WebSocket client/server loopback pair.", "mut (client, server) := try websocket.pair()"},
                {"Conn.send_text", "fn (mut ws: Conn) send_text(msg: str) -> void!", "ws.send_text(msg)", "Sends a UTF-8 text frame to the peer (masked if client).", "try ws.send_text(\"hello world\")"},
                {"Conn.send_binary", "fn (mut ws: Conn) send_binary(data: []byte) -> void!", "ws.send_binary(data)", "Sends a binary data frame to the peer (masked if client).", "try ws.send_binary(payload)"},
                {"Conn.recv_text", "fn (mut ws: Conn) recv_text() -> str!", "ws.recv_text()", "Receives the next text frame from the peer, automatically responding to Ping frames with Pong.", "msg := try ws.recv_text()"},
                {"Conn.recv_binary", "fn (mut ws: Conn) recv_binary() -> []byte!", "ws.recv_binary()", "Receives the next binary frame from the peer.", "bytes := try ws.recv_binary()"},
                {"Conn.ping", "fn (mut ws: Conn) ping(payload: str = \"\") -> void!", "ws.ping(payload)", "Sends a Ping control frame to check peer liveness.", "try ws.ping(\"heartbeat\")"},
                {"Conn.close", "fn (mut ws: Conn) close(code: u16 = 1000, reason: str = \"\")", "ws.close(code, reason)", "Initiates a clean RFC 6455 closure handshake and closes the socket.", "ws.close()"},
                {"Conn.is_open", "fn (ws: Conn) is_open() -> bool", "ws.is_open()", "Returns true if the WebSocket connection is open.", "if ws.is_open() { ... }"}
            }
        },
        {
            "core:time", "time", "Time, Calendar Dates, Durations & RFC 3339 Formatting", "(header-only)",
            "High-resolution monotonic timing, duration arithmetic, and RFC 3339 / ISO 8601 formatting backed by `std::chrono`.",
            {
                {"now_ms", "fn now_ms() -> i64", "cfire::time::now_ms()", "Returns monotonic milliseconds from `std::chrono::steady_clock`.", "t0 := time.now_ms()"},
                {"unix_seconds", "fn unix_seconds() -> i64", "cfire::time::unix_seconds()", "Returns current UTC Unix timestamp in seconds.", "ts := time.unix_seconds()"},
                {"sleep_ms", "fn sleep_ms(ms: i64)", "cfire::time::sleep_ms(ms)", "Sleeps the current thread for `ms` milliseconds.", "time.sleep_ms(10)"},
                {"sleep_ctx", "fn sleep_ctx(ctx: Context, ms: i64) -> bool", "cfire::time::sleep_ctx(ctx, ms)", "Sleeps the current thread for `ms` or until `ctx` is cancelled; returns true if completed without cancellation.", "ok := time.sleep_ctx(ctx, 50)"},
                {"now", "fn now() -> Time", "cfire::time::now()", "Returns current wall clock `Time` instant.", "t := time.now()"},
                {"unix", "fn unix(sec: i64, nsec: i64 = 0) -> Time", "cfire::time::unix(sec, nsec)", "Constructs a `Time` instant from Unix seconds and nanoseconds.", "t := time.unix(1700000000)"},
                {"parse_rfc3339", "fn parse_rfc3339(s: str) -> Time!", "cfire::time::parse_rfc3339(s)", "Parses an RFC 3339 / ISO 8601 timestamp string into a `Time` instant.", "t := try time.parse_rfc3339(\"2026-10-18T12:00:00Z\")"},
                {"duration_seconds", "fn duration_seconds(s: i64) -> Duration", "cfire::time::duration_seconds(s)", "Constructs a typed `Duration` representing `s` seconds.", "d := time.duration_seconds(5)"},
                {"duration_millis", "fn duration_millis(ms: i64) -> Duration", "cfire::time::duration_millis(ms)", "Constructs a typed `Duration` representing `ms` milliseconds.", "d := time.duration_millis(250)"},
                {"sleep", "fn sleep(d: Duration)", "cfire::time::sleep(d)", "Sleeps the current thread for duration `d`.", "time.sleep(time.duration_millis(50))"}
            }
        },
        {
            "core:os", "os", "Environment, Working Directory & Subprocesses", "(header-only)",
            "Process environment inspection, working directory queries, and subprocess output capture.",
            {
                {"getenv", "fn getenv(key: str, fallback: str = \"\") -> str", "cfire::os::getenv(key, fallback)", "Reads environment variable `key`, returning `fallback` if unset.", "mode := os.getenv(\"ENV\", \"dev\")"},
                {"setenv", "fn setenv(key: str, value: str)", "cfire::os::setenv(key, value)", "Sets environment variable `key` to `value` in the current process.", "os.setenv(\"MODE\", \"prod\")"},
                {"cwd", "fn cwd() -> str", "cfire::os::cwd()", "Returns the current working directory path.", "dir := os.cwd()"},
                {"exec_output", "fn exec_output(cmd: str) -> str!", "cfire::os::exec_output(cmd)", "Executes a shell command via `popen` and returns its captured stdout.", "out := try os.exec_output(\"uname -s\")"},
                {"command", "fn command(binary: str, args: []str) -> ExecResult!", "cfire::os::command(binary, args)", "Spawns a subprocess with explicit argument list via `posix_spawnp` (zero shell injection risk) and captures stdout/stderr.", "res := try os.command(\"git\", [\"status\"])"}
            }
        },
        {
            "core:sync", "sync", "Thread-Safe Typed Channels", "(header-only)",
            "Go-style typed MPSC/MPMC channels (`Channel[T]`) synchronized with `std::mutex` and `std::condition_variable`.",
            {
                {"make_channel", "fn make_channel[T]() -> Channel[T]", "cfire::sync::make_channel<T>()", "Creates a new shared, thread-safe channel of element type `T`.", "ch := sync.make_channel[i64]()"},
                {"Channel.send", "fn (ch: Channel[T]) send(val: T)", "ch.send(val)", "Sends a value into the channel and wakes one waiting receiver.", "ch.send(42)"},
                {"Channel.recv", "fn (ch: Channel[T]) recv() -> T!", "ch.recv()", "Blocks until a value is available or returns an error if the channel is closed and empty.", "v := ch.recv() or 0"},
                {"Channel.recv_ctx", "fn (ch: Channel[T]) recv_ctx(ctx: Context) -> T!", "ch.recv_ctx(ctx)", "Receives from the channel or aborts immediately with an error if `ctx` is cancelled.", "v := try ch.recv_ctx(ctx)"},
                {"Channel.close", "fn (ch: Channel[T]) close()", "ch.close()", "Closes the channel and wakes all waiting receivers.", "ch.close()"}
            }
        },
        {
            "core:sync/context", "context", "Cancellation Tokens & Deadline Contexts", "(header-only)",
            "Structured concurrency cancellation token and deadline propagation hierarchy (`core:sync/context`).",
            {
                {"background", "fn background() -> Context", "cfire::context::background()", "Returns an empty, root non-cancellable Context.", "ctx := context.background()"},
                {"with_cancel", "fn with_cancel(parent: Context) -> (Context, fn(reason: str = \"\"))", "cfire::context::with_cancel(parent)", "Derives a child Context and a cancellation handle function from a parent Context.", "(ctx, cancel) := context.with_cancel(parent)"},
                {"with_timeout", "fn with_timeout(parent: Context, ms: i64) -> (Context, fn(reason: str = \"\"))", "cfire::context::with_timeout(parent, ms)", "Derives a child Context with an automatic millisecond deadline.", "(ctx, cancel) := context.with_timeout(parent, 250)"},
                {"Context.is_cancelled", "fn (ctx: Context) is_cancelled() -> bool", "ctx.is_cancelled()", "Returns true if the Context has been cancelled or timed out.", "if ctx.is_cancelled() { return }"},
                {"Context.done", "fn (ctx: Context) done() -> bool", "ctx.done()", "Synonym for `is_cancelled()`.", "if ctx.done() { return }"},
                {"Context.reason", "fn (ctx: Context) reason() -> str", "ctx.reason()", "Returns the cancellation reason string.", "msg := ctx.reason()"},
                {"Context.wait", "fn (ctx: Context) wait()", "ctx.wait()", "Blocks until the Context is cancelled or its deadline expires.", "ctx.wait()"},
                {"Context.wait_for", "fn (ctx: Context) wait_for(ms: i64) -> bool", "ctx.wait_for(ms)", "Blocks up to `ms` milliseconds or until the Context is cancelled; returns true if cancelled.", "cancelled := ctx.wait_for(100)"}
            }
        },
        {
            "core:str", "str", "UTF-8 String Manipulation", "(header-only)",
            "Zero-surprise string splitting, joining, trimming, searching, replacement, and ASCII case conversion.",
            {
                {"split", "fn split(s: str, sep: str) -> []str", "cfire::str::split(s, sep)", "Splits `s` around every occurrence of `sep`.", "parts := str.split(\"a,b,c\", \",\")"},
                {"join", "fn join(items: []str, sep: str) -> str", "cfire::str::join(items, sep)", "Joins a slice of strings with `sep`.", "s := str.join(parts, \"-\")"},
                {"trim", "fn trim(s: str) -> str", "cfire::str::trim(s)", "Strips leading and trailing whitespace.", "clean := str.trim(\"  hi  \")"},
                {"starts_with", "fn starts_with(s: str, prefix: str) -> bool", "cfire::str::starts_with(s, prefix)", "Returns `true` if `s` starts with `prefix`.", "if str.starts_with(s, \"cf\") { ... }"},
                {"ends_with", "fn ends_with(s: str, suffix: str) -> bool", "cfire::str::ends_with(s, suffix)", "Returns `true` if `s` ends with `suffix`.", "if str.ends_with(f, \".cf\") { ... }"},
                {"contains", "fn contains(s: str, needle: str) -> bool", "cfire::str::contains(s, needle)", "Returns `true` if `s` contains `needle`.", "if str.contains(s, \"fire\") { ... }"},
                {"replace", "fn replace(s: str, from: str, to: str) -> str", "cfire::str::replace(s, from, to)", "Replaces all occurrences of `from` in `s` with `to`.", "r := str.replace(s, \"old\", \"new\")"},
                {"to_upper", "fn to_upper(s: str) -> str", "cfire::str::to_upper(s)", "Converts ASCII characters to uppercase.", "u := str.to_upper(\"cfire\")"},
                {"to_lower", "fn to_lower(s: str) -> str", "cfire::str::to_lower(s)", "Converts ASCII characters to lowercase.", "l := str.to_lower(\"CFIRE\")"}
            }
        },
        {
            "core:math", "math", "Numeric & Floating-Point Utilities", "(header-only)",
            "Standard mathematical constants (`PI`, `E`) and numeric clamping/transcendental functions.",
            {
                {"clamp_i64", "fn clamp_i64(val: i64, lo: i64, hi: i64) -> i64", "cfire::math::clamp_i64(val, lo, hi)", "Clamps a 64-bit integer to `[lo, hi]`.", "c := math.clamp_i64(x, 0, 100)"},
                {"clamp_f64", "fn clamp_f64(val: f64, lo: f64, hi: f64) -> f64", "cfire::math::clamp_f64(val, lo, hi)", "Clamps a 64-bit float to `[lo, hi]`.", "c := math.clamp_f64(v, 0.0, 1.0)"},
                {"sqrt", "fn sqrt(x: f64) -> f64", "cfire::math::sqrt(x)", "Computes square root.", "r := math.sqrt(16.0)"},
                {"abs_f64", "fn abs_f64(x: f64) -> f64", "cfire::math::abs_f64(x)", "Computes absolute value of a `f64`.", "a := math.abs_f64(-3.5)"}
            }
        },
        {
            "core:rand", "rand", "Thread-Local MT19937_64 & CSPRNG Generator", "(header-only)",
            "Fast, thread-safe pseudo-random and OS-entropy CSPRNG random generation.",
            {
                {"u64", "fn u64() -> u64", "cfire::rand::u64()", "Generates a random 64-bit unsigned integer.", "id := rand.u64()"},
                {"range_i64", "fn range_i64(min_incl: i64, max_incl: i64) -> i64", "cfire::rand::range_i64(min_incl, max_incl)", "Generates a uniform integer in `[min_incl, max_incl]`.", "n := rand.range_i64(1, 10)"},
                {"f64_unit", "fn f64_unit() -> f64", "cfire::rand::f64_unit()", "Generates a uniform `f64` in `[0.0, 1.0)`.", "p := rand.f64_unit()"},
                {"bytes", "fn bytes(n: usize) -> []byte", "cfire::rand::bytes(n)", "Generates `n` cryptographically secure random bytes from OS entropy.", "buf := rand.bytes(32)"}
            }
        },
        {
            "core:encoding/base64", "base64", "RFC 4648 Base64 & URL-Safe Encoding/Decoding", "(header-only)",
            "High-performance, zero-allocation Base64 and URL-safe Base64 encoder and decoder.",
            {
                {"encode", "fn encode(data: []byte, url_safe: bool = false) -> str", "cfire::base64::encode(data, url_safe)", "Encodes binary byte slice into standard or URL-safe Base64 string.", "s := base64.encode(raw)"},
                {"encode_str", "fn encode_str(s: str, url_safe: bool = false) -> str", "cfire::base64::encode_str(s, url_safe)", "Encodes a UTF-8 text string into standard or URL-safe Base64.", "token := base64.encode_str(\"user:pass\")"},
                {"decode", "fn decode(s: str) -> []byte!", "cfire::base64::decode(s)", "Decodes a Base64 string into binary bytes.", "bytes := try base64.decode(token)"},
                {"decode_str", "fn decode_str(s: str) -> str!", "cfire::base64::decode_str(s)", "Decodes a Base64 string into a UTF-8 text string.", "plain := try base64.decode_str(token)"}
            }
        },
        {
            "core:encoding/hex", "hex", "Hexadecimal Encoding & Decoding", "(header-only)",
            "Encodes binary bytes to lowercase hexadecimal strings and decodes hex strings to byte buffers.",
            {
                {"encode", "fn encode(data: []byte) -> str", "cfire::hex::encode(data)", "Encodes binary bytes to lowercase hexadecimal string.", "h := hex.encode(digest)"},
                {"encode_str", "fn encode_str(s: str) -> str", "cfire::hex::encode_str(s)", "Encodes text characters to hexadecimal string.", "h := hex.encode_str(\"abc\")"},
                {"decode", "fn decode(s: str) -> []byte!", "cfire::hex::decode(s)", "Decodes a hexadecimal string to binary bytes.", "buf := try hex.decode(h)"},
                {"decode_str", "fn decode_str(s: str) -> str!", "cfire::hex::decode_str(s)", "Decodes a hexadecimal string to a UTF-8 string.", "txt := try hex.decode_str(h)"}
            }
        },
        {
            "core:encoding/csv", "csv", "RFC 4180 CSV Parsing & Stringification", "(header-only)",
            "RFC 4180 compliant CSV parser and serializer supporting multi-line quoted fields and comma escaping.",
            {
                {"parse", "fn parse(s: str) -> [][]str", "cfire::csv::parse(s)", "Parses a CSV string into a 2D table of rows and columns.", "rows := csv.parse(content)"},
                {"stringify", "fn stringify(rows: [][]str) -> str", "cfire::csv::stringify(rows)", "Serializes a 2D table of strings into an RFC 4180 formatted CSV string.", "out := csv.stringify(rows)"},
                {"read_all", "fn read_all(path: str) -> [][]str!", "cfire::csv::read_all(path)", "Reads an entire CSV file from disk and parses it into rows.", "table := try csv.read_all(\"data.csv\")"},
                {"write_all", "fn write_all(path: str, rows: [][]str) -> void!", "cfire::csv::write_all(path, rows)", "Serializes rows to CSV and writes to disk.", "try csv.write_all(\"report.csv\", table)"}
            }
        },
        {
            "core:regex", "regex", "ECMAScript Regular Expressions", "(header-only)",
            "Pattern matching, group extraction, and text substitution backed by C++23 `<regex>`.",
            {
                {"compile", "fn compile(pattern: str) -> Regex!", "cfire::regex::compile(pattern)", "Compiles an ECMAScript regex pattern into a reusable Regex object.", "re := try regex.compile(\"\\\\d+\")"},
                {"is_match", "fn is_match(pattern: str, text: str) -> bool!", "cfire::regex::is_match(pattern, text)", "Tests whether a regex pattern matches text.", "ok := try regex.is_match(\"^[a-z]+$\", s)"},
                {"replace_all", "fn replace_all(pattern: str, text: str, repl: str) -> str!", "cfire::regex::replace_all(pattern, text, repl)", "Replaces all occurrences matching pattern with repl.", "res := try regex.replace_all(\"\\s+\", s, \" \")"}
            }
        },
        {
            "core:collections", "collections", "Generic Containers (Set, PriorityQueue, Deque)", "(header-only)",
            "Standard generic containers backed by STL unordered_set, priority_queue, and deque.",
            {
                {"Set", "type Set[T] struct", "cfire::collections::Set<T>", "Generic hash set container.", "mut s := collections.Set[str]{}"},
                {"PriorityQueue", "type PriorityQueue[T] struct", "cfire::collections::PriorityQueue<T>", "Generic max-heap priority queue.", "mut pq := collections.PriorityQueue[i32]{}"},
                {"Deque", "type Deque[T] struct", "cfire::collections::Deque<T>", "Generic double-ended queue.", "mut dq := collections.Deque[i32]{}"}
            }
        },
        {
            "core:sort", "sort", "Slice Sorting & Binary Search", "(header-only)",
            "In-place sorting algorithms and binary search on slices.",
            {
                {"slice", "fn slice(mut s: []T)", "cfire::sort::slice(s)", "Sorts a slice in ascending order in-place.", "sort.slice(mut numbers)"},
                {"binary_search", "fn binary_search(s: []T, val: T) -> i64?", "cfire::sort::binary_search(s, val)", "Performs binary search on a sorted slice.", "idx := sort.binary_search(numbers, 42)"}
            }
        },
        {
            "core:log", "log", "Structured Leveled Logging", "(header-only)",
            "Leveled logging (`debug`, `info`, `warn`, `error`) with structured key-value attributes and Text / JSON formatters.",
            {
                {"set_level", "fn set_level(l: Level)", "cfire::log::set_level(l)", "Sets the minimum logging severity threshold.", "log.set_level(log.Level::Debug)"},
                {"set_format", "fn set_format(f: Format)", "cfire::log::set_format(f)", "Sets the log output format (Text or Json).", "log.set_format(log.Format::Json)"},
                {"debug", "fn debug(msg: str, ...attrs: str)", "cfire::log::debug(msg, ...)", "Logs a debug message with optional key-value attribute pairs.", "log.debug(\"cache miss\", \"key\", \"user:1\")"},
                {"info", "fn info(msg: str, ...attrs: str)", "cfire::log::info(msg, ...)", "Logs an informational message with optional key-value attribute pairs.", "log.info(\"server listening\", \"port\", \"8080\")"},
                {"warn", "fn warn(msg: str, ...attrs: str)", "cfire::log::warn(msg, ...)", "Logs a warning message with optional key-value attribute pairs.", "log.warn(\"rate limit reached\", \"client\", ip)"},
                {"error", "fn error(msg: str, ...attrs: str)", "cfire::log::error(msg, ...)", "Logs an error message with optional key-value attribute pairs.", "log.error(\"database connection failed\", \"db\", name)"}
            }
        },
        {
            "core:unicode", "unicode", "UTF-8 Decoding, Rune Counting & Unicode Manipulation", "(header-only)",
            "RFC 3629 compliant branchless UTF-8 decoding, codepoint extraction, rune validation, and Unicode case transformation.",
            {
                {"rune_count", "fn rune_count(s: str) -> usize", "cfire::unicode::rune_count(s)", "Counts the number of Unicode scalar values (codepoints) in a UTF-8 string.", "count := unicode.rune_count(\"Hello, 世界! 🚀\")"},
                {"runes", "fn runes(s: str) -> []u32", "cfire::unicode::runes(s)", "Decodes all Unicode codepoints in `s` into a slice of 32-bit scalar values.", "cps := unicode.runes(\"cfire 🔥\")"},
                {"is_valid_utf8", "fn is_valid_utf8(s: str) -> bool", "cfire::unicode::is_valid_utf8(s)", "Validates whether a string consists of strictly valid RFC 3629 UTF-8 byte sequences.", "ok := unicode.is_valid_utf8(payload)"},
                {"encode_rune", "fn encode_rune(r: u32) -> str", "cfire::unicode::encode_rune(r)", "Encodes a 32-bit Unicode codepoint into its UTF-8 string representation.", "char := unicode.encode_rune(0x1F525)"},
                {"to_lower", "fn to_lower(s: str) -> str", "cfire::unicode::to_lower(s)", "Converts Latin, Greek, and Cyrillic Unicode characters to lowercase.", "lower := unicode.to_lower(\"HELLO, СВЕТ!\")"},
                {"to_upper", "fn to_upper(s: str) -> str", "cfire::unicode::to_upper(s)", "Converts Latin, Greek, and Cyrillic Unicode characters to uppercase.", "upper := unicode.to_upper(\"hello, свет!\")"}
            }
        }
    };
    return catalog;
}

[[nodiscard]] inline auto diagnostics_catalog() -> const std::vector<DiagnosticDoc>& {
    static const std::vector<DiagnosticDoc> diags = {
        {
            "cfire/MUT-CALLSITE",
            "Missing Call-Site `mut` Marker",
            "Functions declaring `mut param: T` mutate the caller's variable in-place (`T&`). Callers must write `mut arg` at the call site so mutation is visible when reading code.",
            "fn bump(mut x: i32) { x += 1 }\nfn main() {\n    mut a := 10\n    bump(a) // ERROR [cfire/MUT-CALLSITE]\n}",
            "fn bump(mut x: i32) { x += 1 }\nfn main() {\n    mut a := 10\n    bump(mut a) // OK\n}"
        },
        {
            "cfire/IMMUTABLE-MUT-ARG",
            "Passing Immutable Binding as `mut` Argument",
            "Bindings declared with `:=` or `x: T = ...` without `mut` are `const` by default and cannot be passed to `mut` parameters.",
            "fn reset(mut v: i32) { v = 0 }\nfn main() {\n    val := 5\n    reset(mut val) // ERROR [cfire/IMMUTABLE-MUT-ARG]\n}",
            "fn reset(mut v: i32) { v = 0 }\nfn main() {\n    mut val := 5\n    reset(mut val) // OK\n}"
        },
        {
            "cfire/MUT-RECEIVER",
            "Calling `mut` Method on Immutable Receiver",
            "Methods with a mutable receiver `fn (mut self: Counter) inc()` cannot be invoked on an immutable binding.",
            "c := Counter{Value: 0}\nc.inc() // ERROR [cfire/MUT-RECEIVER]",
            "mut c := Counter{Value: 0}\nc.inc() // OK"
        },
        {
            "cfire/MUT-ALIAS",
            "Mutable Aliasing Across Call-Site Arguments (Law of Exclusivity)",
            "A variable passed as a `mut` or `out` argument (or used as a `mut` method receiver) cannot also be passed as another argument in the same call.",
            "fn transfer(mut from: i32, mut to: i32) { ... }\nfn main() {\n    mut acc := 100\n    transfer(mut acc, mut acc) // ERROR [cfire/MUT-ALIAS]\n}",
            "fn transfer(mut from: i32, mut to: i32) { ... }\nfn main() {\n    mut a := 100\n    mut b := 50\n    transfer(mut a, mut b) // OK: distinct bindings\n}"
        },
        {
            "cfire/USE-AFTER-MOVE",
            "Use of Value After Explicit `move`",
            "Once a variable is passed with `move x` (which lowers to `std::move(x)`), reading or passing `x` again in the same scope is rejected at compile time.",
            "consume(move buf)\nprintln(\"{}\", buf.len()) // ERROR [cfire/USE-AFTER-MOVE]",
            "println(\"{}\", buf.len())\nconsume(move buf) // OK: moved on last use"
        },
        {
            "cfire/NON-EXHAUSTIVE-MATCH",
            "Non-Exhaustive `match` Statement Over Algebraic `enum`",
            "Every `match` over a `type ... enum` must either cover all declared variants or include a wildcard `_ => { ... }` arm.",
            "match shape {\n    Circle(r) => { ... }\n    // ERROR [cfire/NON-EXHAUSTIVE-MATCH]: missing Rect\n}",
            "match shape {\n    Circle(r) => { ... }\n    Rect(w, h) => { ... }\n}"
        },
        {
            "cfire/NON-DIVERGING-OR-BLOCK",
            "Non-Diverging Error Recovery Block on Value Binding",
            "When initializing or assigning a variable with `val := fallible() or err { ... }`, the error recovery block must definitely diverge with `return`, `panic(...)`, or `exit(...)` to avoid unwrapping an uninitialized value.",
            "fn compute() -> i32 {\n    // ERROR [cfire/NON-DIVERGING-OR-BLOCK]: handler does not diverge\n    x := parse_num() or err {\n        println(\"error: {}\", err.message())\n    }\n    return x\n}",
            "fn compute() -> i32 {\n    x := parse_num() or err {\n        println(\"error: {}\", err.message())\n        return 0\n    }\n    return x\n}"
        }
    };
    return diags;
}

namespace detail {

[[nodiscard]] inline auto html_escape_cxx(std::string_view sv) -> std::string {
    std::string out;
    out.reserve(sv.size() + 32);
    for (char c : sv) {
        switch (c) {
            case '&': out += "&amp;"; break;
            case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break;
            case '"': out += "&quot;"; break;
            default: out.push_back(c); break;
        }
    }
    return out;
}

[[nodiscard]] inline auto html_escape(std::string_view sv) -> std::string {
    const char* force_cxx = std::getenv("CFIRE_CXX_DOCGEN");
    if (force_cxx && std::string_view(force_cxx) == "1") {
        return html_escape_cxx(sv);
    }
    return ::html_escape(sv);
}

[[nodiscard]] inline auto module_practical_example(std::string_view mod) -> std::string_view {
    if (mod == "core:fs") {
        return R"CF(package main

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
})CF";
    }
    if (mod == "core:cli") {
        return R"CF(package main

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
})CF";
    }
    if (mod == "core:compress/zstd") {
        return R"CF(package main

import "core:compress/zstd"

fn main() {
    raw := []byte("high-throughput packet trace payload ... high-throughput packet trace payload ...")

    // Hardware-accelerated Zstandard frame compression
    compressed := try zstd.compress(raw, level: 3)
    println("Original size: {} bytes -> Compressed: {} bytes", raw.len(), compressed.len())

    // Decompression using the embedded frame content size
    decompressed := try zstd.decompress(compressed)
    println("Decompressed size: {} bytes", decompressed.len())
})CF";
    }
    if (mod == "core:json") {
        return R"CF(package main

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
})CF";
    }
    if (mod == "core:db/sqlite") {
        return R"CF(package main

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
})CF";
    }
    if (mod == "core:crypto/sha256") {
        return R"CF(package main

import "core:crypto/sha256"

fn main() {
    payload := "system_audit_transaction_event_2026"
    digest := sha256.hash_hex(payload)
    println("FIPS 180-4 SHA-256 Digest: {}", digest)
})CF";
    }
    if (mod == "core:crypto/hmac") {
        return R"CF(package main

import "core:crypto/hmac"

fn main() {
    secret_key := "super-secure-signing-secret"
    data := "tenant_id:42;session:8849"

    signature := hmac.sha256_hex(secret_key, data)
    println("HMAC-SHA256 signature: {}", signature)

    // Constant-time validation check prevents timing side-channel attacks
    is_valid := hmac.equal(signature, signature)
    println("Signature verified: {}", is_valid)
})CF";
    }
    if (mod == "core:crypto/aes") {
        return R"CF(package main

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
})CF";
    }
    if (mod == "core:crypto/blake3") {
        return R"CF(package main

import "core:crypto/blake3"

fn main() {
    blob := []byte("immutable chunk storage block 0x8842")
    hash := blake3.hash_hex(blob)
    println("Blake3 256-bit digest: {}", hash)
})CF";
    }
    if (mod == "core:net") {
        return R"CF(package main

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
})CF";
    }
    if (mod == "core:net/url") {
        return R"CF(package main

import "core:net/url"

fn main() {
    raw := "https://api.domain.com:8443/v1/search?q=systems+c%2B%2B&limit=25#results"
    u := try url.parse(raw)
    println("Scheme: {}, Host: {}, Port: {}, Path: {}", u.scheme, u.host, u.port, u.path)
    println("Query parameter 'q': {}", u.query["q"])

    encoded := url.encode("raw query with spaces & symbols!")
    println("Percent-encoded: {}", encoded)
})CF";
    }
    if (mod == "core:net/http") {
        return R"CF(package main

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
})CF";
    }
    if (mod == "core:net/websocket") {
        return R"CF(package main

import "core:net/websocket"

fn main() {
    // Establish full-duplex RFC 6455 WebSocket connection
    mut ws := try websocket.connect("ws://127.0.0.1:8080/market-data")
    defer ws.close()

    try ws.send_text("SUBSCRIBE BTC-USD")
    frame := try ws.recv_text()
    println("Received WebSocket stream frame: {}", frame)
})CF";
    }
    if (mod == "core:time") {
        return R"CF(package main

import "core:time"

fn main() {
    now := time.now()
    println("Current RFC 3339 timestamp: {}", now.format_rfc3339())

    // High-precision duration measurement
    time.sleep_ms(30)
    elapsed := time.since(now)
    println("Elapsed duration: {} ms ({} us)", elapsed.to_millis(), elapsed.to_micros())
})CF";
    }
    if (mod == "core:os") {
        return R"CF(package main

import "core:os"
import "core:os/exec"

fn main() {
    home := os.getenv("HOME") or "/tmp"
    println("Environment $HOME: {}", home)

    // Execute external process and capture stdout/exit code
    cmd := exec.command("git", ["rev-parse", "--short", "HEAD"])
    output := try cmd.output()
    println("Current Git commit: {} (exit code {})", output.stdout, output.exit_code)
})CF";
    }
    if (mod == "core:sync") {
        return R"CF(package main

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
})CF";
    }
    if (mod == "core:sync/context") {
        return R"CF(package main

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
})CF";
    }
    if (mod == "core:str") {
        return R"CF(package main

import "core:str"

fn main() {
    raw := "  systems, programming, language  "
    trimmed := str.trim(raw)
    tokens := str.split(trimmed, ", ")
    formatted := str.join(tokens, "::")
    println("Processed string: {}", formatted)
})CF";
    }
    if (mod == "core:math") {
        return R"CF(package main

import "core:math"

fn main() {
    bounded := math.clamp_f64(3.14159, 0.0, 1.0)
    hypot := math.sqrt(3.0 * 3.0 + 4.0 * 4.0)
    println("Clamped: {}, Hypot: {}", bounded, hypot)
})CF";
    }
    if (mod == "core:rand") {
        return R"CF(package main

import "core:rand"

fn main() {
    uid := rand.u64()
    dice_roll := rand.range_i64(1, 6)
    token_bytes := rand.bytes(16)
    println("ID: {}, Roll: {}, Entropy bytes: {}", uid, dice_roll, token_bytes.len())
})CF";
    }
    if (mod == "core:encoding/base64") {
        return R"CF(package main

import "core:encoding/base64"

fn main() {
    credentials := "service_account:token_xyz_9988"
    encoded := base64.encode_str(credentials)
    println("Base64 encoded: {}", encoded)

    decoded := try base64.decode_str(encoded)
    println("Decoded text: {}", decoded)
})CF";
    }
    if (mod == "core:encoding/hex") {
        return R"CF(package main

import "core:encoding/hex"

fn main() {
    binary_bytes := []byte{0xCA, 0xFE, 0xBA, 0xBE}
    hex_str := hex.encode(binary_bytes)
    println("Hexadecimal string: 0x{}", hex_str)

    restored := try hex.decode(hex_str)
    println("Restored byte length: {}", restored.len())
})CF";
    }
    if (mod == "core:encoding/csv") {
        return R"CF(package main

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
})CF";
    }
    if (mod == "core:regex") {
        return R"CF(package main

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
})CF";
    }
    if (mod == "core:collections") {
        return R"CF(package main

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
})CF";
    }
    if (mod == "core:sort") {
        return R"CF(package main

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
})CF";
    }
    if (mod == "core:log") {
        return R"CF(package main

import "core:log"

fn main() {
    log.set_level(log.Level::Debug)
    log.set_format(log.Format::Json) // Output format: Text or Json

    // Leveled structured logging with key-value attributes
    log.info("service worker started", "port", "8080", "cluster", "prod-us-east")
    log.warn("memory threshold reached", "usage_pct", "84", "threshold", "80")
    log.error("database pool exhausted", "active_conns", "100", "pool_size", "100")
})CF";
    }
    if (mod == "core:unicode") {
        return R"CF(package main

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
})CF";
    }
    return "";
}

[[nodiscard]] inline auto shared_css() -> std::string {
    return R"CSS(
:root {
    --bg: #FAFAF9;
    --bg-card: #FFFFFF;
    --bg-card-hover: #F5F5F4;
    --bg-code: #18181B;
    --border: #E7E5E4;
    --border-dark: #D6D3D1;
    --text: #1C1917;
    --text-bright: #0C0A09;
    --muted: #57534E;
    --accent: #1D4ED8;
    --accent-hover: #1E40AF;
    --ok: #047857;
    --warn: #B45309;
    --err: #B91C1C;
    --font-sans: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, "Helvetica Neue", sans-serif;
    --font-mono: ui-monospace, "JetBrains Mono", SFMono-Regular, Menlo, Monaco, Consolas, monospace;
}
*, *::before, *::after { box-sizing: border-box; margin: 0; padding: 0; }
html, body {
    width: 100%;
    max-width: 100%;
    overflow-x: hidden;
    margin: 0;
    padding: 0;
}
body {
    font-family: var(--font-sans);
    background: var(--bg);
    color: var(--text);
    line-height: 1.65;
    min-height: 100vh;
    -webkit-font-smoothing: antialiased;
}
::selection { background: #1C1917; color: #FFFFFF; }
a { color: var(--accent); text-decoration: none; transition: color 0.15s ease; }
a:hover { color: var(--accent-hover); text-decoration: underline; }

/* Sticky Edge-to-Edge Navigation Bar */
.navbar {
    position: sticky; top: 0; z-index: 100;
    width: 100%;
    background: rgba(255, 255, 255, 0.96);
    backdrop-filter: blur(12px);
    -webkit-backdrop-filter: blur(12px);
    border-bottom: 1px solid var(--border);
}
.navbar-inner {
    max-width: 1120px;
    margin: 0 auto;
    padding: 0.75rem 1.25rem;
    display: flex;
    align-items: center;
    justify-content: space-between;
    gap: 1rem;
    width: 100%;
    box-sizing: border-box;
}
.brand {
    display: flex; align-items: center; gap: 0.65rem;
    font-weight: 800; font-size: 1.25rem; color: var(--text);
    text-decoration: none; flex-shrink: 0;
}
.brand:hover { text-decoration: none; }
.brand img.brand-icon {
    width: 30px; height: 30px; object-fit: contain;
}
.brand span.brand-text {
    font-size: 1.2rem; font-weight: 850; letter-spacing: -0.02em; color: var(--text);
}
.brand span.fire { color: #EA580C; font-weight: 850; }
.brand-badge {
    font-family: var(--font-mono); font-size: 0.65rem; font-weight: 700;
    background: #F5F5F4; border: 1px solid var(--border); color: #57534E;
    padding: 0.12rem 0.45rem; border-radius: 4px; margin-left: 0.25rem;
}
.nav-links { display: flex; align-items: center; gap: 1.25rem; font-size: 0.92rem; font-weight: 600; }
.nav-links a {
    color: var(--muted); transition: all 0.15s ease;
    padding: 0.25rem 0.4rem; border-radius: 5px;
}
.nav-links a:hover { color: var(--text); text-decoration: none; background: #F5F5F4; }
.nav-links a.active { color: var(--text); text-decoration: none; font-weight: 750; background: #F5F5F4; }
.btn-nav-primary {
    background: #1C1917 !important; color: #FFFFFF !important;
    padding: 0.4rem 0.9rem !important; border-radius: 6px !important; font-size: 0.85rem !important; font-weight: 600 !important;
    transition: background 0.15s !important; border: none;
}
.btn-nav-primary:hover { background: #292524 !important; text-decoration: none !important; }

/* Page container with robust bounds */
.container {
    width: 100%;
    max-width: 1120px;
    margin: 0 auto;
    padding: 2.5rem 1.25rem 5rem;
    box-sizing: border-box;
    min-width: 0;
}

/* Documentation Layout (Guide & Reference) */
.doc-layout {
    display: grid;
    grid-template-columns: 260px minmax(0, 1fr);
    gap: 2.5rem;
    align-items: start;
    width: 100%;
    min-width: 0;
}
.sidebar {
    position: sticky;
    top: 4.5rem;
    max-height: calc(100vh - 5.5rem);
    overflow-y: auto;
    background: #FFFFFF;
    border: 1px solid var(--border);
    border-radius: 8px;
    padding: 1.25rem;
    box-shadow: 0 1px 2px rgba(0,0,0,0.02);
}
.sidebar-summary { display: none; }
.sidebar h4 {
    font-size: 0.75rem; font-weight: 800; text-transform: uppercase;
    letter-spacing: 0.08em; color: var(--muted); margin-bottom: 0.75rem;
}
.sidebar ul { list-style: none; margin-bottom: 1.25rem; }
.sidebar li { margin-bottom: 0.25rem; }
.sidebar a {
    display: block; padding: 0.35rem 0.6rem; border-radius: 5px;
    color: var(--muted); font-size: 0.88rem; text-decoration: none;
    transition: all 0.15s ease;
    white-space: nowrap; overflow: hidden; text-overflow: ellipsis;
}
.sidebar a:hover {
    background: #F5F5F4; color: var(--text); text-decoration: none;
}
.sidebar a.active {
    background: #F5F5F4; color: var(--accent); font-weight: 700;
}
.sidebar a code { background: none; border: none; padding: 0; color: inherit; font-size: inherit; }

main {
    min-width: 0;
    max-width: 100%;
    width: 100%;
    overflow-wrap: break-word;
    word-break: break-word;
}
main h1 {
    font-size: clamp(1.85rem, 4vw, 2.5rem); font-weight: 800;
    color: var(--text); letter-spacing: -0.03em; margin-bottom: 0.75rem;
}
main h2 {
    font-size: 1.5rem; font-weight: 800; color: var(--text);
    letter-spacing: -0.02em; margin: 2.5rem 0 0.85rem; padding-bottom: 0.4rem;
    border-bottom: 1px solid var(--border);
}
main h3 {
    font-size: 1.2rem; font-weight: 700; color: var(--text);
    margin: 1.75rem 0 0.5rem;
}
main p { color: #44403C; font-size: 0.98rem; line-height: 1.7; margin-bottom: 1rem; }
main ul, main ol { margin: 0 0 1.25rem 1.5rem; color: #44403C; font-size: 0.98rem; line-height: 1.7; }
main li { margin-bottom: 0.4rem; }

/* Hero section */
.hero { text-align: center; padding: 3rem 0.5rem 3.5rem; }
.badge {
    display: inline-block; padding: 0.35rem 0.85rem; border-radius: 999px;
    font-family: var(--font-mono); font-size: 0.75rem; font-weight: 700;
    letter-spacing: 0.06em; text-transform: uppercase;
    background: #FFFFFF; border: 1px solid var(--border); color: var(--accent);
    margin-bottom: 1.5rem;
}
h1.hero-title {
    font-size: clamp(2rem, 5vw, 3.25rem); font-weight: 850;
    letter-spacing: -0.03em; color: var(--text); margin-bottom: 1.25rem;
    line-height: 1.15;
}
.subtitle {
    max-width: 44rem; margin: 0 auto 2.25rem; font-size: clamp(1rem, 2.5vw, 1.2rem);
    color: var(--muted); line-height: 1.6;
}
.cta-row { display: flex; justify-content: center; gap: 0.85rem; flex-wrap: wrap; margin-bottom: 2rem; }
.btn {
    padding: 0.75rem 1.5rem; border-radius: 8px; font-weight: 600; font-size: 0.95rem;
    border: 1px solid var(--border); transition: all 0.15s ease; display: inline-flex;
    align-items: center; gap: 0.5rem; text-decoration: none; cursor: pointer;
}
.btn:hover { text-decoration: none; }
.btn-primary { background: #1C1917; color: #FFFFFF; border-color: #1C1917; }
.btn-primary:hover { background: #292524; border-color: #292524; }
.btn-secondary { background: #FFFFFF; color: var(--text); border-color: var(--border); }
.btn-secondary:hover { background: #F5F5F4; border-color: var(--border-dark); }

/* Install Box */
.install-bar {
    display: inline-flex; align-items: center; justify-content: space-between; gap: 0.75rem;
    background: #FFFFFF; border: 1px solid var(--border); border-radius: 8px;
    padding: 0.65rem 1rem; font-family: var(--font-mono); font-size: 0.88rem;
    color: #292524; max-width: 100%; overflow-x: auto;
}
.install-bar .prompt { color: var(--muted); font-weight: 700; user-select: none; }
.copy-btn {
    background: #1C1917; color: #FFFFFF; border: none;
    padding: 0.35rem 0.75rem; border-radius: 5px; font-size: 0.75rem; font-weight: 700;
    font-family: var(--font-mono); cursor: pointer; transition: background 0.15s; shrink-0;
}
.copy-btn:hover { background: #292524; }

/* Stats ribbon */
.stats-ribbon {
    display: grid; grid-template-columns: repeat(auto-fit, minmax(180px, 1fr)); gap: 1rem;
    margin: 3rem 0 2rem;
}
.stat-pill {
    background: #FFFFFF; border: 1px solid var(--border); border-radius: 8px;
    padding: 1.25rem 1rem; text-align: center;
}
.stat-val {
    font-size: 1.85rem; font-weight: 850; color: var(--text); font-family: var(--font-mono);
}
.stat-lbl {
    font-size: 0.76rem; color: var(--muted); font-weight: 700; text-transform: uppercase;
    letter-spacing: 0.06em; margin-top: 0.35rem;
}

/* Grids & Cards */
.grid-2 { display: grid; grid-template-columns: repeat(auto-fit, minmax(340px, 1fr)); gap: 1.25rem; }
.grid-3 { display: grid; grid-template-columns: repeat(auto-fit, minmax(280px, 1fr)); gap: 1.25rem; }
.card {
    background: #FFFFFF; border: 1px solid var(--border); border-radius: 8px;
    padding: 1.5rem; transition: border-color 0.15s ease;
}
.card:hover { border-color: var(--border-dark); }
.card-num {
    font-family: var(--font-mono); font-size: 0.78rem; font-weight: 800;
    color: var(--accent); margin-bottom: 0.5rem; text-transform: uppercase;
}
.card h3 { font-size: 1.15rem; font-weight: 750; margin-bottom: 0.5rem; color: var(--text); }
.card p { color: var(--muted); font-size: 0.92rem; line-height: 1.6; margin: 0; }

/* Code Blocks: Strict Horizontal Scroll Containment */
pre {
    background: var(--bg-code); border: 1px solid #27272A;
    border-radius: 8px; padding: 1.1rem 1.25rem; overflow-x: auto;
    max-width: 100%; min-width: 0; box-sizing: border-box;
    font-family: var(--font-mono); font-size: 0.85rem; line-height: 1.65;
    color: #F4F4F5; -webkit-overflow-scrolling: touch;
}
code {
    font-family: var(--font-mono); font-size: 0.88em;
    background: #F5F5F4; border: 1px solid var(--border);
    padding: 0.12rem 0.35rem; border-radius: 4px; color: #1C1917;
    word-break: break-word;
}
pre code { background: none; border: none; padding: 0; color: inherit; word-break: normal; }
.code-header {
    font-family: var(--font-mono); font-size: 0.75rem; font-weight: 700;
    text-transform: uppercase; letter-spacing: 0.05em; color: var(--muted);
    margin-bottom: 0.45rem; display: flex; justify-content: space-between;
}
.code-header span.tag-cf { color: #60A5FA; }
.code-header span.tag-cpp { color: #F59E0B; }

.section-title {
    font-size: clamp(1.6rem, 3.5vw, 2.2rem); font-weight: 800; margin: 4rem 0 0.75rem;
    text-align: center; letter-spacing: -0.02em; color: var(--text);
}
.section-subtitle {
    text-align: center; color: var(--muted); font-size: 1.05rem; max-width: 640px; margin: 0 auto 2.5rem;
}

.tabs { display: flex; gap: 0.5rem; margin-bottom: 1.25rem; justify-content: center; flex-wrap: wrap; }
.tab-btn {
    background: #FFFFFF; color: var(--muted); border: 1px solid var(--border);
    padding: 0.5rem 1rem; border-radius: 6px; cursor: pointer; font-weight: 600;
    font-size: 0.88rem; font-family: var(--font-mono); transition: all 0.15s;
}
.tab-btn:hover { color: var(--text); border-color: var(--border-dark); }
.tab-btn.active {
    background: #1C1917; color: #FFFFFF; border-color: #1C1917;
}
.tab-panel { display: none; }
.tab-panel.active { display: block; }

/* Rosetta Mobile Toggle */
.rosetta-mobile-toggle {
    display: none; margin-bottom: 0.75rem; gap: 0.35rem; background: #E7E5E4;
    padding: 0.25rem; border-radius: 6px;
}
.rosetta-mobile-toggle button {
    flex: 1; padding: 0.45rem 0.5rem; border: none; background: transparent;
    font-family: var(--font-mono); font-size: 0.78rem; font-weight: 700;
    color: var(--muted); border-radius: 4px; cursor: pointer;
}
.rosetta-mobile-toggle button.active {
    background: #FFFFFF; color: var(--text);
}

/* Console preview */
.console-preview {
    background: #F5F5F4; border: 1px solid var(--border); border-radius: 6px;
    padding: 0.75rem 1rem; margin-top: 0.85rem; font-family: var(--font-mono);
    font-size: 0.82rem; color: #1C1917;
}
.console-header {
    font-size: 0.7rem; font-weight: 700; color: var(--muted); text-transform: uppercase;
    letter-spacing: 0.05em; margin-bottom: 0.3rem; display: flex; align-items: center; gap: 0.35rem;
}
.console-dot { width: 7px; height: 7px; border-radius: 50%; background: #059669; }

/* Robust Table System: Isolated Scrolling & Polished Mobile Experience */
.table-wrap {
    position: relative;
    width: 100%;
    max-width: 100%;
    min-width: 0;
    overflow-x: auto;
    -webkit-overflow-scrolling: touch;
    margin: 1.5rem 0 2.25rem;
    background: #FFFFFF;
    border: 1px solid var(--border);
    border-radius: 8px;
    box-shadow: 0 1px 3px rgba(0, 0, 0, 0.02);
}
.table-wrap table {
    width: 100%;
    min-width: 620px;
    border-collapse: collapse;
    background: transparent;
    border: none;
    margin: 0;
}
th, td {
    text-align: left; padding: 0.85rem 1rem; border-bottom: 1px solid var(--border);
    font-size: 0.9rem; vertical-align: top;
}
th {
    background: #F5F5F4; color: var(--text); font-weight: 750; font-size: 0.82rem;
    text-transform: uppercase; letter-spacing: 0.04em;
}
tr:last-child td { border-bottom: none; }
tr:hover td { background: #FAFAF9; }

/* Subtle mobile scroll hint banner */
.table-scroll-cue {
    display: none;
    font-size: 0.72rem;
    font-weight: 700;
    color: var(--muted);
    text-transform: uppercase;
    letter-spacing: 0.05em;
    padding: 0.4rem 0.85rem;
    background: #F5F5F4;
    border-bottom: 1px solid var(--border);
    text-align: right;
}

/* Step Grid */
.step-grid {
    display: grid; grid-template-columns: repeat(auto-fit, minmax(280px, 1fr)); gap: 1.25rem; margin: 2rem 0;
}
.step-card {
    background: #FFFFFF; border: 1px solid var(--border); border-radius: 8px;
    padding: 1.5rem; position: relative;
}
.step-badge {
    display: inline-block; font-family: var(--font-mono); font-size: 0.72rem;
    font-weight: 800; text-transform: uppercase; letter-spacing: 0.05em;
    background: #1C1917; color: #FFFFFF; padding: 0.2rem 0.6rem; border-radius: 4px;
    margin-bottom: 0.75rem;
}
.step-card h4 { font-size: 1.15rem; margin-bottom: 0.5rem; color: var(--text); }
.step-card p { font-size: 0.9rem; color: var(--muted); line-height: 1.6; margin-bottom: 0.85rem; }

/* Roadmap */
.roadmap-grid {
    display: grid; grid-template-columns: repeat(auto-fit, minmax(240px, 1fr)); gap: 1rem; margin: 2rem 0;
}
.roadmap-card {
    background: #FFFFFF; border: 1px solid var(--border); border-radius: 8px;
    padding: 1.25rem; transition: border-color 0.15s ease; text-decoration: none; display: block;
}
.roadmap-card:hover {
    border-color: var(--border-dark); text-decoration: none;
}
.roadmap-num {
    font-family: var(--font-mono); font-size: 0.72rem; font-weight: 800;
    text-transform: uppercase; letter-spacing: 0.05em; color: var(--accent); margin-bottom: 0.35rem;
}
.roadmap-card h4 { font-size: 1.05rem; color: var(--text); margin-bottom: 0.35rem; font-weight: 700; }
.roadmap-card p { font-size: 0.85rem; color: var(--muted); line-height: 1.5; margin: 0; }

.footer {
    text-align: center; padding: 3rem 1rem 2.5rem; border-top: 1px solid var(--border);
    background: #FFFFFF; color: var(--muted); font-size: 0.88rem; margin-top: 4.5rem;
}
.footer-logo { max-width: 120px; margin-bottom: 1rem; }
.footer-links { display: flex; justify-content: center; gap: 1.5rem; margin: 1rem 0; flex-wrap: wrap; }
.footer-links a { color: var(--muted); font-weight: 600; font-size: 0.88rem; }
.footer-links a:hover { color: var(--text); }

/* Responsive adjustments for mobile & tablet */
@media (max-width: 840px) {
    .container { padding: 1.5rem 1rem 3.5rem; }
    .doc-layout { display: block; width: 100%; min-width: 0; }
    .sidebar {
        position: static; max-height: none; width: 100%;
        margin-bottom: 2rem; padding: 0.85rem 1rem;
    }
    .sidebar-summary {
        display: flex; align-items: center; justify-content: space-between;
        font-weight: 750; font-size: 0.9rem; cursor: pointer; color: var(--text);
        user-select: none;
    }
    .sidebar-summary::-webkit-details-marker { display: none; }
    .sidebar-chevron { font-size: 0.8rem; color: var(--muted); transition: transform 0.2s ease; }
    .sidebar-details:not([open]) .sidebar-content { display: none; }
    .sidebar-details[open] .sidebar-content {
        display: block; margin-top: 0.85rem; padding-top: 0.85rem;
        border-top: 1px solid var(--border); max-height: 50vh; overflow-y: auto;
    }
    .sidebar-details[open] .sidebar-chevron { transform: rotate(180deg); }
    .grid-2 { grid-template-columns: 1fr; }
    .rosetta-mobile-toggle { display: flex; }
    .rosetta-pane-cpp { display: none; }
    .rosetta-pane-cpp.is-active { display: block; }
    .rosetta-pane-cf.is-inactive { display: none; }
    .hero { padding: 2rem 0.25rem 2.5rem; }
}

@media (max-width: 680px) {
    .navbar-inner { flex-wrap: wrap; padding: 0.6rem 0.85rem; gap: 0.5rem; }
    .brand { font-size: 1.1rem; }
    .brand img.brand-icon { width: 26px; height: 26px; }
    .nav-links {
        display: flex; align-items: center; gap: 0.4rem;
        width: 100%; overflow-x: auto; -webkit-overflow-scrolling: touch;
        scrollbar-width: none; padding: 0.1rem 0;
    }
    .nav-links::-webkit-scrollbar { display: none; }
    .nav-links a {
        white-space: nowrap; padding: 0.35rem 0.65rem; border-radius: 6px;
        background: #F5F5F4; font-size: 0.82rem; color: var(--muted); flex-shrink: 0;
    }
    .nav-links a.active { background: #1C1917; color: #FFFFFF !important; }
    .btn-nav-primary {
        margin-left: auto; padding: 0.35rem 0.75rem !important;
        font-size: 0.8rem !important; flex-shrink: 0;
    }
    .table-scroll-cue { display: block; }

    /* API Reference Tables: Mobile Card Transformation */
    .api-table-wrap {
        border: none; background: transparent; overflow-x: visible;
        box-shadow: none; margin: 1rem 0 1.75rem;
    }
    .api-table-wrap .table-scroll-cue { display: none; }
    .api-table-wrap table,
    .api-table-wrap thead,
    .api-table-wrap tbody,
    .api-table-wrap th,
    .api-table-wrap td,
    .api-table-wrap tr {
        display: block; width: 100%; min-width: 0 !important; box-sizing: border-box;
    }
    .api-table-wrap thead { display: none; }
    .api-table-wrap tr {
        background: #FFFFFF; border: 1px solid var(--border); border-radius: 8px;
        margin-bottom: 0.85rem; padding: 0.95rem 1rem; box-shadow: 0 1px 2px rgba(0, 0, 0, 0.03);
    }
    .api-table-wrap td { padding: 0.25rem 0; border: none; font-size: 0.88rem; }
    .api-table-wrap td:first-child {
        padding-bottom: 0.45rem; border-bottom: 1px dashed var(--border); margin-bottom: 0.45rem;
    }
    .api-table-wrap td:first-child code {
        font-weight: 700; font-size: 0.88rem; color: var(--accent);
        background: #EFF6FF; border-color: #DBEAFE; word-break: break-all;
        display: inline-block; padding: 0.2rem 0.45rem; border-radius: 4px;
    }
    .api-table-wrap td:nth-child(2)::before {
        content: "C++23: "; font-weight: 700; font-size: 0.72rem; text-transform: uppercase;
        letter-spacing: 0.05em; color: var(--muted); display: inline-block; margin-right: 0.35rem;
    }
    .api-table-wrap td:nth-child(2) {
        font-size: 0.82rem; color: #57534E; padding-bottom: 0.35rem; word-break: break-all;
    }
    .api-table-wrap td:last-child { color: #1C1917; line-height: 1.55; }
    .api-table-wrap td:last-child code {
        display: block; margin-top: 0.35rem; word-break: break-all; background: #F5F5F4;
    }
}

@media (min-width: 841px) {
    .rosetta-mobile-toggle { display: none; }
    .rosetta-pane-cf, .rosetta-pane-cpp { display: block !important; }
}
)CSS";
}

[[nodiscard]] inline auto navbar_html_cxx(std::string_view active_page) -> std::string {
    return std::format(R"HTML(
<nav class="navbar">
  <div class="navbar-inner">
    <a class="brand" href="index.html">
      <img src="assets/cfire_logo_emblem.png" alt="cfire logo" class="brand-icon" />
      <span class="brand-text">c<span class="fire">fire</span></span>
      <span class="brand-badge">ISO C++23</span>
    </a>
    <div class="nav-links">
      <a href="index.html" class="{0}">Overview</a>
      <a href="guide.html" class="{1}">Guide</a>
      <a href="reference.html" class="{2}">Stdlib</a>
      <a href="https://github.com/gsandoval/cfire" target="_blank" rel="noopener">GitHub &rarr;</a>
      <a href="index.html#install" class="btn-nav-primary">Install</a>
    </div>
  </div>
</nav>
)HTML",
        active_page == "index" ? "active" : "",
        active_page == "guide" ? "active" : "",
        active_page == "reference" ? "active" : "");
}

[[nodiscard]] inline auto navbar_html(std::string_view active_page) -> std::string {
    const char* force_cxx = std::getenv("CFIRE_CXX_DOCGEN");
    if (force_cxx && std::string_view(force_cxx) == "1") {
        return navbar_html_cxx(active_page);
    }
    return ::navbar_html(active_page);
}

} // namespace detail

[[nodiscard]] inline auto html_escape(std::string_view s) -> std::string {
    return detail::html_escape(s);
}

[[nodiscard]] inline auto compile_snippet_to_cpp(std::string_view filename, std::string_view cf_source) -> std::string {
    auto ast = cfire::frontend::parse_source(std::string(filename), cf_source);
    if (!ast) return std::format("// Parse error: {}\n", ast.error());
    auto checked = cfire::frontend::analyze(std::move(*ast));
    if (!checked) return std::format("// Sema error: {}\n", checked.error());
    return cfire::frontend::emit_cpp(*checked);
}

[[nodiscard]] inline auto generate_reproducible_metrics() -> std::string {
    return std::format(R"HTML(  <!-- Metrics Ribbon -->
  <div class="stats-ribbon">
    <div class="stat-pill">
      <div class="stat-val">0 ms</div>
      <div class="stat-lbl">GC Pauses (Pure RAII)</div>
    </div>
    <div class="stat-pill">
      <div class="stat-val">1-to-1</div>
      <div class="stat-lbl">Readable ISO C++23</div>
    </div>
    <div class="stat-pill">
      <div class="stat-val">{}</div>
      <div class="stat-lbl">Built-in `core:*` Modules</div>
    </div>
    <div class="stat-pill">
      <div class="stat-val">0 &mu;s</div>
      <div class="stat-lbl">C / C++ Binding Overhead</div>
    </div>
    <div class="stat-pill">
      <div class="stat-val">1-Click</div>
      <div class="stat-lbl">CMake Project Ejection</div>
    </div>
  </div>)HTML", stdlib_catalog().size());
}

[[nodiscard]] inline auto generate_reproducible_showcase() -> std::string {
    // 1. Intents Snippet
    std::string_view snip1_cf = R"(package main

type Box struct {
    value: i32
}

fn increment(mut b: Box, amount: i32) {
    b.value = b.value + amount
}

fn main() {
    mut b := Box{ value: 40 }
    // Call-site 'mut' marker is mandatory:
    increment(mut b, 2)
    
    // Explicit unique ownership:
    p := make(own[Box], Box{ value: 100 })
    println("b={} p={}", b.value, (*p).value)
})";
    std::string snip1_cpp = compile_snippet_to_cpp("snippet_intents.cf", snip1_cf);

    // 2. Patterns Snippet
    std::string_view snip2_cf = R"(package main

type Token enum {
    Ident(name: str),
    Number(val: i64),
    Eof,
}

fn classify(s: []str) -> str {
    match s {
        []                   => { return "empty" }
        [head, ..tail]       => { return "head=" + head }
        [first, ..mid, last] => { return first + ".." + last }
    }
    return "other"
}

fn main() {
    items: []str = ["alpha", "beta", "gamma"]
    println("result: {}", classify(items))
})";
    std::string snip2_cpp = compile_snippet_to_cpp("snippet_patterns.cf", snip2_cf);

    // 3. WebSockets & Concurrency Snippet
    std::string_view snip3_cf = R"(package main

import "core:net/websocket"
import "core:sync/context"
import "core:time"

fn main() {
    bg := context.background()
    (ctx, cancel) := context.with_timeout(bg, 500)
    defer cancel("done")

    (mut client, mut server) := try websocket.pair()
    try client.send_text("ping-payload")
    msg := try server.recv_text()
    println("received: {}", msg)
})";
    std::string snip3_cpp = compile_snippet_to_cpp("snippet_net.cf", snip3_cf);

    // 4. JSON Struct Tags Snippet
    std::string_view snip4_cf = R"(package main

import "core:json"

type Metric struct {
    service: str  `json:"service"`
    latency: f64  `json:"latency_ms"`
    healthy: bool `json:"healthy"`
}

fn main() {
    payload := `{"service":"auth","latency_ms":1.42,"healthy":true}`
    m := try json.parse[Metric](payload)
    println("service={} latency={}ms", m.service, m.latency)
})";
    std::string snip4_cpp = compile_snippet_to_cpp("snippet_json.cf", snip4_cf);

    // 5. Clang Interop Snippet
    std::string_view snip5_cf = R"(package main

// Direct C header ingestion via Clang AST bridge:
import cpp "fast_hash.h"

fn main() {
    msg := "cfire systems programming"
    // Automatic boundary coercion: 'str' passes as 'const char*'
    hash := cpp::fh_fnv1a(msg, msg.len())
    println("fnv1a hash: 0x{:x}", hash)
})";
    std::string snip5_cpp = compile_snippet_to_cpp("snippet_interop.cf", snip5_cf);

    std::string out;
    out += R"HTML(  <!-- Interactive Live Rosetta Showcase -->
  <h2 class="section-title">See the Lowering: Live `.cf` &rarr; ISO C++23</h2>
  <p class="section-subtitle">Select any systems pattern below to inspect the clean, idiomatic ISO C++23 code emitted by the compiler.</p>

  <div class="tabs">
    <button class="tab-btn active" onclick="showTab('tab-intents', this)">1. Intents &amp; Smart Pointers</button>
    <button class="tab-btn" onclick="showTab('tab-patterns', this)">2. Enums &amp; Slice Patterns</button>
    <button class="tab-btn" onclick="showTab('tab-net', this)">3. WebSockets &amp; Cancellation</button>
    <button class="tab-btn" onclick="showTab('tab-json', this)">4. SIMD JSON Reflection</button>
    <button class="tab-btn" onclick="showTab('tab-interop', this)">5. Clang C/C++ Interop</button>
  </div>

  <!-- Tab 1: Intents -->
  <div id="tab-intents" class="tab-panel active">
    <div class="rosetta-mobile-toggle">
      <button class="active" onclick="toggleRosettaMobile(this, 'cf')">cfire (.cf)</button>
      <button onclick="toggleRosettaMobile(this, 'cpp')">Emitted ISO C++23</button>
    </div>
    <div class="grid-2">
      <div class="rosetta-pane-cf">
        <div class="code-header"><span class="tag-cf">cfire (.cf)</span><span>Call-site Intent Safety</span></div>
        <pre><code>)HTML";
    out += html_escape(snip1_cf);
    out += R"HTML(</code></pre>
      </div>
      <div class="rosetta-pane-cpp">
        <div class="code-header"><span class="tag-cpp">Emitted ISO C++23</span><span>Compiler-Verified Lowering</span></div>
        <pre><code>)HTML";
    out += html_escape(snip1_cpp);
    out += R"HTML(</code></pre>
      </div>
    </div>
    <div class="console-preview">
      <div class="console-header"><span class="console-dot"></span> Terminal Output Preview</div>
      <div>$ cfire run main.cf<br />b=42 p=100</div>
    </div>
  </div>

  <!-- Tab 2: Enums & Patterns -->
  <div id="tab-patterns" class="tab-panel">
    <div class="rosetta-mobile-toggle">
      <button class="active" onclick="toggleRosettaMobile(this, 'cf')">cfire (.cf)</button>
      <button onclick="toggleRosettaMobile(this, 'cpp')">Emitted ISO C++23</button>
    </div>
    <div class="grid-2">
      <div class="rosetta-pane-cf">
        <div class="code-header"><span class="tag-cf">cfire (.cf)</span><span>Exhaustive Match &amp; Destructuring</span></div>
        <pre><code>)HTML";
    out += html_escape(snip2_cf);
    out += R"HTML(</code></pre>
      </div>
      <div class="rosetta-pane-cpp">
        <div class="code-header"><span class="tag-cpp">Emitted ISO C++23</span><span>std::variant &amp; std::span</span></div>
        <pre><code>)HTML";
    out += html_escape(snip2_cpp);
    out += R"HTML(</code></pre>
      </div>
    </div>
    <div class="console-preview">
      <div class="console-header"><span class="console-dot"></span> Terminal Output Preview</div>
      <div>$ cfire run main.cf<br />result: head=alpha</div>
    </div>
  </div>

  <!-- Tab 3: WebSockets & Cancellation -->
  <div id="tab-net" class="tab-panel">
    <div class="rosetta-mobile-toggle">
      <button class="active" onclick="toggleRosettaMobile(this, 'cf')">cfire (.cf)</button>
      <button onclick="toggleRosettaMobile(this, 'cpp')">Emitted ISO C++23</button>
    </div>
    <div class="grid-2">
      <div class="rosetta-pane-cf">
        <div class="code-header"><span class="tag-cf">cfire (.cf)</span><span>Modern Concurrency &amp; WebSockets</span></div>
        <pre><code>)HTML";
    out += html_escape(snip3_cf);
    out += R"HTML(</code></pre>
      </div>
      <div class="rosetta-pane-cpp">
        <div class="code-header"><span class="tag-cpp">Emitted ISO C++23</span><span>RFC 6455 Loopback Pair</span></div>
        <pre><code>)HTML";
    out += html_escape(snip3_cpp);
    out += R"HTML(</code></pre>
      </div>
    </div>
    <div class="console-preview">
      <div class="console-header"><span class="console-dot"></span> Terminal Output Preview</div>
      <div>$ cfire run main.cf<br />received: ping-payload</div>
    </div>
  </div>

  <!-- Tab 4: JSON -->
  <div id="tab-json" class="tab-panel">
    <div class="rosetta-mobile-toggle">
      <button class="active" onclick="toggleRosettaMobile(this, 'cf')">cfire (.cf)</button>
      <button onclick="toggleRosettaMobile(this, 'cpp')">Emitted ISO C++23</button>
    </div>
    <div class="grid-2">
      <div class="rosetta-pane-cf">
        <div class="code-header"><span class="tag-cf">cfire (.cf)</span><span>Compile-Time SIMD Struct Tags</span></div>
        <pre><code>)HTML";
    out += html_escape(snip4_cf);
    out += R"HTML(</code></pre>
      </div>
      <div class="rosetta-pane-cpp">
        <div class="code-header"><span class="tag-cpp">Emitted ISO C++23</span><span>simdjson::ondemand Specialized</span></div>
        <pre><code>)HTML";
    out += html_escape(snip4_cpp);
    out += R"HTML(</code></pre>
      </div>
    </div>
    <div class="console-preview">
      <div class="console-header"><span class="console-dot"></span> Terminal Output Preview</div>
      <div>$ cfire run main.cf<br />service=auth latency=1.42ms</div>
    </div>
  </div>

  <!-- Tab 5: Interop -->
  <div id="tab-interop" class="tab-panel">
    <div class="rosetta-mobile-toggle">
      <button class="active" onclick="toggleRosettaMobile(this, 'cf')">cfire (.cf)</button>
      <button onclick="toggleRosettaMobile(this, 'cpp')">Emitted ISO C++23</button>
    </div>
    <div class="grid-2">
      <div class="rosetta-pane-cf">
        <div class="code-header"><span class="tag-cf">cfire (.cf)</span><span>Direct C/C++ Header Ingestion</span></div>
        <pre><code>)HTML";
    out += html_escape(snip5_cf);
    out += R"HTML(</code></pre>
      </div>
      <div class="rosetta-pane-cpp">
        <div class="code-header"><span class="tag-cpp">Emitted ISO C++23</span><span>Direct Inlined Invocation</span></div>
        <pre><code>)HTML";
    out += html_escape(snip5_cpp);
    out += R"HTML(</code></pre>
      </div>
    </div>
    <div class="console-preview">
      <div class="console-header"><span class="console-dot"></span> Terminal Output Preview</div>
      <div>$ cfire run main.cf<br />fnv1a hash: 0x811c9dc5</div>
    </div>
  </div>)HTML";

    return out;
}

[[nodiscard]] inline auto generate_landing_html(const std::filesystem::path& docs_dir = "docs") -> std::string {
    auto template_path = docs_dir / "index.template.html";
    std::string tmpl;
    if (std::filesystem::exists(template_path)) {
        std::ifstream ifs(template_path);
        std::stringstream ss;
        ss << ifs.rdbuf();
        tmpl = ss.str();
    }

    if (tmpl.empty()) {
        // Fallback default template if template file is absent
        tmpl = R"HTML(<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8" />
  <meta name="viewport" content="width=device-width, initial-scale=1.0" />
  <title>cfire — Sapphire on Fire | Ergonomic Systems Language Lowering 1-to-1 to ISO C++23</title>
  <link rel="icon" type="image/png" href="assets/cfire_logo_emblem.png" />
  <style>)HTML" + detail::shared_css() + R"HTML(</style>
</head>
<body>
)HTML" + detail::navbar_html("index") + R"HTML(
<div class="container">
  <section class="hero">
    <div><span class="badge">&#128507; PURE C++23 SELF-HOSTED &bull; ZERO-WRAPPER ABI &bull; BUILT-IN LSP &amp; FMT</span></div>

    <!-- Interactive Logo Showcase -->
    <div class="logo-showcase">
      <div class="logo-stage">
        <img id="hero-logo-img" class="hero-logo-img" src="assets/cfire_logo_badge_cfire.png" alt="cfire Sapphire on Fire Mountain Emblem" />
      </div>
      <div class="logo-switcher">
        <button id="btn-edition-1" class="logo-switch-btn active" onclick="switchBrandEdition('badge_cfire', this)">
          <span class="switch-dot"></span> Classic Wordmark
        </button>
        <button id="btn-edition-2" class="logo-switch-btn" onclick="switchBrandEdition('badge_fire', this)">
          <span class="switch-dot"></span> Systems c[&#128293;]
        </button>
        <button id="btn-edition-3" class="logo-switch-btn" onclick="switchBrandEdition('horizontal', this)">
          <span class="switch-dot"></span> Horizontal Banner
        </button>
      </div>
    </div>

    <h1 class="hero-title">
      The Speed &amp; Safety of C++23.<br />
      <span class="gradient-text">The Clarity &amp; Joy of Go.</span>
    </h1>
    <p class="subtitle">
      Zero garbage collection pauses. Zero foreign-wrapper runtime overhead. <code>cfire</code> pairs Go's 10-minute onboarding and Odin's batteries-included <code>core:*</code> standard library with Rust-grade call-site intent safety&mdash;lowering 1-to-1 into human-readable, zero-overhead <strong>ISO C++23</strong>.
    </p>

    <div class="cta-row">
      <a class="btn btn-primary" href="guide.html#chapter-1">&#128640; Start in 5 Minutes &rarr;</a>
      <a class="btn btn-secondary" href="guide.html">&#128214; Read the 10-Chapter Guide</a>
      <a class="btn btn-secondary" href="reference.html">&#9889; Explore Stdlib Reference</a>
    </div>

    <div>
      <div class="install-bar">
        <span class="prompt">$</span>
        <span id="cmd-install">curl -fsSL https://cfire.dev/install.sh | sh &amp;&amp; cfire run hello.cf</span>
        <button class="copy-btn" onclick="copyInstallCmd()">Copy</button>
      </div>
    </div>
  </section>

  <!-- REPRODUCIBLE_METRICS -->

  <!-- Value Pillars -->
  <h2 class="section-title">Why Systems Developers Choose cfire</h2>
  <p class="section-subtitle">A language designed without compromise: native C++23 ABI fidelity, strict intent safety, and rapid development speed.</p>

  <div class="grid-3">
    <div class="card">
      <div class="card-icon">&#9889;</div>
      <h3>1. Zero-Wrapper ISO C++23 ABI</h3>
      <p>Every <code>type S struct</code> is a direct C++23 aggregate. <code>str</code> lowers to <code>std::string</code>, <code>[]T</code> to <code>std::span&lt;const T&gt;</code>, and <code>T!</code> to <code>std::expected&lt;T, cfire::Error&gt;</code>. Inspect the generated source with <code>cfire emit-cpp</code> at any time.</p>
    </div>
    <div class="card">
      <div class="card-icon">&#128737;&#65039;</div>
      <h3>2. Call-Site Intent &amp; Alias Safety</h3>
      <p>Parameters default to immutable borrows (<code>in</code>). Mutating (<code>mut</code>), output (<code>out</code>), and consuming (<code>move</code>) semantics require explicit markers at both definition and call site. The compiler enforces <code>MUT-ALIAS</code> exclusivity and <code>USE-AFTER-MOVE</code> tracking.</p>
    </div>
    <div class="card">
      <div class="card-icon">&#128268;</div>
      <h3>3. Seamless Clang C/C++ Ingestion</h3>
      <p>Ingest C and C++ headers directly via <code>import cpp "header.h"</code>. Clang's AST bridge automatically maps types and coerces strings and slices. Call Raylib, SQLite, OpenSSL, or custom headers with zero manual FFI glue.</p>
    </div>
    <div class="card">
      <div class="card-icon">&#128230;</div>
      <h3>4. 13 Built-In Production Modules</h3>
      <p>Batteries included from day one: RFC 6455 WebSockets, HTTP client/server, SQLite, Zstandard compression, AES-256 GCM AEAD, HMAC-SHA256, cancellation Contexts, SIMD JSON, UTF-8 Unicode, and CLI struct tag parsers.</p>
    </div>
    <div class="card">
      <div class="card-icon">&#128038;</div>
      <h3>5. Clean Go-Like Ergonomics</h3>
      <p>Short declarations (<code>:=</code>), multi-return tuples (<code>(x, y) := f()</code>), slice destructuring (<code>[first, ..rest]</code>), <code>defer</code> resource cleanup, and clean error propagation (<code>try</code> and <code>or</code>) make writing systems code a genuine joy.</p>
    </div>
    <div class="card">
      <div class="card-icon">&#128640;</div>
      <h3>6. Complete Toolchain + CMake Eject</h3>
      <p>Includes built-in <code>run</code>, <code>build</code>, <code>test</code>, <code>fmt</code>, <code>check</code>, <code>lsp</code> (Language Server Protocol), <code>doc</code>, and <code>cfire eject</code> to export a standalone C++23 project with <code>CMakeLists.txt</code> and zero lock-in.</p>
    </div>
  </div>

  <!-- REPRODUCIBLE_ROSETTA_SHOWCASE -->

  <!-- Comparison Matrix -->
  <h2 class="section-title">How cfire Compares</h2>
  <p class="section-subtitle">Where cfire fits in the modern systems programming landscape.</p>

  <table class="comp-table">
    <thead>
      <tr>
        <th>Dimension</th>
        <th>C++23 (Raw)</th>
        <th>Rust</th>
        <th>Go</th>
        <th>cfire (Sapphire on Fire)</th>
      </tr>
    </thead>
    <tbody>
      <tr>
        <td><strong>Syntax &amp; Ergonomics</strong></td>
        <td>Complex, verbose, 40 years of legacy baggage.</td>
        <td>Steep learning curve, explicit lifetime annotations.</td>
        <td>Clean, rapid 10-minute onboarding.</td>
        <td><strong>Go-clean syntax</strong> with pattern matching, tuples, and defer.</td>
      </tr>
      <tr>
        <td><strong>Runtime &amp; GC</strong></td>
        <td>Zero GC, manual or RAII memory management.</td>
        <td>Zero GC, compile-time borrow checker.</td>
        <td>Mandatory ~20MB garbage-collected runtime.</td>
        <td><strong>Zero GC, zero wrapper runtime</strong>, pure C++23 RAII.</td>
      </tr>
      <tr>
        <td><strong>Intent &amp; Mutation Safety</strong></td>
        <td>Default const ref, easy dangling references.</td>
        <td>Strict borrow checker, exclusive <code>&amp;mut</code>.</td>
        <td>Pointers allow unrestricted aliased mutations.</td>
        <td><strong>Call-site intent markers</strong> (<code>mut</code>, <code>move</code>, <code>out</code>) + alias exclusivity.</td>
      </tr>
      <tr>
        <td><strong>C &amp; C++ Interop</strong></td>
        <td>Native, but complex build &amp; header dependencies.</td>
        <td>Requires <code>unsafe</code> C FFI or bindgen/cxx wrappers.</td>
        <td>Cgo overhead, difficult direct C++ integration.</td>
        <td><strong>Direct Clang AST ingestion</strong> + 1-to-1 ISO C++23 ABI.</td>
      </tr>
      <tr>
        <td><strong>Stdlib Batteries</strong></td>
        <td>STL provided; networking, JSON, SQLite require 3rd party libs.</td>
        <td>Minimal stdlib; relies on external crates.</td>
        <td>Rich built-in stdlib (HTTP, crypto, json).</td>
        <td><strong>13 built-in <code>core:*</code> modules</strong> (WebSockets, HTTP, SQLite, Zstd, AES).</td>
      </tr>
      <tr>
        <td><strong>Project Ejection</strong></td>
        <td>N/A</td>
        <td>N/A (locked into Cargo toolchain)</td>
        <td>N/A</td>
        <td><strong><code>cfire eject</code></strong> exports clean C++23 + standard CMake.</td>
      </tr>
    </tbody>
  </table>

  <!-- Quickstart in 3 Steps -->
  <h2 class="section-title">Get Started in 3 Steps</h2>
  <p class="section-subtitle">From zero to running your first high-performance systems service in under 60 seconds.</p>

  <div class="step-grid">
    <div class="step-card">
      <span class="step-badge">STEP 1</span>
      <h4>Build or Install</h4>
      <p>Clone and compile the pure C++23 compiler in seconds with zero external dependencies:</p>
      <pre><code>git clone https://github.com/cfire-lang/cfire
cd cfire &amp;&amp; make</code></pre>
    </div>
    <div class="step-card">
      <span class="step-badge">STEP 2</span>
      <h4>Write `main.cf`</h4>
      <p>Create a clean, idiomatic service file with built-in stdlib:</p>
      <pre><code>package main

import "core:time"

fn main() {
    now := time.now()
    println("Hello from cfire! &#128640;")
    println("Time: {}", now.format_rfc3339())
}</code></pre>
    </div>
    <div class="step-card">
      <span class="step-badge">STEP 3</span>
      <h4>Run, Build or Eject</h4>
      <p>Execute immediately, compile a binary, or eject to CMake:</p>
      <pre><code>./cfire run main.cf
./cfire build main.cf -o my_service
./cfire eject main.cf ./standalone_pkg</code></pre>
    </div>
  </div>

  <!-- Documentation & Learning Hub -->
  <h2 class="section-title">Documentation &amp; Starting Guide</h2>
  <p class="section-subtitle">Comprehensive guides, language references, and standard library catalogs.</p>

  <div class="roadmap-grid">
    <a class="roadmap-card" href="guide.html#chapter-1">
      <div class="roadmap-num">Chapter 1</div>
      <h4>Philosophy &amp; Architecture</h4>
      <p>1:1 ISO C++23 lowering, mechanical sympathy, language comparison, and 5-minute quickstart.</p>
    </a>
    <a class="roadmap-card" href="guide.html#chapter-2">
      <div class="roadmap-num">Chapter 2</div>
      <h4>Tour of the Toolchain</h4>
      <p>CLI driver commands (<code>build</code>, <code>run</code>, <code>test</code>, <code>doc</code>, <code>fmt</code>), and embedded unit testing.</p>
    </a>
    <a class="roadmap-card" href="guide.html#chapter-3">
      <div class="roadmap-num">Chapter 3</div>
      <h4>Syntax &amp; Control Flow</h4>
      <p>Immutability by default, slices, arrays, maps, tuples, optionals, and smart pointers.</p>
    </a>
    <a class="roadmap-card" href="guide.html#chapter-4">
      <div class="roadmap-num">Chapter 4</div>
      <h4>Functions &amp; Intents</h4>
      <p>Call-site intent safety (<code>in</code>, <code>mut</code>, <code>out</code>, <code>move</code>), alias exclusivity, and closures.</p>
    </a>
    <a class="roadmap-card" href="guide.html#chapter-5">
      <div class="roadmap-num">Chapter 5</div>
      <h4>Structs, Tags &amp; Enums</h4>
      <p>Compile-time struct tags, algebraic enums (tagged unions), and slice destructuring pattern matching.</p>
    </a>
    <a class="roadmap-card" href="guide.html#chapter-6">
      <div class="roadmap-num">Chapter 6</div>
      <h4>Error Handling &amp; Defer</h4>
      <p>Fallible return types (<code>T!</code>), early propagation (<code>try</code>), inline fallbacks, and <code>defer</code>.</p>
    </a>
    <a class="roadmap-card" href="guide.html#chapter-7">
      <div class="roadmap-num">Chapter 7</div>
      <h4>Concurrency &amp; Contexts</h4>
      <p>Structured concurrency with <code>async.scope</code>, typed channels, and cancellation contexts.</p>
    </a>
    <a class="roadmap-card" href="guide.html#chapter-8">
      <div class="roadmap-num">Chapter 8</div>
      <h4>Zero-Friction C &amp; C++ Interop</h4>
      <p>Direct Clang header ingestion, automatic string coercion, and inline <code>cpp { ... }</code> escape hatch.</p>
    </a>
    <a class="roadmap-card" href="guide.html#chapter-9">
      <div class="roadmap-num">Chapter 9</div>
      <h4>Build System &amp; Eject</h4>
      <p>Declarative <code>build.cf</code>, vendor subprojects, and zero-lock-in ISO C++23 CMake ejection.</p>
    </a>
    <a class="roadmap-card" href="guide.html#chapter-10">
      <div class="roadmap-num">Chapter 10</div>
      <h4>HTTP Servers &amp; Routers</h4>
      <p>Native HTTP/1.1 wire parser, fluent response builder, path parameters (<code>:id</code>), and independent routing.</p>
    </a>
  </div>

  <div style="text-align: center; margin-top: 2rem;">
    <a class="btn btn-primary" href="guide.html">&#128214; Read the Full Language Guide &rarr;</a>
    <a class="btn btn-secondary" href="reference.html" style="margin-left: 1rem;">&#128218; Stdlib &amp; Diagnostics Reference</a>
  </div>
</div>

<!-- Footer -->
<footer class="footer">
  <div>
    <img src="assets/cfire_logo_horizontal.png" alt="cfire logo horizontal" class="footer-logo" />
  </div>
  <div class="footer-links">
    <a href="index.html">Overview</a>
    <a href="guide.html">Language Guide</a>
    <a href="guide.html#chapter-1">Quickstart</a>
    <a href="reference.html">Stdlib Catalog</a>
    <a href="reference.html#diagnostics">Compiler Diagnostics</a>
    <a href="https://github.com/cfire-lang/cfire">GitHub</a>
  </div>
  <p>cfire &bull; The Sapphire on Fire Programming Language &bull; ISO C++23 Pure Native Architecture</p>
  <p style="font-size: 0.8rem; margin-top: 0.5rem; opacity: 0.7;">Single-source documentation automatically kept in sync via <code>cfire doc</code>.</p>
</footer>

<script>
function switchBrandEdition(edition, btn) {
  const img = document.getElementById('hero-logo-img');
  document.querySelectorAll('.logo-switch-btn').forEach(b => b.classList.remove('active'));
  btn.classList.add('active');
  if (edition === 'badge_cfire') {
    img.src = 'assets/cfire_logo_badge_cfire.png';
  } else if (edition === 'badge_fire') {
    img.src = 'assets/cfire_logo_badge_fire.png';
  } else if (edition === 'horizontal') {
    img.src = 'assets/cfire_logo_horizontal.png';
  }
}

function showTab(id, btn) {
  document.querySelectorAll('.tab-panel').forEach(p => p.classList.remove('active'));
  document.querySelectorAll('.tab-btn').forEach(b => b.classList.remove('active'));
  document.getElementById(id).classList.add('active');
  btn.classList.add('active');
}

function copyInstallCmd() {
  const text = document.getElementById('cmd-install').innerText;
  navigator.clipboard.writeText(text).then(() => {
    const btn = document.querySelector('.copy-btn');
    btn.innerText = 'Copied!';
    setTimeout(() => { btn.innerText = 'Copy'; }, 2000);
  });
}
</script>
</body>
</html>
)HTML";
    }

    std::string metrics = generate_reproducible_metrics();
    std::string showcase = generate_reproducible_showcase();

    auto replace_marker = [](std::string& src, std::string_view marker, std::string_view replacement) {
        auto pos = src.find(marker);
        if (pos != std::string::npos) {
            src.replace(pos, marker.size(), replacement);
        }
    };

    replace_marker(tmpl, "<!-- REPRODUCIBLE_METRICS -->", metrics);
    replace_marker(tmpl, "<!-- REPRODUCIBLE_ROSETTA_SHOWCASE -->", showcase);

    return tmpl;
}

[[nodiscard]] inline auto generate_guide_markdown() -> std::string {
    return R"MD(# The `cfire` Language & Systems Programming Guide

> **Sapphire on Fire (`cfire`)** is an ergonomic, memory-safe systems programming language that lowers 1-to-1 into modern **ISO C++23**. It combines the clean syntax and rapid onboarding of Go, the batteries-included `core:*` catalog of Odin, and the explicit mutation and move safety of Rust—with zero garbage collection, zero runtime lock-in, and full mechanical sympathy.

---

## Chapter 1: Philosophy & Architecture

### 1.1 The Systems Programming Landscape
Modern systems software demands both high developer velocity and uncompromising runtime performance. For decades, engineers have navigated difficult compromises:
- **C++** offers unmatched performance, vast ecosystem reach, and zero-cost abstractions, but suffers from forty years of accumulated syntactic complexity, implicit mutable references, and silent use-after-move bugs.
- **Rust** provides ironclad memory safety guarantees via compile-time lifetime analysis (`'a`), but introduces steep cognitive overhead and challenging FFI barriers when integrating with existing C++ codebases.
- **Go** offers outstanding developer ergonomics and networking libraries, but mandates a garbage collector and non-deterministic pause times unsuitable for real-time, game engine, or embedded kernels.
- **Zig and Odin** provide brilliant C replacements with manual allocators, but cannot directly consume C++ templates, classes, or standard library types without brittle C ABI wrappers.

`cfire` takes a pragmatic path designed specifically for teams whose target environment is **ISO C++23**:
1. **Ergonomic, Modern Frontend**: Clean syntax, immutability by default, explicit parameter intents (`in`, `mut`, `move`), multi-return tuples, algebraic enums, and monadic error handling (`T!`).
2. **1-to-1 ISO C++23 Lowering**: Every `cfire` type, function, and expression lowers directly to standard C++23 concepts (`std::string_view`, `std::span`, `std::variant`, `std::expected`, `std::unique_ptr`).
3. **Zero FFI Marshaling**: Call any C or C++ header directly without writing manual wrappers.
4. **Zero Lock-In**: Run `cfire eject` at any time to export your entire project as standard C++23 source code with a standard `CMakeLists.txt`.

### 1.2 Predictable 1:1 Lowering Table

The following table summarizes how core `cfire` types lower into standard ISO C++23:

| `cfire` Type | ISO C++23 Lowering | Ownership & Semantics |
|---|---|---|
| `i8`, `i16`, `i32`, `i64` | `int8_t`, `int16_t`, `int32_t`, `int64_t` | Signed fixed-width integers |
| `u8`, `u16`, `u32`, `u64` | `uint8_t`, `uint16_t`, `uint32_t`, `uint64_t` | Unsigned fixed-width integers |
| `f32`, `f64` | `float`, `double` | IEEE 754 floating point |
| `bool`, `char` | `bool`, `char` | Boolean flag, ASCII/UTF-8 byte |
| `str` (value) | `std::string` | Owned, heap-backed UTF-8 string |
| `str` (`in` param) | `std::string_view` | Non-allocating read-only string view |
| `path` | `std::filesystem::path` | Platform-native filesystem path |
| `[N]T` | `std::array<T, N>` | Contiguous stack array, zero overhead |
| `[]T` (value) | `std::vector<T>` | Owned, dynamic contiguous buffer |
| `[]T` (`in` param) | `std::span<const T>` | Non-allocating contiguous slice borrow |
| `map[K]V` | `std::unordered_map<K, V>` | Hash table with O(1) average lookup |
| `(T1, T2)` | `std::tuple<T1, T2>` | Heterogeneous value tuple |
| `T?` | `std::optional<T>` | Nullable value container |
| `T!` | `std::expected<T, cfire::Error>` | Value-or-error monadic result |
| `own[T]` | `std::unique_ptr<T>` | Single heap ownership with RAII cleanup |
| `shared[T]` | `std::shared_ptr<T>` | Atomic reference-counted shared ownership |
| `type S struct` | `struct S { ... }` | Standard C++ aggregate struct |
| `type E enum` | `std::variant<...>` | Tagged union with exhaustive `match` |

### 1.3 Hello, Systems! (A First Look)

Here is a complete, working `cfire` program showing basic syntax, imports, error handling, and deterministic deferral:

```cf
package main

import "core:fs"
import "core:time"
import "core:str"

fn main() {
    t0 := time.now_ms()

    // Immutability by default: 'msg' cannot be reassigned
    msg := "Hello from Sapphire on Fire (cfire)!"
    println("{}", msg)

    // Fallible file operation with early-exit 'try'
    try fs.write_text("build_info.txt", "runtime=cfire\nstandard=c++23\n")
    
    // Deterministic scope cleanup: runs when main exits
    defer {
        println("Cleanup: execution finished in {} ms", time.now_ms() - t0)
    }

    content := try fs.read_text("build_info.txt")
    lines := str.split(content, "\n")
    for idx, line in lines {
        if line.len() > 0 {
            println("  [{}] {}", idx + 1, line)
        }
    }
}
```

---

## Chapter 2: Tour of the Toolchain

`cfire` ships as a single, self-contained binary. There are no secondary package managers, formatters, or test runners to install.

### 2.1 Toolchain Subcommands Reference

| Subcommand | Usage | Purpose & Details |
|---|---|---|
| `run` | `cfire run main.cf` | Compiles and executes the program. Auto-links required `core:*` and C/C++ libraries. |
| `run --sanitize` | `cfire run --sanitize main.cf` | Enables Clang AddressSanitizer and UndefinedBehaviorSanitizer (`-fsanitize=address,undefined`). |
| `build` | `cfire build main.cf -o app` | Produces an optimized native executable (`-O3`) in the current directory. |
| `test` | `cfire test main.cf` | Discovers and executes all embedded `test "..." { ... }` suites in parallel. |
| `test --sanitize` | `cfire test --sanitize main.cf` | Executes all unit tests with ASan and UBSan active to catch memory leaks and bounds errors. |
| `fmt` | `cfire fmt src/` | Canonically formats `.cf` files in-place according to strict formatting rules. |
| `fmt --check` | `cfire fmt --check src/` | CI validation mode. Exits with non-zero status if any file is unformatted. |
| `check` | `cfire check main.cf` | Fast frontend validation: parses and runs semantic analysis without invoking the C++ backend. |
| `eval` | `cfire eval '2 + 2'` | Evaluates a single `cfire` expression or statement directly from the command line. |
| `lsp` | `cfire lsp` | Starts the standard Language Server Protocol daemon for VSCode, Neovim, and Helix. |
| `doc` | `cfire doc` | Generates verified HTML and Markdown documentation from the compiler catalog. |
| `doc --check` | `cfire doc --check` | Verifies that documentation files match the compiler catalog character-for-character. |
| `eject` | `cfire eject main.cf ./export` | Exports the program and all dependencies into standalone ISO C++23 files + `CMakeLists.txt`. |

### 2.2 Embedded Unit Testing

`cfire` includes testing as a core language primitive. You can write isolated test suites directly inside any `.cf` file:

```cf
package math_util

fn add(a: i64, b: i64) -> i64 {
    return a + b
}

fn clamp(val: f64, low: f64, high: f64) -> f64 {
    if val < low { return low }
    if val > high { return high }
    return val
}

test "integer arithmetic addition" {
    assert(add(10, 20) == 30)
    assert(add(-5, 5) == 0)
}

test "floating-point clamp bounds" {
    assert(clamp(5.0, 0.0, 10.0) == 5.0)
    assert(clamp(-2.0, 0.0, 10.0) == 0.0)
    assert(clamp(15.0, 0.0, 10.0) == 10.0)
}
```

Run tests with a single command:
```zsh
./cfire test math_util.cf
```

---

## Chapter 3: Syntax, Primitives, Variables & Control Flow

### 3.1 Immutability by Default
In `cfire`, bindings declared with `:=` or explicit type annotations without `mut` are strictly **immutable (`const`)**. Attempting to reassign an immutable binding triggers a compiler error:

```cf
limit := 100
// limit = 200 // ERROR: cannot assign to immutable binding 'limit'

mut counter := 0 // Mutable binding
counter += 1     // OK
```

Explicit type declarations follow the `name: Type = value` syntax:
```cf
max_payload: u64 = 1048576
mut buffer_size: i32 = 4096
```

### 3.2 Slices (`[]T`) vs Fixed Arrays (`[N]T`)
`cfire` makes a sharp distinction between stack-allocated fixed arrays and dynamically sized slices:
- **Fixed Arrays (`[N]T`)**: Size is known at compile time, memory is allocated on the stack with zero overhead, lowering to `std::array<T, N>`.
- **Dynamic Slices (`[]T`)**: Growable dynamic buffers backed by the heap, lowering to `std::vector<T>` when owned, or `std::span<const T>` when borrowed as an `in` parameter.

```cf
// Fixed array (std::array<int32_t, 4>)
fixed: [4]i32 = [10, 20, 30, 40]
println("fixed length: {}", fixed.len())

// Dynamic slice (std::vector<int32_t>)
mut dynamic: []i32 = [1, 2, 3]
dynamic.push(4)
dynamic.push(5)
println("dynamic length: {}", dynamic.len())

// Slicing views (std::span)
sub := dynamic[1:3] // View of elements at index 1 and 2
```

### 3.3 Hash Maps (`map[K]V`)
Hash maps provide $O(1)$ key-value lookups backed by `std::unordered_map`:

```cf
mut scores: map[str]i64 = {}
scores["Alice"] = 95
scores["Bob"] = 88

// Safe retrieval with 'or' fallback
alice_score := scores["Alice"] or 0
charlie_score := scores["Charlie"] or -1

println("Alice: {}, Charlie: {}", alice_score, charlie_score)

// Check existence
if scores.contains("Alice") {
    println("Alice is enrolled")
}
```

### 3.4 Tuples & Destructuring
Tuples group multiple values together without declaring a formal struct:

```cf
// Constructing a tuple
coord := (12.5, 45.8, "GPS-Locked")

// Destructuring into individual bindings
(lat, lon, status) := coord
println("Lat: {}, Lon: {}, Status: {}", lat, lon, status)
```

### 3.5 Optionals (`T?`)
Values that may be absent are wrapped in `T?`, lowering directly to `std::optional<T>`:

```cf
fn find_user(id: i64) -> str? {
    if id == 42 {
        return "Alice"
    }
    return null
}

user := find_user(42) or "Guest"
println("User: {}", user)
```

### 3.6 Smart Pointers (`own[T]` and `shared[T]`)
Value semantics and slices cover most systems programming needs. For recursive data structures or explicit heap allocations, `cfire` provides `own[T]` (`std::unique_ptr<T>`) and `shared[T]` (`std::shared_ptr<T>`):

```cf
type Node struct {
    Value: i64,
    Next:  own[Node]?,
}

// Allocate an owned heap box
first := make(own[Node], Node{Value: 10, Next: null})
println("Node value: {}", (*first).Value)
```

### 3.7 Control Flow (`if`, `for`, `match`)

```cf
// 1. Conditional branching
if score >= 90 {
    println("Grade: A")
} else if score >= 80 {
    println("Grade: B")
} else {
    println("Grade: C")
}

// 2. Iterating elements
items := ["alpha", "beta", "gamma"]
for item in items {
    println("Item: {}", item)
}

// 3. Iterating with index
for idx, item in items {
    println("[{}] = {}", idx, item)
}

// 4. Iterating map key-value pairs
for key, val in scores {
    println("{} => {}", key, val)
}

// 5. C-style numeric loop
for mut i := 0; i < 5; i += 1 {
    if i == 2 { continue }
    println("i = {}", i)
}
```

---

## Chapter 4: Functions & Call-Site Intent Safety

### 4.1 Function Declarations and Methods
Functions are declared with `fn`. Associated methods specify a receiver before the function name:

```cf
type Account struct {
    Owner:   str,
    Balance: i64,
}

// Function with regular parameters
fn calculate_fee(amount: i64) -> i64 {
    return amount / 100
}

// Read-only receiver method
fn (self: Account) display() {
    println("Account {}: ${}", self.Owner, self.Balance)
}

// Mutable receiver method: requires 'mut self'
fn (mut self: Account) deposit(amount: i64) {
    self.Balance += amount
}
```

### 4.2 Explicit Parameter Intents (`in`, `mut`, `move`)
In raw C++, calling `process(data)` hides whether `data` is read-only, modified in-place, or moved from. `cfire` enforces visibility at **both** declaration and call site:

| Intent | Declaration | Call Site | C++23 Lowering | Guarantee |
|---|---|---|---|---|
| **`in` (default)** | `x: T` | `f(x)` | `const T&` / `std::string_view` / `std::span` | Read-only borrow; zero copy overhead. |
| **`mut`** | `mut x: T` | `f(mut x)` | `T&` | In-place mutation; requires `mut` at call site. |
| **`move`** | `move x: T` | `f(move x)` | `T&&` + `std::move(x)` | Ownership transfer; callee consumes value. |

To return multiple values, `cfire` uses first-class tuples rather than mutable output parameters:
```cf
fn parse_coords(raw: str) -> (f64, f64)! {
    return (12.5, 45.2)
}

(lat, lon) := try parse_coords("12.5,45.2")
```

### 4.3 Semantic Safety Invariants

The `cfire` semantic analyzer verifies parameter intents at compile time:

#### 1. `cfire/MUT-CALLSITE`
Passing a variable to a `mut` parameter without the `mut` marker at the call site is rejected:
```cf
fn increment(mut val: i32) { val += 1 }

mut count := 0
// increment(count)     // ERROR: [cfire/MUT-CALLSITE] missing 'mut' keyword
increment(mut count)    // OK
```

#### 2. `cfire/IMMUTABLE-MUT-ARG`
Passing an immutable binding to a `mut` parameter is rejected:
```cf
val := 42
// increment(mut val)   // ERROR: [cfire/IMMUTABLE-MUT-ARG] 'val' is immutable
```

#### 3. `cfire/MUT-ALIAS` (The Law of Exclusivity)
A variable cannot be passed as a mutable argument while simultaneously aliased as another argument in the same call:
```cf
fn transfer(mut from: i64, mut to: i64, amount: i64) {
    from -= amount
    to += amount
}

mut account_a := 1000
// transfer(mut account_a, mut account_a, 100) // ERROR: [cfire/MUT-ALIAS]
mut account_b := 500
transfer(mut account_a, mut account_b, 100)    // OK: distinct bindings
```

#### 4. `cfire/USE-AFTER-MOVE`
Referencing a variable after it has been transferred via `move` is rejected:
```cf
fn sink(move data: []byte) { /* takes ownership */ }

mut payload: []byte = [1, 2, 3]
sink(move payload)
// println("len = {}", payload.len()) // ERROR: [cfire/USE-AFTER-MOVE]
```

### 4.4 Closures & Lambdas
Closures use pipe syntax `|params| -> ReturnType { ... }`:

```cf
items := [1, 2, 3, 4, 5]

// Inline closure passed to higher-order transform
multiplier := 10
transform := |x: i32| -> i32 {
    return x * multiplier
}

for item in items {
    println("transformed: {}", transform(item))
}
```

---

## Chapter 5: Structs, Compile-Time Tags & Algebraic Enums

### 5.1 Structs & Compile-Time Field Metadata
Structs define composite data records. Field tags (enclosed in backticks) provide zero-cost metadata inspected by the compiler during code generation:

```cf
type DatabaseConfig struct {
    Host:     str  `json:"host"     cli:"--host,-h"    sql:"host"`
    Port:     u16  `json:"port"     cli:"--port,-p"    sql:"port"`
    MaxConns: i32  `json:"max_conns" cli:"--max-conns" sql:"max_connections"`
    SSL:      bool `json:"ssl"      cli:"--ssl"        sql:"use_ssl"`
}
```

Because these tags are processed at compile time, `core:json`, `core:cli`, and `core:db/sqlite` generate direct C++ assignments without runtime reflection overhead:

```cf
import "core:json"
import "core:cli"

fn main() {
    // 1. Automatic CLI flag parsing directly into typed struct
    cfg := try cli.parse[DatabaseConfig]()
    println("Config loaded: host={}, port={}", cfg.Host, cfg.Port)

    // 2. Automatic JSON serialization
    json_str := try json.stringify(cfg)
    println("Serialized: {}", json_str)
}
```

### 5.2 Algebraic Enums (Tagged Unions)
Algebraic enums represent data that can take one of several distinct variants, each carrying its own typed payload:

```cf
type NetworkEvent enum {
    Connected(peer_ip: str, port: u16),
    DataReceived(payload: []byte),
    Disconnected(reason: str),
    Heartbeat,
}
```

This lowers to `std::variant<Connected, DataReceived, Disconnected, Heartbeat>` in ISO C++23.

### 5.3 Exhaustive Pattern Matching (`match`)
Pattern matching inspects algebraic enums and binds variant payloads. The compiler enforces exhaustiveness (`cfire/NON-EXHAUSTIVE-MATCH`):

```cf
fn handle_event(event: NetworkEvent) {
    match event {
        Connected(ip, port) => {
            println("Peer connected: {}:{}", ip, port)
        }
        DataReceived(data) => {
            println("Received payload of {} bytes", data.len())
        }
        Disconnected(reason) => {
            println("Connection closed: {}", reason)
        }
        Heartbeat => {
            // Keep-alive ack
        }
    }
}
```

If a new variant is added to `NetworkEvent`, every `match` statement across your codebase that lacks a wildcard `_ =>` arm will fail compilation at build time.

### 5.4 Slice Destructuring Matching
`match` can also destructure slices into head and tail elements:

```cf
fn summarize_tokens(tokens: []str) {
    match tokens {
        [] => {
            println("No tokens found")
        }
        [command] => {
            println("Single command: {}", command)
        }
        [cmd, ..args] => {
            println("Command: {}, Argument count: {}", cmd, args.len())
        }
    }
}
```

---

## Chapter 6: Fearless Error Handling & Cleanup

### 6.1 The Fallible Return Type `T!`
`cfire` rejects hidden C++ exceptions. Functions that can fail return `T!`, which lowers directly to `std::expected<T, cfire::Error>`. Errors are concrete value objects carrying a code, an error message, and source context:

```cf
import "core:fs"

fn load_api_key(path: str) -> str! {
    if !fs.exists(path) {
        return cfire.error("API key file not found: {}", path)
    }
    raw := try fs.read_text(path)
    return str.trim(raw)
}
```

### 6.2 The Three Ways to Handle Errors

#### 1. Early Return Propagation (`try`)
The `try` operator unwraps the success value or immediately returns the error from the enclosing function:
```cf
fn setup_system() -> void! {
    cfg := try load_api_key("api.key")
    println("Key loaded: {} chars", cfg.len())
}
```
If used inside `fn main()`, an unhandled error printed via `try` outputs a formatted diagnostic and exits the process with status code `1`.

#### 2. Default Fallbacks (`or value`)
When a reasonable default exists, use `or fallback` (lowers to `std::expected::value_or`):
```cf
port := try parse_port(os.getenv("PORT")) or 8080
println("Binding to port {}", port)
```

#### 3. Inline Recovery Blocks (`or err { ... }`)
When you need to log an error, clean up local state, or alter control flow, use an `or err { ... }` block. To prevent unwrapping an uninitialized value, the compiler enforces that value-binding recovery blocks must definitely diverge (`cfire/NON-DIVERGING-OR-BLOCK`):

```cf
token := load_api_key("secret.key") or err {
    println("[WARN] Failed to load key: {}. Falling back to sandbox mode.", err.message())
    return // Diverges cleanly
}
```

### 6.3 Deterministic Scope Cleanup (`defer`)
The `defer` statement registers an action to be executed in Last-In, First-Out (LIFO) order when the surrounding scope exits. It is implemented via RAII scope guards, guaranteeing execution regardless of whether the scope exits via normal return, `try` error propagation, or a `break` statement:

```cf
import "core:fs"
import "core:sync"

mut mtx: sync.Mutex = {}

fn process_transaction() -> void! {
    mtx.lock()
    defer mtx.unlock() // Guaranteed to unlock even on error return

    mut file := try fs.open("audit.log", fs.OpenMode::Append)
    defer file.close() // File descriptor closed cleanly

    try file.write_str("Transaction started\n")
    // If an error happens below, both file.close() and mtx.unlock() execute deterministically
    try execute_db_write()
    try file.write_str("Transaction committed\n")
}
```

---

## Chapter 7: Structured Concurrency & Contexts

### 7.1 Structured Concurrency with `async.scope`
Unlike raw threads or Go's unconstrained `go func()`, `cfire` uses structured task pools. An `async.scope` block defines a lexical boundary: the scope **cannot exit** until every spawned child task has completed:

```cf
import "core:time"

fn process_batch(items: []str) {
    // Spawns a pool of 4 worker threads
    async.scope(workers: 4) |scope| {
        for idx, item in items {
            scope.spawn || {
                println("Worker processing item [{}]: {}", idx, item)
                time.sleep_ms(50)
            }
        }
    }
    // All 4 worker tasks are 100% joined before this line executes!
    println("All batch items successfully processed.")
}
```

Structured concurrency eliminates orphaned background threads, use-after-free references to stack frames, and race conditions during application shutdown.

### 7.2 Message Passing via Typed Channels (`core:sync`)
For decoupled producer-consumer pipelines, `core:sync` provides Go-style typed channels:

```cf
import "core:sync"

fn main() {
    ch := sync.make_channel[i64](capacity: 10)

    async.scope(workers: 2) |scope| {
        // Producer task
        scope.spawn || {
            for mut i := 1; i <= 5; i += 1 {
                ch.send(i * 10)
            }
            ch.close()
        }

        // Consumer task
        scope.spawn || {
            for {
                val := ch.recv() or break // Exits loop when channel is closed and empty
                println("Received: {}", val)
            }
        }
    }
}
```

### 7.3 Cancellation Contexts & Deadlines (`core:sync/context`)
Long-running background operations support cooperative cancellation via `Context`:

```cf
import "core:sync/context"
import "core:time"

fn worker(ctx: context.Context, id: i32) {
    for {
        if ctx.is_cancelled() {
            println("Worker [{}] received cancellation notice", id)
            return
        }
        time.sleep_ms(100)
    }
}

fn main() {
    // Derive a context that cancels automatically after 250 milliseconds
    (ctx, cancel) := context.with_timeout(context.background(), 250)
    defer cancel()

    async.scope(workers: 2) |scope| {
        scope.spawn || { worker(ctx, 1) }
        scope.spawn || { worker(ctx, 2) }
    }
    println("Workers shut down cleanly.")
}
```

---

## Chapter 8: Zero-Friction C & C++ Interop

### 8.1 Direct C Header Ingestion
`cfire` compiles natively to C++23, which means communicating with C libraries requires no manual binding generation. When you import a C header with `import "c:..."`, the compiler automatically invokes Clang to extract type definitions and function prototypes, caching the result in `.cfire/cache/ast/`:

```cf
package main

// Directly imports the system zlib header
import "c:zlib.h" as zlib

fn main() {
    version_str := zlib.zlibVersion()
    println("Zlib library version: {}", version_str)
}
```

### 8.2 Automatic String Boundary Coercion
When calling a C function expecting a null-terminated `const char*`, `cfire` automatically inserts `cfire::c_str(arg)` at compile time. You can pass a native `cfire` `str` directly without handwritten conversion:

```cf
import "c:stdio.h" as c_io

fn main() {
    filename := "output.txt"
    mode := "w"
    
    // Automatically coerces 'filename' and 'mode' to const char*
    mut f := c_io.fopen(filename, mode)
    if f != null {
        c_io.fputs("Written via direct C interop\n", f)
        c_io.fclose(f)
    }
}
```

### 8.3 Direct C++ Header Imports
You can import modern C++ standard and third-party headers directly:

```cf
import "cpp:vector"
import "cpp:algorithm"

fn main() {
    // Direct access to C++ standard library algorithms
    mut values: []i32 = [5, 2, 8, 1, 9]
    // Under the hood, values is std::vector<int32_t>
    println("Sorted elements:")
    // ...
}
```

### 8.4 Inline C++ Escape Hatch (`cpp { ... }`)
For platform intrinsics, inline assembly, or specialized template metaprogramming, `cfire` allows raw C++ statements inside any function:

```cf
fn get_cpu_cycle_count() -> u64 {
    mut cycles: u64 = 0
    cpp {
        #if defined(__x86_64__)
        cycles = __rdtsc();
        #elif defined(__aarch64__)
        asm volatile("mrs %0, cntvct_el0" : "=r"(cycles));
        #endif
    }
    return cycles
}
```

---

## Chapter 9: The Unified Build System & CMake Ejection

### 9.1 Declarative `build.cf`
Projects with complex C/C++ static dependencies or CMake subprojects include a declarative `build.cf` file in the root directory:

```cf
package build

import "core:build"

// 1. Build a local C static library dependency
FastHash := build.CC("./native/fast_hash", {
    Sources:  ["fast_hash.c"],
    Includes: ["."],
    CFlags:   ["-O3", "-march=native"],
})

// 2. Build a CMake-based vendor subproject
KVStore := build.CMake("./vendor/libkv_cmake", {
    Target:   "kvstore",
    Includes: ["include"],
    Defines:  {"KV_SALT": 42},
})

// 3. Link a system package via pkg-config
Sqlite3 := build.SystemLib("sqlite3")
```

When you run `./cfire build main.cf`:
1. `cfire` discovers `build.cf`.
2. It compiles `FastHash` and `LibZstd` into static archives in `.cfire/cache/lib/*.a`.
3. It links all libraries together into a single, high-performance binary.

### 9.2 Zero-Lock-In CMake Ejection (`cfire eject`)
One of the most important architectural design decisions in `cfire` is that **you are never locked in**. At any point in your project lifecycle, you can eject to standard ISO C++23:

```zsh
./cfire eject src/main.cf ./standalone_project
```

This exports:
- `standalone_project/src/main.cpp`: Clean, human-readable ISO C++23 source code corresponding to your `.cf` files.
- `standalone_project/include/cfire_prelude.hpp`: The lightweight, header-only runtime prelude.
- `standalone_project/CMakeLists.txt`: A standard, production-ready CMake build file configuring all include directories, dependencies, and compilation flags.

Anyone on your team can now build your software using standard CMake and Clang without needing `cfire` installed:
```zsh
cd standalone_project
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

---

## Chapter 10: Building Web Services, HTTP Servers & Independent Routers (`core:net/http`)

Modern systems software frequently needs to expose telemetry endpoints, REST APIs, or lightweight RPC microservices. Unlike traditional frameworks that force heavy external dependencies or invasive event-loop architectures, `cfire` provides a zero-dependency, native HTTP/1.1 protocol engine and independent router built directly on top of POSIX TCP sockets and ISO C++23.

### 10.1 Philosophy & Zero External Dependencies
The `core:net/http` module is designed with two fundamental principles:
1. **Zero Third-Party Libraries:** The HTTP wire parser, response serializer, server accept loop, and route matcher are implemented in standard ISO C++23 without requiring `libmicrohttpd`, `boost::beast`, or external C libraries.
2. **Decoupled & Independent Features:** Routing and dispatch are strictly decoupled from network sockets. You can create an independent `Router`, register parameterized routes, and unit-test request dispatching purely in memory without opening sockets or listening on network ports.

### 10.2 Request Parsing & Response Writing
The `http.Request` struct represents an incoming HTTP/1.1 request:
- `req.method`: Normalized uppercase HTTP verb (`"GET"`, `"POST"`, `"PUT"`, `"DELETE"`, etc.).
- `req.path`: Clean decoded URI path (e.g. `"/api/users/42"`).
- `req.query(key, fallback)`: Decoded query parameters (e.g. `req.query("limit", "10")`).
- `req.header(name, fallback)`: Case-insensitive header lookup (e.g. `req.header("Authorization")`).
- `req.param(name, fallback)`: Named path parameters extracted by the router (e.g. `req.param("id")`).
- `req.body`: Complete payload body.

The `http.Response` struct provides fluent static constructors for standard HTTP responses:
```cf
// 200 OK with plain text
res1 := http.Response.text(200, "operation successful")

// 200 OK with JSON payload
res2 := http.Response.json(200, "{\"status\":\"ok\"}")

// 201 Created with JSON
res3 := http.Response.json(201, "{\"id\":42}")

// 302 Found redirect
res4 := http.Response.redirect("/login")

// Standard error responses
res5 := http.Response.not_found("user not found")
res6 := http.Response.bad_request("invalid payload")
```

### 10.3 The Independent HTTP Request Router
The router matches incoming requests based on path patterns and HTTP verbs:
- **Exact Path Matching:** `/health`, `/api/v1/metrics`
- **Named Path Parameters (`:param`):** `/users/:id` matches `/users/42` with `req.param("id") == "42"`.
- **Greedy Suffix Wildcards (`*filepath`):** `/static/*filepath` matches `/static/css/theme.css` with `req.param("filepath") == "css/theme.css"`.
- **Method Filtering:** `router.get`, `router.post`, `router.put`, `router.delete_`, `router.patch`, `router.all`.
- **RFC-Compliant Status Handlers:** Automatically returns `405 Method Not Allowed` with an `Allow` header if the path matches a route registered for different verbs, or `404 Not Found` if unmatched.
- **Middleware Pipeline:** `router.use(middleware)` allows adding pre-routing authentication, audit logging, or header validation.

### 10.4 In-Memory Standalone Unit Testing
Because the `Router` is completely independent of network sockets, request handlers can be tested in-memory in pure unit tests without flakiness or port collisions:

```cf
package main

import "core:net/http"

fn main() {
    mut router := http.make_router()
    router.get("/users/:id", |req| {
        return http.Response.text(200, "User ID: " + req.param("id"))
    })

    // Pure in-memory unit test — NO network sockets opened!
    req := http.Request{ method: "GET", path: "/users/100" }
    res := router.dispatch(req)
    assert(res.status_code == 200)
    assert(res.body == "User ID: 100")
}
```

### 10.5 Running the Live HTTP Server
When you are ready to serve live network traffic, instantiate `http.make_server` and pass the router directly to `server.serve_in_background(router)` or `server.listen_and_serve(router)`:

```cf
package main

import "core:net/http"
import "core:json"

type Item struct {
    id: i64 `json:"id"`
    name: str `json:"name"`
}

fn main() {
    mut router := http.make_router()

    router.get("/health", |req| {
        return http.Response.json(200, "{\"status\":\"healthy\"}")
    })

    router.get("/items/:id", |req| {
        return http.Response.text(200, "Lookup id=" + req.param("id"))
    })

    router.post("/items", |req| {
        return http.Response.json(201, "{\"created\":true,\"data\":" + req.body + "}")
    })

    // Start server on port 8080 (or 0 for an ephemeral port)
    mut server := try http.make_server(port: 8080)
    println("Server listening on http://127.0.0.1:{}", server.port())

    // Run non-blocking background event loop
    server.serve_in_background(router)

    // Execute client requests
    resp := try http.get("http://127.0.0.1:8080/health")
    println("GET /health -> status: {}, body: {}", resp.status_code, resp.body)

    // Gracefully stop the server when done
    server.stop()
}
```
)MD";
}

[[nodiscard]] inline auto generate_guide_html() -> std::string {
    std::string out;
    out += "<!DOCTYPE html>\n<html lang=\"en\">\n<head>\n";
    out += "  <meta charset=\"UTF-8\" />\n";
    out += "  <meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\" />\n";
    out += "  <title>Language Guide — cfire (Sapphire on Fire)</title>\n";
    out += "  <style>" + detail::shared_css() + "</style>\n";
    out += "</head>\n<body>\n";
    out += detail::navbar_html("guide");
    out += R"HTML(<div class="container">
  <div class="doc-layout">
    <aside class="sidebar">
      <details class="sidebar-details" open>
        <summary class="sidebar-summary">
          <span>Chapters &amp; Navigation</span>
          <span class="sidebar-chevron">&#9662;</span>
        </summary>
        <div class="sidebar-content">
          <h4>Guide Chapters</h4>
          <ul>
            <li><a href="#chapter-1">1. Philosophy &amp; Architecture</a></li>
            <li><a href="#chapter-2">2. Tour of the Toolchain</a></li>
            <li><a href="#chapter-3">3. Syntax &amp; Control Flow</a></li>
            <li><a href="#chapter-4">4. Functions &amp; Intents</a></li>
            <li><a href="#chapter-5">5. Structs, Tags &amp; Enums</a></li>
            <li><a href="#chapter-6">6. Error Handling &amp; Defer</a></li>
            <li><a href="#chapter-7">7. Concurrency &amp; Contexts</a></li>
            <li><a href="#chapter-8">8. C &amp; C++ Interop</a></li>
            <li><a href="#chapter-9">9. Build System &amp; Eject</a></li>
            <li><a href="#chapter-10">10. HTTP Servers &amp; Routers</a></li>
          </ul>
          <h4 style="margin-top: 1.25rem;">Quick Links</h4>
          <ul>
            <li><a href="reference.html">Stdlib Catalog (28 Modules) &rarr;</a></li>
            <li><a href="index.html#landscape">Systems Landscape &rarr;</a></li>
            <li><a href="index.html#benchmarks">Compiler Benchmarks &rarr;</a></li>
          </ul>
        </div>
      </details>
    </aside>
    <main>
      <h1>The <code>cfire</code> Language &amp; Systems Programming Guide</h1>
      <p class="subtitle" style="margin: 0 0 2rem;">
        An in-depth tutorial and reference manual for writing robust, high-performance systems software targeting modern ISO C++23.
      </p>

      <!-- CHAPTER 1 -->
      <h2 id="chapter-1"><a id="ch1"></a>1. Philosophy &amp; Architecture</h2>
      <p>
        Modern systems software demands both rapid developer velocity and uncompromising mechanical sympathy. For decades, developers have navigated difficult compromises:
      </p>
      <ul>
        <li><strong>C++</strong> offers unmatched performance, vast ecosystem reach, and zero-cost abstractions, but suffers from forty years of accumulated syntactic complexity, implicit mutable references, and silent use-after-move bugs.</li>
        <li><strong>Rust</strong> provides ironclad memory safety guarantees via compile-time lifetime analysis (<code>'a</code>), but introduces steep cognitive overhead and challenging FFI barriers when integrating with existing C++ codebases.</li>
        <li><strong>Go</strong> offers outstanding developer ergonomics and networking libraries, but mandates a garbage collector and non-deterministic pause times unsuitable for real-time, game engine, or embedded kernels.</li>
        <li><strong>Zig and Odin</strong> provide brilliant C replacements with manual allocators, but cannot directly consume C++ templates, classes, or standard library types without brittle C ABI wrappers.</li>
      </ul>
      <p>
        <code>cfire</code> takes a pragmatic path designed specifically for teams whose target environment is <strong>ISO C++23</strong>:
      </p>
      <ol>
        <li><strong>Ergonomic, Modern Frontend:</strong> Clean syntax, immutability by default, explicit parameter intents (<code>in</code>, <code>mut</code>, <code>move</code>), multi-return tuples, algebraic enums, and monadic error handling (<code>T!</code>).</li>
        <li><strong>1-to-1 ISO C++23 Lowering:</strong> Every <code>cfire</code> type, function, and expression lowers directly to standard C++23 concepts (<code>std::string_view</code>, <code>std::span</code>, <code>std::variant</code>, <code>std::expected</code>, <code>std::unique_ptr</code>).</li>
        <li><strong>Zero FFI Marshaling:</strong> Call any C or C++ header directly without writing manual wrappers.</li>
        <li><strong>Zero Lock-In:</strong> Run <code>cfire eject</code> at any time to export your entire project as standard C++23 source code with a standard <code>CMakeLists.txt</code>.</li>
      </ol>

      <h3>1.2 Predictable 1:1 Lowering Table</h3>
      <div class="table-wrap">
        <div class="table-scroll-cue">Scroll table &rarr;</div>
        <table>
          <thead>
            <tr><th><code>cfire</code> Type</th><th>ISO C++23 Lowering</th><th>Ownership &amp; Semantics</th></tr>
          </thead>
          <tbody>
            <tr><td><code>i8</code>, <code>i16</code>, <code>i32</code>, <code>i64</code></td><td><code>int8_t</code>, <code>int16_t</code>, <code>int32_t</code>, <code>int64_t</code></td><td>Signed fixed-width integers</td></tr>
            <tr><td><code>u8</code>, <code>u16</code>, <code>u32</code>, <code>u64</code></td><td><code>uint8_t</code>, <code>uint16_t</code>, <code>uint32_t</code>, <code>uint64_t</code></td><td>Unsigned fixed-width integers</td></tr>
            <tr><td><code>f32</code>, <code>f64</code></td><td><code>float</code>, <code>double</code></td><td>IEEE 754 floating point</td></tr>
            <tr><td><code>bool</code>, <code>char</code></td><td><code>bool</code>, <code>char</code></td><td>Boolean flag, ASCII/UTF-8 byte</td></tr>
            <tr><td><code>str</code> (value)</td><td><code>std::string</code></td><td>Owned, heap-backed UTF-8 string</td></tr>
            <tr><td><code>str</code> (<code>in</code> param)</td><td><code>std::string_view</code></td><td>Non-allocating read-only string view</td></tr>
            <tr><td><code>path</code></td><td><code>std::filesystem::path</code></td><td>Platform-native filesystem path</td></tr>
            <tr><td><code>[N]T</code></td><td><code>std::array&lt;T, N&gt;</code></td><td>Contiguous stack array, zero overhead</td></tr>
            <tr><td><code>[]T</code> (value)</td><td><code>std::vector&lt;T&gt;</code></td><td>Owned, dynamic contiguous buffer</td></tr>
            <tr><td><code>[]T</code> (<code>in</code> param)</td><td><code>std::span&lt;const T&gt;</code></td><td>Non-allocating contiguous slice borrow</td></tr>
            <tr><td><code>map[K]V</code></td><td><code>std::unordered_map&lt;K, V&gt;</code></td><td>Hash table with O(1) average lookup</td></tr>
            <tr><td><code>(T1, T2)</code></td><td><code>std::tuple&lt;T1, T2&gt;</code></td><td>Heterogeneous value tuple</td></tr>
            <tr><td><code>T?</code></td><td><code>std::optional&lt;T&gt;</code></td><td>Nullable value container</td></tr>
            <tr><td><code>T!</code></td><td><code>std::expected&lt;T, cfire::Error&gt;</code></td><td>Value-or-error monadic result</td></tr>
            <tr><td><code>own[T]</code></td><td><code>std::unique_ptr&lt;T&gt;</code></td><td>Single heap ownership with RAII cleanup</td></tr>
            <tr><td><code>shared[T]</code></td><td><code>std::shared_ptr&lt;T&gt;</code></td><td>Atomic reference-counted shared ownership</td></tr>
            <tr><td><code>type S struct</code></td><td><code>struct S { ... }</code></td><td>Standard C++ aggregate struct</td></tr>
            <tr><td><code>type E enum</code></td><td><code>std::variant&lt;...&gt;</code></td><td>Tagged union with exhaustive <code>match</code></td></tr>
          </tbody>
        </table>
      </div>

      <h3>1.3 Hello, Systems! (A First Look)</h3>
      <div class="code-header"><span class="tag-cf">cfire</span><span>examples/01-hello-core/main.cf</span></div>
      <pre><code>package main

import "core:fs"
import "core:time"
import "core:str"

fn main() {
    t0 := time.now_ms()

    // Immutability by default: 'msg' cannot be reassigned
    msg := "Hello from Sapphire on Fire (cfire)!"
    println("{}", msg)

    // Fallible file operation with early-exit 'try'
    try fs.write_text("build_info.txt", "runtime=cfire\nstandard=c++23\n")
    
    // Deterministic scope cleanup: runs when main exits
    defer {
        println("Cleanup: execution finished in {} ms", time.now_ms() - t0)
    }

    content := try fs.read_text("build_info.txt")
    lines := str.split(content, "\n")
    for idx, line in lines {
        if line.len() > 0 {
            println("  [{}] {}", idx + 1, line)
        }
    }
}</code></pre>

      <!-- CHAPTER 2 -->
      <h2 id="chapter-2"><a id="ch2"></a>2. Tour of the Toolchain</h2>
      <p>
        <code>cfire</code> ships as a single, self-contained executable. There are no secondary package managers, external formatters, or test runners to install.
      </p>

      <div class="table-wrap">
        <div class="table-scroll-cue">Scroll table &rarr;</div>
        <table>
          <thead>
            <tr><th>Subcommand</th><th>Usage</th><th>Purpose &amp; Details</th></tr>
          </thead>
          <tbody>
            <tr><td><code>run</code></td><td><code>./cfire run main.cf</code></td><td>Compiles and executes the program. Auto-links required <code>core:*</code> and C/C++ libraries.</td></tr>
            <tr><td><code>run --sanitize</code></td><td><code>./cfire run --sanitize main.cf</code></td><td>Enables Clang AddressSanitizer and UndefinedBehaviorSanitizer (<code>-fsanitize=address,undefined</code>).</td></tr>
            <tr><td><code>build</code></td><td><code>./cfire build main.cf -o app</code></td><td>Produces an optimized native executable (<code>-O3</code>) in the current directory.</td></tr>
            <tr><td><code>test</code></td><td><code>./cfire test main.cf</code></td><td>Discovers and executes all embedded <code>test "..." { ... }</code> suites in parallel.</td></tr>
            <tr><td><code>test --sanitize</code></td><td><code>./cfire test --sanitize main.cf</code></td><td>Executes all unit tests with ASan and UBSan active to catch memory leaks and bounds errors.</td></tr>
            <tr><td><code>fmt</code></td><td><code>./cfire fmt src/</code></td><td>Canonically formats <code>.cf</code> files in-place according to strict formatting rules.</td></tr>
            <tr><td><code>fmt --check</code></td><td><code>./cfire fmt --check src/</code></td><td>CI validation mode. Exits with non-zero status if any file is unformatted.</td></tr>
            <tr><td><code>check</code></td><td><code>./cfire check main.cf</code></td><td>Fast frontend validation: parses and runs semantic analysis without invoking the C++ backend.</td></tr>
            <tr><td><code>eval</code></td><td><code>./cfire eval '2 + 2'</code></td><td>Evaluates a single <code>cfire</code> expression or statement directly from the command line.</td></tr>
            <tr><td><code>lsp</code></td><td><code>./cfire lsp</code></td><td>Starts the standard Language Server Protocol daemon for VSCode, Neovim, and Helix.</td></tr>
            <tr><td><code>doc</code></td><td><code>./cfire doc</code></td><td>Generates verified HTML and Markdown documentation from the compiler catalog.</td></tr>
            <tr><td><code>doc --check</code></td><td><code>./cfire doc --check</code></td><td>Verifies that documentation files match the compiler catalog character-for-character.</td></tr>
            <tr><td><code>eject</code></td><td><code>./cfire eject main.cf ./export</code></td><td>Exports the program and all dependencies into standalone ISO C++23 files + <code>CMakeLists.txt</code>.</td></tr>
          </tbody>
        </table>
      </div>

      <h3>2.2 Embedded Unit Testing</h3>
      <p>
        <code>cfire</code> includes testing as a core language primitive. You can write isolated test suites directly inside any <code>.cf</code> file:
      </p>
      <div class="code-header"><span class="tag-cf">cfire</span><span>math_util.cf</span></div>
      <pre><code>package math_util

fn add(a: i64, b: i64) -> i64 {
    return a + b
}

fn clamp(val: f64, low: f64, high: f64) -> f64 {
    if val &lt; low { return low }
    if val &gt; high { return high }
    return val
}

test "integer arithmetic addition" {
    assert(add(10, 20) == 30)
    assert(add(-5, 5) == 0)
}

test "floating-point clamp bounds" {
    assert(clamp(5.0, 0.0, 10.0) == 5.0)
    assert(clamp(-2.0, 0.0, 10.0) == 0.0)
    assert(clamp(15.0, 0.0, 10.0) == 10.0)
}</code></pre>

      <!-- CHAPTER 3 -->
      <h2 id="chapter-3"><a id="ch3"></a>3. Syntax, Primitives, Variables &amp; Control Flow</h2>

      <h3>3.1 Immutability by Default</h3>
      <p>
        In <code>cfire</code>, bindings declared with <code>:=</code> or explicit type annotations without <code>mut</code> are strictly <strong>immutable (<code>const</code>)</strong>. Attempting to reassign an immutable binding triggers a compile error:
      </p>
      <div class="code-header"><span class="tag-cf">cfire</span></div>
      <pre><code>limit := 100
// limit = 200 // ERROR: cannot assign to immutable binding 'limit'

mut counter := 0 // Mutable binding
counter += 1     // OK

max_payload: u64 = 1048576
mut buffer_size: i32 = 4096</code></pre>

      <h3>3.2 Slices (<code>[]T</code>) vs Fixed Arrays (<code>[N]T</code>)</h3>
      <p>
        <code>cfire</code> makes a sharp distinction between stack-allocated fixed arrays and dynamically sized slices:
      </p>
      <ul>
        <li><strong>Fixed Arrays (<code>[N]T</code>):</strong> Size is known at compile time, memory is allocated on the stack with zero overhead, lowering to <code>std::array&lt;T, N&gt;</code>.</li>
        <li><strong>Dynamic Slices (<code>[]T</code>):</strong> Growable dynamic buffers backed by the heap, lowering to <code>std::vector&lt;T&gt;</code> when owned, or <code>std::span&lt;const T&gt;</code> when borrowed as an <code>in</code> parameter.</li>
      </ul>
      <div class="code-header"><span class="tag-cf">cfire</span></div>
      <pre><code>// Fixed array (std::array&lt;int32_t, 4&gt;)
fixed: [4]i32 = [10, 20, 30, 40]
println("fixed length: {}", fixed.len())

// Dynamic slice (std::vector&lt;int32_t&gt;)
mut dynamic: []i32 = [1, 2, 3]
dynamic.push(4)
dynamic.push(5)
println("dynamic length: {}", dynamic.len())

// Slicing views (std::span)
sub := dynamic[1:3] // View of elements at index 1 and 2</code></pre>

      <h3>3.3 Hash Maps (<code>map[K]V</code>)</h3>
      <div class="code-header"><span class="tag-cf">cfire</span></div>
      <pre><code>mut scores: map[str]i64 = {}
scores["Alice"] = 95
scores["Bob"] = 88

// Safe retrieval with 'or' fallback
alice_score := scores["Alice"] or 0
charlie_score := scores["Charlie"] or -1

println("Alice: {}, Charlie: {}", alice_score, charlie_score)

// Check existence
if scores.contains("Alice") {
    println("Alice is enrolled")
}</code></pre>

      <h3>3.4 Smart Pointers (<code>own[T]</code> &amp; <code>shared[T]</code>)</h3>
      <div class="code-header"><span class="tag-cf">cfire</span></div>
      <pre><code>type Node struct {
    Value: i64,
    Next:  own[Node]?,
}

// Allocate an owned heap box (std::unique_ptr&lt;Node&gt;)
first := make(own[Node], Node{Value: 10, Next: null})
println("Node value: {}", (*first).Value)</code></pre>

      <h3>3.5 Control Flow</h3>
      <div class="code-header"><span class="tag-cf">cfire</span></div>
      <pre><code>// 1. Conditional branching
if score &gt;= 90 {
    println("Grade: A")
} else if score &gt;= 80 {
    println("Grade: B")
} else {
    println("Grade: C")
}

// 2. Iterating elements
items := ["alpha", "beta", "gamma"]
for item in items {
    println("Item: {}", item)
}

// 3. Iterating with index
for idx, item in items {
    println("[{}] = {}", idx, item)
}

// 4. Iterating map key-value pairs
for key, val in scores {
    println("{} => {}", key, val)
}

// 5. C-style numeric loop
for mut i := 0; i &lt; 5; i += 1 {
    if i == 2 { continue }
    println("i = {}", i)
}</code></pre>

      <!-- CHAPTER 4 -->
      <h2 id="chapter-4"><a id="ch4"></a>4. Functions &amp; Call-Site Intent Safety</h2>

      <h3>4.1 Function Declarations and Methods</h3>
      <div class="code-header"><span class="tag-cf">cfire</span></div>
      <pre><code>type Account struct {
    Owner:   str,
    Balance: i64,
}

// Read-only receiver method
fn (self: Account) display() {
    println("Account {}: ${}", self.Owner, self.Balance)
}

// Mutable receiver method: requires 'mut self'
fn (mut self: Account) deposit(amount: i64) {
    self.Balance += amount
}</code></pre>

      <h3>4.2 Explicit Parameter Intents</h3>
      <p>
        In C++, calling <code>process(data)</code> hides whether <code>data</code> is read-only, modified in-place, or moved from. <code>cfire</code> enforces visibility at <strong>both</strong> declaration and call site:
      </p>
      <div class="table-wrap">
        <div class="table-scroll-cue">Scroll table &rarr;</div>
        <table>
          <thead>
            <tr><th>Intent</th><th>Declaration</th><th>Call Site</th><th>C++23 Lowering</th><th>Guarantee</th></tr>
          </thead>
          <tbody>
            <tr><td><strong><code>in</code> (default)</strong></td><td><code>x: T</code></td><td><code>f(x)</code></td><td><code>const T&amp;</code> / <code>string_view</code> / <code>span</code></td><td>Read-only borrow; zero copy overhead.</td></tr>
            <tr><td><strong><code>mut</code></strong></td><td><code>mut x: T</code></td><td><code>f(mut x)</code></td><td><code>T&amp;</code></td><td>In-place mutation; requires <code>mut</code> at call site.</td></tr>
            <tr><td><strong><code>move</code></strong></td><td><code>move x: T</code></td><td><code>f(move x)</code></td><td><code>T&amp;&amp;</code> + <code>std::move(x)</code></td><td>Ownership transfer; callee consumes value.</td></tr>
          </tbody>
        </table>
      </div>
      <p style="margin-top:0.85rem;">
        To return multiple values, <code>cfire</code> uses first-class tuples and deconstructing assignments instead of mutable output parameters:
      </p>
      <pre><code>fn parse_coords(raw: str) -&gt; (f64, f64)! {
    return (12.5, 45.2)
}

(lat, lon) := try parse_coords("12.5,45.2")</code></pre>

      <h3>4.3 Call-Site Safety Diagnostics</h3>
      <p>The compiler enforces strict aliasing and mutation invariants:</p>
      <ul>
        <li><strong><code>cfire/MUT-CALLSITE</code>:</strong> Caller must explicitly write <code>mut</code> when passing to a mutable parameter (e.g., <code>increment(mut count)</code>).</li>
        <li><strong><code>cfire/IMMUTABLE-MUT-ARG</code>:</strong> Rejects passing an immutable binding to a mutable parameter.</li>
        <li><strong><code>cfire/MUT-ALIAS</code> (Law of Exclusivity):</strong> Rejects passing the same variable to multiple mutable parameters in the same call:
          <pre><code>// transfer(mut account, mut account, 100) // REJECTED: [cfire/MUT-ALIAS]</code></pre>
        </li>
        <li><strong><code>cfire/USE-AFTER-MOVE</code>:</strong> Rejects reading or passing a variable after it has been moved:
          <pre><code>sink(move payload)
// println("len = {}", payload.len()) // REJECTED: [cfire/USE-AFTER-MOVE]</code></pre>
        </li>
      </ul>

      <!-- CHAPTER 5 -->
      <h2 id="chapter-5"><a id="ch5"></a>5. Structs, Compile-Time Tags &amp; Algebraic Enums</h2>

      <h3>5.1 Structs &amp; Compile-Time Field Metadata</h3>
      <p>
        Field tags (enclosed in backticks) provide zero-cost metadata inspected by the compiler during code generation to eliminate runtime reflection:
      </p>
      <div class="code-header"><span class="tag-cf">cfire</span></div>
      <pre><code>type DatabaseConfig struct {
    Host:     str  `json:"host"     cli:"--host,-h"    sql:"host"`
    Port:     u16  `json:"port"     cli:"--port,-p"    sql:"port"`
    MaxConns: i32  `json:"max_conns" cli:"--max-conns" sql:"max_connections"`
    SSL:      bool `json:"ssl"      cli:"--ssl"        sql:"use_ssl"`
}

fn main() {
    // Automatic CLI flag parsing directly into typed struct
    cfg := try cli.parse[DatabaseConfig]()
    println("Config loaded: host={}, port={}", cfg.Host, cfg.Port)

    // Automatic JSON serialization
    json_str := try json.stringify(cfg)
    println("Serialized: {}", json_str)
}</code></pre>

      <h3>5.2 Algebraic Enums &amp; Exhaustive Pattern Matching</h3>
      <p>
        Algebraic enums represent tagged unions, lowering to <code>std::variant</code> in C++23. The compiler enforces exhaustiveness (<code>cfire/NON-EXHAUSTIVE-MATCH</code>):
      </p>
      <div class="code-header"><span class="tag-cf">cfire</span></div>
      <pre><code>type NetworkEvent enum {
    Connected(peer_ip: str, port: u16),
    DataReceived(payload: []byte),
    Disconnected(reason: str),
    Heartbeat,
}

fn handle_event(event: NetworkEvent) {
    match event {
        Connected(ip, port) => {
            println("Peer connected: {}:{}", ip, port)
        }
        DataReceived(data) => {
            println("Received payload of {} bytes", data.len())
        }
        Disconnected(reason) => {
            println("Connection closed: {}", reason)
        }
        Heartbeat => {
            // Keep-alive ack
        }
    }
}</code></pre>

      <!-- CHAPTER 6 -->
      <h2 id="chapter-6"><a id="ch6"></a>6. Fearless Error Handling &amp; Cleanup</h2>
      <p>
        <code>cfire</code> eliminates hidden C++ exceptions. Functions that can fail return <code>T!</code>, which lowers directly to <code>std::expected&lt;T, cfire::Error&gt;</code>:
      </p>
      <div class="code-header"><span class="tag-cf">cfire</span></div>
      <pre><code>import "core:fs"
import "core:sync"

mut mtx: sync.Mutex = {}

fn process_transaction() -> void! {
    mtx.lock()
    defer mtx.unlock() // Guaranteed to unlock even on early return

    mut file := try fs.open("audit.log", fs.OpenMode::Append)
    defer file.close() // File closed cleanly

    try file.write_str("Transaction started\n")
    try execute_db_write()
    try file.write_str("Transaction committed\n")
}</code></pre>

      <h3>6.2 The Three Ways to Handle Errors</h3>
      <ol>
        <li><strong>Early Return (<code>try</code>):</strong> Unwraps the value or returns the error up the call stack immediately.</li>
        <li><strong>Default Fallback (<code>or value</code>):</strong> Provides a fallback value if an error occurs (e.g. <code>port := parse_port(env) or 8080</code>).</li>
        <li><strong>Inline Recovery (<code>or err { ... }</code>):</strong> Executes a custom block that must definitely diverge (<code>return</code>, <code>panic</code>, <code>break</code>).</li>
      </ol>

      <!-- CHAPTER 7 -->
      <h2 id="chapter-7"><a id="ch7"></a>7. Structured Concurrency &amp; Contexts</h2>

      <h3>7.1 Structured Concurrency with <code>async.scope</code></h3>
      <p>
        Unlike raw threads, an <code>async.scope</code> block guarantees that all spawned worker tasks join completely before the scope exits:
      </p>
      <div class="code-header"><span class="tag-cf">cfire</span></div>
      <pre><code>import "core:time"

fn process_batch(items: []str) {
    async.scope(workers: 4) |scope| {
        for idx, item in items {
            scope.spawn || {
                println("Worker processing item [{}]: {}", idx, item)
                time.sleep_ms(50)
            }
        }
    }
    // All 4 worker tasks are 100% joined before this line executes!
    println("All batch items successfully processed.")
}</code></pre>

      <h3>7.2 Message Passing via Typed Channels (<code>core:sync</code>)</h3>
      <div class="code-header"><span class="tag-cf">cfire</span></div>
      <pre><code>import "core:sync"

fn main() {
    ch := sync.make_channel[i64](capacity: 10)

    async.scope(workers: 2) |scope| {
        scope.spawn || {
            for mut i := 1; i &lt;= 5; i += 1 {
                ch.send(i * 10)
            }
            ch.close()
        }

        scope.spawn || {
            for {
                val := ch.recv() or break
                println("Received: {}", val)
            }
        }
    }
}</code></pre>

      <!-- CHAPTER 8 -->
      <h2 id="chapter-8"><a id="ch8"></a>8. Zero-Friction C &amp; C++ Interop</h2>

      <h3>8.1 Direct C Header Ingestion</h3>
      <p>
        When you write <code>import "c:zlib.h" as zlib</code>, <code>cfire</code> extracts the Clang AST JSON dump, caches the signatures in <code>.cfire/cache/ast/</code>, and automatically coerces <code>str</code> to <code>const char*</code> via <code>cfire::c_str(s)</code>:
      </p>
      <div class="code-header"><span class="tag-cf">cfire</span></div>
      <pre><code>package main

import "c:zlib.h" as zlib
import "c:stdio.h" as c_io

fn main() {
    version_str := zlib.zlibVersion()
    println("Zlib library version: {}", version_str)

    // Direct C file operations with automatic const char* coercion
    mut f := c_io.fopen("output.txt", "w")
    if f != null {
        c_io.fputs("Written via direct C interop\n", f)
        c_io.fclose(f)
    }
}</code></pre>

      <h3>8.2 Inline C++ Escape Hatch (<code>cpp { ... }</code>)</h3>
      <p>
        For platform intrinsics, inline assembly, or specialized template metaprogramming, drop raw C++ statements inside any function:
      </p>
      <div class="code-header"><span class="tag-cf">cfire</span></div>
      <pre><code>fn get_cpu_cycle_count() -> u64 {
    mut cycles: u64 = 0
    cpp {
        #if defined(__x86_64__)
        cycles = __rdtsc();
        #elif defined(__aarch64__)
        asm volatile("mrs %0, cntvct_el0" : "=r"(cycles));
        #endif
    }
    return cycles
}</code></pre>

      <!-- CHAPTER 9 -->
      <h2 id="chapter-9"><a id="ch9"></a>9. The Unified Build System &amp; CMake Ejection</h2>

      <h3>9.1 Declarative <code>build.cf</code></h3>
      <div class="code-header"><span class="tag-cf">cfire</span><span>build.cf</span></div>
      <pre><code>package build

import "core:build"

// 1. Build a local C static library dependency
FastHash := build.CC("./native/fast_hash", {
    Sources:  ["fast_hash.c"],
    Includes: ["."],
    CFlags:   ["-O3", "-march=native"],
})

// 2. Build a CMake-based vendor subproject
KVStore := build.CMake("./vendor/libkv_cmake", {
    Target:   "kvstore",
    Includes: ["include"],
    Defines:  {"KV_SALT": 42},
})

// 3. Link a system package via pkg-config
Sqlite3 := build.SystemLib("sqlite3")</code></pre>

      <h3>9.2 Zero-Lock-In CMake Ejection (<code>cfire eject</code>)</h3>
      <p>
        Export your entire codebase to standard ISO C++23 with a single command:
      </p>
      <pre><code>./cfire eject src/main.cf ./standalone_project</code></pre>
      <p>
        This generates standalone ISO C++23 source files, header-only runtime preludes, and a complete <code>CMakeLists.txt</code>. Anyone on your team can now build your software using standard CMake and Clang without needing <code>cfire</code> installed:
      </p>
      <pre><code>cd standalone_project
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build</code></pre>

      <!-- CHAPTER 10 -->
      <h2 id="chapter-10"><a id="ch10"></a>10. Building Web Services, HTTP Servers &amp; Independent Routers</h2>
      <p>
        Modern systems software frequently needs to expose telemetry endpoints, REST APIs, or lightweight RPC microservices. Rather than forcing invasive third-party event loops or heavy foreign C libraries, <code>cfire</code> provides a native, zero-dependency HTTP/1.1 protocol engine and independent router built directly on top of POSIX TCP sockets and ISO C++23.
      </p>

      <h3>10.1 Decoupled Architecture &amp; Zero External Dependencies</h3>
      <p>
        The <code>core:net/http</code> module adheres to two key design principles:
      </p>
      <ul>
        <li><strong>Zero Third-Party Libraries:</strong> The wire stream parser, response serializer, multi-mode server, and route matcher are pure standard C++23. No <code>libmicrohttpd</code> or external libraries required.</li>
        <li><strong>Independent Features:</strong> Routing and dispatching operate strictly on <code>http.Request &rarr; http.Response</code>. Routers can be created, configured, and unit-tested purely in memory without opening sockets or listening on network ports.</li>
      </ul>

      <h3>10.2 Request Parsing &amp; Fluent Response Builders</h3>
      <p>
        Incoming requests are automatically parsed into the structured <code>http.Request</code> model, providing clean decoded paths, query parameters, and case-insensitive headers:
      </p>
      <div class="code-header"><span class="tag-cf">cfire</span></div>
      <pre><code>// Method, path, queries, and headers
method := req.method            // "GET", "POST", etc.
path   := req.path              // "/api/users/42"
limit  := req.query("limit", "10") // auto percent-decoded
token  := req.header("Authorization") // case-insensitive
id     := req.param("id")       // extracted by router (:id)</code></pre>

      <p>
        Construct responses using fluent type-safe factory methods:
      </p>
      <div class="code-header"><span class="tag-cf">cfire</span></div>
      <pre><code>// Type-safe status &amp; MIME constructors
res1 := http.Response.text(200, "operation successful")
res2 := http.Response.json(200, "{\"status\":\"healthy\"}")
res3 := http.Response.redirect("/dashboard", 302)
res4 := http.Response.not_found("user not found")</code></pre>

      <h3>10.3 Path Parameters, Wildcards &amp; Independent Routing</h3>
      <p>
        The <code>http.Router</code> matches routes by HTTP verb and pattern. It supports single-segment path parameters (<code>:param</code>) and greedy suffix wildcards (<code>*filepath</code>):
      </p>
      <div class="code-header"><span class="tag-cf">cfire</span></div>
      <pre><code>mut router := http.make_router()

// Exact match
router.get("/health", |req| http.Response.json(200, "{\"status\":\"ok\"}"))

// Path parameters (:id)
router.get("/users/:id", |req| {
    return http.Response.text(200, "User ID: " + req.param("id"))
})

// Greedy wildcard (*filepath)
router.get("/static/*filepath", |req| {
    return http.Response.text(200, "Serving: " + req.param("filepath"))
})

// HTTP verb differentiation
router.post("/items", |req| {
    return http.Response.json(201, "{\"created\":true}")
})</code></pre>

      <h3>10.4 In-Memory Standalone Unit Testing</h3>
      <p>
        Because the router is independent of networking sockets, handlers can be verified with instant, deterministic unit tests without opening sockets or risking port conflicts:
      </p>
      <div class="code-header"><span class="tag-cf">cfire</span></div>
      <pre><code>// Dispatch synthetic requests directly in-memory
test_req := http.Request{ method: "GET", path: "/users/42" }
test_res := router.dispatch(test_req)

assert(test_res.status_code == 200)
assert(test_res.body == "User ID: 42")</code></pre>

      <h3>10.5 Serving Live Requests with <code>HttpServer</code></h3>
      <p>
        When you are ready to serve live network traffic, instantiate <code>http.make_server</code> and pass the router directly to <code>server.serve_in_background(router)</code> or <code>server.listen_and_serve(router)</code>:
      </p>
      <div class="code-header"><span class="tag-cf">cfire</span><span>examples/11-http-server-router/main.cf</span></div>
      <pre><code>package main

import "core:net/http"

fn main() {
    mut router := http.make_router()
    router.get("/health", |req| http.Response.json(200, "{\"status\":\"healthy\"}"))
    router.get("/users/:id", |req| http.Response.text(200, "User: " + req.param("id")))

    // Bind server to port (0 selects an ephemeral port for testing)
    mut server := try http.make_server(port: 8080)
    println("Listening on http://127.0.0.1:{}", server.port())

    // Serve requests asynchronously in a dedicated worker thread
    server.serve_in_background(router)

    // Execute client requests
    resp := try http.get("http://127.0.0.1:8080/health")
    println("Response: {} -> {}", resp.status_code, resp.body)

    // Clean graceful shutdown
    server.stop()
}</code></pre>
    </main>
  </div>
</div>
<footer class="footer">
  Generated by <code>cfire doc</code> &bull; <a href="reference.html">Explore the `core:*` Stdlib &amp; Diagnostics Reference &rarr;</a>
</footer>
</body>
</html>
)HTML";
    return out;
}

[[nodiscard]] inline auto generate_reference_markdown_cxx() -> std::string {
    std::string out = "# `cfire` Standard Library (`core:*`) & Diagnostics Reference\n\n";
    out += "> Auto-generated from the compiler's single-source-of-truth catalog via `./cfire doc`.\n\n";
    out += "## 1. Built-In `core:*` Standard Library Modules\n\n";
    for (const auto& mod : stdlib_catalog()) {
        out += std::format("### `{}` (`{}`) — {}\n\n", mod.import_path, mod.ns_alias, mod.title);
        out += std::format("- **Auto-Link Flags:** `{}`\n", mod.link_flags);
        out += std::format("- **Overview:** {}\n\n", mod.overview);
        auto ex = detail::module_practical_example(mod.import_path);
        if (!ex.empty()) {
            out += "**Practical Example:**\n```cf\n" + std::string(ex) + "\n```\n\n";
        }
        out += "| `.cf` Signature | Lowered C++23 | Description | Example |\n";
        out += "|---|---|---|---|\n";
        for (const auto& fn : mod.functions) {
            out += std::format("| `{}` | `{}` | {} | `{}` |\n",
                fn.cf_sig, fn.cpp_lowering, fn.summary, fn.example);
        }
        out += "\n";
    }

    out += "## 2. Semantic Diagnostic Codes\n\n";
    for (const auto& d : diagnostics_catalog()) {
        out += std::format("### `[{}]` — {}\n\n", d.code, d.title);
        out += d.rule + "\n\n";
        out += "**Rejected (`.cf`):**\n```cf\n" + d.bad_example + "\n```\n\n";
        out += "**Accepted (`.cf`):**\n```cf\n" + d.good_example + "\n```\n\n";
    }
    return out;
}

[[nodiscard]] inline auto generate_reference_markdown() -> std::string {
    const char* force_cxx = std::getenv("CFIRE_CXX_DOCGEN");
    if (force_cxx && std::string_view(force_cxx) == "1") {
        return generate_reference_markdown_cxx();
    }
    const auto& catalog = stdlib_catalog();
    std::vector<std::string> examples;
    examples.reserve(catalog.size());
    for (const auto& mod : catalog) {
        examples.emplace_back(detail::module_practical_example(mod.import_path));
    }
    return ::generate_reference_markdown(catalog, diagnostics_catalog(), examples);
}

[[nodiscard]] inline auto generate_reference_html_cxx() -> std::string {
    std::string out;
    out += "<!DOCTYPE html>\n<html lang=\"en\">\n<head>\n";
    out += "  <meta charset=\"UTF-8\" />\n";
    out += "  <meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\" />\n";
    out += "  <title>Stdlib &amp; Diagnostics Reference — cfire</title>\n";
    out += "  <style>" + detail::shared_css() + "</style>\n";
    out += "</head>\n<body>\n";
    out += detail::navbar_html("reference");
    out += "<div class=\"container\">\n";
    out += "  <div class=\"doc-layout\">\n";
    out += "    <aside class=\"sidebar\">\n";
    out += "      <details class=\"sidebar-details\" open>\n";
    out += "        <summary class=\"sidebar-summary\">\n";
    out += "          <span>Modules &amp; Diagnostics</span>\n";
    out += "          <span class=\"sidebar-chevron\">&#9662;</span>\n";
    out += "        </summary>\n";
    out += "        <div class=\"sidebar-content\">\n";
    out += "          <h4>`core:*` Modules</h4>\n          <ul>\n";
    for (const auto& mod : stdlib_catalog()) {
        out += std::format("            <li><a href=\"#mod-{}\"><code>{}</code></a></li>\n", mod.ns_alias, mod.import_path);
    }
    out += "          </ul>\n          <h4 style=\"margin-top:1.25rem;\">Diagnostics</h4>\n          <ul>\n";
    for (const auto& d : diagnostics_catalog()) {
        out += std::format("            <li><a href=\"#diag-{}\"><code>{}</code></a></li>\n", d.code, d.code);
    }
    out += "          </ul>\n        </div>\n      </details>\n    </aside>\n    <main>\n";
    out += "      <h1>`core:*` Standard Library &amp; Diagnostics Reference</h1>\n";
    out += "      <p class=\"subtitle\" style=\"margin:0 0 2rem;\">Automatically generated from the live <code>cfire</code> compiler metadata catalog and verified by <code>cfire doc --check</code>.</p>\n";

    for (const auto& mod : stdlib_catalog()) {
        out += std::format("      <h2 id=\"mod-{}\"><code>import \"{}\"</code> &mdash; {}</h2>\n",
            mod.ns_alias, mod.import_path, detail::html_escape(mod.title));
        out += std::format("      <p>{} <br/><small style=\"color:var(--muted)\">Auto-linked flags: <code>{}</code></small></p>\n",
            mod.overview, detail::html_escape(mod.link_flags));
        auto ex = detail::module_practical_example(mod.import_path);
        if (!ex.empty()) {
            out += "      <div class=\"code-header\"><span class=\"tag-cf\">Practical Workflow Example</span></div>\n";
            out += "      <pre><code>" + detail::html_escape(ex) + "</code></pre>\n";
        }
        out += "      <div class=\"table-wrap api-table-wrap\"><table>\n        <thead><tr><th>`.cf` Signature</th><th>C++23 Lowering</th><th>Description &amp; Example</th></tr></thead>\n        <tbody>\n";
        for (const auto& fn : mod.functions) {
            out += std::format("          <tr><td><code>{}</code></td><td><code>{}</code></td><td>{}<br/><code>{}</code></td></tr>\n",
                detail::html_escape(fn.cf_sig),
                detail::html_escape(fn.cpp_lowering),
                detail::html_escape(fn.summary),
                detail::html_escape(fn.example));
        }
        out += "        </tbody>\n      </table></div>\n";
    }

    out += "      <h2 class=\"section-title\">Compiler Semantic Diagnostics</h2>\n";
    for (const auto& d : diagnostics_catalog()) {
        out += std::format("      <div class=\"card\" id=\"diag-{}\" style=\"margin-bottom:1.25rem;\">\n", d.code);
        out += std::format("        <h3><code>[{}]</code> &mdash; {}</h3>\n", d.code, detail::html_escape(d.title));
        out += std::format("        <p style=\"margin-bottom:0.85rem;\">{}</p>\n", detail::html_escape(d.rule));
        out += "        <div class=\"grid-2\">\n";
        out += std::format("          <div><div class=\"code-header\"><span style=\"color:var(--err);font-weight:700;\">Rejected (.cf)</span></div><pre><code>{}</code></pre></div>\n",
            detail::html_escape(d.bad_example));
        out += std::format("          <div><div class=\"code-header\"><span style=\"color:var(--ok);font-weight:700;\">Accepted (.cf)</span></div><pre><code>{}</code></pre></div>\n",
            detail::html_escape(d.good_example));
        out += "        </div>\n      </div>\n";
    }

    out += "    </main>\n  </div>\n</div>\n";
    out += "<footer class=\"footer\">Generated by <code>cfire doc</code> &bull; Always kept in sync with <code>src/docgen.hpp</code>.</footer>\n";
    out += "</body>\n</html>\n";
    return out;
}

[[nodiscard]] inline auto generate_reference_html() -> std::string {
    const char* force_cxx = std::getenv("CFIRE_CXX_DOCGEN");
    if (force_cxx && std::string_view(force_cxx) == "1") {
        return generate_reference_html_cxx();
    }
    const auto& catalog = stdlib_catalog();
    std::vector<std::string> examples;
    examples.reserve(catalog.size());
    for (const auto& mod : catalog) {
        examples.emplace_back(detail::module_practical_example(mod.import_path));
    }
    return ::generate_reference_html(catalog, diagnostics_catalog(), detail::shared_css(), examples);
}

// Extracts Markdown API documentation for a specific `.cf` file (including doc comments and lowered C++23 signatures)
[[nodiscard]] inline auto extract_file_markdown_doc_cxx(const std::filesystem::path& cf_file)
    -> std::expected<std::string, std::string>
{
    if (!std::filesystem::exists(cf_file)) {
        return std::unexpected(std::format("file not found: {}", cf_file.string()));
    }
    std::ifstream ifs(cf_file);
    std::stringstream ss;
    ss << ifs.rdbuf();
    std::string src = ss.str();

    std::vector<std::string> lines;
    {
        std::istringstream ls(src);
        std::string l;
        while (std::getline(ls, l)) {
            if (!l.empty() && l.back() == '\r') l.pop_back();
            lines.push_back(l);
        }
    }

    auto comment_above = [&](int one_based_line) -> std::string {
        int idx = one_based_line - 2;
        std::vector<std::string> collected;
        while (idx >= 0) {
            std::string t = cfire::fmt::detail::trim_ws(lines[static_cast<std::size_t>(idx)]);
            if (t.starts_with("//")) {
                std::string clean = cfire::fmt::detail::trim_ws(std::string_view(t).substr(t.starts_with("///") ? 3 : 2));
                collected.push_back(clean);
                --idx;
            } else {
                break;
            }
        }
        std::reverse(collected.begin(), collected.end());
        std::string out;
        for (const auto& c : collected) {
            if (!out.empty()) out += " ";
            out += c;
        }
        return out;
    };

    auto ast_res = cfire::frontend::parse_source(cf_file.string(), src);
    if (!ast_res) {
        return std::unexpected(ast_res.error());
    }
    const auto& ast = *ast_res;

    std::string md = std::format("# Package `{}` (`{}`)\n\n", ast.package_name, cf_file.filename().string());
    if (!ast.imports.empty()) {
        md += "## Imports\n";
        for (const auto& imp : ast.imports) {
            md += std::format("- `\"{}\"`\n", imp.path);
        }
        md += "\n";
    }
    if (!ast.structs.empty()) {
        md += "## Structs\n\n";
        for (const auto& st : ast.structs) {
            md += std::format("### `type {} struct`\n", st.name);
            if (auto doc = comment_above(st.line); !doc.empty()) md += doc + "\n\n";
            md += "```cf\n" + cfire::fmt::format_struct_signature(st) + "\n```\n\n";
        }
    }
    if (!ast.enums.empty()) {
        md += "## Algebraic Enums\n\n";
        for (const auto& en : ast.enums) {
            md += std::format("### `type {} enum`\n", en.name);
            if (auto doc = comment_above(en.line); !doc.empty()) md += doc + "\n\n";
            md += "```cf\n" + cfire::fmt::format_enum_signature(en) + "\n```\n\n";
        }
    }
    if (!ast.functions.empty()) {
        md += "## Functions\n\n";
        for (const auto& fn : ast.functions) {
            md += std::format("### `{}`\n", cfire::fmt::format_fn_signature(fn));
            if (auto doc = comment_above(fn.line); !doc.empty()) md += doc + "\n\n";
        }
    }
    if (!ast.tests.empty()) {
        md += "## Built-in Tests\n\n";
        for (const auto& t : ast.tests) {
            md += std::format("- `test \"{}\"` (line {})\n", t.name, t.line);
        }
        md += "\n";
    }
    return md;
}

[[nodiscard]] inline auto extract_file_markdown_doc(const std::filesystem::path& cf_file)
    -> std::expected<std::string, std::string>
{
    const char* force_cxx = std::getenv("CFIRE_CXX_DOCGEN");
    if (force_cxx && std::string_view(force_cxx) == "1") {
        return extract_file_markdown_doc_cxx(cf_file);
    }
    if (!std::filesystem::exists(cf_file)) {
        return std::unexpected(std::format("file not found: {}", cf_file.string()));
    }
    std::ifstream ifs(cf_file);
    std::stringstream ss;
    ss << ifs.rdbuf();
    std::string src = ss.str();

    std::vector<std::string> lines;
    {
        std::istringstream ls(src);
        std::string l;
        while (std::getline(ls, l)) {
            if (!l.empty() && l.back() == '\r') l.pop_back();
            lines.push_back(l);
        }
    }

    auto comment_above = [&](int one_based_line) -> std::string {
        int idx = one_based_line - 2;
        std::vector<std::string> collected;
        while (idx >= 0) {
            std::string t = cfire::fmt::detail::trim_ws(lines[static_cast<std::size_t>(idx)]);
            if (t.starts_with("//")) {
                std::string clean = cfire::fmt::detail::trim_ws(std::string_view(t).substr(t.starts_with("///") ? 3 : 2));
                collected.push_back(clean);
                --idx;
            } else {
                break;
            }
        }
        std::reverse(collected.begin(), collected.end());
        std::string out;
        for (const auto& c : collected) {
            if (!out.empty()) out += " ";
            out += c;
        }
        return out;
    };

    auto ast_res = cfire::frontend::parse_source(cf_file.string(), src);
    if (!ast_res) {
        return std::unexpected(ast_res.error());
    }
    const auto& ast = *ast_res;

    std::vector<std::string> imp_paths;
    for (const auto& imp : ast.imports) imp_paths.push_back(imp.path);

    std::vector<DocStruct> doc_structs;
    for (const auto& st : ast.structs) {
        doc_structs.push_back(DocStruct{
            st.name,
            comment_above(st.line),
            cfire::fmt::format_struct_signature(st)
        });
    }

    std::vector<DocEnum> doc_enums;
    for (const auto& en : ast.enums) {
        doc_enums.push_back(DocEnum{
            en.name,
            comment_above(en.line),
            cfire::fmt::format_enum_signature(en)
        });
    }

    std::vector<DocFunction> doc_functions;
    for (const auto& fn : ast.functions) {
        doc_functions.push_back(DocFunction{
            fn.name,
            comment_above(fn.line),
            cfire::fmt::format_fn_signature(fn)
        });
    }

    std::vector<DocTest> doc_tests;
    for (const auto& t : ast.tests) {
        doc_tests.push_back(DocTest{
            t.name,
            static_cast<std::size_t>(t.line)
        });
    }

    return ::format_package_markdown(
        ast.package_name,
        cf_file.filename().string(),
        imp_paths,
        doc_structs,
        doc_enums,
        doc_functions,
        doc_tests
    );
}

// Generates or verifies (`check_only = true`) the full documentation suite in `<repo>/docs/`.
[[nodiscard]] inline auto sync_docs_directory(const std::filesystem::path& docs_dir, bool check_only)
    -> std::expected<void, std::string>
{
    struct DocTarget {
        std::filesystem::path path;
        std::string content;
    };
    std::vector<DocTarget> targets = {
        {docs_dir / "index.html", generate_landing_html(docs_dir)},
        {docs_dir / "guide.html", generate_guide_html()},
        {docs_dir / "reference.html", generate_reference_html()},
        {docs_dir / "GUIDE.md", generate_guide_markdown()},
        {docs_dir / "STDLIB_REFERENCE.md", generate_reference_markdown()},
    };

    if (!check_only) {
        std::filesystem::create_directories(docs_dir);
    }

    for (const auto& t : targets) {
        if (check_only) {
            if (!std::filesystem::exists(t.path)) {
                return std::unexpected(std::format("missing documentation file: {} (run `./cfire doc` to regenerate)", t.path.string()));
            }
            std::ifstream ifs(t.path);
            std::stringstream ss;
            ss << ifs.rdbuf();
            if (ss.str() != t.content) {
                return std::unexpected(std::format("documentation file out of date: {} (run `./cfire doc` to regenerate)", t.path.string()));
            }
        } else {
            std::ofstream ofs(t.path, std::ios::trunc);
            ofs << t.content;
        }
    }
    return {};
}

} // namespace cfire::doc

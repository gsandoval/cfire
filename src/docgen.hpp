#pragma once
// src/docgen.hpp — Single-Source-Of-Truth Metadata Catalog & Documentation Generator (`cfire doc`)
// Generates:
//   - docs/index.html            (Interactive "Sapphire on Fire" Landing Page)
//   - docs/guide.html & GUIDE.md (8-Chapter Language & Systems Programming Guide)
//   - docs/reference.html & STDLIB_REFERENCE.md (Complete `core:*` Stdlib & Diagnostics Reference)
// Also powers `cfire lsp` hover/autocomplete and `./cfire doc <file.cf>` package doc extraction.

#include "buildsys.hpp"
#include "formatter.hpp"
#include <expected>
#include <filesystem>
#include <format>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace cfire::doc {

struct StdFnDoc {
    std::string name;
    std::string cf_sig;
    std::string cpp_lowering;
    std::string summary;
    std::string example;
};

struct StdModuleDoc {
    std::string import_path; // e.g. "core:fs"
    std::string ns_alias;    // e.g. "fs"
    std::string title;
    std::string link_flags;
    std::string overview;
    std::vector<StdFnDoc> functions;
};

struct DiagnosticDoc {
    std::string code;
    std::string title;
    std::string rule;
    std::string bad_example;
    std::string good_example;
};

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
            "Computes fast 256-bit cryptographic hex digests over byte slices (backward-compatible alias for `core:crypto/sha256`).",
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
            "core:net/http", "http", "HTTP/1.1 Client & Lightweight Dispatch Server", "(POSIX sockets)",
            "High-level HTTP/1.1 client (`get`, `post`, `request`) and lightweight synchronous/threaded HTTP server dispatcher (`make_server`).",
            {
                {"get", "fn get(url: str, headers: map[str]str = {}) -> Response!", "cfire::http::get(url, headers)", "Executes an HTTP GET request against a target URL.", "resp := try http.get(\"http://127.0.0.1:8080/health\")"},
                {"post", "fn post(url: str, body: str, content_type: str = \"application/json\", headers: map[str]str = {}) -> Response!", "cfire::http::post(url, body, content_type, headers)", "Executes an HTTP POST request with payload body.", "resp := try http.post(\"http://127.0.0.1:8080/items\", \"{\\\"id\\\": 1}\")"},
                {"make_server", "fn make_server(port: u16 = 0) -> HttpServer!", "cfire::http::make_server(port)", "Creates a lightweight HTTP server listening on `port` (0 for ephemeral).", "mut srv := try http.make_server(8080)"}
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
            "cfire/OUT-CALLSITE",
            "Missing Call-Site `out` Marker",
            "Functions declaring `out param: T` initialize or overwrite an output parameter (`T&`). Callers must write `out arg` at the call site.",
            "fn write_sum(out total: i32) { total = 42 }\nfn main() {\n    mut s: i32 = 0\n    write_sum(s) // ERROR [cfire/OUT-CALLSITE]\n}",
            "fn write_sum(out total: i32) { total = 42 }\nfn main() {\n    mut s: i32 = 0\n    write_sum(out s) // OK\n}"
        },
        {
            "cfire/IMMUTABLE-MUT-ARG",
            "Passing Immutable Binding as `mut` or `out` Argument",
            "Bindings declared with `:=` or `x: T = ...` without `mut` are `const` by default and cannot be passed to `mut` or `out` parameters.",
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
        },
        {
            "cfire/DEPRECATED-OUT-PARAM",
            "Deprecated `out` Parameter Intent (Favor Multi-Return Tuples)",
            "The `out` parameter intent is deprecated in favor of multi-return tuples `fn parse() -> (f64, f64)!` and deconstructing assignments `(x, y) := parse()`. `out` parameters bypass definite assignment checks and hinder expression composition.",
            "fn parse_coords(s: str, out x: f64, out y: f64) { ... } // WARNING [cfire/DEPRECATED-OUT-PARAM]",
            "fn parse_coords(s: str) -> (f64, f64)! { return (1.0, 2.0) }\n(x, y) := try parse_coords(\"1,2\") // OK"
        },
        {
            "cfire/DEPRECATED-VAR",
            "Deprecated `var` Keyword (Favor Canonical `:=` or `: Type =`)",
            "The `var` keyword is deprecated in favor of canonical inferred declarations `x := val` / `mut x := val` and explicitly typed declarations `x: T = val` / `mut x: T = val`.",
            "var x = 42 // WARNING [cfire/DEPRECATED-VAR]\nvar mut name: str = \"hello\" // WARNING [cfire/DEPRECATED-VAR]",
            "x := 42 // OK\nmut name: str = \"hello\" // OK"
        }
    };
    return diags;
}

namespace detail {

[[nodiscard]] inline auto html_escape(std::string_view sv) -> std::string {
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

[[nodiscard]] inline auto shared_css() -> std::string {
    return R"CSS(
:root {
    --bg: #070b14;
    --bg-card: #0d1527;
    --bg-card-hover: #131f38;
    --bg-code: #050811;
    --border: #1e293b;
    --border-glow: rgba(56, 189, 248, 0.35);
    --text: #e2e8f0;
    --text-bright: #ffffff;
    --muted: #94a3b8;
    --sapphire: #38bdf8;
    --sapphire-deep: #0284c7;
    --sapphire-navy: #1e3a8a;
    --fire: #f97316;
    --fire-gold: #facc15;
    --ok: #34d399;
}
* { box-sizing: border-box; margin: 0; padding: 0; }
body {
    font-family: -apple-system, BlinkMacSystemFont, "Inter", "Segoe UI", Roboto, sans-serif;
    background: radial-gradient(circle at 50% 0%, #0c234b 0%, #070b14 60%);
    color: var(--text);
    line-height: 1.65;
    min-height: 100vh;
}
a { color: var(--sapphire); text-decoration: none; transition: color 0.15s; }
a:hover { color: #7dd3fc; text-decoration: underline; }
.navbar {
    position: sticky; top: 0; z-index: 50;
    background: rgba(7, 11, 20, 0.92);
    backdrop-filter: blur(16px);
    border-bottom: 1px solid var(--border);
    padding: 0.85rem 2rem;
    display: flex; align-items: center; justify-content: space-between;
}
.brand {
    display: flex; align-items: center; gap: 0.75rem;
    font-weight: 800; font-size: 1.25rem; color: #fff;
    text-decoration: none;
}
.brand:hover { text-decoration: none; }
.brand img.brand-icon {
    width: 38px; height: 38px; object-fit: contain;
    filter: drop-shadow(0 2px 8px rgba(56, 189, 248, 0.4));
}
.brand span.brand-text {
    font-size: 1.35rem; font-weight: 900; letter-spacing: -0.03em;
}
.brand span.fire {
    background: linear-gradient(90deg, var(--sapphire), var(--fire));
    -webkit-background-clip: text; -webkit-text-fill-color: transparent;
}
.nav-links { display: flex; align-items: center; gap: 1.5rem; font-size: 0.95rem; font-weight: 600; }
.nav-links a { color: var(--muted); transition: all 0.15s; }
.nav-links a:hover, .nav-links a.active { color: #fff; text-decoration: none; }
.btn-nav-primary {
    background: linear-gradient(135deg, var(--sapphire-deep), #2563eb);
    color: #fff !important; padding: 0.45rem 1.05rem; border-radius: 8px;
    box-shadow: 0 0 15px rgba(56, 189, 248, 0.25);
    font-size: 0.88rem; font-weight: 700;
}
.btn-nav-primary:hover { opacity: 0.92; transform: translateY(-1px); }

.container { max-width: 1200px; margin: 0 auto; padding: 2.5rem 1.5rem 5rem; }

/* Hero section */
.hero { text-align: center; padding: 2.5rem 1rem 3.5rem; }
.badge {
    display: inline-block; padding: 0.4rem 1.1rem; border-radius: 999px;
    font-size: 0.82rem; font-weight: 700; letter-spacing: 0.05em;
    background: rgba(14, 165, 233, 0.12); border: 1px solid rgba(56, 189, 248, 0.45);
    color: var(--sapphire); margin-bottom: 1.5rem;
    box-shadow: 0 0 20px rgba(56, 189, 248, 0.15);
}

/* Logo Showcase & Switcher */
.logo-showcase {
    display: flex; flex-direction: column; align-items: center; margin-bottom: 2rem;
}
.logo-stage {
    position: relative; width: 180px; height: 180px;
    display: flex; align-items: center; justify-content: center;
    background: radial-gradient(circle at center, rgba(30, 58, 138, 0.35) 0%, rgba(13, 21, 39, 0) 70%);
    border: 1px solid rgba(56, 189, 248, 0.25);
    border-radius: 28px;
    padding: 1.25rem;
    box-shadow: 0 0 50px rgba(56, 189, 248, 0.22), 0 0 80px rgba(249, 115, 22, 0.15);
    transition: all 0.25s ease;
}
.logo-stage:hover {
    border-color: rgba(56, 189, 248, 0.5);
    box-shadow: 0 0 65px rgba(56, 189, 248, 0.35), 0 0 100px rgba(249, 115, 22, 0.25);
}
.hero-logo-img {
    max-width: 100%; max-height: 100%; object-fit: contain;
    filter: drop-shadow(0 8px 24px rgba(0, 0, 0, 0.6));
    transition: transform 0.25s ease;
}
.hero-logo-img:hover { transform: scale(1.04); }
.logo-switcher {
    display: flex; gap: 0.5rem; margin-top: 1.25rem; background: var(--bg-card);
    border: 1px solid var(--border); border-radius: 12px; padding: 0.35rem;
}
.logo-switch-btn {
    background: transparent; color: var(--muted); border: none;
    padding: 0.4rem 0.9rem; border-radius: 8px; font-size: 0.82rem; font-weight: 700;
    cursor: pointer; transition: all 0.15s; display: flex; align-items: center; gap: 0.4rem;
}
.logo-switch-btn:hover { color: #fff; }
.logo-switch-btn.active {
    background: rgba(56, 189, 248, 0.18); color: #fff; border: 1px solid rgba(56, 189, 248, 0.4);
}
.switch-dot {
    width: 6px; height: 6px; border-radius: 50%; background: var(--sapphire);
}

h1.hero-title {
    font-size: 3.4rem; font-weight: 900; letter-spacing: -0.03em; margin-bottom: 1.2rem; line-height: 1.15;
}
.gradient-text {
    background: linear-gradient(135deg, #ffffff 15%, #38bdf8 60%, #f97316 100%);
    -webkit-background-clip: text; -webkit-text-fill-color: transparent;
}
.subtitle {
    max-width: 820px; margin: 0 auto 2.2rem; font-size: 1.22rem; color: var(--muted); line-height: 1.6;
}
.cta-row { display: flex; justify-content: center; gap: 1rem; flex-wrap: wrap; margin-bottom: 2rem; }
.btn {
    padding: 0.85rem 1.75rem; border-radius: 12px; font-weight: 700; font-size: 1rem;
    border: 1px solid var(--border); transition: all 0.2s ease; display: inline-flex;
    align-items: center; gap: 0.5rem; text-decoration: none;
}
.btn:hover { text-decoration: none; transform: translateY(-2px); }
.btn-primary {
    background: linear-gradient(135deg, #0284c7, #2563eb 50%, #f97316 100%);
    color: #fff; border: none;
    box-shadow: 0 4px 25px rgba(2, 132, 199, 0.45);
}
.btn-primary:hover { box-shadow: 0 6px 30px rgba(2, 132, 199, 0.6); opacity: 0.96; }
.btn-secondary { background: var(--bg-card); color: var(--text); border-color: #334155; }
.btn-secondary:hover { border-color: var(--sapphire); color: #fff; }

/* Install Box */
.install-bar {
    display: inline-flex; align-items: center; gap: 0.75rem;
    background: var(--bg-code); border: 1px solid #334155; border-radius: 12px;
    padding: 0.65rem 1.25rem; font-family: "JetBrains Mono", monospace; font-size: 0.92rem;
    color: #93c5fd; box-shadow: 0 4px 20px rgba(0, 0, 0, 0.4);
}
.install-bar .prompt { color: #f97316; font-weight: 700; }
.copy-btn {
    background: #1e293b; color: #e2e8f0; border: 1px solid #475569;
    padding: 0.3rem 0.65rem; border-radius: 6px; font-size: 0.76rem; font-weight: 600;
    cursor: pointer; transition: all 0.15s;
}
.copy-btn:hover { background: var(--sapphire-deep); color: #fff; border-color: var(--sapphire); }

/* Stats ribbon */
.stats-ribbon {
    display: grid; grid-template-columns: repeat(auto-fit, minmax(200px, 1fr)); gap: 1rem;
    margin: 3.5rem 0 2rem;
}
.stat-pill {
    background: var(--bg-card); border: 1px solid var(--border); border-radius: 14px;
    padding: 1.25rem 1rem; text-align: center;
}
.stat-val {
    font-size: 2rem; font-weight: 900; color: #fff;
    background: linear-gradient(135deg, #fff, var(--sapphire));
    -webkit-background-clip: text; -webkit-text-fill-color: transparent;
}
.stat-lbl { font-size: 0.85rem; color: var(--muted); font-weight: 600; text-transform: uppercase; letter-spacing: 0.05em; margin-top: 0.3rem; }

/* Grids and Cards */
.grid-2 { display: grid; grid-template-columns: repeat(auto-fit, minmax(460px, 1fr)); gap: 1.5rem; }
.grid-3 { display: grid; grid-template-columns: repeat(auto-fit, minmax(320px, 1fr)); gap: 1.5rem; }
.grid-4 { display: grid; grid-template-columns: repeat(auto-fit, minmax(260px, 1fr)); gap: 1.25rem; margin: 2.5rem 0; }
.card {
    background: var(--bg-card); border: 1px solid var(--border);
    border-radius: 16px; padding: 1.75rem; transition: all 0.2s ease;
}
.card:hover {
    background: var(--bg-card-hover); border-color: rgba(56, 189, 248, 0.35);
    transform: translateY(-2px); box-shadow: 0 8px 30px rgba(0, 0, 0, 0.4);
}
.card-icon {
    font-size: 1.75rem; margin-bottom: 0.75rem; display: inline-block;
}
.card h3 { font-size: 1.25rem; margin-bottom: 0.65rem; color: #fff; }
.card p { color: var(--muted); font-size: 0.95rem; line-height: 1.6; }

/* Code Blocks & Tabs */
pre {
    background: var(--bg-code); border: 1px solid var(--border);
    border-radius: 12px; padding: 1.2rem 1.4rem; overflow-x: auto;
    font-family: "JetBrains Mono", "SFMono-Regular", Menlo, Consolas, monospace;
    font-size: 0.87rem; line-height: 1.6; color: #e2e8f0;
}
code {
    font-family: "JetBrains Mono", "SFMono-Regular", Menlo, Consolas, monospace;
    font-size: 0.88em; background: rgba(30, 41, 59, 0.7);
    padding: 0.15rem 0.45rem; border-radius: 6px; color: #7dd3fc;
}
pre code { background: none; padding: 0; color: inherit; }
.code-header {
    font-size: 0.78rem; font-weight: 700; text-transform: uppercase; letter-spacing: 0.06em;
    color: var(--muted); margin-bottom: 0.5rem; display: flex; justify-content: space-between;
}
.code-header span.tag-cf { color: var(--sapphire); }
.code-header span.tag-cpp { color: var(--fire); }

.section-title {
    font-size: 2.1rem; font-weight: 850; margin: 4rem 0 1rem; text-align: center;
    letter-spacing: -0.02em; color: #fff;
}
.section-subtitle {
    text-align: center; color: var(--muted); font-size: 1.08rem; max-width: 680px; margin: 0 auto 2.5rem;
}

.tabs { display: flex; gap: 0.5rem; margin-bottom: 1.25rem; justify-content: center; flex-wrap: wrap; }
.tab-btn {
    background: var(--bg-card); color: var(--muted); border: 1px solid var(--border);
    padding: 0.65rem 1.25rem; border-radius: 10px; cursor: pointer; font-weight: 700; font-size: 0.92rem;
    transition: all 0.15s;
}
.tab-btn:hover { color: #fff; border-color: #475569; }
.tab-btn.active {
    background: rgba(56, 189, 248, 0.18); color: #fff; border-color: var(--sapphire);
    box-shadow: 0 0 15px rgba(56, 189, 248, 0.2);
}
.tab-panel { display: none; }
.tab-panel.active { display: block; }

/* Console execution preview */
.console-preview {
    background: #020408; border: 1px solid #1e293b; border-radius: 10px;
    padding: 0.85rem 1.1rem; margin-top: 1rem; font-family: "JetBrains Mono", monospace;
    font-size: 0.84rem; color: #4ade80;
}
.console-header {
    font-size: 0.72rem; font-weight: 700; color: #64748b; text-transform: uppercase;
    letter-spacing: 0.05em; margin-bottom: 0.35rem; display: flex; align-items: center; gap: 0.4rem;
}
.console-dot { width: 8px; height: 8px; border-radius: 50%; background: #22c55e; }

/* Comparison Table */
table.comp-table {
    width: 100%; border-collapse: collapse; margin: 1.5rem 0 3rem;
    background: var(--bg-card); border: 1px solid var(--border); border-radius: 14px; overflow: hidden;
}
table.comp-table th, table.comp-table td {
    text-align: left; padding: 1rem 1.2rem; border-bottom: 1px solid var(--border); font-size: 0.92rem;
}
table.comp-table th { background: rgba(15, 23, 42, 0.8); color: #fff; font-weight: 700; font-size: 0.95rem; }
table.comp-table tr:last-child td { border-bottom: none; }
table.comp-table tr:hover td { background: rgba(56, 189, 248, 0.04); }

/* Quickstart 3-step row */
.step-grid {
    display: grid; grid-template-columns: repeat(auto-fit, minmax(320px, 1fr)); gap: 1.5rem; margin: 2rem 0;
}
.step-card {
    background: var(--bg-card); border: 1px solid var(--border); border-radius: 16px;
    padding: 1.75rem; position: relative;
}
.step-badge {
    position: absolute; top: -14px; left: 1.5rem;
    background: linear-gradient(135deg, var(--sapphire-deep), var(--fire));
    color: #fff; font-weight: 900; font-size: 0.82rem; padding: 0.2rem 0.75rem;
    border-radius: 999px; box-shadow: 0 4px 12px rgba(0, 0, 0, 0.5);
}
.step-card h4 { font-size: 1.2rem; margin: 0.5rem 0 0.75rem; color: #fff; }

/* Guide Roadmap */
.roadmap-grid {
    display: grid; grid-template-columns: repeat(auto-fit, minmax(260px, 1fr)); gap: 1.25rem; margin: 2rem 0;
}
.roadmap-card {
    background: var(--bg-card); border: 1px solid var(--border); border-radius: 14px;
    padding: 1.35rem; transition: all 0.2s ease; text-decoration: none; color: inherit; display: block;
}
.roadmap-card:hover {
    background: var(--bg-card-hover); border-color: var(--sapphire);
    transform: translateY(-2px); text-decoration: none; color: inherit;
}
.roadmap-num {
    font-size: 0.78rem; font-weight: 800; text-transform: uppercase; letter-spacing: 0.06em;
    color: var(--sapphire); margin-bottom: 0.4rem;
}
.roadmap-card h4 { font-size: 1.1rem; color: #fff; margin-bottom: 0.4rem; }
.roadmap-card p { font-size: 0.88rem; color: var(--muted); line-height: 1.5; margin: 0; }

.footer {
    text-align: center; padding: 3.5rem 1rem 2rem; border-top: 1px solid var(--border);
    color: var(--muted); font-size: 0.92rem; margin-top: 5rem;
}
.footer-logo {
    max-width: 140px; margin-bottom: 1rem; filter: drop-shadow(0 2px 8px rgba(56, 189, 248, 0.3));
}
.footer-links { display: flex; justify-content: center; gap: 2rem; margin: 1.25rem 0; flex-wrap: wrap; }
.footer-links a { color: var(--muted); font-weight: 600; font-size: 0.92rem; }
.footer-links a:hover { color: #fff; text-decoration: none; }

@media (max-width: 900px) {
    .grid-2 { grid-template-columns: 1fr; }
    h1.hero-title { font-size: 2.5rem; }
    .navbar { padding: 0.85rem 1rem; flex-wrap: wrap; gap: 0.75rem; }
    .nav-links { gap: 1rem; font-size: 0.88rem; }
}
)CSS";
}

[[nodiscard]] inline auto navbar_html(std::string_view active_page) -> std::string {
    return std::format(R"HTML(
<nav class="navbar">
  <a class="brand" href="index.html">
    <img src="assets/cfire_logo_emblem.png" alt="cfire logo" class="brand-icon" />
    <span class="brand-text">c<span class="fire">fire</span></span>
  </a>
  <div class="nav-links">
    <a href="index.html" class="{}">Overview</a>
    <a href="guide.html" class="{}">Language Guide</a>
    <a href="reference.html" class="{}">Stdlib Reference</a>
    <a href="guide.html#chapter-1" class="btn-nav-primary">Quickstart &rarr;</a>
  </div>
</nav>
)HTML",
        active_page == "index" ? "active" : "",
        active_page == "guide" ? "active" : "",
        active_page == "reference" ? "active" : "");
}

} // namespace detail

[[nodiscard]] inline auto html_escape(std::string_view s) -> std::string {
    std::string out;
    out.reserve(s.size() + 32);
    for (char c : s) {
        switch (c) {
            case '&': out += "&amp;"; break;
            case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break;
            case '"': out += "&quot;"; break;
            default:  out += c; break;
        }
    }
    return out;
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
    <div class="grid-2">
      <div>
        <div class="code-header"><span class="tag-cf">cfire (.cf)</span><span>Call-site Intent Safety</span></div>
        <pre><code>)HTML";
    out += html_escape(snip1_cf);
    out += R"HTML(</code></pre>
      </div>
      <div>
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
    <div class="grid-2">
      <div>
        <div class="code-header"><span class="tag-cf">cfire (.cf)</span><span>Exhaustive Match &amp; Destructuring</span></div>
        <pre><code>)HTML";
    out += html_escape(snip2_cf);
    out += R"HTML(</code></pre>
      </div>
      <div>
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
    <div class="grid-2">
      <div>
        <div class="code-header"><span class="tag-cf">cfire (.cf)</span><span>Modern Concurrency &amp; WebSockets</span></div>
        <pre><code>)HTML";
    out += html_escape(snip3_cf);
    out += R"HTML(</code></pre>
      </div>
      <div>
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
    <div class="grid-2">
      <div>
        <div class="code-header"><span class="tag-cf">cfire (.cf)</span><span>Compile-Time SIMD Struct Tags</span></div>
        <pre><code>)HTML";
    out += html_escape(snip4_cf);
    out += R"HTML(</code></pre>
      </div>
      <div>
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
    <div class="grid-2">
      <div>
        <div class="code-header"><span class="tag-cf">cfire (.cf)</span><span>Direct C/C++ Header Ingestion</span></div>
        <pre><code>)HTML";
    out += html_escape(snip5_cf);
    out += R"HTML(</code></pre>
      </div>
      <div>
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
      <a class="btn btn-secondary" href="guide.html">&#128214; Read the 8-Chapter Guide</a>
      <a class="btn btn-secondary" href="reference.html">&#9889; Explore 13 Stdlib Modules</a>
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
      <h4>5-Minute Tour &amp; Quickstart</h4>
      <p>Compiler architecture, project layout, CLI driver commands, and your first programs.</p>
    </a>
    <a class="roadmap-card" href="guide.html#chapter-2">
      <div class="roadmap-num">Chapter 2</div>
      <h4>Syntax, Types &amp; Intents</h4>
      <p>Primitives, compound types, <code>in</code>, <code>mut</code>, <code>out</code>, <code>move</code>, and exclusivity rules.</p>
    </a>
    <a class="roadmap-card" href="guide.html#chapter-3">
      <div class="roadmap-num">Chapter 3</div>
      <h4>Algebraic Enums &amp; Match</h4>
      <p>Tagged unions, pattern matching, slice destructuring (<code>[head, ..tail]</code>), and exhaustiveness.</p>
    </a>
    <a class="roadmap-card" href="guide.html#chapter-4">
      <div class="roadmap-num">Chapter 4</div>
      <h4>Zero-Cost C++ Interop</h4>
      <p>Direct Clang AST ingestion, C++ STL lowering, automatic boundary coercion, and CMake ejection.</p>
    </a>
    <a class="roadmap-card" href="guide.html#chapter-5">
      <div class="roadmap-num">Chapter 5</div>
      <h4>Error Handling &amp; Definite Return</h4>
      <p>Fallible types (<code>T!</code>), early exit (<code>try</code>), fallback divergence (<code>or err { ... }</code>), and <code>defer</code>.</p>
    </a>
    <a class="roadmap-card" href="guide.html#chapter-6">
      <div class="roadmap-num">Chapter 6</div>
      <h4>Concurrency &amp; Contexts</h4>
      <p>Thread-safe channels, cancellation Contexts, timeout deadlines, and async scopes.</p>
    </a>
    <a class="roadmap-card" href="guide.html#chapter-7">
      <div class="roadmap-num">Chapter 7</div>
      <h4>Standard Library Catalog</h4>
      <p>Deep-dive into 13 <code>core:*</code> modules: WebSockets, HTTP, SQLite, Zstd, Crypto, and JSON.</p>
    </a>
    <a class="roadmap-card" href="guide.html#chapter-8">
      <div class="roadmap-num">Chapter 8</div>
      <h4>Build System &amp; Remote Packages</h4>
      <p>Declarative <code>build.cf</code>, Git remote dependencies, offline vendoring, and compiler caching.</p>
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

> **Sapphire on Fire (`cfire`)** is a statically typed systems programming language that combines Go's readability and fast onboarding, Odin's batteries-included `core:*` library catalog, and Rust-inspired explicit mutation/move safety—compiling 1-to-1 into human-readable **ISO C++23**.

---

## Chapter 1: Philosophy & Architecture

### Why Transpile 1-to-1 to ISO C++23?
Most modern C++ successors fail in production for one of two reasons:
1. **The Interop Wall**: Languages with custom object layouts or opaque runtimes require expensive FFI marshaling to talk to C and C++ libraries.
2. **The Lock-In Risk**: Adopting a new language is risky if your team cannot easily debug, profile, or migrate the codebase using standard tools.

`cfire` solves both by treating **modern ISO C++23 as an ergonomic, zero-wrapper target**:
- Every `type S struct` lowers to a standard C++23 aggregate `struct S`.
- Every `str` is `std::string` (owned) or `std::string_view` (`in` parameter borrow).
- Every `[]T` slice is `std::vector<T>` (owned) or `std::span<const T>` (`in` parameter borrow).
- Every fallible return `T!` is `std::expected<T, cfire::Error>`.
- At any moment, `cfire eject` exports your project into a standalone C++23 + `CMakeLists.txt` repository with zero lock-in.

---

## Chapter 2: Tour of the Toolchain

`cfire` ships as a single, self-contained C++23 binary containing the compiler, build system, test runner, formatter, language server, and documentation generator:

```zsh
# 1. Run a program directly (auto-links core/vendor/C++ dependencies)
./cfire run examples/01-hello-core/main.cf

# 2. Build an optimized native binary
./cfire build examples/07-types-match-map/main.cf

# 3. Run built-in `test "..." { ... }` blocks
./cfire test examples/09-stdlib-test-pointers/main.cf

# 4. Canonically format `.cf` files (or verify formatting in CI with --check)
./cfire fmt examples/
./cfire fmt --check examples/

# 5. Start the Language Server Protocol (LSP) server for your editor
./cfire lsp

# 6. Generate or verify always-up-to-date docs (or inspect a single .cf file)
./cfire doc
./cfire doc --check
./cfire doc examples/09-stdlib-test-pointers/main.cf

# 7. Eject to a standalone C++23 + CMake project
./cfire eject examples/08-multi-pkg-cc-build/main.cf ./standalone_cpp
```

---

## Chapter 3: Values, Ownership, Parameter Intents & Smart Pointers

### 3.1 Immutable by Default
Local variables created with `:=` or explicit types (`x: i32 = 10`) are **immutable (`const`) by default**. Use `mut` when you intend to reassign or mutate a binding:

```cf
limit := 100              // immutable (const)
fixed: [4]i32 = [1,2,3,4] // immutable std::array<int32_t, 4>
mut counter: i32 = 0      // mutable
counter += 1
```

### 3.2 Explicit Parameter Intents (`in`, `mut`, `out`, `move`)
In C++, a function call `process(data)` hides whether `data` is read-only, mutated in-place, or moved from. `cfire` makes parameter intent explicit at **both** the declaration and the call site:

| Intent | Declaration | Call Site | C++23 Lowering | Semantic Guarantee |
|---|---|---|---|---|
| **`in` (default)** | `x: T` | `f(x)` | `const T&` / `std::string_view` / `std::span<const E>` | Read-only borrow; zero copy |
| **`mut`** | `mut x: T` | `f(mut x)` | `T&` | Requires `mut` binding & `mut` at call site (`cfire/MUT-CALLSITE`, `cfire/IMMUTABLE-MUT-ARG`) |
| **`out`** | `out x: T` | `f(out x)` | `T&` | Requires `mut` binding & `out` at call site (`cfire/OUT-CALLSITE`) |
| **`move`** | `move x: T` | `f(move x)` | `T&&` + `std::move(x)` | Rejects any subsequent use of `x` (`cfire/USE-AFTER-MOVE`) |

### 3.3 Smart Pointers (`own[T]` & `shared[T]`)
Value types and slices cover 95% of systems code. When heap allocation or shared ownership is needed, use `own[T]` (`std::unique_ptr<T>`) and `shared[T]` (`std::shared_ptr<T>`):

```cf
owned_box := make(own[Box], Box{Value: 42})
shared_box := make(shared[Box], Box{Value: 58})
total := (*owned_box).Value + (*shared_box).Value
```

---

## Chapter 4: Algebraic Data Types, Struct Tags & Exhaustive `match`

### 4.1 Structs & Compile-Time Field Tags
Struct fields support backtick metadata tags (`json:"..."`, `sql:"..."`, `cli:"..."`) inspected at compile time by `cfire` to generate zero-overhead parsers, serializers, and database row mappers:

```cf
type Metric struct {
    Service: str  `json:"service" sql:"service"`
    Count:   i64  `json:"count"   sql:"count"`
    Latency: f64  `json:"latency"`
    Healthy: bool `json:"healthy"`
}
```

### 4.2 Algebraic Enums & Exhaustive `match`
`type ... enum` declares a tagged union lowered to `std::variant`. The semantic analyzer enforces `cfire/NON-EXHAUSTIVE-MATCH` if any variant is omitted without a `_ => { ... }` fallback arm:

```cf
type Shape enum {
    Circle(radius: f64),
    Rect(width: f64, height: f64),
    Empty,
}

fn describe(s: Shape) -> str {
    mut label := "unknown"
    match s {
        Circle(r)  => { label = "circle" }
        Rect(w, h) => { label = "rect" }
        Empty      => { label = "empty" }
    }
    return label
}
```

---

## Chapter 5: Fearless Error Handling & `defer`

`cfire` eliminates C++ exceptions in favor of value-based `T!` (`std::expected<T, cfire::Error>`):

- **`try expr`**: Propagates an error immediately to the caller (or prints & exits with code `1` in `main`).
- **`expr or fallback`**: Unwraps the value or evaluates `fallback` (`std::expected::value_or`).
- **`expr or err { ... }`**: Handles the error `err` inline with a block that returns or recovers.
- **`defer expr`**: Schedules deterministic scope cleanup via `cfire::scope_exit` RAII guards.

```cf
fn load_payload(path: str) -> []byte! {
    raw := try fs.read_all(path)
    return raw
}

fn main() {
    data := load_payload("missing.bin") or err {
        println("recovered from: {}", err.message())
        return
    }
}
```

---

## Chapter 6: Structured Concurrency & Typed Channels

### 6.1 `async.scope` Structured Task Pools
`async.scope(workers: N) |scope| { ... }` creates a bounded thread-pool scope that automatically joins all spawned tasks (`scope.spawn || { ... }`) before exiting the block—preventing dangling background threads:

```cf
mut counter := atomic(0u64)
async.scope(workers: 4) |scope| {
    for _, item in items {
        scope.spawn || {
            counter.fetch_add(1)
        }
    }
}
```

### 6.2 `core:sync` Typed Channels (`Channel[T]`)
Combine `async.scope` with `sync.make_channel[T]()` for message-passing pipelines:

```cf
import "core:sync"

ch := sync.make_channel[i64]()
async.scope(workers: 2) |scope| {
    scope.spawn || {
        ch.send(777)
    }
}
val := ch.recv() or 0
```

---

## Chapter 7: Zero-Friction C & C++ Interop

1. **Direct Header Imports (`import "cpp:..."` and `import "c:..."`)**:
   `cfire` invokes `clang++ -Xclang -ast-dump=json` on imported C/C++ headers, caches the extracted function signatures in `.cfire/cache/ast/<hash>.idx`, and automatically inserts `cfire::c_str(arg)` when passing a `cfire` `str` to a C `const char*` parameter.
2. **Inline `cpp { ... }` Escape Hatch**:
   Drop raw C++23 statements directly inside any `.cf` function when using intrinsics or template metaprogramming.

---

## Chapter 8: Unified Build System (`build.cf`) & CMake Ejection

When a project needs custom C/C++ static libraries, CMake subprojects, or `pkg-config` system libraries, add a declarative `build.cf` alongside `main.cf`:

```cf
package build

import "core:build"

// Pure C/C++ static library compilation driver:
var FastHash = build.CC("./native/fast_hash", {
    Sources:  ["fast_hash.c"],
    Includes: ["."],
    CFlags:   ["-O3"],
})

// CMake subproject dependency driver:
var KVStore = build.CMake("./vendor/libkv_cmake", {
    Target:   "kvstore",
    Includes: ["include"],
    Defines:  {"KV_SALT": 42},
})
```

Supported drivers include `build.CC`, `build.CMake`, `build.Make`, and `build.Ninja`. Run `./cfire build main.cf` to compile all dependencies into cached static archives (`.cfire/cache/lib/*.a`) automatically, or run `./cfire eject main.cf ./exported` to generate a standalone `CMakeLists.txt`!

---

## Chapter 9: How `cfire` Compares & Honest Design Trade-Offs

### 9.1 `cfire` vs. Cppfront (Cpp2) and Carbon
- **Cppfront (Cpp2)** is a syntax reskin of all of C++: it requires declaration-site intents (`inout`, `out`), but a call `f(x)` still hides whether `x` is borrowed or mutated, and you still write raw CMake and STL boilerplate.
- **Carbon** builds a massive custom compiler frontend to support full C++ class-hierarchy inheritance and template specialization.
- **`cfire`** takes the **TypeScript + Go/Odin approach to C++23**: it keeps the language deliberately small, enforces call-site intents (`mut`, `out`, `move`) + call-site mutable aliasing exclusivity (`cfire/MUT-ALIAS`), ships 13 `core:*` modules + `build.cf` + `fmt` + `lsp` + `doc`, and compiles 1-to-1 to standard C++23 types with a zero-lock-in `cfire eject` escape hatch.

### 9.2 `cfire` vs. Rust, Zig, and Odin
- **Vs. Zig & Odin:** Zig and Odin are outstanding C replacements, but hit a wall when consuming C++ STL/templates/classes (`std::string`, `std::vector`, `<simdjson.h>`, RocksDB, PyTorch C++). `cfire` natively shares the C++23 ABI.
- **Vs. Rust:** `cfire` captures ~85% of day-to-day bug prevention (immutable by default, explicit `mut`/`out`/`move` at call sites, `cfire/MUT-ALIAS` Law of Exclusivity at call sites, `cfire/USE-AFTER-MOVE`, `cfire/NON-EXHAUSTIVE-MATCH`, `T!` error handling) without lifetime annotations (`'a`).

### 9.3 Honest Trade-Offs & Built-In Mitigations
1. **No Full Borrow-Checker Lifetimes Across Heap Graphs:** `cfire` checks call-site mutation exclusivity (`cfire/MUT-ALIAS`) and local use-after-move (`cfire/USE-AFTER-MOVE`), but does not track reference lifetimes stored inside structs. **Mitigation:** Run `./cfire test --sanitize` or `./cfire run --sanitize` to enable Clang AddressSanitizer + UndefinedBehaviorSanitizer (`-fsanitize=address,undefined`) out of the box.
2. **Backend Compile Speed Bounded by `clang++ -std=c++23`:** Because `cfire` emits standard C++23, final binary compilation invokes `clang++`. **Mitigation:** `cfire` automatically precompiles `cfire_prelude.hpp` into a cached `.pch` precompiled header and caches Clang AST header dumps in `.cfire/cache/ast/`.
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
    out += R"HTML(
<div class="container">
  <div class="doc-layout">
    <aside class="sidebar">
      <h4>Guide Chapters</h4>
      <ul>
        <li><a href="#ch1">1. Philosophy &amp; Architecture</a></li>
        <li><a href="#ch2">2. Tour of the Toolchain</a></li>
        <li><a href="#ch3">3. Intents &amp; Smart Pointers</a></li>
        <li><a href="#ch4">4. Struct Tags, Enums &amp; Match</a></li>
        <li><a href="#ch5">5. Error Handling &amp; Defer</a></li>
        <li><a href="#ch6">6. Concurrency &amp; Channels</a></li>
        <li><a href="#ch7">7. C &amp; C++ Interop</a></li>
        <li><a href="#ch8">8. Build System &amp; Eject</a></li>
        <li><a href="#ch9">9. Comparisons &amp; Trade-Offs</a></li>
      </ul>
    </aside>
    <main>
      <h1>The `cfire` Language &amp; Systems Programming Guide</h1>
      <p class="subtitle" style="margin: 0 0 2rem;">
        A deep dive into writing safe, high-velocity systems software in <code>cfire</code> with 1-to-1 ISO C++23 lowering.
      </p>

      <h2 id="ch1">1. Philosophy &amp; Architecture</h2>
      <p>
        <code>cfire</code> ("Sapphire on Fire") is designed for engineers who want the fast, readable iteration loop of Go and Odin without sacrificing the raw performance or ecosystem of C++23. Every construct in <code>cfire</code> maps predictably to modern C++23 types—meaning zero FFI marshaling, zero hidden garbage collection, and full compatibility with <code>lldb</code>, <code>gdb</code>, Instruments, and Clang sanitizers.
      </p>

      <h2 id="ch2">2. Tour of the Toolchain</h2>
      <pre><code>./cfire run examples/01-hello-core/main.cf
./cfire run --sanitize examples/09-stdlib-test-pointers/main.cf
./cfire build examples/07-types-match-map/main.cf
./cfire test --sanitize examples/09-stdlib-test-pointers/main.cf
./cfire fmt --check examples/
./cfire lsp
./cfire doc --check
./cfire eject examples/08-multi-pkg-cc-build/main.cf ./standalone_cpp</code></pre>

      <h2 id="ch3">3. Values, Ownership, Parameter Intents &amp; Smart Pointers</h2>
      <p>
        Bindings declared with <code>:=</code> are <code>const</code> by default. Function parameters default to zero-copy read-only borrows (<code>in</code>), while <code>mut</code>, <code>out</code>, and <code>move</code> parameters require matching call-site annotations and enforce the Law of Exclusivity at call sites (<code>cfire/MUT-ALIAS</code>):
      </p>
      <pre><code>fn compute_fixed_sum(arr: [4]i32, out total: i32) {
    total = arr[0] + arr[1] + arr[2] + arr[3]
}

fn main() {
    owned_box := make(own[Box], Box{Value: 42})
    shared_box := make(shared[Box], Box{Value: 58})
    fixed_arr: [4]i32 = [10, 20, 30, 40]
    mut sum_out: i32 = 0
    compute_fixed_sum(fixed_arr, out sum_out)
}</code></pre>

      <h2 id="ch4">4. Algebraic Data Types, Struct Tags &amp; Exhaustive `match`</h2>
      <p>
        Struct tags (<code>`json:"..."`</code>, <code>`sql:"..."`</code>, <code>`cli:"..."`</code>) drive compile-time code generation for <code>core:json</code>, <code>core:db/sqlite</code>, and <code>core:cli</code>. Algebraic <code>enum</code> types lower to <code>std::variant</code> and enforce exhaustive <code>match</code> checking (<code>cfire/NON-EXHAUSTIVE-MATCH</code>).
      </p>

      <h2 id="ch5">5. Fearless Error Handling &amp; `defer`</h2>
      <p>
        Fallible functions return <code>T!</code> (<code>std::expected&lt;T, cfire::Error&gt;</code>). Callers propagate errors with <code>try</code>, provide defaults with <code>or val</code>, or recover inline with <code>or err { ... }</code>. Deterministic cleanup is handled via <code>defer</code>.
      </p>

      <h2 id="ch6">6. Structured Concurrency &amp; Typed Channels</h2>
      <p>
        Use <code>async.scope(workers: N) |scope| { ... }</code> for structured parallel task execution and <code>sync.make_channel[T]()</code> from <code>core:sync</code> for thread-safe typed message passing.
      </p>

      <h2 id="ch7">7. Zero-Friction C &amp; C++ Interop</h2>
      <p>
        When you write <code>import "c:fast_hash.h" as fh</code> or <code>import "cpp:..."</code>, <code>cfire</code> invokes Clang's JSON AST dump, caches the header index in <code>.cfire/cache/ast/</code>, and automatically inserts <code>cfire::c_str(...)</code> at <code>const char*</code> boundaries.
      </p>

      <h2 id="ch8">8. Unified Build System (`build.cf`) &amp; CMake Ejection</h2>
      <p>
        Add a <code>build.cf</code> file to compile companion C/C++ sources (<code>build.CC</code>), CMake subprojects (<code>build.CMake</code>), or system packages (<code>build.SystemLib</code>). Run <code>./cfire eject</code> at any time to generate a standalone C++23 + <code>CMakeLists.txt</code> project.
      </p>

      <h2 id="ch9">9. Comparisons &amp; Honest Design Trade-Offs</h2>
      <p>
        <strong>Vs. Cppfront &amp; Carbon:</strong> Rather than reskinning all of C++'s accidental complexity, <code>cfire</code> provides a smaller Go/Odin-inspired language with call-site mutation/move visibility, 13 built-in <code>core:*</code> modules, a unified build system, and <code>cfire eject</code>.
      </p>
      <p>
        <strong>Vs. Rust, Zig &amp; Odin:</strong> Unlike Zig and Odin (which target the C ABI), <code>cfire</code> natively speaks the ISO C++23 ABI (<code>std::string</code>, <code>std::span</code>, <code>std::expected</code>). Unlike Rust, <code>cfire</code> enforces call-site mutation exclusivity (<code>cfire/MUT-ALIAS</code>) and move tracking (<code>cfire/USE-AFTER-MOVE</code>) without lifetime annotations (<code>'a</code>), paired with first-class Clang sanitizer support (<code>--sanitize</code>) and automatic precompiled header (<code>.pch</code>) acceleration.
      </p>
    </main>
  </div>
</div>
<footer class="footer">
  Generated by <code>cfire doc</code> &bull; <a href="reference.html">Continue to the `core:*` Stdlib &amp; Diagnostics Reference &rarr;</a>
</footer>
</body>
</html>
)HTML";
    return out;
}

[[nodiscard]] inline auto generate_reference_markdown() -> std::string {
    std::string out = "# `cfire` Standard Library (`core:*`) & Diagnostics Reference\n\n";
    out += "> Auto-generated from the compiler's single-source-of-truth catalog via `./cfire doc`.\n\n";
    out += "## 1. Built-In `core:*` Standard Library Modules\n\n";
    for (const auto& mod : stdlib_catalog()) {
        out += std::format("### `{}` (`{}`) — {}\n\n", mod.import_path, mod.ns_alias, mod.title);
        out += std::format("- **Auto-Link Flags:** `{}`\n", mod.link_flags);
        out += std::format("- **Overview:** {}\n\n", mod.overview);
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

[[nodiscard]] inline auto generate_reference_html() -> std::string {
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
    out += "      <h4>`core:*` Modules</h4>\n      <ul>\n";
    for (const auto& mod : stdlib_catalog()) {
        out += std::format("        <li><a href=\"#mod-{}\"><code>{}</code></a></li>\n", mod.ns_alias, mod.import_path);
    }
    out += "      </ul>\n      <h4 style=\"margin-top:1.25rem;\">Diagnostics</h4>\n      <ul>\n";
    for (const auto& d : diagnostics_catalog()) {
        out += std::format("        <li><a href=\"#diag-{}\"><code>{}</code></a></li>\n", d.code, d.code);
    }
    out += "      </ul>\n    </aside>\n    <main>\n";
    out += "      <h1>`core:*` Standard Library &amp; Diagnostics Reference</h1>\n";
    out += "      <p class=\"subtitle\" style=\"margin:0 0 2rem;\">Automatically generated from the live <code>cfire</code> compiler metadata catalog and verified by <code>cfire doc --check</code>.</p>\n";

    for (const auto& mod : stdlib_catalog()) {
        out += std::format("      <h2 id=\"mod-{}\"><code>import \"{}\"</code> &mdash; {}</h2>\n",
            mod.ns_alias, mod.import_path, detail::html_escape(mod.title));
        out += std::format("      <p>{} <br/><small style=\"color:var(--muted)\">Auto-linked flags: <code>{}</code></small></p>\n",
            mod.overview, detail::html_escape(mod.link_flags));
        out += "      <table>\n        <thead><tr><th>`.cf` Signature</th><th>C++23 Lowering</th><th>Description &amp; Example</th></tr></thead>\n        <tbody>\n";
        for (const auto& fn : mod.functions) {
            out += std::format("          <tr><td><code>{}</code></td><td><code>{}</code></td><td>{}<br/><code>{}</code></td></tr>\n",
                detail::html_escape(fn.cf_sig),
                detail::html_escape(fn.cpp_lowering),
                detail::html_escape(fn.summary),
                detail::html_escape(fn.example));
        }
        out += "        </tbody>\n      </table>\n";
    }

    out += "      <h2 class=\"section-title\">Compiler Semantic Diagnostics</h2>\n";
    for (const auto& d : diagnostics_catalog()) {
        out += std::format("      <div class=\"card\" id=\"diag-{}\" style=\"margin-bottom:1.25rem;\">\n", d.code);
        out += std::format("        <h3><code>[{}]</code> &mdash; {}</h3>\n", d.code, detail::html_escape(d.title));
        out += std::format("        <p style=\"margin-bottom:0.85rem;\">{}</p>\n", detail::html_escape(d.rule));
        out += "        <div class=\"grid-2\">\n";
        out += std::format("          <div><div class=\"code-header\"><span style=\"color:#f87171\">Rejected (.cf)</span></div><pre><code>{}</code></pre></div>\n",
            detail::html_escape(d.bad_example));
        out += std::format("          <div><div class=\"code-header\"><span style=\"color:#34d399\">Accepted (.cf)</span></div><pre><code>{}</code></pre></div>\n",
            detail::html_escape(d.good_example));
        out += "        </div>\n      </div>\n";
    }

    out += "    </main>\n  </div>\n</div>\n";
    out += "<footer class=\"footer\">Generated by <code>cfire doc</code> &bull; Always kept in sync with <code>src/docgen.hpp</code>.</footer>\n";
    out += "</body>\n</html>\n";
    return out;
}

// Extracts Markdown API documentation for a specific `.cf` file (including doc comments and lowered C++23 signatures)
[[nodiscard]] inline auto extract_file_markdown_doc(const std::filesystem::path& cf_file)
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

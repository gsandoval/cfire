#pragma once
// src/docgen/catalog.hpp — Single-Source-Of-Truth Metadata Catalog (`core:*` Stdlib & Semantic Diagnostics)

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
            "Computes fast 256-bit cryptographic hex digests over byte slices (convenience alias for `core:crypto/sha256`).",
            {
                {"hash_hex", "fn hash_hex(data: []byte) -> str", "cfire::blake3::hash_hex(data)", "Returns a 64-character lowercase hex digest of `data`.", "digest := blake3.hash_hex(raw)"}
            }
        },
        {
            "core:net", "net", "POSIX BSD TCP Sockets & Loopback Networking", "(POSIX sockets)",
            "RAII TCP client and listener sockets backed by BSD `AF_INET` sockets with non-blocking `poll` timeouts and automatic `AF_UNIX` `socketpair` loopback fallback inside sandboxes.",
            {
                {"listen_tcp", "fn listen_tcp(port: u16 = 0) -> TcpListener!", "cfire::net::listen_tcp(port)", "Binds a TCP listener on `0.0.0.0:port` (`0` selects an ephemeral port).", "mut ln := try net.listen_tcp(0)"},
                {"listen_tcp_addr", "fn listen_tcp_addr(host: str, port: u16 = 0) -> TcpListener!", "cfire::net::listen_tcp_addr(host, port)", "Binds a TCP listener on a specific interface address (`host:port`).", "mut ln := try net.listen_tcp_addr(\"127.0.0.1\", 8080)"},
                {"TcpListener.port", "fn (ln: TcpListener) port() -> u16", "ln.port()", "Returns the bound TCP port.", "p := ln.port()"},
                {"TcpListener.accept", "fn (mut ln: TcpListener) accept() -> TcpConn!", "ln.accept()", "Accepts an incoming TCP connection with a safe poll timeout.", "mut conn := try ln.accept()"},
                {"connect_tcp", "fn connect_tcp(host: str, port: u16) -> TcpConn!", "cfire::net::connect_tcp(host, port)", "Connects a `TcpConn` to `host:port`.", "mut c := try net.connect_tcp(\"127.0.0.1\", p)"},
                {"TcpConn.write", "fn (mut c: TcpConn) write(payload: str) -> usize!", "c.write(payload)", "Writes bytes to an open TCP connection.", "_ := try c.write(\"PING\")"},
                {"TcpConn.read_str", "fn (mut c: TcpConn) read_str(max_bytes: usize = 1024) -> str!", "c.read_str(max_bytes)", "Reads up to `max_bytes` from the socket as a string.", "msg := try conn.read_str(64)"},
                {"TcpConn.set_timeout_ms", "fn (mut c: TcpConn) set_timeout_ms(ms: i32) -> void", "c.set_timeout_ms(ms)", "Sets socket read/write timeout in milliseconds (`0` for blocking).", "conn.set_timeout_ms(5000)"}
            }
        },
        {
            "core:net/tls", "tls", "TLS 1.2 / 1.3 & X.509 Certificate Engine", "(OpenSSL dynamic bridge)",
            "Zero-build-flag TLS 1.2/1.3 client/server encryption, SNI hostname negotiation, X.509 peer certificate verification, PEM certificate loading, and in-memory self-signed certificate generation.",
            {
                {"available", "fn available() -> bool", "cfire::tls::available()", "Returns `true` if the OpenSSL (`libssl` / `libcrypto`) runtime bridge is available.", "if tls.available() { ... }"},
                {"generate_self_signed_pem", "fn generate_self_signed_pem(common_name: str = \"localhost\", days: i32 = 365) -> CertKeyPair!", "cfire::tls::generate_self_signed_pem(common_name, days)", "Generates an in-memory RSA-2048 X.509v3 self-signed certificate and private key in PEM format.", "pair := try tls.generate_self_signed_pem(\"localhost\")"},
                {"wrap_client", "fn wrap_client(mut conn: TcpConn, server_name: str = \"\", verify_peer: bool = true, ca_file: str = \"\") -> void!", "cfire::tls::wrap_client(conn, server_name, verify_peer, ca_file)", "Upgrades an open `TcpConn` to a client TLS session with SNI and optional X.509 certificate verification.", "try tls.wrap_client(mut conn, \"example.com\", true)"},
                {"connect", "fn connect(host: str, port: u16 = 443, verify_peer: bool = true, ca_file: str = \"\") -> TcpConn!", "cfire::tls::connect(host, port, verify_peer, ca_file)", "Connects a TCP socket to `host:port` and completes a TLS client handshake.", "mut c := try tls.connect(\"example.com\", 443)"},
                {"ServerContext.from_files", "fn ServerContext.from_files(cert_file: str, key_file: str) -> ServerContext!", "cfire::tls::ServerContext::from_files(cert_file, key_file)", "Creates a TLS server context from X.509 certificate chain and private key PEM files on disk.", "ctx := try tls.ServerContext.from_files(\"cert.pem\", \"key.pem\")"},
                {"ServerContext.from_pem", "fn ServerContext.from_pem(cert_pem: str, key_pem: str) -> ServerContext!", "cfire::tls::ServerContext::from_pem(cert_pem, key_pem)", "Creates a TLS server context directly from in-memory certificate and private key PEM strings.", "ctx := try tls.ServerContext.from_pem(pair.cert_pem, pair.key_pem)"},
                {"ServerContext.wrap_server", "fn (ctx: ServerContext) wrap_server(mut conn: TcpConn) -> void!", "ctx.wrap_server(conn)", "Performs a server-side TLS handshake (`SSL_accept`) on an accepted `TcpConn`.", "try ctx.wrap_server(mut conn)"}
            }
        },
        {
            "core:net/url", "url", "RFC 3986 URL Parsing & URL Encoding", "(header-only)",
            "RFC 3986 URL parsing with component extraction (scheme, host, port, path, query, fragment), form-urlencoded query parameter maps, and percent encoding/decoding.",
            {
                {"parse", "fn parse(raw: str) -> Url!", "cfire::url::parse(raw)", "Parses an RFC 3986 URL string into component fields and query parameters (`https` and `wss` default to port 443).", "u := try url.parse(\"wss://stream.example.com/ws?q=1\")"},
                {"encode", "fn encode(s: str) -> str", "cfire::url::encode(s)", "Percent-encodes a string for safe use in URL path or query strings.", "enc := url.encode(\"hello world!\")"},
                {"decode", "fn decode(s: str) -> str", "cfire::url::decode(s)", "Decodes a percent-encoded URL string.", "dec := url.decode(enc)"}
            }
        },
        {
            "core:net/http", "http", "HTTP/1.1 & HTTPS Client, Wire Protocol Engine, Server & Router", "(POSIX sockets + TLS)",
            "RFC 9112 HTTP/1.1 & HTTPS client (`get`, `post`, `request`), wire parser (`parse_request`), response serializer (`Response`), multi-mode HTTP/HTTPS server (`make_server`, `make_tls_server`), and request router (`make_router`) with integrated WebSocket route upgrade (`Router.ws`).",
            {
                {"get", "fn get(url: str, headers: map[str]str = {}, verify_peer: bool = true) -> Response!", "cfire::http::get(url, headers, verify_peer)", "Executes an HTTP or HTTPS GET request against a target URL.", "resp := try http.get(\"https://127.0.0.1:8443/health\", {}, false)"},
                {"get_insecure", "fn get_insecure(url: str, headers: map[str]str = {}) -> Response!", "cfire::http::get_insecure(url, headers)", "Executes an HTTPS GET request skipping peer certificate verification (for self-signed certs).", "resp := try http.get_insecure(\"https://127.0.0.1:8443/health\")"},
                {"post", "fn post(url: str, body: str, content_type: str = \"application/json\", headers: map[str]str = {}, verify_peer: bool = true) -> Response!", "cfire::http::post(url, body, content_type, headers, verify_peer)", "Executes an HTTP or HTTPS POST request with payload body.", "resp := try http.post(\"http://127.0.0.1:8080/items\", \"{\\\"id\\\": 1}\")"},
                {"post_insecure", "fn post_insecure(url: str, body: str, content_type: str = \"application/json\", headers: map[str]str = {}) -> Response!", "cfire::http::post_insecure(url, body, content_type, headers)", "Executes an HTTPS POST request skipping peer certificate verification.", "resp := try http.post_insecure(\"https://127.0.0.1:8443/items\", \"{}\")"},
                {"request", "fn request(method: str, url: str, body: str = \"\", content_type: str = \"\", headers: map[str]str = {}, verify_peer: bool = true) -> Response!", "cfire::http::request(method, url, body, content_type, headers, verify_peer)", "Executes an arbitrary HTTP or HTTPS request (PUT, DELETE, PATCH, etc.).", "resp := try http.request(\"DELETE\", \"http://127.0.0.1:8080/items/1\")"},
                {"make_server", "fn make_server(port: u16 = 0) -> HttpServer!", "cfire::http::make_server(port)", "Creates a lightweight HTTP server listening on `port` (0 for ephemeral).", "mut srv := try http.make_server(8080)"},
                {"make_tls_server", "fn make_tls_server(port: u16, cert_file: str, key_file: str) -> HttpServer!", "cfire::http::make_tls_server(port, cert_file, key_file)", "Creates an HTTPS/WSS server backed by certificate and private key PEM files.", "mut srv := try http.make_tls_server(8443, \"cert.pem\", \"key.pem\")"},
                {"make_tls_server_pem", "fn make_tls_server_pem(port: u16, cert_pem: str, key_pem: str) -> HttpServer!", "cfire::http::make_tls_server_pem(port, cert_pem, key_pem)", "Creates an HTTPS/WSS server from in-memory certificate and private key PEM strings.", "mut srv := try http.make_tls_server_pem(0, pair.cert_pem, pair.key_pem)"},
                {"make_router", "fn make_router() -> Router", "cfire::http::make_router()", "Creates an independent HTTP & WebSocket request router supporting path parameters (`:id`) and wildcards (`*path`).", "mut r := http.make_router()"},
                {"parse_request", "fn parse_request(raw: str) -> Request!", "cfire::http::parse_request(raw)", "Parses raw HTTP/1.1 wire text into a structured Request model.", "req := try http.parse_request(raw_text)"},
                {"Response.text", "fn Response.text(code: i32, payload: str) -> Response", "cfire::http::Response::text(code, payload)", "Creates an HTTP Response with text/plain content-type and standard status reason.", "return http.Response.text(200, \"OK\")"},
                {"Response.json", "fn Response.json(code: i32, json_str: str) -> Response", "cfire::http::Response::json(code, json_str)", "Creates an HTTP Response with application/json content-type.", "return http.Response.json(200, \"{\\\"status\\\":\\\"ok\\\"}\")"},
                {"Response.html", "fn Response.html(code: i32, html: str) -> Response", "cfire::http::Response::html(code, html)", "Creates an HTTP Response with text/html content-type.", "return http.Response.html(200, \"<h1>Hello</h1>\")"},
                {"Response.redirect", "fn Response.redirect(location: str, code: i32 = 302) -> Response", "cfire::http::Response::redirect(location, code)", "Creates an HTTP redirect Response setting Location header.", "return http.Response.redirect(\"/login\", 302)"},
                {"Router.get", "fn (mut r: Router) get(pattern: str, handler: fn(Request) -> Response) -> Router&", "r.get(pattern, handler)", "Registers an HTTP GET route with optional path parameters (`:id`) or wildcards (`*path`).", "r.get(\"/users/:id\", |req| http.Response.text(200, req.param(\"id\")))"},
                {"Router.post", "fn (mut r: Router) post(pattern: str, handler: fn(Request) -> Response) -> Router&", "r.post(pattern, handler)", "Registers an HTTP POST route handler.", "r.post(\"/items\", |req| http.Response.json(201, \"{}\"))"},
                {"Router.ws", "fn (mut r: Router) ws(pattern: str, handler: fn(Request, mut websocket::Conn) -> void) -> Router&", "r.ws(pattern, handler)", "Registers a WebSocket route that automatically upgrades matching HTTP requests and invokes `handler(req, mut ws)`.", "r.ws(\"/chat/:room\", |req, mut ws| { ... })"},
                {"Router.dispatch", "fn (r: Router) dispatch(req: Request) -> Response", "r.dispatch(req)", "Dispatches an HTTP request through the router in-memory without open network sockets.", "res := router.dispatch(test_req)"},
                {"Router.use", "fn (mut r: Router) use(mw: fn(mut Request) -> Response?) -> Router&", "r.use(mw)", "Registers pre-routing middleware that can short-circuit or validate HTTP and WebSocket upgrade requests.", "r.use(|req| { return None })"},
                {"HttpServer.enable_tls_files", "fn (mut s: HttpServer) enable_tls_files(cert_file: str, key_file: str) -> void!", "s.enable_tls_files(cert_file, key_file)", "Enables TLS on the server using certificate and private key PEM files.", "try srv.enable_tls_files(\"cert.pem\", \"key.pem\")"},
                {"HttpServer.enable_tls_pem", "fn (mut s: HttpServer) enable_tls_pem(cert_pem: str, key_pem: str) -> void!", "s.enable_tls_pem(cert_pem, key_pem)", "Enables TLS on the server using in-memory certificate and private key PEM strings.", "try srv.enable_tls_pem(pair.cert_pem, pair.key_pem)"},
                {"HttpServer.serve_in_background", "fn (mut s: HttpServer) serve_in_background(handler: fn(Request) -> Response | Router) -> void", "s.serve_in_background(handler)", "Starts a non-blocking background accept loop serving HTTP and WebSocket upgrade requests.", "server.serve_in_background(router)"},
                {"HttpServer.stop", "fn (mut s: HttpServer) stop() -> void", "s.stop()", "Gracefully closes the listening socket and stops worker threads.", "server.stop()"},
                {"Request.param", "fn (req: Request) param(key: str, fallback: str = \"\") -> str", "req.param(key, fallback)", "Retrieves a named route path parameter extracted by the router.", "id := req.param(\"id\")"},
                {"Request.query", "fn (req: Request) query(key: str, fallback: str = \"\") -> str", "req.query(key, fallback)", "Retrieves a decoded URL query parameter.", "page := req.query(\"page\", \"1\")"},
                {"Request.header", "fn (req: Request) header(key: str, fallback: str = \"\") -> str", "req.header(key, fallback)", "Performs a case-insensitive header lookup.", "auth := req.header(\"Authorization\")"}
            }
        },
        {
            "core:net/websocket", "websocket", "RFC 6455 High-Level WebSockets & WSS Client & Server", "(POSIX sockets + TLS)",
            "RFC 6455 full-duplex `ws://` and `wss://` WebSocket client (`connect`, `connect_insecure`), standalone server (`make_server`, `make_tls_server`, `make_tls_server_pem`), and connection upgrade handler (`upgrade`) with automated framing, client masking, and Ping/Pong management.",
            {
                {"connect", "fn connect(url: str, verify_peer: bool = true) -> Conn!", "cfire::websocket::connect(url, verify_peer)", "Establishes a `ws://` or `wss://` WebSocket connection and completes the RFC 6455 handshake.", "mut ws := try websocket.connect(\"ws://127.0.0.1:8080/ws\")"},
                {"connect_insecure", "fn connect_insecure(url: str) -> Conn!", "cfire::websocket::connect_insecure(url)", "Establishes a `wss://` WebSocket connection skipping peer certificate verification (for self-signed certs).", "mut ws := try websocket.connect_insecure(\"wss://127.0.0.1:8443/ws\")"},
                {"make_server", "fn make_server(port: u16 = 0) -> Server!", "cfire::websocket::make_server(port)", "Creates a standalone WebSocket server listening on `port` (`0` for ephemeral).", "mut srv := try websocket.make_server(0)"},
                {"make_tls_server", "fn make_tls_server(port: u16, cert_file: str, key_file: str) -> Server!", "cfire::websocket::make_tls_server(port, cert_file, key_file)", "Creates a standalone `wss://` WebSocket server backed by certificate and private key PEM files.", "mut srv := try websocket.make_tls_server(8443, \"cert.pem\", \"key.pem\")"},
                {"make_tls_server_pem", "fn make_tls_server_pem(port: u16, cert_pem: str, key_pem: str) -> Server!", "cfire::websocket::make_tls_server_pem(port, cert_pem, key_pem)", "Creates a standalone `wss://` WebSocket server from in-memory certificate and private key PEM strings.", "mut srv := try websocket.make_tls_server_pem(0, pair.cert_pem, pair.key_pem)"},
                {"upgrade", "fn upgrade(req: Request, mut conn: TcpConn) -> Conn!", "cfire::websocket::upgrade(req, conn)", "Upgrades an incoming HTTP or HTTPS connection to a WebSocket connection.", "mut ws := try websocket.upgrade(req, mut conn)"},
                {"pair", "fn pair() -> (Conn, Conn)!", "cfire::websocket::pair()", "Creates an in-process full-duplex WebSocket client/server loopback pair.", "mut (client, server) := try websocket.pair()"},
                {"Conn.send_text", "fn (mut ws: Conn) send_text(msg: str) -> void!", "ws.send_text(msg)", "Sends a UTF-8 text frame to the peer (masked if client).", "try ws.send_text(\"hello world\")"},
                {"Conn.send_binary", "fn (mut ws: Conn) send_binary(data: []byte) -> void!", "ws.send_binary(data)", "Sends a binary data frame to the peer (masked if client).", "try ws.send_binary(payload)"},
                {"Conn.recv_text", "fn (mut ws: Conn) recv_text() -> str!", "ws.recv_text()", "Receives the next text frame from the peer, automatically responding to Ping frames with Pong.", "msg := try ws.recv_text()"},
                {"Conn.recv_binary", "fn (mut ws: Conn) recv_binary() -> []byte!", "ws.recv_binary()", "Receives the next binary frame from the peer.", "bytes := try ws.recv_binary()"},
                {"Conn.ping", "fn (mut ws: Conn) ping(payload: str = \"\") -> void!", "ws.ping(payload)", "Sends a Ping control frame to check peer liveness.", "try ws.ping(\"heartbeat\")"},
                {"Conn.set_timeout_ms", "fn (mut ws: Conn) set_timeout_ms(ms: i32) -> void", "ws.set_timeout_ms(ms)", "Sets read/write socket timeout in milliseconds (`0` for blocking).", "ws.set_timeout_ms(10000)"},
                {"Conn.close", "fn (mut ws: Conn) close(code: u16 = 1000, reason: str = \"\")", "ws.close(code, reason)", "Initiates a clean RFC 6455 closure handshake and closes the socket.", "ws.close()"},
                {"Conn.is_open", "fn (ws: Conn) is_open() -> bool", "ws.is_open()", "Returns true if the WebSocket connection is open.", "if ws.is_open() { ... }"},
                {"Server.accept", "fn (mut s: Server) accept() -> Conn!", "s.accept()", "Accepts one incoming connection, completes the TLS and RFC 6455 upgrade handshake, and returns a `Conn`.", "mut ws := try srv.accept()"},
                {"Server.serve_once", "fn (mut s: Server) serve_once(handler: fn(mut Conn) -> void) -> void", "s.serve_once(handler)", "Accepts and serves a single WebSocket connection with `handler`.", "srv.serve_once(|mut ws| { ... })"},
                {"Server.listen_and_serve", "fn (mut s: Server) listen_and_serve(handler: fn(mut Conn) -> void) -> void", "s.listen_and_serve(handler)", "Runs a blocking accept loop upgrading and serving WebSocket connections.", "srv.listen_and_serve(|mut ws| { ... })"},
                {"Server.serve_in_background", "fn (mut s: Server) serve_in_background(handler: fn(mut Conn) -> void) -> void", "s.serve_in_background(handler)", "Starts a background thread accepting and serving WebSocket connections.", "srv.serve_in_background(|mut ws| { ... })"},
                {"Server.stop", "fn (mut s: Server) stop() -> void", "s.stop()", "Stops the background WebSocket server thread.", "srv.stop()"}
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
    if (mod == "core:net/tls") {
        return R"CF(package main

import "core:net/tls"
import "core:net/http"

fn main() {
    // 1. Generate an in-memory RSA-2048 X.509 self-signed certificate pair
    pair := try tls.generate_self_signed_pem("localhost", 365)

    // 2. Start an HTTPS server backed by the in-memory certificate
    mut router := http.make_router()
    router.get("/secure", |req| {
        return http.Response.text(200, "TLS 1.3 Secure OK")
    })
    mut srv := try http.make_tls_server_pem(0, pair.cert_pem, pair.key_pem)
    srv.serve_in_background(router)
    defer srv.stop()
})CF";
    }
    if (mod == "core:net/websocket") {
        return R"CF(package main

import "core:net/websocket"

fn main() {
    // 1. Start a standalone WebSocket server on an ephemeral port
    mut srv := try websocket.make_server(0)
    srv.serve_in_background(|mut ws: websocket.Conn| {
        msg := ws.recv_text() or ""
        _ = ws.send_text("ECHO: " + msg)
    })
    defer srv.stop()

    // 2. Connect full-duplex RFC 6455 WebSocket client
    mut ws := try websocket.connect("ws://127.0.0.1:" + str(srv.port()) + "/stream")
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

} // namespace detail
} // namespace cfire::doc

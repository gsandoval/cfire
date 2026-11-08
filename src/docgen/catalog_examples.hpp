#pragma once
// src/docgen/catalog_examples.hpp — Practical Runnable Cfire Code Examples per Standard Library Module

#include <string_view>

namespace cfire::doc::detail {

[[nodiscard]] inline auto module_practical_example(std::string_view mod) -> std::string_view {
    if (mod == "core:build") {
        return R"CF(package main

import "core:build"

// Declarative desktop GUI & CLI build manifest (`build.cf`)
app := build.Executable("Flint", {
    Entry: "src/main.cf",
    Subsystem: "gui",
    OutDir: "build",
    BundleId: "dev.flint.app",
    Icon: "assets/icon.png",
    Targets: ["native"]
})

fn main() {
    println("Target OS: {}, Arch: {}", build.os(), build.arch())
})CF";
    }
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

    _ = try file.write_text("[INFO] Worker subsystem initialized\n")
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
    if (mod == "ext:compress/zstd") {
        return R"CF(package main

import "ext:compress/zstd"

fn main() {
    raw := []byte("high-throughput packet trace payload ... high-throughput packet trace payload ...")

    // Hardware-accelerated Zstandard frame compression
    compressed := try zstd.compress(raw, level: 3)
    println("Original size: {} bytes -> Compressed: {} bytes", raw.len, compressed.len)

    // Decompression using the embedded frame content size
    decompressed := try zstd.decompress(compressed)
    println("Decompressed size: {} bytes", decompressed.len)
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
    if (mod == "ext:db/sqlite") {
        return R"CF(package main

import "ext:db/sqlite"

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
    _ := try client.write_text("PING\n")

    // 3. Accept incoming connection and read data
    mut conn := try ln.accept()
    msg := try conn.read_text(max_bytes: 64)
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

fn main() {
    home := os.getenv_opt("HOME") or "/tmp"
    println("Environment $HOME: {}", home)

    // Execute external process without shell interpretation and capture stdout/exit code
    output := try os.command("git", ["rev-parse", "--short", "HEAD"])
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
            for {
                item := ch.recv() or break
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
    binary_bytes := []byte([0xCA, 0xFE, 0xBA, 0xBE])
    hex_str := hex.encode(binary_bytes)
    println("Hexadecimal string: 0x{}", hex_str)

    restored := try hex.decode(hex_str)
    println("Restored byte length: {}", restored.len)
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
    idx := sort.binary_search(numbers, 48)
    if idx != none {
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
    if (mod == "vendor:raylib") {
        return R"CF(package main

import "vendor:raylib" as rl

fn main() {
    rl.InitWindow(800, 450, "cfire + vendor:raylib")
    defer rl.CloseWindow()

    mut pos := rl.Vector2{x: 400.0, y: 225.0}
    mut score := 0
    rl.SetTargetFPS(60)

    for !rl.WindowShouldClose() {
        dt := rl.GetFrameTime()
        if rl.IsKeyDown(rl.KEY_RIGHT) {
            pos = rl.Vector2Add(pos, rl.Vector2{x: 200.0 * dt, y: 0.0})
        }

        rl.BeginDrawing()
        defer rl.EndDrawing()
        rl.ClearBackground(rl.RAYWHITE)
        rl.DrawCircleV(pos, 24.0, rl.MAROON)
        rl.DrawText("Score: {score}", 20, 20, 20, rl.DARKGRAY)
    }
})CF";
    }
    if (mod == "core:audio") {
        return R"CF(package main

import "core:audio"

fn main() {
    try audio.init(48000, 2)
    defer audio.close()
    audio.set_master_volume(0.8)

    // 1. Zero-asset procedural sound effect playback
    coin_wave := audio.gen_sfx(audio.SfxPreset::Coin)
    coin_sfx := audio.load_sound_from_wave(coin_wave)
    coin_sfx.play()

    // 2. Synthesize tone, encode/save as 16-bit RIFF WAV, and stream live DSP
    tone := audio.gen_tone(audio.Waveform::Sine, 440.0, 0.25, 0.5)
    try audio.save_wav(tone, "a440.wav")

    stream := try audio.open_stream(48000, 2)
    if stream.needs_data(2048) {
        stream.push_samples(tone.samples)
    }
    println("Audio RMS: {}, Peak: {}", audio.rms_level(), audio.peak_level())
})CF";
    }
    if (mod == "ext:ai") {
        return R"CF(package main

import "ext:ai"
import "ext:db/sqlite"
import "core:json"

type SearchArgs struct {
    query: str `json:"query" desc:"Semantic search query" validate:"non_empty"`
    limit: i32 `json:"limit" desc:"Maximum results to return" validate:"min=1,max=10"`
}

type DocHit struct {
    title: str `sql:"title" json:"title"`
    score: f64 `sql:"score" json:"score"`
}

fn main() {
    // 1. Compile-time JSON Schema reflection & validation constraints for LLM tools
    println("Tool schema: {}", json.schema[SearchArgs]())

    // 2. Parse & chunk structured documents (.pdf, .pptx, .docx, .odt, .epub, .md)
    doc := try ai.parse_document("# Systems Guide\nZero-overhead C++23 memory safety.\n# Vector SQLite\nEmbedded SIMD vector similarity search and RAG.", "guide.md")
    chunks := ai.chunk_document(doc, 256, 32)

    // 3. Stateful on-device AI client + SQLite SIMD vector similarity search
    client := ai.local()
    mut db := try sqlite.open(":memory:")
    defer db.close()
    try db.exec("CREATE TABLE docs (title TEXT, vec BLOB)")

    for ch in chunks {
        vec := try client.embed(ch.text)
        try db.exec("INSERT INTO docs VALUES (?1, ?2)", ch.title, vec)
    }

    qvec := try client.embed("vector search in sqlite")
    hits := try db.query[DocHit](
        "SELECT title, vec_cosine_similarity(vec, ?1) AS score FROM docs ORDER BY score DESC LIMIT 1",
        qvec
    )
    println("Top match: {} (score={})", hits[0].title, hits[0].score)
})CF";
    }
    if (mod == "ext:crypto/sodium") {
        return R"CF(package main

import "ext:crypto/sodium"

fn main() {
    salt := sodium.random_bytes(16)
    mut key := try sodium.argon2id("correct horse battery staple", salt, 32)
    defer sodium.memzero(mut key)

    sealed := try sodium.xchacha20poly1305_encrypt(key, []byte("top-secret-vault-payload"))
    opened := try sodium.xchacha20poly1305_decrypt(key, sealed)
    println("Decrypted bytes: {}", opened.len)
})CF";
    }
    if (mod == "core:keychain") {
        return R"CF(package main

import "core:keychain"

fn main() {
    try keychain.set("dev.flint.ssh", "vault-key", "super-secret-token")
    secret := try keychain.get("dev.flint.ssh", "vault-key")
    println("Retrieved secret length: {}", secret.len())
    try keychain.delete_password("dev.flint.ssh", "vault-key")
})CF";
    }
    if (mod == "ext:net/ssh") {
        return R"CF(package main

import "ext:net/ssh"
import "core:keychain"

fn main() {
    mut sess := try ssh.dial("10.0.0.12", 22, "deploy", 5000)
    defer sess.close()

    fp := sess.host_key_fingerprint()
    println("Host key: {fp}")

    priv_key := try keychain.get("com.example.ssh", "id_ed25519")
    try sess.auth_publickey_mem(priv_key, "")

    res := try sess.exec("uname -a")
    print(res.stdout)

    mut ch := try sess.open_pty("xterm-256color", 120, 36)
    defer ch.close()
    _ = try ch.write_text("ls -la\n")
    chunk := try ch.read_text(4096)
    print(chunk)
})CF";
    }
    if (mod == "ext:webui") {
        return R"CF(package main

import "ext:webui"

type TrackMixReq struct {
    channel: i32  `json:"channel"`
    gain_db: f64  `json:"gain_db"`
    muted:   bool `json:"muted"`
}

type TrackStatusRes struct {
    channel:      i32 `json:"channel"`
    effective_db: f64 `json:"effective_db"`
    status:       str `json:"status"`
}

fn main() {
    mut app := webui.new_app("Pocket Mastering Studio", 960, 640)
    defer app.close()

    app.serve_dir("./ui")
    app.set_dark_mode(true)

    // Bind a typed cfire struct handler callable from JS via `await window.backend.set_track({...})`
    app.bind[TrackMixReq]("set_track") |req| {
        mut eff := req.gain_db
        mut label := "active"
        if req.muted {
            eff = -96.0
            label = "muted"
        }
        res := TrackStatusRes{
            channel:      req.channel,
            effective_db: eff,
            status:       label,
        }
        app.emit("meter:peak", res)
        return res
    }

    // Listen for live frontend events (`window.runtime.EventsEmit("fader:input", ...)`)
    app.on("fader:input") |payload| {
        app.set_state("last_fader", payload)
    }

    app.run()
})CF";
    }
    if (mod == "vendor:raygui") {
        return R"CF(package main

import "vendor:raylib" as rl
import "vendor:raygui" as rg

fn main() {
    rl.InitWindow(640, 360, "cfire + vendor:raygui")
    defer rl.CloseWindow()

    mut volume: f32 = 75.0
    mut vsync := true
    mut name := "Player1"
    mut edit_name := false

    for !rl.WindowShouldClose() {
        rl.BeginDrawing()
        defer rl.EndDrawing()
        rl.ClearBackground(rl.RAYWHITE)

        rg.GuiWindowBox(rl.Rectangle{x: 40.0, y: 30.0, width: 320.0, height: 220.0}, "Settings")
        rg.GuiCheckBox(rl.Rectangle{x: 60.0, y: 70.0, width: 20.0, height: 20.0}, "Enable VSync", &mut vsync)
        rg.GuiSliderBar(rl.Rectangle{x: 100.0, y: 110.0, width: 180.0, height: 20.0}, "Vol", "{volume}", &mut volume, 0.0, 100.0)
        if rg.GuiTextBox(rl.Rectangle{x: 60.0, y: 150.0, width: 220.0, height: 28.0}, &mut name, 64, edit_name) != 0 {
            edit_name = !edit_name
        }
    }
})CF";
    }
    return "";
}

} // namespace cfire::doc::detail

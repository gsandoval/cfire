#pragma once
// src/docgen/catalog_types.hpp — Lightweight Metadata Catalog Types & Static Tables (`stdlib_catalog` & `diagnostics_catalog`)

#include "catalog_type_docs.hpp"
#include "sema/builtin_methods.hpp"
#include <algorithm>
#include <format>
#include <string>
#include <vector>

namespace cfire::doc {

struct StdFnDoc {
    std::string name;
    std::string cf_sig;
    std::string cpp_lowering;
    std::string summary;
    std::string example;
    std::vector<std::size_t> escaping_param_indices{};
};

struct StdModuleDoc {
    std::string import_path; // e.g. "core:fs"
    std::string ns_alias;    // e.g. "fs"
    std::string title;
    std::string link_flags;
    std::string overview;
    std::vector<StdFnDoc> functions;
    std::vector<StdTypeDoc> type_docs{};  // CF-188: one per name in `types`, filled from catalog_type_docs.hpp

    // Authoritative unified metadata for compilation, sema, module registry & codegen:
    std::string header_path{};
    std::string core_key{};
    std::vector<std::string> extra_core_keys{};
    std::vector<std::string> types{};
    std::vector<std::string> defines{};
    std::vector<std::string> link_tags{};
    std::vector<std::string> aliases{};
    bool is_vendor = false;
    std::vector<std::string> implied_vendors{};
};

struct DiagnosticDoc {
    std::string code;
    std::string title;
    std::string rule;
    std::string bad_example;
    std::string good_example;
};

inline void apply_module_metadata(StdModuleDoc& m) {
    if (m.import_path == "core:audio") {
        m.header_path = "core/audio.hpp";
        m.core_key = "audio";
        m.types = {"Waveform", "SfxPreset", "Wave", "Sound", "Stream"};
    } else if (m.import_path == "core:build") {
        m.core_key = "buildsys";
        m.header_path = "core/buildsys.hpp";
        m.defines = {"-DCFIRE_CORE_BUILDSYS=1"};
        m.aliases = {"core:buildsys"};
        m.types = {"CC", "CMake", "Make", "Git", "Archive", "Command", "Executable", "Library", "Step", "PkgConfig", "Link", "Include", "Tako", "Target", "StepTarget"};
    } else if (m.import_path == "core:cli") {
        m.header_path = "core/cli.hpp";
        m.core_key = "cli";
    } else if (m.import_path == "core:collections") {
        m.header_path = "core/collections.hpp";
        m.core_key = "collections";
        m.types = {"Set", "Deque", "PriorityQueue", "MinQueue"};
    } else if (m.import_path == "core:crypto/aes") {
        m.header_path = "core/crypto.hpp";
        m.core_key = "crypto";
        m.types = {"GcmResult"};
        m.aliases = {"core:aes"};
    } else if (m.import_path == "core:crypto/blake3") {
        m.header_path = "core/crypto.hpp";
        m.core_key = "crypto";
        m.aliases = {"core:blake3"};
    } else if (m.import_path == "core:crypto/hmac") {
        m.header_path = "core/crypto.hpp";
        m.core_key = "crypto";
        m.aliases = {"core:hmac"};
    } else if (m.import_path == "core:crypto/sha256") {
        m.header_path = "core/crypto.hpp";
        m.core_key = "crypto";
        m.aliases = {"core:sha256", "core:crypto"};
    } else if (m.import_path == "core:encoding/base64") {
        m.header_path = "core/encoding.hpp";
        m.core_key = "encoding";
        m.aliases = {"core:base64"};
    } else if (m.import_path == "core:encoding/csv") {
        m.header_path = "core/encoding.hpp";
        m.core_key = "encoding";
        m.aliases = {"core:csv"};
    } else if (m.import_path == "core:encoding/hex") {
        m.header_path = "core/encoding.hpp";
        m.core_key = "encoding";
        m.aliases = {"core:hex", "core:encoding"};
    } else if (m.import_path == "core:fs") {
        m.header_path = "core/fs.hpp";
        m.core_key = "fs";
        m.types = {"OpenMode", "SeekWhence", "File"};
    } else if (m.import_path == "core:json") {
        m.header_path = "core/json.hpp";
        m.core_key = "json";
        m.types = {"Value"};
        m.defines = {"#define CFIRE_USE_SIMDJSON 1"};
        m.link_tags = {"-lsimdjson"};
    } else if (m.import_path == "core:keychain") {
        m.header_path = "core/keychain.hpp";
        m.core_key = "keychain";
        m.aliases = {"core:os/keychain"};
    } else if (m.import_path == "core:log") {
        m.header_path = "core/log.hpp";
        m.core_key = "log";
        m.types = {"Level", "Format"};
    } else if (m.import_path == "core:math") {
        m.header_path = "core/math.hpp";
        m.core_key = "math";
    } else if (m.import_path == "core:net") {
        m.header_path = "core/net.hpp";
        m.core_key = "net";
        m.types = {"TcpListener", "TcpConn"};
    } else if (m.import_path == "core:net/http") {
        m.header_path = "core/net.hpp";
        m.core_key = "net";
        m.types = {"Request", "Response", "Router", "HttpServer", "Server", "SseEvent", "ServerLimits", "TlsConfig"};
        m.aliases = {"core:http"};
    } else if (m.import_path == "core:net/tls") {
        m.header_path = "core/net.hpp";
        m.core_key = "net";
        m.types = {"ServerContext", "CertKeyPair"};
        m.aliases = {"core:tls"};
    } else if (m.import_path == "core:net/url") {
        m.header_path = "core/net.hpp";
        m.core_key = "net";
        m.types = {"Url"};
        m.aliases = {"core:url"};
    } else if (m.import_path == "core:net/websocket") {
        m.header_path = "core/net.hpp";
        m.core_key = "net";
        m.types = {"Conn", "Server", "TlsConfig"};
        m.aliases = {"core:websocket"};
    } else if (m.import_path == "core:os") {
        m.header_path = "core/os.hpp";
        m.core_key = "os";
        m.extra_core_keys = {"fs"};
        m.types = {"ExecResult", "TermSize", "RawTerminal"};
    } else if (m.import_path == "core:rand") {
        m.header_path = "core/math.hpp";
        m.core_key = "math";
    } else if (m.import_path == "core:regex") {
        m.header_path = "core/regex.hpp";
        m.core_key = "regex";
        m.types = {"Regex"};
    } else if (m.import_path == "core:sync") {
        m.header_path = "core/sync.hpp";
        m.core_key = "sync";
        m.types = {"Mutex", "Channel", "Context", "Atomic", "MutexGuard"};
    } else if (m.import_path == "core:sync/context") {
        m.header_path = "core/sync.hpp";
        m.core_key = "sync";
        m.types = {"Context"};
        m.aliases = {"core:context"};
    } else if (m.import_path == "core:tako") {
        m.header_path = "core/tako.hpp";
        m.core_key = "tako";
        m.extra_core_keys = {"json"};
        m.defines = {"#define CFIRE_USE_SIMDJSON 1"};
        m.link_tags = {"-lsimdjson"};
        m.aliases = {"ext:tako"};
    } else if (m.import_path == "core:time") {
        m.header_path = "core/time.hpp";
        m.core_key = "time";
        m.types = {"Duration", "Time"};
    } else if (m.import_path == "core:unicode") {
        m.header_path = "core/unicode.hpp";
        m.core_key = "unicode";
    } else if (m.import_path == "ext:ai") {
        m.header_path = "core/ai.hpp";
        m.core_key = "ai";
        m.types = {"Provider", "Client", "Agent", "Document", "Message", "Tool", "McpServer", "ZipEntry", "Attachment", "ChatResponse", "DocumentSection", "ToolCall"};
        m.defines = {"#define CFIRE_USE_SIMDJSON 1"};
        m.link_tags = {"-lsimdjson"};
        m.aliases = {"ext:ai/agent", "ext:ai/client", "ext:ai/documents", "ext:ai/vectors", "ext:ai/vision"};
    } else if (m.import_path == "ext:compress/zstd") {
        m.header_path = "core/compress.hpp";
        m.core_key = "compress";
        m.defines = {"#define CFIRE_USE_ZSTD 1"};
        m.link_tags = {"-lzstd"};
        m.aliases = {"ext:compress", "ext:zstd", "core:compress", "core:zstd", "core:compress/zstd"};
    } else if (m.import_path == "ext:crypto/sodium") {
        m.header_path = "core/sodium.hpp";
        m.core_key = "sodium";
        m.defines = {"#define CFIRE_USE_SODIUM 1"};
        m.link_tags = {"@pkg:libsodium"};
        m.aliases = {"ext:sodium", "core:sodium", "core:crypto/sodium"};
    } else if (m.import_path == "ext:db/sqlite") {
        m.header_path = "core/sqlite.hpp";
        m.core_key = "sqlite";
        m.types = {"Conn"};
        m.defines = {"#define CFIRE_USE_SQLITE 1"};
        m.link_tags = {"-lsqlite3"};
        m.aliases = {"ext:sqlite", "core:sqlite", "core:db/sqlite"};
    } else if (m.import_path == "ext:net/ssh") {
        m.header_path = "core/ssh.hpp";
        m.core_key = "ssh";
        m.types = {"Session", "Channel", "ExecResult", "SftpEntry"};
        m.link_tags = {"@pkg:libssh"};
        m.aliases = {"ext:ssh", "core:ssh", "core:net/ssh"};
    } else if (m.import_path == "ext:webui") {
        m.header_path = "core/webui.hpp";
        m.core_key = "webui";
        m.extra_core_keys = {"json"};
        m.types = {"App"};
        m.defines = {"#define CFIRE_USE_SIMDJSON 1"};
        m.link_tags = {"-lsimdjson"};
        m.aliases = {"core:webui"};
    }
}

[[nodiscard]] inline auto stdlib_catalog() -> const std::vector<StdModuleDoc>& {
    static const std::vector<StdModuleDoc> catalog = []() {
        std::vector<StdModuleDoc> cat = {
        {
            "core:audio", "audio", "Audio Engine, Polyphonic Mixer, Synth & WAV", "(AudioToolbox / WebAudio / ALSA)",
            "Zero-dependency multi-target audio synthesis, RIFF/WAV encoding and decoding, polyphonic `Sound` playback with pitch/pan/looping, and real-time float32 `Stream` DSP across macOS, iOS, WebAssembly, and Linux.",
            {
                {"init", "fn init(sample_rate: i32 = 48000, channels: i32 = 2) -> void!", "cfire::audio::init(sample_rate, channels)", "Initializes the hardware audio output queue (or headless software mixer in CI).", "try audio.init(48000, 2)"},
                {"close", "fn close()", "cfire::audio::close()", "Stops all active sounds and streams and releases audio hardware resources.", "defer audio.close()"},
                {"is_ready", "fn is_ready() -> bool", "cfire::audio::is_ready()", "Returns `true` if the audio subsystem is initialized.", "if audio.is_ready() { ... }"},
                {"set_master_volume", "fn set_master_volume(vol: f32)", "cfire::audio::set_master_volume(vol)", "Sets the master output volume multiplier (`0.0` to `1.0`).", "audio.set_master_volume(0.8)"},
                {"master_volume", "fn master_volume() -> f32", "cfire::audio::master_volume()", "Returns the current master output volume multiplier.", "v := audio.master_volume()"},
                {"rms_level", "fn rms_level() -> f32", "cfire::audio::rms_level()", "Returns the live master RMS amplitude (`0.0` to `1.0`) for UI audio meters.", "rms := audio.rms_level()"},
                {"peak_level", "fn peak_level() -> f32", "cfire::audio::peak_level()", "Returns the live master peak amplitude (`0.0` to `1.0`) for UI visualizers.", "pk := audio.peak_level()"},
                {"gen_tone", "fn gen_tone(kind: Waveform, freq_hz: f32, duration_sec: f32, volume: f32 = 0.5) -> Wave", "cfire::audio::gen_tone(kind, freq_hz, duration_sec, volume)", "Synthesizes a stereo waveform (`Sine`, `Square`, `Saw`, `Triangle`, `Noise`) with smooth ADSR envelope.", "w := audio.gen_tone(audio.Waveform::Sine, 440.0, 0.25, 0.5)"},
                {"gen_chirp", "fn gen_chirp(start_hz: f32, end_hz: f32, duration_sec: f32, kind: Waveform = Waveform::Sine, volume: f32 = 0.5) -> Wave", "cfire::audio::gen_chirp(start_hz, end_hz, duration_sec, kind, volume)", "Synthesizes a frequency-swept tone from `start_hz` to `end_hz`.", "sweep := audio.gen_chirp(220.0, 880.0, 0.2, audio.Waveform::Saw, 0.4)"},
                {"gen_sfx", "fn gen_sfx(preset: SfxPreset) -> Wave", "cfire::audio::gen_sfx(preset)", "Generates a zero-asset procedural sound effect (`Tap`, `Coin`, `Laser`, `Explosion`, `PowerUp`, `Jump`, `Alert`).", "coin := audio.gen_sfx(audio.SfxPreset::Coin)"},
                {"encode_wav", "fn encode_wav(wave: Wave) -> []byte", "cfire::audio::encode_wav(wave)", "Encodes an in-memory `Wave` into a 16-bit PCM RIFF/WAV byte slice.", "wav_bytes := audio.encode_wav(w)"},
                {"decode_wav", "fn decode_wav(data: []byte) -> Wave!", "cfire::audio::decode_wav(data)", "Decodes a RIFF/WAV byte slice (8/16/24-bit PCM or 32-bit float) into a `Wave`.", "w := try audio.decode_wav(wav_bytes)"},
                {"save_wav", "fn save_wav(wave: Wave, path: str) -> void!", "cfire::audio::save_wav(wave, path)", "Writes a `Wave` to disk as a standard `.wav` file.", "try audio.save_wav(w, \"tone.wav\")"},
                {"load_wav", "fn load_wav(path: str) -> Wave!", "cfire::audio::load_wav(path)", "Loads and decodes a `.wav` file from disk or app bundle.", "w := try audio.load_wav(\"tone.wav\")"},
                {"load_sound_from_wave", "fn load_sound_from_wave(wave: Wave) -> Sound", "cfire::audio::load_sound_from_wave(wave)", "Registers an in-memory `Wave` with the polyphonic mixer and returns a playable `Sound` handle.", "sfx := audio.load_sound_from_wave(coin)"},
                {"load_sound", "fn load_sound(path: str) -> Sound!", "cfire::audio::load_sound(path)", "Loads a `.wav` file from disk into a playable polyphonic `Sound` handle.", "sfx := try audio.load_sound(\"click.wav\")"},
                {"Sound.play", "fn (s: Sound) play()", "s.play()", "Triggers polyphonic playback of the sound.", "sfx.play()"},
                {"Sound.stop", "fn (s: Sound) stop()", "s.stop()", "Stops active voices playing this sound.", "sfx.stop()"},
                {"Sound.set_volume", "fn (s: Sound) set_volume(vol: f32)", "s.set_volume(vol)", "Sets playback gain (`0.0` to `2.0`) for this sound.", "sfx.set_volume(0.75)"},
                {"Sound.set_pitch", "fn (s: Sound) set_pitch(pitch: f32)", "s.set_pitch(pitch)", "Sets playback pitch/rate multiplier (`1.0` = original pitch).", "sfx.set_pitch(1.25)"},
                {"Sound.set_pan", "fn (s: Sound) set_pan(pan: f32)", "s.set_pan(pan)", "Sets constant-power stereo pan (`-1.0` left, `0.0` center, `+1.0` right).", "sfx.set_pan(-0.5)"},
                {"Sound.set_looping", "fn (s: Sound) set_looping(loop: bool)", "s.set_looping(loop)", "Enables or disables seamless loop playback.", "sfx.set_looping(true)"},
                {"open_stream", "fn open_stream(sample_rate: i32 = 48000, channels: i32 = 2) -> Stream!", "cfire::audio::open_stream(sample_rate, channels)", "Opens a real-time push `Stream` for custom DSP synthesis.", "stream := try audio.open_stream(48000, 2)"},
                {"Stream.push_samples", "fn (st: Stream) push_samples(samples: []f32)", "st.push_samples(samples)", "Pushes interleaved `f32` PCM samples into the stream's lock-free ring buffer.", "stream.push_samples(pcm_chunk)"},
                {"Stream.needs_data", "fn (st: Stream) needs_data(target_frames: i32 = 2048) -> bool", "st.needs_data(target_frames)", "Returns `true` when buffered frames fall below `target_frames`.", "if stream.needs_data(1024) { ... }"}
            }
        },
        {
            "core:build", "build", "Declarative Project & Cross-Platform Build System (`build.cf`)", "(compiler-integrated)",
            "Declarative build manifest module (`build.cf`) for configuring native, GUI, WebAssembly, and iOS executables (`build.Executable`), static/shared libraries (`build.Library`), pre-build codegen steps (`build.Step`), external CMake/Git dependencies (`build.CMake`, `build.GitDependency`), pkg-config/framework links, and compile-time host/target queries (`build.os()`, `build.arch()`).",
            {
                {"Executable", "fn Executable(name: str, cfg: ExecutableConfig) -> Target", "(buildsys manifest)", "Declares an executable build target with `Entry`, `Subsystem` (`\"cli\"` or `\"gui\"`), `OutDir`, `Targets` (`[\"native\", \"wasm\", \"ios-sim\"]`), `WebProfile`, `BundleId`, `AppName`, `Icon` (`.png`/`.svg`/`.icns`/`.ico`), `Sources`, `Includes`, `Defines`, and `Flags`.", "app := build.Executable(\"Flint\", { Entry: \"src/main.cf\", Subsystem: \"gui\", BundleId: \"dev.flint.app\", Icon: \"assets/icon.png\" })"},
                {"Library", "fn Library(name: str, cfg: LibraryConfig) -> Target", "(buildsys manifest)", "Declares a static or shared C/C++/cfire library target with `Sources`, `Includes`, `Defines`, and `Flags`.", "lib := build.Library(\"corelib\", { Sources: [\"native/bridge.cpp\"], Includes: [\"native\"] })"},
                {"Step", "fn Step(name: str, cfg: StepConfig) -> StepTarget", "(buildsys manifest)", "Declares a custom build step executing `Command` and `Args` in `WorkDir` whenever `Inputs` are newer than `Outputs`.", "gen := build.Step(\"assets\", { Command: \"python3\", Args: [\"scripts/bundle.py\"], Inputs: [\"ui/index.html\"], Outputs: [\"ui/bundle.js\"] })"},
                {"CMake", "fn CMake(name: str, cfg: CMakeConfig) -> DepTarget", "(buildsys manifest)", "Declares an external CMake or Git dependency (`Url`, `Commit`/`Branch`, `Sha256`, `Includes`, `Flags`, `Driver`).", "dep := build.CMake(\"zstd\", { Url: \"https://github.com/facebook/zstd.git\", Branch: \"v1.5.6\" })"},
                {"Link", "fn Link(lib: str)", "(buildsys manifest)", "Adds a system library (`\"sqlite3\"`) or Apple framework (`\"-framework Security\"`) to the linker flags.", "build.Link(\"sqlite3\")"},
                {"Include", "fn Include(dir: str)", "(buildsys manifest)", "Adds a relative or absolute header search directory (`-I<dir>`) for `import cpp` headers.", "build.Include(\"src/native/include\")"},
                {"PkgConfig", "fn PkgConfig(pkg: str)", "(buildsys manifest)", "Queries `pkg-config --cflags --libs <pkg>` and links the resolved flags into the build.", "build.PkgConfig(\"libssh\")"},
                {"Tako", "fn Tako()", "(buildsys manifest)", "Auto-discovers and links the local Tako GUI runtime and headers (`import \"pkg:tako\"`).", "build.Tako()"},
                {"os", "fn os() -> str", "(compile-time constant)", "Returns the target operating system identifier (`\"macos\"`, `\"linux\"`, `\"windows\"`, `\"ios\"`, `\"wasm\"`).", "if build.os() == \"macos\" { ... }"},
                {"arch", "fn arch() -> str", "(compile-time constant)", "Returns the target CPU architecture identifier (`\"arm64\"`, `\"x86_64\"`, `\"wasm32\"`).", "if build.arch() == \"arm64\" { ... }"}
            }
        },
        {
            "core:cli", "cli", "Struct-Tag Command-Line Parser & Interactive Prompts", "(header-only)",
            "Generates zero-boilerplate command-line flag parsers from struct field tags (`cli:\"--flag,help\"`), subcommand/positional argument helpers, and interactive terminal prompts.",
            {
                {"parse", "fn parse[T](args: []str = []) -> T!", "cfire::cli::parse<T>(argc, argv)", "Parses CLI flags (`str`, `i64`, `i32`, `f64`, `bool`) into a tagged struct `T`.", "args := try cli.parse[Args]()"},
                {"try_parse", "fn try_parse[T](args: []str = []) -> T?", "cfire::cli::try_parse<T>(argc, argv)", "Parses and validates CLI flags into a tagged struct `T`, returning `none` if any numeric flag argument is invalid.", "args := cli.try_parse[Args]() or { return }"},
                {"args", "fn args() -> []str", "cfire::cli::args()", "Returns the raw command-line arguments passed to the process (excluding `argv[0]`).", "raw := cli.args()"},
                {"subcommand", "fn subcommand() -> str", "cfire::cli::subcommand()", "Returns the first non-flag positional argument (subcommand name) or `\"\"` if none.", "cmd := cli.subcommand()"},
                {"positional", "fn positional() -> []str", "cfire::cli::positional()", "Returns all non-flag positional arguments passed on the command line.", "files := cli.positional()"},
                {"prompt", "fn prompt(message: str = \"\") -> str?", "cfire::cli::prompt(message)", "Prints `message` to stderr and reads a line from stdin without trailing newline, or `none` on EOF.", "user := cli.prompt(\"Username: \") or { return }"},
                {"prompt_password", "fn prompt_password(message: str = \"\") -> str?", "cfire::cli::prompt_password(message)", "Prints `message` to stderr with terminal echo disabled (`termios` `~ECHO`) and reads a password line from stdin, or `none` on EOF.", "pw := cli.prompt_password(\"Password: \") or { return }"}
            }
        },
        {
            "core:collections", "collections", "Generic Containers (Set, PriorityQueue, MinQueue, Deque)", "(header-only)",
            "Standard generic containers backed by STL unordered_set, priority_queue, and deque.",
            {
                {"Set", "type Set[T] struct", "cfire::collections::Set<T>", "Generic hash set container.", "mut s := collections.Set[str]{}"},
                {"PriorityQueue", "type PriorityQueue[T] struct", "cfire::collections::PriorityQueue<T>", "Max-first priority queue: `push(v)`, `pop() -> T?`, `peek() -> T?`, `len()`, `empty()`.", "mut pq := collections.PriorityQueue[i32]{}"},
                {"MinQueue", "type MinQueue[T] struct", "cfire::collections::MinQueue<T>", "Min-first priority queue (smallest element pops first), with the same methods as `PriorityQueue[T]`. Tuples order element by element, so `MinQueue[(i64, i64)]` pops the lowest cost first.", "mut open := collections.MinQueue[i64]{}"},
                {"Deque", "type Deque[T] struct", "cfire::collections::Deque<T>", "Generic double-ended queue.", "mut dq := collections.Deque[i32]{}"}
            }
        },
        {
            "core:crypto/aes", "aes", "NIST FIPS 197 AES-256 Symmetric Encryption & GCM AEAD", "(Header-only pure C++23)",
            "Constant-time AES-256 block cipher implementation with Counter Mode (CTR) and Galois/Counter Mode (GCM) Authenticated Encryption with Associated Data (AEAD).",
            {
                {"generate_key", "fn generate_key() -> []byte", "cfire::aes::generate_key()", "Generates a 32-byte (256-bit) cryptographically secure AES-256 key from OS entropy.", "key := aes.generate_key()"},
                {"encrypt", "fn encrypt(key: []byte, plaintext: []byte, aad: []byte = []) -> []byte!", "cfire::aes::encrypt(key, pt, aad)", "Encrypts `plaintext` with AES-256-GCM using a fresh 12-byte random nonce and returns `[12-byte nonce || ciphertext || 16-byte tag]`.", "blob := try aes.encrypt(key, secret, aad)"},
                {"decrypt", "fn decrypt(key: []byte, combined: []byte, aad: []byte = []) -> []byte!", "cfire::aes::decrypt(key, combined, aad)", "Authenticates and decrypts a combined `[12-byte nonce || ciphertext || 16-byte tag]` AES-256-GCM payload.", "pt := try aes.decrypt(key, blob, aad)"},
                {"encrypt_gcm", "fn encrypt_gcm(key: []byte, nonce: []byte, plaintext: []byte, aad: []byte = []) -> GcmResult!", "cfire::aes::encrypt_gcm(key, nonce, pt, aad)", "Encrypts plaintext with AES-256-GCM AEAD, producing ciphertext and a 16-byte authentication tag.", "res := try aes.encrypt_gcm(key, nonce, secret, aad)"},
                {"decrypt_gcm", "fn decrypt_gcm(key: []byte, nonce: []byte, ciphertext: []byte, tag: []byte, aad: []byte = []) -> []byte!", "cfire::aes::decrypt_gcm(key, nonce, ct, tag, aad)", "Authenticates and decrypts AES-256-GCM ciphertext, failing if tag verification fails or data was tampered.", "pt := try aes.decrypt_gcm(key, nonce, res.ciphertext, res.tag, aad)"},
                {"encrypt_ctr", "fn encrypt_ctr(key: []byte, iv: []byte, plaintext: []byte) -> []byte!", "cfire::aes::encrypt_ctr(key, iv, pt)", "Encrypts plaintext using AES-256-CTR mode.", "ct := try aes.encrypt_ctr(key, iv, pt)"},
                {"decrypt_ctr", "fn decrypt_ctr(key: []byte, iv: []byte, ciphertext: []byte) -> []byte!", "cfire::aes::decrypt_ctr(key, iv, ct)", "Decrypts ciphertext using AES-256-CTR mode.", "pt := try aes.decrypt_ctr(key, iv, ct)"},
                {"decrypt_gcm_str", "fn decrypt_gcm_str(key: []byte, nonce: []byte, ct: []byte, tag: []byte, aad: []byte = []) -> str!", "cfire::aes::decrypt_gcm_str(key, nonce, ct, tag, aad)", "Decrypts an authenticated AES-GCM ciphertext and validates the tag, returning a UTF-8 string.", "pt := try aes.decrypt_gcm_str(key, nonce, ct, tag)"}
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
            "core:crypto/hmac", "hmac", "RFC 2104 HMAC-SHA256, RFC 5869 HKDF & PBKDF2-SHA256", "(CommonCrypto / built-in)",
            "Computes RFC 2104 keyed HMAC-SHA256 authentication digests, RFC 5869 HKDF-SHA256 derived keys, and RFC 8018 PBKDF2-HMAC-SHA256 password-derived keys.",
            {
                {"sha256_hex", "fn sha256_hex(key: str, data: str) -> str", "cfire::hmac::sha256_hex(key, data)", "Returns a 64-character lowercase hex HMAC-SHA256 digest.", "sig := hmac.sha256_hex(secret, payload)"},
                {"sha256_bytes", "fn sha256_bytes(key: []byte, data: []byte) -> []byte", "cfire::crypto::hmac_sha256(key, data)", "Computes HMAC-SHA256 over raw byte slices.", "mac := hmac.sha256_bytes(key, data)"},
                {"verify", "fn verify(key: str, data: str, mac: str) -> bool", "cfire::hmac::verify(key, data, mac)", "Verifies in constant time whether `mac` matches the computed HMAC-SHA256 hex digest of `data` under `key`.", "valid := hmac.verify(secret, payload, mac)"},
                {"equal", "fn equal(a: str, b: str) -> bool", "cfire::hmac::equal(a, b)", "Performs a constant-time equality check on two MAC digests to prevent timing attacks.", "ok := hmac.equal(sig1, sig2)"},
                {"hkdf_sha256", "fn hkdf_sha256(ikm: []byte, salt: []byte = [], info: []byte = [], length: usize = 32) -> []byte", "cfire::hmac::hkdf_sha256(ikm, salt, info, length)", "Derives cryptographic key material of `length` bytes using RFC 5869 HKDF-SHA256 Extract-and-Expand.", "key := hmac.hkdf_sha256(ikm, salt, info, 32)"},
                {"hkdf_sha256_hex", "fn hkdf_sha256_hex(ikm: str, salt: str = \"\", info: str = \"\", length: usize = 32) -> str", "cfire::hmac::hkdf_sha256_hex(ikm, salt, info, length)", "Derives a hex-encoded key string using RFC 5869 HKDF-SHA256.", "khex := hmac.hkdf_sha256_hex(secret, \"salt\", \"ctx\", 32)"},
                {"pbkdf2_sha256", "fn pbkdf2_sha256(password: str, salt: []byte, iterations: u32 = 600000, length: usize = 32) -> []byte", "cfire::hmac::pbkdf2_sha256(password, salt, iterations, length)", "Derives a cryptographic key of `length` bytes from `password` and `salt` using RFC 8018 PBKDF2-HMAC-SHA256.", "key := hmac.pbkdf2_sha256(password, salt, 600000, 32)"}
            }
        },
        {
            "core:crypto/sha256", "sha256", "Standard FIPS 180-4 SHA-256 Digest & CSPRNG", "(CommonCrypto / built-in)",
            "Computes NIST FIPS 180-4 SHA-256 digests and generates OS-backed cryptographically secure random bytes (`arc4random_buf` / `/dev/urandom`).",
            {
                {"hash_hex", "fn hash_hex(data: []byte) -> str", "cfire::sha256::hash_hex(data)", "Returns a 64-character lowercase hex digest of `data`.", "digest := sha256.hash_hex(raw)"},
                {"hash_bytes", "fn hash_bytes(data: []byte) -> []byte", "cfire::sha256::hash_bytes(data)", "Returns 32-byte binary SHA-256 digest of `data`.", "b := sha256.hash_bytes(raw)"},
                {"random_bytes", "fn random_bytes(n: usize) -> []byte", "cfire::sha256::random_bytes(n)", "Generates `n` cryptographically secure random bytes from the OS CSPRNG.", "nonce := sha256.random_bytes(12)"},
                {"random_hex", "fn random_hex(n: usize = 32) -> str", "cfire::sha256::random_hex(n)", "Generates `n` cryptographically secure random bytes encoded as a `2*n`-character lowercase hex string.", "token := sha256.random_hex(16)"},
                {"hash_file_hex", "fn hash_file_hex(path: str) -> str!", "cfire::sha256::hash_file_hex(path)", "Computes the hex-encoded SHA-256 digest of a file on disk.", "h := try sha256.hash_file_hex(\"file.bin\")"}
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
            "core:fs", "fs", "Filesystem & Zero-Copy Byte I/O", "(header-only)",
            "Provides RAII file and directory utilities backed by `std::filesystem` and `std::vector<std::uint8_t>` with strict `_bytes` / `_text` symmetry.",
            {
                {"read_bytes", "fn read_bytes(path: str) -> []byte!", "cfire::fs::read_bytes(path)", "Reads an entire binary file into a byte buffer.", "raw := try fs.read_bytes(\"input.bin\")"},
                {"write_bytes", "fn write_bytes(path: str, data: []byte) -> void!", "cfire::fs::write_bytes(path, data)", "Writes a byte slice to disk (truncating any existing file).", "try fs.write_bytes(\"out.bin\", raw)"},
                {"append_bytes", "fn append_bytes(path: str, data: []byte) -> void!", "cfire::fs::append_bytes(path, data)", "Appends a byte slice to a file on disk (creating the file if it does not exist).", "try fs.append_bytes(\"out.bin\", raw)"},
                {"read_text", "fn read_text(path: str) -> str!", "cfire::fs::read_text(path)", "Reads an entire UTF-8 text file into a `str`.", "txt := try fs.read_text(\"config.json\")"},
                {"write_text", "fn write_text(path: str, text: str) -> void!", "cfire::fs::write_text(path, text)", "Writes a UTF-8 string to disk.", "try fs.write_text(\"hello.txt\", \"sapphire\")"},
                {"append_text", "fn append_text(path: str, text: str) -> void!", "cfire::fs::append_text(path, text)", "Appends a UTF-8 string to a file on disk.", "try fs.append_text(\"app.log\", \"started\\n\")"},
                {"rename", "fn rename(from: str, to: str) -> void!", "cfire::fs::rename(from, to)", "Renames or moves a file or directory.", "try fs.rename(\"tmp.bin\", \"final.bin\")"},
                {"copy_file", "fn copy_file(from: str, to: str) -> void!", "cfire::fs::copy_file(from, to)", "Copies a file from `from` to `to` (overwriting `to` if it exists).", "try fs.copy_file(\"a.bin\", \"b.bin\")"},
                {"file_size", "fn file_size(path: str) -> u64!", "cfire::fs::file_size(path)", "Returns the size of a file in bytes.", "sz := try fs.file_size(\"input.bin\")"},
                {"open", "fn open(path: str, mode: OpenMode = OpenMode::Read) -> File!", "cfire::fs::open(path, mode)", "Opens a file with specified mode (Read, Write, Append, ReadWrite) returning a streaming File handle.", "mut f := try fs.open(\"log.txt\", OpenMode::Append)"},
                {"File.fd", "fn (f: File) fd() -> i32", "f.fd()", "Returns the underlying OS file descriptor (`-1` if closed).", "fd := f.fd()"},
                {"File.is_open", "fn (f: File) is_open() -> bool", "f.is_open()", "Returns `true` if the file handle holds a valid open file descriptor.", "if f.is_open() { ... }"},
                {"File.is_tty", "fn (f: File) is_tty() -> bool", "f.is_tty()", "Returns `true` if the file stream refers to an interactive terminal.", "if os.stdout().is_tty() { ... }"},
                {"File.poll", "fn (f: File) poll(timeout_ms: i64 = 0) -> bool", "f.poll(timeout_ms)", "Polls the file stream for readability up to `timeout_ms` milliseconds (`EINTR`-safe).", "if r.poll(50) { ... }"},
                {"File.read_into", "fn (mut f: File) read_into(mut buf: []byte) -> usize!", "f.read_into(buf)", "Reads up to `buf.len()` bytes from the file stream into `buf`.", "n := try f.read_into(mut buf)"},
                {"File.read_exact", "fn (mut f: File) read_exact(exact_bytes: usize) -> []byte!", "f.read_exact(exact_bytes)", "Reads exactly `exact_bytes` from the file stream or returns an error on premature EOF.", "hdr := try f.read_exact(8)"},
                {"File.read_bytes", "fn (mut f: File) read_bytes(max_bytes: usize = 67108864) -> []byte!", "f.read_bytes(max_bytes)", "Reads up to `max_bytes` from the file stream into a new byte slice.", "chunk := try f.read_bytes(4096)"},
                {"File.read_text", "fn (mut f: File) read_text(max_bytes: usize = 67108864) -> str!", "f.read_text(max_bytes)", "Reads up to `max_bytes` from the file stream as a UTF-8 string.", "body := try f.read_text()"},
                {"File.read_bytes_until", "fn (mut f: File) read_bytes_until(delim: byte, max_bytes: usize = 65536) -> []byte!", "f.read_bytes_until(delim, max_bytes)", "Reads bytes from the file stream up to and including `delim` (or `max_bytes`).", "frame := try f.read_bytes_until(byte(0))"},
                {"File.read_text_until", "fn (mut f: File) read_text_until(delim: str, max_bytes: usize = 65536) -> str!", "f.read_text_until(delim, max_bytes)", "Reads text from the file stream up to and including the first byte of `delim` (or `max_bytes`).", "line := try f.read_text_until(\"\\n\")"},
                {"File.read_line", "fn (mut f: File) read_line(max_bytes: usize = 65536) -> str?", "f.read_line(max_bytes)", "Reads the next line from the file stream (stripping `\\r\\n` / `\\n`), or `none` on EOF.", "line := f.read_line() or { return }"},
                {"File.read_lines", "fn (mut f: File) read_lines() -> []str!", "f.read_lines()", "Reads all remaining lines from the file stream into a string slice.", "lines := try f.read_lines()"},
                {"File.write_bytes", "fn (mut f: File) write_bytes(data: []byte) -> usize!", "f.write_bytes(data)", "Writes all bytes in `data` to the file stream, looping across partial writes and `EINTR`.", "n := try f.write_bytes(raw)"},
                {"File.write_text", "fn (mut f: File) write_text(text: str) -> usize!", "f.write_text(text)", "Writes a UTF-8 string to the file stream, looping across partial writes and `EINTR`.", "n := try f.write_text(\"header\\n\")"},
                {"File.seek", "fn (mut f: File) seek(offset: i64, whence: SeekWhence = SeekWhence::Start) -> i64!", "f.seek(offset, whence)", "Seeks within file to offset from Start, Current, or End.", "pos := try f.seek(0, SeekWhence::Start)"},
                {"File.tell", "fn (mut f: File) tell() -> i64!", "f.tell()", "Returns current byte offset within file stream.", "pos := try f.tell()"},
                {"File.flush", "fn (mut f: File) flush() -> void!", "f.flush()", "Flushes buffered stream data to disk.", "try f.flush()"},
                {"File.close", "fn (mut f: File) close() -> void", "f.close()", "Closes open file handle.", "f.close()"},
                {"dirname", "fn dirname(path: str) -> str", "cfire::fs::dirname(path)", "Returns directory portion of a filesystem path.", "dir := fs.dirname(\"/a/b/c.txt\")"},
                {"basename", "fn basename(path: str) -> str", "cfire::fs::basename(path)", "Returns file name portion of a filesystem path.", "name := fs.basename(\"/a/b/c.txt\")"},
                {"extension", "fn extension(path: str) -> str", "cfire::fs::extension(path)", "Returns file extension including leading dot.", "ext := fs.extension(\"main.cf\")"},
                {"exists", "fn exists(path: str) -> bool", "cfire::fs::exists(path)", "Returns `true` if the path exists on disk.", "if fs.exists(\"build.cf\") { ... }"},
                {"is_dir", "fn is_dir(path: str) -> bool", "cfire::fs::is_dir(path)", "Returns `true` if `path` exists and is a directory.", "if fs.is_dir(\"src\") { ... }"},
                {"is_file", "fn is_file(path: str) -> bool", "cfire::fs::is_file(path)", "Returns `true` if `path` exists and is a regular file.", "if fs.is_file(\"build.cf\") { ... }"},
                {"mkdir_all", "fn mkdir_all(path: str) -> void!", "cfire::fs::mkdir_all(path)", "Recursively creates a directory and all parent directories.", "try fs.mkdir_all(\".cfire/cache\")"},
                {"remove", "fn remove(path: str) -> void!", "cfire::fs::remove(path)", "Removes a file or directory tree recursively.", "try fs.remove(\"tmp_dir\")"},
                {"list_dir", "fn list_dir(path: str) -> []str!", "cfire::fs::list_dir(path)", "Lists sorted filenames inside a directory.", "entries := try fs.list_dir(\"examples\")"}
            }
        },
        {
            "core:json", "json", "Compile-Time SIMD JSON Reflection & Dynamic DOM", "-lsimdjson (or header-only)",
            "Uses `json:\"...\"` struct tags (with automatic field-name fallback and `T?` optional field support) to emit specialized `simdjson::ondemand` parsers and fast `std::format` serializers with zero runtime reflection overhead, along with a zero-dependency dynamic DOM `json.Value`.",
            {
                {"parse", "fn parse[T](payload: []byte | str) -> T!", "cfire::json::parse<T>(payload)", "Parses UTF-8 JSON into a struct `T` using `simdjson::ondemand`.", "cfg := try json.parse[Config](raw)"},
                {"stringify", "fn stringify[T](val: T) -> str", "cfire::json::stringify<T>(val)", "Serializes a struct `T` (`str`, `u64`, `i64`, `i32`, `f64`, `bool`, `T?`, slices, nested structs) into a JSON string.", "payload := json.stringify(metric)"},
                {"schema", "fn schema[T]() -> str", "cfire::json::schema<T>()", "Generates a Draft 2020-12 / OpenAI Strict Mode / MCP JSON Schema string for struct `T` using `json:\"...\"` and `desc:\"...\"` tags.", "s := json.schema[SearchTool]()"},
                {"escape_string", "fn escape_string(s: str) -> str", "cfire::json::escape_string(s)", "Escapes quotes, backslashes, and control characters for JSON strings.", "esc := json.escape_string(raw_txt)"},
                {"parse_dynamic", "fn parse_dynamic(payload: str) -> Value!", "cfire::json::parse_dynamic(payload)", "Parses arbitrary JSON into a recursive dynamic DOM `Value`.", "v := try json.parse_dynamic(raw)"},
                {"stringify_dynamic", "fn stringify_dynamic(val: Value, indent: i32 = 0) -> str", "cfire::json::stringify_dynamic(val, indent)", "Serializes a dynamic DOM `Value` into compact or indented JSON.", "s := json.stringify_dynamic(v, 2)"}
            }
        },
        {
            "core:keychain", "keychain", "Native OS Credential Keychain & Secret Service", "-framework Security (macOS/iOS) / libsecret (Linux) / Advapi32 (Windows)",
            "Cross-platform OS credential storage backed by `Security.framework` (`SecItemAdd` / `SecItemCopyMatching` / `SecItemDelete`) on macOS & iOS, `libsecret` (`secret-tool`) on Linux, and Windows Credential Manager (`CredWriteA` / `CredReadA`) on Windows.",
            {
                {"set", "fn set(service: str, account: str, secret: str) -> void!", "cfire::keychain::set(service, account, secret)", "Stores or updates a generic password for `(service, account)` in the OS keychain.", "try keychain.set(\"dev.flint.vault\", \"master\", token)"},
                {"get", "fn get(service: str, account: str) -> str!", "cfire::keychain::get(service, account)", "Retrieves the secret stored for `(service, account)` from the OS keychain.", "sec := try keychain.get(\"dev.flint.vault\", \"master\")"},
                {"delete_password", "fn delete_password(service: str, account: str) -> void!", "cfire::keychain::delete_password(service, account)", "Removes the credential entry for `(service, account)` from the OS keychain.", "try keychain.delete_password(\"dev.flint.vault\", \"master\")"}
            }
        },
        {
            "core:log", "log", "Structured Leveled Logging", "(header-only)",
            "Leveled logging (`debug`, `info`, `warn`, `error`) with `Level` (`Debug`, `Info`, `Warn`, `Error`), `Format` (`Text`, `Json`), and structured key-value attributes.",
            {
                {"set_level", "fn set_level(l: Level)", "cfire::log::set_level(l)", "Sets the minimum logging severity threshold (`log.Level::Debug`, `Info`, `Warn`, `Error`).", "log.set_level(log.Level::Debug)"},
                {"get_level", "fn get_level() -> Level", "cfire::log::get_level()", "Returns the current minimum logging severity threshold.", "lvl := log.get_level()"},
                {"set_format", "fn set_format(f: Format)", "cfire::log::set_format(f)", "Sets the log output format (`log.Format::Text` or `log.Format::Json`).", "log.set_format(log.Format::Json)"},
                {"get_format", "fn get_format() -> Format", "cfire::log::get_format()", "Returns the current log output format.", "fmt := log.get_format()"},
                {"debug", "fn debug(msg: str, ...attrs: any)", "cfire::log::debug(msg, ...)", "Logs a debug message with optional key-value attribute pairs.", "log.debug(\"cache miss\", \"key\", \"user:1\")"},
                {"info", "fn info(msg: str, ...attrs: any)", "cfire::log::info(msg, ...)", "Logs an informational message with optional key-value attribute pairs.", "log.info(\"server listening\", \"port\", 8080)"},
                {"warn", "fn warn(msg: str, ...attrs: any)", "cfire::log::warn(msg, ...)", "Logs a warning message with optional key-value attribute pairs.", "log.warn(\"rate limit reached\", \"client\", ip)"},
                {"error", "fn error(msg: str, ...attrs: any)", "cfire::log::error(msg, ...)", "Logs an error message with optional key-value attribute pairs.", "log.error(\"database connection failed\", \"db\", name)"}
            }
        },
        {
            "core:math", "math", "Math Constants & Multi-Input Functions", "(header-only)",
            "Mathematical constants (`math.PI`, `math.E`, `math.TAU`, `math.SQRT_2`, `math.LN_2`, `math.LN_10`) and functions with no natural receiver (`atan2`, `hypot`, `gcd`, `lcm`). Single-value operations are methods on the number itself (`x.abs()`, `f.sqrt()`, `x.clamp(lo, hi)`, `a.checked_add(b)`); type limits and parsing are associated with the type (`i32.MAX`, `f64.NAN`, `i32.parse(s)`). See the builtin method reference.",
            {
                {"PI", "const PI: f64", "cfire::math::PI", "Ratio of a circle's circumference to its diameter (π).", "area := math.PI * r * r"},
                {"E", "const E: f64", "cfire::math::E", "Euler's number (e).", "y := math.E.powi(2)"},
                {"TAU", "const TAU: f64", "cfire::math::TAU", "Full turn in radians (2π).", "step := math.TAU / f64(n)"},
                {"SQRT_2", "const SQRT_2: f64", "cfire::math::SQRT_2", "Square root of 2.", "d := side * math.SQRT_2"},
                {"LN_2", "const LN_2: f64", "cfire::math::LN_2", "Natural logarithm of 2.", "bits := x.ln() / math.LN_2"},
                {"LN_10", "const LN_10: f64", "cfire::math::LN_10", "Natural logarithm of 10.", "d := x.ln() / math.LN_10"},
                {"atan2", "fn atan2(y: f64, x: f64) -> f64", "cfire::math::atan2(y, x)", "Angle of the point `(x, y)` from the positive x-axis, in radians (`-π..=π`).", "heading := math.atan2(dy, dx)"},
                {"hypot", "fn hypot(x: f64, y: f64) -> f64", "cfire::math::hypot(x, y)", "`sqrt(x*x + y*y)` without intermediate overflow or underflow.", "dist := math.hypot(dx, dy)"},
                {"gcd", "fn gcd(a: i64, b: i64) -> i64", "cfire::math::gcd(a, b)", "Greatest common divisor (non-negative); `gcd(0, 0) == 0`. Panics for the minimum `i64`.", "g := math.gcd(12, 18)"},
                {"lcm", "fn lcm(a: i64, b: i64) -> i64", "cfire::math::lcm(a, b)", "Least common multiple (non-negative); panics on overflow.", "l := math.lcm(4, 6)"}
            }
        },
        {
            "core:net", "net", "POSIX BSD TCP Sockets & Loopback Networking", "(POSIX sockets)",
            "RAII TCP client, listener, and connected socketpair handles backed by BSD `AF_INET` / `AF_UNIX` sockets with non-blocking `poll` timeouts and strict `_bytes` / `_text` I/O symmetry.",
            {
                {"listen", "fn listen(port: u16 = 0, host: str = \"0.0.0.0\") -> TcpListener!", "cfire::net::listen(port, host)", "Binds a TCP listener on `host:port` (`0` picks an ephemeral port; the default host is every interface).", "mut ln := try net.listen(0)"},
                {"TcpListener.port", "fn (ln: TcpListener) port() -> u16", "ln.port()", "Returns the bound TCP port.", "p := ln.port()"},
                {"TcpListener.accept", "fn (mut ln: TcpListener) accept() -> TcpConn!", "ln.accept()", "Accepts an incoming TCP connection with a safe poll timeout.", "mut conn := try ln.accept()"},
                {"connect", "fn connect(host: str, port: u16) -> TcpConn!", "cfire::net::connect(host, port)", "Connects a `TcpConn` to `host:port`.", "mut c := try net.connect(\"127.0.0.1\", p)"},
                {"socketpair", "fn socketpair() -> (TcpConn, TcpConn)!", "cfire::net::socketpair()", "Creates a connected pair of full-duplex `TcpConn` stream sockets.", "(mut a, mut b) := try net.socketpair()"},
                {"TcpConn.remote_addr", "fn (c: TcpConn) remote_addr() -> str", "c.remote_addr()", "The peer address of an accepted connection (`203.0.113.7`, `2001:db8::1`; IPv4-mapped IPv6 peers are reported as IPv4), or the host a client connection dialed.", "println(\"client {}\", conn.remote_addr())"},
                {"TcpConn.fd", "fn (c: TcpConn) fd() -> i32", "c.fd()", "Returns the underlying socket file descriptor (`-1` if closed).", "fd := conn.fd()"},
                {"TcpConn.is_open", "fn (c: TcpConn) is_open() -> bool", "c.is_open()", "Returns `true` if the socket connection is open.", "if conn.is_open() { ... }"},
                {"TcpConn.poll", "fn (c: TcpConn) poll(timeout_ms: i64 = 0) -> bool", "c.poll(timeout_ms)", "Polls the socket for readability up to `timeout_ms` milliseconds (`EINTR`-safe).", "if conn.poll(50) { ... }"},
                {"TcpConn.write_bytes", "fn (mut c: TcpConn) write_bytes(payload: []byte) -> usize!", "c.write_bytes(payload)", "Writes all bytes in `payload` to the TCP/TLS connection, looping across partial sends and `EINTR`.", "_ = try c.write_bytes(raw)"},
                {"TcpConn.write_text", "fn (mut c: TcpConn) write_text(text: str) -> usize!", "c.write_text(text)", "Writes a UTF-8 string to the TCP/TLS connection, looping across partial sends and `EINTR`.", "_ = try c.write_text(\"PING\\n\")"},
                {"TcpConn.read_into", "fn (mut c: TcpConn) read_into(mut buf: []byte) -> usize!", "c.read_into(buf)", "Reads up to `buf.len()` bytes from the socket into `buf` (`EINTR`-safe).", "n := try conn.read_into(mut buf)"},
                {"TcpConn.read_bytes", "fn (mut c: TcpConn) read_bytes(max_bytes: usize = 4096) -> []byte!", "c.read_bytes(max_bytes)", "Reads up to `max_bytes` from the socket into a new byte slice.", "pkt := try conn.read_bytes(256)"},
                {"TcpConn.read_exact", "fn (mut c: TcpConn) read_exact(exact_bytes: usize) -> []byte!", "c.read_exact(exact_bytes)", "Reads exactly `exact_bytes` from the socket or returns an error on premature EOF.", "hdr := try conn.read_exact(8)"},
                {"TcpConn.read_text", "fn (mut c: TcpConn) read_text(max_bytes: usize = 4096) -> str!", "c.read_text(max_bytes)", "Reads up to `max_bytes` from the socket as a UTF-8 string.", "msg := try conn.read_text(64)"},
                {"TcpConn.read_bytes_until", "fn (mut c: TcpConn) read_bytes_until(delim: byte, max_bytes: usize = 65536) -> []byte!", "c.read_bytes_until(delim, max_bytes)", "Reads bytes from the socket up to and including `delim` (or `max_bytes`).", "frame := try conn.read_bytes_until(byte(0))"},
                {"TcpConn.read_text_until", "fn (mut c: TcpConn) read_text_until(delim: str, max_bytes: usize = 65536) -> str!", "c.read_text_until(delim, max_bytes)", "Reads text from the socket up to and including the first byte of `delim` (or `max_bytes`).", "line := try conn.read_text_until(\"\\n\")"},
                {"TcpConn.read_line", "fn (mut c: TcpConn) read_line(max_bytes: usize = 65536) -> str?", "c.read_line(max_bytes)", "Reads a newline-delimited line up to `max_bytes` from the socket, or `none` on EOF.", "line := conn.read_line() or { return }"},
                {"TcpConn.set_timeout_ms", "fn (mut c: TcpConn) set_timeout_ms(ms: i32) -> void", "c.set_timeout_ms(ms)", "Sets socket read/write timeout in milliseconds (`0` for blocking).", "conn.set_timeout_ms(5000)"},
                {"TcpConn.close", "fn (mut c: TcpConn) close() -> void", "c.close()", "Closes the TCP socket connection.", "conn.close()"}
            }
        },
        {
            "core:net/http", "http", "HTTP/1.1 & HTTPS Client, Server & Router", "(POSIX sockets + TLS)",
            "RFC 9112 HTTP/1.1 & HTTPS client (`get`, `post`, `request`), wire parser (`parse_request`), response serializer (`Response`), HTTP/HTTPS server (`make(http.Server, port)`, `make(http.Server, port, tls: http.TlsConfig{...})`, per-connection `http.ServerLimits`), and request router (`make_router`) with integrated WebSocket route upgrade (`Router.ws`).",
            {
                {"get", "fn get(url: str, headers: map[str]str = {}, verify_peer: bool = true) -> Response!", "cfire::http::get(url, headers, verify_peer)", "Executes an HTTP or HTTPS GET request against a target URL.", "resp := try http.get(\"https://127.0.0.1:8443/health\", {}, false)"},
                {"post", "fn post(url: str, body: str, content_type: str = \"application/json\", headers: map[str]str = {}, verify_peer: bool = true) -> Response!", "cfire::http::post(url, body, content_type, headers, verify_peer)", "Executes an HTTP or HTTPS POST request with payload body.", "resp := try http.post(\"http://127.0.0.1:8080/items\", \"{\\\"id\\\": 1}\")"},
                {"request", "fn request(method: str, url: str, body: str = \"\", content_type: str = \"\", headers: map[str]str = {}, verify_peer: bool = true, ca_file: str = \"\") -> Response!", "cfire::http::request(method, url, body, content_type, headers, verify_peer, ca_file)", "Executes an arbitrary HTTP or HTTPS request (PUT, DELETE, PATCH, etc.).", "resp := try http.request(\"DELETE\", \"http://127.0.0.1:8080/items/1\")"},
                {"post_sse", "fn post_sse(url: str, body: str, headers: map[str]str = {}, on_event: fn(SseEvent) -> bool, verify_peer: bool = true) -> void!", "cfire::http::post_sse(url, body, headers, on_event, verify_peer)", "Streams an HTTP/HTTPS POST response using Server-Sent Events (`text/event-stream`) and chunked transfer encoding, invoking `on_event` for each event.", "try http.post_sse(url, payload, headers, |ev| { print(\"{}\", ev.data); return true })"},
                {"Server.make", "fn make(port: u16 = 0, tls: TlsConfig = {}) -> HttpServer!", "cfire::http::make_server(port, tls)", "`make(http.Server, port)` creates an HTTP server listening on `port` (`0` picks an ephemeral port); `make(http.Server, port, tls: http.TlsConfig{cert_file: c, key_file: k})` (or `cert_pem` / `key_pem`) serves HTTPS.", "mut srv := try make(http.Server, 8443, tls: http.TlsConfig{cert_file: \"cert.pem\", key_file: \"key.pem\"})"},
                {"Router.make", "fn make() -> Router", "cfire::http::make_router()", "`make(http.Router)` creates an independent HTTP & WebSocket request router supporting path parameters (`:id`) and wildcards (`*path`).", "mut router := make(http.Router)"},
                {"parse_request", "fn parse_request(raw: str) -> Request!", "cfire::http::parse_request(raw)", "Parses raw HTTP/1.1 wire text into a structured Request model.", "req := try http.parse_request(raw_text)"},
                {"Response.text", "fn Response.text(code: i32, payload: str) -> Response", "cfire::http::Response::text(code, payload)", "Creates an HTTP Response with text/plain content-type and standard status reason.", "return http.Response.text(200, \"OK\")"},
                {"Response.json", "fn Response.json(code: i32, json_str: str) -> Response", "cfire::http::Response::json(code, json_str)", "Creates an HTTP Response with application/json content-type.", "return http.Response.json(200, \"{\\\"status\\\":\\\"ok\\\"}\")"},
                {"Response.html", "fn Response.html(code: i32, html: str) -> Response", "cfire::http::Response::html(code, html)", "Creates an HTTP Response with text/html content-type.", "return http.Response.html(200, \"<h1>Hello</h1>\")"},
                {"Response.redirect", "fn Response.redirect(location: str, code: i32 = 302) -> Response", "cfire::http::Response::redirect(location, code)", "Creates an HTTP redirect Response setting Location header.", "return http.Response.redirect(\"/login\", 302)"},
                {"Router.get", "fn (mut r: Router) get(pattern: str, handler: fn(Request) -> Response) -> Router", "r.get(pattern, handler)", "Registers an HTTP GET route with optional path parameters (`:id`) or wildcards (`*path`).", "r.get(\"/users/:id\", |req| http.Response.text(200, req.param(\"id\")))"},
                {"Router.post", "fn (mut r: Router) post(pattern: str, handler: fn(Request) -> Response) -> Router", "r.post(pattern, handler)", "Registers an HTTP POST route handler.", "r.post(\"/items\", |req| http.Response.json(201, \"{}\"))"},
                {"Router.ws", "fn (mut r: Router) ws(pattern: str, handler: fn(Request, mut websocket.Conn) -> void) -> Router", "r.ws(pattern, handler)", "Registers a WebSocket route that automatically upgrades matching HTTP requests and invokes `handler(req, mut ws)`.", "r.ws(\"/chat/:room\", |req, mut ws| { ... })"},
                {"Router.dispatch", "fn (r: Router) dispatch(req: Request) -> Response", "r.dispatch(req)", "Dispatches an HTTP request through the router in-memory without open network sockets.", "res := router.dispatch(test_req)"},
                {"Router.use", "fn (mut r: Router) use(mw: fn(mut Request) -> Response?) -> Router", "r.use(mw)", "Registers pre-routing middleware that can short-circuit or validate HTTP and WebSocket upgrade requests.", "r.use(|req| { return None })"},
                {"ServerLimits", "type ServerLimits struct { header_timeout_ms: i32 = 10000, body_timeout_ms: i32 = 30000, max_body_bytes: usize = 10485760, workers: usize = 0, max_pending: usize = 128, keepalive_timeout_ms: i32 = 5000, max_requests_per_conn: usize = 1000, max_websockets: usize = 1024, max_connections: usize = 10000 }", "cfire::http::ServerLimits", "Per-connection server limits (Q22, CF-180, CF-192, CF-193). Idle and slow connections wait in the server's event loop, so `workers` is how many handlers run at once. Omitted fields keep their defaults; `workers = 0` keeps the default (twice the core count, at least 4). `keepalive_timeout_ms = 0` turns keep-alive off.", "srv.set_limits(http.ServerLimits{max_body_bytes: 1048576, header_timeout_ms: 5000, keepalive_timeout_ms: 2000})"},
                {"TlsConfig", "type TlsConfig struct { cert_file: str, key_file: str, cert_pem: str, key_pem: str }", "cfire::http::TlsConfig", "Server certificate for `make(http.Server, port, tls: ...)` (Q31): set `cert_file` / `key_file`, or `cert_pem` / `key_pem` for in-memory PEM.", "tls: http.TlsConfig{cert_pem: pair.cert_pem, key_pem: pair.key_pem}"},
                {"HttpServer.set_limits", "fn (mut s: HttpServer) set_limits(limits: ServerLimits)", "s.set_limits(limits)", "Replaces the per-connection limits (Q22, CF-192): `http.ServerLimits{header_timeout_ms, body_timeout_ms, max_body_bytes, workers, max_pending, keepalive_timeout_ms, max_requests_per_conn, max_websockets, max_connections}`; omitted fields keep their defaults.", "srv.set_limits(http.ServerLimits{max_body_bytes: 1048576})"},
                {"HttpServer.enable_tls_files", "fn (mut s: HttpServer) enable_tls_files(cert_file: str, key_file: str) -> void!", "s.enable_tls_files(cert_file, key_file)", "Enables TLS on the server using certificate and private key PEM files.", "try srv.enable_tls_files(\"cert.pem\", \"key.pem\")"},
                {"HttpServer.enable_tls_pem", "fn (mut s: HttpServer) enable_tls_pem(cert_pem: str, key_pem: str) -> void!", "s.enable_tls_pem(cert_pem, key_pem)", "Enables TLS on the server using in-memory certificate and private key PEM strings.", "try srv.enable_tls_pem(pair.cert_pem, pair.key_pem)"},
                {"HttpServer.serve_in_background", "fn (mut s: HttpServer) serve_in_background(handler: fn(Request) -> Response | Router) -> void", "s.serve_in_background(handler)", "Starts a non-blocking background accept loop serving HTTP and WebSocket upgrade requests.", "server.serve_in_background(router)"},
                {"HttpServer.port", "fn (s: HttpServer) port() -> u16", "s.port()", "Returns the port the server is listening on (useful after binding port 0).", "port := srv.port()"},
                {"HttpServer.stop", "fn (mut s: HttpServer) stop() -> void", "s.stop()", "Gracefully closes the listening socket and stops worker threads.", "server.stop()"},
                {"Request.param", "fn (req: Request) param(key: str, fallback: str = \"\") -> str", "req.param(key, fallback)", "Retrieves a named route path parameter extracted by the router.", "id := req.param(\"id\")"},
                {"Request.query", "fn (req: Request) query(key: str, fallback: str = \"\") -> str", "req.query(key, fallback)", "Retrieves a decoded URL query parameter.", "page := req.query(\"page\", \"1\")"},
                {"Request.header", "fn (req: Request) header(key: str, fallback: str = \"\") -> str", "req.header(key, fallback)", "Performs a case-insensitive header lookup.", "auth := req.header(\"Authorization\")"}
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
            "core:net/websocket", "websocket", "RFC 6455 High-Level WebSockets & WSS Client & Server", "(POSIX sockets + TLS)",
            "RFC 6455 full-duplex `ws://` and `wss://` WebSocket client (`connect`, with `verify_peer: false` for self-signed certificates), standalone server (`make(websocket.Server, port)`, `tls: websocket.TlsConfig{...}` for `wss://`), and connection upgrade handler (`upgrade`) with automated framing, client masking, and Ping/Pong management.",
            {
                {"connect", "fn connect(url: str, verify_peer: bool = true) -> Conn!", "cfire::websocket::connect(url, verify_peer)", "Establishes a `ws://` or `wss://` WebSocket connection and completes the RFC 6455 handshake.", "mut ws := try websocket.connect(\"ws://127.0.0.1:8080/ws\")"},
                {"TlsConfig", "type TlsConfig struct { cert_file: str, key_file: str, cert_pem: str, key_pem: str }", "cfire::http::TlsConfig", "Server certificate for `make(websocket.Server, port, tls: ...)` (Q31); the same type as `http.TlsConfig`.", "tls: websocket.TlsConfig{cert_file: \"cert.pem\", key_file: \"key.pem\"}"},
                {"Server.make", "fn make(port: u16 = 0, tls: TlsConfig = {}) -> Server!", "cfire::websocket::make_server(port, tls)", "`make(websocket.Server, port)` creates a standalone WebSocket server listening on `port` (`0` picks an ephemeral port); add `tls: websocket.TlsConfig{...}` for `wss://`.", "mut srv := try make(websocket.Server, 9001)"},
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
            "core:os", "os", "Environment, Standard Streams, Terminal, Pipes & Subprocesses", "(header-only)",
            "Process environment inspection, working directory queries, standard stream handles (`os.stdin()`, `os.stdout()`, `os.stderr()`), RAII pipes (`os.pipe()`), raw terminal control (`is_tty`, `term_size`, `make(os.RawTerminal)`, `on_signal`), and subprocess execution (`command`, `exec_output`).",
            {
                {"args", "fn args() -> []str", "cfire::os::args()", "Returns the command-line arguments passed to the current process.", "argv := os.args()"},
                {"exit", "fn exit(code: i32)", "cfire::os::exit(code)", "Terminates the current process immediately with status `code`.", "os.exit(1)"},
                {"getenv", "fn getenv(key: str, fallback: str = \"\") -> str", "cfire::os::getenv(key, fallback)", "Reads environment variable `key`, returning `fallback` if unset.", "mode := os.getenv(\"ENV\", \"dev\")"},
                {"getenv_opt", "fn getenv_opt(key: str) -> str?", "cfire::os::getenv_opt(key)", "Reads environment variable `key`, returning `none` if unset.", "val := os.getenv_opt(\"API_KEY\") or { return }"},
                {"setenv", "fn setenv(key: str, value: str)", "cfire::os::setenv(key, value)", "Sets environment variable `key` to `value` in the current process.", "os.setenv(\"MODE\", \"prod\")"},
                {"cwd", "fn cwd() -> str", "cfire::os::cwd()", "Returns the current working directory path.", "dir := os.cwd()"},
                {"getpid", "fn getpid() -> i64", "cfire::os::getpid()", "Returns the process ID (PID) of the current process.", "pid := os.getpid()"},
                {"stdin", "fn stdin() -> File", "cfire::os::stdin_file()", "Returns a non-owning `fs.File` stream handle wrapping standard input (FD 0).", "line := os.stdin().read_line() or { return }"},
                {"stdout", "fn stdout() -> File", "cfire::os::stdout_file()", "Returns a non-owning `fs.File` stream handle wrapping standard output (FD 1).", "_ = try os.stdout().write_text(\"ok\\n\")"},
                {"stderr", "fn stderr() -> File", "cfire::os::stderr_file()", "Returns a non-owning `fs.File` stream handle wrapping standard error (FD 2).", "_ = try os.stderr().write_text(\"warn\\n\")"},
                {"pipe", "fn pipe() -> (File, File)!", "cfire::os::pipe()", "Creates an OS pipe returning an owning `(reader, writer)` tuple of `fs.File` handles with `FD_CLOEXEC` enabled.", "(mut r, mut w) := try os.pipe()"},
                {"is_tty", "fn is_tty() -> bool", "cfire::os::is_tty()", "Returns `true` if standard input refers to an interactive terminal (or call `.is_tty()` on any `fs.File`).", "if os.is_tty() { ... }"},
                {"term_size", "fn term_size() -> TermSize", "cfire::os::term_size()", "Queries terminal dimensions (`cols`, `rows`) via `TIOCGWINSZ`, defaulting to 80x24 if not a TTY.", "sz := os.term_size()"},
                {"RawTerminal.make", "fn make() -> RawTerminal!", "cfire::os::make_raw()", "`make(os.RawTerminal)` puts standard input into raw terminal mode (`cfmakeraw`) and returns an RAII guard that restores `termios` when it goes out of scope.", "raw := try make(os.RawTerminal)"},
                {"on_signal", "fn on_signal(sig: i32, handler: fn(i32))", "cfire::os::on_signal(sig, handler)", "Registers a callback handler for POSIX signal `sig` (e.g., `SIGINT`, `SIGTERM`, `SIGWINCH`).", "os.on_signal(28, |sig| { println(\"resized: {}\", sig) })"},
                {"exec_output", "fn exec_output(cmd: str) -> str!", "cfire::os::exec_output(cmd)", "Executes a shell command via `popen` and returns its captured stdout.", "out := try os.exec_output(\"uname -s\")"},
                {"exec", "fn exec(cmd: str) -> str!", "cfire::os::exec_output(cmd)", "Executes a shell command via `popen` and returns its captured stdout.", "out := try os.exec(\"uname -s\")"},
                {"command", "fn command(binary: str, args: []str) -> ExecResult!", "cfire::os::command(binary, args)", "Spawns a subprocess with explicit argument list via `posix_spawnp` (zero shell injection risk) and captures stdout/stderr.", "res := try os.command(\"git\", [\"status\"])"}
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
            "core:regex", "regex", "ECMAScript Regular Expressions", "(header-only)",
            "Pattern matching, group extraction, and text substitution backed by C++23 `<regex>`.",
            {
                {"compile", "fn compile(pattern: str) -> Regex!", "cfire::regex::compile(pattern)", "Compiles an ECMAScript regex pattern into a reusable Regex object.", "re := try regex.compile(\"\\\\d+\")"},
                {"is_match", "fn is_match(pattern: str, text: str) -> bool!", "cfire::regex::is_match(pattern, text)", "Tests whether a regex pattern matches text.", "ok := try regex.is_match(\"^[a-z]+$\", s)"},
{"find", "fn find(pattern: str, text: str) -> str?!", "cfire::regex::find(pattern, text)", "Returns the first match of `pattern` in `text`, or `none`; an invalid pattern is an error.", "word := try regex.find(\"[a-z]+\", s) or { return }"},
                {"find_all", "fn find_all(pattern: str, text: str) -> []str!", "cfire::regex::find_all(pattern, text)", "Returns every non-overlapping match of `pattern` in `text`.", "nums := try regex.find_all(\"[0-9]+\", line)"},
                                {"replace_all", "fn replace_all(pattern: str, text: str, repl: str) -> str!", "cfire::regex::replace_all(pattern, text, repl)", "Replaces all occurrences matching pattern with repl.", "res := try regex.replace_all(\"\\s+\", s, \" \")"},
                {"Regex.is_match", "fn (r: Regex) is_match(text: str) -> bool", "r.is_match(text)", "Tests whether the compiled pattern matches anywhere in `text`.", "if re.is_match(line) { ... }"},
                {"Regex.find", "fn (r: Regex) find(text: str) -> str?", "r.find(text)", "Returns the first match in `text`, or `none`.", "m := re.find(line) or { continue }"},
                {"Regex.find_all", "fn (r: Regex) find_all(text: str) -> []str", "r.find_all(text)", "Returns every non-overlapping match in `text`.", "nums := re.find_all(line)"},
                {"Regex.replace_all", "fn (r: Regex) replace_all(text: str, repl: str) -> str", "r.replace_all(text, repl)", "Replaces every match in `text` with `repl` (`$1` refers to a group).", "clean := re.replace_all(s, \" \")"},
                {"Regex.pattern", "fn (r: Regex) pattern() -> str", "r.pattern()", "Returns the source pattern.", "println(re.pattern())"}
            }
        },
        {
            "core:sync", "sync", "Thread-Safe Typed Channels & Mutexes", "(header-only)",
            "Go-style typed MPSC/MPMC channels (`Channel[T]`) with non-blocking and timeout receives, plus movable mutual exclusion locks (`Mutex`).",
            {
                {"Channel.make", "fn make[T](capacity: usize = 0) -> Channel[T]", "cfire::sync::make_channel<T>(capacity)", "`make(sync.Channel[T], capacity)` creates a shared, thread-safe channel of element type `T` (`0` = unbuffered).", "ch := make(sync.Channel[i32], 8)"},
                {"Mutex.make", "fn make[T](initial: T) -> Mutex[T]", "cfire::sync::make_mutex<T>(initial)", "`make(sync.Mutex[T], initial)` creates a mutex protecting a value of type `T`; `make(sync.Mutex[void])` is a pure lock.", "mu := make(sync.Mutex[i64], 0)"},
                {"Atomic.make", "fn make[T](initial: T) -> Atomic[T]", "cfire::sync::make_atomic<T>(initial)", "`make(sync.Atomic[T], initial)` creates an atomic variable holding `initial`.", "hits := make(sync.Atomic[i64], 0)"},
                {"Channel.send", "fn (ch: Channel[T]) send(val: T)", "ch.send(val)", "Sends a value into the channel and wakes one waiting receiver.", "ch.send(42)"},
                {"Channel.recv", "fn (ch: Channel[T]) recv() -> T!", "ch.recv()", "Blocks until a value is available or returns an error if the channel is closed and empty.", "v := ch.recv() or 0"},
                {"Channel.try_recv", "fn (ch: Channel[T]) try_recv() -> T?", "ch.try_recv()", "Attempts a non-blocking receive from the channel, returning `none` immediately if the queue is empty.", "v := ch.try_recv() or { return }"},
                {"Channel.recv_timeout", "fn (ch: Channel[T]) recv_timeout(timeout_ms: i64) -> T?", "ch.recv_timeout(timeout_ms)", "Waits up to `timeout_ms` milliseconds for a value from the channel, returning `none` on timeout or closed empty channel.", "v := ch.recv_timeout(50) or { return }"},
                {"Channel.recv_ctx", "fn (ch: Channel[T]) recv_ctx(ctx: Context) -> T!", "ch.recv_ctx(ctx)", "Receives from the channel or aborts immediately with an error if `ctx` is cancelled.", "v := try ch.recv_ctx(ctx)"},
                {"Channel.len", "fn (ch: Channel[T]) len() -> usize", "ch.len()", "Returns the number of buffered items currently queued in the channel.", "n := ch.len()"},
                {"Channel.close", "fn (mut ch: Channel[T]) close()", "ch.close()", "Closes the channel and wakes all waiting receivers.", "ch.close()"},
                {"Mutex", "type Mutex[T] struct", "cfire::sync::MutexT<T>", "Mutual exclusion (CF-154): `Mutex[T]` protects a value of type `T`; `Mutex[void]` is a pure lock. Copies share the same lock.", "mu := make(sync.Mutex[i64], 0)"},
                {"Mutex.lock", "fn (m: Mutex[T]) lock() -> MutexGuard[T]", "m.lock()", "`Mutex[T]`: blocks, then returns a guard that gives access to the value and unlocks when it goes out of scope. `Mutex[void]`: acquires the lock; pair it with `unlock()`.", "mut g := mu.lock()"},
                {"Mutex.unlock", "fn (m: Mutex[void]) unlock()", "m.unlock()", "Releases a `Mutex[void]` acquired with `lock()`.", "lk.unlock()"},
                {"Mutex.try_lock", "fn (m: Mutex[T]) try_lock() -> MutexGuard[T]?", "m.try_lock()", "Non-blocking `lock()`: `none` (`false` for `Mutex[void]`) when another task holds the lock.", "if g := mu.try_lock() { ... }"},
                {"Mutex.with_lock", "fn (m: Mutex[T]) with_lock(f: fn(mut T) -> R) -> R", "m.with_lock(f)", "Runs `f` with the protected value while holding the lock (`fn() -> R` for `Mutex[void]`) and unlocks when it returns.", "n := mu.with_lock(|mut v| { v += 1; return v })"},
                {"Mutex.get", "fn (m: Mutex[T]) get() -> T", "m.get()", "Copy of the protected value, read under the lock.", "total := mu.get()"},
                {"Mutex.set", "fn (m: Mutex[T]) set(v: T)", "m.set(v)", "Replaces the protected value under the lock.", "mu.set(0)"},
                {"Atomic", "type Atomic[T] struct", "cfire::sync::Atomic<T>", "Atomic variable.", "hits := make(sync.Atomic[i64], 0)"},
                {"Atomic.load", "fn (a: Atomic[T]) load() -> T", "a.load()", "Loads the atomic value.", "v := a.load()"},
                {"Atomic.store", "fn (a: Atomic[T]) store(val: T)", "a.store(val)", "Stores a value into the atomic.", "a.store(10)"},
                {"Atomic.add", "fn (a: Atomic[T]) add(delta: T) -> T", "a.add(delta)", "Adds delta to atomic value.", "a.add(1)"},
                {"Atomic.sub", "fn (a: Atomic[T]) sub(delta: T) -> T", "a.sub(delta)", "Subtracts delta from atomic value.", "a.sub(1)"}
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
            "core:tako", "tako", "Tako Cloud SDK Primitives (S3, SQS)", "(header-only)",
            "Cloud primitives and storage compatibility services (`core:tako`).",
            {
                {"NewS3Service", "fn NewS3Service() -> S3Service", "cfire::tako::NewS3Service()", "Creates an S3 compatible storage service client.", "s3 := tako.NewS3Service()"},
                {"NewSQSService", "fn NewSQSService() -> SQSService", "cfire::tako::NewSQSService()", "Creates an SQS compatible message queue service client.", "sqs := tako.NewSQSService()"},
                {"S3Service.CreateBucket", "fn (mut s: S3Service) CreateBucket(name: str) -> void!", "s.CreateBucket(name)", "Creates a storage bucket.", "try s3.CreateBucket(\"b\")"},
                {"S3Service.PutObject", "fn (mut s: S3Service) PutObject(bucket: str, key: str, data: str, content_type: str) -> str!", "s.PutObject(bucket, key, data, content_type)", "Uploads an object and returns its ETag.", "etag := try s3.PutObject(\"b\", \"k\", \"d\", \"text/plain\")"},
                {"S3Service.GetObject", "fn (mut s: S3Service) GetObject(bucket: str, key: str) -> S3Object!", "s.GetObject(bucket, key)", "Downloads an object from storage.", "obj := try s3.GetObject(\"b\", \"k\")"},
                {"SQSService.CreateQueue", "fn (mut s: SQSService) CreateQueue(name: str, fifo: bool = false) -> str!", "s.CreateQueue(name, fifo)", "Creates a message queue and returns its URL.", "q := try sqs.CreateQueue(\"q\", true)"},
                {"SQSService.SendMessage", "fn (mut s: SQSService) SendMessage(queue_url: str, body: str, group: str = \"\", dedup: str = \"\") -> str!", "s.SendMessage(queue_url, body, group, dedup)", "Publishes a message to the queue.", "id := try sqs.SendMessage(q, \"body\")"},
                {"SQSService.ReceiveMessages", "fn (mut s: SQSService) ReceiveMessages(queue_url: str, max_messages: i32 = 1) -> []SQSMessage!", "s.ReceiveMessages(queue_url, max_messages)", "Receives available messages from the queue.", "msgs := try sqs.ReceiveMessages(q, 1)"}
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
                {"duration_nanos", "fn duration_nanos(n: i64) -> Duration", "cfire::time::duration_nanos(n)", "Constructs a Duration from nanoseconds.", "d := time.duration_nanos(1000)"},
                {"duration_micros", "fn duration_micros(us: i64) -> Duration", "cfire::time::duration_micros(us)", "Constructs a Duration from microseconds.", "d := time.duration_micros(500)"},
                {"duration_minutes", "fn duration_minutes(m: i64) -> Duration", "cfire::time::duration_minutes(m)", "Constructs a Duration from minutes.", "d := time.duration_minutes(5)"},
                {"duration_hours", "fn duration_hours(h: i64) -> Duration", "cfire::time::duration_hours(h)", "Constructs a Duration from hours.", "d := time.duration_hours(2)"},
                {"from_unix_seconds", "fn from_unix_seconds(sec: i64) -> Time", "cfire::time::from_unix_seconds(sec)", "Constructs a Time instant from Unix epoch seconds.", "t := time.from_unix_seconds(1700000000)"},
                {"sleep", "fn sleep(d: Duration)", "cfire::time::sleep(d)", "Sleeps the current thread for duration `d`.", "time.sleep(time.duration_millis(50))"}
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
        },
        {
            "ext:ai", "ai", "On-Device & Cloud AI, Agent Orchestration, Document Ingestion & MCP", "(POSIX sockets + TLS + AVX2/NEON SIMD + NaturalLanguage / PDFKit / Vision / Tesseract)",
            "Unified AI SDK built around stateful `Client` and `Agent` objects supporting Cross-Platform On-Device Intelligence (`ai.local()` / `ai.apple()`: `NaturalLanguage`/`PDFKit`/`Vision` on Apple; dynamic `libtesseract`/`pdftotext` + pure C++23 decoders and AVX2/NEON SIMD on Linux/Windows/WASM), OpenAI (`ai.openai()`), Anthropic Claude (`ai.anthropic()`), Google Gemini (`ai.gemini()`), Ollama (`ai.ollama()`), vLLM/llama.cpp (`ai.openai_compatible()`), and deterministic offline mocks (`ai.mock()`, `ai.mock_sequence()`), with multi-turn tool-calling agents (`make(ai.Agent, client, model)`), self-correcting structured outputs (`Client.generate_struct[T]`, `Agent.run_struct[T]`, `Agent.run_struct_validated[T]`) backed by compile-time `validate:\"...\"` struct tags, stateful client embeddings/OCR/language recognition (`Client.embed`, `Client.embed_image`, `Client.ocr`, `Client.detect_language`), pure stateless document parsers (`load_document`, `chunk_document`), SIMD vector math (`cosine_similarity`, `l2_distance`, `dot_product`, `normalize`), and a Model Context Protocol server (`McpServer`) that shares tool definitions with `Agent` (`use_mcp`).",
            {
                {"local", "fn local() -> Client", "cfire::ai::local()", "Creates a cross-platform on-device `Client` (`default_model: \"local\"`) backed by Apple `NaturalLanguage`/`Vision`/`PDFKit` on macOS/iOS and `libtesseract`/`pdftotext` + pure C++23 SIMD embeddings on Linux/Windows/WASM.", "client := ai.local()"},
                {"apple", "fn apple() -> Client", "cfire::ai::apple()", "Alias for `ai.local()` creating an on-device `Client` (`default_model: \"local\"`).", "client := ai.apple()"},
                {"openai", "fn openai(api_key: str = \"\", model: str = \"gpt-4o-mini\") -> Client", "cfire::ai::openai(api_key, model)", "Creates an OpenAI client (reads `OPENAI_API_KEY` if `api_key` is empty).", "client := ai.openai()"},
                {"anthropic", "fn anthropic(api_key: str = \"\", model: str = \"claude-3-5-sonnet-latest\") -> Client", "cfire::ai::anthropic(api_key, model)", "Creates an Anthropic Claude client (reads `ANTHROPIC_API_KEY` if `api_key` is empty).", "client := ai.anthropic()"},
                {"gemini", "fn gemini(api_key: str = \"\", model: str = \"gemini-2.5-flash\") -> Client", "cfire::ai::gemini(api_key, model)", "Creates a Google Gemini client (reads `GEMINI_API_KEY` or `GOOGLE_API_KEY` if `api_key` is empty).", "client := ai.gemini()"},
                {"ollama", "fn ollama(host: str = \"http://127.0.0.1:11434\", model: str = \"llama3.2\") -> Client", "cfire::ai::ollama(host, model)", "Creates a local Ollama client (uses `OLLAMA_HOST` if set).", "client := ai.ollama()"},
                {"openai_compatible", "fn openai_compatible(base_url: str, api_key: str = \"\", model: str = \"default\") -> Client", "cfire::ai::openai_compatible(base_url, api_key, model)", "Creates a client for any OpenAI-compatible server (`vLLM`, `llama-server`, `LM Studio`, `Groq`, `Together`, `DeepSeek`).", "client := ai.openai_compatible(\"http://127.0.0.1:8080/v1\")"},
                {"mock", "fn mock(handler: fn([]Message, []Tool) -> ChatResponse!) -> Client", "cfire::ai::mock(handler)", "Creates a deterministic offline mock `Client` for unit-testing multi-turn `Agent` tool loops and structured output validation retries without network access.", "client := ai.mock(|msgs, tools| -> ai.ChatResponse! { ... })"},
                {"mock_sequence", "fn mock_sequence(responses: []ChatResponse) -> Client", "cfire::ai::mock_sequence(responses)", "Creates a deterministic offline `Client` that replays a scripted sequence of `ChatResponse` turns in order.", "client := ai.mock_sequence(turns)"},
                {"user", "fn user(content: str) -> Message", "cfire::ai::user(content)", "Constructs a user role `Message`.", "m := ai.user(\"Summarize this file.\")"},
                {"user_with_file", "fn user_with_file(content: str, file_path: str) -> Message!", "cfire::ai::user_with_file(content, file_path)", "Constructs a multimodal user `Message` with an attached file (`.png`, `.jpg`, `.webp`, `.bmp`, `.pdf`, `.docx`, `.pptx`, `.xlsx`, `.odt`, `.odp`, `.ods`, `.epub`, `.html`, `.csv`, `.wav`).", "m := try ai.user_with_file(\"Describe this diagram\", \"arch.png\")"},
                {"load_attachment", "fn load_attachment(path: str) -> Attachment!", "cfire::ai::load_attachment(path)", "Loads a file from disk into a MIME-typed multimodal `Attachment` (populating extracted Markdown automatically for Office/OpenDocument/EPUB/text formats).", "att := try ai.load_attachment(\"slides.pptx\")"},
                {"detect_mime_type", "fn detect_mime_type(path_or_name: str, data: []byte = []) -> str", "cfire::ai::detect_mime_type(path_or_name, data)", "Detects the MIME type of a file from magic bytes (`%PDF-`, `PNG`, `BMP`, `JPEG`, `WEBP`, `ZIP`/OOXML/ODF/EPUB) and filename extension.", "mime := ai.detect_mime_type(\"report.pdf\")"},
                {"load_document", "fn load_document(path: str) -> Document!", "cfire::ai::load_document(path)", "Extracts structured Markdown and per-page/per-slide `sections` (`[]DocumentSection`) from `.pdf` (Apple `PDFKit`, Linux `pdftotext`, or portable C++23 `/FlateDecode` + `/ToUnicode` CMap), `.docx`, `.pptx`, `.xlsx`, `.odt`, `.odp`, `.ods`, `.epub`, `.html`, `.md`, `.csv`, or image files.", "doc := try ai.load_document(\"deck.pptx\")"},
                {"parse_document", "fn parse_document(data: []byte, filename: str = \"document.txt\") -> Document!", "cfire::ai::parse_document(data, filename)", "Parses an in-memory byte slice or string into a structured `Document` with page/slide `sections`.", "doc := try ai.parse_document(raw_bytes, \"spec.docx\")"},
                {"chunk_document", "fn chunk_document(doc: Document, max_chars: i32 = 512, overlap: i32 = 64) -> []DocumentSection", "cfire::ai::chunk_document(doc, max_chars, overlap)", "Splits a `Document` into RAG-ready chunks while preserving each chunk's 1-based page/slide `index` and section `title`.", "chunks := ai.chunk_document(doc, 512, 64)"},
                {"system", "fn system(content: str) -> Message", "cfire::ai::system(content)", "Constructs a system instruction `Message`.", "m := ai.system(\"You are a concise coding assistant.\")"},
                {"assistant", "fn assistant(content: str) -> Message", "cfire::ai::assistant(content)", "Constructs an assistant role `Message`.", "m := ai.assistant(\"Here is the summary:\")"},
                {"tool_result", "fn tool_result(tool_call_id: str, tool_name: str, content: str, is_error: bool = false) -> Message", "cfire::ai::tool_result(tool_call_id, tool_name, content, is_error)", "Constructs a tool-result `Message` correlated with a model `ToolCall` (`id` and `name`).", "m := ai.tool_result(tc.id, tc.name, output)"},
                {"tool_def", "fn tool_def[T](name: str, description: str) -> Tool", "cfire::ai::tool_def<T>(name, description)", "Constructs a typed LLM `Tool` definition whose JSON Schema is derived at compile time from struct `T`.", "t := ai.tool_def[WeatherArgs](\"get_weather\", \"Look up current weather\")"},
                {"Client.set_model", "fn (mut c: Client) set_model(model: str)", "c.set_model(model)", "Sets the client's stateful `default_model` used by 1-argument `ask`, `embed`, `embed_image`, and `generate_struct[T]` calls.", "client.set_model(\"gpt-4o\")"},
                {"Client.model", "fn (c: Client) model() -> str", "c.model()", "Returns the client's active `default_model`.", "m := client.model()"},
                {"Client.ask", "fn (c: Client) ask(prompt: str) -> str!", "c.ask(prompt)", "Executes a single-turn prompt using `c.model()` (or pass `(model, prompt, system_prompt)` to override) and returns the assistant text response.", "reply := try client.ask(\"Hello!\")"},
                {"Client.ask_with_file", "fn (c: Client) ask_with_file(prompt: str, file_path: str) -> str!", "c.ask_with_file(prompt, file_path)", "Executes a multimodal prompt with an attached image, PDF, slide deck, or document.", "ans := try client.ask_with_file(\"Summarize this PDF\", \"paper.pdf\")"},
                {"Client.chat", "fn (c: Client) chat(messages: []Message) -> ChatResponse!", "c.chat(messages)", "Executes a multi-turn chat completion returning text, tool calls, and token usage.", "res := try client.chat(msgs)"},
                {"Client.chat_with_tools", "fn (c: Client) chat_with_tools(messages: []Message, tools: []Tool) -> ChatResponse!", "c.chat_with_tools(messages, tools)", "Executes a chat completion with compile-time schema-typed `tools` and returns any requested `ToolCall` invocations.", "res := try client.chat_with_tools(msgs, [weather_tool])"},
                {"Client.stream", "fn (c: Client) stream(messages: []Message, on_delta: fn(str)) -> str!", "c.stream(messages, on_delta)", "Streams tokens in real time via SSE across OpenAI, Anthropic, Gemini, or Ollama, invoking `on_delta` for each token chunk.", "full := try client.stream(msgs, |tok| { print(\"{}\", tok) })"},
                {"Client.generate_struct", "fn (c: Client) generate_struct[T](prompt: str) -> T!", "c.generate_struct<T>(prompt)", "Enforces `json.schema[T]()` on the model response, parses and validates `T` (including `validate:\"...\"` constraints), and automatically feeds validation errors back to the model to self-correct.", "inv := try client.generate_struct[Invoice](raw_text)"},
                {"Client.generate_struct_from_file", "fn (c: Client) generate_struct_from_file[T](prompt: str, file_path: str) -> T!", "c.generate_struct_from_file<T>(prompt, file_path)", "Extracts a strongly-typed `cfire` struct `T` directly from an image, PDF, slide deck, or document using compile-time `json.schema[T]()` with self-correcting validation retries.", "inv := try client.generate_struct_from_file[Invoice](\"Extract fields\", \"receipt.png\")"},
                {"Client.embed", "fn (c: Client) embed(text: str) -> []f32!", "c.embed(text)", "Generates a normalized `[]f32` embedding vector using `c`'s provider and `default_model` (or pass `(model, text)` to override).", "vec := try client.embed(\"compile-time memory safety\")"},
                {"Client.embed_image", "fn (c: Client) embed_image(path: str) -> []f32!", "c.embed_image(path)", "Computes a normalized `[]f32` visual/multimodal embedding vector for an image file using `c`'s provider (`ai.local()` decodes PNG/BMP/PPM pixels in pure C++23 with 4x4 RGB/Luma moments, Sobel HOG edge histograms, 8x8 dHash, and fused OCR semantics).", "ivec := try client.embed_image(\"chart.png\")"},
                {"Client.ocr", "fn (c: Client) ocr(path: str) -> str!", "c.ocr(path)", "Extracts text from an image file using `c`'s provider (`Vision.framework` on macOS/iOS, `libtesseract.so.5` / `tesseract` CLI on Linux, PNG/JPEG metadata, or cloud multimodal vision).", "text := try client.ocr(\"receipt.png\")"},
                {"Client.detect_language", "fn (c: Client) detect_language(text: str) -> str", "c.detect_language(text)", "Detects the dominant BCP-47 language code (e.g. `\"en\"`, `\"es\"`, `\"ja\"`) using Apple `NLLanguageRecognizer` on macOS/iOS or portable UTF-8 script/stopword classification.", "lang := client.detect_language(\"Bonjour tout le monde\")"},
                {"cosine_similarity", "fn cosine_similarity(a: []f32, b: []f32) -> f32", "cfire::ai::cosine_similarity(a, b)", "Computes the cosine similarity in `[-1.0, 1.0]` between two `[]f32` embedding vectors.", "sim := ai.cosine_similarity(v1, v2)"},
                {"l2_distance", "fn l2_distance(a: []f32, b: []f32) -> f32", "cfire::ai::l2_distance(a, b)", "Computes the Euclidean (L2) distance between two `[]f32` vectors.", "dist := ai.l2_distance(v1, v2)"},
                {"dot_product", "fn dot_product(a: []f32, b: []f32) -> f32", "cfire::ai::dot_product(a, b)", "Computes the inner product of two `[]f32` vectors.", "dot := ai.dot_product(v1, v2)"},
                {"normalize", "fn normalize(v: []f32) -> []f32", "cfire::ai::normalize(v)", "Returns an L2-normalized unit vector copy of `v`.", "unit := ai.normalize(raw_vec)"},
                {"chunk_text", "fn chunk_text(text: str, max_chars: i32 = 512, overlap: i32 = 64) -> []str", "cfire::ai::chunk_text(text, max_chars, overlap)", "Splits a document into overlapping paragraph/sentence-aware chunks for RAG indexing.", "chunks := ai.chunk_text(markdown, 512, 64)"},
                {"Agent.make", "fn make(client: Client, model: str = \"\", system_prompt: str = \"\") -> Agent", "cfire::ai::make_agent(client, model, system_prompt)", "`make(ai.Agent, client, model, system_prompt)` creates a stateful multi-turn `Agent` that runs tool calls through `client`.", "mut agent := make(ai.Agent, client, \"gpt-4o\")"},
                {"Agent.register_tool", "fn (mut a: Agent) register_tool[Args](name: str, description: str, handler: fn(Args) -> R!)", "a.register_tool<Args>(name, description, handler)", "Registers a schema-typed tool on the `Agent` (`handler` may return `str`, `str!`, or any JSON-serializable struct `R` / `R!`; errors are returned to the LLM to self-correct).", "agent.register_tool[LookupArgs](\"lookup\", \"Lookup item\", |args| { return item })"},
                {"Agent.register_tool_action", "fn (mut a: Agent) register_tool_action(name: str, description: str, handler: fn() -> R!)", "a.register_tool_action(name, description, handler)", "Registers a parameterless tool on the `Agent`.", "agent.register_tool_action(\"ping\", \"Health check\", || { return \"pong\" })"},
                {"Agent.use_mcp", "fn (mut a: Agent) use_mcp(mcp: McpServer)", "a.use_mcp(mcp)", "Mounts all tools registered on `mcp` directly into the `Agent`.", "agent.use_mcp(mcp)"},
                {"Agent.set_max_steps", "fn (mut a: Agent) set_max_steps(steps: i32)", "a.set_max_steps(steps)", "Configures the maximum number of tool-calling turns per `run` (default `8`).", "agent.set_max_steps(10)"},
                {"Agent.set_max_retries", "fn (mut a: Agent) set_max_retries(retries: i32)", "a.set_max_retries(retries)", "Configures the maximum number of self-correction retries for tool or output validation failures (default `2`).", "agent.set_max_retries(3)"},
                {"Agent.run", "fn (mut a: Agent) run(prompt: str) -> str!", "a.run(prompt)", "Runs the multi-turn agent loop until the model produces a final text response.", "reply := try agent.run(\"Check the deployment status\")"},
                {"Agent.run_messages", "fn (mut a: Agent) run_messages(messages: []Message) -> ChatResponse!", "a.run_messages(messages)", "Appends `messages` to the agent history and runs the multi-turn tool loop, returning the final `ChatResponse`.", "res := try agent.run_messages([ai.user(\"Hello\")])"},
                {"Agent.run_struct", "fn (mut a: Agent) run_struct[Out](prompt: str) -> Out!", "a.run_struct<Out>(prompt)", "Runs the multi-turn agent loop and extracts a validated struct `Out` (automatically retrying on schema or `validate:\"...\"` failures).", "report := try agent.run_struct[Report](\"Generate the status report\")"},
                {"Agent.run_struct_validated", "fn (mut a: Agent) run_struct_validated[Out](prompt: str, validator: fn(Out) -> void!) -> Out!", "a.run_struct_validated<Out>(prompt, validator)", "Runs the multi-turn agent loop, parses `Out`, and executes custom cross-field `validator` logic (feeding any `error(...)` back to the model to self-correct).", "report := try agent.run_struct_validated[Report](\"Summarize\", |r: Report| -> void! { ... })"},
                {"Agent.history", "fn (a: Agent) history() -> []Message", "a.history()", "Returns the accumulated multi-turn conversation and tool-call `[]Message` history.", "msgs := agent.history()"},
                {"Agent.clear_history", "fn (mut a: Agent) clear_history()", "a.clear_history()", "Clears the agent's conversation history.", "agent.clear_history()"},
                {"McpServer.make", "fn make(name: str, version: str = \"1.0.0\") -> McpServer", "cfire::ai::make_mcp_server(name, version)", "`make(ai.McpServer, name, version)` creates a Model Context Protocol (MCP) server that serves registered tools over stdio.", "mut mcp := make(ai.McpServer, \"docs\", \"1.0.0\")"},
                {"McpServer.register_tool", "fn (mut s: McpServer) register_tool[T](name: str, description: str, handler: fn(T) -> R!)", "s.register_tool<T>(name, description, handler)", "Registers an MCP tool whose input schema is derived from `T` (`json.schema[T]()`) and whose handler may return `str`, `str!`, or any JSON-serializable struct `R` / `R!`.", "mcp.register_tool[Query](\"search\", \"Search docs\", |q| { return \"ok\" })"},
                {"McpServer.register_tool_action", "fn (mut s: McpServer) register_tool_action(name: str, description: str, handler: fn() -> R!)", "s.register_tool_action(name, description, handler)", "Registers a parameterless MCP tool.", "mcp.register_tool_action(\"status\", \"Server status\", || { return \"ready\" })"},
                {"McpServer.handle_jsonrpc", "fn (mut s: McpServer) handle_jsonrpc(req_json: str) -> str", "s.handle_jsonrpc(req_json)", "Processes a single MCP JSON-RPC 2.0 request (`initialize`, `tools/list`, `tools/call`, `ping`) and returns the JSON-RPC response string.", "resp := mcp.handle_jsonrpc(line)"},
                {"McpServer.serve_stdio", "fn (mut s: McpServer) serve_stdio() -> i32", "s.serve_stdio()", "Runs the MCP JSON-RPC 2.0 server loop over `stdin`/`stdout`.", "mcp.serve_stdio()"},
                {"create_zip", "fn create_zip(entries: map[str]str) -> []byte", "cfire::ai::create_zip(entries)", "Constructs an in-memory zip archive from filename-content pairs.", "z := ai.create_zip(entries)"}
            }
        },
        {
            "ext:compress/zstd", "zstd", "Native Zstandard Frame Compression", "-lzstd",
            "Hardware-accelerated Zstandard frame compression and decompression backed by native `libzstd`.",
            {
                {"compress", "fn compress(input: []byte, level: i32 = 3) -> []byte!", "cfire::zstd::compress(input, level)", "Compresses a byte slice into a Zstandard frame.", "blob := try zstd.compress(raw, level: 3)"},
                {"compress_str", "fn compress_str(input: str, level: i32 = 3) -> []byte!", "cfire::zstd::compress_str(input, level)", "Compresses a string into a Zstandard frame.", "blob := try zstd.compress_str(s, 3)"},
                {"decompress", "fn decompress(frame: []byte) -> []byte!", "cfire::zstd::decompress(frame)", "Decompresses a Zstandard frame using the embedded frame content size.", "orig := try zstd.decompress(blob)"},
                {"decompress_str", "fn decompress_str(frame: []byte) -> str!", "cfire::zstd::decompress_str(frame)", "Decompresses a Zstandard frame into a UTF-8 string.", "orig := try zstd.decompress_str(blob)"}
            }
        },
        {
            "ext:crypto/sodium", "sodium", "Memory-Hard Argon2id KDF, BLAKE2b, Subkey Derivation & XChaCha20-Poly1305 AEAD", "-lsodium",
            "Modern cryptographic primitives backed by `libsodium` (`crypto_pwhash` Argon2id password hashing, `crypto_generichash` keyed/unkeyed BLAKE2b, `crypto_kdf_derive_from_key` subkey derivation, and `crypto_aead_xchacha20poly1305_ietf` 192-bit nonce AEAD).",
            {
                {"argon2id", "fn argon2id(password: str, salt: []byte, out_len: usize = 32, opslimit: u64 = 2, memlimit: usize = 67108864) -> []byte!", "cfire::sodium::argon2id(password, salt, out_len, opslimit, memlimit)", "Derives a cryptographic key of `out_len` bytes from `password` and a 16-byte `salt` using Argon2id.", "key := try sodium.argon2id(master_pw, salt, 32)"},
                {"argon2id_str", "fn argon2id_str(password: str, opslimit: u64 = 2, memlimit: usize = 67108864) -> str!", "cfire::sodium::argon2id_str(password, opslimit, memlimit)", "Hashes a password into a PHC-formatted `$argon2id$...` verification string.", "encoded := try sodium.argon2id_str(password)"},
                {"argon2id_verify", "fn argon2id_verify(encoded: str, password: str) -> bool", "cfire::sodium::argon2id_verify(encoded, password)", "Verifies a password against an encoded Argon2id hash string in constant time.", "ok := sodium.argon2id_verify(encoded, password)"},
                {"blake2b", "fn blake2b(input: []byte | str, out_len: usize = 32, key: []byte = []) -> []byte!", "cfire::sodium::blake2b(input, out_len, key)", "Computes a keyed or unkeyed BLAKE2b hash (`crypto_generichash`) of `out_len` bytes (16..64).", "mac := try sodium.blake2b(payload, 32, key)"},
                {"derive_subkey", "fn derive_subkey(master_key: []byte, subkey_id: u64, context: str, out_len: usize = 32) -> []byte!", "cfire::sodium::derive_subkey(master_key, subkey_id, context, out_len)", "Derives a domain-separated subkey of `out_len` bytes (16..64) from a 32-byte `master_key`, `subkey_id`, and 8-byte `context` (`crypto_kdf_derive_from_key`).", "sub := try sodium.derive_subkey(master_key, 1, \"flintkdf\", 32)"},
                {"xchacha20poly1305_encrypt", "fn xchacha20poly1305_encrypt(key: []byte, plaintext: []byte, aad: []byte = []) -> []byte!", "cfire::sodium::xchacha20poly1305_encrypt(key, pt, aad)", "Encrypts `plaintext` with a 32-byte key using XChaCha20-Poly1305, returning `[24-byte random nonce || ciphertext || 16-byte Poly1305 tag]`.", "sealed := try sodium.xchacha20poly1305_encrypt(key, secret)"},
                {"xchacha20poly1305_decrypt", "fn xchacha20poly1305_decrypt(key: []byte, combined: []byte, aad: []byte = []) -> []byte!", "cfire::sodium::xchacha20poly1305_decrypt(key, combined, aad)", "Authenticates and decrypts a `[24-byte nonce || ciphertext || 16-byte tag]` XChaCha20-Poly1305 payload.", "pt := try sodium.xchacha20poly1305_decrypt(key, sealed)"},
                {"random_bytes", "fn random_bytes(n: usize) -> []byte", "cfire::sodium::random_bytes(n)", "Generates `n` cryptographically secure random bytes (`randombytes_buf`).", "salt := sodium.random_bytes(16)"},
                {"memzero", "fn memzero(mut buf: []byte)", "cfire::sodium::memzero(buf)", "Securely zeroes sensitive key material in memory (`sodium_memzero`).", "sodium.memzero(mut key)"}
            }
        },
        {
            "ext:db/sqlite", "sqlite", "Embedded SQLite3 & SQLCipher with WAL, Vector Search & Row Mapping", "-lsqlite3 / -lsqlcipher",
            "RAII SQLite3 & SQLCipher wrapper enabling `PRAGMA journal_mode=WAL`, `open_encrypted` full-database encryption, parameterized queries, `[]f32` embedding BLOB binding, built-in SQL vector similarity functions (`vec_cosine_similarity`, `vec_l2_distance`, `vec_dot`), dynamic extension loading (`sqlite-vec`), atomic transactions, and compile-time `sql:\"...\"` struct row mapping.",
            {
                {"open", "fn open(path: str) -> Conn!", "cfire::sqlite::open(path)", "Opens an SQLite3 database connection, enables WAL mode, and registers vector SQL functions (`vec_cosine_similarity`, `vec_cosine_distance`, `vec_l2_distance`, `vec_dot`, `vec_dims`).", "mut db := try sqlite.open(\"app.db\")"},
                {"open_encrypted", "fn open_encrypted(path: str, key: str) -> Conn!", "cfire::sqlite::open_encrypted(path, key)", "Opens an SQLite3 / SQLCipher database and applies `PRAGMA key` encryption immediately before schema access.", "mut db := try sqlite.open_encrypted(\"vault.db\", master_key)"},
                {"Conn.apply_key", "fn (mut db: Conn) apply_key(key: str) -> void!", "db.apply_key(key)", "Applies an encryption key (`sqlite3_key` / `PRAGMA key`) to an open database connection.", "try db.apply_key(master_key)"},
                {"Conn.rekey", "fn (mut db: Conn) rekey(new_key: str) -> void!", "db.rekey(new_key)", "Rotates the database encryption key (`sqlite3_rekey` / `PRAGMA rekey`).", "try db.rekey(next_key)"},
                {"Conn.exec", "fn (mut db: Conn) exec(sql: str, ...args) -> void!", "db.exec(sql, args...)", "Executes a parameterized SQL statement (`?1`, `?2`, ...), including `[]f32` vector and `[]byte` BLOB parameters.", "try db.exec(\"INSERT INTO docs VALUES (?1, ?2)\", id, vec)"},
                {"Conn.query", "fn (mut db: Conn) query[T](sql: str, ...args) -> []T!", "db.query<T>(sql, args...)", "Executes a `SELECT` query and maps rows directly into struct `T` via `sql:\"...\"` tags (supporting `str`, `i64`, `f64`, `[]f32`, `[]byte`).", "rows := try db.query[Doc](\"SELECT id, text FROM docs ORDER BY vec_cosine_similarity(vec, ?1) DESC\", qvec)"},
                {"Conn.load_extension", "fn (mut db: Conn) load_extension(path: str, entry_point: str = \"\") -> void!", "db.load_extension(path, entry_point)", "Dynamically loads a native SQLite extension shared library (e.g. `sqlite-vec` `vec0` or `sqlite-vss`).", "try db.load_extension(\"./vec0\")"},
                {"Conn.tx", "fn (mut db: Conn) tx(block: fn(mut tx: Conn) -> void!) -> void!", "db.tx([&](Conn& tx) { ... })", "Runs a closure inside an atomic `BEGIN IMMEDIATE ... COMMIT` transaction with automatic rollback on error.", "try db.tx |mut tx| { ... }"}
            }
        },
        {
            "ext:net/ssh", "ssh", "First-Class SSH Client, PTY, SFTP & Port Forwarding", "-lssh (libssh)",
            "SSHv2 client, interactive PTY terminal sessions, one-shot command execution, SFTP file transfers, and TCP port forwarding backed by `libssh` (`CFIRE_HAS_LIBSSH=1`) with deterministic loopback/mock support (`ssh.enable_mock_server()`) for headless testing.",
            {
                {"connect", "fn connect(host: str, port: u16 = 22, user: str = \"\", timeout_ms: i64 = 10000) -> Session!", "cfire::ssh::connect(host, port, user, timeout_ms)", "Connects an SSH session to `host:port` as `user` (default: the local login name, like ssh(1)).", "mut sess := try ssh.connect(\"build-box\", 22, \"deploy\")"},
                {"enable_mock_server", "fn enable_mock_server(enabled: bool = true)", "cfire::ssh::enable_mock_server(enabled)", "Enables or disables the deterministic in-memory loopback SSH/PTY/SFTP mock server for unit tests.", "ssh.enable_mock_server(true)"},
                {"Session.host_key_fingerprint", "fn (sess: Session) host_key_fingerprint() -> str", "sess.host_key_fingerprint()", "Returns the connected server's public key fingerprint in `SHA256:<base64>` format.", "fp := sess.host_key_fingerprint()"},
                {"Session.is_known_host", "fn (sess: Session) is_known_host() -> bool", "sess.is_known_host()", "Checks whether the server's public key matches `~/.ssh/known_hosts`.", "if !sess.is_known_host() { ... }"},
                {"Session.write_known_host", "fn (mut sess: Session) write_known_host() -> void!", "sess.write_known_host()", "Appends the server's public key to `~/.ssh/known_hosts` (TOFU).", "try sess.write_known_host()"},
                {"Session.auth_publickey_mem", "fn (mut sess: Session) auth_publickey_mem(private_key_pem: str, passphrase: str = \"\") -> void!", "sess.auth_publickey_mem(private_key_pem, passphrase)", "Authenticates using an in-memory private key (PEM/OpenSSH) without writing temporary files to disk.", "try sess.auth_publickey_mem(pem_key, \"\")"},
                {"Session.auth_publickey_file", "fn (mut sess: Session) auth_publickey_file(private_key_path: str, passphrase: str = \"\") -> void!", "sess.auth_publickey_file(private_key_path, passphrase)", "Authenticates using a private key file on disk.", "try sess.auth_publickey_file(\"/Users/me/.ssh/id_ed25519\", \"\")"},
                {"Session.auth_password", "fn (mut sess: Session) auth_password(password: str) -> void!", "sess.auth_password(password)", "Authenticates using password authentication.", "try sess.auth_password(secret_pw)"},
                {"Session.auth_agent", "fn (mut sess: Session) auth_agent() -> void!", "sess.auth_agent()", "Authenticates via the local `ssh-agent` (`SSH_AUTH_SOCK`).", "try sess.auth_agent()"},
                {"Session.exec", "fn (mut sess: Session) exec(command: str) -> ExecResult!", "sess.exec(command)", "Executes a one-shot remote command and returns `ExecResult{exit_code, stdout, stderr}`.", "res := try sess.exec(\"uname -a\")"},
                {"Session.open_pty", "fn (mut sess: Session) open_pty(term: str = \"xterm-256color\", cols: i32 = 80, rows: i32 = 24) -> Channel!", "sess.open_pty(term, cols, rows)", "Opens an interactive PTY shell channel with initial terminal type and dimensions.", "mut ch := try sess.open_pty(\"xterm-256color\", 120, 36)"},
                {"Session.open_forward", "fn (mut sess: Session) open_forward(remote_host: str, remote_port: u16, source_host: str = \"127.0.0.1\", local_port: u16 = 0) -> Channel!", "sess.open_forward(remote_host, remote_port, source_host, local_port)", "Opens a direct TCP/IP port-forwarding channel (`ssh_channel_open_forward`).", "mut fwd := try sess.open_forward(\"127.0.0.1\", 5432)"},
                {"Session.sftp_write_text", "fn (mut sess: Session) sftp_write_text(remote_path: str, text: str, mode: u32 = 0644) -> void!", "sess.sftp_write_text(remote_path, text, mode)", "Uploads UTF-8 `text` to `remote_path` over SFTP.", "try sess.sftp_write_text(\"/remote/app.conf\", cfg_text)"},
                {"Session.sftp_write_bytes", "fn (mut sess: Session) sftp_write_bytes(remote_path: str, data: []byte, mode: u32 = 0644) -> void!", "sess.sftp_write_bytes(remote_path, data, mode)", "Uploads binary `data` to `remote_path` over SFTP.", "try sess.sftp_write_bytes(\"/remote/app.bin\", raw_bytes)"},
                {"Session.sftp_read_text", "fn (mut sess: Session) sftp_read_text(remote_path: str) -> str!", "sess.sftp_read_text(remote_path)", "Downloads the UTF-8 text contents of `remote_path` over SFTP.", "body := try sess.sftp_read_text(\"/remote/app.conf\")"},
                {"Session.sftp_read_bytes", "fn (mut sess: Session) sftp_read_bytes(remote_path: str) -> []byte!", "sess.sftp_read_bytes(remote_path)", "Downloads the raw binary bytes of `remote_path` over SFTP.", "raw := try sess.sftp_read_bytes(\"/remote/app.bin\")"},
                {"Session.sftp_list_dir", "fn (mut sess: Session) sftp_list_dir(remote_path: str) -> []SftpEntry!", "sess.sftp_list_dir(remote_path)", "Lists directory entries in `remote_path` over SFTP.", "entries := try sess.sftp_list_dir(\"/remote\")"},
                {"Session.sftp_mkdir", "fn (mut sess: Session) sftp_mkdir(remote_path: str, mode: u32 = 0755) -> void!", "sess.sftp_mkdir(remote_path, mode)", "Creates a remote directory over SFTP.", "try sess.sftp_mkdir(\"/remote/releases\")"},
                {"Session.sftp_remove", "fn (mut sess: Session) sftp_remove(remote_path: str) -> void!", "sess.sftp_remove(remote_path)", "Removes a remote file over SFTP.", "try sess.sftp_remove(\"/remote/old.tmp\")"},
                {"Session.close", "fn (mut sess: Session) close()", "sess.close()", "Disconnects and frees the SSH session.", "defer sess.close()"},
                {"Channel.read_text", "fn (mut ch: Channel) read_text(max_bytes: usize = 4096) -> str!", "ch.read_text(max_bytes)", "Non-blockingly reads up to `max_bytes` of UTF-8 text from the SSH channel (`\"\"` when no data is currently ready).", "chunk := try ch.read_text(4096)"},
                {"Channel.read_bytes", "fn (mut ch: Channel) read_bytes(max_bytes: usize = 4096) -> []byte!", "ch.read_bytes(max_bytes)", "Non-blockingly reads up to `max_bytes` of raw bytes from the SSH channel.", "raw := try ch.read_bytes(4096)"},
                {"Channel.write_text", "fn (mut ch: Channel) write_text(text: str) -> usize!", "ch.write_text(text)", "Writes UTF-8 `text` into the SSH channel (e.g. PTY stdin).", "_ = try ch.write_text(\"ls -la\\n\")"},
                {"Channel.write_bytes", "fn (mut ch: Channel) write_bytes(data: []byte) -> usize!", "ch.write_bytes(data)", "Writes binary `data` into the SSH channel.", "_ = try ch.write_bytes(raw)"},
                {"Channel.resize_pty", "fn (mut ch: Channel) resize_pty(cols: i32, rows: i32) -> void!", "ch.resize_pty(cols, rows)", "Dynamically resizes an open interactive PTY (`ssh_channel_change_pty_size`).", "try ch.resize_pty(140, 42)"},
                {"Channel.is_eof", "fn (ch: Channel) is_eof() -> bool", "ch.is_eof()", "Returns `true` if the remote channel has reached EOF.", "if ch.is_eof() { break }"},
                {"Channel.close", "fn (mut ch: Channel) close()", "ch.close()", "Sends EOF and closes the SSH channel.", "defer ch.close()"}
            }
        },
        {
            "ext:webui", "webui", "Hybrid Native WebView & Two-Way Typed RPC Application Framework", "(WKWebView / WebKitGTK / WebView2 + JSON-RPC 2.0)",
            "Wails/Tauri-style hybrid desktop and mobile application runtime hosting an OS-native WebView (`WKWebView` on macOS & iOS, `WebKitGTK` on Linux, `WebView2` on Windows) over a custom `app://localhost/` URI scheme with two-way typed struct RPC (`app.bind[T]`), live event streaming (`app.on`, `app.emit`), reactive key-value state (`app.set_state`), and an injected `window.backend.*` ES6 Proxy + `window.invoke` + `window.runtime.*` JavaScript bridge.",
            {
                {"App.make", "fn make(title: str = \"cfire WebUI\", width: i32 = 960, height: i32 = 640) -> App", "cfire::webui::new_app(title, width, height)", "`make(webui.App, title, width, height)` creates a native WebView application window.", "mut app := make(webui.App, \"Studio\", 1280, 800)"},
                {"App.bind", "fn (mut app: App) bind[T](name: str, handler: fn(T) -> any!)", "app.bind<T>(name, handler)", "Binds a typed `cfire` closure to `window.backend.<name>(params)` and `window.invoke(name, params)`, automatically deserializing incoming JSON into struct `T` and serializing the return value.", "app.bind[TrackMix](\"set_track\") |req| { return compute_mix(req) }"},
                {"App.bind_async", "fn (mut app: App) bind_async[T](name: str, handler: fn(T) -> any!)", "app.bind_async<T>(name, handler)", "Binds a typed RPC handler that executes asynchronously on a background worker thread without blocking the main UI/WebView thread.", "app.bind_async[ConnectReq](\"connect_ssh\") |req| { return dial(req) }"},
                {"App.bind_action", "fn (mut app: App) bind_action(name: str, handler: fn() -> any!)", "app.bind_action(name, handler)", "Binds a zero-argument action closure to `window.backend.<name>()`.", "app.bind_action(\"reset_desk\") || { return \"ok\" }"},
                {"App.bind_action_async", "fn (mut app: App) bind_action_async(name: str, handler: fn() -> any!)", "app.bind_action_async(name, handler)", "Binds a zero-argument RPC action that executes asynchronously on a background worker thread.", "app.bind_action_async(\"sync_now\") || { return run_sync() }"},
                {"App.bind_raw", "fn (mut app: App) bind_raw(name: str, handler: fn(str) -> str!)", "app.bind_raw(name, handler)", "Binds a raw JSON string RPC handler to `window.backend.<name>(params)`.", "app.bind_raw(\"raw_op\") |raw_json| { return raw_json }"},
                {"App.on", "fn (mut app: App) on(event: str, handler: fn(str))", "app.on(event, handler)", "Subscribes a backend callback to frontend events emitted via `window.runtime.EventsEmit(event, data)`.", "app.on(\"fader:input\") |payload| { println(\"{}\", payload) }"},
                {"App.emit", "fn (mut app: App) emit(event: str, payload: any)", "app.emit(event, payload)", "Serializes `payload` as JSON and dispatches it to all frontend `window.runtime.EventsOn(event, fn)` listeners.", "app.emit(\"meter:peak\", status)"},
                {"App.emit_bytes", "fn (mut app: App) emit_bytes(event: str, data: []byte)", "app.emit_bytes(event, data)", "Emits a binary `[]byte` payload to frontend `window.runtime.EventsOn(event, fn)` listeners as a Base64-decoded `Uint8Array`.", "app.emit_bytes(\"pty:data\", chunk)"},
                {"App.clear_history", "fn (mut app: App) clear_history()", "app.clear_history()", "Clears the bounded in-memory ring buffer of recorded emitted events.", "app.clear_history()"},
                {"App.set_max_history", "fn (mut app: App) set_max_history(max_events: usize)", "app.set_max_history(max_events)", "Sets the maximum number of emitted events retained per event name for testing inspection (`0` disables history retention).", "app.set_max_history(128)"},
                {"App.clipboard_set", "fn (app: App) clipboard_set(text: str, concealed: bool = false, clear_after_ms: i64 = 0)", "app.clipboard_set(text, concealed, clear_after_ms)", "Copies `text` to the native OS system clipboard (`NSPasteboard` on macOS, `GtkClipboard` on Linux, `Win32` on Windows), optionally marking it concealed (`org.nspasteboard.ConcealedType` / `ExcludeClipboardContentFromMonitorProcessing`) and auto-clearing after `clear_after_ms` if unchanged.", "app.clipboard_set(token, true, 15000)"},
                {"App.clipboard_set_secret", "fn (app: App) clipboard_set_secret(text: str, clear_after_ms: i64 = 30000)", "app.clipboard_set_secret(text, clear_after_ms)", "Copies a sensitive secret to the OS clipboard marked as concealed/transient and automatically clears it after `clear_after_ms` if the clipboard has not been overwritten.", "app.clipboard_set_secret(password, 30000)"},
                {"App.clipboard_get", "fn (app: App) clipboard_get() -> str", "app.clipboard_get()", "Reads current plain text from the native OS system clipboard.", "clip := app.clipboard_get()"},
                {"App.monospace_fonts", "fn (app: App) monospace_fonts() -> []str", "app.monospace_fonts()", "Enumerates installed system monospace font families (`NSFontManager` on macOS, `fc-list :spacing=mono` on Linux, `EnumFontFamiliesExW` on Windows).", "fonts := app.monospace_fonts()"},
                {"App.set_icon", "fn (mut app: App) set_icon(icon_path: str)", "app.set_icon(icon_path)", "Sets the application window and dock/taskbar icon from a `.png`, `.icns`, or `.ico` file.", "app.set_icon(\"assets/icon.png\")"},
                {"App.set_tray", "fn (mut app: App) set_tray(icon_path: str, tooltip: str = \"\")", "app.set_tray(icon_path, tooltip)", "Configures a system tray / menu-bar status item (`NSStatusItem` on macOS, `GtkStatusIcon` on Linux, `Shell_NotifyIconW` on Windows).", "app.set_tray(\"assets/tray.png\", \"Flint\")"},
                {"App.add_tray_item", "fn (mut app: App) add_tray_item(title: str, event: str)", "app.add_tray_item(title, event)", "Appends a menu item to the system tray context menu that emits `event` to `app.on(event)` when clicked (or `\"-\"` for a separator).", "app.add_tray_item(\"Quick Connect\", \"tray:connect\")"},
                {"App.hide_window", "fn (mut app: App) hide_window()", "app.hide_window()", "Hides the native application window without terminating the process or system tray.", "app.hide_window()"},
                {"App.show_window", "fn (mut app: App) show_window()", "app.show_window()", "Restores and focuses a hidden or minimized native application window.", "app.show_window()"},
                {"App.set_hide_on_close", "fn (mut app: App) set_hide_on_close(enabled: bool)", "app.set_hide_on_close(enabled)", "Intercepts the window close button (`windowShouldClose:` / `delete-event` / `WM_CLOSE`) to hide the window to the system tray instead of exiting.", "app.set_hide_on_close(true)"},
                {"App.add_menu_item", "fn (mut app: App) add_menu_item(menu: str, title: str, shortcut: str, event: str)", "app.add_menu_item(menu, title, shortcut, event)", "Adds a custom menu item with keyboard shortcut to the application menu bar that emits `event` to `app.on(event)` when triggered.", "app.add_menu_item(\"Shell\", \"New Tab\", \"t\", \"menu:new_tab\")"},
                {"App.set_state", "fn (mut app: App) set_state(key: str, val: str)", "app.set_state(key, val)", "Updates a reactive key-value state entry and broadcasts a `state:change` event (`{key, value}`) to the frontend.", "app.set_state(\"preset\", \"Warm Tape\")"},
                {"App.get_state", "fn (app: App) get_state(key: str) -> str", "app.get_state(key)", "Reads the current string value for `key` from the application's state store.", "p := app.get_state(\"preset\")"},
                {"App.set_html", "fn (mut app: App) set_html(html: str)", "app.set_html(html)", "Registers inline HTML content as `/index.html` on the `app://localhost/` virtual filesystem.", "app.set_html(\"<!doctype html><html><body><h1>Hello</h1></body></html>\")"},
                {"App.embed_asset", "fn (mut app: App) embed_asset(path: str, content: str, mime: str = \"\")", "app.embed_asset(path, content, mime)", "Embeds an in-memory asset (`.html`, `.css`, `.js`, `.svg`, etc.) into the `app://localhost/` virtual filesystem.", "app.embed_asset(\"/app.css\", \"body { background: #111; }\", \"text/css\")"},
                {"App.serve_dir", "fn (mut app: App) serve_dir(dir_path: str)", "app.serve_dir(dir_path)", "Mounts a local or bundled asset directory (e.g. `\"./ui\"`) under `app://localhost/`.", "app.serve_dir(\"./ui\")"},
                {"App.eval", "fn (mut app: App) eval(js: str)", "app.eval(js)", "Asynchronously evaluates arbitrary JavaScript inside the active WebView on the main UI thread.", "app.eval(\"document.body.classList.add('ready')\")"},
                {"App.set_title", "fn (mut app: App) set_title(title: str)", "app.set_title(title)", "Updates the native window title.", "app.set_title(\"Mastering Studio — Active\")"},
                {"App.set_size", "fn (mut app: App) set_size(width: i32, height: i32)", "app.set_size(width, height)", "Resizes the native application window.", "app.set_size(1280, 800)"},
                {"App.set_min_size", "fn (mut app: App) set_min_size(min_w: i32, min_h: i32)", "app.set_min_size(min_w, min_h)", "Sets the minimum resizable dimensions for the native window.", "app.set_min_size(640, 420)"},
                {"App.set_max_size", "fn (mut app: App) set_max_size(max_w: i32, max_h: i32)", "app.set_max_size(max_w, max_h)", "Sets the maximum resizable dimensions for the native window.", "app.set_max_size(1920, 1080)"},
                {"App.set_frameless", "fn (mut app: App) set_frameless(frameless: bool)", "app.set_frameless(frameless)", "Configures a frameless full-size content view window with CSS `--wails-draggable: drag` support.", "app.set_frameless(true)"},
                {"App.set_transparent", "fn (mut app: App) set_transparent(transparent: bool)", "app.set_transparent(transparent)", "Enables or disables transparent window and WebView background rendering.", "app.set_transparent(true)"},
                {"App.set_dark_mode", "fn (mut app: App) set_dark_mode(dark: bool)", "app.set_dark_mode(dark)", "Configures dark or light native window appearance.", "app.set_dark_mode(true)"},
                {"App.set_always_on_top", "fn (mut app: App) set_always_on_top(on_top: bool)", "app.set_always_on_top(on_top)", "Floats the application window above standard desktop windows.", "app.set_always_on_top(true)"},
                {"App.fullscreen", "fn (mut app: App) fullscreen()", "app.fullscreen()", "Toggles native fullscreen window mode.", "app.fullscreen()"},
                {"App.minimize", "fn (mut app: App) minimize()", "app.minimize()", "Minimizes the native window to the dock or taskbar.", "app.minimize()"},
                {"App.maximize", "fn (mut app: App) maximize()", "app.maximize()", "Zooms or maximizes the native window.", "app.maximize()"},
                {"App.center", "fn (mut app: App) center()", "app.center()", "Centers the native window on the active screen.", "app.center()"},
                {"App.open_file_dialog", "fn (app: App) open_file_dialog(title: str = \"Open File\") -> str", "app.open_file_dialog(title)", "Opens a native OS file picker dialog and returns the selected path (or `\"\"` if cancelled).", "path := app.open_file_dialog(\"Select Audio Track\")"},
                {"App.save_file_dialog", "fn (app: App) save_file_dialog(title: str = \"Save File\", default_name: str = \"\") -> str", "app.save_file_dialog(title, default_name)", "Opens a native OS save-file dialog and returns the chosen destination path.", "dest := app.save_file_dialog(\"Export Preset\", \"studio.json\")"},
                {"App.message_dialog", "fn (app: App) message_dialog(title: str, message: str)", "app.message_dialog(title, message)", "Displays a modal native OS alert dialog.", "app.message_dialog(\"Export Complete\", \"Saved master preset.\")"},
                {"App.call_rpc", "fn (mut app: App) call_rpc(method: str, params_json: str = \"{}\") -> str!", "app.call_rpc(method, params_json)", "Directly invokes a bound RPC handler in-memory without opening a window (ideal for `cfire test` and `fuzz` blocks).", "res := try app.call_rpc(\"set_track\", \"{\\\"channel\\\":1}\")"},
                {"App.dispatch_event", "fn (mut app: App) dispatch_event(event: str, data_json: str = \"{}\")", "app.dispatch_event(event, data_json)", "Dispatches a synthetic frontend event to `app.on(event)` listeners during headless testing.", "app.dispatch_event(\"fader:input\", \"{\\\"channel\\\":1}\")"},
                {"App.resolve_asset_mime", "fn (app: App) resolve_asset_mime(path: str) -> str", "app.resolve_asset_mime(path)", "Returns the MIME type the embedded asset server reports for `path` (useful in headless contract tests).", "mime := app.resolve_asset_mime(\"/index.html\")"},
                {"App.emitted_events_count", "fn (app: App) emitted_events_count(event: str) -> i64", "app.emitted_events_count(event)", "Returns the number of times `app.emit(event, ...)` has been triggered.", "n := app.emitted_events_count(\"meter:peak\")"},
                {"App.last_emitted_payload", "fn (app: App) last_emitted_payload(event: str) -> str", "app.last_emitted_payload(event)", "Returns the most recent JSON payload emitted for `event`.", "last := app.last_emitted_payload(\"meter:peak\")"},
                {"App.run_webview_test", "fn (mut app: App) run_webview_test(timeout_ms: i32 = 3000) -> bool!", "app.run_webview_test(timeout_ms)", "Boots an offscreen native `WKWebView`, executes the loaded page's JavaScript and RPC calls until `window.runtime.Quit()` or `timeout_ms`, and returns `true` if `Quit()` was reached.", "ok := try app.run_webview_test(3000)"},
                {"App.run", "fn (mut app: App) run()", "app.run()", "Launches the native OS window and WebView event loop (`NSWindow`+`WKWebView` on macOS, `UIWindow`+`WKWebView` on iOS, `GtkWindow`+`WebKitGTK` on Linux).", "app.run()"},
                {"App.quit", "fn (mut app: App) quit()", "app.quit()", "Requests termination of the native WebView event loop.", "app.quit()"},
                {"App.close", "fn (mut app: App) close()", "app.close()", "Deterministic RAII cleanup handle for `defer app.close()`.", "defer app.close()"},
                {"clipboard_get", "fn clipboard_get() -> str", "cfire::webui::clipboard_get()", "Reads plaintext from the system clipboard.", "t := webui.clipboard_get()"},
                {"clipboard_set", "fn clipboard_set(text: str, concealed: bool = false, clear_after_ms: i64 = 0)", "cfire::webui::clipboard_set(text, concealed, clear_after_ms)", "Writes plaintext to the system clipboard with optional auto-clear timeout.", "webui.clipboard_set(\"secret\", true, 10000)"},
                {"clipboard_set_secret", "fn clipboard_set_secret(text: str, clear_after_ms: i64 = 30000)", "cfire::webui::clipboard_set_secret(text, clear_after_ms)", "Writes sensitive secret to the clipboard and schedules automatic clearing.", "webui.clipboard_set_secret(\"pass\")"},
                {"monospace_fonts", "fn monospace_fonts() -> []str", "cfire::webui::monospace_fonts()", "Returns a list of platform-installed monospaced font family names.", "fonts := webui.monospace_fonts()"}
            }
        }
    };
        for (auto& m : cat) {
            apply_module_metadata(m);
            m.type_docs = detail::module_type_docs(m.import_path);
            // Structs that `.cf` code constructs (CF-180) document their fields once, in the sema table.
            for (auto& td : m.type_docs) {
                const auto* st = cfire::frontend::find_stdlib_struct(m.ns_alias, td.bare_name());
                if (st == nullptr || !td.summary.empty()) continue;
                td.summary = std::string(st->summary);
                for (const auto& f : cfire::frontend::stdlib_struct_fields(*st)) {
                    td.fields.push_back({std::string(f.name), std::string(f.type), f.def.empty() ? std::string() : std::format("Default `{}`.", f.def)});
                }
            }
        }
        return cat;
    }();
    return catalog;
}

// CF-158: one line for every diagnostic code the compiler can emit. Codes with a detailed entry in
// diagnostics_catalog() take their summary from there; the unit test `test_cf158_doc_integrity` fails when the
// sources emit a code that is in neither list. The GUIDE's diagnostics table is generated from this.
struct DiagnosticCodeDoc {
    std::string code;
    std::string category;
    std::string summary;
};

[[nodiscard]] inline auto diagnostics_catalog() -> const std::vector<DiagnosticDoc>&;

[[nodiscard]] inline auto diagnostic_code_registry() -> const std::vector<DiagnosticCodeDoc>& {
    static const std::vector<DiagnosticCodeDoc> all = [] {
        std::vector<DiagnosticCodeDoc> v = {
            {"cfire/AMBIGUOUS-INTERPOLATION", "Syntax", "An interpolation `{...}` that starts with a number literal must be parenthesized."},
            {"cfire/ARITY-MISMATCH", "Types", "A call passes the wrong number of arguments."},
            {"cfire/ASSIGN-IMMUTABLE", "Mutability", "Assignment to a binding declared without `mut`."},
            {"cfire/DANGLING-TEMPORARY", "Memory safety", "A view or pointer borrows from a temporary that is destroyed at the end of the statement."},
            {"cfire/DEPRECATED-API", "Migration", "A stdlib function that was renamed or replaced before 0.16 (`fs.read_all`, `os.read_stdin`, ...)."},
            {"cfire/DEPRECATED-DISCARD", "Migration", "`_ := expr` is not a discard form; write `_ = expr`."},
            {"cfire/DEPRECATED-IN-PARAM", "Migration", "Explicit `in` intent; parameters are read-only by default."},
            {"cfire/DEPRECATED-INTENT-POS", "Migration", "`x: mut T` intent placement; write `mut x: T`."},
            {"cfire/DEPRECATED-SEPARATOR", "Syntax", "`::` used where `.` is required (only enum variants use `::`)."},
            {"cfire/DUPLICATE-DECLARATION", "Declarations", "Two declarations of the same type, function or method."},
            {"cfire/DYNAMIC-INTERFACE-UNSUPPORTED", "Types", "A slice or array of interface values; interfaces lower to C++ concepts."},
            {"cfire/ERROR", "Tooling", "An internal error with no more specific code."},
            {"cfire/ESCAPING-CAPTURE-MUT", "Concurrency", "An escaping closure mutates a captured outer variable."},
            {"cfire/FILE-NOT-FOUND", "Tooling", "A file named on the command line or imported does not exist."},
            {"cfire/INVALID-IMPORT", "Imports", "A module imported under the wrong tier prefix (`core:` vs `ext:`)."},
            {"cfire/INVALID-INDEX", "Types", "Indexing a value that is not a string, slice, array or map."},
            {"cfire/INVALID-INTERPOLATION", "Syntax", "An interpolation `{...}` that is not a valid expression."},
            {"cfire/INVALID-MEMBER", "Types", "A member access on a primitive value that has no such member."},
            {"cfire/INVALID-SMART-PTR-CTOR", "Construction", "`own(v)` / `shared(v)` called as functions; write `make(own[T], v)`."},
            {"cfire/LITERAL-OUT-OF-RANGE", "Numbers", "An integer literal that does not fit its target type (Q9)."},
            {"cfire/MISSING-IMPORT", "Imports", "A stdlib module is used without being imported."},
            {"cfire/MISSING-RETURN", "Control flow", "A function with a return type can fall off its end."},
            {"cfire/NON-BOOL-CONDITION", "Types", "A condition (`if`, `for`, `&&`, `||`, `!`) that is not `bool`."},
            {"cfire/OUT-CALLSITE", "Migration", "A call-site `out` marker (retired with the `out` intent)."},
            {"cfire/PREFER-ENUM", "Style", "A cluster of related integer constants that should be an enum (warning)."},
            {"cfire/RAW-POINTER", "Ownership", "Raw pointer syntax (`*T`, `&x`, `*p`, `.data()`) outside C++ interop (CF-189)."},
            {"cfire/REFERENCE-TYPE", "Ownership", "A `&T` / `&mut T` type; parameters are views by default and `mut` by intent (Q53)."},
            {"cfire/REDUNDANT-DEREF", "Style", "`(*p).field` on a smart pointer; write `p.field`."},
            {"cfire/SEMA-ERROR", "Tooling", "A semantic error with no more specific code."},
            {"cfire/SCOPE-RESULT", "Concurrency", "A value-producing `async.scope` whose spawned tasks return no value or different types (Q51)."},
            {"cfire/SHIFT-OUT-OF-RANGE", "Numbers", "A constant shift count outside the operand's bit width (Q11)."},
            {"cfire/SLOT-CAPTURE", "Concurrency", "A slot capture `|mut xs[i]|` used outside `scope.spawn`, or a task that uses its slice other than `xs[i]`, or a slice resized while tasks write it (Q51)."},
            {"cfire/SPAWN-SHARED-MUT", "Concurrency", "A spawned task writes through a pointer other tasks can reach (Q20)."},
            {"cfire/SYNTAX", "Syntax", "The source does not parse."},
            {"cfire/TYPE-ERROR", "Tooling", "The C++ backend rejected generated code; Sema should have caught it (please report)."},
            {"cfire/TYPE-MISMATCH", "Types", "A value of one type is used where another is required."},
            {"cfire/UNDECLARED-IDENT", "Names", "A name that is not declared, or a module used under the wrong alias."},
            {"cfire/UNHANDLED-RESULT", "Errors", "A fallible `T!` value used without `try` or `or`."},
            {"cfire/UNKNOWN-ENUM-VARIANT", "Names", "A variant that the enum does not declare."},
            {"cfire/UNKNOWN-FIELD", "Names", "A field or associated item that the type does not have."},
            {"cfire/UNKNOWN-METHOD", "Names", "A method that the receiver type does not have (Q14 closed builtin surface)."},
            {"cfire/UNKNOWN-MODULE", "Imports", "An import path that names no known module."},
            {"cfire/UNKNOWN-STDLIB-FN", "Names", "A function that the stdlib module does not export."},
            {"cfire/UNKNOWN-STDLIB-TYPE", "Names", "A type that the stdlib module does not export, or a `build.*` type outside `build.cf` (CF-188)."},
            {"cfire/UNKNOWN-TYPE", "Types", "An expression whose type cannot be inferred (warning)."},
            {"cfire/UNRESOLVED-PACKAGE", "Imports", "A local package import that cannot be found."},
            {"cfire/UNSATISFIED-CONSTRAINT", "Generics", "A type argument that does not satisfy the interface constraint."},
            {"cfire/UNWRAPPED-OPTIONAL", "Errors", "A member access on a `T?` value without unwrapping."},
            {"cfire/WRONG-ARG-COUNT", "Types", "A builtin called with the wrong number of arguments."},
        };
        for (const auto& d : diagnostics_catalog()) {
            if (std::none_of(v.begin(), v.end(), [&](const DiagnosticCodeDoc& r) { return r.code == d.code; })) {
                v.push_back({d.code, "Detailed rule", d.title});
            }
        }
        std::sort(v.begin(), v.end(), [](const DiagnosticCodeDoc& a, const DiagnosticCodeDoc& b) { return a.code < b.code; });
        return v;
    }();
    return all;
}

[[nodiscard]] inline auto diagnostics_catalog() -> const std::vector<DiagnosticDoc>& {
    static const std::vector<DiagnosticDoc> diags = {
        {
            "cfire/RETIRED-API",
            "API Retired in the 0.16 Uniform-API Migration",
            "The 0.16 release (Q16) replaced free functions, sentinels and duplicate spellings with one form each: methods on values (`s.trim()`, `xs.sort()`, `x.abs()`), type-associated items (`i32.parse(s)`, `i32.MAX`, `math.PI`), `.len()`, `.is_empty()`, `make(T, ...)` construction, `connect` / `listen` verbs and `verify_peer: false`. Every retired form is a hard error with a fix-it; `cfire fix --only cfire/RETIRED-API` applies exactly these rewrites.",
            "import \"core:str\" // ERROR [cfire/RETIRED-API]\nfn main() {\n    t := str.trim(\" x \") // ERROR [cfire/RETIRED-API]\n    n := t.len // ERROR [cfire/RETIRED-API]\n}",
            "fn main() {\n    t := \" x \".trim() // OK\n    n := t.len() // OK\n}"
        },
        {
            "cfire/INVALID-MAKE",
            "Invalid `make(T, ...)` Arguments",
            "`make` takes the type first (Q25). Slices take `len` and optional `cap` integers, maps and arrays take nothing, and a struct takes either nothing (default fields) or the arguments of its declared constructor `fn (T) make(...) -> T`.",
            "type Q struct { a: i32 }\nfn main() {\n    v := make([]i32, 1, 2, 3) // ERROR [cfire/INVALID-MAKE]\n    q := make(Q, 1) // ERROR [cfire/INVALID-MAKE]\n}",
            "type Q struct { a: i32 }\nfn (Q) make(a: i32) -> Q { return Q{a: a} }\nfn main() {\n    v := make([]i32, 1, 2) // OK\n    q := make(Q, 1) // OK\n}"
        },
        {
            "cfire/INVALID-CONSTRUCTOR",
            "Associated Function Other Than `make`",
            "A receiver type with no binding (`fn (T) ...`) declares the type's constructor, and the only name it may have is `make` (Q25). Methods need a receiver binding.",
            "type P struct { x: i32 }\nfn (P) origin() -> P { return P{x: 0} } // ERROR [cfire/INVALID-CONSTRUCTOR]",
            "type P struct { x: i32 }\nfn (P) make(x: i32) -> P { return P{x: x} } // OK\nfn (p: P) shifted() -> P { return P{x: p.x + 1} } // OK"
        },
        {
            "cfire/INVALID-RELEASE",
            "`release` on a Shared Pointer",
            "`o.release() -> *mut T` hands ownership to C (CF-181) and exists only on `own[T]`: a `shared[T]` may have other owners.",
            "type B struct { v: i32 }\nfn main() {\n    s := make(shared[B], B{v: 1})\n    p := s.release() // ERROR [cfire/INVALID-RELEASE]\n}",
            "type B struct { v: i32 }\nfn main() {\n    o := make(own[B], B{v: 1})\n    p := o.release() // OK; adopt it back with own[B].from_raw(p)\n}"
        },
        {
            "cfire/UNKNOWN-ARG-LABEL",
            "Unknown Labeled Argument",
            "A labeled argument (`verify_peer: false`) must name a parameter of the called function; labels place arguments by name and skipped parameters keep their defaults.",
            "import \"core:net/http\"\nfn main() -> void! {\n    r := try http.get(\"https://x\", verify: false) // ERROR [cfire/UNKNOWN-ARG-LABEL]\n}",
            "import \"core:net/http\"\nfn main() -> void! {\n    r := try http.get(\"https://x\", verify_peer: false) // OK\n}"
        },
        {
            "cfire/FIELD-NAME-CASE",
            "Struct Field Is Not snake_case",
            "Struct fields are snake_case like functions and locals (Q28); capitalization carries no meaning. This is a warning; `cfire fix` renames the field and every use across the workspace. Untagged JSON/CLI keys follow the field name, so add a `json:\"...\"` tag to keep an existing wire name.",
            "type Doc struct {\n    TopK: i32 // WARNING [cfire/FIELD-NAME-CASE]\n}",
            "type Doc struct {\n    top_k: i32 `json:\"TopK\"` // OK\n}"
        },
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
            "A variable passed as a `mut` argument (or used as a `mut` method receiver) cannot also be passed as another argument in the same call.",
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
            "When initializing or assigning a variable with `val := fallible() or err { ... }`, the error recovery block must definitely diverge with `return`, `panic(...)`, or `os.exit(...)` to avoid unwrapping an uninitialized value.",
            "fn compute() -> i32 {\n    // ERROR [cfire/NON-DIVERGING-OR-BLOCK]: handler does not diverge\n    x := parse_num() or err {\n        println(\"error: {}\", err.message())\n    }\n    return x\n}",
            "fn compute() -> i32 {\n    x := parse_num() or err {\n        println(\"error: {}\", err.message())\n        return 0\n    }\n    return x\n}"
        },
        {
            "cfire/UNREACHABLE-MATCH-ARM",
            "Unreachable `match` Arm Shadowed by Earlier Pattern",
            "Every arm in a `match` statement or expression must be reachable. Arms appearing after a wildcard `_`, duplicate enum/literal patterns, or slice patterns already subsumed by a broader prefix/suffix pattern (e.g., `[first, ..mid, last]` after `[head, ..tail]`) are rejected at compile time.",
            "match items {\n    [head, ..tail] => { return head }\n    [first, ..mid, last] => { return last } // ERROR [cfire/UNREACHABLE-MATCH-ARM]\n    _ => { return \"\" }\n}",
            "match items {\n    [] => { return \"empty\" }\n    [only] => { return only }\n    [first, ..mid, last] => { return first + last } // OK: size >= 2 is reachable\n}"
        },
        {
            "cfire/MOVE-CALLSITE",
            "Missing Call-Site `move` Marker",
            "Functions declaring `move param: T` take ownership of the caller's argument (`std::move(arg)`). Callers must write `move arg` at the call site so ownership transfer is explicit.",
            "fn consume(move buf: []byte) { ... }\nfn main() {\n    data := []byte(\"payload\")\n    consume(data) // ERROR [cfire/MOVE-CALLSITE]\n}",
            "fn consume(move buf: []byte) { ... }\nfn main() {\n    data := []byte(\"payload\")\n    consume(move data) // OK\n}"
        },
        {
            "cfire/OPTIONAL-OR-ERR",
            "Binding Error Variable `or err` on Optional `T?`",
            "Optional values (`T?`, lowered to `std::optional<T>`) represent absence (`none`), not an error (`cfire::Error`). Use `or fallback` or a parameterless `or { ... }` block on `T?`, and reserve `or err { ... }` for fallible `T!` (`std::expected<T, cfire::Error>`) results.",
            "opt := \"42\".parse_i64() or err {\n    return 0 // ERROR [cfire/OPTIONAL-OR-ERR]\n}",
            "opt := \"42\".parse_i64() or {\n    return 0 // OK\n}"
        },
        {
            "cfire/USE-AFTER-FREE",
            "Use of a Slice View After Owner Invalidation",
            "Slice views (`s[a..b]`, `xs[a..b]`) track their root owner variable. If the owner is moved (`move x`), reassigned (`x = ...`), or structurally mutated by a reallocating container method (`.push()`, `.pop()`, `.insert()`, `.remove()`, `.clear()`), every view taken before is invalidated, and reading it is rejected.",
            "mut nums: []i32 = [10, 20, 30]\nview := nums[0..2]\nnums.push(40) // may reallocate 'nums'\nprintln(\"{}\", view[0]) // ERROR [cfire/USE-AFTER-FREE]",
            "mut nums: []i32 = [10, 20, 30]\nnums.push(40)\nview := nums[0..2] // view taken after the mutation\nprintln(\"{}\", view[0]) // OK"
        },
        {
            "cfire/DANGLING-POINTER",
            "Inner-Scope Address Escaping to an Outer Pointer (retired with raw pointers, CF-189)",
            "Raw pointers are not cfire syntax any more (CF-189), so no pointer can outlive its target; the check remains for code that still reaches it. Keep an index, the value itself, or `own[T]` instead.",
            "mut p := &x // ERROR [cfire/RAW-POINTER]: raw pointers exist only in C++ interop",
            "mut values: []i32 = [1, 2]\nmut at := 0 // an index instead of a pointer\nvalues[at] = 42 // OK"
        },
        {
            "cfire/DANGLING-RETURN",
            "Returning a Pointer to Function-Local Storage (retired with raw pointers, CF-189)",
            "Functions return values (`T`, `str`, `[]T`) or `own[T]`; a returned `str` or `[]T` is a copy of any view, so nothing returned can borrow a local (Q36, CF-189).",
            "fn bad() -> *i32 { // ERROR [cfire/RAW-POINTER]\n    local := 99\n    return &local\n}",
            "fn good() -> i32 {\n    local := 99\n    return local // OK: returned by value\n}"
        },
        {
            "cfire/ITERATOR-INVALIDATION",
            "Structural Mutation or Move of Collection During `for` Iteration",
            "While iterating over a collection in `for x in col` or `for i, x in col`, structurally mutating (`col.push(...)`, `col.clear()`), reassigning (`col = ...`), or moving (`move col`) the collection is rejected at compile time.",
            "mut items := [1, 2, 3]\nfor x in items {\n    items.push(x * 2) // ERROR [cfire/ITERATOR-INVALIDATION]\n}",
            "items := [1, 2, 3]\nmut doubled: []i32 = []\nfor x in items {\n    doubled.push(x * 2) // OK: mutating separate collection\n}"
        },
        {
            "cfire/NULL-DEREF",
            "Member Access on an Optional Variable That May Be `none`",
            "A variable of type `T?` (`own[T]?`, `Session?`) is tracked across branches. Reading a field or calling a method on it is rejected until the code proves it holds a value: `if s != none { ... }`, an early `return` when it is `none`, `assert(s != none)`, or `if v := s { ... }`. (cfire values are otherwise never null: there are no raw pointers, CF-189.)",
            "type Session struct { id: i32 }\nfn find(id: i32) -> Session? {\n    return none\n}\ns := find(1)\nprintln(\"{}\", s.id) // ERROR [cfire/NULL-DEREF]",
            "type Session struct { id: i32 }\nfn find(id: i32) -> Session? {\n    return none\n}\ns := find(1)\nif s != none {\n    println(\"{}\", s.id) // OK: narrowed to present\n}"
        },
        {
            "cfire/INDEX-OUT-OF-BOUNDS",
            "Compile-Time Out-of-Bounds or Negative Constant Sequence Index",
            "Negative integer literal indices (`arr[-1]`) on sequences and non-negative constant indices `>= N` on fixed-size arrays (`[N]T`) or array literals are rejected at compile time. Dynamic indices are verified at runtime via `cfire::index_at`.",
            "arr: [3]i32 = [10, 20, 30]\nprintln(\"{}\", arr[3]) // ERROR [cfire/INDEX-OUT-OF-BOUNDS]",
            "arr: [3]i32 = [10, 20, 30]\nprintln(\"{}\", arr[2]) // OK"
        },
        {
            "cfire/DIVIDE-BY-ZERO",
            "Compile-Time Division or Modulo by Constant Zero",
            "Dividing or taking modulo by a constant zero literal (`x / 0`, `x % 0`, `x /= 0`, `x %= 0`) is rejected at compile time.",
            "x := 100 / 0 // ERROR [cfire/DIVIDE-BY-ZERO]",
            "denom := 4\nx := 100 / denom // OK"
        },
        {
            "cfire/SQL-INJECTION-RISK",
            "Dynamic String Concatenation or Interpolation in SQL Query",
            "Passing a dynamically concatenated or interpolated string (`\"SELECT ... \" + input`) to `db.exec` or `db.query` risks SQL injection. Pass a static SQL string with `?1`, `?2` parameter placeholders instead.",
            "try db.exec(\"DELETE FROM users WHERE name = '\" + user_name + \"'\") // WARNING [cfire/SQL-INJECTION-RISK]",
            "try db.exec(\"DELETE FROM users WHERE name = ?1\", user_name) // OK: parameterized binding"
        },
        {
            "cfire/SHELL-INJECTION-RISK",
            "Dynamic String Concatenation or Interpolation in Shell Command",
            "Passing a dynamically concatenated or interpolated string (`\"ls \" + user_arg`) to `os.exec` or `os.exec_output` invokes `/bin/sh -c` and risks command injection. Use `os.command(binary, args)` to spawn subprocesses without shell interpretation.",
            "out := try os.exec(\"cat \" + untrusted_path) // WARNING [cfire/SHELL-INJECTION-RISK]",
            "res := try os.command(\"cat\", [untrusted_path]) // OK: direct posix_spawnp argv"
        },
        {
            "cfire/RETURN-IN-DEFER",
            "`return` Statement Inside `defer { ... }` Block",
            "`defer { ... }` blocks lower to a C++ RAII scope-exit destructor (`struct _CfireDefer { ~_CfireDefer() { ... } }`) and cannot return a value to the enclosing function.",
            "fn process() -> i32 {\n    defer {\n        return 0 // ERROR [cfire/RETURN-IN-DEFER]\n    }\n    return 1\n}",
            "fn process() -> i32 {\n    defer {\n        cleanup()\n    }\n    return 1 // OK: return from enclosing function\n}"
        },
        {
            "cfire/INVALID-STRUCT-TAG",
            "Duplicate or Malformed Struct Reflection Tag",
            "Struct reflection tags (`json:\"...\"`, `sql:\"...\"`, `db:\"...\"`, `cli:\"...\"`, `default:\"...\"`) are validated at compile time. Duplicate `json`, `sql`/`db`, or `cli` flag names across fields of the same struct, or `default:\"...\"` literals that cannot parse into the field's numeric or boolean type, are rejected at compile time.",
            "type Config struct {\n    Port: i64 `json:\"port\" default:\"not_a_number\"`\n    Alt:  i64 `json:\"port\"` // ERROR [cfire/INVALID-STRUCT-TAG]\n}",
            "type Config struct {\n    Port: i64 `json:\"port\" default:\"8080\"`\n    Alt:  i64 `json:\"alt_port\"` // OK\n}"
        },
        {
            "cfire/REDUNDANT-TRY",
            "Redundant `try` on Non-Fallible Expression",
            "`try` propagates errors from fallible `T!` (`std::expected<T, cfire::Error>`) expressions. Applying `try` to a plain value, literal, or non-fallible function call has no effect and is rejected at compile time.",
            "fn add(a: i32, b: i32) -> i32 { return a + b }\nfn main() {\n    x := try add(1, 2) // ERROR [cfire/REDUNDANT-TRY]\n}",
            "fn add(a: i32, b: i32) -> i32 { return a + b }\nfn main() {\n    x := add(1, 2) // OK\n}"
        },
        {
            "cfire/TRY-ON-OPTIONAL",
            "Using `try` on Optional `T?` Instead of `or`",
            "`try` only propagates fallible `T!` errors. Optional `T?` values represent absence (`none`) without an error payload and must be unwrapped using `or fallback` or `or { ... }`.",
            "fn main() {\n    n := try \"42\".parse_i64() // ERROR [cfire/TRY-ON-OPTIONAL]\n}",
            "fn main() {\n    n := \"42\".parse_i64() or 0 // OK\n}"
        },
        {
            "cfire/TRY-IN-NON-FALLIBLE-FN",
            "`try` Inside Non-Fallible Function",
            "Functions that use `try` (other than `main` and `test`/`fuzz`/`bench` blocks) must declare a fallible `T!` or `void!` return type so propagated errors can be returned to the caller.",
            "fn load() -> str {\n    return try fs.read_text(\"config.json\") // ERROR [cfire/TRY-IN-NON-FALLIBLE-FN]\n}",
            "fn load() -> str! {\n    return try fs.read_text(\"config.json\") // OK\n}"
        },
        {
            "cfire/TRY-IN-DEFER",
            "`try` Inside `defer` Block",
            "`defer` blocks run inside an RAII scope-exit destructor and cannot propagate errors out of the enclosing function with `try`. Handle errors explicitly with `or` inside `defer`.",
            "fn write_log(mut f: fs.File) -> void! {\n    defer {\n        try f.flush() // ERROR [cfire/TRY-IN-DEFER]\n    }\n}",
            "fn write_log(mut f: fs.File) -> void! {\n    defer {\n        f.flush() or err { eprintln(\"flush failed: {}\", err.message()) } // OK\n    }\n}"
        },
        {
            "cfire/INVALID-TRY-CONTEXT",
            "`try` Inside Short-Circuit Operand or `for` Condition",
            "Hoisting `try` before a statement would eagerly evaluate the right-hand side of `&&` / `||` or evaluate a `for <cond> { ... }` loop condition only once before the loop. Hoist the `try` into a separate statement or use `or fallback`.",
            "if ready && (try check_remote()) { // ERROR [cfire/INVALID-TRY-CONTEXT]\n}",
            "if ready {\n    if try check_remote() { ... } // OK\n}"
        },
        {
            "cfire/REDUNDANT-OR",
            "Redundant `or` on Non-Fallible, Non-Optional Expression",
            "`or` provides fallback values or diverging recovery blocks for fallible `T!` and optional `T?` expressions. Using `or` on a plain value or non-fallible call has no effect and is rejected at compile time.",
            "x := (1 + 2) or 0 // ERROR [cfire/REDUNDANT-OR]",
            "x := 1 + 2 // OK"
        },
        {
            "cfire/INVALID-OR-BLOCK-CONTEXT",
            "Block `or { ... }` Inside Short-Circuit Operand or `for` Condition",
            "Block-form `or { ... }` cannot be hoisted out of the right-hand side of `&&` / `||` or a `for <cond> { ... }` condition without breaking short-circuit or per-iteration evaluation. Use value fallback `or fallback` or hoist into a statement.",
            "if ok && (parse_flag() or { return }) { // ERROR [cfire/INVALID-OR-BLOCK-CONTEXT]\n}",
            "if ok && (parse_flag() or false) { // OK\n}"
        },
        {
            "cfire/REDUNDANT-CALLSITE-INTENT",
            "Redundant `mut` Annotation on a Read-Only Parameter",
            "Call-site `mut` annotations are only valid when the target parameter is declared `mut`. Writing `mut` on a read-only parameter misleads readers into thinking the call mutates the argument.",
            "fn show(x: i32) { println(\"{}\", x) }\nfn main() {\n    mut a := 10\n    show(mut a) // ERROR [cfire/REDUNDANT-CALLSITE-INTENT]\n}",
            "fn show(x: i32) { println(\"{}\", x) }\nfn main() {\n    mut a := 10\n    show(a) // OK\n}"
        },
        {
            "cfire/INVALID-MOVE",
            "No-Op `move` on Rvalue or Scalar `Copy` Variable",
            "`move` transfers ownership of an owned variable (`str`, `[]T`, `map[K]V`, `own[T]`, structs/enums). Applying `move` to a temporary rvalue/literal (`move 42`, `move foo()`) or to a scalar `Copy` primitive (`i32`, `f64`, `bool`) when not required by a `move` parameter has no effect and is rejected.",
            "mut count := 10\ny := move count // ERROR [cfire/INVALID-MOVE]",
            "mut buf := \"hello\"\ny := move buf // OK: transfers ownership of non-Copy 'str'"
        },
        {
            "cfire/BREAK-OUTSIDE-LOOP",
            "`break` or `continue` Outside of a `for` Loop",
            "`break` and `continue` are only valid inside the body of a `for` loop (and cannot cross closure or `defer` boundaries).",
            "fn helper() {\n    break // ERROR [cfire/BREAK-OUTSIDE-LOOP]\n}",
            "fn helper() {\n    for i in 0..10 {\n        if i == 5 { break } // OK\n    }\n}"
        },
        {
            "cfire/NO-EFFECT-STMT",
            "Pure Expression Statement With No Side Effects",
            "Standalone expression statements must perform an effect (such as a function/method call, `try`, `or { ... }`, `match`, `break`, or `continue`). Pure expressions like `x == 1` or `a + b` as statements discard their result and are rejected.",
            "mut x := 0\nx == 1 // ERROR [cfire/NO-EFFECT-STMT]: did you mean '=' assignment?",
            "mut x := 0\nx = 1 // OK"
        }
    };
    return diags;
}

} // namespace cfire::doc

#pragma once
// src/docgen/catalog_type_docs.hpp — What each stdlib type is, in cfire terms (CF-188): a summary, and the fields of
// value types or the variants of enums. Methods are not repeated here; hover and the reference list them from the
// module's `Type.method` catalog entries. `doc --check` fails when a type in a module's `types` list has no entry.

#include <string>
#include <string_view>
#include <vector>

namespace cfire::doc {

struct StdFieldDoc {
    std::string name;
    std::string type;
    std::string summary;
};

struct StdTypeDoc {
    std::string name;     // as written after the module qualifier, with type parameters: `Channel[T]`
    std::string kind;     // "struct" (a value with fields), "enum", "handle" (use it through its methods), "manifest"
    std::string summary;
    std::vector<StdFieldDoc> fields{};
    std::vector<std::string> variants{};

    // The bare name (`Channel` for `Channel[T]`), which is what the `types` list and code use.
    [[nodiscard]] auto bare_name() const -> std::string_view {
        std::string_view n = name;
        return n.substr(0, n.find('['));
    }
};

namespace detail {

// clang-format off
[[nodiscard]] inline auto module_type_docs(std::string_view mod) -> std::vector<StdTypeDoc> {
    if (mod == "core:audio") return {
        {"Waveform", "enum", "Oscillator shape for `audio.gen_tone` and `audio.gen_chirp`.", {}, {"Sine", "Square", "Saw", "Triangle", "Noise"}},
        {"SfxPreset", "enum", "A procedural sound effect for `audio.gen_sfx`.", {}, {"Tap", "Coin", "Laser", "Explosion", "PowerUp", "Jump", "Alert"}},
        {"Wave", "struct", "An in-memory waveform: interleaved `f32` samples in `-1.0`..`1.0`.", {
            {"samples", "[]f32", "Interleaved samples (`channels` values per frame)."},
            {"sample_rate", "i32", "Frames per second (default `48000`)."},
            {"channels", "i32", "Channels per frame (default `2`)."}}},
        {"Sound", "handle", "A wave registered with the mixer, playable many times at once (polyphonic)."},
        {"Stream", "handle", "A real-time output stream you push samples into, for custom synthesis."},
    };
    if (mod == "core:build") return {
        {"CC", "manifest", "In `build.cf`: compiles C or C++ sources into the project (`build.CC({ Sources: [...] })`)."},
        {"CMake", "manifest", "In `build.cf`: an external CMake project built as a dependency."},
        {"Make", "manifest", "In `build.cf`: an external Make project built as a dependency."},
        {"Git", "manifest", "In `build.cf`: a dependency fetched from a Git repository at a pinned revision."},
        {"Archive", "manifest", "In `build.cf`: a dependency fetched as a checksummed archive."},
        {"Command", "manifest", "In `build.cf`: a command run as part of the build."},
        {"Executable", "manifest", "In `build.cf`: declares an executable target (`build.Executable(name, { Entry: ... })`)."},
        {"Library", "manifest", "In `build.cf`: declares a static or shared library target."},
        {"Step", "manifest", "In `build.cf`: a code-generation step run when its inputs are newer than its outputs."},
        {"PkgConfig", "manifest", "In `build.cf`: links a system library found through pkg-config."},
        {"Link", "manifest", "In `build.cf`: links a library by name."},
        {"Include", "manifest", "In `build.cf`: adds a header search directory."},
        {"Tako", "manifest", "In `build.cf`: enables the Tako deployment integration."},
        {"Target", "manifest", "In `build.cf`: what `build.Executable` and `build.Library` return."},
        {"StepTarget", "manifest", "In `build.cf`: what `build.Step` returns."},
    };
    if (mod == "core:collections") return {
        {"Set[T]", "handle", "An unordered set of unique values."},
        {"Deque[T]", "handle", "A double-ended queue: push and pop at both ends in constant time."},
        {"PriorityQueue[T]", "handle", "A max-heap: `pop()` returns the largest value first, or `none` when empty."},
        {"MinQueue[T]", "handle", "A min-heap: `pop()` returns the smallest value first, or `none` when empty."},
    };
    if (mod == "core:crypto/aes") return {
        {"GcmResult", "struct", "The output of AES-256-GCM encryption.", {
            {"ciphertext", "[]byte", "The encrypted bytes."},
            {"tag", "[]byte", "The 16-byte authentication tag; decryption fails if it does not match."}}},
    };
    if (mod == "core:fs") return {
        {"OpenMode", "enum", "How `fs.open` opens a file.", {}, {"Read", "Write", "Append", "ReadWrite"}},
        {"SeekWhence", "enum", "The origin for `file.seek`.", {}, {"Start", "Current", "End"}},
        {"File", "handle", "An open file, closed automatically when it goes out of scope."},
    };
    if (mod == "core:json") return {
        {"Value", "handle", "A parsed JSON value of any shape, from `json.parse_dynamic`; inspect it with its methods."},
    };
    if (mod == "core:log") return {
        {"Level", "enum", "Minimum severity for `log.set_level`.", {}, {"Debug", "Info", "Warn", "Error"}},
        {"Format", "enum", "Output format for `log.set_format`.", {}, {"Text", "Json"}},
    };
    if (mod == "core:net") return {
        {"TcpListener", "handle", "A listening TCP socket; `accept()` returns a `TcpConn` per client."},
        {"TcpConn", "handle", "A connected TCP stream, closed when it goes out of scope."},
    };
    if (mod == "core:net/http") return {
        {"Request", "struct", "An HTTP request, as a handler receives it or as you build one to send.", {
            {"remote_addr", "str", "The client's IP address (server side)."},
            {"method", "str", "`GET`, `POST`, ... (default `GET`)."},
            {"path", "str", "The path without the query string (default `/`)."},
            {"raw_query", "str", "The query string as received, without `?`."},
            {"query_params", "map[str]str", "Decoded query parameters; `req.query(key)` reads one."},
            {"headers", "map[str]str", "Headers with lower-case names; `req.header(name)` reads one."},
            {"params", "map[str]str", "Route parameters such as `:id`; `req.param(name)` reads one."},
            {"body", "str", "The request body."}}},
        {"Response", "struct", "An HTTP response; build one with `http.Response.text`, `.json`, `.html` or `.redirect`.", {
            {"status", "i32", "The status code (default `200`)."},
            {"status_code", "i32", "The same status code, under its older name."},
            {"status_text", "str", "The reason phrase (default `OK`)."},
            {"headers", "map[str]str", "Headers with lower-case names."},
            {"body", "str", "The response body."}}},
        {"Router", "handle", "Maps method and path patterns to handlers; registering a route returns the router, so calls chain."},
        {"HttpServer", "handle", "An HTTP server bound to a port (`make(http.Server, port)`); `serve_in_background` runs it and `stop` ends it."},
        {"Server", "handle", "The name `make(http.Server, port)` constructs; the same type as `http.HttpServer`."},
        {"SseEvent", "struct", "One server-sent event.", {
            {"event", "str", "The event name (empty for the default `message`)."},
            {"data", "str", "The payload."},
            {"id", "str", "The event id, for reconnecting clients."}}},
        {"ServerLimits", "struct", ""},  // fields and summary from kStdlibStructs
        {"TlsConfig", "struct", ""},
    };
    if (mod == "core:net/tls") return {
        {"ServerContext", "handle", "A TLS server configuration (certificate and key) that wraps accepted connections."},
        {"CertKeyPair", "struct", "A certificate and its private key, in PEM form.", {
            {"cert_pem", "str", "The certificate chain."},
            {"key_pem", "str", "The private key."}}},
    };
    if (mod == "core:net/url") return {
        {"Url", "struct", "A parsed URL, from `url.parse`.", {
            {"scheme", "str", "`http`, `https`, ... (default `http`)."},
            {"host", "str", "The host name or address."},
            {"port", "u16", "The port; the scheme's default when the URL has none."},
            {"path", "str", "The path (default `/`)."},
            {"query", "str", "The query string, without `?`."},
            {"fragment", "str", "The fragment, without `#`."},
            {"query_params", "map[str]str", "Decoded query parameters."}}},
    };
    if (mod == "core:net/websocket") return {
        {"Conn", "handle", "An open WebSocket connection; messages are `str` (`recv_text`) or `[]byte` (`recv_binary`)."},
        {"Server", "handle", "A WebSocket server bound to a port."},
        {"TlsConfig", "struct", ""},
    };
    if (mod == "core:os") return {
        {"ExecResult", "struct", "The outcome of a finished subprocess (`os.command`).", {
            {"exit_code", "i32", "The process exit status."},
            {"stdout", "str", "Everything it wrote to standard output."},
            {"stderr", "str", "Everything it wrote to standard error."}}},
        {"TermSize", "struct", "A terminal's size in character cells.", {
            {"cols", "i32", "Columns (default `80`)."},
            {"rows", "i32", "Rows (default `24`)."}}},
        {"RawTerminal", "handle", "The terminal in raw mode (no echo, no line buffering); restored when it goes out of scope."},
    };
    if (mod == "core:regex") return {
        {"Regex", "handle", "A compiled regular expression, from `regex.compile`."},
    };
    if (mod == "core:sync") return {
        {"Mutex[T]", "handle", "A value of type `T` behind a lock; `lock()` returns a guard, or use `with_lock`."},
        {"MutexGuard[T]", "handle", "Holds a `Mutex[T]` locked and gives access to its value; the lock is released when the guard goes out of scope."},
        {"Channel[T]", "handle", "A thread-safe queue between tasks: `send`, `recv`, `try_recv`; `close()` wakes every waiting receiver."},
        {"Atomic[T]", "handle", "A value shared between threads and read or changed atomically: `load`, `store`, `add`, `sub`."},
        {"Context", "handle", "Carries cancellation and a deadline across tasks; see `core:sync/context`."},
    };
    if (mod == "core:sync/context") return {
        {"Context", "handle", "Carries cancellation and a deadline across tasks; cancelling a parent cancels its children."},
    };
    if (mod == "core:time") return {
        {"Duration", "handle", "A span of time with nanosecond precision, made with `time.duration_millis(ms)` and friends and passed to `time.sleep`."},
        {"Time", "handle", "A point in wall-clock time, from `time.now()`, `time.unix(sec)` or `time.from_unix_seconds(sec)`."},
    };
    if (mod == "ext:ai") return {
        {"Provider", "enum", "Which model API a `Client` talks to.", {}, {"OpenAI", "Anthropic", "Gemini", "Ollama", "OpenAICompatible", "Local", "Apple", "Mock"}},
        {"Client", "handle", "A connection to a model provider; send it messages, optionally with tools."},
        {"Agent", "handle", "A client plus a tool registry that runs the tool-calling loop for you."},
        {"McpServer", "handle", "A Model Context Protocol server exposing your tools over stdio."},
        {"Message", "struct", "One chat message.", {
            {"role", "str", "`system`, `user`, `assistant` or `tool` (default `user`)."},
            {"content", "str", "The message text."},
            {"attachments", "[]Attachment", "Files attached to the message."},
            {"tool_calls", "[]ToolCall", "Tools the assistant asked to call."},
            {"tool_call_id", "str", "For a `tool` message: the call it answers."},
            {"tool_name", "str", "For a `tool` message: the tool that ran."},
            {"is_error", "bool", "For a `tool` message: whether the tool failed."}}},
        {"Tool", "struct", "A tool the model may call; usually built with `ai.tool_def[T]`.", {
            {"name", "str", "The tool's name."},
            {"description", "str", "What it does, for the model."},
            {"schema_json", "str", "The JSON Schema of its arguments."},
            {"parameters_schema", "str", "An alternative schema field some providers use."}}},
        {"ToolCall", "struct", "A model's request to run one tool.", {
            {"id", "str", "The call id, echoed back in the tool result."},
            {"name", "str", "The tool to run."},
            {"arguments_json", "str", "Its arguments, as JSON."}}},
        {"ChatResponse", "struct", "A model's reply.", {
            {"text", "str", "The reply text."},
            {"model", "str", "The model that answered."},
            {"tool_calls", "[]ToolCall", "Tools it asked to call."}}},
        {"Attachment", "struct", "A file attached to a message.", {
            {"filename", "str", "The file name."},
            {"mime_type", "str", "Its media type."},
            {"extracted_text", "str", "Its text content."}}},
        {"Document", "struct", "A parsed document (`ai.parse_document`).", {
            {"path", "str", "Where it came from."},
            {"mime_type", "str", "Its media type."},
            {"format", "str", "The detected format."},
            {"text", "str", "The full text."},
            {"sections", "[]DocumentSection", "The text split into sections."}}},
        {"DocumentSection", "struct", "One section of a document, as `ai.chunk_document` returns them.", {
            {"index", "i32", "Its position, from `1`."},
            {"title", "str", "Its heading, if any."},
            {"text", "str", "Its text."}}},
        {"ZipEntry", "struct", "One file from a zip archive.", {
            {"name", "str", "The path inside the archive."},
            {"content", "str", "The file's contents."}}},
    };
    if (mod == "ext:db/sqlite") return {
        {"Conn", "handle", "An open SQLite database; `exec` runs statements and `query[T]` maps rows to structs."},
    };
    if (mod == "ext:net/ssh") return {
        {"Session", "handle", "An authenticated SSH connection; run commands or open channels on it."},
        {"Channel", "handle", "An SSH channel (a shell or a command) with its own input and output."},
        {"ExecResult", "struct", "The outcome of a remote command.", {
            {"exit_code", "i32", "The remote exit status."},
            {"stdout", "str", "Its standard output."},
            {"stderr", "str", "Its standard error."}}},
        {"SftpEntry", "struct", "One entry of a remote directory listing.", {
            {"name", "str", "The entry name."},
            {"is_dir", "bool", "Whether it is a directory."}}},
    };
    if (mod == "ext:webui") return {
        {"App", "handle", "A native desktop window running a web UI, with RPC between the page and cfire."},
    };
    return {};
}
// clang-format on

} // namespace detail
} // namespace cfire::doc

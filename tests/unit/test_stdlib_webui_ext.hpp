#pragma once

struct WebuiTestMixReq {
    std::int32_t channel{1};
    double gain_db{0.0};
    bool muted{false};
};

struct WebuiTestMixRes {
    std::int32_t channel{1};
    double effective_db{0.0};
    std::string status{"ok"};
};

namespace cfire::json {
template <>
inline auto parse_struct<WebuiTestMixReq>(simdjson::ondemand::object obj) -> std::expected<WebuiTestMixReq, cfire::Error> {
    WebuiTestMixReq out{};
    CFIRE_TRY_ASSIGN(out.channel, cfire::json::get_i32(obj, "channel"));
    CFIRE_TRY_ASSIGN(out.gain_db, cfire::json::get_f64(obj, "gain_db"));
    CFIRE_TRY_ASSIGN(out.muted, cfire::json::get_bool(obj, "muted"));
    return out;
}

template <>
inline auto stringify_struct<WebuiTestMixRes>(const WebuiTestMixRes& val) -> std::string {
    return std::format("{{\"channel\":{},\"effective_db\":{},\"status\":\"{}\"}}",
        val.channel, val.effective_db, cfire::json::escape_string(val.status));
}
} // namespace cfire::json

void test_webui_rpc_events_and_linux_backend([[maybe_unused]] const std::filesystem::path& rt_dir) {
    std::println("=== RUN   test_webui_rpc_events_and_linux_backend");

    auto app = cfire::webui::new_app("Studio Desk", 1024, 720);
    require(app.title() == "Studio Desk", "initial app title");

    // 1. Typed struct RPC binding
    app.bind<WebuiTestMixReq>("set_track", [&](const WebuiTestMixReq& req) -> std::expected<WebuiTestMixRes, cfire::Error> {
        if (req.channel <= 0) {
            return cfire::make_error("channel must be >= 1");
        }
        double eff = req.muted ? -96.0 : req.gain_db;
        app.emit("meter:update", WebuiTestMixRes{req.channel, eff, req.muted ? "muted" : "active"});
        return WebuiTestMixRes{req.channel, eff, req.muted ? "muted" : "active"};
    });

    // 2. Zero-arg action binding
    int reset_count = 0;
    app.bind_action("reset_desk", [&]() -> std::string {
        ++reset_count;
        return "desk_reset_ok";
    });

    // 3. Frontend event listener (`app.on`)
    std::string last_fader_event;
    app.on("fader:drag", [&](const std::string& payload) {
        last_fader_event = payload;
    });

    // 4. Verify RPC execution via `call_rpc`
    auto ok_res = app.call_rpc("set_track", R"({"channel":2,"gain_db":-4.5,"muted":false})");
    require(ok_res.has_value(), "call_rpc(set_track) succeeds");
    if (ok_res) {
        require(ok_res->contains("\"channel\":2") && ok_res->contains("\"effective_db\":-4.5"),
            "call_rpc serialized WebuiTestMixRes JSON");
    }
    require(app.emitted_events_count("meter:update") == 1, "app.emit recorded 1 meter:update event");
    require(app.last_emitted_payload("meter:update").contains("\"status\":\"active\""),
        "last_emitted_payload matches emitted struct");

    auto err_res = app.call_rpc("set_track", R"({"channel":0,"gain_db":0.0,"muted":false})");
    require(!err_res.has_value() && err_res.error().message().contains("channel must be >= 1"),
        "call_rpc propagates fallible error");

    auto act_res = app.call_rpc("reset_desk", "{}");
    require(act_res.has_value() && *act_res == "\"desk_reset_ok\"" && reset_count == 1,
        "bind_action executes and serializes string result");

    // 5. Verify raw IPC JSON-RPC & event messages (`handle_ipc`)
    app.handle_ipc(R"({"event":"fader:drag","data":{"ch":3,"db":-12.0}})");
    require(last_fader_event.contains("\"ch\":3"), "app.on receives frontend event payload");

    app.handle_ipc(R"({"__sys":"set_title","title":"Mastering Console v2"})");
    require(app.title() == "Mastering Console v2", "__sys set_title updates window title");

    // 6. Verify KV state store & VFS asset resolution
    app.set_state("preset", "Warm Tape");
    require(app.get_state("preset") == "Warm Tape", "get_state returns set_state value");
    require(app.emitted_events_count("state:change") == 1, "set_state emits state:change event");

    app.set_html("<!doctype html><html><body>Studio UI</body></html>");
    require(app.resolve_asset_content("/").contains("Studio UI"), "resolve_asset_content('/') maps to /index.html");
    require(app.resolve_asset_mime("/index.html") == "text/html; charset=utf-8", "MIME type for .html");

    ScopedTempDir vfs_dir("webui_vfs");
    (void)vfs_dir.file("app.css", "body { background: #000; }");
    (void)vfs_dir.file("bundle.mjs", "export const x = 1;");
    app.serve_dir(vfs_dir.path.string());
    require(app.resolve_asset_content("/app.css").contains("background: #000"), "serve_dir resolves /app.css");
    require(app.resolve_asset_mime("/bundle.mjs") == "text/javascript; charset=utf-8", "MIME type for .mjs");
    require(app.resolve_asset_content("/../etc/passwd").empty(), "path traversal rejected in Asset VFS");

    // 7. Verify Linux WebKitGTK Backend Compilation & IPC Dispatch via Mock GTK/WebKitGTK C Headers
    {
        std::string_view linux_mock_cpp = R"CPP(
#define CFIRE_TEST_LINUX_WEBKITGTK_BACKEND 1
#include <cstring>
#include <cstdlib>
#include <string>

// Minimal mock declarations for GTK3 + WebKit2GTK C API
using gboolean = int;
using gint = int;
using gint64 = long long;
using gssize = long;
using guint = unsigned int;
using gpointer = void*;
using GDestroyNotify = void(*)(gpointer);
using GCallback = void(*)();

#ifndef FALSE
#define FALSE 0
#endif
#ifndef TRUE
#define TRUE 1
#endif
constexpr gboolean G_SOURCE_REMOVE = 0;
constexpr int G_PRIORITY_DEFAULT = 0;
constexpr int GTK_WINDOW_TOPLEVEL = 0;
constexpr int GTK_WIN_POS_CENTER = 1;
constexpr int WEBKIT_USER_CONTENT_INJECT_ALL_FRAMES = 0;
constexpr int WEBKIT_USER_SCRIPT_INJECT_AT_DOCUMENT_START = 0;

struct GtkWidget {};
struct GtkWindow {};
struct GtkContainer {};
struct GInputStream { std::string data; };
struct WebKitURISchemeRequest { std::string path; std::string finished_body; std::string finished_mime; };
struct WebKitUserContentManager {};
struct WebKitUserScript {};
struct WebKitWebContext {};
struct WebKitWebView { std::string last_eval_js; std::string loaded_uri; };
struct JSCValue { std::string str; };
struct WebKitJavascriptResult { JSCValue val; };

using WebKitURISchemeRequestCallback = void(*)(WebKitURISchemeRequest*, gpointer);

static WebKitURISchemeRequestCallback g_registered_scheme_cb = nullptr;
static gpointer g_registered_scheme_ud = nullptr;
static WebKitWebView g_mock_webview{};
static GtkWidget g_mock_window{};

#define GTK_WINDOW(w) reinterpret_cast<GtkWindow*>(w)
#define GTK_CONTAINER(w) reinterpret_cast<GtkContainer*>(w)
#define WEBKIT_WEB_VIEW(w) (&g_mock_webview)
#define G_CALLBACK(fn) reinterpret_cast<GCallback>(fn)

inline gpointer g_malloc(std::size_t n) { return std::malloc(n); }
inline void g_free(gpointer p) { std::free(p); }
inline void g_object_unref(gpointer p) { delete static_cast<GInputStream*>(p); }

inline GInputStream* g_memory_input_stream_new_from_data(const void* data, gssize len, GDestroyNotify destroy) {
    auto* s = new GInputStream{};
    if (data && len > 0) s->data.assign(static_cast<const char*>(data), static_cast<std::size_t>(len));
    if (destroy) destroy(const_cast<void*>(data));
    return s;
}

inline const char* webkit_uri_scheme_request_get_path(WebKitURISchemeRequest* req) { return req->path.c_str(); }
inline void webkit_uri_scheme_request_finish(WebKitURISchemeRequest* req, GInputStream* stream, gint64, const char* mime) {
    req->finished_body = stream->data;
    req->finished_mime = mime ? mime : "";
}

inline JSCValue* webkit_javascript_result_get_js_value(WebKitJavascriptResult* r) { return &r->val; }
inline gboolean jsc_value_is_string(JSCValue*) { return TRUE; }
inline char* jsc_value_to_string(JSCValue* v) {
    char* out = static_cast<char*>(g_malloc(v->str.size() + 1));
    std::memcpy(out, v->str.c_str(), v->str.size() + 1);
    return out;
}

inline void gtk_main_quit() {}
inline void webkit_web_view_evaluate_javascript(WebKitWebView* wv, const char* script, gssize, const char*, const char*, void*, void*, void*) {
    if (wv && script) wv->last_eval_js = script;
}
inline guint g_idle_add_full(gint, gboolean(*fn)(gpointer), gpointer data, GDestroyNotify) {
    if (fn) fn(data);
    return 1;
}
inline gboolean gtk_init_check(int*, char***) { return TRUE; }
inline GtkWidget* gtk_window_new(int) { return &g_mock_window; }
inline void gtk_window_set_title(GtkWindow*, const char*) {}
inline void gtk_window_set_default_size(GtkWindow*, gint, gint) {}
inline void gtk_window_set_position(GtkWindow*, int) {}
inline void gtk_window_set_decorated(GtkWindow*, gboolean) {}
inline void gtk_window_resize(GtkWindow*, gint, gint) {}
inline void gtk_window_fullscreen(GtkWindow*) {}
inline void gtk_window_unfullscreen(GtkWindow*) {}
inline void gtk_window_iconify(GtkWindow*) {}
inline void gtk_window_maximize(GtkWindow*) {}
inline void gtk_window_unmaximize(GtkWindow*) {}
inline void g_signal_connect(gpointer, const char*, GCallback, gpointer) {}
inline WebKitWebContext* webkit_web_context_get_default() { return nullptr; }
inline void webkit_web_context_register_uri_scheme(WebKitWebContext*, const char*, WebKitURISchemeRequestCallback cb, gpointer ud, GDestroyNotify) {
    g_registered_scheme_cb = cb;
    g_registered_scheme_ud = ud;
}
inline WebKitUserContentManager* webkit_user_content_manager_new() { return nullptr; }
inline WebKitUserScript* webkit_user_script_new(const char*, int, int, const char* const*, const char* const*) { return nullptr; }
inline void webkit_user_content_manager_add_script(WebKitUserContentManager*, WebKitUserScript*) {}
inline void webkit_user_script_unref(WebKitUserScript*) {}
inline void webkit_user_content_manager_register_script_message_handler(WebKitUserContentManager*, const char*) {}
inline GtkWidget* webkit_web_view_new_with_user_content_manager(WebKitUserContentManager*) { return reinterpret_cast<GtkWidget*>(&g_mock_webview); }
inline void gtk_container_add(GtkContainer*, GtkWidget*) {}
inline void webkit_web_view_load_uri(WebKitWebView* wv, const char* uri) { if (wv && uri) wv->loaded_uri = uri; }
inline void gtk_widget_show_all(GtkWidget*) {}
inline void gtk_main() {
    // Exercise custom URI scheme callback (`app://localhost/index.html`)
    if (g_registered_scheme_cb) {
        WebKitURISchemeRequest req{"/index.html", "", ""};
        g_registered_scheme_cb(&req, g_registered_scheme_ud);
        if (req.finished_body.find("Linux WebKitGTK Ready") == std::string::npos) std::exit(2);
    }
}

#include "core/webui.hpp"

int main() {
    auto app = cfire::webui::new_app("Linux Test", 800, 600);
    app.set_html("<html><body>Linux WebKitGTK Ready</body></html>");
    int ping_calls = 0;
    app.bind_action("ping", [&]() -> std::string {
        ++ping_calls;
        return "pong";
    });
    app.run_linux_webkitgtk();
    if (g_mock_webview.loaded_uri != "app://localhost/index.html") return 3;
    auto r = app.call_rpc("ping", "{}");
    if (!r || *r != "\"pong\"" || ping_calls != 1) return 4;
    return 0;
}
)CPP";
        auto linux_res = compile_and_run_cpp(linux_mock_cpp, rt_dir, "webui_linux_mock");
        linux_res.assert_ok("Linux WebKitGTK backend mock-header compilation and URI/IPC verification");
    }

    // 4. Verify Upstream Stdlib & Extension Additions (CF-002, CF-003, CF-006, CF-007, CF-008, CF-021, CF-022, CF-026, CF-028, CF-043, CF-045, CF-046, CF-053)
    {
        // CF-006: ext:crypto/sodium + PBKDF2 + AES auto-nonce encrypt/decrypt
#if defined(CFIRE_SODIUM_NATIVE)
        auto salt = cfire::sodium::random_bytes(16);
        require(salt.size() == 16, "CF-006: sodium::random_bytes(16)");
        auto kdf_key = cfire::sodium::argon2id("flint-master-password", salt, 32);
        require(kdf_key.has_value() && kdf_key->size() == 32, "CF-006: sodium::argon2id derived 32-byte key");

        auto phc = cfire::sodium::argon2id_str("flint-master-password");
        require(phc.has_value() && cfire::sodium::argon2id_verify(*phc, "flint-master-password"),
            "CF-006: sodium::argon2id_str + argon2id_verify pass");
        require(!cfire::sodium::argon2id_verify(*phc, "wrong-password"),
            "CF-006: sodium::argon2id_verify rejects wrong password");

        auto plain_sod = cfire::to_bytes("ssh-ed25519 AAAAC3NzaC1lZDI1NTE5AAAAI");
        auto xsealed = cfire::sodium::xchacha20poly1305_encrypt(*kdf_key, plain_sod);
        require(xsealed.has_value() && xsealed->size() == plain_sod.size() + 40,
            "CF-006: sodium::xchacha20poly1305_encrypt output size");
        auto xopened = cfire::sodium::xchacha20poly1305_decrypt(*kdf_key, *xsealed);
        require(xopened.has_value() && str(*xopened) == "ssh-ed25519 AAAAC3NzaC1lZDI1NTE5AAAAI",
            "CF-006: sodium::xchacha20poly1305_decrypt round-trip");
        auto b2 = cfire::sodium::blake2b(plain_sod, 32);
        require(b2.has_value() && b2->size() == 32, "CF-006: sodium::blake2b(32)");
        auto subk = cfire::sodium::derive_subkey(*kdf_key, 1, "flintvlt", 32);
        require(subk.has_value() && subk->size() == 32, "CF-006: sodium::derive_subkey");
        cfire::sodium::memzero(*kdf_key);
#endif
        auto plain = cfire::to_bytes("ssh-ed25519 AAAAC3NzaC1lZDI1NTE5AAAAI");

        auto aes_key = cfire::aes::generate_key();
        auto aes_blob = cfire::aes::encrypt(aes_key, plain);
        require(aes_blob.has_value(), "CF-006: aes::encrypt auto-nonce");
        auto aes_pt = cfire::aes::decrypt(aes_key, *aes_blob);
        require(aes_pt.has_value() && str(*aes_pt) == "ssh-ed25519 AAAAC3NzaC1lZDI1NTE5AAAAI",
            "CF-006: aes::decrypt auto-nonce round-trip");

        auto pbkdf2_key = cfire::hmac::pbkdf2_sha256("password", "salt", 1000, 32);
        require(pbkdf2_key.size() == 32, "CF-006: hmac::pbkdf2_sha256");

        std::vector<std::byte> raw_blob{std::byte{0xDE}, std::byte{0xAD}, std::byte{0xBE}, std::byte{0xEF}};

        // CF-008: ext:keychain in headless mode
        ::setenv("CFIRE_HEADLESS_TEST", "1", 1);
        require(cfire::keychain::set("dev.flint.test", "acct1", "s3cr3t").has_value(), "CF-008: keychain::set");
        auto kc_val = cfire::keychain::get("dev.flint.test", "acct1");
        require(kc_val.has_value() && *kc_val == "s3cr3t", "CF-008: keychain::get");
        require(cfire::keychain::delete_password("dev.flint.test", "acct1").has_value(), "CF-008: keychain::delete_password");

        // CF-021 & CF-022: byte == u8 comparisons and str::parse_*
        std::byte b{0x0A};
        require(b == 0x0A && b == static_cast<std::uint8_t>(10), "CF-021: std::byte == integral comparison");
        require(cfire::str::parse_i64(" -98765 ").value_or(0) == -98765, "CF-022: str::parse_i64");
        require(cfire::str::parse_f64("3.5").value_or(0.0) == 3.5, "CF-022: str::parse_f64");
        require(cfire::str::parse_bool("true").value_or(false) == true, "CF-022: str::parse_bool");
        require(!cfire::str::parse_bool("yes").has_value(), "CF-022: str::parse_bool strictly rejects YAML slang");

        // CF-026: sync::Mutex, try_recv, recv_timeout
        cfire::sync::Mutex mtx;
        int counter = 0;
        mtx.with_lock([&]() { counter += 10; });
        require(counter == 10, "CF-026: sync::Mutex::with_lock");
        auto ch = cfire::sync::make_channel<int>(4);
        require(!ch.try_recv().has_value(), "CF-026: try_recv on empty channel returns nullopt");
        ch.send(77);
        require(ch.len() == 1, "CF-026: ch.len() == 1");
        require(ch.try_recv().value_or(0) == 77, "CF-026: try_recv returns queued item");
        require(!ch.recv_timeout(5).has_value(), "CF-026: recv_timeout(5ms) on empty channel times out cleanly");

        // CF-043: os::socketpair, os::pipe, os::fd_write, os::fd_read, os::fd_read_bytes, os::fd_close, and os::poll_fd
        auto sp = cfire::os::socketpair();
        require(sp.has_value(), "CF-043: os::socketpair()");
        require(cfire::os::fd_write((*sp)[0], "X").value_or(0) == 1, "CF-043: os::fd_write on socketpair");
        require(cfire::os::poll_fd((*sp)[1], 50), "CF-043: os::poll_fd detects readable socketpair");
        auto sp_bytes = cfire::os::fd_read_bytes((*sp)[1], 16);
        require(sp_bytes.has_value() && sp_bytes->size() == 1 && (*sp_bytes)[0] == std::byte{'X'},
            "CF-043: os::fd_read_bytes on socketpair");
        require(cfire::os::fd_close((*sp)[0]).has_value(), "CF-043: os::fd_close(sp[0])");
        require(cfire::os::fd_close((*sp)[1]).has_value(), "CF-043: os::fd_close(sp[1])");

        auto pp = cfire::os::pipe();
        require(pp.has_value() && pp->size() == 2, "CF-043: os::pipe()");
        require(cfire::os::fd_write((*pp)[1], "pipe-payload").value_or(0) == 12, "CF-043: os::fd_write on pipe");
        require(cfire::os::fd_close((*pp)[1]).has_value(), "CF-043: os::fd_close(pipe write end)");
        std::vector<std::byte> pipe_buf(32);
        auto pipe_n = cfire::os::fd_read((*pp)[0], pipe_buf);
        require(pipe_n.has_value() && *pipe_n == 12, "CF-043: os::fd_read on pipe");
        pipe_buf.resize(*pipe_n);
        require(cfire::to_str(pipe_buf) == "pipe-payload", "CF-043: pipe payload round-trip");
        require(cfire::os::fd_close((*pp)[0]).has_value(), "CF-043: os::fd_close(pipe read end)");

        // CF-002, CF-003, CF-028, CF-045, CF-046, CF-053: webui enhancements
        ScopedTempDir ui_tmp("cfire_webui_serve_dir");
        ui_tmp.file("index.html", "<!doctype html><html><body>Custom ServeDir Index</body></html>");
        auto app = cfire::webui::new_app("Flint", 900, 600);
        app.serve_dir(ui_tmp.path.string());
        require(app.resolve_asset_content("/index.html").contains("Custom ServeDir Index"),
            "CF-028: serve_dir(/index.html) takes priority over default placeholder without needing set_html");

        app.set_max_history(4);
        for (int i = 0; i < 10; ++i) {
            app.emit("term:data", i);
        }
        require(app.emitted_events_count("term:data") == 4, "CF-002: emitted_history capped at max_history_entries");
        app.clear_history();
        require(app.emitted_events_count("term:data") == 0, "CF-002: clear_history clears emitted_history");

        app.emit_bytes("pty:out", raw_blob);
        require(app.emitted_events_count("pty:out") == 1 && app.last_emitted_payload("pty:out") == "3q2+7w==",
            "CF-003: emit_bytes Base64 encodes binary frame for Uint8Array dispatch");

        app.bind_action_async("slow_op", []() -> std::string {
            return "async_done";
        });
        auto async_res = app.call_rpc("slow_op", "{}");
        require(async_res.has_value() && *async_res == "\"async_done\"", "CF-046: bind_action_async resolves");

        app.clipboard_set("ssh root@10.0.0.1");
        require(app.clipboard_get() == "ssh root@10.0.0.1", "CF-053: clipboard_set / clipboard_get");
        require(!app.is_clipboard_concealed(), "CF-053: normal clipboard_set is not concealed");

        // CF-053: concealed & auto-clearing secret clipboard
        app.clipboard_set_secret("super-secret-password", 35);
        require(app.clipboard_get() == "super-secret-password", "CF-053: clipboard_set_secret stores secret initially");
        require(app.is_clipboard_concealed(), "CF-053: clipboard_set_secret marks clipboard as concealed");
        std::this_thread::sleep_for(std::chrono::milliseconds(60));
        require(app.clipboard_get().empty(), "CF-053: clipboard_set_secret auto-clears after timeout when unchanged");

        // CF-053: overwriting clipboard before timeout cancels stale auto-clear
        app.clipboard_set_secret("transient-secret", 50);
        app.clipboard_set("user-copied-later");
        std::this_thread::sleep_for(std::chrono::milliseconds(70));
        require(app.clipboard_get() == "user-copied-later", "CF-053: auto-clear does not wipe newer clipboard content");

        // CF-051, CF-052 & CF-082: set_icon, add_menu_item, system tray, and window visibility controls
        app.set_icon("assets/flint-icon.svg");
        app.add_menu_item("Shell", "New Tab", "t", "menu:new_tab");
        app.set_tray("assets/flint-tray.png", "Flint SSH");
        app.add_tray_item("Quick Connect", "tray:connect");
        app.set_hide_on_close(true);
        app.hide_window();
        app.show_window();
        require(!app.monospace_fonts().empty(), "CF-045: monospace_fonts returns non-empty list");
        require(cfire::os::getpid() > 0, "CF-064: os::getpid() > 0");

        // CF-005: ext:net/ssh (dial, host_key_fingerprint, known_hosts, auth_publickey_mem, exec, PTY, SFTP, forward)
        {
            cfire::ssh::enable_mock_server(true);
            auto sess_res = cfire::ssh::dial("127.0.0.1", 2222, "deploy", 2000);
            require(sess_res.has_value(), "CF-005: ssh::dial succeeds in mock/loopback mode");
            if (sess_res) {
                auto& sess = *sess_res;
                std::string fp = sess.host_key_fingerprint();
                require(fp.starts_with("SHA256:"), "CF-005: host_key_fingerprint returns SHA256:<base64>");
                require(!sess.is_known_host(), "CF-005: new host starts unknown");
                require(sess.write_known_host().has_value(), "CF-005: write_known_host()");
                require(sess.is_known_host(), "CF-005: is_known_host() true after write_known_host()");

                std::string pem = "-----BEGIN OPENSSH PRIVATE KEY-----\nmock-ed25519-key\n-----END OPENSSH PRIVATE KEY-----";
                require(sess.auth_publickey_mem(pem).has_value(), "CF-005: auth_publickey_mem succeeds from memory without temp file");

                auto ex = sess.exec("echo hello-ssh");
                require(ex.has_value() && ex->exit_code == 0 && ex->stdout_cf_.contains("hello-ssh"),
                    "CF-005: sess.exec returns ExecResult with stdout");

                auto pty_res = sess.open_pty("xterm-256color", 120, 40);
                require(pty_res.has_value(), "CF-005: sess.open_pty succeeds");
                if (pty_res) {
                    auto& pty = *pty_res;
                    auto wn = pty.write("uptime\n");
                    require(wn.has_value() && *wn == 7, "CF-005: pty.write");
                    require(pty.resize_pty(160, 50).has_value(), "CF-005: pty.resize_pty");
                    auto out = pty.read(1024);
                    require(out.has_value() && out->contains("uptime") && out->contains("50;160t"),
                        "CF-005: pty.read reflects interactive stream and resize");
                    pty.close();
                    require(pty.is_eof(), "CF-005: pty.is_eof after close");
                }

                require(sess.sftp_mkdir("/remote/etc").has_value(), "CF-005: sftp_mkdir");
                require(sess.sftp_write_all("/remote/etc/app.conf", "port=8080\n").has_value(), "CF-005: sftp_write_all");
                auto sftp_content = sess.sftp_read_all("/remote/etc/app.conf");
                require(sftp_content.has_value() && *sftp_content == "port=8080\n", "CF-005: sftp_read_all round-trip");
                auto dir_entries = sess.sftp_list_dir("/remote/etc");
                require(dir_entries.has_value() && !dir_entries->empty() && (*dir_entries)[0].name == "app.conf",
                    "CF-005: sftp_list_dir");
                require(sess.sftp_remove("/remote/etc/app.conf").has_value(), "CF-005: sftp_remove");

                auto fwd_res = sess.open_forward("127.0.0.1", 5432);
                require(fwd_res.has_value(), "CF-005: open_forward");
                sess.close();
            }
        }

        // UPSTREAM-02 Item 8 & 9: Domain-distinct I/O (`fs::File`, `os` FDs) and `cli::FlagReader` validation
        {
            ScopedTempDir stream_tmp("cfire_unified_stream_test");
            auto spath = stream_tmp.path / "lines.txt";
            {
                auto wf = cfire::fs::open(spath, cfire::fs::OpenMode::Write);
                require(wf.has_value(), "UPSTREAM-02: fs::open for write");
                require(wf->fd() >= 0, "UPSTREAM-02: File::fd() >= 0 while open");
                require(!cfire::os::is_tty(wf->fd()), "UPSTREAM-02: regular file fd is not a tty");
                require(wf->write("first line\n").has_value(), "UPSTREAM-02: File::write(1)");
                require(wf->write("second line\n").has_value(), "UPSTREAM-02: File::write(2)");
                require(wf->flush().has_value(), "UPSTREAM-02: File::flush()");
            }
            {
                auto rf = cfire::fs::open(spath, cfire::fs::OpenMode::Read);
                require(rf.has_value(), "UPSTREAM-02: fs::open for read");
                auto l1 = rf->read_line();
                require(l1.has_value() && *l1 == "first line", "UPSTREAM-02: File::read_line(1)");
                auto l2 = rf->read_line();
                require(l2.has_value() && *l2 == "second line", "UPSTREAM-02: File::read_line(2)");
                require(!rf->read_line().has_value(), "UPSTREAM-02: File::read_line(EOF) returns nullopt");
            }

            // FlagReader numeric validation
            char arg0[] = "app";
            char arg1[] = "--port";
            char arg2[] = "not_a_port";
            char* argv_bad[] = {arg0, arg1, arg2};
            cfire::cli::FlagReader bad_reader(3, argv_bad);
            std::int32_t port = 8080;
            bad_reader.bind("--port", "-p", port);
            require(bad_reader.has_errors(), "UPSTREAM-02: FlagReader records error on invalid i32 flag");
        }
    }

    std::println("--- PASS: test_webui_rpc_events_and_linux_backend");
}

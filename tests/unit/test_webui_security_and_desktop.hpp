#pragma once
// tests/unit/test_webui_security_and_desktop.hpp — Verification for ext:webui
// release security defaults, autofill toggles, HiDPI, Dock reopen, fullscreen, and file drop.

#include "../conformance_runner.hpp"
#include "../process_run.hpp"
#include "../test_harness.hpp"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace test_webui_sec_detail {

inline void write_checked(const std::filesystem::path& p, std::string_view content) {
    std::error_code ec;
    if (!p.parent_path().empty()) {
        std::filesystem::create_directories(p.parent_path(), ec);
    }
    auto res = cfire::fs::write_text(p, content);
    require(res.has_value(), std::format("write_checked failed for {}", p.string()));
}

inline void verify_cli_release_and_debug_profiles(const std::filesystem::path& rt_dir) {
    (void)rt_dir;
    if (!conformance::host_has_optional_lib("webkit2gtk")) {
        return;
    }
    const std::string cfire = std::filesystem::absolute(cfire_exe()).string();
    ScopedTempDir tmp("cfire_webui_profile");
    auto main_cf = tmp.file("main.cf",
        "package main\n"
        "import \"ext:webui\"\n"
        "fn main() {\n"
        "    mut app := make(webui.App, \"ProfileProbe\", 800, 600)\n"
        "    defer app.close()\n"
        "    println(\"init:{},{},{}\", app.devtools_enabled(), app.context_menu_enabled(), app.autofill_enabled())\n"
        "    app.set_devtools(true)\n"
        "    app.set_context_menu(true)\n"
        "    app.set_autofill(false)\n"
        "    println(\"after:{},{},{}\", app.devtools_enabled(), app.context_menu_enabled(), app.autofill_enabled())\n"
        "    mut fs_seen := \"\"\n"
        "    app.on(\"window:fullscreen\") |p| {\n"
        "        fs_seen += p\n"
        "    }\n"
        "    app.fullscreen()\n"
        "    app.fullscreen()\n"
        "    println(\"fs:{}\", fs_seen)\n"
        "}\n");

#if defined(_WIN32)
    const char* ext = ".exe";
#else
    const char* ext = "";
#endif
    auto rel_bin = tmp.path / std::format("probe_release{}", ext);
    auto dbg_bin = tmp.path / std::format("probe_debug{}", ext);

    auto b_rel = run_process({cfire, "build", main_cf.string(), "-o", rel_bin.string()}, tmp.path, 120);
    require(b_rel.exit_code == 0, std::format("cfire build (Release) webui probe failed: {}\n{}", b_rel.out, b_rel.err));
    auto r_rel = run_process({rel_bin.string()}, tmp.path, 30);
    require(r_rel.exit_code == 0, std::format("Release webui probe exited {}: {}\n{}", r_rel.exit_code, r_rel.out, r_rel.err));
    require(r_rel.out.contains("init:false,false,true"),
            std::format("Release binary must default devtools=false context_menu=false autofill=true, got: {}", r_rel.out));
    require(r_rel.out.contains("after:true,true,false"),
            std::format("Release setters override defaults cleanly, got: {}", r_rel.out));
    require(r_rel.out.contains("fs:{\"fullscreen\":true}{\"fullscreen\":false}"),
            std::format("Release binary emits window:fullscreen enter/exit events, got: {}", r_rel.out));

    auto b_dbg = run_process({cfire, "build", "--debug", main_cf.string(), "-o", dbg_bin.string()}, tmp.path, 120);
    require(b_dbg.exit_code == 0, std::format("cfire build --debug webui probe failed: {}\n{}", b_dbg.out, b_dbg.err));
    auto r_dbg = run_process({dbg_bin.string()}, tmp.path, 30);
    require(r_dbg.exit_code == 0, std::format("Debug webui probe exited {}: {}\n{}", r_dbg.exit_code, r_dbg.out, r_dbg.err));
    require(r_dbg.out.contains("init:true,true,true"),
            std::format("DevDebug binary must default devtools=true context_menu=true autofill=true, got: {}", r_dbg.out));
}

inline void verify_shared_bridge_helpers(const std::filesystem::path& rt_dir) {
    auto state = std::make_shared<cfire::webui::detail::AppState>();
    std::vector<std::string> fs_events;
    std::vector<std::string> drop_events;
    state->event_listeners["window:fullscreen"].push_back([&](const std::string& p) { fs_events.push_back(p); });
    state->event_listeners["window:file_drop"].push_back([&](const std::string& p) { drop_events.push_back(p); });

    state->notify_fullscreen_changed(true);
    require(state->is_fullscreen && fs_events.size() == 1 && fs_events[0] == "{\"fullscreen\":true}",
            "notify_fullscreen_changed(true) updates state and emits JSON payload");
    state->notify_fullscreen_changed(false);
    require(!state->is_fullscreen && fs_events.size() == 2 && fs_events[1] == "{\"fullscreen\":false}",
            "notify_fullscreen_changed(false) clears state and emits JSON payload");

    state->notify_file_drop({"/var/vault/my file.txt", "C:\\Users\\alice\\quote\"key.pem"});
    require(drop_events.size() == 1, "notify_file_drop emits one window:file_drop event");
    auto parsed = cfire::json::parse_dynamic(drop_events[0]);
    require(parsed.has_value() && parsed->is_object(), "window:file_drop payload is valid JSON object");
    auto paths_v = parsed->get("paths");
    require(paths_v && paths_v->is_array() && paths_v->as_array().size() == 2, "window:file_drop carries two paths");
    require(paths_v->as_array()[0].as_str() == "/var/vault/my file.txt", "first dropped path preserved");
    require(paths_v->as_array()[1].as_str() == "C:\\Users\\alice\\quote\"key.pem",
            "dropped Windows backslash and quote characters escaped cleanly");

    state->devtools_enabled = false;
    state->context_menu_enabled = false;
    state->autofill_enabled = false;
    std::string preload = cfire::webui::detail::build_preload_script(*state);
    require(preload.contains("\"devtools\":false") && preload.contains("\"contextMenu\":false") &&
                preload.contains("\"autofill\":false"),
            "build_preload_script injects initial config flags");
    require(preload.contains("__cfire_set_autofill") && preload.contains("postMessageWithAdditionalObjects"),
            "preload script contains autofill enforcement and WebView2 drop helper");

    // Verify Release build plan flag & eject_cmake generator expression
    const auto prev_profile = cfire::build::current_build_profile();
    struct ProfileGuard {
        cfire::build::BuildProfile saved;
        ~ProfileGuard() { cfire::build::set_build_profile(saved); }
    } pg{prev_profile};

    ScopedTempDir plan_tmp("cfire_webui_plan");
    auto plan_cf = plan_tmp.file("main.cf", "package main\nimport \"ext:webui\"\nfn main() {}\n");

    cfire::build::set_build_profile(cfire::build::BuildProfile::Release);
    auto rel_res = cfire::build::analyze_and_emit(plan_cf, rt_dir, false, false);
    require(rel_res.has_value(), "Release analyze_and_emit succeeds");
    const auto& rel_plan = rel_res->second;
    require(std::ranges::find(rel_plan.cflags, "-DCFIRE_RELEASE_BUILD=1") != rel_plan.cflags.end(),
            "Release build plan includes -DCFIRE_RELEASE_BUILD=1");
    auto [rel_opt, rel_pch_flags] = cfire::build::build_default_pch_cflags(rt_dir, false, false, false);
    (void)rel_opt;
    require(std::ranges::find(rel_pch_flags, "-DCFIRE_RELEASE_BUILD=1") != rel_pch_flags.end(),
            "Release default PCH cflags include -DCFIRE_RELEASE_BUILD=1");

    cfire::build::set_build_profile(cfire::build::BuildProfile::DevDebug);
    auto dbg_res = cfire::build::analyze_and_emit(plan_cf, rt_dir, false, false);
    require(dbg_res.has_value(), "DevDebug analyze_and_emit succeeds");
    require(std::ranges::find(dbg_res->second.cflags, "-DCFIRE_RELEASE_BUILD=1") == dbg_res->second.cflags.end(),
            "DevDebug build plan omits -DCFIRE_RELEASE_BUILD=1");
    auto test_res = cfire::build::analyze_and_emit(plan_cf, rt_dir, false, true);
    require(test_res.has_value(), "Test mode analyze_and_emit succeeds");
    require(std::ranges::find(test_res->second.cflags, "-DCFIRE_RELEASE_BUILD=1") == test_res->second.cflags.end(),
            "Test mode build plan omits -DCFIRE_RELEASE_BUILD=1");
    auto [dbg_opt, dbg_pch_flags] = cfire::build::build_default_pch_cflags(rt_dir, false, false, false);
    (void)dbg_opt;
    require(std::ranges::find(dbg_pch_flags, "-DCFIRE_RELEASE_BUILD=1") == dbg_pch_flags.end(),
            "DevDebug default PCH cflags omit -DCFIRE_RELEASE_BUILD=1");

    cfire::build::set_build_profile(cfire::build::BuildProfile::Release);
    std::string cmake = cfire::build::generate_cmake_content(rel_plan);
    require(cmake.contains("$<$<NOT:$<CONFIG:Debug>>:CFIRE_RELEASE_BUILD=1>"),
            "generate_cmake_content emits config-conditional CFIRE_RELEASE_BUILD=1");
}

#if !defined(_WIN32)
inline void verify_linux_and_win32_mocks(const std::filesystem::path& rt_dir) {
    auto build_linux_probe = [](bool release_build) -> std::string {
        std::string hdr = release_build ? "#define CFIRE_RELEASE_BUILD 1\n" : "";
        return hdr + R"CPP(
#define CFIRE_TEST_LINUX_WEBKITGTK_BACKEND 1
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

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
constexpr guint GDK_WINDOW_STATE_FULLSCREEN = 1U << 4;

struct GtkWidget {};
struct GtkWindow {};
struct GtkContainer {};
struct GtkSettings {};
struct GInputStream { std::string data; };
struct WebKitURISchemeRequest { std::string path; };
struct WebKitUserContentManager {};
struct WebKitUserScript { std::string src; };
struct WebKitWebContext {};
struct WebKitSettings { gboolean dev_extras{FALSE}; };
struct WebKitContextMenu {};
struct WebKitHitTestResult {};
struct GdkEvent {};
struct GdkEventWindowState { guint changed_mask{0}; guint new_window_state{0}; };
struct GdkDragContext {};
struct GtkSelectionData { std::vector<std::string> uris; };
struct WebKitWebView {
    std::vector<std::string> eval_scripts;
    WebKitSettings settings;
};
struct JSCValue { std::string str; };
struct WebKitJavascriptResult { JSCValue val; };
using WebKitURISchemeRequestCallback = void(*)(WebKitURISchemeRequest*, gpointer);

static WebKitWebView g_wv{};
static GtkWidget g_win{};
static std::string g_last_preload;
static gboolean (*g_ctx_menu_cb)(WebKitWebView*, WebKitContextMenu*, GdkEvent*, WebKitHitTestResult*, gpointer) = nullptr;
static gpointer g_ctx_menu_ud = nullptr;
static gboolean (*g_win_state_cb)(GtkWidget*, GdkEventWindowState*, gpointer) = nullptr;
static gpointer g_win_state_ud = nullptr;
static void (*g_drag_cb)(GtkWidget*, GdkDragContext*, gint, gint, GtkSelectionData*, guint, guint, gpointer) = nullptr;
static gpointer g_drag_ud = nullptr;
static bool g_uri_targets_added = false;
static bool g_drag_finished = false;
static bool g_drag_signal_stopped = false;
static bool g_init_dev_extras = false;
static gboolean g_dev_extras_after_launch = FALSE;

#define GTK_WINDOW(w) reinterpret_cast<GtkWindow*>(w)
#define GTK_CONTAINER(w) reinterpret_cast<GtkContainer*>(w)
#define WEBKIT_WEB_VIEW(w) (&g_wv)
#define G_CALLBACK(fn) reinterpret_cast<GCallback>(fn)

inline gpointer g_malloc(std::size_t n) { return std::malloc(n); }
inline void g_free(gpointer p) { std::free(p); }
inline void g_strfreev(char** v) {
    if (!v) return;
    for (int i = 0; v[i] != nullptr; ++i) std::free(v[i]);
    std::free(v);
}
inline void g_object_unref(gpointer p) { delete static_cast<GInputStream*>(p); }
inline void g_signal_stop_emission_by_name(gpointer, const char* sig) {
    if (sig && std::string_view(sig) == "drag-data-received") g_drag_signal_stopped = true;
}
inline GInputStream* g_memory_input_stream_new_from_data(const void* data, gssize len, GDestroyNotify d) {
    auto* s = new GInputStream{};
    if (data && len > 0) s->data.assign(static_cast<const char*>(data), static_cast<std::size_t>(len));
    if (d) d(const_cast<void*>(data));
    return s;
}
inline const char* webkit_uri_scheme_request_get_path(WebKitURISchemeRequest* r) { return r->path.c_str(); }
inline void webkit_uri_scheme_request_finish(WebKitURISchemeRequest*, GInputStream*, gint64, const char*) {}
inline WebKitSettings* webkit_web_view_get_settings(WebKitWebView* wv) { return wv ? &wv->settings : nullptr; }
inline void webkit_settings_set_enable_developer_extras(WebKitSettings* s, gboolean en) {
    if (s) s->dev_extras = en;
}
inline void gtk_drag_dest_add_uri_targets(GtkWidget*) { g_uri_targets_added = true; }
inline char** gtk_selection_data_get_uris(GtkSelectionData* sd) {
    if (!sd || sd->uris.empty()) return nullptr;
    char** out = static_cast<char**>(std::calloc(sd->uris.size() + 1, sizeof(char*)));
    for (std::size_t i = 0; i < sd->uris.size(); ++i) {
        out[i] = static_cast<char*>(std::malloc(sd->uris[i].size() + 1));
        std::memcpy(out[i], sd->uris[i].c_str(), sd->uris[i].size() + 1);
    }
    return out;
}
inline char* g_filename_from_uri(const char* uri, char**, void**) {
    if (!uri) return nullptr;
    std::string_view sv(uri);
    if (sv.starts_with("file://")) sv.remove_prefix(7);
    std::string decoded;
    for (std::size_t i = 0; i < sv.size(); ++i) {
        if (sv[i] == '%' && i + 2 < sv.size() && sv[i + 1] == '2' && sv[i + 2] == '0') {
            decoded.push_back(' ');
            i += 2;
        } else {
            decoded.push_back(sv[i]);
        }
    }
    char* out = static_cast<char*>(std::malloc(decoded.size() + 1));
    std::memcpy(out, decoded.c_str(), decoded.size() + 1);
    return out;
}
inline void gtk_drag_finish(GdkDragContext*, gboolean ok, gboolean, guint) { if (ok) g_drag_finished = true; }
inline JSCValue* webkit_javascript_result_get_js_value(WebKitJavascriptResult* r) { return &r->val; }
inline gboolean jsc_value_is_string(JSCValue*) { return TRUE; }
inline char* jsc_value_to_string(JSCValue* v) {
    char* out = static_cast<char*>(g_malloc(v->str.size() + 1));
    std::memcpy(out, v->str.c_str(), v->str.size() + 1);
    return out;
}
inline void gtk_main_quit() {}
inline void webkit_web_view_evaluate_javascript(WebKitWebView* wv, const char* script, gssize, const char*, const char*, void*, void*, void*) {
    if (wv && script) wv->eval_scripts.emplace_back(script);
}
inline guint g_idle_add_full(gint, gboolean(*fn)(gpointer), gpointer data, GDestroyNotify) {
    if (fn) fn(data);
    return 1;
}
inline gboolean gtk_init_check(int*, char***) { return TRUE; }
inline GtkWidget* gtk_window_new(int) { return &g_win; }
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
inline GtkSettings* gtk_settings_get_default() { static GtkSettings s; return &s; }
inline void g_object_set(gpointer, const char*, ...) {}
inline void g_signal_connect(gpointer, const char* sig, GCallback cb, gpointer ud) {
    if (!sig) return;
    std::string_view sv(sig);
    if (sv == "context-menu") {
        g_ctx_menu_cb = reinterpret_cast<decltype(g_ctx_menu_cb)>(cb);
        g_ctx_menu_ud = ud;
    } else if (sv == "window-state-event") {
        g_win_state_cb = reinterpret_cast<decltype(g_win_state_cb)>(cb);
        g_win_state_ud = ud;
    } else if (sv == "drag-data-received") {
        g_drag_cb = reinterpret_cast<decltype(g_drag_cb)>(cb);
        g_drag_ud = ud;
    }
}
inline WebKitWebContext* webkit_web_context_get_default() { return nullptr; }
inline void webkit_web_context_register_uri_scheme(WebKitWebContext*, const char*, WebKitURISchemeRequestCallback, gpointer, GDestroyNotify) {}
inline WebKitUserContentManager* webkit_user_content_manager_new() { return nullptr; }
inline WebKitUserScript* webkit_user_script_new(const char* src, int, int, const char* const*, const char* const*) {
    if (src) g_last_preload = src;
    return nullptr;
}
inline void webkit_user_content_manager_add_script(WebKitUserContentManager*, WebKitUserScript*) {}
inline void webkit_user_script_unref(WebKitUserScript*) {}
inline void webkit_user_content_manager_register_script_message_handler(WebKitUserContentManager*, const char*) {}
inline GtkWidget* webkit_web_view_new_with_user_content_manager(WebKitUserContentManager*) { return reinterpret_cast<GtkWidget*>(&g_wv); }
inline void gtk_container_add(GtkContainer*, GtkWidget*) {}
inline void webkit_web_view_load_uri(WebKitWebView*, const char*) {
    g_dev_extras_after_launch = g_wv.settings.dev_extras;
}
inline void gtk_widget_show_all(GtkWidget*) {}
inline void gtk_main();

#include "core/webui.hpp"

static cfire::webui::App* g_app_ptr = nullptr;

inline void gtk_main() {
#if defined(CFIRE_RELEASE_BUILD)
    const gboolean want_init = FALSE;
    const gboolean want_suppressed = TRUE;
#else
    const gboolean want_init = TRUE;
    const gboolean want_suppressed = FALSE;
#endif
    (void)g_init_dev_extras;
    if (g_dev_extras_after_launch != want_init) std::exit(11);
    if (!g_uri_targets_added || !g_ctx_menu_cb || !g_win_state_cb || !g_drag_cb) std::exit(12);
    if (g_ctx_menu_cb(&g_wv, nullptr, nullptr, nullptr, g_ctx_menu_ud) != want_suppressed) std::exit(13);

    g_app_ptr->set_devtools(true);
    g_app_ptr->set_context_menu(true);
    g_app_ptr->set_autofill(false);
    if (g_wv.settings.dev_extras != TRUE) std::exit(14);
    if (g_ctx_menu_cb(&g_wv, nullptr, nullptr, nullptr, g_ctx_menu_ud) != FALSE) std::exit(15);

    bool saw_autofill_off = false;
    for (const auto& s : g_wv.eval_scripts) {
        if (s.find("__cfire_set_autofill(false)") != std::string::npos) saw_autofill_off = true;
    }
    if (!saw_autofill_off) std::exit(16);

    GdkEventWindowState ev_enter{GDK_WINDOW_STATE_FULLSCREEN, GDK_WINDOW_STATE_FULLSCREEN};
    g_win_state_cb(&g_win, &ev_enter, g_win_state_ud);
    GdkEventWindowState ev_exit{GDK_WINDOW_STATE_FULLSCREEN, 0};
    g_win_state_cb(&g_win, &ev_exit, g_win_state_ud);

    GtkSelectionData sd{{"file:///var/vault/alpha.key", "file:///var/vault/beta%20file.txt"}};
    g_drag_cb(reinterpret_cast<GtkWidget*>(&g_wv), nullptr, 0, 0, &sd, 0, 123, g_drag_ud);
    if (!g_drag_finished || !g_drag_signal_stopped) std::exit(17);
}

int main() {
    auto app = cfire::webui::new_app("LinuxSecurity", 800, 600);
    g_app_ptr = &app;
    std::vector<std::string> fs_events;
    std::string drop_payload;
    app.on("window:fullscreen", [&](const std::string& p) { fs_events.push_back(p); });
    app.on("window:file_drop", [&](const std::string& p) { drop_payload = p; });
    app.run_linux_webkitgtk();
    if (fs_events.size() != 2 || fs_events[0] != "{\"fullscreen\":true}" || fs_events[1] != "{\"fullscreen\":false}") return 21;
    if (drop_payload.find("/var/vault/alpha.key") == std::string::npos ||
        drop_payload.find("/var/vault/beta file.txt") == std::string::npos) return 22;
    return 0;
}
)CPP";
    };

    compile_and_run_cpp(build_linux_probe(true), rt_dir, "webui_linux_release_probe")
        .assert_ok("Linux WebKitGTK release security defaults & desktop events mock");
    compile_and_run_cpp(build_linux_probe(false), rt_dir, "webui_linux_debug_probe")
        .assert_ok("Linux WebKitGTK debug defaults & desktop events mock");

    // Cross-platform Win32 + WebView2 COM probe (Release + Debug)
    auto build_win32_probe = [](bool release_build) -> std::string {
        std::string hdr = release_build ? "#define CFIRE_RELEASE_BUILD 1\n" : "";
        return hdr + R"CPP(
#define CFIRE_TEST_WIN32_BACKEND 1
#define CFIRE_TEST_WEBVIEW2_COM 1
#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <map>
#include <string>
#include <vector>

using BOOL = int;
using BYTE = unsigned char;
using UINT = unsigned int;
using ULONG = unsigned long;
using LONG = long;
using DWORD = unsigned long;
using WPARAM = std::uintptr_t;
using LPARAM = std::intptr_t;
using LRESULT = std::intptr_t;
using LONG_PTR = std::intptr_t;
using UINT_PTR = std::uintptr_t;
using HRESULT = long;
using HANDLE = void*;
using HGLOBAL = void*;
using HWND = void*;
using HMENU = void*;
using HINSTANCE = void*;
using HICON = void*;
using HCURSOR = void*;
using HBRUSH = void*;
using HDC = void*;
using HMODULE = void*;
using HDROP = void*;
using LPWSTR = wchar_t*;
using LPCWSTR = const wchar_t*;

#ifndef STDMETHODCALLTYPE
#define STDMETHODCALLTYPE
#endif
#ifndef CALLBACK
#define CALLBACK
#endif
#ifndef FALSE
#define FALSE 0
#endif
#ifndef TRUE
#define TRUE 1
#endif
#define S_OK static_cast<HRESULT>(0L)
#define E_NOINTERFACE static_cast<HRESULT>(0x80004002L)
#define E_POINTER static_cast<HRESULT>(0x80004003L)
#define SUCCEEDED(hr) ((hr) >= 0)
#define FAILED(hr) ((hr) < 0)

struct IID {
    unsigned long Data1;
    unsigned short Data2;
    unsigned short Data3;
    unsigned char Data4[8];
};
using REFIID = const IID&;
inline bool IsEqualIID(REFIID a, REFIID b) {
    return a.Data1 == b.Data1 && a.Data2 == b.Data2 && a.Data3 == b.Data3 &&
           std::memcmp(a.Data4, b.Data4, 8) == 0;
}

inline const IID IID_IUnknown = {0x00000000, 0, 0, {0xC0, 0, 0, 0, 0, 0, 0, 0x46}};

struct IUnknown {
    virtual HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) = 0;
    virtual ULONG STDMETHODCALLTYPE AddRef() = 0;
    virtual ULONG STDMETHODCALLTYPE Release() = 0;
    virtual ~IUnknown() = default;
};

template <typename T>
inline const IID& __cfire_uuidof_helper();
#define __uuidof(T) __cfire_uuidof_helper<T>()
template <>
inline const IID& __cfire_uuidof_helper<IUnknown>() { return IID_IUnknown; }

inline void CoTaskMemFree(void* p) { std::free(p); }

constexpr UINT CP_UTF8 = 65001;
constexpr UINT GMEM_MOVEABLE = 2;
constexpr UINT CF_UNICODETEXT = 13;
constexpr int MAX_PATH = 260;
constexpr DWORD OFN_PATHMUSTEXIST = 0x800;
constexpr DWORD OFN_FILEMUSTEXIST = 0x1000;
constexpr DWORD OFN_NOCHANGEDIR = 0x8;
constexpr DWORD OFN_OVERWRITEPROMPT = 0x2;
constexpr UINT MB_OK = 0;
constexpr UINT MB_ICONINFORMATION = 0x40;
constexpr unsigned char FIXED_PITCH = 1;
constexpr DWORD TRUETYPE_FONTTYPE = 0x4;
constexpr unsigned char DEFAULT_CHARSET = 1;
constexpr int GWL_STYLE = -16;
constexpr int GWLP_USERDATA = -21;
constexpr int SM_CXSCREEN = 0;
constexpr int SM_CYSCREEN = 1;
constexpr UINT WM_CREATE = 1;
constexpr UINT WM_DESTROY = 2;
constexpr UINT WM_SIZE = 5;
constexpr UINT WM_CLOSE = 0x10;
constexpr UINT WM_QUIT = 0x12;
constexpr UINT WM_GETMINMAXINFO = 0x24;
constexpr UINT WM_SETICON = 0x80;
constexpr UINT WM_NCLBUTTONDOWN = 0xA1;
constexpr UINT WM_LBUTTONUP = 0x202;
constexpr UINT WM_RBUTTONUP = 0x205;
constexpr UINT WM_APP = 0x8000;
constexpr int SW_HIDE = 0;
constexpr int SW_SHOW = 5;
constexpr int SW_MINIMIZE = 6;
constexpr int SW_MAXIMIZE = 3;
constexpr int SW_RESTORE = 9;
constexpr UINT SWP_NOSIZE = 1;
constexpr UINT SWP_NOMOVE = 2;
constexpr UINT SWP_NOZORDER = 4;
constexpr UINT SWP_NOACTIVATE = 0x10;
constexpr UINT SWP_FRAMECHANGED = 0x20;
constexpr UINT SWP_NOOWNERZORDER = 0x200;
const HWND HWND_TOP = nullptr;
constexpr WPARAM HTCAPTION = 2;
constexpr UINT MF_SEPARATOR = 0x800;
constexpr UINT MF_STRING = 0;
constexpr UINT TPM_RETURNCMD = 0x100;
constexpr UINT TPM_NONOTIFY = 0x80;
constexpr UINT TPM_RIGHTBUTTON = 0x2;
constexpr DWORD COINIT_APARTMENTTHREADED = 2;
constexpr UINT CS_HREDRAW = 2;
constexpr UINT CS_VREDRAW = 1;
constexpr DWORD WS_POPUP = 0x80000000U;
constexpr DWORD WS_OVERLAPPEDWINDOW = 0x00CF0000U;
constexpr DWORD WS_EX_TOPMOST = 0x8U;
constexpr int CW_USEDEFAULT = -2147483647 - 1;
constexpr UINT IMAGE_ICON = 1;
constexpr UINT LR_LOADFROMFILE = 0x10;
constexpr UINT LR_DEFAULTSIZE = 0x40;
constexpr WPARAM ICON_BIG = 1;
constexpr WPARAM ICON_SMALL = 0;
constexpr UINT NIF_MESSAGE = 1;
constexpr UINT NIF_ICON = 2;
constexpr UINT NIF_TIP = 4;
constexpr DWORD NIM_ADD = 0;
constexpr DWORD NIM_DELETE = 2;
constexpr UINT PM_REMOVE = 1;
#define MAKEINTRESOURCEW(i) reinterpret_cast<const wchar_t*>(static_cast<std::uintptr_t>(i))
#define RGB(r, g, b) (static_cast<DWORD>(r) | (static_cast<DWORD>(g) << 8) | (static_cast<DWORD>(b) << 16))

struct POINT { long x; long y; };
struct RECT { long left; long top; long right; long bottom; };
struct MINMAXINFO { POINT ptReserved, ptMaxSize, ptMaxPosition, ptMinTrackSize, ptMaxTrackSize; };
struct CREATESTRUCTW { void* lpCreateParams; };
struct OPENFILENAMEW {
    DWORD lStructSize; HWND hwndOwner; wchar_t* lpstrFile; DWORD nMaxFile;
    const wchar_t* lpstrFilter; DWORD nFilterIndex; const wchar_t* lpstrTitle; DWORD Flags;
};
struct LOGFONTW { unsigned char lfCharSet; unsigned char lfPitchAndFamily; wchar_t lfFaceName[32]; };
struct TEXTMETRICW { int dummy; };
using WNDPROC = LRESULT(*)(HWND, UINT, WPARAM, LPARAM);
struct WNDCLASSEXW {
    UINT cbSize; UINT style; WNDPROC lpfnWndProc; HINSTANCE hInstance;
    HICON hIcon; HCURSOR hCursor; HBRUSH hbrBackground; const wchar_t* lpszClassName; HICON hIconSm;
};
struct NOTIFYICONDATAW {
    DWORD cbSize; HWND hWnd; UINT uID; UINT uFlags; UINT uCallbackMessage; HICON hIcon; wchar_t szTip[128];
};
struct MSG { HWND hwnd; UINT message; WPARAM wParam; LPARAM lParam; };

static HANDLE g_dpi_ctx_arg = nullptr;
static int g_dpi_ctx_calls = 0;
static int g_create_w = 0;
static int g_create_h = 0;
static RECT g_last_pos{};
static BOOL g_drag_accepted = FALSE;
static BOOL g_drag_finished = FALSE;
static LONG_PTR g_wnd_userdata = 0;
static WNDPROC g_wndproc = nullptr;
static int g_dummy_obj = 1;
static std::vector<std::wstring> g_mock_drop_files{L"C:\\Users\\op\\id_ed25519", L"C:\\Users\\op\\my notes.txt"};

inline BOOL MockSetProcessDpiAwarenessContext(HANDLE ctx) {
    ++g_dpi_ctx_calls;
    g_dpi_ctx_arg = ctx;
    return TRUE;
}
inline UINT MockGetDpiForWindow(HWND) { return 192; } // 200% DPI
inline UINT MockGetDpiForSystem() { return 192; }

inline void* GetProcAddress(HMODULE, const char* name) {
    if (!name) return nullptr;
    if (std::string_view(name) == "SetProcessDpiAwarenessContext") {
        return reinterpret_cast<void*>(&MockSetProcessDpiAwarenessContext);
    }
    if (std::string_view(name) == "GetDpiForWindow") {
        return reinterpret_cast<void*>(&MockGetDpiForWindow);
    }
    if (std::string_view(name) == "GetDpiForSystem") {
        return reinterpret_cast<void*>(&MockGetDpiForSystem);
    }
    return nullptr;
}
inline int MulDiv(int n, int num, int den) {
    return den == 0 ? 0 : static_cast<int>((static_cast<long long>(n) * num) / den);
}
inline void DragAcceptFiles(HWND, BOOL b) { g_drag_accepted = b; }
inline UINT DragQueryFileW(HDROP, UINT idx, wchar_t* buf, UINT cch) {
    if (idx == 0xFFFFFFFFU) return static_cast<UINT>(g_mock_drop_files.size());
    if (idx >= g_mock_drop_files.size()) return 0;
    const auto& s = g_mock_drop_files[idx];
    if (!buf) return static_cast<UINT>(s.size());
    UINT copy_n = std::min<UINT>(static_cast<UINT>(s.size()), cch > 0 ? cch - 1 : 0);
    std::memcpy(buf, s.data(), copy_n * sizeof(wchar_t));
    if (cch > 0) buf[copy_n] = L'\0';
    return copy_n;
}
inline void DragFinish(HDROP) { g_drag_finished = TRUE; }

inline int MultiByteToWideChar(UINT, DWORD, const char* src, int srclen, wchar_t* dst, int dstlen) {
    if (!src || srclen <= 0) return 0;
    if (!dst || dstlen == 0) return srclen;
    for (int i = 0; i < srclen && i < dstlen; ++i) dst[i] = static_cast<wchar_t>(static_cast<unsigned char>(src[i]));
    return srclen;
}
inline int WideCharToMultiByte(UINT, DWORD, const wchar_t* src, int srclen, char* dst, int dstlen, const char*, BOOL*) {
    if (!src || srclen <= 0) return 0;
    if (!dst || dstlen == 0) return srclen;
    for (int i = 0; i < srclen && i < dstlen; ++i) dst[i] = static_cast<char>(src[i] & 0xFF);
    return srclen;
}
inline HWND GetActiveWindow() { return &g_dummy_obj; }
inline BOOL OpenClipboard(HWND) { return TRUE; }
inline BOOL EmptyClipboard() { return TRUE; }
inline BOOL CloseClipboard() { return TRUE; }
inline HGLOBAL GlobalAlloc(UINT, std::size_t) { return nullptr; }
inline void* GlobalLock(HGLOBAL) { return nullptr; }
inline BOOL GlobalUnlock(HGLOBAL) { return TRUE; }
inline HGLOBAL GlobalFree(HGLOBAL) { return nullptr; }
inline HANDLE SetClipboardData(UINT, HANDLE) { return nullptr; }
inline HANDLE GetClipboardData(UINT) { return nullptr; }
inline UINT RegisterClipboardFormatW(const wchar_t*) { return 1; }
inline BOOL GetOpenFileNameW(OPENFILENAMEW*) { return FALSE; }
inline BOOL GetSaveFileNameW(OPENFILENAMEW*) { return FALSE; }
inline int MessageBoxW(HWND, const wchar_t*, const wchar_t*, UINT) { return 1; }
inline HDC GetDC(HWND) { return nullptr; }
inline int ReleaseDC(HWND, HDC) { return 1; }
using FONTENUMPROCW = int(*)(const LOGFONTW*, const TEXTMETRICW*, DWORD, LPARAM);
inline int EnumFontFamiliesExW(HDC, LOGFONTW*, FONTENUMPROCW, LPARAM, DWORD) { return 1; }
inline LONG_PTR GetWindowLongPtrW(HWND, int) { return g_wnd_userdata; }
inline LONG_PTR SetWindowLongPtrW(HWND, int idx, LONG_PTR v) {
    if (idx == GWLP_USERDATA) { LONG_PTR o = g_wnd_userdata; g_wnd_userdata = v; return o; }
    return v;
}
inline BOOL GetClientRect(HWND, RECT* r) { if (r) *r = RECT{0, 0, g_create_w, g_create_h}; return TRUE; }
inline BOOL GetWindowRect(HWND, RECT* r) { if (r) *r = RECT{10, 20, 10 + g_create_w, 20 + g_create_h}; return TRUE; }
inline int GetSystemMetrics(int i) { return i == SM_CXSCREEN ? 3840 : 2160; }
inline BOOL ShowWindow(HWND, int) { return TRUE; }
inline BOOL DestroyWindow(HWND h) { if (g_wndproc) g_wndproc(h, WM_DESTROY, 0, 0); return TRUE; }
inline HMENU CreatePopupMenu() { return &g_dummy_obj; }
inline BOOL AppendMenuW(HMENU, UINT, UINT_PTR, const wchar_t*) { return TRUE; }
inline BOOL GetCursorPos(POINT* p) { if (p) *p = POINT{0, 0}; return TRUE; }
inline BOOL SetForegroundWindow(HWND) { return TRUE; }
inline BOOL TrackPopupMenu(HMENU, UINT, int, int, int, HWND, const RECT*) { return 0; }
inline BOOL DestroyMenu(HMENU) { return TRUE; }
inline void PostQuitMessage(int) {}
inline LRESULT DefWindowProcW(HWND, UINT, WPARAM, LPARAM) { return 0; }
inline HRESULT CoInitializeEx(void*, DWORD) { return 0; }
inline void CoUninitialize() {}
inline HINSTANCE GetModuleHandleW(const wchar_t*) { return &g_dummy_obj; }
inline HCURSOR LoadCursorW(HINSTANCE, const wchar_t*) { return &g_dummy_obj; }
inline HBRUSH CreateSolidBrush(DWORD) { return &g_dummy_obj; }
inline UINT RegisterClassExW(const WNDCLASSEXW* wc) { if (wc) g_wndproc = wc->lpfnWndProc; return 1; }
inline HWND CreateWindowExW(DWORD, const wchar_t*, const wchar_t*, DWORD, int, int, int w, int h, HWND, HMENU, HINSTANCE, void* lpParam) {
    g_create_w = w; g_create_h = h;
    HWND hwnd = &g_dummy_obj;
    if (g_wndproc) { CREATESTRUCTW cs{lpParam}; g_wndproc(hwnd, WM_CREATE, 0, reinterpret_cast<LPARAM>(&cs)); }
    return hwnd;
}
inline HRESULT DwmSetWindowAttribute(HWND, DWORD, const void*, DWORD) { return 0; }
inline HANDLE LoadImageW(HINSTANCE, const wchar_t*, UINT, int, int, UINT) { return &g_dummy_obj; }
inline HICON LoadIconW(HINSTANCE, const wchar_t*) { return &g_dummy_obj; }
inline LRESULT SendMessageW(HWND, UINT, WPARAM, LPARAM) { return 0; }
inline BOOL Shell_NotifyIconW(DWORD, NOTIFYICONDATAW*) { return TRUE; }
inline BOOL SetWindowTextW(HWND, const wchar_t*) { return TRUE; }
inline BOOL SetWindowPos(HWND, HWND, int x, int y, int w, int h, UINT) {
    g_last_pos = RECT{x, y, x + w, y + h};
    return TRUE;
}
inline BOOL ReleaseCapture() { return TRUE; }
inline BOOL PostMessageW(HWND h, UINT m, WPARAM w, LPARAM l) { if (g_wndproc) g_wndproc(h, m, w, l); return TRUE; }
inline BOOL UpdateWindow(HWND) { return TRUE; }
inline BOOL GetMessageW(MSG*, HWND, UINT, UINT) { return 0; }
inline BOOL TranslateMessage(const MSG*) { return TRUE; }
inline LRESULT DispatchMessageW(const MSG*) { return 0; }

static int g_win_step = 0;
inline BOOL PeekMessageW(MSG* msg, HWND, UINT, UINT, UINT) {
    if (!msg) return FALSE;
    ++g_win_step;
    if (g_win_step == 1 && g_wndproc) {
        RECT r{40, 50, 1240, 850};
        g_wndproc(&g_dummy_obj, 0x02E0, 0, reinterpret_cast<LPARAM>(&r)); // WM_DPICHANGED
        return FALSE;
    }
    if (g_win_step == 2 && g_wndproc) {
        g_wndproc(&g_dummy_obj, 0x0233, reinterpret_cast<WPARAM>(&g_dummy_obj), 0); // WM_DROPFILES
        return FALSE;
    }
    if (g_win_step == 3) {
        msg->hwnd = &g_dummy_obj;
        msg->message = WM_QUIT;
        return TRUE;
    }
    return FALSE;
}

#include "core/webui.hpp"

using cfire::webui::detail::ICoreWebView2Settings4;
using cfire::webui::detail::ICoreWebView2File;
using cfire::webui::detail::ICoreWebView2ObjectCollectionView;
using cfire::webui::detail::ICoreWebView2WebMessageReceivedEventArgs2;

struct MockSettings4 final : ICoreWebView2Settings4 {
    std::atomic<ULONG> refs{1};
    BOOL devtools{TRUE};
    BOOL ctx_menu{TRUE};
    BOOL pw_autosave{TRUE};
    BOOL general_autofill{TRUE};
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID r, void** out) override {
        if (!out) return E_POINTER;
        if (IsEqualIID(r, IID_IUnknown) || IsEqualIID(r, cfire::webui::detail::wv2_iid(cfire::webui::detail::kIidSettings4))) {
            *out = static_cast<ICoreWebView2Settings4*>(this);
            AddRef();
            return S_OK;
        }
        *out = nullptr;
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs; }
    ULONG STDMETHODCALLTYPE Release() override { ULONG n = --refs; if (n == 0) delete this; return n; }
    HRESULT STDMETHODCALLTYPE get_IsScriptEnabled(BOOL* v) override { *v = TRUE; return S_OK; }
    HRESULT STDMETHODCALLTYPE put_IsScriptEnabled(BOOL) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE get_IsWebMessageEnabled(BOOL* v) override { *v = TRUE; return S_OK; }
    HRESULT STDMETHODCALLTYPE put_IsWebMessageEnabled(BOOL) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE get_AreDefaultScriptDialogsEnabled(BOOL* v) override { *v = TRUE; return S_OK; }
    HRESULT STDMETHODCALLTYPE put_AreDefaultScriptDialogsEnabled(BOOL) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE get_IsStatusBarEnabled(BOOL* v) override { *v = FALSE; return S_OK; }
    HRESULT STDMETHODCALLTYPE put_IsStatusBarEnabled(BOOL) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE get_AreDevToolsEnabled(BOOL* v) override { *v = devtools; return S_OK; }
    HRESULT STDMETHODCALLTYPE put_AreDevToolsEnabled(BOOL v) override { devtools = v; return S_OK; }
    HRESULT STDMETHODCALLTYPE get_AreDefaultContextMenusEnabled(BOOL* v) override { *v = ctx_menu; return S_OK; }
    HRESULT STDMETHODCALLTYPE put_AreDefaultContextMenusEnabled(BOOL v) override { ctx_menu = v; return S_OK; }
    HRESULT STDMETHODCALLTYPE get_AreHostObjectsAllowed(BOOL* v) override { *v = TRUE; return S_OK; }
    HRESULT STDMETHODCALLTYPE put_AreHostObjectsAllowed(BOOL) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE get_IsZoomControlEnabled(BOOL* v) override { *v = TRUE; return S_OK; }
    HRESULT STDMETHODCALLTYPE put_IsZoomControlEnabled(BOOL) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE get_IsBuiltInErrorPageEnabled(BOOL* v) override { *v = TRUE; return S_OK; }
    HRESULT STDMETHODCALLTYPE put_IsBuiltInErrorPageEnabled(BOOL) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE get_UserAgent(LPWSTR*) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE put_UserAgent(LPCWSTR) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE get_AreBrowserAcceleratorKeysEnabled(BOOL* v) override { *v = TRUE; return S_OK; }
    HRESULT STDMETHODCALLTYPE put_AreBrowserAcceleratorKeysEnabled(BOOL) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE get_IsPasswordAutosaveEnabled(BOOL* v) override { *v = pw_autosave; return S_OK; }
    HRESULT STDMETHODCALLTYPE put_IsPasswordAutosaveEnabled(BOOL v) override { pw_autosave = v; return S_OK; }
    HRESULT STDMETHODCALLTYPE get_IsGeneralAutofillEnabled(BOOL* v) override { *v = general_autofill; return S_OK; }
    HRESULT STDMETHODCALLTYPE put_IsGeneralAutofillEnabled(BOOL v) override { general_autofill = v; return S_OK; }
};

struct MockWv2File final : ICoreWebView2File {
    std::atomic<ULONG> refs{1};
    std::wstring path;
    explicit MockWv2File(std::wstring p) : path(std::move(p)) {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID r, void** out) override {
        if (!out) return E_POINTER;
        if (IsEqualIID(r, IID_IUnknown) || IsEqualIID(r, cfire::webui::detail::wv2_iid(cfire::webui::detail::kIidFile))) {
            *out = static_cast<ICoreWebView2File*>(this);
            AddRef();
            return S_OK;
        }
        *out = nullptr;
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs; }
    ULONG STDMETHODCALLTYPE Release() override { ULONG n = --refs; if (n == 0) delete this; return n; }
    HRESULT STDMETHODCALLTYPE get_Path(LPWSTR* out) override {
        if (!out) return E_POINTER;
        std::size_t bytes = (path.size() + 1) * sizeof(wchar_t);
        auto* mem = static_cast<wchar_t*>(std::malloc(bytes));
        std::memcpy(mem, path.c_str(), bytes);
        *out = mem;
        return S_OK;
    }
};

struct MockObjCollection final : ICoreWebView2ObjectCollectionView {
    std::atomic<ULONG> refs{1};
    std::vector<MockWv2File*> items;
    explicit MockObjCollection(std::vector<MockWv2File*> it) : items(std::move(it)) {
        for (auto* p : items) p->AddRef();
    }
    ~MockObjCollection() {
        for (auto* p : items) p->Release();
    }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID r, void** out) override {
        if (!out) return E_POINTER;
        if (IsEqualIID(r, IID_IUnknown)) { *out = static_cast<IUnknown*>(this); AddRef(); return S_OK; }
        *out = nullptr;
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs; }
    ULONG STDMETHODCALLTYPE Release() override { ULONG n = --refs; if (n == 0) delete this; return n; }
    HRESULT STDMETHODCALLTYPE get_Count(UINT* v) override { *v = static_cast<UINT>(items.size()); return S_OK; }
    HRESULT STDMETHODCALLTYPE GetValueAtIndex(UINT idx, IUnknown** out) override {
        if (!out || idx >= items.size()) return E_POINTER;
        items[idx]->AddRef();
        *out = items[idx];
        return S_OK;
    }
};

struct MockMsgArgs2 final : ICoreWebView2WebMessageReceivedEventArgs2 {
    std::atomic<ULONG> refs{1};
    MockObjCollection* coll;
    explicit MockMsgArgs2(MockObjCollection* c) : coll(c) { coll->AddRef(); }
    ~MockMsgArgs2() { coll->Release(); }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID r, void** out) override {
        if (!out) return E_POINTER;
        if (IsEqualIID(r, IID_IUnknown) || IsEqualIID(r, cfire::webui::detail::wv2_iid(cfire::webui::detail::kIidWebMessageReceivedArgs2))) {
            *out = static_cast<ICoreWebView2WebMessageReceivedEventArgs2*>(this);
            AddRef();
            return S_OK;
        }
        *out = nullptr;
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs; }
    ULONG STDMETHODCALLTYPE Release() override { ULONG n = --refs; if (n == 0) delete this; return n; }
    HRESULT STDMETHODCALLTYPE get_Source(LPWSTR*) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE get_WebMessageAsJson(LPWSTR*) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE TryGetWebMessageAsString(LPWSTR*) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE get_AdditionalObjects(ICoreWebView2ObjectCollectionView** out) override {
        coll->AddRef();
        *out = coll;
        return S_OK;
    }
};

int main() {
    auto app = cfire::webui::new_app("Win32Security", 960, 640);
    if (g_dpi_ctx_calls == 0 || g_dpi_ctx_arg != reinterpret_cast<HANDLE>(static_cast<LONG_PTR>(-4))) return 2;

    std::string drop_payload;
    std::vector<std::string> fs_events;
    app.on("window:file_drop", [&](const std::string& p) { drop_payload = p; });
    app.on("window:fullscreen", [&](const std::string& p) { fs_events.push_back(p); });

    app.run_win32_webview2(true, 100);
    if (g_create_w != 1920 || g_create_h != 1280) return 3;
    if (!g_drag_accepted || !g_drag_finished) return 4;
    if (g_last_pos.left != 40 || g_last_pos.top != 50 || (g_last_pos.right - g_last_pos.left) != 1200) return 5;
    if (drop_payload.find("id_ed25519") == std::string::npos || drop_payload.find("my notes.txt") == std::string::npos) return 6;

    auto* s4 = new MockSettings4();
#if defined(CFIRE_RELEASE_BUILD)
    const BOOL expected_dev_default = FALSE;
#else
    const BOOL expected_dev_default = TRUE;
#endif
    cfire::webui::detail::apply_webview2_settings(s4, app.devtools_enabled(), app.context_menu_enabled(), app.autofill_enabled());
    if (s4->devtools != expected_dev_default || s4->ctx_menu != expected_dev_default) return 7;
    if (s4->pw_autosave != TRUE || s4->general_autofill != TRUE) return 8;

    app.set_autofill(false);
    app.set_devtools(true);
    app.set_context_menu(true);
    cfire::webui::detail::apply_webview2_settings(s4, app.devtools_enabled(), app.context_menu_enabled(), app.autofill_enabled());
    if (s4->devtools != TRUE || s4->ctx_menu != TRUE || s4->pw_autosave != FALSE || s4->general_autofill != FALSE) return 9;
    s4->Release();

    auto* f1 = new MockWv2File(L"C:\\Users\\op\\dropped.bin");
    auto* coll = new MockObjCollection({f1});
    auto* msg2 = new MockMsgArgs2(coll);
    auto extracted = cfire::webui::detail::extract_webview2_dropped_files(msg2);
    msg2->Release();
    coll->Release();
    f1->Release();
    if (extracted.size() != 1 || extracted[0] != "C:\\Users\\op\\dropped.bin") return 10;

    app.fullscreen();
    app.fullscreen();
    if (fs_events.size() != 2 || fs_events[0] != "{\"fullscreen\":true}" || fs_events[1] != "{\"fullscreen\":false}") return 11;
    return 0;
}
)CPP";
    };

    compile_and_run_cpp(build_win32_probe(true), rt_dir, "webui_win32_release_probe")
        .assert_ok("Win32 + WebView2 COM release security defaults, HiDPI & file drop mock");
    compile_and_run_cpp(build_win32_probe(false), rt_dir, "webui_win32_debug_probe")
        .assert_ok("Win32 + WebView2 COM debug defaults, HiDPI & file drop mock");
}
#endif // !defined(_WIN32)

#if defined(__APPLE__)
inline void verify_macos_cocoa_and_webkit(const std::filesystem::path& rt_dir) {
    std::string_view macos_mm = R"MM(
#define CFIRE_RELEASE_BUILD 1
#import <Cocoa/Cocoa.h>
#import <WebKit/WebKit.h>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <mutex>
#include <string>
#include <vector>

#include "core/webui.hpp"

@interface CfireTestDraggingInfo : NSObject <NSDraggingInfo> {
@public
    NSPasteboard* pb;
}
@end

@implementation CfireTestDraggingInfo
- (NSWindow*)draggingDestinationWindow { return nil; }
- (NSDragOperation)draggingSourceOperationMask { return NSDragOperationCopy; }
- (NSPoint)draggingLocation { return NSMakePoint(10, 10); }
- (NSPoint)draggedImageLocation { return NSMakePoint(10, 10); }
- (NSImage*)draggedImage { return nil; }
- (NSPasteboard*)draggingPasteboard { return pb; }
- (id)draggingSource { return nil; }
- (NSInteger)draggingSequenceNumber { return 1; }
- (void)slideDraggedImageTo:(NSPoint)screenPoint { (void)screenPoint; }
- (NSArray<NSString*>*)namesOfPromisedFilesDroppedAtDestination:(NSURL*)dropDestination {
    (void)dropDestination;
    return @[];
}
- (void)enumerateDraggingItemsWithOptions:(NSDraggingItemEnumerationOptions)enumOpts
                                  forView:(NSView*)view
                                  classes:(NSArray<Class>*)classArray
                            searchOptions:(NSDictionary<NSPasteboardReadingOptionKey, id>*)searchOptions
                               usingBlock:(void (^)(NSDraggingItem* draggingItem, NSInteger idx, BOOL* stop))block {
    (void)enumOpts; (void)view; (void)classArray; (void)searchOptions; (void)block;
}
@synthesize draggingFormation;
@synthesize animatesToDestination;
@synthesize numberOfValidItemsForDrop;
@synthesize springLoadingHighlight;
- (void)resetSpringLoading {}
@end

int main(int argc, char** argv) {
    @autoreleasepool {
        [NSApplication sharedApplication];
        auto state = std::make_shared<cfire::webui::detail::AppState>();
        if (state->devtools_enabled || state->context_menu_enabled || !state->autofill_enabled) {
            return 2;
        }

        // 1. Inspect CfireWKWebView Release security defaults + live toggle + context menu suppression + file drop
        WKWebViewConfiguration* config = [[WKWebViewConfiguration alloc] init];
        cfire::webui::detail::apply_macos_webview_prefs(config, nil, state->devtools_enabled);
        id devExtras = [config.preferences valueForKey:@"developerExtrasEnabled"];
        if (!devExtras || [devExtras boolValue] != NO) return 3;

        CfireWKWebView* wv = [[CfireWKWebView alloc] initWithFrame:NSMakeRect(0, 0, 400, 300) configuration:config];
        wv->state = state;
        cfire::webui::detail::apply_macos_webview_prefs(config, wv, state->devtools_enabled);
        if (@available(macOS 13.3, *)) {
            if (wv.inspectable != NO) return 4;
        }

        NSMenu* ctxMenu = [[NSMenu alloc] initWithTitle:@"Context"];
        NSEvent* dummyEvt = [NSEvent mouseEventWithType:NSEventTypeRightMouseDown
                                               location:NSMakePoint(10, 10)
                                          modifierFlags:0
                                              timestamp:0
                                           windowNumber:0
                                                context:nil
                                            eventNumber:0
                                             clickCount:1
                                               pressure:1.0];
        [ctxMenu addItemWithTitle:@"Inspect Element" action:nil keyEquivalent:@""];
        [wv willOpenMenu:ctxMenu withEvent:dummyEvt];
        if ([ctxMenu numberOfItems] != 0) return 5;

        {
            std::lock_guard<std::recursive_mutex> lock(state->mtx);
            state->devtools_enabled = true;
            state->context_menu_enabled = true;
        }
        cfire::webui::detail::apply_macos_webview_prefs(config, wv, state->devtools_enabled);
        if ([[config.preferences valueForKey:@"developerExtrasEnabled"] boolValue] != YES) return 6;
        if (@available(macOS 13.3, *)) {
            if (wv.inspectable != YES) return 7;
        }
        [ctxMenu addItemWithTitle:@"Inspect Element" action:nil keyEquivalent:@""];
        [wv willOpenMenu:ctxMenu withEvent:dummyEvt];
        if ([ctxMenu numberOfItems] != 1) return 8;

        // 2. Test Dock reopen delegate & Window fullscreen notifications
        NSWindow* win = [[NSWindow alloc] initWithContentRect:NSMakeRect(-2000, -2000, 320, 240)
                                                    styleMask:NSWindowStyleMaskTitled
                                                      backing:NSBackingStoreBuffered
                                                        defer:NO];
        [win setReleasedWhenClosed:NO];
        {
            std::lock_guard<std::recursive_mutex> lock(state->mtx);
            state->window_hidden = true;
        }
        [win orderOut:nil];

        int reopen_count = 0;
        std::vector<std::string> fs_events;
        std::string drop_payload;
        state->event_listeners["window:reopen"].push_back([&](const std::string&) { ++reopen_count; });
        state->event_listeners["window:fullscreen"].push_back([&](const std::string& p) { fs_events.push_back(p); });
        state->event_listeners["window:file_drop"].push_back([&](const std::string& p) { drop_payload = p; });

        CfireWebUIMacAppDelegate* appDel = [[CfireWebUIMacAppDelegate alloc] init];
        appDel->state = state;
        appDel->window = win;
        BOOL reopenRet = [appDel applicationShouldHandleReopen:NSApp hasVisibleWindows:NO];
        if (reopenRet != NO || state->window_hidden || ![win isVisible] || reopen_count != 1) return 9;

        CfireWebUIWindowDelegate* winDel = [[CfireWebUIWindowDelegate alloc] init];
        winDel->state = state;
        winDel->stop_nsapp_on_close = false;
        [winDel windowDidEnterFullScreen:[NSNotification notificationWithName:NSWindowDidEnterFullScreenNotification object:win]];
        [winDel windowDidExitFullScreen:[NSNotification notificationWithName:NSWindowDidExitFullScreenNotification object:win]];
        if (fs_events.size() != 2 || fs_events[0] != "{\"fullscreen\":true}" || fs_events[1] != "{\"fullscreen\":false}") return 10;

        NSPasteboard* pb = [NSPasteboard pasteboardWithUniqueName];
        [pb clearContents];
        NSString* dropPath = (argc > 1 && argv[1] && argv[1][0] != '\0')
            ? [NSString stringWithUTF8String:argv[1]]
            : @"/var/vault/cocoa_drop.key";
        NSURL* fileURL = [NSURL fileURLWithPath:dropPath];
        BOOL pbWrote = [pb writeObjects:@[fileURL]];
        CfireTestDraggingInfo* dragInfo = [[CfireTestDraggingInfo alloc] init];
        dragInfo->pb = pb;
        if ([wv draggingEntered:dragInfo] != NSDragOperationCopy) return 11;
        if ([wv performDragOperation:dragInfo] != YES) return 12;
        if (drop_payload.find("cocoa_drop.key") == std::string::npos) {
            NSArray<NSURL*>* dbgUrls = [pb readObjectsForClasses:@[[NSURL class]]
                                                         options:@{NSPasteboardURLReadingFileURLsOnlyKey: @YES}];
            std::fprintf(stderr, "check 13 failed: pbWrote=%d types=%lu urls=%lu drop_payload='%s'\n",
                         pbWrote ? 1 : 0,
                         static_cast<unsigned long>([[pb types] count]),
                         static_cast<unsigned long>(dbgUrls ? [dbgUrls count] : 0UL),
                         drop_payload.c_str());
            return 13;
        }
        [win close];

        // 3. Verify live offscreen WKWebView autofill opt-out + default release inspector state
        auto app = cfire::webui::new_app("LiveWKTest", 480, 320);
        if (app.devtools_enabled() || app.context_menu_enabled() || !app.autofill_enabled()) return 14;
        app.set_autofill(false);
        std::string auto_result;
        app.bind_action("report_autofill", [&](const std::string& p) -> std::string {
            auto_result = p;
            return "ok";
        });
        app.set_html(R"HTML(<!doctype html><html><body>
            <form id="f"><input id="pw" type="password" /><textarea id="ta"></textarea></form>
            <script>
            (async () => {
                const f = document.getElementById("f");
                const pw = document.getElementById("pw");
                const ta = document.getElementById("ta");
                await window.invoke("report_autofill", {
                    form: f.getAttribute("autocomplete") || "",
                    input: pw.getAttribute("autocomplete") || "",
                    textarea: ta.getAttribute("autocomplete") || ""
                });
                window.runtime.Quit();
            })();
            </script></body></html>)HTML");
        auto wt = app.run_webview_test(4000);
        if (!wt || !*wt) return 15;
        if (auto_result.find("\"form\":\"off\"") == std::string::npos ||
            auto_result.find("\"input\":\"off\"") == std::string::npos ||
            auto_result.find("\"textarea\":\"off\"") == std::string::npos) {
            return 16;
        }
    }
    return 0;
}
)MM";

    ScopedTempDir tmp("cfire_macos_webui");
    auto drop_file = tmp.file("cocoa_drop.key", "secret");
    auto mm_file = tmp.file("macos_webui_test.mm", macos_mm);
    auto bin_file = tmp.path / "macos_webui_test";
    std::string comp_cmd = std::format(
        "clang++ -std=c++2b -O2 -Wall -Wextra -fobjc-arc -x objective-c++ -I\"{}\" \"{}\" -framework Cocoa -framework WebKit -o \"{}\"",
        rt_dir.string(), mm_file.string(), bin_file.string());
    int rc = 0;
    std::string comp_out = cfire::bridge::run_command_capture(comp_cmd, rc);
    require(rc == 0, std::format("macOS Objective-C++ Cocoa/WKWebView test failed to compile (rc={}):\n{}", rc, comp_out));
    require(!comp_out.contains("warning:"), std::format("macOS Objective-C++ test emitted warnings:\n{}", comp_out));

    int run_rc = 0;
    std::string run_out = cfire::bridge::run_command_capture(
        std::format("\"{}\" \"{}\"", bin_file.string(), drop_file.string()), run_rc);
    require(run_rc == 0, std::format("macOS Cocoa/WKWebView test failed with exit {}:\n{}", run_rc, run_out));
}
#endif // defined(__APPLE__)

#if defined(_WIN32)
inline void verify_native_win32_and_com() {
    (void)cfire::webui::detail::win32_enable_per_monitor_dpi_v2();

    // 1. Test real Win32 HWND + cfire_win32_wndproc WM_DPICHANGED handling
    auto state = std::make_shared<cfire::webui::detail::AppState>();
    cfire::webui::detail::Win32WindowContext ctx{state, nullptr, nullptr, nullptr, nullptr};
    HWND hwnd = ::CreateWindowExW(
        0, L"STATIC", L"DpiTest", WS_POPUP, 10, 10, 400, 300, nullptr, nullptr, ::GetModuleHandleW(nullptr), nullptr);
    require(hwnd != nullptr, "native Win32 test window created");
    ctx.hwnd = hwnd;
    ::SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(&ctx));
    RECT suggested{50, 60, 650, 560};
    cfire::webui::detail::cfire_win32_wndproc(hwnd, 0x02E0 /* WM_DPICHANGED */, 0, reinterpret_cast<LPARAM>(&suggested));
    RECT actual{};
    ::GetWindowRect(hwnd, &actual);
    require(actual.left == 50 && actual.top == 60 && (actual.right - actual.left) == 600 &&
                (actual.bottom - actual.top) == 500,
            "WM_DPICHANGED resizes real Win32 HWND to suggested rect");
    ::DestroyWindow(hwnd);

    // 2. Test apply_webview2_settings & extract_webview2_dropped_files with real Win32 CoTaskMemAlloc
    using cfire::webui::detail::ICoreWebView2Settings4;
    using cfire::webui::detail::ICoreWebView2File;
    using cfire::webui::detail::ICoreWebView2ObjectCollectionView;
    using cfire::webui::detail::ICoreWebView2WebMessageReceivedEventArgs2;
    static constexpr IID kIidUnknown = cfire::webui::detail::wv2_iid("00000000-0000-0000-c000-000000000046");

    struct NativeSettings4 final : ICoreWebView2Settings4 {
        std::atomic<ULONG> refs{1};
        BOOL devtools{TRUE};
        BOOL ctx_menu{TRUE};
        BOOL pw_autosave{TRUE};
        BOOL general_autofill{TRUE};
        HRESULT STDMETHODCALLTYPE QueryInterface(REFIID r, void** out) override {
            if (!out) return E_POINTER;
            if (IsEqualIID(r, kIidUnknown) ||
                IsEqualIID(r, cfire::webui::detail::wv2_iid(cfire::webui::detail::kIidSettings4))) {
                *out = static_cast<ICoreWebView2Settings4*>(this);
                AddRef();
                return S_OK;
            }
            *out = nullptr;
            return E_NOINTERFACE;
        }
        ULONG STDMETHODCALLTYPE AddRef() override { return ++refs; }
        ULONG STDMETHODCALLTYPE Release() override { ULONG n = --refs; if (n == 0) delete this; return n; }
        HRESULT STDMETHODCALLTYPE get_IsScriptEnabled(BOOL* v) override { *v = TRUE; return S_OK; }
        HRESULT STDMETHODCALLTYPE put_IsScriptEnabled(BOOL) override { return S_OK; }
        HRESULT STDMETHODCALLTYPE get_IsWebMessageEnabled(BOOL* v) override { *v = TRUE; return S_OK; }
        HRESULT STDMETHODCALLTYPE put_IsWebMessageEnabled(BOOL) override { return S_OK; }
        HRESULT STDMETHODCALLTYPE get_AreDefaultScriptDialogsEnabled(BOOL* v) override { *v = TRUE; return S_OK; }
        HRESULT STDMETHODCALLTYPE put_AreDefaultScriptDialogsEnabled(BOOL) override { return S_OK; }
        HRESULT STDMETHODCALLTYPE get_IsStatusBarEnabled(BOOL* v) override { *v = FALSE; return S_OK; }
        HRESULT STDMETHODCALLTYPE put_IsStatusBarEnabled(BOOL) override { return S_OK; }
        HRESULT STDMETHODCALLTYPE get_AreDevToolsEnabled(BOOL* v) override { *v = devtools; return S_OK; }
        HRESULT STDMETHODCALLTYPE put_AreDevToolsEnabled(BOOL v) override { devtools = v; return S_OK; }
        HRESULT STDMETHODCALLTYPE get_AreDefaultContextMenusEnabled(BOOL* v) override { *v = ctx_menu; return S_OK; }
        HRESULT STDMETHODCALLTYPE put_AreDefaultContextMenusEnabled(BOOL v) override { ctx_menu = v; return S_OK; }
        HRESULT STDMETHODCALLTYPE get_AreHostObjectsAllowed(BOOL* v) override { *v = TRUE; return S_OK; }
        HRESULT STDMETHODCALLTYPE put_AreHostObjectsAllowed(BOOL) override { return S_OK; }
        HRESULT STDMETHODCALLTYPE get_IsZoomControlEnabled(BOOL* v) override { *v = TRUE; return S_OK; }
        HRESULT STDMETHODCALLTYPE put_IsZoomControlEnabled(BOOL) override { return S_OK; }
        HRESULT STDMETHODCALLTYPE get_IsBuiltInErrorPageEnabled(BOOL* v) override { *v = TRUE; return S_OK; }
        HRESULT STDMETHODCALLTYPE put_IsBuiltInErrorPageEnabled(BOOL) override { return S_OK; }
        HRESULT STDMETHODCALLTYPE get_UserAgent(LPWSTR*) override { return S_OK; }
        HRESULT STDMETHODCALLTYPE put_UserAgent(LPCWSTR) override { return S_OK; }
        HRESULT STDMETHODCALLTYPE get_AreBrowserAcceleratorKeysEnabled(BOOL* v) override { *v = TRUE; return S_OK; }
        HRESULT STDMETHODCALLTYPE put_AreBrowserAcceleratorKeysEnabled(BOOL) override { return S_OK; }
        HRESULT STDMETHODCALLTYPE get_IsPasswordAutosaveEnabled(BOOL* v) override { *v = pw_autosave; return S_OK; }
        HRESULT STDMETHODCALLTYPE put_IsPasswordAutosaveEnabled(BOOL v) override { pw_autosave = v; return S_OK; }
        HRESULT STDMETHODCALLTYPE get_IsGeneralAutofillEnabled(BOOL* v) override { *v = general_autofill; return S_OK; }
        HRESULT STDMETHODCALLTYPE put_IsGeneralAutofillEnabled(BOOL v) override { general_autofill = v; return S_OK; }
    };

    auto* s4 = new NativeSettings4();
    cfire::webui::detail::apply_webview2_settings(s4, false, false, false);
    require(s4->devtools == FALSE && s4->ctx_menu == FALSE &&
                s4->pw_autosave == FALSE && s4->general_autofill == FALSE,
            "native Windows WebView2 apply_webview2_settings disables devtools, context menu, and autofill");
    cfire::webui::detail::apply_webview2_settings(s4, true, true, true);
    require(s4->devtools == TRUE && s4->ctx_menu == TRUE &&
                s4->pw_autosave == TRUE && s4->general_autofill == TRUE,
            "native Windows WebView2 apply_webview2_settings enables settings cleanly");
    s4->Release();

    HMODULE ole = ::GetModuleHandleW(L"ole32.dll");
    if (!ole) ole = ::LoadLibraryW(L"ole32.dll");
    using AllocFn = LPVOID(WINAPI*)(SIZE_T);
    auto co_alloc = ole ? reinterpret_cast<AllocFn>(reinterpret_cast<void*>(::GetProcAddress(ole, "CoTaskMemAlloc"))) : nullptr;
    require(co_alloc != nullptr, "ole32 CoTaskMemAlloc available on Windows");

    struct NativeWv2File final : ICoreWebView2File {
        std::atomic<ULONG> refs{1};
        AllocFn alloc_fn;
        std::wstring path;
        NativeWv2File(AllocFn a, std::wstring p) : alloc_fn(a), path(std::move(p)) {}
        HRESULT STDMETHODCALLTYPE QueryInterface(REFIID r, void** out) override {
            if (!out) return E_POINTER;
            if (IsEqualIID(r, kIidUnknown) ||
                IsEqualIID(r, cfire::webui::detail::wv2_iid(cfire::webui::detail::kIidFile))) {
                *out = static_cast<ICoreWebView2File*>(this);
                AddRef();
                return S_OK;
            }
            *out = nullptr;
            return E_NOINTERFACE;
        }
        ULONG STDMETHODCALLTYPE AddRef() override { return ++refs; }
        ULONG STDMETHODCALLTYPE Release() override { ULONG n = --refs; if (n == 0) delete this; return n; }
        HRESULT STDMETHODCALLTYPE get_Path(LPWSTR* out) override {
            if (!out || !alloc_fn) return E_POINTER;
            std::size_t bytes = (path.size() + 1) * sizeof(wchar_t);
            auto* mem = static_cast<wchar_t*>(alloc_fn(bytes));
            std::memcpy(mem, path.c_str(), bytes);
            *out = mem;
            return S_OK;
        }
    };
    struct NativeCollection final : ICoreWebView2ObjectCollectionView {
        std::atomic<ULONG> refs{1};
        NativeWv2File* item;
        explicit NativeCollection(NativeWv2File* it) : item(it) { item->AddRef(); }
        ~NativeCollection() { item->Release(); }
        HRESULT STDMETHODCALLTYPE QueryInterface(REFIID r, void** out) override {
            if (!out) return E_POINTER;
            if (IsEqualIID(r, kIidUnknown)) { *out = static_cast<IUnknown*>(this); AddRef(); return S_OK; }
            *out = nullptr;
            return E_NOINTERFACE;
        }
        ULONG STDMETHODCALLTYPE AddRef() override { return ++refs; }
        ULONG STDMETHODCALLTYPE Release() override { ULONG n = --refs; if (n == 0) delete this; return n; }
        HRESULT STDMETHODCALLTYPE get_Count(UINT* v) override { *v = 1; return S_OK; }
        HRESULT STDMETHODCALLTYPE GetValueAtIndex(UINT idx, IUnknown** out) override {
            if (!out || idx != 0) return E_POINTER;
            item->AddRef();
            *out = item;
            return S_OK;
        }
    };
    struct NativeMsgArgs2 final : ICoreWebView2WebMessageReceivedEventArgs2 {
        std::atomic<ULONG> refs{1};
        NativeCollection* coll;
        explicit NativeMsgArgs2(NativeCollection* c) : coll(c) { coll->AddRef(); }
        ~NativeMsgArgs2() { coll->Release(); }
        HRESULT STDMETHODCALLTYPE QueryInterface(REFIID r, void** out) override {
            if (!out) return E_POINTER;
            if (IsEqualIID(r, kIidUnknown) ||
                IsEqualIID(r, cfire::webui::detail::wv2_iid(cfire::webui::detail::kIidWebMessageReceivedArgs2))) {
                *out = static_cast<ICoreWebView2WebMessageReceivedEventArgs2*>(this);
                AddRef();
                return S_OK;
            }
            *out = nullptr;
            return E_NOINTERFACE;
        }
        ULONG STDMETHODCALLTYPE AddRef() override { return ++refs; }
        ULONG STDMETHODCALLTYPE Release() override { ULONG n = --refs; if (n == 0) delete this; return n; }
        HRESULT STDMETHODCALLTYPE get_Source(LPWSTR*) override { return S_OK; }
        HRESULT STDMETHODCALLTYPE get_WebMessageAsJson(LPWSTR*) override { return S_OK; }
        HRESULT STDMETHODCALLTYPE TryGetWebMessageAsString(LPWSTR*) override { return S_OK; }
        HRESULT STDMETHODCALLTYPE get_AdditionalObjects(ICoreWebView2ObjectCollectionView** out) override {
            coll->AddRef();
            *out = coll;
            return S_OK;
        }
    };

    auto* nf = new NativeWv2File(co_alloc, L"C:\\Users\\memo\\native_drop.pem");
    auto* nc = new NativeCollection(nf);
    auto* na = new NativeMsgArgs2(nc);
    auto got = cfire::webui::detail::extract_webview2_dropped_files(na);
    na->Release();
    nc->Release();
    nf->Release();
    require(got.size() == 1 && got[0] == "C:\\Users\\memo\\native_drop.pem",
            "native Windows extract_webview2_dropped_files recovers real OS path");
}
#endif // defined(_WIN32)

} // namespace test_webui_sec_detail

void test_webui_security_and_desktop(const std::filesystem::path& rt_dir) {
    std::println("=== RUN   test_webui_security_and_desktop");
    test_webui_sec_detail::verify_cli_release_and_debug_profiles(rt_dir);
    test_webui_sec_detail::verify_shared_bridge_helpers(rt_dir);
#if !defined(_WIN32)
    test_webui_sec_detail::verify_linux_and_win32_mocks(rt_dir);
#endif
#if defined(__APPLE__)
    test_webui_sec_detail::verify_macos_cocoa_and_webkit(rt_dir);
#endif
#if defined(_WIN32)
    test_webui_sec_detail::verify_native_win32_and_com();
#endif
    std::println("--- PASS: test_webui_security_and_desktop");
}

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
#include <vector>

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
struct GtkSettings {};
struct GInputStream { std::string data; };
struct WebKitURISchemeRequest { std::string path; std::string finished_body; std::string finished_mime; };
struct WebKitUserContentManager {};
struct WebKitUserScript {};
struct WebKitWebContext {};
struct WebKitSettings { gboolean dev_extras{FALSE}; };
struct WebKitContextMenu {};
struct WebKitHitTestResult {};
struct GdkEvent {};
struct GdkEventWindowState { guint changed_mask{0}; guint new_window_state{0}; };
struct GdkDragContext {};
struct GtkSelectionData { std::vector<std::string> uris; };
struct WebKitWebView { std::string last_eval_js; std::string loaded_uri; WebKitSettings settings; };
struct JSCValue { std::string str; };
struct WebKitJavascriptResult { JSCValue val; };
constexpr guint GDK_WINDOW_STATE_FULLSCREEN = 1U << 4;

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
inline void g_strfreev(char** v) {
    if (!v) return;
    for (int i = 0; v[i] != nullptr; ++i) std::free(v[i]);
    std::free(v);
}
inline void g_object_unref(gpointer p) { delete static_cast<GInputStream*>(p); }
inline void g_signal_stop_emission_by_name(gpointer, const char*) {}

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
inline WebKitSettings* webkit_web_view_get_settings(WebKitWebView* wv) { return wv ? &wv->settings : nullptr; }
inline void webkit_settings_set_enable_developer_extras(WebKitSettings* s, gboolean en) { if (s) s->dev_extras = en; }
inline void gtk_drag_dest_add_uri_targets(GtkWidget*) {}
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
    char* out = static_cast<char*>(std::malloc(sv.size() + 1));
    std::memcpy(out, sv.data(), sv.size());
    out[sv.size()] = '\0';
    return out;
}
inline void gtk_drag_finish(GdkDragContext*, gboolean, gboolean, guint) {}

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
inline GtkSettings* gtk_settings_get_default() {
    static GtkSettings s_settings{};
    return &s_settings;
}
inline void g_object_set(gpointer, const char*, ...) {}
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

    // 7b. Verify Windows Win32 + WebView2 Backend Compilation, Clipboard, Dialogs, Tray & WndProc via Mock Win32 Headers
    {
        std::string_view win32_mock_cpp = R"CPP(
#define CFIRE_TEST_WIN32_BACKEND 1
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <map>
#include <string>
#include <vector>

using BOOL = int;
using UINT = unsigned int;
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

#ifndef CALLBACK
#define CALLBACK
#endif
#ifndef FALSE
#define FALSE 0
#endif
#ifndef TRUE
#define TRUE 1
#endif

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
struct MINMAXINFO {
    POINT ptReserved;
    POINT ptMaxSize;
    POINT ptMaxPosition;
    POINT ptMinTrackSize;
    POINT ptMaxTrackSize;
};
struct CREATESTRUCTW { void* lpCreateParams; };
struct OPENFILENAMEW {
    DWORD lStructSize;
    HWND hwndOwner;
    wchar_t* lpstrFile;
    DWORD nMaxFile;
    const wchar_t* lpstrFilter;
    DWORD nFilterIndex;
    const wchar_t* lpstrTitle;
    DWORD Flags;
};
struct LOGFONTW {
    unsigned char lfCharSet;
    unsigned char lfPitchAndFamily;
    wchar_t lfFaceName[32];
};
struct TEXTMETRICW { int dummy; };
using WNDPROC = LRESULT(*)(HWND, UINT, WPARAM, LPARAM);
struct WNDCLASSEXW {
    UINT cbSize;
    UINT style;
    WNDPROC lpfnWndProc;
    HINSTANCE hInstance;
    HICON hIcon;
    HCURSOR hCursor;
    HBRUSH hbrBackground;
    const wchar_t* lpszClassName;
    HICON hIconSm;
};
struct NOTIFYICONDATAW {
    DWORD cbSize;
    HWND hWnd;
    UINT uID;
    UINT uFlags;
    UINT uCallbackMessage;
    HICON hIcon;
    wchar_t szTip[128];
};
struct MSG {
    HWND hwnd;
    UINT message;
    WPARAM wParam;
    LPARAM lParam;
};

static std::map<UINT, std::vector<unsigned char>> g_clip_data;
static std::map<std::wstring, UINT> g_clip_formats;
static UINT g_next_fmt = 0xC000;
static LONG_PTR g_wnd_userdata = 0;
static WNDPROC g_registered_wndproc = nullptr;
static int g_dummy_hwnd_obj = 1;
static int g_tray_add_calls = 0;
static int g_tray_del_calls = 0;
static int g_last_show_cmd = -1;
static std::wstring g_last_msgbox_title;
static std::wstring g_last_msgbox_text;
static std::vector<std::wstring> g_popup_menu_items;

inline int MultiByteToWideChar(UINT, DWORD, const char* src, int srclen, wchar_t* dst, int dstlen) {
    if (!src || srclen <= 0) return 0;
    if (!dst || dstlen == 0) return srclen;
    for (int i = 0; i < srclen && i < dstlen; ++i) {
        dst[i] = static_cast<wchar_t>(static_cast<unsigned char>(src[i]));
    }
    return srclen;
}

inline int WideCharToMultiByte(UINT, DWORD, const wchar_t* src, int srclen, char* dst, int dstlen, const char*, BOOL*) {
    if (!src || srclen <= 0) return 0;
    if (!dst || dstlen == 0) return srclen;
    for (int i = 0; i < srclen && i < dstlen; ++i) {
        dst[i] = static_cast<char>(src[i] & 0xFF);
    }
    return srclen;
}

inline HWND GetActiveWindow() { return &g_dummy_hwnd_obj; }
inline BOOL OpenClipboard(HWND) { return TRUE; }
inline BOOL EmptyClipboard() { g_clip_data.clear(); return TRUE; }
inline BOOL CloseClipboard() { return TRUE; }
inline HGLOBAL GlobalAlloc(UINT, std::size_t bytes) {
    auto* v = new std::vector<unsigned char>(bytes, 0);
    return v;
}
inline void* GlobalLock(HGLOBAL h) {
    return h ? static_cast<std::vector<unsigned char>*>(h)->data() : nullptr;
}
inline BOOL GlobalUnlock(HGLOBAL) { return TRUE; }
inline HGLOBAL GlobalFree(HGLOBAL h) {
    delete static_cast<std::vector<unsigned char>*>(h);
    return nullptr;
}
inline HANDLE SetClipboardData(UINT fmt, HANDLE hMem) {
    if (hMem) {
        auto* v = static_cast<std::vector<unsigned char>*>(hMem);
        g_clip_data[fmt] = *v;
        delete v;
    }
    return hMem;
}
inline HANDLE GetClipboardData(UINT fmt) {
    auto it = g_clip_data.find(fmt);
    if (it == g_clip_data.end()) return nullptr;
    static std::vector<unsigned char> s_view_buf;
    s_view_buf = it->second;
    return &s_view_buf;
}
inline UINT RegisterClipboardFormatW(const wchar_t* name) {
    std::wstring k(name ? name : L"");
    auto it = g_clip_formats.find(k);
    if (it != g_clip_formats.end()) return it->second;
    UINT id = ++g_next_fmt;
    g_clip_formats[k] = id;
    return id;
}

inline BOOL GetOpenFileNameW(OPENFILENAMEW* ofn) {
    if (!ofn || !ofn->lpstrFile) return FALSE;
    std::wcsncpy(ofn->lpstrFile, L"C:\\Users\\memo\\vault.kdbx", ofn->nMaxFile - 1);
    return TRUE;
}
inline BOOL GetSaveFileNameW(OPENFILENAMEW* ofn) {
    if (!ofn || !ofn->lpstrFile) return FALSE;
    return TRUE;
}
inline int MessageBoxW(HWND, const wchar_t* text, const wchar_t* caption, UINT) {
    g_last_msgbox_text = text ? text : L"";
    g_last_msgbox_title = caption ? caption : L"";
    return 1;
}

inline HDC GetDC(HWND) { return &g_dummy_hwnd_obj; }
inline int ReleaseDC(HWND, HDC) { return 1; }
using FONTENUMPROCW = int(*)(const LOGFONTW*, const TEXTMETRICW*, DWORD, LPARAM);
inline int EnumFontFamiliesExW(HDC, LOGFONTW*, FONTENUMPROCW cb, LPARAM lParam, DWORD) {
    LOGFONTW lf{};
    lf.lfPitchAndFamily = FIXED_PITCH;
    std::wcsncpy(lf.lfFaceName, L"Cascadia Mono", 31);
    TEXTMETRICW tm{};
    if (cb) cb(&lf, &tm, TRUETYPE_FONTTYPE, lParam);
    return 1;
}

inline LONG GetWindowLongW(HWND, int) { return static_cast<LONG>(WS_OVERLAPPEDWINDOW); }
inline LONG SetWindowLongW(HWND, int, LONG v) { return v; }
inline LONG_PTR GetWindowLongPtrW(HWND, int) { return g_wnd_userdata; }
inline LONG_PTR SetWindowLongPtrW(HWND, int, LONG_PTR v) {
    LONG_PTR old = g_wnd_userdata;
    g_wnd_userdata = v;
    return old;
}
inline BOOL GetClientRect(HWND, RECT* r) {
    if (r) *r = RECT{0, 0, 960, 640};
    return TRUE;
}
inline BOOL GetWindowRect(HWND, RECT* r) {
    if (r) *r = RECT{100, 100, 1060, 740};
    return TRUE;
}
inline int GetSystemMetrics(int idx) {
    return idx == SM_CXSCREEN ? 1920 : 1080;
}
inline BOOL ShowWindow(HWND, int cmd) { g_last_show_cmd = cmd; return TRUE; }
inline BOOL DestroyWindow(HWND hwnd) {
    if (g_registered_wndproc) g_registered_wndproc(hwnd, WM_DESTROY, 0, 0);
    return TRUE;
}
inline HMENU CreatePopupMenu() {
    g_popup_menu_items.clear();
    return &g_dummy_hwnd_obj;
}
inline BOOL AppendMenuW(HMENU, UINT flags, UINT_PTR, const wchar_t* item) {
    if (flags == MF_SEPARATOR) g_popup_menu_items.emplace_back(L"-");
    else if (item) g_popup_menu_items.emplace_back(item);
    return TRUE;
}
inline BOOL GetCursorPos(POINT* pt) { if (pt) *pt = POINT{100, 100}; return TRUE; }
inline BOOL SetForegroundWindow(HWND) { return TRUE; }
inline BOOL TrackPopupMenu(HMENU, UINT, int, int, int, HWND, const RECT*) {
    // Simulate selecting item 1 ("Show Flint")
    return 1;
}
inline BOOL DestroyMenu(HMENU) { return TRUE; }
inline void PostQuitMessage(int) {}
inline LRESULT DefWindowProcW(HWND, UINT, WPARAM, LPARAM) { return 0; }

inline HRESULT CoInitializeEx(void*, DWORD) { return 0; }
inline void CoUninitialize() {}
inline void* GetProcAddress(HMODULE, const char*) { return nullptr; }
inline int MulDiv(int nNumber, int nNumerator, int nDenominator) {
    return nDenominator == 0 ? 0 : static_cast<int>((static_cast<long long>(nNumber) * nNumerator) / nDenominator);
}
inline void DragAcceptFiles(HWND, BOOL) {}
inline UINT DragQueryFileW(HDROP, UINT, wchar_t*, UINT) { return 0; }
inline void DragFinish(HDROP) {}
inline HINSTANCE GetModuleHandleW(const wchar_t*) { return &g_dummy_hwnd_obj; }
inline HCURSOR LoadCursorW(HINSTANCE, const wchar_t*) { return &g_dummy_hwnd_obj; }
inline HBRUSH CreateSolidBrush(DWORD) { return &g_dummy_hwnd_obj; }
inline UINT RegisterClassExW(const WNDCLASSEXW* wc) {
    if (wc) g_registered_wndproc = wc->lpfnWndProc;
    return 1;
}
inline HWND CreateWindowExW(DWORD, const wchar_t*, const wchar_t*, DWORD, int, int, int, int, HWND, HMENU, HINSTANCE, void* lpParam) {
    HWND hwnd = &g_dummy_hwnd_obj;
    if (g_registered_wndproc) {
        CREATESTRUCTW cs{lpParam};
        g_registered_wndproc(hwnd, WM_CREATE, 0, reinterpret_cast<LPARAM>(&cs));
    }
    return hwnd;
}
inline HRESULT DwmSetWindowAttribute(HWND, DWORD, const void*, DWORD) { return 0; }
inline HANDLE LoadImageW(HINSTANCE, const wchar_t*, UINT, int, int, UINT) { return &g_dummy_hwnd_obj; }
inline HICON LoadIconW(HINSTANCE, const wchar_t*) { return &g_dummy_hwnd_obj; }
inline LRESULT SendMessageW(HWND, UINT, WPARAM, LPARAM) { return 0; }
inline BOOL Shell_NotifyIconW(DWORD msg, NOTIFYICONDATAW*) {
    if (msg == NIM_ADD) ++g_tray_add_calls;
    if (msg == NIM_DELETE) ++g_tray_del_calls;
    return TRUE;
}
inline BOOL SetWindowTextW(HWND, const wchar_t*) { return TRUE; }
inline BOOL SetWindowPos(HWND, HWND, int, int, int, int, UINT) { return TRUE; }
inline BOOL ReleaseCapture() { return TRUE; }
inline BOOL PostMessageW(HWND hwnd, UINT msg, WPARAM w, LPARAM l) {
    if (g_registered_wndproc) g_registered_wndproc(hwnd, msg, w, l);
    return TRUE;
}
inline BOOL UpdateWindow(HWND) { return TRUE; }
static int g_peek_step = 0;
inline BOOL PeekMessageW(MSG* msg, HWND, UINT, UINT, UINT) {
    if (!msg) return FALSE;
    ++g_peek_step;
    if (g_peek_step == 1) {
        // Step 1: simulate user clicking window close button while hide_on_close is true
        if (g_registered_wndproc) g_registered_wndproc(&g_dummy_hwnd_obj, WM_CLOSE, 0, 0);
        return FALSE;
    }
    if (g_peek_step == 2) {
        // Step 2: simulate user right-clicking system tray icon (selects item 1)
        if (g_registered_wndproc) g_registered_wndproc(&g_dummy_hwnd_obj, WM_APP + 1, 0, WM_RBUTTONUP);
        return FALSE;
    }
    if (g_peek_step == 3) {
        msg->hwnd = &g_dummy_hwnd_obj;
        msg->message = WM_QUIT;
        return TRUE;
    }
    return FALSE;
}
inline BOOL GetMessageW(MSG*, HWND, UINT, UINT) { return 0; }
inline BOOL TranslateMessage(const MSG*) { return TRUE; }
inline LRESULT DispatchMessageW(const MSG*) { return 0; }

#include "core/webui.hpp"

int main() {
    // 1. UTF-8 <-> Wide round-trip
    auto w = cfire::webui::detail::utf8_to_wide("Flint Win32");
    if (cfire::webui::detail::wide_to_utf8(w) != "Flint Win32") return 2;

    // 2. Concealed clipboard & format tags
    cfire::webui::detail::win32_clipboard_set("top-secret-pw", true);
    if (cfire::webui::detail::win32_clipboard_get() != "top-secret-pw") return 3;
    if (!g_clip_formats.contains(L"ExcludeClipboardContentFromMonitorProcessing")) return 4;
    if (!g_clip_formats.contains(L"CanIncludeInClipboardHistory")) return 5;

    // 3. File & message dialogs + font enumeration
    if (cfire::webui::detail::win32_open_file_dialog("Open Vault") != "C:\\Users\\memo\\vault.kdbx") return 6;
    if (cfire::webui::detail::win32_save_file_dialog("Save Vault", "backup.kdbx") != "backup.kdbx") return 7;
    cfire::webui::detail::win32_message_dialog("Notice", "Saved!");
    if (g_last_msgbox_title != L"Notice" || g_last_msgbox_text != L"Saved!") return 8;
    auto fonts = cfire::webui::detail::win32_monospace_fonts();
    if (fonts.empty() || fonts[0] != "Cascadia Mono") return 9;

    // 4. Full Win32 event loop, tray menu dispatch, and hide_on_close behavior
    auto app = cfire::webui::new_app("Flint Win32", 960, 640);
    app.set_hide_on_close(true);
    app.set_tray("", "Flint SSH");
    app.add_tray_item("Show Flint", "tray:show");
    app.add_tray_item("-", "");
    app.add_tray_item("Quit", "tray:quit");

    int close_req_count = 0;
    int tray_show_count = 0;
    app.on("window:close_requested", [&]() { ++close_req_count; });
    app.on("tray:show", [&]() { ++tray_show_count; });
    app.emit("ping", "pong");
    if (app.last_emitted_payload("ping") != "\"pong\"") return 13;

    app.run_win32_webview2(true, 100);
    app.set_dark_mode(false);
    app.center();
    app.fullscreen();
    app.fullscreen();

    if (g_tray_add_calls != 1 || g_tray_del_calls != 1) return 10;
    if (close_req_count != 1 || !app.is_window_hidden() || g_last_show_cmd != SW_HIDE) return 11;
    if (tray_show_count != 1 || g_popup_menu_items.size() != 3) return 12;
    return 0;
}
)CPP";
        auto win32_res = compile_and_run_cpp(win32_mock_cpp, rt_dir, "webui_win32_mock");
        win32_res.assert_ok("Windows Win32 + WebView2 backend mock-header compilation and clipboard/dialog/tray/WndProc verification");
    }

    // 4. Verify Upstream Stdlib & Extension Additions
    {
        // ext:crypto/sodium + PBKDF2 + AES auto-nonce encrypt/decrypt
#if defined(CFIRE_SODIUM_NATIVE)
        auto salt = cfire::sodium::random_bytes(16);
        require(salt.size() == 16, "sodium::random_bytes(16)");
        auto kdf_key = cfire::sodium::argon2id("flint-master-password", salt, 32);
        require(kdf_key.has_value() && kdf_key->size() == 32, "sodium::argon2id derived 32-byte key");

        // Verify byte-identical Argon2id contract vs direct ::crypto_pwhash (64 MiB default, 8 MiB, 256 MiB)
        for (std::size_t mem_bytes : {std::size_t{64ULL * 1024ULL * 1024ULL}, std::size_t{8ULL * 1024ULL * 1024ULL}, std::size_t{256ULL * 1024ULL * 1024ULL}}) {
            std::vector<std::uint8_t> expected_key(32);
            require(::crypto_pwhash(expected_key.data(), 32,
                                    "flint-master-password", 21,
                                    salt.data(), 2, mem_bytes,
                                    crypto_pwhash_ALG_ARGON2ID13) == 0,
                    "direct crypto_pwhash reference succeeds");
            auto got_key = cfire::sodium::argon2id("flint-master-password", salt, 32, 2, mem_bytes);
            require(got_key.has_value() && *got_key == expected_key,
                    std::format("sodium::argon2id matches crypto_pwhash byte-for-byte at {} bytes", mem_bytes));
        }
        {
            std::vector<std::uint8_t> expected_default(32);
            require(::crypto_pwhash(expected_default.data(), 32,
                                    "flint-master-password", 21,
                                    salt.data(), 2, 67108864ULL,
                                    crypto_pwhash_ALG_ARGON2ID13) == 0,
                    "direct crypto_pwhash default 64 MiB reference");
            require(*kdf_key == expected_default, "sodium::argon2id default equals 64 MiB (67108864 bytes)");
        }
        require(!cfire::sodium::argon2id("flint-master-password", salt, 32, 2, 4096).has_value(),
                "sodium::argon2id rejects memlimit < MEMLIMIT_MIN without clamping or guessing");
        require(!cfire::sodium::argon2id("flint-master-password", salt, 32, 0, 67108864).has_value(),
                "sodium::argon2id rejects opslimit < OPSLIMIT_MIN");

        auto phc = cfire::sodium::argon2id_str("flint-master-password");
        require(phc.has_value() && phc->contains("$m=65536,t=2,p=") &&
                cfire::sodium::argon2id_verify(*phc, "flint-master-password"),
            "sodium::argon2id_str reports 64 MiB (m=65536) and verifies");
        require(!cfire::sodium::argon2id_verify(*phc, "wrong-password"),
            "sodium::argon2id_verify rejects wrong password");
        require(!cfire::sodium::argon2id_str("flint-master-password", 2, 4096).has_value(),
                "sodium::argon2id_str rejects memlimit < MEMLIMIT_MIN");

        auto plain_sod = cfire::to_bytes("ssh-ed25519 AAAAC3NzaC1lZDI1NTE5AAAAI");
        auto xsealed = cfire::sodium::xchacha20poly1305_encrypt(*kdf_key, plain_sod);
        require(xsealed.has_value() && xsealed->size() == plain_sod.size() + 40,
            "sodium::xchacha20poly1305_encrypt output size");
        auto xopened = cfire::sodium::xchacha20poly1305_decrypt(*kdf_key, *xsealed);
        require(xopened.has_value() && str(*xopened) == "ssh-ed25519 AAAAC3NzaC1lZDI1NTE5AAAAI",
            "sodium::xchacha20poly1305_decrypt round-trip");
        auto b2 = cfire::sodium::blake2b(plain_sod, 32);
        require(b2.has_value() && b2->size() == 32, "sodium::blake2b(32)");
        auto subk = cfire::sodium::derive_subkey(*kdf_key, 1, "flintvlt", 32);
        require(subk.has_value() && subk->size() == 32, "sodium::derive_subkey");
        cfire::sodium::memzero(*kdf_key);
#endif
        auto plain = cfire::to_bytes("ssh-ed25519 AAAAC3NzaC1lZDI1NTE5AAAAI");

        auto aes_key = cfire::aes::generate_key();
        auto aes_blob = cfire::aes::encrypt(aes_key, plain);
        require(aes_blob.has_value(), "aes::encrypt auto-nonce");
        auto aes_pt = cfire::aes::decrypt(aes_key, *aes_blob);
        require(aes_pt.has_value() && str(*aes_pt) == "ssh-ed25519 AAAAC3NzaC1lZDI1NTE5AAAAI",
            "aes::decrypt auto-nonce round-trip");

        auto pbkdf2_key = cfire::hmac::pbkdf2_sha256("password", "salt", 1000, 32);
        require(pbkdf2_key.size() == 32, "hmac::pbkdf2_sha256");

        std::vector<std::uint8_t> raw_blob{std::uint8_t{0xDE}, std::uint8_t{0xAD}, std::uint8_t{0xBE}, std::uint8_t{0xEF}};

        // ext:keychain in headless mode
        test_set_env("CFIRE_HEADLESS_TEST", "1");
        require(cfire::keychain::set("dev.flint.test", "acct1", "s3cr3t").has_value(), "keychain::set");
        auto kc_val = cfire::keychain::get("dev.flint.test", "acct1");
        require(kc_val.has_value() && *kc_val == "s3cr3t", "keychain::get");
        require(cfire::keychain::delete_password("dev.flint.test", "acct1").has_value(), "keychain::delete_password");

        // byte == u8 comparisons and str::parse_*
        std::uint8_t b{0x0A};
        require(b == 0x0A && b == static_cast<std::uint8_t>(10), "std::uint8_t == integral comparison");
        require(cfire::str::parse_i64(" -98765 ").value_or(0) == -98765, "str::parse_i64");
        require(cfire::str::parse_f64("3.5").value_or(0.0) == 3.5, "str::parse_f64");
        require(cfire::str::parse_bool("true").value_or(false) == true, "str::parse_bool");
        require(!cfire::str::parse_bool("yes").has_value(), "str::parse_bool strictly rejects YAML slang");

        // sync::Mutex, try_recv, recv_timeout
        cfire::sync::Mutex mtx;
        int counter = 0;
        mtx.with_lock([&]() { counter += 10; });
        require(counter == 10, "sync::Mutex::with_lock");
        auto ch = cfire::sync::make_channel<int>(4);
        require(!ch.try_recv().has_value(), "try_recv on empty channel returns nullopt");
        ch.send(77);
        require(ch.len() == 1, "ch.len() == 1");
        require(ch.try_recv().value_or(0) == 77, "try_recv returns queued item");
        require(!ch.recv_timeout(5).has_value(), "recv_timeout(5ms) on empty channel times out cleanly");

        // net::socketpair, os::pipe, os::stdin/stdout/stderr RAII handles, and fail-closed net::connect_tcp
        auto sp = cfire::net::socketpair();
        require(sp.has_value(), "net::socketpair()");
        auto& [sp_a, sp_b] = *sp;
        require(sp_a.write_text("X\n").value_or(0) == 2, "TcpConn::write_text on socketpair");
        require(sp_b.poll(50), "TcpConn::poll detects readable socketpair");
        auto sp_line = sp_b.read_line();
        require(sp_line.has_value() && *sp_line == "X", "TcpConn::read_line on socketpair");
        sp_a.close();
        sp_b.close();
        require(!sp_a.is_open() && !sp_b.is_open(), "socketpair closed");
        require(!sp_a.read_bytes(16).has_value(), "reading from closed TcpConn returns error");
        require(!cfire::net::connect_tcp("127.0.0.1", 59999).has_value(),
            "net::connect_tcp to non-listening loopback port fails closed with error");

        auto pp = cfire::os::pipe();
        require(pp.has_value(), "os::pipe() returns (fs::File, fs::File)");
        auto& [pipe_r, pipe_w] = *pp;
        require(pipe_w.write_text("pipe-payload").value_or(0) == 12, "pipe writer write_text");
        pipe_w.close();
        std::vector<std::uint8_t> pipe_buf(32);
        auto pipe_n = pipe_r.read_into(pipe_buf);
        require(pipe_n.has_value() && *pipe_n == 12, "pipe reader read_into");
        pipe_buf.resize(*pipe_n);
        require(cfire::to_str(pipe_buf) == "pipe-payload", "pipe payload round-trip");
        pipe_r.close();

        {
            auto out_f = cfire::os::stdout_file();
            auto err_f = cfire::os::stderr_file();
            require(out_f.fd() == 1 && err_f.fd() == 2, "os::stdout_file/stderr_file non-owning FDs 1 and 2");
        }

        // webui enhancements
        ScopedTempDir ui_tmp("cfire_webui_serve_dir");
        ui_tmp.file("index.html", "<!doctype html><html><body>Custom ServeDir Index</body></html>");
        auto app = cfire::webui::new_app("Flint", 900, 600);
        app.serve_dir(ui_tmp.path.string());
        require(app.resolve_asset_content("/index.html").contains("Custom ServeDir Index"),
            "serve_dir(/index.html) takes priority over default placeholder without needing set_html");

        app.set_max_history(4);
        for (int i = 0; i < 10; ++i) {
            app.emit("term:data", i);
        }
        require(app.emitted_events_count("term:data") == 4, "emitted_history capped at max_history_entries");
        app.clear_history();
        require(app.emitted_events_count("term:data") == 0, "clear_history clears emitted_history");

        app.emit_bytes("pty:out", raw_blob);
        require(app.emitted_events_count("pty:out") == 1 && app.last_emitted_payload("pty:out") == "3q2+7w==",
            "emit_bytes Base64 encodes binary frame for Uint8Array dispatch");

        app.bind_action_async("slow_op", []() -> std::string {
            return "async_done";
        });
        auto async_res = app.call_rpc("slow_op", "{}");
        require(async_res.has_value() && *async_res == "\"async_done\"", "bind_action_async resolves");

        app.clipboard_set("ssh root@10.0.0.1");
        require(app.clipboard_get() == "ssh root@10.0.0.1", "clipboard_set / clipboard_get");
        require(!app.is_clipboard_concealed(), "normal clipboard_set is not concealed");

        // concealed & auto-clearing secret clipboard
        app.clipboard_set_secret("super-secret-password", 35);
        require(app.clipboard_get() == "super-secret-password", "clipboard_set_secret stores secret initially");
        require(app.is_clipboard_concealed(), "clipboard_set_secret marks clipboard as concealed");
        for (int i = 0; i < 150 && !app.clipboard_get().empty(); ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
        require(app.clipboard_get().empty(), "clipboard_set_secret auto-clears after timeout when unchanged");

        // overwriting clipboard before timeout cancels stale auto-clear
        app.clipboard_set_secret("transient-secret", 50);
        app.clipboard_set("user-copied-later");
        std::this_thread::sleep_for(std::chrono::milliseconds(120));
        require(app.clipboard_get() == "user-copied-later", "auto-clear does not wipe newer clipboard content");

        // set_icon, add_menu_item, system tray, and window visibility controls
        app.set_icon("assets/flint-icon.svg");
        app.add_menu_item("Shell", "New Tab", "t", "menu:new_tab");
        app.set_tray("assets/flint-tray.png", "Flint SSH");
        app.add_tray_item("Quick Connect", "tray:connect");
        app.set_hide_on_close(true);
        app.hide_window();
        app.show_window();
        require(!app.monospace_fonts().empty(), "monospace_fonts returns non-empty list");
        require(cfire::os::getpid() > 0, "os::getpid() > 0");

        // ext:net/ssh (dial, host_key_fingerprint, known_hosts, auth_publickey_mem, exec, PTY, SFTP, forward)
        {
            cfire::ssh::enable_mock_server(true);
            auto sess_res = cfire::ssh::dial("127.0.0.1", 2222, "deploy", 2000);
            require(sess_res.has_value(), "ssh::dial succeeds in mock/loopback mode");
            if (sess_res) {
                auto& sess = *sess_res;
                std::string fp = sess.host_key_fingerprint();
                require(fp.starts_with("SHA256:"), "host_key_fingerprint returns SHA256:<base64>");
                require(!sess.is_known_host(), "new host starts unknown");
                require(sess.write_known_host().has_value(), "write_known_host()");
                require(sess.is_known_host(), "is_known_host() true after write_known_host()");

                std::string pem = "-----BEGIN OPENSSH PRIVATE KEY-----\nmock-ed25519-key\n-----END OPENSSH PRIVATE KEY-----";
                require(sess.auth_publickey_mem(pem).has_value(), "auth_publickey_mem succeeds from memory without temp file");

                auto ex = sess.exec("echo hello-ssh");
                require(ex.has_value() && ex->exit_code == 0 && ex->stdout_cf_.contains("hello-ssh"),
                    "sess.exec returns ExecResult with stdout");

                auto pty_res = sess.open_pty("xterm-256color", 120, 40);
                require(pty_res.has_value(), "sess.open_pty succeeds");
                if (pty_res) {
                    auto& pty = *pty_res;
                    auto wn = pty.write_text("uptime\n");
                    require(wn.has_value() && *wn == 7, "pty.write_text");
                    require(pty.resize_pty(160, 50).has_value(), "pty.resize_pty");
                    auto out = pty.read_text(1024);
                    require(out.has_value() && out->contains("uptime") && out->contains("50;160t"),
                        "pty.read_text reflects interactive stream and resize");
                    pty.close();
                    require(pty.is_eof(), "pty.is_eof after close");
                }

                require(sess.sftp_mkdir("/remote/etc").has_value(), "sftp_mkdir");
                require(sess.sftp_write_text("/remote/etc/app.conf", "port=8080\n").has_value(), "sftp_write_text");
                auto sftp_content = sess.sftp_read_text("/remote/etc/app.conf");
                require(sftp_content.has_value() && *sftp_content == "port=8080\n", "sftp_read_text round-trip");
                auto dir_entries = sess.sftp_list_dir("/remote/etc");
                require(dir_entries.has_value() && !dir_entries->empty() && (*dir_entries)[0].name == "app.conf",
                    "sftp_list_dir");
                require(sess.sftp_remove("/remote/etc/app.conf").has_value(), "sftp_remove");

                auto fwd_res = sess.open_forward("127.0.0.1", 5432);
                require(fwd_res.has_value(), "open_forward");
                sess.close();
            }
            cfire::ssh::enable_mock_server(false);
        }

        // UPSTREAM-02 Item 8 & 9: Domain-distinct I/O (`fs::File`, `os` FDs) and `cli::FlagReader` validation
        {
            ScopedTempDir stream_tmp("cfire_unified_stream_test");
            auto spath = stream_tmp.path / "lines.txt";
            {
                auto wf = cfire::fs::open(spath, cfire::fs::OpenMode::Write);
                require(wf.has_value(), "UPSTREAM-02: fs::open for write");
                require(wf->fd() >= 0, "UPSTREAM-02: File::fd() >= 0 while open");
                require(!wf->is_tty(), "UPSTREAM-02: regular file fd is not a tty");
                require(wf->write_text("first line\n").has_value(), "UPSTREAM-02: File::write_text(1)");
                require(wf->write_text("second line\n").has_value(), "UPSTREAM-02: File::write_text(2)");
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

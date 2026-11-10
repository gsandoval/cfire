#pragma once

#include "../../cfire_prelude.hpp"
#include "../fs.hpp"
#include "../json.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <expected>
#include <filesystem>
#include <functional>
#include <future>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

#if defined(__APPLE__)
#include <TargetConditionals.h>
#include <mach-o/dyld.h>
#if defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE
#define CFIRE_WEBUI_IOS 1
#else
#define CFIRE_WEBUI_IOS 0
#endif
#if defined(__OBJC__)
#import <Foundation/Foundation.h>
#import <WebKit/WebKit.h>
#if CFIRE_WEBUI_IOS
#import <UIKit/UIKit.h>
#else
#import <Cocoa/Cocoa.h>
#endif
#endif
#elif defined(__linux__)
#include <unistd.h>
#if __has_include(<webkit2/webkit2.h>) && __has_include(<gtk/gtk.h>) && !defined(CFIRE_TEST_LINUX_WEBKITGTK_BACKEND)
#define CFIRE_HAS_LINUX_WEBKITGTK 1
#include <gtk/gtk.h>
#include <webkit2/webkit2.h>
#include <jsc/jsc.h>
#endif
#elif defined(_WIN32) && !defined(CFIRE_TEST_WIN32_BACKEND)
#ifndef NOMINMAX
#define NOMINMAX 1
#endif
#include <windows.h>
#include <commdlg.h>
#include <dwmapi.h>
#include <objbase.h>
#include <shellapi.h>
#endif

#if defined(CFIRE_TEST_LINUX_WEBKITGTK_BACKEND)
#define CFIRE_HAS_LINUX_WEBKITGTK 1
#endif

#if defined(_WIN32) || defined(CFIRE_TEST_WIN32_BACKEND)
#define CFIRE_HAS_WIN32_BACKEND 1
#endif

namespace cfire::webui {

namespace detail {

template <typename T>
struct is_expected : std::false_type {};

template <typename V, typename E>
struct is_expected<std::expected<V, E>> : std::true_type {};

[[nodiscard]] inline auto normalize_asset_path(std::string_view raw) -> std::string {
    std::string p(raw);
    if (auto q = p.find('?'); q != std::string::npos) p.erase(q);
    if (auto h = p.find('#'); h != std::string::npos) p.erase(h);
    while (!p.empty() && p.front() == '/') p.erase(p.begin());
    if (p.empty()) return "/index.html";
    return "/" + p;
}

[[nodiscard]] inline auto has_path_traversal(std::string_view p) noexcept -> bool {
    if (p.find("..") != std::string_view::npos) return true;
    if (p.find('\\') != std::string_view::npos) return true;
    return false;
}

[[nodiscard]] inline auto guess_mime_type(std::string_view path) -> std::string {
    auto dot = path.rfind('.');
    if (dot == std::string_view::npos) return "application/octet-stream";
    std::string ext(path.substr(dot));
    for (char& c : ext) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    if (ext == ".html" || ext == ".htm") return "text/html; charset=utf-8";
    if (ext == ".css") return "text/css; charset=utf-8";
    if (ext == ".js" || ext == ".mjs") return "text/javascript; charset=utf-8";
    if (ext == ".json" || ext == ".map") return "application/json; charset=utf-8";
    if (ext == ".svg") return "image/svg+xml";
    if (ext == ".png") return "image/png";
    if (ext == ".jpg" || ext == ".jpeg") return "image/jpeg";
    if (ext == ".webp") return "image/webp";
    if (ext == ".gif") return "image/gif";
    if (ext == ".ico") return "image/x-icon";
    if (ext == ".woff2") return "font/woff2";
    if (ext == ".woff") return "font/woff";
    if (ext == ".ttf") return "font/ttf";
    if (ext == ".otf") return "font/otf";
    if (ext == ".wasm") return "application/wasm";
    if (ext == ".wav") return "audio/wav";
    if (ext == ".mp3") return "audio/mpeg";
    if (ext == ".ogg") return "audio/ogg";
    if (ext == ".txt" || ext == ".md") return "text/plain; charset=utf-8";
    return "application/octet-stream";
}

[[nodiscard]] inline auto executable_dir() -> std::filesystem::path {
#if defined(__APPLE__)
    uint32_t size = 0;
    _NSGetExecutablePath(nullptr, &size);
    if (size > 0) {
        std::string buf(size, '\0');
        if (_NSGetExecutablePath(buf.data(), &size) == 0) {
            std::error_code ec;
            auto p = std::filesystem::weakly_canonical(std::filesystem::path(buf.c_str()), ec);
            if (!ec) return p.parent_path();
            return std::filesystem::path(buf.c_str()).parent_path();
        }
    }
#elif defined(__linux__)
    char buf[4096];
    ssize_t len = ::readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (len > 0) {
        buf[len] = '\0';
        return std::filesystem::path(buf).parent_path();
    }
#elif defined(_WIN32)
    wchar_t wbuf[MAX_PATH];
    DWORD len = ::GetModuleFileNameW(nullptr, wbuf, MAX_PATH);
    if (len > 0) {
        return std::filesystem::path(wbuf).parent_path();
    }
#endif
    std::error_code ec;
    auto cwd = std::filesystem::current_path(ec);
    return ec ? std::filesystem::path(".") : cwd;
}

inline constexpr std::string_view kPreloadBridgeJs = R"JS(
(function() {
  if (window.__cfire_webui_initialized) return;
  window.__cfire_webui_initialized = true;

  const pending = new Map();
  const listeners = new Map();
  let seq = 1;

  function postRaw(obj) {
    const msg = JSON.stringify(obj);
    if (window.webkit && window.webkit.messageHandlers && window.webkit.messageHandlers.external) {
      window.webkit.messageHandlers.external.postMessage(msg);
    } else if (window.chrome && window.chrome.webview && typeof window.chrome.webview.postMessage === "function") {
      window.chrome.webview.postMessage(msg);
    } else if (window.external && typeof window.external.invoke === "function") {
      window.external.invoke(msg);
    }
  }

  window.__cfire_rpc_resolve = function(id, ok, payload) {
    const entry = pending.get(id);
    if (!entry) return;
    pending.delete(id);
    if (ok) entry.resolve(payload);
    else entry.reject(new Error(typeof payload === "string" ? payload : JSON.stringify(payload)));
  };

  window.__cfire_event_dispatch = function(event, data) {
    const set = listeners.get(event);
    if (!set) return;
    for (const fn of Array.from(set)) {
      try { fn(data); } catch (err) { console.error("[cfire:webui]", err); }
    }
  };

  window.__cfire_event_dispatch_bytes = function(event, b64) {
    const set = listeners.get(event);
    if (!set) return;
    const bin = atob(b64);
    const bytes = new Uint8Array(bin.length);
    for (let i = 0; i < bin.length; ++i) bytes[i] = bin.charCodeAt(i);
    for (const fn of Array.from(set)) {
      try { fn(bytes); } catch (err) { console.error("[cfire:webui]", err); }
    }
  };

  window.invoke = function(method, params) {
    return new Promise((resolve, reject) => {
      const id = seq++;
      pending.set(id, { resolve, reject });
      postRaw({ jsonrpc: "2.0", id, method: String(method), params: params === undefined ? {} : params });
    });
  };

  window.backend = new Proxy({}, {
    get: function(_target, prop) {
      if (prop === "then" || typeof prop === "symbol") return undefined;
      return function(params) {
        return window.invoke(String(prop), params);
      };
    }
  });

  window.runtime = {
    EventsOn: function(event, callback) {
      let set = listeners.get(event);
      if (!set) { set = new Set(); listeners.set(event, set); }
      set.add(callback);
      return function() { set.delete(callback); };
    },
    EventsOnce: function(event, callback) {
      const off = window.runtime.EventsOn(event, function(data) {
        off();
        callback(data);
      });
      return off;
    },
    EventsOff: function(event) {
      listeners.delete(event);
    },
    EventsEmit: function(event, data) {
      postRaw({ event: String(event), data: data === undefined ? null : data });
    },
    WindowSetTitle: function(title) {
      postRaw({ __sys: "set_title", title: String(title) });
    },
    WindowSetSize: function(w, h) {
      postRaw({ __sys: "set_size", width: Number(w) || 800, height: Number(h) || 600 });
    },
    WindowFullscreen: function() {
      postRaw({ __sys: "fullscreen" });
    },
    WindowMinimise: function() {
      postRaw({ __sys: "minimize" });
    },
    WindowMaximise: function() {
      postRaw({ __sys: "maximize" });
    },
    WindowCenter: function() {
      postRaw({ __sys: "center" });
    },
    Quit: function() {
      postRaw({ __sys: "quit" });
    }
  };

  window.addEventListener("dragover", function(e) {
    e.preventDefault();
  });
  window.addEventListener("drop", function(e) {
    e.preventDefault();
    if (!e.dataTransfer || !e.dataTransfer.files || e.dataTransfer.files.length === 0) return;
    const names = [];
    for (let i = 0; i < e.dataTransfer.files.length; ++i) {
      const f = e.dataTransfer.files[i];
      names.push(f.path || f.name);
    }
    postRaw({ event: "files:dropped", data: names });
    window.__cfire_event_dispatch("files:dropped", names);
  });

  window.addEventListener("mousedown", function(e) {
    if (e.button !== 0) return;
    let el = e.target;
    while (el && el !== document.body) {
      if (el.tagName === "BUTTON" || el.tagName === "INPUT" || el.tagName === "SELECT" || el.tagName === "TEXTAREA" || el.tagName === "A") {
        return;
      }
      if (el.hasAttribute && (el.hasAttribute("data-webui-drag-region") || el.hasAttribute("data-wails-drag"))) {
        postRaw({ __sys: "drag_window" });
        return;
      }
      if (window.getComputedStyle) {
        const dragProp = window.getComputedStyle(el).getPropertyValue("--wails-draggable").trim();
        if (dragProp === "drag") {
          postRaw({ __sys: "drag_window" });
          return;
        }
      }
      el = el.parentElement;
    }
  });
})();
)JS";

struct AssetEntry {
    std::string content{};
    std::string mime{"text/html; charset=utf-8"};
};

struct MenuItemSpec {
    std::string menu{};
    std::string title{};
    std::string shortcut{};
    std::string event{};
};

struct TrayItemSpec {
    std::string title{};
    std::string event{};
};

using RpcHandler = std::function<std::expected<std::string, cfire::Error>(std::string_view)>;
using AsyncRpcHandler = std::function<void(std::string_view, std::function<void(std::expected<std::string, cfire::Error>)>)>;
using EventCallback = std::function<void(const std::string&)>;

struct AppState {
    mutable std::recursive_mutex mtx{};
    std::string title{"cfire WebUI"};
    std::int32_t width{960};
    std::int32_t height{640};
    std::int32_t min_width{0};
    std::int32_t min_height{0};
    std::int32_t max_width{0};
    std::int32_t max_height{0};
    bool frameless{false};
    bool transparent{false};
    bool dark_mode{true};
    bool always_on_top{false};
    bool is_fullscreen{false};
    bool is_minimized{false};
    bool is_maximized{false};
    bool hide_on_close{false};
    bool window_hidden{false};
    bool has_tray{false};
    std::string tray_icon_path{};
    std::string tray_tooltip{};
    std::vector<TrayItemSpec> tray_items{};
    std::atomic<bool> quit_requested{false};
    std::atomic<bool> closed{false};

    std::string icon_path{
#if defined(CFIRE_APP_ICON_PATH)
        CFIRE_APP_ICON_PATH
#endif
    };
    std::vector<MenuItemSpec> custom_menus{};
    std::uint64_t clipboard_generation{0};
    bool last_clipboard_concealed{false};

    std::unordered_map<std::string, AssetEntry> embedded_assets{};
    std::string fallback_index_html{
        "<!doctype html><html><head><meta charset=\"utf-8\"><title>cfire WebUI</title></head>"
        "<body style=\"background:#0d1117;color:#e6edf3;font-family:system-ui,sans-serif;padding:24px\">"
        "<h1>cfire WebUI Ready</h1></body></html>"
    };
    std::vector<std::filesystem::path> serve_dirs{};
    std::unordered_map<std::string, RpcHandler> rpc_handlers{};
    std::unordered_map<std::string, AsyncRpcHandler> async_rpc_handlers{};
    std::unordered_map<std::string, std::vector<EventCallback>> event_listeners{};
    std::unordered_map<std::string, std::string> kv_state{};

    std::size_t max_history_entries{256};
    std::unordered_map<std::string, std::vector<std::string>> emitted_history{};
    std::string last_emitted_topic{};
    std::vector<std::string> eval_history{};
    std::string clipboard_fallback{};

    std::function<void(const std::string&)> platform_eval{};
    std::function<void(const std::string&)> platform_set_title{};
    std::function<void(std::int32_t, std::int32_t)> platform_set_size{};
    std::function<void(bool)> platform_set_dark_mode{};
    std::function<void()> platform_fullscreen{};
    std::function<void()> platform_minimize{};
    std::function<void()> platform_maximize{};
    std::function<void()> platform_center{};
    std::function<void()> platform_drag{};
    std::function<void()> platform_hide_window{};
    std::function<void()> platform_show_window{};
    std::function<void()> platform_quit{};
    std::function<void(const std::string&, std::function<void(bool)>)> platform_screenshot{};
#if defined(CFIRE_HAS_WIN32_BACKEND)
    HWND win32_hwnd{nullptr};
#endif

    AppState() = default;

    void record_emitted(std::string_view event, const std::string& payload) {
        std::lock_guard<std::recursive_mutex> lock(mtx);
        last_emitted_topic = std::string(event);
        auto& vec = emitted_history[std::string(event)];
        if (max_history_entries > 0 && vec.size() >= max_history_entries) {
            vec.erase(vec.begin());
        }
        vec.push_back(payload);
    }

    [[nodiscard]] auto resolve_asset(std::string_view raw_path) const -> std::optional<AssetEntry> {
        std::string norm = normalize_asset_path(raw_path);
        if (has_path_traversal(norm)) return std::nullopt;

        std::lock_guard<std::recursive_mutex> lock(mtx);
        if (auto it = embedded_assets.find(norm); it != embedded_assets.end()) {
            return it->second;
        }

        std::string rel = norm.starts_with("/") ? norm.substr(1) : norm;
        if (rel.empty()) rel = "index.html";

        for (const auto& base : serve_dirs) {
            std::error_code ec;
            auto candidate = base / rel;
            if (std::filesystem::exists(candidate, ec) && std::filesystem::is_regular_file(candidate, ec)) {
                auto data = cfire::fs::read_text(candidate);
                if (data) {
                    return AssetEntry{
                        .content = std::move(*data),
                        .mime = guess_mime_type(norm)
                    };
                }
            }
        }
        if (norm == "/index.html" && !fallback_index_html.empty()) {
            return AssetEntry{
                .content = fallback_index_html,
                .mime = "text/html; charset=utf-8"
            };
        }
        return std::nullopt;
    }

    void eval_js(std::string js) {
        std::function<void(const std::string&)> fn;
        {
            std::lock_guard<std::recursive_mutex> lock(mtx);
            if (max_history_entries > 0 && eval_history.size() >= max_history_entries) {
                eval_history.erase(eval_history.begin());
            }
            eval_history.push_back(js);
            fn = platform_eval;
        }
        if (fn) fn(js);
    }

    void handle_ipc_message(std::string_view raw_json) {
        auto parsed = cfire::json::parse_dynamic(raw_json);
        if (!parsed || !parsed->is_object()) return;

        // 1. Check for system window command (`__sys`)
        if (auto sys_cmd = parsed->get("__sys"); sys_cmd && sys_cmd->is_string()) {
            std::string_view cmd = sys_cmd->as_str();
            if (cmd == "quit") {
                quit_requested.store(true, std::memory_order_relaxed);
                std::function<void()> qfn;
                {
                    std::lock_guard<std::recursive_mutex> lock(mtx);
                    qfn = platform_quit;
                }
                if (qfn) qfn();
            } else if (cmd == "set_title") {
                if (auto t = parsed->get("title"); t && t->is_string()) {
                    std::string new_title(t->as_str());
                    std::function<void(const std::string&)> tfn;
                    {
                        std::lock_guard<std::recursive_mutex> lock(mtx);
                        title = new_title;
                        tfn = platform_set_title;
                    }
                    if (tfn) tfn(new_title);
                }
            } else if (cmd == "set_size") {
                std::int32_t w = width;
                std::int32_t h = height;
                if (auto wv = parsed->get("width"); wv && wv->is_number()) w = static_cast<std::int32_t>(wv->as_int());
                if (auto hv = parsed->get("height"); hv && hv->is_number()) h = static_cast<std::int32_t>(hv->as_int());
                std::function<void(std::int32_t, std::int32_t)> sfn;
                {
                    std::lock_guard<std::recursive_mutex> lock(mtx);
                    width = w;
                    height = h;
                    sfn = platform_set_size;
                }
                if (sfn) sfn(w, h);
            } else if (cmd == "fullscreen") {
                std::function<void()> ffn;
                {
                    std::lock_guard<std::recursive_mutex> lock(mtx);
                    is_fullscreen = !is_fullscreen;
                    ffn = platform_fullscreen;
                }
                if (ffn) ffn();
            } else if (cmd == "minimize") {
                std::function<void()> mfn;
                {
                    std::lock_guard<std::recursive_mutex> lock(mtx);
                    is_minimized = true;
                    mfn = platform_minimize;
                }
                if (mfn) mfn();
            } else if (cmd == "maximize") {
                std::function<void()> mfn;
                {
                    std::lock_guard<std::recursive_mutex> lock(mtx);
                    is_maximized = !is_maximized;
                    mfn = platform_maximize;
                }
                if (mfn) mfn();
            } else if (cmd == "center") {
                std::function<void()> cfn;
                {
                    std::lock_guard<std::recursive_mutex> lock(mtx);
                    cfn = platform_center;
                }
                if (cfn) cfn();
            } else if (cmd == "drag_window") {
                std::function<void()> dfn;
                {
                    std::lock_guard<std::recursive_mutex> lock(mtx);
                    dfn = platform_drag;
                }
                if (dfn) dfn();
            }
            return;
        }

        // 2. Check for frontend event (`event`)
        if (auto ev = parsed->get("event"); ev && ev->is_string()) {
            std::string event_name(ev->as_str());
            std::string data_payload = "null";
            if (auto dv = parsed->get("data")) {
                if (dv->is_string()) {
                    data_payload = std::string(dv->as_str());
                } else {
                    data_payload = cfire::json::stringify_dynamic(*dv, 0);
                }
            }
            dispatch_event_internal(event_name, data_payload);
            return;
        }

        // 3. Check for JSON-RPC 2.0 invocation (`method`)
        if (auto m = parsed->get("method"); m && m->is_string()) {
            std::string method_name(m->as_str());
            std::string id_js = "null";
            if (auto id_v = parsed->get("id")) {
                id_js = cfire::json::stringify_dynamic(*id_v, 0);
            }
            std::string params_json = "{}";
            if (auto pv = parsed->get("params")) {
                params_json = cfire::json::stringify_dynamic(*pv, 0);
            }
            if (method_name == "__screenshot") {
                std::string shot_path{};
                if (auto pv = parsed->get("params"); pv && pv->is_object()) {
                    if (auto pp = pv->get("path"); pp && pp->is_string()) {
                        shot_path = std::string(pp->as_str());
                    }
                }
                std::function<void(const std::string&, std::function<void(bool)>)> sfn;
                {
                    std::lock_guard<std::recursive_mutex> lock(mtx);
                    sfn = platform_screenshot;
                }
                if (sfn && !shot_path.empty()) {
                    sfn(shot_path, [this, id_js](bool ok) {
                        eval_js(std::format("window.__cfire_rpc_resolve({}, true, {});", id_js, ok ? "true" : "false"));
                    });
                } else {
                    eval_js(std::format("window.__cfire_rpc_resolve({}, true, false);", id_js));
                }
                return;
            }
            AsyncRpcHandler async_handler;
            {
                std::lock_guard<std::recursive_mutex> lock(mtx);
                if (auto ait = async_rpc_handlers.find(method_name); ait != async_rpc_handlers.end()) {
                    async_handler = ait->second;
                }
            }
            if (async_handler) {
                async_handler(params_json.empty() ? "{}" : params_json,
                    [this, id_js](std::expected<std::string, cfire::Error> res) {
                        if (res) {
                            std::string payload_js = res->empty() ? "null" : *res;
                            eval_js(std::format("window.__cfire_rpc_resolve({}, true, {});", id_js, payload_js));
                        } else {
                            std::string err_escaped = cfire::json::escape_string(res.error().message());
                            eval_js(std::format("window.__cfire_rpc_resolve({}, false, \"{}\");", id_js, err_escaped));
                        }
                    });
                return;
            }
            auto res = call_rpc_internal(method_name, params_json);
            if (res) {
                std::string payload_js = res->empty() ? "null" : *res;
                eval_js(std::format("window.__cfire_rpc_resolve({}, true, {});", id_js, payload_js));
            } else {
                std::string err_escaped = cfire::json::escape_string(res.error().message());
                eval_js(std::format("window.__cfire_rpc_resolve({}, false, \"{}\");", id_js, err_escaped));
            }
        }
    }

    [[nodiscard]] auto call_rpc_internal(std::string_view method, std::string_view params_json)
        -> std::expected<std::string, cfire::Error>
    {
        RpcHandler handler;
        AsyncRpcHandler async_handler;
        {
            std::lock_guard<std::recursive_mutex> lock(mtx);
            auto it = rpc_handlers.find(std::string(method));
            if (it != rpc_handlers.end()) {
                handler = it->second;
            } else if (auto ait = async_rpc_handlers.find(std::string(method)); ait != async_rpc_handlers.end()) {
                async_handler = ait->second;
            } else {
                return cfire::make_error(std::format("webui: unknown RPC method '{}'", method));
            }
        }
        if (handler) {
            return handler(params_json.empty() ? "{}" : params_json);
        }
        auto prom = std::make_shared<std::promise<std::expected<std::string, cfire::Error>>>();
        auto fut = prom->get_future();
        async_handler(params_json.empty() ? "{}" : params_json,
            [prom](std::expected<std::string, cfire::Error> r) {
                prom->set_value(std::move(r));
            });
        return fut.get();
    }

    void dispatch_event_internal(std::string_view event, const std::string& data_payload) {
        std::vector<EventCallback> cbs;
        {
            std::lock_guard<std::recursive_mutex> lock(mtx);
            if (auto it = event_listeners.find(std::string(event)); it != event_listeners.end()) {
                cbs = it->second;
            }
        }
        for (const auto& cb : cbs) {
            if (cb) cb(data_payload);
        }
    }
};

} // namespace detail
} // namespace cfire::webui

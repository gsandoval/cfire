#pragma once
#include "../cfire_prelude.hpp"
#include "fs.hpp"
#include "json.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <expected>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
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
#endif

#if defined(CFIRE_TEST_LINUX_WEBKITGTK_BACKEND)
#define CFIRE_HAS_LINUX_WEBKITGTK 1
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

using RpcHandler = std::function<std::expected<std::string, cfire::Error>(std::string_view)>;
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
    std::atomic<bool> quit_requested{false};
    std::atomic<bool> closed{false};

    std::unordered_map<std::string, AssetEntry> embedded_assets{};
    std::vector<std::filesystem::path> serve_dirs{};
    std::unordered_map<std::string, RpcHandler> rpc_handlers{};
    std::unordered_map<std::string, std::vector<EventCallback>> event_listeners{};
    std::unordered_map<std::string, std::string> kv_state{};

    std::unordered_map<std::string, std::vector<std::string>> emitted_history{};
    std::vector<std::string> eval_history{};

    std::function<void(const std::string&)> platform_eval{};
    std::function<void(const std::string&)> platform_set_title{};
    std::function<void(std::int32_t, std::int32_t)> platform_set_size{};
    std::function<void()> platform_fullscreen{};
    std::function<void()> platform_minimize{};
    std::function<void()> platform_maximize{};
    std::function<void()> platform_center{};
    std::function<void()> platform_drag{};
    std::function<void()> platform_quit{};

    AppState() {
        embedded_assets["/index.html"] = AssetEntry{
            .content = "<!doctype html><html><head><meta charset=\"utf-8\"><title>cfire WebUI</title></head>"
                       "<body style=\"background:#0d1117;color:#e6edf3;font-family:system-ui,sans-serif;padding:24px\">"
                       "<h1>cfire WebUI Ready</h1></body></html>",
            .mime = "text/html; charset=utf-8"
        };
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
        return std::nullopt;
    }

    void eval_js(std::string js) {
        std::function<void(const std::string&)> fn;
        {
            std::lock_guard<std::recursive_mutex> lock(mtx);
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
        {
            std::lock_guard<std::recursive_mutex> lock(mtx);
            auto it = rpc_handlers.find(std::string(method));
            if (it == rpc_handlers.end()) {
                return cfire::make_error(std::format("webui: unknown RPC method '{}'", method));
            }
            handler = it->second;
        }
        return handler(params_json.empty() ? "{}" : params_json);
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

#if defined(__APPLE__) && defined(__OBJC__)

@interface CfireWebUISchemeHandler : NSObject <WKURLSchemeHandler> {
@public
    std::shared_ptr<cfire::webui::detail::AppState> state;
    std::mutex active_tasks_mtx;
    NSMutableSet<id<WKURLSchemeTask>>* active_tasks;
}
@end

@implementation CfireWebUISchemeHandler
- (instancetype)init {
    self = [super init];
    if (self) {
        active_tasks = [[NSMutableSet alloc] init];
    }
    return self;
}

- (void)webView:(WKWebView*)webView startURLSchemeTask:(id<WKURLSchemeTask>)urlSchemeTask {
    (void)webView;
    {
        std::lock_guard<std::mutex> lock(active_tasks_mtx);
        [active_tasks addObject:urlSchemeTask];
    }
    NSURL* url = urlSchemeTask.request.URL;
    std::string path_str = url.path ? [url.path UTF8String] : "/index.html";
    if (path_str.empty() || path_str == "/") path_str = "/index.html";

    auto asset = state ? state->resolve_asset(path_str) : std::nullopt;
    NSInteger status_code = asset.has_value() ? 200 : 404;
    std::string body_str = asset.has_value() ? asset->content : "404 Not Found";
    std::string mime_str = asset.has_value() ? asset->mime : "text/plain; charset=utf-8";

    NSString* ns_mime = [NSString stringWithUTF8String:mime_str.c_str()];
    NSData* data = [NSData dataWithBytes:body_str.data() length:body_str.size()];
    NSDictionary* headers = @{
        @"Content-Type": ns_mime ? ns_mime : @"application/octet-stream",
        @"Content-Length": [NSString stringWithFormat:@"%lu", static_cast<unsigned long>(body_str.size())],
        @"Access-Control-Allow-Origin": @"*",
        @"Cache-Control": @"no-store"
    };

    NSHTTPURLResponse* response = [[NSHTTPURLResponse alloc] initWithURL:url
                                                              statusCode:status_code
                                                             HTTPVersion:@"HTTP/1.1"
                                                            headerFields:headers];
    {
        std::lock_guard<std::mutex> lock(active_tasks_mtx);
        if (![active_tasks containsObject:urlSchemeTask]) return;
        [urlSchemeTask didReceiveResponse:response];
        [urlSchemeTask didReceiveData:data];
        [urlSchemeTask didFinish];
        [active_tasks removeObject:urlSchemeTask];
    }
}

- (void)webView:(WKWebView*)webView stopURLSchemeTask:(id<WKURLSchemeTask>)urlSchemeTask {
    (void)webView;
    std::lock_guard<std::mutex> lock(active_tasks_mtx);
    [active_tasks removeObject:urlSchemeTask];
}
@end

@interface CfireWebUIMessageHandler : NSObject <WKScriptMessageHandler> {
@public
    std::shared_ptr<cfire::webui::detail::AppState> state;
}
@end

@implementation CfireWebUIMessageHandler
- (void)userContentController:(WKUserContentController*)userContentController
      didReceiveScriptMessage:(WKScriptMessage*)message {
    (void)userContentController;
    if (!state) return;
    if ([message.body isKindOfClass:[NSString class]]) {
        NSString* body = (NSString*)message.body;
        const char* utf8 = [body UTF8String];
        if (utf8) {
            state->handle_ipc_message(utf8);
        }
    }
}
@end

#if CFIRE_WEBUI_IOS

namespace cfire::webui::detail {
inline std::shared_ptr<AppState>& active_ios_state() {
    static std::shared_ptr<AppState> s;
    return s;
}
} // namespace cfire::webui::detail

@interface CfireWebUIAppDelegate : UIResponder <UIApplicationDelegate>
@property (strong, nonatomic) UIWindow* window;
@property (strong, nonatomic) WKWebView* webView;
@end

@implementation CfireWebUIAppDelegate
- (BOOL)application:(UIApplication*)application didFinishLaunchingWithOptions:(NSDictionary*)launchOptions {
    (void)application;
    (void)launchOptions;
    auto st = cfire::webui::detail::active_ios_state();

    CGRect bounds = [[UIScreen mainScreen] bounds];
    self.window = [[UIWindow alloc] initWithFrame:bounds];
    UIViewController* rootVC = [[UIViewController alloc] init];
    rootVC.view.backgroundColor = [UIColor colorWithRed:0.05 green:0.07 blue:0.09 alpha:1.0];

    WKWebViewConfiguration* config = [[WKWebViewConfiguration alloc] init];
    WKUserContentController* ucm = [[WKUserContentController alloc] init];

    NSString* preloadSource = [NSString stringWithUTF8String:std::string(cfire::webui::detail::kPreloadBridgeJs).c_str()];
    WKUserScript* preloadScript = [[WKUserScript alloc] initWithSource:preloadSource
                                                         injectionTime:WKUserScriptInjectionTimeAtDocumentStart
                                                      forMainFrameOnly:YES];
    [ucm addUserScript:preloadScript];

    CfireWebUIMessageHandler* msgHandler = [[CfireWebUIMessageHandler alloc] init];
    msgHandler->state = st;
    [ucm addScriptMessageHandler:msgHandler name:@"external"];
    config.userContentController = ucm;

    CfireWebUISchemeHandler* schemeHandler = [[CfireWebUISchemeHandler alloc] init];
    schemeHandler->state = st;
    [config setURLSchemeHandler:schemeHandler forURLScheme:@"app"];

    self.webView = [[WKWebView alloc] initWithFrame:bounds configuration:config];
    self.webView.autoresizingMask = UIViewAutoresizingFlexibleWidth | UIViewAutoresizingFlexibleHeight;
    self.webView.scrollView.contentInsetAdjustmentBehavior = UIScrollViewContentInsetAdjustmentNever;
    self.webView.opaque = NO;
    self.webView.backgroundColor = [UIColor clearColor];

    [rootVC.view addSubview:self.webView];
    self.window.rootViewController = rootVC;
    [self.window makeKeyAndVisible];

    if (st) {
        WKWebView* wv = self.webView;
        std::lock_guard<std::recursive_mutex> lock(st->mtx);
        st->platform_eval = [wv](const std::string& js) {
            NSString* ns_js = [NSString stringWithUTF8String:js.c_str()];
            if (!ns_js) return;
            dispatch_async(dispatch_get_main_queue(), ^{
                [wv evaluateJavaScript:ns_js completionHandler:nil];
            });
        };
    }

    NSURL* startURL = [NSURL URLWithString:@"app://localhost/index.html"];
    [self.webView loadRequest:[NSURLRequest requestWithURL:startURL]];
    return YES;
}
@end

#else // macOS Cocoa + WKWebView

@interface CfireWebUIWindowDelegate : NSObject <NSWindowDelegate> {
@public
    std::shared_ptr<cfire::webui::detail::AppState> state;
    bool stop_nsapp_on_close;
}
@end

@implementation CfireWebUIWindowDelegate
- (void)windowWillClose:(NSNotification*)notification {
    (void)notification;
    if (state) {
        state->quit_requested.store(true, std::memory_order_relaxed);
    }
    if (stop_nsapp_on_close) {
        [NSApp stop:nil];
        NSEvent* ev = [NSEvent otherEventWithType:NSEventTypeApplicationDefined
                                         location:NSMakePoint(0, 0)
                                    modifierFlags:0
                                        timestamp:0
                                     windowNumber:0
                                          context:nil
                                          subtype:0
                                            data1:0
                                            data2:0];
        [NSApp postEvent:ev atStart:YES];
    }
}
@end

#endif // PLATFORM_IOS vs macOS
#endif // defined(__APPLE__) && defined(__OBJC__)

namespace cfire::webui {

class App {
public:
    App() : state_(std::make_shared<detail::AppState>()) {
        register_default_serve_dirs();
    }

    App(std::string_view title, std::int32_t width, std::int32_t height)
        : state_(std::make_shared<detail::AppState>())
    {
        state_->title = std::string(title);
        state_->width = width > 0 ? width : 960;
        state_->height = height > 0 ? height : 640;
        register_default_serve_dirs();
    }

    // --- Typed Two-Way RPC Binding (`app.bind[ReqStruct]("method") |req| { ... }`) ---

    template <typename ReqStruct, typename Fn>
    void bind(std::string_view name, Fn&& fn) {
        std::lock_guard<std::recursive_mutex> lock(state_->mtx);
        state_->rpc_handlers[std::string(name)] =
            [f = std::forward<Fn>(fn)](std::string_view params_json) mutable -> std::expected<std::string, cfire::Error> {
                std::string payload = params_json.empty() ? "{}" : std::string(params_json);
                auto parsed = cfire::json::parse<ReqStruct>(payload);
                if (!parsed) return std::unexpected(parsed.error());

                using Ret = std::invoke_result_t<decltype(f)&, const ReqStruct&>;
                if constexpr (std::is_void_v<Ret>) {
                    f(*parsed);
                    return std::string("null");
                } else if constexpr (detail::is_expected<Ret>::value) {
                    auto res = f(*parsed);
                    if (!res) return std::unexpected(res.error());
                    using ValT = typename Ret::value_type;
                    if constexpr (std::is_void_v<ValT>) {
                        return std::string("null");
                    } else {
                        return cfire::json::stringify(*res);
                    }
                } else {
                    auto res = f(*parsed);
                    return cfire::json::stringify(res);
                }
            };
    }

    template <typename Fn>
    void bind_action(std::string_view name, Fn&& fn) {
        std::lock_guard<std::recursive_mutex> lock(state_->mtx);
        state_->rpc_handlers[std::string(name)] =
            [f = std::forward<Fn>(fn)](std::string_view params_json) mutable -> std::expected<std::string, cfire::Error> {
                if constexpr (std::is_invocable_v<decltype(f)&>) {
                    using Ret = std::invoke_result_t<decltype(f)&>;
                    if constexpr (std::is_void_v<Ret>) {
                        f();
                        return std::string("null");
                    } else if constexpr (detail::is_expected<Ret>::value) {
                        auto res = f();
                        if (!res) return std::unexpected(res.error());
                        using ValT = typename Ret::value_type;
                        if constexpr (std::is_void_v<ValT>) {
                            return std::string("null");
                        } else {
                            return cfire::json::stringify(*res);
                        }
                    } else {
                        auto res = f();
                        return cfire::json::stringify(res);
                    }
                } else {
                    std::string arg(params_json.empty() ? "{}" : params_json);
                    using Ret = std::invoke_result_t<decltype(f)&, const std::string&>;
                    if constexpr (std::is_void_v<Ret>) {
                        f(arg);
                        return std::string("null");
                    } else if constexpr (detail::is_expected<Ret>::value) {
                        auto res = f(arg);
                        if (!res) return std::unexpected(res.error());
                        using ValT = typename Ret::value_type;
                        if constexpr (std::is_void_v<ValT>) {
                            return std::string("null");
                        } else {
                            return cfire::json::stringify(*res);
                        }
                    } else {
                        auto res = f(arg);
                        return cfire::json::stringify(res);
                    }
                }
            };
    }

    template <typename Fn>
    void bind_raw(std::string_view name, Fn&& fn) {
        std::lock_guard<std::recursive_mutex> lock(state_->mtx);
        state_->rpc_handlers[std::string(name)] =
            [f = std::forward<Fn>(fn)](std::string_view params_json) mutable -> std::expected<std::string, cfire::Error> {
                std::string arg(params_json.empty() ? "{}" : params_json);
                using Ret = std::invoke_result_t<decltype(f)&, const std::string&>;
                if constexpr (detail::is_expected<Ret>::value) {
                    return f(arg);
                } else if constexpr (std::is_void_v<Ret>) {
                    f(arg);
                    return std::string("null");
                } else {
                    return std::string(f(arg));
                }
            };
    }

    // --- Bidirectional Event Bus (`app.on` & `app.emit`) ---

    template <typename Fn>
    void on(std::string_view event, Fn&& fn) {
        std::lock_guard<std::recursive_mutex> lock(state_->mtx);
        state_->event_listeners[std::string(event)].push_back(
            [f = std::forward<Fn>(fn)](const std::string& payload) mutable {
                if constexpr (std::is_invocable_v<decltype(f)&, const std::string&>) {
                    f(payload);
                } else {
                    f();
                }
            });
    }

    template <typename T>
    void emit(std::string_view event, const T& payload) {
        std::string json_val = cfire::json::stringify(payload);
        {
            std::lock_guard<std::recursive_mutex> lock(state_->mtx);
            state_->emitted_history[std::string(event)].push_back(json_val);
        }
        std::string ev_escaped = cfire::json::escape_string(event);
        state_->eval_js(std::format("window.__cfire_event_dispatch(\"{}\", {});", ev_escaped, json_val));
    }

    // --- Reactive Key-Value State Store ---

    void set_state(std::string_view key, std::string_view val) {
        {
            std::lock_guard<std::recursive_mutex> lock(state_->mtx);
            state_->kv_state[std::string(key)] = std::string(val);
        }
        std::string k_esc = cfire::json::escape_string(key);
        std::string v_esc = cfire::json::escape_string(val);
        std::string json_obj = std::format("{{\"key\":\"{}\",\"value\":\"{}\"}}", k_esc, v_esc);
        {
            std::lock_guard<std::recursive_mutex> lock(state_->mtx);
            state_->emitted_history["state:change"].push_back(json_obj);
        }
        state_->eval_js(std::format("window.__cfire_event_dispatch(\"state:change\", {});", json_obj));
    }

    [[nodiscard]] auto get_state(std::string_view key) const -> std::string {
        std::lock_guard<std::recursive_mutex> lock(state_->mtx);
        if (auto it = state_->kv_state.find(std::string(key)); it != state_->kv_state.end()) {
            return it->second;
        }
        return "";
    }

    // --- Asset VFS (`app://localhost/`) ---

    void set_html(std::string_view html) {
        embed_asset("/index.html", html, "text/html; charset=utf-8");
    }

    void embed_asset(std::string_view path, std::string_view content, std::string_view mime = "") {
        std::string norm = detail::normalize_asset_path(path);
        std::string resolved_mime = mime.empty() ? detail::guess_mime_type(norm) : std::string(mime);
        std::lock_guard<std::recursive_mutex> lock(state_->mtx);
        state_->embedded_assets[norm] = detail::AssetEntry{
            .content = std::string(content),
            .mime = std::move(resolved_mime)
        };
    }

    void serve_dir(std::string_view dir_path) {
        if (dir_path.empty()) return;
        std::filesystem::path p(dir_path);
        std::lock_guard<std::recursive_mutex> lock(state_->mtx);
        if (p.is_absolute()) {
            state_->serve_dirs.insert(state_->serve_dirs.begin(), p);
            return;
        }
        auto exe = detail::executable_dir();
        // Priority order: CWD/dir, macOS bundle Resources/dir, iOS/portable exe/dir
        state_->serve_dirs.insert(state_->serve_dirs.begin(), exe / p);
        state_->serve_dirs.insert(state_->serve_dirs.begin(), exe / ".." / "Resources" / p);
        std::error_code ec;
        auto cwd = std::filesystem::current_path(ec);
        if (!ec) {
            state_->serve_dirs.insert(state_->serve_dirs.begin(), cwd / p);
        }
    }

    void eval(std::string_view js) {
        state_->eval_js(std::string(js));
    }

    // --- Window Chrome, Lifecycle & Appearance Controls ---

    void set_title(std::string_view title) {
        std::function<void(const std::string&)> fn;
        std::string s(title);
        {
            std::lock_guard<std::recursive_mutex> lock(state_->mtx);
            state_->title = s;
            fn = state_->platform_set_title;
        }
        if (fn) fn(s);
    }

    [[nodiscard]] auto title() const -> std::string {
        std::lock_guard<std::recursive_mutex> lock(state_->mtx);
        return state_->title;
    }

    void set_size(std::int32_t width, std::int32_t height) {
        std::function<void(std::int32_t, std::int32_t)> fn;
        {
            std::lock_guard<std::recursive_mutex> lock(state_->mtx);
            state_->width = width;
            state_->height = height;
            fn = state_->platform_set_size;
        }
        if (fn) fn(width, height);
    }

    void set_min_size(std::int32_t min_w, std::int32_t min_h) {
        std::lock_guard<std::recursive_mutex> lock(state_->mtx);
        state_->min_width = min_w;
        state_->min_height = min_h;
    }

    void set_max_size(std::int32_t max_w, std::int32_t max_h) {
        std::lock_guard<std::recursive_mutex> lock(state_->mtx);
        state_->max_width = max_w;
        state_->max_height = max_h;
    }

    void set_frameless(bool frameless) {
        std::lock_guard<std::recursive_mutex> lock(state_->mtx);
        state_->frameless = frameless;
    }

    void set_transparent(bool transparent) {
        std::lock_guard<std::recursive_mutex> lock(state_->mtx);
        state_->transparent = transparent;
    }

    void set_dark_mode(bool dark) {
        std::lock_guard<std::recursive_mutex> lock(state_->mtx);
        state_->dark_mode = dark;
    }

    void set_always_on_top(bool on_top) {
        std::lock_guard<std::recursive_mutex> lock(state_->mtx);
        state_->always_on_top = on_top;
    }

    void fullscreen() {
        std::function<void()> fn;
        {
            std::lock_guard<std::recursive_mutex> lock(state_->mtx);
            state_->is_fullscreen = !state_->is_fullscreen;
            fn = state_->platform_fullscreen;
        }
        if (fn) fn();
    }

    void minimize() {
        std::function<void()> fn;
        {
            std::lock_guard<std::recursive_mutex> lock(state_->mtx);
            state_->is_minimized = true;
            fn = state_->platform_minimize;
        }
        if (fn) fn();
    }

    void maximize() {
        std::function<void()> fn;
        {
            std::lock_guard<std::recursive_mutex> lock(state_->mtx);
            state_->is_maximized = !state_->is_maximized;
            fn = state_->platform_maximize;
        }
        if (fn) fn();
    }

    void center() {
        std::function<void()> fn;
        {
            std::lock_guard<std::recursive_mutex> lock(state_->mtx);
            fn = state_->platform_center;
        }
        if (fn) fn();
    }

    void quit() {
        state_->quit_requested.store(true, std::memory_order_relaxed);
        std::function<void()> fn;
        {
            std::lock_guard<std::recursive_mutex> lock(state_->mtx);
            fn = state_->platform_quit;
        }
        if (fn) fn();
    }

    void close() noexcept {
        if (!state_) return;
        state_->closed.store(true, std::memory_order_relaxed);
        quit();
    }

    // --- Native OS Dialogs ---

    [[nodiscard]] auto open_file_dialog(std::string_view dialog_title = "Open File") const -> std::string {
        if (const char* env_h = std::getenv("CFIRE_HEADLESS_TEST"); env_h && std::string_view(env_h) == "1") {
            return "";
        }
#if defined(__APPLE__) && defined(__OBJC__) && !CFIRE_WEBUI_IOS
        NSOpenPanel* panel = [NSOpenPanel openPanel];
        [panel setTitle:[NSString stringWithUTF8String:std::string(dialog_title).c_str()]];
        [panel setCanChooseFiles:YES];
        [panel setCanChooseDirectories:NO];
        [panel setAllowsMultipleSelection:NO];
        if ([panel runModal] == NSModalResponseOK) {
            NSURL* url = [[panel URLs] firstObject];
            if (url && url.path) return std::string([url.path UTF8String]);
        }
#else
        (void)dialog_title;
#endif
        return "";
    }

    [[nodiscard]] auto save_file_dialog(std::string_view dialog_title = "Save File", std::string_view default_name = "") const -> std::string {
        if (const char* env_h = std::getenv("CFIRE_HEADLESS_TEST"); env_h && std::string_view(env_h) == "1") {
            return "";
        }
#if defined(__APPLE__) && defined(__OBJC__) && !CFIRE_WEBUI_IOS
        NSSavePanel* panel = [NSSavePanel savePanel];
        [panel setTitle:[NSString stringWithUTF8String:std::string(dialog_title).c_str()]];
        if (!default_name.empty()) {
            [panel setNameFieldStringValue:[NSString stringWithUTF8String:std::string(default_name).c_str()]];
        }
        if ([panel runModal] == NSModalResponseOK) {
            NSURL* url = [panel URL];
            if (url && url.path) return std::string([url.path UTF8String]);
        }
#else
        (void)dialog_title;
        (void)default_name;
#endif
        return "";
    }

    void message_dialog(std::string_view dialog_title, std::string_view message) const {
        if (const char* env_h = std::getenv("CFIRE_HEADLESS_TEST"); env_h && std::string_view(env_h) == "1") {
            return;
        }
#if defined(__APPLE__) && defined(__OBJC__) && !CFIRE_WEBUI_IOS
        NSAlert* alert = [[NSAlert alloc] init];
        [alert setMessageText:[NSString stringWithUTF8String:std::string(dialog_title).c_str()]];
        [alert setInformativeText:[NSString stringWithUTF8String:std::string(message).c_str()]];
        [alert addButtonWithTitle:@"OK"];
        [alert runModal];
#else
        (void)dialog_title;
        (void)message;
#endif
    }

    // --- Headless Test & Inspection API (`cfire test`) ---

    [[nodiscard]] auto call_rpc(std::string_view method, std::string_view params_json = "{}")
        -> std::expected<std::string, cfire::Error>
    {
        return state_->call_rpc_internal(method, params_json);
    }

    void dispatch_event(std::string_view event, std::string_view data_json = "{}") {
        state_->dispatch_event_internal(event, std::string(data_json));
    }

    void handle_ipc(std::string_view raw_ipc_json) {
        state_->handle_ipc_message(raw_ipc_json);
    }

    [[nodiscard]] auto resolve_asset_content(std::string_view path) const -> std::string {
        if (auto a = state_->resolve_asset(path)) return a->content;
        return "";
    }

    [[nodiscard]] auto resolve_asset_mime(std::string_view path) const -> std::string {
        if (auto a = state_->resolve_asset(path)) return a->mime;
        return "";
    }

    [[nodiscard]] auto emitted_events_count(std::string_view event) const -> std::int64_t {
        std::lock_guard<std::recursive_mutex> lock(state_->mtx);
        if (auto it = state_->emitted_history.find(std::string(event)); it != state_->emitted_history.end()) {
            return static_cast<std::int64_t>(it->second.size());
        }
        return 0;
    }

    [[nodiscard]] auto last_emitted_payload(std::string_view event) const -> std::string {
        std::lock_guard<std::recursive_mutex> lock(state_->mtx);
        if (auto it = state_->emitted_history.find(std::string(event)); it != state_->emitted_history.end() && !it->second.empty()) {
            return it->second.back();
        }
        return "";
    }

    // --- Offscreen / Automated Live `WKWebView` Test Runner ---

    [[nodiscard]] auto run_webview_test(std::int32_t timeout_ms = 3000) -> std::expected<bool, cfire::Error> {
#if defined(__APPLE__) && defined(__OBJC__) && !CFIRE_WEBUI_IOS
        @autoreleasepool {
            [NSApplication sharedApplication];
            state_->quit_requested.store(false, std::memory_order_relaxed);

            NSRect frame = NSMakeRect(-2000, -2000, state_->width, state_->height);
            NSWindow* window = [[NSWindow alloc] initWithContentRect:frame
                                                           styleMask:NSWindowStyleMaskBorderless
                                                             backing:NSBackingStoreBuffered
                                                               defer:NO];
            [window setReleasedWhenClosed:NO];

            WKWebViewConfiguration* config = [[WKWebViewConfiguration alloc] init];
            WKUserContentController* ucm = [[WKUserContentController alloc] init];

            NSString* preloadSource = [NSString stringWithUTF8String:std::string(detail::kPreloadBridgeJs).c_str()];
            WKUserScript* preloadScript = [[WKUserScript alloc] initWithSource:preloadSource
                                                                 injectionTime:WKUserScriptInjectionTimeAtDocumentStart
                                                              forMainFrameOnly:YES];
            [ucm addUserScript:preloadScript];

            CfireWebUIMessageHandler* msgHandler = [[CfireWebUIMessageHandler alloc] init];
            msgHandler->state = state_;
            [ucm addScriptMessageHandler:msgHandler name:@"external"];
            config.userContentController = ucm;

            CfireWebUISchemeHandler* schemeHandler = [[CfireWebUISchemeHandler alloc] init];
            schemeHandler->state = state_;
            [config setURLSchemeHandler:schemeHandler forURLScheme:@"app"];

            WKWebView* webView = [[WKWebView alloc] initWithFrame:NSMakeRect(0, 0, state_->width, state_->height)
                                                    configuration:config];
            [window setContentView:webView];

            {
                std::lock_guard<std::recursive_mutex> lock(state_->mtx);
                state_->platform_eval = [webView](const std::string& js) {
                    NSString* ns_js = [NSString stringWithUTF8String:js.c_str()];
                    if (!ns_js) return;
                    if ([NSThread isMainThread]) {
                        [webView evaluateJavaScript:ns_js completionHandler:nil];
                    } else {
                        dispatch_async(dispatch_get_main_queue(), ^{
                            [webView evaluateJavaScript:ns_js completionHandler:nil];
                        });
                    }
                };
                state_->platform_set_title = [window](const std::string& new_title) {
                    NSString* ns_t = [NSString stringWithUTF8String:new_title.c_str()];
                    if (ns_t) [window setTitle:ns_t];
                };
                state_->platform_quit = [st = state_]() {
                    st->quit_requested.store(true, std::memory_order_relaxed);
                };
            }

            NSURL* startURL = [NSURL URLWithString:@"app://localhost/index.html"];
            [webView loadRequest:[NSURLRequest requestWithURL:startURL]];

            auto start = std::chrono::steady_clock::now();
            while (!state_->quit_requested.load(std::memory_order_relaxed)) {
                CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.01, true);
                auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - start).count();
                if (elapsed >= timeout_ms) {
                    break;
                }
            }

            [ucm removeScriptMessageHandlerForName:@"external"];
            {
                std::lock_guard<std::recursive_mutex> lock(state_->mtx);
                state_->platform_eval = nullptr;
                state_->platform_set_title = nullptr;
                state_->platform_quit = nullptr;
            }
            [window close];
            return state_->quit_requested.load(std::memory_order_relaxed);
        }
#else
        (void)timeout_ms;
        return true;
#endif
    }

    // --- Native Application Event Loop (`app.run()`) ---

    void run() {
        if (const char* env_h = std::getenv("CFIRE_HEADLESS_TEST"); env_h && std::string_view(env_h) == "1") {
            return;
        }
        state_->quit_requested.store(false, std::memory_order_relaxed);

#if defined(__APPLE__) && defined(__OBJC__)
#if CFIRE_WEBUI_IOS
        detail::active_ios_state() = state_;
        @autoreleasepool {
            char arg0[] = "cfire_webui";
            char* argv_dummy[] = {arg0, nullptr};
            UIApplicationMain(1, argv_dummy, nil, NSStringFromClass([CfireWebUIAppDelegate class]));
        }
#else
        @autoreleasepool {
            [NSApplication sharedApplication];
            [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];

            NSWindowStyleMask style = NSWindowStyleMaskTitled | NSWindowStyleMaskClosable |
                                      NSWindowStyleMaskMiniaturizable | NSWindowStyleMaskResizable;
            if (state_->frameless) {
                style |= NSWindowStyleMaskFullSizeContentView;
            }

            NSRect frame = NSMakeRect(0, 0, state_->width, state_->height);
            NSWindow* window = [[NSWindow alloc] initWithContentRect:frame
                                                           styleMask:style
                                                             backing:NSBackingStoreBuffered
                                                               defer:NO];
            [window setReleasedWhenClosed:NO];
            [window setTitle:[NSString stringWithUTF8String:state_->title.c_str()]];
            [window center];

            if (state_->min_width > 0 && state_->min_height > 0) {
                [window setContentMinSize:NSMakeSize(state_->min_width, state_->min_height)];
            }
            if (state_->max_width > 0 && state_->max_height > 0) {
                [window setContentMaxSize:NSMakeSize(state_->max_width, state_->max_height)];
            }
            if (state_->dark_mode) {
                [window setAppearance:[NSAppearance appearanceNamed:NSAppearanceNameDarkAqua]];
            } else {
                [window setAppearance:[NSAppearance appearanceNamed:NSAppearanceNameAqua]];
            }
            if (state_->frameless) {
                [window setTitlebarAppearsTransparent:YES];
                [window setTitleVisibility:NSWindowTitleHidden];
            }
            if (state_->transparent) {
                [window setOpaque:NO];
                [window setBackgroundColor:[NSColor clearColor]];
            } else {
                [window setBackgroundColor:[NSColor colorWithSRGBRed:0.05 green:0.07 blue:0.09 alpha:1.0]];
            }
            if (state_->always_on_top) {
                [window setLevel:NSFloatingWindowLevel];
            }

            CfireWebUIWindowDelegate* winDelegate = [[CfireWebUIWindowDelegate alloc] init];
            winDelegate->state = state_;
            winDelegate->stop_nsapp_on_close = true;
            [window setDelegate:winDelegate];

            WKWebViewConfiguration* config = [[WKWebViewConfiguration alloc] init];
            [config.preferences setValue:@YES forKey:@"developerExtrasEnabled"];

            WKUserContentController* ucm = [[WKUserContentController alloc] init];
            NSString* preloadSource = [NSString stringWithUTF8String:std::string(detail::kPreloadBridgeJs).c_str()];
            WKUserScript* preloadScript = [[WKUserScript alloc] initWithSource:preloadSource
                                                                 injectionTime:WKUserScriptInjectionTimeAtDocumentStart
                                                              forMainFrameOnly:YES];
            [ucm addUserScript:preloadScript];

            CfireWebUIMessageHandler* msgHandler = [[CfireWebUIMessageHandler alloc] init];
            msgHandler->state = state_;
            [ucm addScriptMessageHandler:msgHandler name:@"external"];
            config.userContentController = ucm;

            CfireWebUISchemeHandler* schemeHandler = [[CfireWebUISchemeHandler alloc] init];
            schemeHandler->state = state_;
            [config setURLSchemeHandler:schemeHandler forURLScheme:@"app"];

            WKWebView* webView = [[WKWebView alloc] initWithFrame:[[window contentView] bounds]
                                                    configuration:config];
            [webView setAutoresizingMask:(NSViewWidthSizable | NSViewHeightSizable)];
            if (state_->transparent) {
                [webView setValue:@NO forKey:@"drawsBackground"];
            }
            [window setContentView:webView];

            {
                std::lock_guard<std::recursive_mutex> lock(state_->mtx);
                state_->platform_eval = [webView](const std::string& js) {
                    NSString* ns_js = [NSString stringWithUTF8String:js.c_str()];
                    if (!ns_js) return;
                    dispatch_async(dispatch_get_main_queue(), ^{
                        [webView evaluateJavaScript:ns_js completionHandler:nil];
                    });
                };
                state_->platform_set_title = [window](const std::string& new_title) {
                    NSString* ns_t = [NSString stringWithUTF8String:new_title.c_str()];
                    if (!ns_t) return;
                    dispatch_async(dispatch_get_main_queue(), ^{
                        [window setTitle:ns_t];
                    });
                };
                state_->platform_set_size = [window](std::int32_t w, std::int32_t h) {
                    dispatch_async(dispatch_get_main_queue(), ^{
                        [window setContentSize:NSMakeSize(w, h)];
                    });
                };
                state_->platform_fullscreen = [window]() {
                    dispatch_async(dispatch_get_main_queue(), ^{
                        [window toggleFullScreen:nil];
                    });
                };
                state_->platform_minimize = [window]() {
                    dispatch_async(dispatch_get_main_queue(), ^{
                        [window miniaturize:nil];
                    });
                };
                state_->platform_maximize = [window]() {
                    dispatch_async(dispatch_get_main_queue(), ^{
                        [window zoom:nil];
                    });
                };
                state_->platform_center = [window]() {
                    dispatch_async(dispatch_get_main_queue(), ^{
                        [window center];
                    });
                };
                state_->platform_drag = [window]() {
                    dispatch_async(dispatch_get_main_queue(), ^{
                        NSEvent* ev = [NSApp currentEvent];
                        if (ev) [window performWindowDragWithEvent:ev];
                    });
                };
                state_->platform_quit = [window]() {
                    dispatch_async(dispatch_get_main_queue(), ^{
                        [window close];
                        [NSApp stop:nil];
                    });
                };
            }

            NSURL* startURL = [NSURL URLWithString:@"app://localhost/index.html"];
            [webView loadRequest:[NSURLRequest requestWithURL:startURL]];

            [window makeKeyAndOrderFront:nil];
            [NSApp activateIgnoringOtherApps:YES];
            [NSApp run];

            [ucm removeScriptMessageHandlerForName:@"external"];
            {
                std::lock_guard<std::recursive_mutex> lock(state_->mtx);
                state_->platform_eval = nullptr;
                state_->platform_set_title = nullptr;
                state_->platform_set_size = nullptr;
                state_->platform_fullscreen = nullptr;
                state_->platform_minimize = nullptr;
                state_->platform_maximize = nullptr;
                state_->platform_center = nullptr;
                state_->platform_drag = nullptr;
                state_->platform_quit = nullptr;
            }
        }
#endif
#elif defined(CFIRE_HAS_LINUX_WEBKITGTK)
        run_linux_webkitgtk();
#endif
    }

#if defined(CFIRE_HAS_LINUX_WEBKITGTK)
    static void linux_scheme_cb(WebKitURISchemeRequest* request, gpointer user_data) {
        auto* st = static_cast<detail::AppState*>(user_data);
        const char* path_c = webkit_uri_scheme_request_get_path(request);
        std::string path_str = (path_c && path_c[0] != '\0') ? path_c : "/index.html";

        auto asset = st ? st->resolve_asset(path_str) : std::nullopt;
        std::string body = asset ? asset->content : "404 Not Found";
        std::string mime = asset ? asset->mime : "text/plain; charset=utf-8";

        char* copy_buf = static_cast<char*>(g_malloc(body.size() > 0 ? body.size() : 1));
        if (!body.empty()) {
            std::memcpy(copy_buf, body.data(), body.size());
        }
        GInputStream* stream = g_memory_input_stream_new_from_data(
            copy_buf, static_cast<gssize>(body.size()), g_free);
        webkit_uri_scheme_request_finish(request, stream, static_cast<gint64>(body.size()), mime.c_str());
        g_object_unref(stream);
    }

    static void linux_script_msg_cb(WebKitUserContentManager* ucm, WebKitJavascriptResult* js_result, gpointer user_data) {
        (void)ucm;
        auto* st = static_cast<detail::AppState*>(user_data);
        if (!st || !js_result) return;
        JSCValue* value = webkit_javascript_result_get_js_value(js_result);
        if (value && jsc_value_is_string(value)) {
            char* str_val = jsc_value_to_string(value);
            if (str_val) {
                st->handle_ipc_message(str_val);
                g_free(str_val);
            }
        }
    }

    static void linux_destroy_cb(GtkWidget* widget, gpointer user_data) {
        (void)widget;
        auto* st = static_cast<detail::AppState*>(user_data);
        if (st) st->quit_requested.store(true, std::memory_order_relaxed);
        gtk_main_quit();
    }

    struct LinuxEvalPayload {
        WebKitWebView* webview;
        std::string js;
    };

    static gboolean linux_idle_eval(gpointer data) {
        auto* p = static_cast<LinuxEvalPayload*>(data);
        if (p && p->webview) {
            webkit_web_view_evaluate_javascript(p->webview, p->js.c_str(), -1, nullptr, nullptr, nullptr, nullptr, nullptr);
        }
        delete p;
        return G_SOURCE_REMOVE;
    }

    void run_linux_webkitgtk() {
        if (!gtk_init_check(nullptr, nullptr)) {
            return;
        }
        GtkWidget* window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
        gtk_window_set_title(GTK_WINDOW(window), state_->title.c_str());
        gtk_window_set_default_size(GTK_WINDOW(window), state_->width, state_->height);
        gtk_window_set_position(GTK_WINDOW(window), GTK_WIN_POS_CENTER);
        if (state_->frameless) {
            gtk_window_set_decorated(GTK_WINDOW(window), FALSE);
        }
        g_signal_connect(window, "destroy", G_CALLBACK(linux_destroy_cb), state_.get());

        WebKitWebContext* context = webkit_web_context_get_default();
        webkit_web_context_register_uri_scheme(context, "app", linux_scheme_cb, state_.get(), nullptr);

        WebKitUserContentManager* ucm = webkit_user_content_manager_new();
        WebKitUserScript* preload = webkit_user_script_new(
            std::string(detail::kPreloadBridgeJs).c_str(),
            WEBKIT_USER_CONTENT_INJECT_ALL_FRAMES,
            WEBKIT_USER_SCRIPT_INJECT_AT_DOCUMENT_START,
            nullptr,
            nullptr);
        webkit_user_content_manager_add_script(ucm, preload);
        webkit_user_script_unref(preload);

        g_signal_connect(ucm, "script-message-received::external", G_CALLBACK(linux_script_msg_cb), state_.get());
        webkit_user_content_manager_register_script_message_handler(ucm, "external");

        GtkWidget* webview_widget = webkit_web_view_new_with_user_content_manager(ucm);
        WebKitWebView* webview = WEBKIT_WEB_VIEW(webview_widget);
        gtk_container_add(GTK_CONTAINER(window), webview_widget);

        {
            std::lock_guard<std::recursive_mutex> lock(state_->mtx);
            state_->platform_eval = [webview](const std::string& js) {
                auto* payload = new LinuxEvalPayload{webview, js};
                g_idle_add_full(G_PRIORITY_DEFAULT, linux_idle_eval, payload, nullptr);
            };
            state_->platform_set_title = [window](const std::string& new_title) {
                gtk_window_set_title(GTK_WINDOW(window), new_title.c_str());
            };
            state_->platform_set_size = [window](std::int32_t w, std::int32_t h) {
                gtk_window_resize(GTK_WINDOW(window), w, h);
            };
            state_->platform_fullscreen = [window, st = state_]() {
                if (st->is_fullscreen) gtk_window_fullscreen(GTK_WINDOW(window));
                else gtk_window_unfullscreen(GTK_WINDOW(window));
            };
            state_->platform_minimize = [window]() {
                gtk_window_iconify(GTK_WINDOW(window));
            };
            state_->platform_maximize = [window, st = state_]() {
                if (st->is_maximized) gtk_window_maximize(GTK_WINDOW(window));
                else gtk_window_unmaximize(GTK_WINDOW(window));
            };
            state_->platform_quit = []() {
                gtk_main_quit();
            };
        }

        webkit_web_view_load_uri(webview, "app://localhost/index.html");
        gtk_widget_show_all(window);
        gtk_main();

        {
            std::lock_guard<std::recursive_mutex> lock(state_->mtx);
            state_->platform_eval = nullptr;
            state_->platform_set_title = nullptr;
            state_->platform_set_size = nullptr;
            state_->platform_fullscreen = nullptr;
            state_->platform_minimize = nullptr;
            state_->platform_maximize = nullptr;
            state_->platform_quit = nullptr;
        }
    }
#endif

private:
    std::shared_ptr<detail::AppState> state_;

    void register_default_serve_dirs() {
        auto exe = detail::executable_dir();
        state_->serve_dirs.push_back(exe / "ui");
        state_->serve_dirs.push_back(exe / ".." / "Resources" / "ui");
        std::error_code ec;
        auto cwd = std::filesystem::current_path(ec);
        if (!ec) {
            state_->serve_dirs.push_back(cwd / "ui");
        }
    }
};

[[nodiscard]] inline auto new_app(std::string_view title = "cfire WebUI",
                                  std::int32_t width = 960,
                                  std::int32_t height = 640) -> App
{
    return App(title, width, height);
}

} // namespace cfire::webui

namespace cfire::core {
namespace webui = ::cfire::webui;
} // namespace cfire::core

namespace webui = ::cfire::webui;

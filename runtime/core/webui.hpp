#pragma once

// cfire Standard Library — Native Desktop & Mobile WebUI Engine (`ext:webui`)
// Modularized across:
//   - runtime/core/webui/bridge.hpp           (AppState, VFS, RPC/Event bus, preload JS bridge)
//   - runtime/core/webui/macos_ios.hpp        (macOS Cocoa + WKWebView & iOS UIKit + WKWebView)
//   - runtime/core/webui/linux_gtk.hpp        (Linux GTK3 + WebKitGTK)
//   - runtime/core/webui/windows_webview2.hpp (Windows Win32 + WebView2)

#include "webui/bridge.hpp"
#include "webui/macos_ios.hpp"
#include "webui/linux_gtk.hpp"
#include "webui/windows_webview2.hpp"

namespace cfire::webui {

namespace detail {
template <typename Fn, typename... Args>
[[nodiscard]] inline auto invoke_and_marshal_rpc(Fn&& f, Args&&... args) -> std::expected<std::string, cfire::Error> {
    using Ret = std::invoke_result_t<Fn, Args...>;
    if constexpr (std::is_void_v<Ret>) {
        std::forward<Fn>(f)(std::forward<Args>(args)...);
        return std::string("null");
    } else if constexpr (is_expected<Ret>::value) {
        auto res = std::forward<Fn>(f)(std::forward<Args>(args)...);
        if (!res) return std::unexpected(res.error());
        using ValT = typename Ret::value_type;
        if constexpr (std::is_void_v<ValT>) {
            return std::string("null");
        } else {
            return cfire::json::stringify(*res);
        }
    } else {
        auto res = std::forward<Fn>(f)(std::forward<Args>(args)...);
        return cfire::json::stringify(res);
    }
}
} // namespace detail

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
                return detail::invoke_and_marshal_rpc(f, *parsed);
            };
    }

    template <typename Fn>
    void bind_action(std::string_view name, Fn&& fn) {
        std::lock_guard<std::recursive_mutex> lock(state_->mtx);
        state_->rpc_handlers[std::string(name)] =
            [f = std::forward<Fn>(fn)](std::string_view params_json) mutable -> std::expected<std::string, cfire::Error> {
                if constexpr (std::is_invocable_v<decltype(f)&>) {
                    return detail::invoke_and_marshal_rpc(f);
                } else {
                    std::string arg(params_json.empty() ? "{}" : params_json);
                    return detail::invoke_and_marshal_rpc(f, arg);
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

    template <typename ReqStruct, typename Fn>
    void bind_async(std::string_view name, Fn&& fn) {
        std::lock_guard<std::recursive_mutex> lock(state_->mtx);
        auto shared_fn = std::make_shared<std::decay_t<Fn>>(std::forward<Fn>(fn));
        state_->async_rpc_handlers[std::string(name)] =
            [shared_fn](std::string_view params_json, std::function<void(std::expected<std::string, cfire::Error>)> reply) {
                std::string payload = params_json.empty() ? "{}" : std::string(params_json);
                std::thread([shared_fn, payload = std::move(payload), reply = std::move(reply)]() mutable {
                    auto parsed = cfire::json::parse<ReqStruct>(payload);
                    if (!parsed) {
                        reply(std::unexpected(parsed.error()));
                        return;
                    }
                    reply(detail::invoke_and_marshal_rpc(*shared_fn, *parsed));
                }).detach();
            };
    }

    template <typename Fn>
    void bind_action_async(std::string_view name, Fn&& fn) {
        std::lock_guard<std::recursive_mutex> lock(state_->mtx);
        auto shared_fn = std::make_shared<std::decay_t<Fn>>(std::forward<Fn>(fn));
        state_->async_rpc_handlers[std::string(name)] =
            [shared_fn](std::string_view params_json, std::function<void(std::expected<std::string, cfire::Error>)> reply) {
                std::string payload = params_json.empty() ? "{}" : std::string(params_json);
                std::thread([shared_fn, payload = std::move(payload), reply = std::move(reply)]() mutable {
                    if constexpr (std::is_invocable_v<decltype(*shared_fn)&>) {
                        reply(detail::invoke_and_marshal_rpc(*shared_fn));
                    } else {
                        reply(detail::invoke_and_marshal_rpc(*shared_fn, payload));
                    }
                }).detach();
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
            state_->record_emitted(event, json_val);
        }
        std::string ev_escaped = cfire::json::escape_string(event);
        state_->eval_js(std::format("window.__cfire_event_dispatch(\"{}\", {});", ev_escaped, json_val));
    }

    void emit_bytes(std::string_view event, std::span<const std::byte> data) {
        std::string b64 = cfire::detail::base64_encode(data);
        {
            std::lock_guard<std::recursive_mutex> lock(state_->mtx);
            state_->record_emitted(event, b64);
        }
        std::string ev_escaped = cfire::json::escape_string(event);
        state_->eval_js(std::format("window.__cfire_event_dispatch_bytes(\"{}\", \"{}\");", ev_escaped, b64));
    }

    void emit_bytes(std::string_view event, std::span<const std::uint8_t> data) {
        emit_bytes(event, std::as_bytes(data));
    }

    void clear_history() {
        std::lock_guard<std::recursive_mutex> lock(state_->mtx);
        state_->emitted_history.clear();
        state_->eval_history.clear();
    }

    void set_max_history(std::size_t max_entries) {
        std::lock_guard<std::recursive_mutex> lock(state_->mtx);
        state_->max_history_entries = max_entries;
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
            state_->record_emitted("state:change", json_obj);
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

    // --- Window Chrome, Menus, Icon, Lifecycle & Appearance Controls (`CF-051`, `CF-052`) ---

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

    void set_icon(std::string_view icon_path) {
        std::lock_guard<std::recursive_mutex> lock(state_->mtx);
        state_->icon_path = std::string(icon_path);
    }

    [[nodiscard]] auto icon() const -> std::string {
        std::lock_guard<std::recursive_mutex> lock(state_->mtx);
        return state_->icon_path;
    }

    void add_menu_item(std::string_view menu, std::string_view title, std::string_view shortcut, std::string_view event) {
        std::lock_guard<std::recursive_mutex> lock(state_->mtx);
        state_->custom_menus.push_back(detail::MenuItemSpec{
            .menu = std::string(menu),
            .title = std::string(title),
            .shortcut = std::string(shortcut),
            .event = std::string(event)
        });
    }

    [[nodiscard]] auto menu_items_count() const -> std::size_t {
        std::lock_guard<std::recursive_mutex> lock(state_->mtx);
        return state_->custom_menus.size();
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

    // --- Native Clipboard & System Fonts (`CF-045`, `CF-053`) ---

    void clipboard_set(std::string_view text, bool concealed = false, std::int64_t clear_after_ms = 0) const {
        std::uint64_t gen = 0;
        {
            std::lock_guard<std::recursive_mutex> lock(state_->mtx);
            state_->kv_state["__clipboard"] = std::string(text);
            state_->last_clipboard_concealed = concealed;
            gen = ++state_->clipboard_generation;
        }
        bool is_headless = false;
        if (const char* env_h = std::getenv("CFIRE_HEADLESS_TEST"); env_h && std::string_view(env_h) == "1") {
            is_headless = true;
        }

#if defined(__APPLE__) && defined(__OBJC__) && !CFIRE_WEBUI_IOS
        NSInteger savedChangeCount = 0;
        if (!is_headless) {
            NSPasteboard* pb = [NSPasteboard generalPasteboard];
            [pb clearContents];
            NSString* ns = [NSString stringWithUTF8String:std::string(text).c_str()];
            if (ns) [pb setString:ns forType:NSPasteboardTypeString];
            if (concealed) {
                NSData* emptyData = [NSData data];
                [pb setData:emptyData forType:@"org.nspasteboard.ConcealedType"];
                [pb setData:emptyData forType:@"org.nspasteboard.TransientType"];
                [pb setData:emptyData forType:@"org.nspasteboard.AutoGeneratedType"];
            }
            savedChangeCount = [pb changeCount];
        }
#elif defined(CFIRE_HAS_LINUX_WEBKITGTK) && !defined(CFIRE_TEST_LINUX_WEBKITGTK_BACKEND)
        if (!is_headless && gtk_init_check(nullptr, nullptr)) {
            GtkClipboard* cb = gtk_clipboard_get(GDK_SELECTION_CLIPBOARD);
            if (cb) gtk_clipboard_set_text(cb, std::string(text).c_str(), static_cast<gint>(text.size()));
        }
#elif defined(_WIN32)
        if (!is_headless) {
            detail::win32_clipboard_set(text, concealed);
        }
#endif

        if (clear_after_ms > 0) {
            auto st = state_;
#if defined(__APPLE__) && defined(__OBJC__) && !CFIRE_WEBUI_IOS
            std::thread([st, gen, clear_after_ms, is_headless, savedChangeCount]() {
                std::this_thread::sleep_for(std::chrono::milliseconds(clear_after_ms));
                {
                    std::lock_guard<std::recursive_mutex> lock(st->mtx);
                    if (st->clipboard_generation != gen) return;
                    st->kv_state["__clipboard"] = "";
                    st->last_clipboard_concealed = false;
                }
                if (!is_headless) {
                    dispatch_async(dispatch_get_main_queue(), ^{
                        NSPasteboard* pb = [NSPasteboard generalPasteboard];
                        if ([pb changeCount] == savedChangeCount) {
                            [pb clearContents];
                        }
                    });
                }
            }).detach();
#else
            std::thread([st, gen, clear_after_ms, is_headless]() {
                std::this_thread::sleep_for(std::chrono::milliseconds(clear_after_ms));
                {
                    std::lock_guard<std::recursive_mutex> lock(st->mtx);
                    if (st->clipboard_generation != gen) return;
                    st->kv_state["__clipboard"] = "";
                    st->last_clipboard_concealed = false;
                }
                (void)is_headless;
#if defined(CFIRE_HAS_LINUX_WEBKITGTK) && !defined(CFIRE_TEST_LINUX_WEBKITGTK_BACKEND)
                if (!is_headless && gtk_init_check(nullptr, nullptr)) {
                    GtkClipboard* cb = gtk_clipboard_get(GDK_SELECTION_CLIPBOARD);
                    if (cb) gtk_clipboard_set_text(cb, "", 0);
                }
#elif defined(_WIN32)
                if (!is_headless) {
                    detail::win32_clipboard_set("", false);
                }
#endif
            }).detach();
#endif
        }
    }

    void clipboard_set_secret(std::string_view text, std::int64_t clear_after_ms = 30000) const {
        clipboard_set(text, true, clear_after_ms);
    }

    [[nodiscard]] auto is_clipboard_concealed() const -> bool {
        std::lock_guard<std::recursive_mutex> lock(state_->mtx);
        return state_->last_clipboard_concealed;
    }

    [[nodiscard]] auto clipboard_get() const -> std::string {
        if (const char* env_h = std::getenv("CFIRE_HEADLESS_TEST"); env_h && std::string_view(env_h) == "1") {
            std::lock_guard<std::recursive_mutex> lock(state_->mtx);
            if (auto it = state_->kv_state.find("__clipboard"); it != state_->kv_state.end()) {
                return it->second;
            }
            return "";
        }
#if defined(__APPLE__) && defined(__OBJC__) && !CFIRE_WEBUI_IOS
        NSPasteboard* pb = [NSPasteboard generalPasteboard];
        NSString* ns = [pb stringForType:NSPasteboardTypeString];
        if (ns) return std::string([ns UTF8String]);
#elif defined(CFIRE_HAS_LINUX_WEBKITGTK) && !defined(CFIRE_TEST_LINUX_WEBKITGTK_BACKEND)
        if (gtk_init_check(nullptr, nullptr)) {
            GtkClipboard* cb = gtk_clipboard_get(GDK_SELECTION_CLIPBOARD);
            if (cb) {
                gchar* txt = gtk_clipboard_wait_for_text(cb);
                if (txt) {
                    std::string out(txt);
                    g_free(txt);
                    return out;
                }
            }
        }
#elif defined(_WIN32)
        std::string wclip = detail::win32_clipboard_get();
        if (!wclip.empty()) return wclip;
#endif
        std::lock_guard<std::recursive_mutex> lock(state_->mtx);
        if (auto it = state_->kv_state.find("__clipboard"); it != state_->kv_state.end()) {
            return it->second;
        }
        return "";
    }

    [[nodiscard]] auto monospace_fonts() const -> std::vector<std::string> {
        std::vector<std::string> fonts;
#if defined(__APPLE__) && defined(__OBJC__) && !CFIRE_WEBUI_IOS
        NSFontManager* fm = [NSFontManager sharedFontManager];
        NSArray<NSString*>* names = [fm availableFontNamesWithTraits:NSFixedPitchFontMask];
        if (names) {
            for (NSString* name in names) {
                if (name) fonts.emplace_back([name UTF8String]);
            }
        }
#elif defined(__linux__)
        FILE* fp = ::popen("fc-list :spacing=mono family 2>/dev/null", "r");
        if (fp) {
            std::set<std::string> unique_families;
            char line[1024];
            while (std::fgets(line, sizeof(line), fp) != nullptr) {
                std::string_view sv(line);
                while (!sv.empty() && (sv.back() == '\n' || sv.back() == '\r' || sv.back() == ' ')) sv.remove_suffix(1);
                std::size_t pos = 0;
                while (pos < sv.size()) {
                    auto comma = sv.find(',', pos);
                    std::string_view part = (comma == std::string_view::npos) ? sv.substr(pos) : sv.substr(pos, comma - pos);
                    while (!part.empty() && part.front() == ' ') part.remove_prefix(1);
                    while (!part.empty() && part.back() == ' ') part.remove_suffix(1);
                    if (!part.empty()) unique_families.emplace(part);
                    if (comma == std::string_view::npos) break;
                    pos = comma + 1;
                }
            }
            ::pclose(fp);
            fonts.assign(unique_families.begin(), unique_families.end());
        }
#elif defined(_WIN32)
        fonts = detail::win32_monospace_fonts();
#endif
        if (fonts.empty()) {
            fonts = {"SF Mono", "Menlo", "Monaco", "JetBrains Mono", "Fira Code", "Consolas", "DejaVu Sans Mono", "monospace"};
        }
        return fonts;
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

    // --- Offscreen / Automated Live WebView Test Runner ---

    [[nodiscard]] auto run_webview_test(std::int32_t timeout_ms = 3000) -> std::expected<bool, cfire::Error> {
#if defined(__APPLE__) && defined(__OBJC__) && !CFIRE_WEBUI_IOS
        return detail::run_macos_webview_test(state_, timeout_ms);
#elif defined(CFIRE_HAS_LINUX_WEBKITGTK) && !defined(CFIRE_TEST_LINUX_WEBKITGTK_BACKEND)
        return detail::run_linux_webview_test(state_, timeout_ms);
#elif defined(_WIN32)
        detail::run_win32_webview2(state_, true, timeout_ms);
        return state_->quit_requested.load(std::memory_order_relaxed);
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
        detail::run_macos_app(state_);
#endif
#elif defined(CFIRE_HAS_LINUX_WEBKITGTK)
        detail::run_linux_webkitgtk(state_);
#elif defined(_WIN32)
        detail::run_win32_webview2(state_, false, 0);
#endif
    }

#if defined(CFIRE_HAS_LINUX_WEBKITGTK)
    void run_linux_webkitgtk() {
        detail::run_linux_webkitgtk(state_);
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

inline void clipboard_set(std::string_view text, bool concealed = false, std::int64_t clear_after_ms = 0) {
    App tmp;
    tmp.clipboard_set(text, concealed, clear_after_ms);
}

inline void clipboard_set_secret(std::string_view text, std::int64_t clear_after_ms = 30000) {
    App tmp;
    tmp.clipboard_set_secret(text, clear_after_ms);
}

[[nodiscard]] inline auto clipboard_get() -> std::string {
    App tmp;
    return tmp.clipboard_get();
}

[[nodiscard]] inline auto monospace_fonts() -> std::vector<std::string> {
    App tmp;
    return tmp.monospace_fonts();
}

} // namespace cfire::webui

namespace cfire::core {
namespace webui = ::cfire::webui;
} // namespace cfire::core

namespace webui = ::cfire::webui;

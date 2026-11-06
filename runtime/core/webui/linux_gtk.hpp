#pragma once

#include "bridge.hpp"

#if defined(CFIRE_HAS_LINUX_WEBKITGTK)

namespace cfire::webui::detail {

inline void linux_scheme_cb(WebKitURISchemeRequest* request, gpointer user_data) {
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

inline void linux_script_msg_cb(WebKitUserContentManager* ucm, WebKitJavascriptResult* js_result, gpointer user_data) {
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

inline void linux_destroy_cb(GtkWidget* widget, gpointer user_data) {
    (void)widget;
    auto* st = static_cast<detail::AppState*>(user_data);
    if (st) st->quit_requested.store(true, std::memory_order_relaxed);
    gtk_main_quit();
}

struct LinuxEvalPayload {
    WebKitWebView* webview;
    std::string js;
};

inline gboolean linux_idle_eval(gpointer data) {
    auto* p = static_cast<LinuxEvalPayload*>(data);
    if (p && p->webview) {
        webkit_web_view_evaluate_javascript(p->webview, p->js.c_str(), -1, nullptr, nullptr, nullptr, nullptr, nullptr);
    }
    delete p;
    return G_SOURCE_REMOVE;
}

inline void run_linux_webkitgtk(const std::shared_ptr<AppState>& state_) {
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
#if !defined(CFIRE_TEST_LINUX_WEBKITGTK_BACKEND)
    if (!state_->icon_path.empty()) {
        gtk_window_set_icon_from_file(GTK_WINDOW(window), state_->icon_path.c_str(), nullptr);
    }
#endif
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

#if !defined(CFIRE_TEST_LINUX_WEBKITGTK_BACKEND)
[[nodiscard]] inline auto run_linux_webview_test(
    const std::shared_ptr<AppState>& state_,
    std::int32_t timeout_ms
) -> std::expected<bool, cfire::Error> {
    if (!gtk_init_check(nullptr, nullptr)) {
        return true;
    }
    state_->quit_requested.store(false, std::memory_order_relaxed);
    GtkWidget* window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_default_size(GTK_WINDOW(window), state_->width, state_->height);
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
        state_->platform_quit = [st = state_]() {
            st->quit_requested.store(true, std::memory_order_relaxed);
        };
    }

    webkit_web_view_load_uri(webview, "app://localhost/index.html");
    gtk_widget_show_all(window);

    auto start = std::chrono::steady_clock::now();
    while (!state_->quit_requested.load(std::memory_order_relaxed)) {
        gtk_main_iteration_do(FALSE);
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start).count();
        if (elapsed >= timeout_ms) {
            break;
        }
    }

    {
        std::lock_guard<std::recursive_mutex> lock(state_->mtx);
        state_->platform_eval = nullptr;
        state_->platform_quit = nullptr;
    }
    gtk_widget_destroy(window);
    return state_->quit_requested.load(std::memory_order_relaxed);
}
#endif

} // namespace cfire::webui::detail

#endif // defined(CFIRE_HAS_LINUX_WEBKITGTK)

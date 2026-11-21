#pragma once

#include "bridge.hpp"

#if defined(CFIRE_HAS_LINUX_WEBKITGTK)
#if !defined(CFIRE_TEST_LINUX_WEBKITGTK_BACKEND)
#include "../os.hpp"
#endif

namespace cfire::webui::detail {

inline void linux_open_external_url(std::string_view url) {
    if (url.empty() || is_headless_webui_env()) return;
#if !defined(CFIRE_TEST_LINUX_WEBKITGTK_BACKEND)
    (void)cfire::os::open_url(url);
#else
    (void)url;
#endif
}

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
    WebKitWebView* wv = nullptr;
    {
        std::lock_guard<std::recursive_mutex> lock(st->mtx);
        wv = static_cast<WebKitWebView*>(st->native_webview_handle);
    }
    const char* uri_c = wv ? webkit_web_view_get_uri(wv) : nullptr;
    std::string_view source_uri = uri_c ? std::string_view(uri_c) : std::string_view{};
    JSCValue* value = webkit_javascript_result_get_js_value(js_result);
    if (value && jsc_value_is_string(value)) {
        char* str_val = jsc_value_to_string(value);
        if (str_val) {
            st->handle_ipc_message_verified(source_uri, str_val, true);
            g_free(str_val);
        }
    }
}

inline gboolean linux_decide_policy_cb(
    WebKitWebView* webview,
    WebKitPolicyDecision* decision,
    WebKitPolicyDecisionType decision_type,
    gpointer user_data
) {
    auto* st = static_cast<detail::AppState*>(user_data);
    if (decision_type != WEBKIT_POLICY_DECISION_TYPE_NAVIGATION_ACTION &&
        decision_type != WEBKIT_POLICY_DECISION_TYPE_NEW_WINDOW_ACTION) {
        return FALSE;
    }
    if (!decision) return FALSE;
    WebKitNavigationPolicyDecision* nav_dec = WEBKIT_NAVIGATION_POLICY_DECISION(decision);
    WebKitNavigationAction* nav_act = nav_dec ? webkit_navigation_policy_decision_get_navigation_action(nav_dec) : nullptr;
    WebKitURIRequest* req = nav_act ? webkit_navigation_action_get_request(nav_act) : nullptr;
    const char* uri_c = req ? webkit_uri_request_get_uri(req) : nullptr;
    if (!uri_c || uri_c[0] == '\0') return FALSE;

    const bool allowed = st ? st->should_allow_in_webview(uri_c) : is_app_origin(uri_c);
    if (!allowed) {
        webkit_policy_decision_ignore(decision);
        if (st) st->notify_external_navigation(uri_c);
        linux_open_external_url(uri_c);
        return TRUE;
    }
    if (decision_type == WEBKIT_POLICY_DECISION_TYPE_NEW_WINDOW_ACTION && webview) {
        webkit_web_view_load_uri(webview, uri_c);
        webkit_policy_decision_ignore(decision);
        return TRUE;
    }
    return FALSE;
}

inline GtkWidget* linux_create_cb(
    WebKitWebView* webview,
    WebKitNavigationAction* navigation_action,
    gpointer user_data
) {
    auto* st = static_cast<detail::AppState*>(user_data);
    WebKitURIRequest* req = navigation_action ? webkit_navigation_action_get_request(navigation_action) : nullptr;
    const char* uri_c = req ? webkit_uri_request_get_uri(req) : nullptr;
    if (uri_c && uri_c[0] != '\0') {
        const bool allowed = st ? st->should_allow_in_webview(uri_c) : is_app_origin(uri_c);
        if (allowed) {
            if (webview) webkit_web_view_load_uri(webview, uri_c);
        } else {
            if (st) st->notify_external_navigation(uri_c);
            linux_open_external_url(uri_c);
        }
    }
    return nullptr;
}

inline gboolean linux_focus_in_cb(GtkWidget* /*widget*/, GdkEventFocus* /*event*/, gpointer user_data) {
    auto* st = static_cast<detail::AppState*>(user_data);
    if (st) st->emit_system_event("app:active", "{}");
    return FALSE;
}

inline gboolean linux_focus_out_cb(GtkWidget* /*widget*/, GdkEventFocus* /*event*/, gpointer user_data) {
    auto* st = static_cast<detail::AppState*>(user_data);
    if (st) st->emit_system_event("app:inactive", "{}");
    return FALSE;
}

inline void linux_destroy_cb(GtkWidget* widget, gpointer user_data) {
    (void)widget;
    auto* st = static_cast<detail::AppState*>(user_data);
    if (st) st->quit_requested.store(true, std::memory_order_relaxed);
    gtk_main_quit();
}

#if !defined(CFIRE_TEST_LINUX_WEBKITGTK_BACKEND)
inline gboolean linux_delete_event_cb(GtkWidget* widget, GdkEvent* /*event*/, gpointer user_data) {
    auto* st = static_cast<detail::AppState*>(user_data);
    if (st && st->hide_on_close && !st->quit_requested.load(std::memory_order_relaxed)) {
        {
            std::lock_guard<std::recursive_mutex> lock(st->mtx);
            st->window_hidden = true;
        }
        gtk_widget_hide(widget);
        st->emit_system_event("window:close_requested", "{}");
        return TRUE;
    }
    return FALSE;
}

struct LinuxTrayMenuItemCtx {
    detail::AppState* st;
    std::string event;
};

inline void linux_tray_item_activate_cb(GtkMenuItem* /*item*/, gpointer user_data) {
    auto* ctx = static_cast<LinuxTrayMenuItemCtx*>(user_data);
    if (ctx && ctx->st) {
        ctx->st->record_emitted(ctx->event, "{}");
        ctx->st->dispatch_event_internal(ctx->event, "{}");
        std::string ev_escaped = cfire::json::escape_string(ctx->event);
        ctx->st->eval_js(std::format("window.__cfire_event_dispatch(\"{}\", {{}});", ev_escaped));
    }
}

G_GNUC_BEGIN_IGNORE_DEPRECATIONS
inline void linux_tray_popup_cb(GtkStatusIcon* status_icon, guint button, guint activate_time, gpointer user_data) {
    auto* menu = static_cast<GtkMenu*>(user_data);
    if (menu) {
        gtk_menu_popup(menu, nullptr, nullptr, gtk_status_icon_position_menu, status_icon, button, activate_time);
    }
}
G_GNUC_END_IGNORE_DEPRECATIONS

struct LinuxSecretClipboardData {
    std::string text;
};

inline void linux_secret_clipboard_get_cb(GtkClipboard* /*cb*/, GtkSelectionData* selection_data, guint info, gpointer user_data) {
    auto* d = static_cast<LinuxSecretClipboardData*>(user_data);
    if (!d) return;
    if (info == 1) {
        GdkAtom atom = gdk_atom_intern_static_string("x-kde-passwordManagerHint");
        const guchar secret_hint[] = "secret";
        gtk_selection_data_set(selection_data, atom, 8, secret_hint, 6);
    } else {
        gtk_selection_data_set_text(selection_data, d->text.c_str(), static_cast<gint>(d->text.size()));
    }
}

inline void linux_secret_clipboard_clear_cb(GtkClipboard* /*cb*/, gpointer user_data) {
    auto* d = static_cast<LinuxSecretClipboardData*>(user_data);
    if (d) {
        if (!d->text.empty()) {
            volatile char* p = d->text.data();
            for (std::size_t i = 0; i < d->text.size(); ++i) p[i] = 0;
        }
        delete d;
    }
}

inline void linux_clipboard_set(std::string_view text, bool concealed) {
    if (!gtk_init_check(nullptr, nullptr)) return;
    GtkClipboard* cb = gtk_clipboard_get(GDK_SELECTION_CLIPBOARD);
    if (!cb) return;
    if (!concealed) {
        gtk_clipboard_set_text(cb, std::string(text).c_str(), static_cast<gint>(text.size()));
        return;
    }
    static GtkTargetEntry targets[] = {
        {const_cast<gchar*>("UTF8_STRING"), 0, 0},
        {const_cast<gchar*>("COMPOUND_TEXT"), 0, 0},
        {const_cast<gchar*>("TEXT"), 0, 0},
        {const_cast<gchar*>("STRING"), 0, 0},
        {const_cast<gchar*>("text/plain;charset=utf-8"), 0, 0},
        {const_cast<gchar*>("text/plain"), 0, 0},
        {const_cast<gchar*>("x-kde-passwordManagerHint"), 0, 1},
    };
    auto* d = new LinuxSecretClipboardData{std::string(text)};
    if (!gtk_clipboard_set_with_data(
            cb,
            targets,
            static_cast<guint>(sizeof(targets) / sizeof(targets[0])),
            linux_secret_clipboard_get_cb,
            linux_secret_clipboard_clear_cb,
            d)) {
        delete d;
    }
}

inline gboolean linux_idle_clear_clipboard(gpointer /*data*/) {
    if (gtk_init_check(nullptr, nullptr)) {
        GtkClipboard* cb = gtk_clipboard_get(GDK_SELECTION_CLIPBOARD);
        if (cb) gtk_clipboard_set_text(cb, "", 0);
    }
    return G_SOURCE_REMOVE;
}
#endif

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

struct LinuxDevtoolsPayload {
    WebKitWebView* webview;
    bool enabled;
};

inline gboolean linux_idle_set_devtools(gpointer data) {
    auto* p = static_cast<LinuxDevtoolsPayload*>(data);
    if (p && p->webview) {
        if (WebKitSettings* wks = webkit_web_view_get_settings(p->webview)) {
            webkit_settings_set_enable_developer_extras(wks, p->enabled ? TRUE : FALSE);
        }
    }
    delete p;
    return G_SOURCE_REMOVE;
}

inline gboolean linux_context_menu_cb(
    WebKitWebView* /*webview*/,
    WebKitContextMenu* /*menu*/,
    GdkEvent* /*event*/,
    WebKitHitTestResult* /*hit_test*/,
    gpointer user_data
) {
    auto* st = static_cast<detail::AppState*>(user_data);
    if (!st) return FALSE;
    bool allow = kDefaultWebUIDevExtras;
    {
        std::lock_guard<std::recursive_mutex> lock(st->mtx);
        allow = st->context_menu_enabled;
    }
    return allow ? FALSE : TRUE;
}

inline gboolean linux_window_state_event_cb(
    GtkWidget* /*widget*/,
    GdkEventWindowState* event,
    gpointer user_data
) {
    auto* st = static_cast<detail::AppState*>(user_data);
    if (st && event && (event->changed_mask & GDK_WINDOW_STATE_FULLSCREEN)) {
        st->notify_fullscreen_changed((event->new_window_state & GDK_WINDOW_STATE_FULLSCREEN) != 0);
    }
    return FALSE;
}

inline void linux_drag_data_received_cb(
    GtkWidget* widget,
    GdkDragContext* context,
    gint /*x*/,
    gint /*y*/,
    GtkSelectionData* selection_data,
    guint /*info*/,
    guint time,
    gpointer user_data
) {
    auto* st = static_cast<detail::AppState*>(user_data);
    char** uris = selection_data ? gtk_selection_data_get_uris(selection_data) : nullptr;
    if (!uris) return;

    std::vector<std::string> paths;
    for (int i = 0; uris[i] != nullptr; ++i) {
        char* local_path = g_filename_from_uri(uris[i], nullptr, nullptr);
        if (local_path) {
            if (local_path[0] != '\0') paths.emplace_back(local_path);
            g_free(local_path);
        }
    }
    g_strfreev(uris);
    if (!paths.empty()) {
        if (st) st->notify_file_drop(paths);
        gtk_drag_finish(context, TRUE, FALSE, time);
        if (widget) g_signal_stop_emission_by_name(widget, "drag-data-received");
    }
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
    g_signal_connect(window, "delete-event", G_CALLBACK(linux_delete_event_cb), state_.get());
    std::vector<std::unique_ptr<LinuxTrayMenuItemCtx>> tray_ctxs;
    GtkWidget* tray_menu = nullptr;
    G_GNUC_BEGIN_IGNORE_DEPRECATIONS
    GtkStatusIcon* tray_icon = nullptr;
    if (state_->has_tray) {
        if (!state_->tray_icon_path.empty()) {
            tray_icon = gtk_status_icon_new_from_file(state_->tray_icon_path.c_str());
        } else {
            tray_icon = gtk_status_icon_new_from_icon_name("application-x-executable");
        }
        if (tray_icon) {
            if (!state_->tray_tooltip.empty()) {
                gtk_status_icon_set_tooltip_text(tray_icon, state_->tray_tooltip.c_str());
            }
            gtk_status_icon_set_visible(tray_icon, TRUE);
            if (!state_->tray_items.empty()) {
                tray_menu = gtk_menu_new();
                std::lock_guard<std::recursive_mutex> lock(state_->mtx);
                for (const auto& titem : state_->tray_items) {
                    if (titem.title == "-") {
                        GtkWidget* sep = gtk_separator_menu_item_new();
                        gtk_menu_shell_append(GTK_MENU_SHELL(tray_menu), sep);
                        continue;
                    }
                    GtkWidget* mi = gtk_menu_item_new_with_label(titem.title.c_str());
                    auto ctx = std::make_unique<LinuxTrayMenuItemCtx>(LinuxTrayMenuItemCtx{state_.get(), titem.event});
                    g_signal_connect(mi, "activate", G_CALLBACK(linux_tray_item_activate_cb), ctx.get());
                    tray_ctxs.push_back(std::move(ctx));
                    gtk_menu_shell_append(GTK_MENU_SHELL(tray_menu), mi);
                }
                gtk_widget_show_all(tray_menu);
                g_signal_connect(tray_icon, "popup-menu", G_CALLBACK(linux_tray_popup_cb), tray_menu);
            }
        }
    }
    G_GNUC_END_IGNORE_DEPRECATIONS
#endif
    g_signal_connect(window, "destroy", G_CALLBACK(linux_destroy_cb), state_.get());
    g_signal_connect(window, "window-state-event", G_CALLBACK(linux_window_state_event_cb), state_.get());
    g_signal_connect(window, "focus-in-event", G_CALLBACK(linux_focus_in_cb), state_.get());
    g_signal_connect(window, "focus-out-event", G_CALLBACK(linux_focus_out_cb), state_.get());

    WebKitWebContext* context = webkit_web_context_get_default();
    webkit_web_context_register_uri_scheme(context, "app", linux_scheme_cb, state_.get(), nullptr);

    WebKitUserContentManager* ucm = webkit_user_content_manager_new();
    static const char* const kWebkitAllowList[] = {"app://localhost/*", nullptr};
    std::string preload_cpp = detail::build_preload_script(*state_, true);
    WebKitUserScript* preload = webkit_user_script_new(
        preload_cpp.c_str(),
        WEBKIT_USER_CONTENT_INJECT_TOP_FRAME,
        WEBKIT_USER_SCRIPT_INJECT_AT_DOCUMENT_START,
        kWebkitAllowList,
        nullptr);
    webkit_user_content_manager_add_script(ucm, preload);
    webkit_user_script_unref(preload);

    g_signal_connect(ucm, "script-message-received::external", G_CALLBACK(linux_script_msg_cb), state_.get());
    webkit_user_content_manager_register_script_message_handler(ucm, "external");

    GtkWidget* webview_widget = webkit_web_view_new_with_user_content_manager(ucm);
    WebKitWebView* webview = WEBKIT_WEB_VIEW(webview_widget);
    {
        std::lock_guard<std::recursive_mutex> lock(state_->mtx);
        state_->native_window_handle = window;
        state_->native_webview_handle = webview;
    }
    bool init_devtools = kDefaultWebUIDevExtras;
    {
        std::lock_guard<std::recursive_mutex> lock(state_->mtx);
        init_devtools = state_->devtools_enabled;
    }
    if (WebKitSettings* wks = webkit_web_view_get_settings(webview)) {
        webkit_settings_set_enable_developer_extras(wks, init_devtools ? TRUE : FALSE);
    }
    g_signal_connect(webview_widget, "context-menu", G_CALLBACK(linux_context_menu_cb), state_.get());
    g_signal_connect(webview_widget, "decide-policy", G_CALLBACK(linux_decide_policy_cb), state_.get());
    g_signal_connect(webview_widget, "create", G_CALLBACK(linux_create_cb), state_.get());
    gtk_drag_dest_add_uri_targets(webview_widget);
    g_signal_connect(webview_widget, "drag-data-received", G_CALLBACK(linux_drag_data_received_cb), state_.get());
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
#if !defined(CFIRE_TEST_LINUX_WEBKITGTK_BACKEND)
        state_->platform_set_dark_mode = [](bool dark) {
            if (GtkSettings* settings = gtk_settings_get_default()) {
                g_object_set(settings, "gtk-application-prefer-dark-theme", dark ? TRUE : FALSE, nullptr);
            }
        };
#endif
        state_->platform_set_devtools = [webview](bool enabled) {
            auto* payload = new LinuxDevtoolsPayload{webview, enabled};
            g_idle_add_full(G_PRIORITY_DEFAULT, linux_idle_set_devtools, payload, nullptr);
        };
        state_->platform_set_context_menu = [](bool) {};
        state_->platform_set_autofill = [webview](bool enabled) {
            auto* payload = new LinuxEvalPayload{
                webview,
                enabled ? "if(window.__cfire_set_autofill)window.__cfire_set_autofill(true);"
                        : "if(window.__cfire_set_autofill)window.__cfire_set_autofill(false);"
            };
            g_idle_add_full(G_PRIORITY_DEFAULT, linux_idle_eval, payload, nullptr);
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
#if !defined(CFIRE_TEST_LINUX_WEBKITGTK_BACKEND)
        state_->platform_hide_window = [window]() {
            gtk_widget_hide(window);
        };
        state_->platform_show_window = [window]() {
            gtk_widget_show_all(window);
            gtk_window_present(GTK_WINDOW(window));
        };
#endif
        state_->platform_quit = []() {
            gtk_main_quit();
        };
    }

    webkit_web_view_load_uri(webview, "app://localhost/index.html");
    gtk_widget_show_all(window);
    gtk_main();

#if !defined(CFIRE_TEST_LINUX_WEBKITGTK_BACKEND)
    if (tray_menu) gtk_widget_destroy(tray_menu);
    if (tray_icon) g_object_unref(tray_icon);
#endif
    {
        std::lock_guard<std::recursive_mutex> lock(state_->mtx);
        state_->native_window_handle = nullptr;
        state_->native_webview_handle = nullptr;
        state_->platform_eval = nullptr;
        state_->platform_set_title = nullptr;
        state_->platform_set_size = nullptr;
        state_->platform_set_dark_mode = nullptr;
        state_->platform_set_devtools = nullptr;
        state_->platform_set_context_menu = nullptr;
        state_->platform_set_autofill = nullptr;
        state_->platform_fullscreen = nullptr;
        state_->platform_minimize = nullptr;
        state_->platform_maximize = nullptr;
        state_->platform_hide_window = nullptr;
        state_->platform_show_window = nullptr;
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
    g_signal_connect(window, "window-state-event", G_CALLBACK(linux_window_state_event_cb), state_.get());
    g_signal_connect(window, "focus-in-event", G_CALLBACK(linux_focus_in_cb), state_.get());
    g_signal_connect(window, "focus-out-event", G_CALLBACK(linux_focus_out_cb), state_.get());

    WebKitUserContentManager* ucm = webkit_user_content_manager_new();
    static const char* const kWebkitAllowList[] = {"app://localhost/*", nullptr};
    std::string preload_cpp = detail::build_preload_script(*state_, true);
    WebKitUserScript* preload = webkit_user_script_new(
        preload_cpp.c_str(),
        WEBKIT_USER_CONTENT_INJECT_TOP_FRAME,
        WEBKIT_USER_SCRIPT_INJECT_AT_DOCUMENT_START,
        kWebkitAllowList,
        nullptr);
    webkit_user_content_manager_add_script(ucm, preload);
    webkit_user_script_unref(preload);

    g_signal_connect(ucm, "script-message-received::external", G_CALLBACK(linux_script_msg_cb), state_.get());
    webkit_user_content_manager_register_script_message_handler(ucm, "external");

    GtkWidget* webview_widget = webkit_web_view_new_with_user_content_manager(ucm);
    WebKitWebView* webview = WEBKIT_WEB_VIEW(webview_widget);
    {
        std::lock_guard<std::recursive_mutex> lock(state_->mtx);
        state_->native_window_handle = window;
        state_->native_webview_handle = webview;
    }
    bool init_devtools = kDefaultWebUIDevExtras;
    {
        std::lock_guard<std::recursive_mutex> lock(state_->mtx);
        init_devtools = state_->devtools_enabled;
    }
    if (WebKitSettings* wks = webkit_web_view_get_settings(webview)) {
        webkit_settings_set_enable_developer_extras(wks, init_devtools ? TRUE : FALSE);
    }
    g_signal_connect(webview_widget, "context-menu", G_CALLBACK(linux_context_menu_cb), state_.get());
    g_signal_connect(webview_widget, "decide-policy", G_CALLBACK(linux_decide_policy_cb), state_.get());
    g_signal_connect(webview_widget, "create", G_CALLBACK(linux_create_cb), state_.get());
    gtk_drag_dest_add_uri_targets(webview_widget);
    g_signal_connect(webview_widget, "drag-data-received", G_CALLBACK(linux_drag_data_received_cb), state_.get());
    gtk_container_add(GTK_CONTAINER(window), webview_widget);

    {
        std::lock_guard<std::recursive_mutex> lock(state_->mtx);
        state_->platform_eval = [webview](const std::string& js) {
            auto* payload = new LinuxEvalPayload{webview, js};
            g_idle_add_full(G_PRIORITY_DEFAULT, linux_idle_eval, payload, nullptr);
        };
        state_->platform_set_devtools = [webview](bool enabled) {
            auto* payload = new LinuxDevtoolsPayload{webview, enabled};
            g_idle_add_full(G_PRIORITY_DEFAULT, linux_idle_set_devtools, payload, nullptr);
        };
        state_->platform_set_context_menu = [](bool) {};
        state_->platform_set_autofill = [webview](bool enabled) {
            auto* payload = new LinuxEvalPayload{
                webview,
                enabled ? "if(window.__cfire_set_autofill)window.__cfire_set_autofill(true);"
                        : "if(window.__cfire_set_autofill)window.__cfire_set_autofill(false);"
            };
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
        state_->native_window_handle = nullptr;
        state_->native_webview_handle = nullptr;
        state_->platform_eval = nullptr;
        state_->platform_set_devtools = nullptr;
        state_->platform_set_context_menu = nullptr;
        state_->platform_set_autofill = nullptr;
        state_->platform_quit = nullptr;
    }
    gtk_widget_destroy(window);
    return state_->quit_requested.load(std::memory_order_relaxed);
}
#endif

} // namespace cfire::webui::detail

#endif // defined(CFIRE_HAS_LINUX_WEBKITGTK)

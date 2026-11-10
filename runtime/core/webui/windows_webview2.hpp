#pragma once

#include "bridge.hpp"

#if defined(CFIRE_HAS_WIN32_BACKEND)

#if defined(_WIN32)
#include <shlwapi.h>
#endif

namespace cfire::webui::detail {

[[nodiscard]] inline auto utf8_to_wide(std::string_view s) -> std::wstring {
    if (s.empty()) return L"";
    int len = ::MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    if (len <= 0) return L"";
    std::wstring out(static_cast<std::size_t>(len), L'\0');
    ::MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), out.data(), len);
    return out;
}

[[nodiscard]] inline auto wide_to_utf8(std::wstring_view w) -> std::string {
    if (w.empty()) return "";
    int len = ::WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
    if (len <= 0) return "";
    std::string out(static_cast<std::size_t>(len), '\0');
    ::WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), out.data(), len, nullptr, nullptr);
    return out;
}

inline void win32_clipboard_set(std::string_view text, bool concealed, HWND hwnd = nullptr) {
    HWND owner = hwnd ? hwnd : ::GetActiveWindow();
    if (!::OpenClipboard(owner)) return;
    ::EmptyClipboard();
    std::wstring w = utf8_to_wide(text);
    std::size_t bytes = (w.size() + 1) * sizeof(wchar_t);
    HGLOBAL hMem = ::GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (hMem) {
        void* ptr = ::GlobalLock(hMem);
        if (ptr) {
            std::memcpy(ptr, w.c_str(), bytes);
            ::GlobalUnlock(hMem);
            ::SetClipboardData(CF_UNICODETEXT, hMem);
        } else {
            ::GlobalFree(hMem);
        }
    }
    if (concealed) {
        UINT fmtExclude = ::RegisterClipboardFormatW(L"ExcludeClipboardContentFromMonitorProcessing");
        if (fmtExclude != 0) {
            HGLOBAL hEx = ::GlobalAlloc(GMEM_MOVEABLE, sizeof(DWORD));
            if (hEx) {
                auto* p = static_cast<DWORD*>(::GlobalLock(hEx));
                if (p) { *p = 1; ::GlobalUnlock(hEx); ::SetClipboardData(fmtExclude, hEx); }
                else ::GlobalFree(hEx);
            }
        }
        UINT fmtHistory = ::RegisterClipboardFormatW(L"CanIncludeInClipboardHistory");
        if (fmtHistory != 0) {
            HGLOBAL hHist = ::GlobalAlloc(GMEM_MOVEABLE, sizeof(DWORD));
            if (hHist) {
                auto* p = static_cast<DWORD*>(::GlobalLock(hHist));
                if (p) { *p = 0; ::GlobalUnlock(hHist); ::SetClipboardData(fmtHistory, hHist); }
                else ::GlobalFree(hHist);
            }
        }
    }
    ::CloseClipboard();
}

// Loads an icon from a file at the given size: an .ico through LoadImageW, or a .png
// through CreateIconFromResourceEx (PNG-compressed icon data supported since Windows Vista).
[[nodiscard]] inline auto win32_load_icon_file(const std::string& path, int cx, int cy) -> HICON {
    if (path.empty()) return nullptr;
    std::wstring wpath = utf8_to_wide(path);
#if defined(_WIN32)
    HANDLE f = ::CreateFileW(wpath.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f != INVALID_HANDLE_VALUE) {
        std::vector<BYTE> bytes;
        LARGE_INTEGER size{};
        if (::GetFileSizeEx(f, &size) && size.QuadPart > 8 && size.QuadPart < (16 << 20)) {
            bytes.resize(static_cast<std::size_t>(size.QuadPart));
            DWORD got = 0;
            if (!::ReadFile(f, bytes.data(), static_cast<DWORD>(bytes.size()), &got, nullptr) || got != bytes.size()) {
                bytes.clear();
            }
        }
        ::CloseHandle(f);
        static constexpr BYTE kPng[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
        if (bytes.size() > 8 && std::memcmp(bytes.data(), kPng, 8) == 0) {
            return ::CreateIconFromResourceEx(bytes.data(), static_cast<DWORD>(bytes.size()), TRUE, 0x00030000, cx, cy, LR_DEFAULTCOLOR);
        }
    }
#endif
    return static_cast<HICON>(::LoadImageW(nullptr, wpath.c_str(), IMAGE_ICON, cx, cy, LR_LOADFROMFILE));
}

inline constexpr DWORD kWin32DialogPathBufLen = 32768;

[[nodiscard]] inline auto win32_open_file_dialog(std::string_view dialog_title, HWND owner = nullptr) -> std::string {
    std::vector<wchar_t> file_buf(kWin32DialogPathBufLen, L'\0');
    std::wstring wtitle = utf8_to_wide(dialog_title);
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = owner ? owner : ::GetActiveWindow();
    ofn.lpstrFile = file_buf.data();
    ofn.nMaxFile = kWin32DialogPathBufLen;
    ofn.lpstrFilter = L"All Files (*.*)\0*.*\0";
    ofn.nFilterIndex = 1;
    ofn.lpstrTitle = wtitle.c_str();
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;
    if (::GetOpenFileNameW(&ofn)) {
        return wide_to_utf8(file_buf.data());
    }
    return "";
}

[[nodiscard]] inline auto win32_save_file_dialog(std::string_view dialog_title, std::string_view default_name, HWND owner = nullptr) -> std::string {
    std::vector<wchar_t> file_buf(kWin32DialogPathBufLen, L'\0');
    if (!default_name.empty()) {
        std::wstring wdef = utf8_to_wide(default_name);
        std::wcsncpy(file_buf.data(), wdef.c_str(), kWin32DialogPathBufLen - 1);
    }
    std::wstring wtitle = utf8_to_wide(dialog_title);
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = owner ? owner : ::GetActiveWindow();
    ofn.lpstrFile = file_buf.data();
    ofn.nMaxFile = kWin32DialogPathBufLen;
    ofn.lpstrFilter = L"All Files (*.*)\0*.*\0";
    ofn.nFilterIndex = 1;
    ofn.lpstrTitle = wtitle.c_str();
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_NOCHANGEDIR;
    if (::GetSaveFileNameW(&ofn)) {
        return wide_to_utf8(file_buf.data());
    }
    return "";
}

inline void win32_message_dialog(std::string_view dialog_title, std::string_view message, HWND owner = nullptr) {
    std::wstring wtitle = utf8_to_wide(dialog_title);
    std::wstring wmsg = utf8_to_wide(message);
    ::MessageBoxW(owner ? owner : ::GetActiveWindow(), wmsg.c_str(), wtitle.c_str(), MB_OK | MB_ICONINFORMATION);
}

[[nodiscard]] inline auto win32_clipboard_get() -> std::string {
    if (!::OpenClipboard(nullptr)) return "";
    std::string out;
    HANDLE hData = ::GetClipboardData(CF_UNICODETEXT);
    if (hData) {
        const auto* pw = static_cast<const wchar_t*>(::GlobalLock(hData));
        if (pw) {
            out = wide_to_utf8(pw);
            ::GlobalUnlock(hData);
        }
    }
    ::CloseClipboard();
    return out;
}

inline int CALLBACK win32_enum_mono_font_proc(
    const LOGFONTW* lpelfe,
    const TEXTMETRICW* /*lpntme*/,
    DWORD fontType,
    LPARAM lParam
) {
    if ((lpelfe->lfPitchAndFamily & 0x03) == FIXED_PITCH && lpelfe->lfFaceName[0] != L'@' && (fontType & TRUETYPE_FONTTYPE)) {
        auto* set_ptr = reinterpret_cast<std::set<std::string>*>(lParam);
        std::string name = wide_to_utf8(lpelfe->lfFaceName);
        if (!name.empty()) set_ptr->insert(std::move(name));
    }
    return 1;
}

[[nodiscard]] inline auto win32_monospace_fonts() -> std::vector<std::string> {
    std::set<std::string> unique_fonts;
    HDC hdc = ::GetDC(nullptr);
    if (hdc) {
        LOGFONTW lf{};
        lf.lfCharSet = DEFAULT_CHARSET;
        ::EnumFontFamiliesExW(hdc, &lf, win32_enum_mono_font_proc, reinterpret_cast<LPARAM>(&unique_fonts), 0);
        ::ReleaseDC(nullptr, hdc);
    }
    return std::vector<std::string>(unique_fonts.begin(), unique_fonts.end());
}

struct Win32WindowContext {
    std::shared_ptr<AppState> state;
    HWND hwnd = nullptr;
    std::function<void(RECT)> on_resize;
    std::function<void(const std::string&)> on_eval;
};

// Posted by platform_eval with a heap std::string* in lParam; the WndProc owns and frees it.
inline constexpr UINT kWmCfireEval = WM_APP + 2;

inline LRESULT CALLBACK cfire_win32_wndproc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    auto* ctx = reinterpret_cast<Win32WindowContext*>(::GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    switch (msg) {
        case WM_CREATE: {
            auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
            ::SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
            return 0;
        }
        case WM_SIZE: {
            if (ctx && ctx->on_resize) {
                RECT rc{};
                ::GetClientRect(hwnd, &rc);
                ctx->on_resize(rc);
            }
            return 0;
        }
        case WM_GETMINMAXINFO: {
            if (ctx && ctx->state) {
                auto* mmi = reinterpret_cast<MINMAXINFO*>(lParam);
                if (ctx->state->min_width > 0 && ctx->state->min_height > 0) {
                    mmi->ptMinTrackSize.x = ctx->state->min_width;
                    mmi->ptMinTrackSize.y = ctx->state->min_height;
                }
                if (ctx->state->max_width > 0 && ctx->state->max_height > 0) {
                    mmi->ptMaxTrackSize.x = ctx->state->max_width;
                    mmi->ptMaxTrackSize.y = ctx->state->max_height;
                }
            }
            return 0;
        }
        case WM_CLOSE: {
            if (ctx && ctx->state) {
                if (ctx->state->hide_on_close && !ctx->state->quit_requested.load(std::memory_order_relaxed)) {
                    {
                        std::lock_guard<std::recursive_mutex> lock(ctx->state->mtx);
                        ctx->state->window_hidden = true;
                    }
                    ::ShowWindow(hwnd, SW_HIDE);
                    ctx->state->record_emitted("window:close_requested", "{}");
                    ctx->state->dispatch_event_internal("window:close_requested", "{}");
                    ctx->state->eval_js("window.__cfire_event_dispatch(\"window:close_requested\", {});");
                    return 0;
                }
                ctx->state->quit_requested.store(true, std::memory_order_relaxed);
            }
            ::DestroyWindow(hwnd);
            return 0;
        }
        case WM_APP + 1: {
            if (ctx && ctx->state && (lParam == WM_RBUTTONUP || lParam == WM_LBUTTONUP)) {
                HMENU hMenu = ::CreatePopupMenu();
                if (hMenu) {
                    std::vector<std::string> events;
                    {
                        std::lock_guard<std::recursive_mutex> lock(ctx->state->mtx);
                        for (std::size_t i = 0; i < ctx->state->tray_items.size(); ++i) {
                            const auto& titem = ctx->state->tray_items[i];
                            if (titem.title == "-") {
                                ::AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
                            } else {
                                std::wstring wt = utf8_to_wide(titem.title);
                                ::AppendMenuW(hMenu, MF_STRING, static_cast<UINT_PTR>(i + 1), wt.c_str());
                            }
                            events.push_back(titem.event);
                        }
                    }
                    POINT pt{};
                    ::GetCursorPos(&pt);
                    ::SetForegroundWindow(hwnd);
                    UINT cmd = static_cast<UINT>(::TrackPopupMenu(
                        hMenu,
                        TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON,
                        pt.x, pt.y, 0, hwnd, nullptr));
                    ::DestroyMenu(hMenu);
                    if (cmd >= 1 && cmd <= events.size()) {
                        const std::string& ev = events[cmd - 1];
                        ctx->state->record_emitted(ev, "{}");
                        ctx->state->dispatch_event_internal(ev, "{}");
                        std::string ev_escaped = cfire::json::escape_string(ev);
                        ctx->state->eval_js(std::format("window.__cfire_event_dispatch(\"{}\", {{}});", ev_escaped));
                    }
                }
                return 0;
            }
            return ::DefWindowProcW(hwnd, msg, wParam, lParam);
        }
        case kWmCfireEval: {
            std::unique_ptr<std::string> js(reinterpret_cast<std::string*>(lParam));
            if (ctx && ctx->on_eval && js) ctx->on_eval(*js);
            return 0;
        }
        case WM_DESTROY: {
            if (ctx && ctx->state) {
                ctx->state->quit_requested.store(true, std::memory_order_relaxed);
            }
            ::PostQuitMessage(0);
            return 0;
        }
        default:
            return ::DefWindowProcW(hwnd, msg, wParam, lParam);
    }
}

#if defined(_WIN32)
#if __has_include(<WebView2.h>)
#include <WebView2.h>
#else
// --- Self-contained WebView2 COM interface declarations (W-06, W-09, W-10) ----------------
// Provides the frozen Microsoft WebView2 COM vtable definitions when <WebView2.h> is not
// installed on the build machine, avoiding external NuGet SDK and EventToken.h issues.
#if __has_include(<eventtoken.h>)
#include <eventtoken.h>
#else
struct EventRegistrationToken {
    long long value;
};
#endif

struct COREWEBVIEW2_COLOR {
    BYTE A;
    BYTE R;
    BYTE G;
    BYTE B;
};

enum COREWEBVIEW2_WEB_RESOURCE_CONTEXT {
    COREWEBVIEW2_WEB_RESOURCE_CONTEXT_ALL = 0
};

struct ICoreWebView2;
struct ICoreWebView2Controller;
struct ICoreWebView2Controller2;
struct ICoreWebView2Environment;
struct ICoreWebView2EnvironmentOptions;
struct ICoreWebView2NavigationCompletedEventArgs;
struct ICoreWebView2Settings;
struct ICoreWebView2WebMessageReceivedEventArgs;
struct ICoreWebView2WebResourceRequest;
struct ICoreWebView2WebResourceRequestedEventArgs;
struct ICoreWebView2WebResourceResponse;

struct ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler : public IUnknown {
    virtual HRESULT STDMETHODCALLTYPE Invoke(HRESULT errorCode, ICoreWebView2Environment* createdEnvironment) = 0;
};

struct ICoreWebView2CreateCoreWebView2ControllerCompletedHandler : public IUnknown {
    virtual HRESULT STDMETHODCALLTYPE Invoke(HRESULT errorCode, ICoreWebView2Controller* createdController) = 0;
};

struct ICoreWebView2AddScriptToExecuteOnDocumentCreatedCompletedHandler : public IUnknown {
    virtual HRESULT STDMETHODCALLTYPE Invoke(HRESULT errorCode, LPCWSTR id) = 0;
};

struct ICoreWebView2ExecuteScriptCompletedHandler : public IUnknown {
    virtual HRESULT STDMETHODCALLTYPE Invoke(HRESULT errorCode, LPCWSTR resultObjectAsJson) = 0;
};

struct ICoreWebView2NavigationCompletedEventHandler : public IUnknown {
    virtual HRESULT STDMETHODCALLTYPE Invoke(ICoreWebView2* sender, ICoreWebView2NavigationCompletedEventArgs* args) = 0;
};

struct ICoreWebView2WebMessageReceivedEventHandler : public IUnknown {
    virtual HRESULT STDMETHODCALLTYPE Invoke(ICoreWebView2* sender, ICoreWebView2WebMessageReceivedEventArgs* args) = 0;
};

struct ICoreWebView2WebResourceRequestedEventHandler : public IUnknown {
    virtual HRESULT STDMETHODCALLTYPE Invoke(ICoreWebView2* sender, ICoreWebView2WebResourceRequestedEventArgs* args) = 0;
};

struct ICoreWebView2WebMessageReceivedEventArgs : public IUnknown {
    virtual HRESULT STDMETHODCALLTYPE get_Source(LPWSTR* source) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_WebMessageAsJson(LPWSTR* webMessageAsJson) = 0;
    virtual HRESULT STDMETHODCALLTYPE TryGetWebMessageAsString(LPWSTR* webMessageAsString) = 0;
};

struct ICoreWebView2WebResourceRequest : public IUnknown {
    virtual HRESULT STDMETHODCALLTYPE get_Uri(LPWSTR* uri) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_Uri(LPCWSTR uri) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_Method(LPWSTR* method) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_Method(LPCWSTR method) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_Content(IStream** content) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_Content(IStream* content) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_Headers(IUnknown** headers) = 0;
};

struct ICoreWebView2WebResourceResponse : public IUnknown {
    virtual HRESULT STDMETHODCALLTYPE get_Content(IStream** content) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_Content(IStream* content) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_Headers(IUnknown** headers) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_StatusCode(int* statusCode) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_StatusCode(int statusCode) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_ReasonPhrase(LPWSTR* reasonPhrase) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_ReasonPhrase(LPCWSTR reasonPhrase) = 0;
};

struct ICoreWebView2WebResourceRequestedEventArgs : public IUnknown {
    virtual HRESULT STDMETHODCALLTYPE get_Request(ICoreWebView2WebResourceRequest** request) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_Response(ICoreWebView2WebResourceResponse** response) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_Response(ICoreWebView2WebResourceResponse* response) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetDeferral(IUnknown** deferral) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_ResourceContext(COREWEBVIEW2_WEB_RESOURCE_CONTEXT* context) = 0;
};

struct ICoreWebView2Settings : public IUnknown {
    virtual HRESULT STDMETHODCALLTYPE get_IsScriptEnabled(BOOL* isScriptEnabled) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_IsScriptEnabled(BOOL isScriptEnabled) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_IsWebMessageEnabled(BOOL* isWebMessageEnabled) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_IsWebMessageEnabled(BOOL isWebMessageEnabled) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_AreDefaultScriptDialogsEnabled(BOOL* areDefaultScriptDialogsEnabled) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_AreDefaultScriptDialogsEnabled(BOOL areDefaultScriptDialogsEnabled) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_IsStatusBarEnabled(BOOL* isStatusBarEnabled) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_IsStatusBarEnabled(BOOL isStatusBarEnabled) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_AreDevToolsEnabled(BOOL* areDevToolsEnabled) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_AreDevToolsEnabled(BOOL areDevToolsEnabled) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_AreDefaultContextMenusEnabled(BOOL* enabled) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_AreDefaultContextMenusEnabled(BOOL enabled) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_AreHostObjectsAllowed(BOOL* allowed) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_AreHostObjectsAllowed(BOOL allowed) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_IsZoomControlEnabled(BOOL* enabled) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_IsZoomControlEnabled(BOOL enabled) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_IsBuiltInErrorPageEnabled(BOOL* enabled) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_IsBuiltInErrorPageEnabled(BOOL enabled) = 0;
};

struct ICoreWebView2 : public IUnknown {
    virtual HRESULT STDMETHODCALLTYPE get_Settings(ICoreWebView2Settings** settings) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_Source(LPWSTR* uri) = 0;
    virtual HRESULT STDMETHODCALLTYPE Navigate(LPCWSTR uri) = 0;
    virtual HRESULT STDMETHODCALLTYPE NavigateToString(LPCWSTR htmlContent) = 0;
    virtual HRESULT STDMETHODCALLTYPE add_NavigationStarting(IUnknown* eventHandler, EventRegistrationToken* token) = 0;
    virtual HRESULT STDMETHODCALLTYPE remove_NavigationStarting(EventRegistrationToken token) = 0;
    virtual HRESULT STDMETHODCALLTYPE add_ContentLoading(IUnknown* eventHandler, EventRegistrationToken* token) = 0;
    virtual HRESULT STDMETHODCALLTYPE remove_ContentLoading(EventRegistrationToken token) = 0;
    virtual HRESULT STDMETHODCALLTYPE add_SourceChanged(IUnknown* eventHandler, EventRegistrationToken* token) = 0;
    virtual HRESULT STDMETHODCALLTYPE remove_SourceChanged(EventRegistrationToken token) = 0;
    virtual HRESULT STDMETHODCALLTYPE add_HistoryChanged(IUnknown* eventHandler, EventRegistrationToken* token) = 0;
    virtual HRESULT STDMETHODCALLTYPE remove_HistoryChanged(EventRegistrationToken token) = 0;
    virtual HRESULT STDMETHODCALLTYPE add_NavigationCompleted(ICoreWebView2NavigationCompletedEventHandler* eventHandler, EventRegistrationToken* token) = 0;
    virtual HRESULT STDMETHODCALLTYPE remove_NavigationCompleted(EventRegistrationToken token) = 0;
    virtual HRESULT STDMETHODCALLTYPE add_FrameNavigationStarting(IUnknown* eventHandler, EventRegistrationToken* token) = 0;
    virtual HRESULT STDMETHODCALLTYPE remove_FrameNavigationStarting(EventRegistrationToken token) = 0;
    virtual HRESULT STDMETHODCALLTYPE add_FrameNavigationCompleted(IUnknown* eventHandler, EventRegistrationToken* token) = 0;
    virtual HRESULT STDMETHODCALLTYPE remove_FrameNavigationCompleted(EventRegistrationToken token) = 0;
    virtual HRESULT STDMETHODCALLTYPE add_ScriptDialogOpening(IUnknown* eventHandler, EventRegistrationToken* token) = 0;
    virtual HRESULT STDMETHODCALLTYPE remove_ScriptDialogOpening(EventRegistrationToken token) = 0;
    virtual HRESULT STDMETHODCALLTYPE add_PermissionRequested(IUnknown* eventHandler, EventRegistrationToken* token) = 0;
    virtual HRESULT STDMETHODCALLTYPE remove_PermissionRequested(EventRegistrationToken token) = 0;
    virtual HRESULT STDMETHODCALLTYPE add_ProcessFailed(IUnknown* eventHandler, EventRegistrationToken* token) = 0;
    virtual HRESULT STDMETHODCALLTYPE remove_ProcessFailed(EventRegistrationToken token) = 0;
    virtual HRESULT STDMETHODCALLTYPE AddScriptToExecuteOnDocumentCreated(LPCWSTR javaScript, ICoreWebView2AddScriptToExecuteOnDocumentCreatedCompletedHandler* handler) = 0;
    virtual HRESULT STDMETHODCALLTYPE RemoveScriptToExecuteOnDocumentCreated(LPCWSTR id) = 0;
    virtual HRESULT STDMETHODCALLTYPE ExecuteScript(LPCWSTR javaScript, ICoreWebView2ExecuteScriptCompletedHandler* handler) = 0;
    virtual HRESULT STDMETHODCALLTYPE CapturePreview(int imageFormat, IStream* imageStream, IUnknown* handler) = 0;
    virtual HRESULT STDMETHODCALLTYPE Reload() = 0;
    virtual HRESULT STDMETHODCALLTYPE PostWebMessageAsJson(LPCWSTR webMessageAsJson) = 0;
    virtual HRESULT STDMETHODCALLTYPE PostWebMessageAsString(LPCWSTR webMessageAsString) = 0;
    virtual HRESULT STDMETHODCALLTYPE add_WebMessageReceived(ICoreWebView2WebMessageReceivedEventHandler* handler, EventRegistrationToken* token) = 0;
    virtual HRESULT STDMETHODCALLTYPE remove_WebMessageReceived(EventRegistrationToken token) = 0;
    virtual HRESULT STDMETHODCALLTYPE CallDevToolsProtocolMethod(LPCWSTR methodName, LPCWSTR parametersAsJson, IUnknown* handler) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_BrowserProcessId(UINT32* value) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_CanGoBack(BOOL* canGoBack) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_CanGoForward(BOOL* canGoForward) = 0;
    virtual HRESULT STDMETHODCALLTYPE GoBack() = 0;
    virtual HRESULT STDMETHODCALLTYPE GoForward() = 0;
    virtual HRESULT STDMETHODCALLTYPE GetDevToolsProtocolEventReceiver(LPCWSTR eventName, IUnknown** receiver) = 0;
    virtual HRESULT STDMETHODCALLTYPE Stop() = 0;
    virtual HRESULT STDMETHODCALLTYPE add_NewWindowRequested(IUnknown* eventHandler, EventRegistrationToken* token) = 0;
    virtual HRESULT STDMETHODCALLTYPE remove_NewWindowRequested(EventRegistrationToken token) = 0;
    virtual HRESULT STDMETHODCALLTYPE add_DocumentTitleChanged(IUnknown* eventHandler, EventRegistrationToken* token) = 0;
    virtual HRESULT STDMETHODCALLTYPE remove_DocumentTitleChanged(EventRegistrationToken token) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_DocumentTitle(LPWSTR* title) = 0;
    virtual HRESULT STDMETHODCALLTYPE AddHostObjectToScript(LPCWSTR name, VARIANT* object) = 0;
    virtual HRESULT STDMETHODCALLTYPE RemoveHostObjectFromScript(LPCWSTR name) = 0;
    virtual HRESULT STDMETHODCALLTYPE OpenDevToolsWindow() = 0;
    virtual HRESULT STDMETHODCALLTYPE add_ContainsFullScreenElementChanged(IUnknown* eventHandler, EventRegistrationToken* token) = 0;
    virtual HRESULT STDMETHODCALLTYPE remove_ContainsFullScreenElementChanged(EventRegistrationToken token) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_ContainsFullScreenElement(BOOL* containsFullScreenElement) = 0;
    virtual HRESULT STDMETHODCALLTYPE add_WebResourceRequested(ICoreWebView2WebResourceRequestedEventHandler* eventHandler, EventRegistrationToken* token) = 0;
    virtual HRESULT STDMETHODCALLTYPE remove_WebResourceRequested(EventRegistrationToken token) = 0;
    virtual HRESULT STDMETHODCALLTYPE AddWebResourceRequestedFilter(LPCWSTR uri, COREWEBVIEW2_WEB_RESOURCE_CONTEXT resourceContext) = 0;
    virtual HRESULT STDMETHODCALLTYPE RemoveWebResourceRequestedFilter(LPCWSTR uri, COREWEBVIEW2_WEB_RESOURCE_CONTEXT resourceContext) = 0;
    virtual HRESULT STDMETHODCALLTYPE add_WindowCloseRequested(IUnknown* eventHandler, EventRegistrationToken* token) = 0;
    virtual HRESULT STDMETHODCALLTYPE remove_WindowCloseRequested(EventRegistrationToken token) = 0;
};

struct ICoreWebView2Controller : public IUnknown {
    virtual HRESULT STDMETHODCALLTYPE get_IsVisible(BOOL* isVisible) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_IsVisible(BOOL isVisible) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_Bounds(RECT* bounds) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_Bounds(RECT bounds) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_ZoomFactor(double* zoomFactor) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_ZoomFactor(double zoomFactor) = 0;
    virtual HRESULT STDMETHODCALLTYPE add_ZoomFactorChanged(IUnknown* eventHandler, EventRegistrationToken* token) = 0;
    virtual HRESULT STDMETHODCALLTYPE remove_ZoomFactorChanged(EventRegistrationToken token) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetBoundsAndZoomFactor(RECT bounds, double zoomFactor) = 0;
    virtual HRESULT STDMETHODCALLTYPE MoveFocus(int reason) = 0;
    virtual HRESULT STDMETHODCALLTYPE add_MoveFocusRequested(IUnknown* eventHandler, EventRegistrationToken* token) = 0;
    virtual HRESULT STDMETHODCALLTYPE remove_MoveFocusRequested(EventRegistrationToken token) = 0;
    virtual HRESULT STDMETHODCALLTYPE add_GotFocus(IUnknown* eventHandler, EventRegistrationToken* token) = 0;
    virtual HRESULT STDMETHODCALLTYPE remove_GotFocus(EventRegistrationToken token) = 0;
    virtual HRESULT STDMETHODCALLTYPE add_LostFocus(IUnknown* eventHandler, EventRegistrationToken* token) = 0;
    virtual HRESULT STDMETHODCALLTYPE remove_LostFocus(EventRegistrationToken token) = 0;
    virtual HRESULT STDMETHODCALLTYPE add_AcceleratorKeyPressed(IUnknown* eventHandler, EventRegistrationToken* token) = 0;
    virtual HRESULT STDMETHODCALLTYPE remove_AcceleratorKeyPressed(EventRegistrationToken token) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_ParentWindow(HWND* parentWindow) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_ParentWindow(HWND parentWindow) = 0;
    virtual HRESULT STDMETHODCALLTYPE NotifyParentWindowPositionChanged() = 0;
    virtual HRESULT STDMETHODCALLTYPE Close() = 0;
    virtual HRESULT STDMETHODCALLTYPE get_CoreWebView2(ICoreWebView2** coreWebView2) = 0;
};

struct ICoreWebView2Controller2 : public ICoreWebView2Controller {
    virtual HRESULT STDMETHODCALLTYPE get_DefaultBackgroundColor(COREWEBVIEW2_COLOR* backgroundColor) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_DefaultBackgroundColor(COREWEBVIEW2_COLOR backgroundColor) = 0;
};

struct ICoreWebView2Environment : public IUnknown {
    virtual HRESULT STDMETHODCALLTYPE CreateCoreWebView2Controller(HWND parentWindow, ICoreWebView2CreateCoreWebView2ControllerCompletedHandler* handler) = 0;
    virtual HRESULT STDMETHODCALLTYPE CreateWebResourceResponse(IStream* content, int statusCode, LPCWSTR reasonPhrase, LPCWSTR headers, ICoreWebView2WebResourceResponse** response) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_BrowserVersionString(LPWSTR* versionInfo) = 0;
    virtual HRESULT STDMETHODCALLTYPE add_NewBrowserVersionAvailable(IUnknown* eventHandler, EventRegistrationToken* token) = 0;
    virtual HRESULT STDMETHODCALLTYPE remove_NewBrowserVersionAvailable(EventRegistrationToken token) = 0;
};
#endif // !__has_include(<WebView2.h>)

// --- WebView2 host (CF-001.a-e, W-06, W-09, W-10) -----------------------------------------
//
// Same contract as the Linux/macOS backends: kPreloadBridgeJs at document start, messages
// from the page (`window.chrome.webview.postMessage`) into AppState::handle_ipc_message,
// assets from AppState::resolve_asset served on `https://app.localhost`, and every script
// evaluated on the UI thread.

inline constexpr std::wstring_view kWv2Origin = L"https://app.localhost";

constexpr auto wv2_hex(char c) -> unsigned {
    return c <= '9' ? static_cast<unsigned>(c - '0') : static_cast<unsigned>((c | 0x20) - 'a' + 10);
}
constexpr auto wv2_iid(const char (&u)[37]) -> IID {
    auto h = [&](int at, int n) {
        unsigned long long v = 0;
        for (int i = 0; i < n; ++i) v = (v << 4) | wv2_hex(u[at + i]);
        return v;
    };
    IID g{};
    g.Data1 = static_cast<unsigned long>(h(0, 8));
    g.Data2 = static_cast<unsigned short>(h(9, 4));
    g.Data3 = static_cast<unsigned short>(h(14, 4));
    g.Data4[0] = static_cast<unsigned char>(h(19, 2));
    g.Data4[1] = static_cast<unsigned char>(h(21, 2));
    for (int i = 0; i < 6; ++i) g.Data4[2 + i] = static_cast<unsigned char>(h(24 + 2 * i, 2));
    return g;
}

template <class I, class M>
struct Wv2Handler;

template <class I, class... A>
struct Wv2Handler<I, HRESULT (STDMETHODCALLTYPE I::*)(A...)> final : I {
    IID iid;
    std::function<HRESULT(A...)> fn;
    std::atomic<ULONG> refs{1};

    Wv2Handler(IID i, std::function<HRESULT(A...)> f) : iid(i), fn(std::move(f)) {}
    virtual ~Wv2Handler() = default;

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** out) override {
        if (!out) return E_POINTER;
        if (IsEqualIID(riid, __uuidof(IUnknown)) || IsEqualIID(riid, iid)) {
            *out = static_cast<I*>(this);
            AddRef();
            return S_OK;
        }
        *out = nullptr;
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs; }
    ULONG STDMETHODCALLTYPE Release() override {
        ULONG n = --refs;
        if (n == 0) delete this;
        return n;
    }
    HRESULT STDMETHODCALLTYPE Invoke(A... a) override { return fn ? fn(a...) : S_OK; }
};

template <class I, class F>
[[nodiscard]] auto wv2_handler(const char (&uuid)[37], F&& f) -> I* {
    using H = Wv2Handler<I, decltype(&I::Invoke)>;
    return new H(wv2_iid(uuid), std::forward<F>(f));
}

inline constexpr char kIidEnvCompleted[] = "4e8a3389-c9d8-4bd2-b6b5-124fee6cc14d";
inline constexpr char kIidControllerCompleted[] = "6c4819f3-c9b7-4260-8127-c9f5bde7f68c";
inline constexpr char kIidWebMessageReceived[] = "57213f19-00e6-49fa-8e07-898ea01ecbd2";
inline constexpr char kIidWebResourceRequested[] = "ab00b74c-15f1-4646-80e8-e76341d25d71";
inline constexpr char kIidNavigationCompleted[] = "d33a35bf-1c49-4f98-93ab-006e0533fe1c";
inline constexpr char kIidController2[] = "c979903e-d4ca-4228-92eb-47ee3fa96eab";

using Wv2CreateEnvironmentFn = HRESULT(STDAPICALLTYPE*)(
    PCWSTR, PCWSTR, ICoreWebView2EnvironmentOptions*,
    ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler*);

using Wv2CreateInternalFn = HRESULT(STDAPICALLTYPE*)(
    bool, int, PCWSTR, IUnknown*,
    ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler*);

[[nodiscard]] inline auto wv2_find_installed_client_dll() -> std::wstring {
    static constexpr const wchar_t* kSubKeys[] = {
        L"SOFTWARE\\WOW6432Node\\Microsoft\\EdgeUpdate\\Clients\\{F3017226-FE2A-4295-8BDF-00C3A9A7E4C5}",
        L"SOFTWARE\\Microsoft\\EdgeUpdate\\Clients\\{F3017226-FE2A-4295-8BDF-00C3A9A7E4C5}"
    };
    const HKEY kRoots[] = {HKEY_LOCAL_MACHINE, HKEY_CURRENT_USER};
#if defined(_M_ARM64) || defined(__aarch64__)
    static constexpr const wchar_t* kArchDir = L"arm64";
#elif defined(_WIN64) || defined(__x86_64__)
    static constexpr const wchar_t* kArchDir = L"x64";
#else
    static constexpr const wchar_t* kArchDir = L"x86";
#endif
    for (HKEY root : kRoots) {
        for (const wchar_t* sub : kSubKeys) {
            HKEY hKey = nullptr;
            if (::RegOpenKeyExW(root, sub, 0, KEY_READ, &hKey) != ERROR_SUCCESS || !hKey) continue;
            wchar_t buf[MAX_PATH * 2] = {0};
            DWORD bytes = sizeof(buf);
            DWORD type = 0;
            LONG rc = ::RegQueryValueExW(hKey, L"EBWebView", nullptr, &type, reinterpret_cast<LPBYTE>(buf), &bytes);
            ::RegCloseKey(hKey);
            if (rc == ERROR_SUCCESS && (type == REG_SZ || type == REG_EXPAND_SZ) && buf[0] != L'\0') {
                std::filesystem::path dll_p = std::filesystem::path(buf) / L"EBWebView" / kArchDir / L"EmbeddedBrowserWebView.dll";
                std::error_code ec;
                if (std::filesystem::exists(dll_p, ec)) {
                    return dll_p.wstring();
                }
            }
        }
    }
    return L"";
}

// Invokes WebView2Loader.dll if present beside the executable or on PATH; otherwise falls
// back to the built-in Registry locator for EmbeddedBrowserWebView.dll so no external DLL
// is required alongside the .exe (W-10).
[[nodiscard]] inline auto wv2_create_environment(
    PCWSTR user_data_dir,
    ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler* handler
) -> HRESULT {
    if (HMODULE mod = ::LoadLibraryW(L"WebView2Loader.dll")) {
        auto fn = reinterpret_cast<Wv2CreateEnvironmentFn>(
            reinterpret_cast<void*>(::GetProcAddress(mod, "CreateCoreWebView2EnvironmentWithOptions")));
        if (fn) {
            return fn(nullptr, user_data_dir, nullptr, handler);
        }
    }
    std::wstring client_dll = wv2_find_installed_client_dll();
    if (client_dll.empty()) {
        return HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);
    }
    HMODULE mod = ::LoadLibraryW(client_dll.c_str());
    if (!mod) {
        return HRESULT_FROM_WIN32(::GetLastError());
    }
    auto internal_fn = reinterpret_cast<Wv2CreateInternalFn>(
        reinterpret_cast<void*>(::GetProcAddress(mod, "CreateWebViewEnvironmentWithOptionsInternal")));
    if (!internal_fn) {
        return HRESULT_FROM_WIN32(ERROR_PROC_NOT_FOUND);
    }
    return internal_fn(true, 0, user_data_dir, nullptr, handler);
}

[[nodiscard]] inline auto wv2_user_data_dir() -> std::wstring {
    wchar_t base[MAX_PATH];
    DWORD n = ::GetEnvironmentVariableW(L"LOCALAPPDATA", base, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) return L"";
    wchar_t exe[MAX_PATH];
    DWORD m = ::GetModuleFileNameW(nullptr, exe, MAX_PATH);
    std::wstring stem = (m > 0 && m < MAX_PATH) ? std::filesystem::path(exe).stem().wstring() : L"cfire-webui";
    return (std::filesystem::path(base) / stem / L"WebView2").wstring();
}

struct Wv2Host {
    std::shared_ptr<AppState> state;
    HWND hwnd = nullptr;
    ICoreWebView2Environment* env = nullptr;
    ICoreWebView2Controller* controller = nullptr;
    ICoreWebView2* webview = nullptr;
    std::vector<std::string> pending_js{};
    bool loaded = false;

    Wv2Host(std::shared_ptr<AppState> st, HWND h) : state(std::move(st)), hwnd(h) {}
    Wv2Host(const Wv2Host&) = delete;
    Wv2Host& operator=(const Wv2Host&) = delete;
    ~Wv2Host() {
        if (webview) webview->Release();
        if (controller) {
            controller->Close();
            controller->Release();
        }
        if (env) env->Release();
    }

    void eval(const std::string& js) {
        if (!webview || !loaded) {
            pending_js.push_back(js);
            return;
        }
        std::wstring w = utf8_to_wide(js);
        webview->ExecuteScript(w.c_str(), nullptr);
    }

    void resize(RECT rc) {
        if (controller) controller->put_Bounds(rc);
    }

    static void report(const char* what, HRESULT hr) {
        if (hr == HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND)) {
            std::fprintf(stderr, "webui: %s failed: the WebView2 Runtime is not installed "
                                 "(https://developer.microsoft.com/microsoft-edge/webview2/).\n", what);
        } else {
            std::fprintf(stderr, "webui: %s failed (HRESULT 0x%08lx).\n", what, static_cast<unsigned long>(hr));
        }
    }

    auto start() -> HRESULT {
        std::wstring udf = wv2_user_data_dir();
        auto* h = wv2_handler<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
            kIidEnvCompleted, [this](HRESULT hr, ICoreWebView2Environment* e) -> HRESULT { return on_environment(hr, e); });
        HRESULT hr = wv2_create_environment(udf.empty() ? nullptr : udf.c_str(), h);
        h->Release();
        if (FAILED(hr)) report("CreateCoreWebView2EnvironmentWithOptions", hr);
        return hr;
    }

    auto on_environment(HRESULT hr, ICoreWebView2Environment* e) -> HRESULT {
        if (FAILED(hr) || !e) {
            report("creating the WebView2 environment", FAILED(hr) ? hr : E_POINTER);
            return S_OK;
        }
        env = e;
        env->AddRef();
        auto* h = wv2_handler<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
            kIidControllerCompleted, [this](HRESULT r, ICoreWebView2Controller* c) -> HRESULT { return on_controller(r, c); });
        HRESULT r = env->CreateCoreWebView2Controller(hwnd, h);
        h->Release();
        if (FAILED(r)) report("CreateCoreWebView2Controller", r);
        return S_OK;
    }

    auto on_controller(HRESULT hr, ICoreWebView2Controller* c) -> HRESULT {
        if (FAILED(hr) || !c) {
            report("creating the WebView2 controller", FAILED(hr) ? hr : E_POINTER);
            return S_OK;
        }
        controller = c;
        controller->AddRef();
        controller->get_CoreWebView2(&webview);
        if (!webview) return S_OK;

        if (state->transparent) {
            ICoreWebView2Controller2* c2 = nullptr;
            IID iid_c2 = wv2_iid(kIidController2);
            if (SUCCEEDED(controller->QueryInterface(iid_c2, reinterpret_cast<void**>(&c2))) && c2) {
                c2->put_DefaultBackgroundColor(COREWEBVIEW2_COLOR{0, 0, 0, 0});
                c2->Release();
            }
        }

        ICoreWebView2Settings* settings = nullptr;
        if (SUCCEEDED(webview->get_Settings(&settings)) && settings) {
            settings->put_IsStatusBarEnabled(FALSE);
            settings->put_IsWebMessageEnabled(TRUE);
            settings->Release();
        }

        RECT rc{};
        ::GetClientRect(hwnd, &rc);
        controller->put_Bounds(rc);

        std::wstring preload = utf8_to_wide(kPreloadBridgeJs);
        webview->AddScriptToExecuteOnDocumentCreated(preload.c_str(), nullptr);

        EventRegistrationToken tok{};
        auto* msg_h = wv2_handler<ICoreWebView2WebMessageReceivedEventHandler>(
            kIidWebMessageReceived, [this](ICoreWebView2*, ICoreWebView2WebMessageReceivedEventArgs* args) -> HRESULT {
                LPWSTR raw = nullptr;
                if (args && SUCCEEDED(args->TryGetWebMessageAsString(&raw)) && raw) {
                    std::string m = wide_to_utf8(raw);
                    ::CoTaskMemFree(raw);
                    state->handle_ipc_message(m);
                }
                return S_OK;
            });
        webview->add_WebMessageReceived(msg_h, &tok);
        msg_h->Release();

        std::wstring filter = std::wstring(kWv2Origin) + L"/*";
        webview->AddWebResourceRequestedFilter(filter.c_str(), COREWEBVIEW2_WEB_RESOURCE_CONTEXT_ALL);
        auto* res_h = wv2_handler<ICoreWebView2WebResourceRequestedEventHandler>(
            kIidWebResourceRequested, [this](ICoreWebView2*, ICoreWebView2WebResourceRequestedEventArgs* args) -> HRESULT {
                serve(args);
                return S_OK;
            });
        webview->add_WebResourceRequested(res_h, &tok);
        res_h->Release();

        auto* nav_h = wv2_handler<ICoreWebView2NavigationCompletedEventHandler>(
            kIidNavigationCompleted, [this](ICoreWebView2*, ICoreWebView2NavigationCompletedEventArgs*) -> HRESULT {
                if (!loaded) {
                    loaded = true;
                    auto queued = std::move(pending_js);
                    pending_js.clear();
                    for (const auto& js : queued) eval(js);
                }
                return S_OK;
            });
        webview->add_NavigationCompleted(nav_h, &tok);
        nav_h->Release();

        std::wstring url = std::wstring(kWv2Origin) + L"/index.html";
        webview->Navigate(url.c_str());
        return S_OK;
    }

    void serve(ICoreWebView2WebResourceRequestedEventArgs* args) {
        if (!args || !env) return;
        ICoreWebView2WebResourceRequest* req = nullptr;
        if (FAILED(args->get_Request(&req)) || !req) return;
        LPWSTR uri_w = nullptr;
        req->get_Uri(&uri_w);
        req->Release();
        std::string uri = uri_w ? wide_to_utf8(uri_w) : std::string{};
        if (uri_w) ::CoTaskMemFree(uri_w);

        std::string origin = wide_to_utf8(kWv2Origin);
        std::string path = uri.starts_with(origin) ? uri.substr(origin.size()) : std::string("/");
        auto asset = state->resolve_asset(path);
        int status = asset ? 200 : 404;
        std::string body = asset ? asset->content : std::string("404 Not Found");
        std::string mime = asset ? asset->mime : std::string("text/plain; charset=utf-8");

        IStream* stream = ::SHCreateMemStream(reinterpret_cast<const BYTE*>(body.data()), static_cast<UINT>(body.size()));
        std::wstring headers = L"Content-Type: " + utf8_to_wide(mime) + L"\r\nCache-Control: no-store";
        ICoreWebView2WebResourceResponse* resp = nullptr;
        env->CreateWebResourceResponse(stream, status, status == 200 ? L"OK" : L"Not Found", headers.c_str(), &resp);
        if (resp) {
            args->put_Response(resp);
            resp->Release();
        }
        if (stream) stream->Release();
    }
};
#endif // defined(_WIN32)

inline void run_win32_webview2(const std::shared_ptr<AppState>& state_, bool offscreen_test = false, std::int32_t timeout_ms = 3000) {
    (void)::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
#if defined(_WIN32)
    if (HMODULE user32 = ::GetModuleHandleW(L"user32.dll")) {
        using SetDpiCtxFn = BOOL(WINAPI*)(HANDLE);
        void* sym = reinterpret_cast<void*>(::GetProcAddress(user32, "SetProcessDpiAwarenessContext"));
        if (sym) (void)reinterpret_cast<SetDpiCtxFn>(sym)(reinterpret_cast<HANDLE>(static_cast<LONG_PTR>(-4))); // PER_MONITOR_AWARE_V2
    }
#endif

    HINSTANCE hInst = ::GetModuleHandleW(nullptr);
    const wchar_t* kClassName = L"CfireWebUIWindowClass";

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = cfire_win32_wndproc;
    wc.hInstance = hInst;
    wc.hCursor = ::LoadCursorW(nullptr, MAKEINTRESOURCEW(32512)); // IDC_ARROW
    wc.hbrBackground = ::CreateSolidBrush(RGB(13, 17, 23));
#if defined(_WIN32)
    wc.hIcon = ::LoadIconW(hInst, MAKEINTRESOURCEW(1));
    wc.hIconSm = static_cast<HICON>(::LoadImageW(hInst, MAKEINTRESOURCEW(1), IMAGE_ICON,
        ::GetSystemMetrics(SM_CXSMICON), ::GetSystemMetrics(SM_CYSMICON), LR_DEFAULTCOLOR));
#endif
    wc.lpszClassName = kClassName;
    ::RegisterClassExW(&wc);

    Win32WindowContext ctx{state_, nullptr, nullptr, nullptr};
    DWORD style = state_->frameless ? WS_POPUP : WS_OVERLAPPEDWINDOW;
    DWORD exStyle = state_->always_on_top ? WS_EX_TOPMOST : 0;

    std::wstring wtitle = utf8_to_wide(state_->title);
    int x = offscreen_test ? -2000 : CW_USEDEFAULT;
    int y = offscreen_test ? -2000 : CW_USEDEFAULT;

    HWND hwnd = ::CreateWindowExW(
        exStyle,
        kClassName,
        wtitle.c_str(),
        style,
        x, y,
        state_->width, state_->height,
        nullptr,
        nullptr,
        hInst,
        &ctx);
    if (!hwnd) {
        ::CoUninitialize();
        return;
    }
    ctx.hwnd = hwnd;

    if (state_->dark_mode) {
        BOOL dark = TRUE;
        (void)::DwmSetWindowAttribute(hwnd, 20 /* DWMWA_USE_IMMERSIVE_DARK_MODE */, &dark, sizeof(dark));
    }

    if (!state_->icon_path.empty()) {
#if defined(_WIN32)
        HICON big = win32_load_icon_file(state_->icon_path, ::GetSystemMetrics(SM_CXICON), ::GetSystemMetrics(SM_CYICON));
        HICON small = win32_load_icon_file(state_->icon_path, ::GetSystemMetrics(SM_CXSMICON), ::GetSystemMetrics(SM_CYSMICON));
        if (big) ::SendMessageW(hwnd, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(big));
        if (small) ::SendMessageW(hwnd, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(small));
#else
        HICON hIcon = win32_load_icon_file(state_->icon_path, 0, 0);
        if (hIcon) {
            ::SendMessageW(hwnd, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(hIcon));
            ::SendMessageW(hwnd, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(hIcon));
        }
#endif
    }

    NOTIFYICONDATAW nid{};
    bool tray_added = false;
    if (state_->has_tray) {
        nid.cbSize = sizeof(NOTIFYICONDATAW);
        nid.hWnd = hwnd;
        nid.uID = 1;
        nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
        nid.uCallbackMessage = WM_APP + 1;
        if (!state_->tray_icon_path.empty()) {
#if defined(_WIN32)
            nid.hIcon = win32_load_icon_file(state_->tray_icon_path, ::GetSystemMetrics(SM_CXSMICON), ::GetSystemMetrics(SM_CYSMICON));
#else
            nid.hIcon = win32_load_icon_file(state_->tray_icon_path, 0, 0);
#endif
        }
        if (!nid.hIcon) {
            nid.hIcon = ::LoadIconW(nullptr, MAKEINTRESOURCEW(32512)); // IDI_APPLICATION
        }
        std::wstring wtip = utf8_to_wide(state_->tray_tooltip.empty() ? state_->title : state_->tray_tooltip);
        std::wcsncpy(nid.szTip, wtip.c_str(), (sizeof(nid.szTip) / sizeof(wchar_t)) - 1);
        tray_added = (::Shell_NotifyIconW(NIM_ADD, &nid) != FALSE);
    }

    {
        std::lock_guard<std::recursive_mutex> lock(state_->mtx);
        state_->win32_hwnd = hwnd;
        state_->platform_set_title = [hwnd](const std::string& new_title) {
            std::wstring wt = utf8_to_wide(new_title);
            ::SetWindowTextW(hwnd, wt.c_str());
        };
        state_->platform_set_size = [hwnd](std::int32_t w, std::int32_t h) {
            ::SetWindowPos(hwnd, nullptr, 0, 0, w, h, SWP_NOMOVE | SWP_NOZORDER);
        };
        state_->platform_set_dark_mode = [hwnd](bool dark) {
            BOOL d = dark ? TRUE : FALSE;
            (void)::DwmSetWindowAttribute(hwnd, 20 /* DWMWA_USE_IMMERSIVE_DARK_MODE */, &d, sizeof(d));
        };
#if defined(_WIN32)
        state_->platform_center = [hwnd]() {
            RECT rc{};
            ::GetWindowRect(hwnd, &rc);
            int w = rc.right - rc.left;
            int h = rc.bottom - rc.top;
            int sw = ::GetSystemMetrics(SM_CXSCREEN);
            int sh = ::GetSystemMetrics(SM_CYSCREEN);
            ::SetWindowPos(hwnd, nullptr, (sw - w) / 2, (sh - h) / 2, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
        };
        state_->platform_fullscreen = [hwnd, st = state_]() {
            DWORD cur_style = static_cast<DWORD>(::GetWindowLongPtrW(hwnd, GWL_STYLE));
            if (st->is_fullscreen) {
                ::SetWindowLongPtrW(hwnd, GWL_STYLE, cur_style & ~static_cast<DWORD>(WS_OVERLAPPEDWINDOW));
                int sw = ::GetSystemMetrics(SM_CXSCREEN);
                int sh = ::GetSystemMetrics(SM_CYSCREEN);
                ::SetWindowPos(hwnd, HWND_TOP, 0, 0, sw, sh, SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
            } else {
                ::SetWindowLongPtrW(hwnd, GWL_STYLE, cur_style | WS_OVERLAPPEDWINDOW);
                ::SetWindowPos(hwnd, nullptr, 100, 100, st->width, st->height, SWP_NOZORDER | SWP_FRAMECHANGED);
            }
        };
#endif
        state_->platform_minimize = [hwnd]() {
            ::ShowWindow(hwnd, SW_MINIMIZE);
        };
        state_->platform_maximize = [hwnd, st = state_]() {
            ::ShowWindow(hwnd, st->is_maximized ? SW_MAXIMIZE : SW_RESTORE);
        };
        state_->platform_drag = [hwnd]() {
            ::ReleaseCapture();
            ::SendMessageW(hwnd, WM_NCLBUTTONDOWN, HTCAPTION, 0);
        };
        state_->platform_hide_window = [hwnd]() {
            ::ShowWindow(hwnd, SW_HIDE);
        };
        state_->platform_show_window = [hwnd]() {
            ::ShowWindow(hwnd, SW_SHOW);
            ::SetForegroundWindow(hwnd);
        };
        state_->platform_quit = [hwnd, st = state_]() {
            st->quit_requested.store(true, std::memory_order_relaxed);
            ::PostMessageW(hwnd, WM_CLOSE, 0, 0);
        };
    }

#if defined(_WIN32)
    std::optional<Wv2Host> wv2;
    wv2.emplace(state_, hwnd);
    ctx.on_resize = [&wv2](RECT rc) { if (wv2) wv2->resize(rc); };
    ctx.on_eval = [&wv2](const std::string& js) { if (wv2) wv2->eval(js); };
    {
        std::lock_guard<std::recursive_mutex> lock(state_->mtx);
        state_->platform_eval = [hwnd](const std::string& js) {
            auto* copy = new std::string(js);
            if (!::PostMessageW(hwnd, kWmCfireEval, 0, reinterpret_cast<LPARAM>(copy))) delete copy;
        };
    }
    (void)wv2->start();
#endif

    if (!offscreen_test) {
        ::ShowWindow(hwnd, SW_SHOW);
        ::UpdateWindow(hwnd);
    }

    auto start = std::chrono::steady_clock::now();
    MSG msg{};
    while (!state_->quit_requested.load(std::memory_order_relaxed)) {
        if (offscreen_test) {
            while (::PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
                if (msg.message == WM_QUIT) {
                    state_->quit_requested.store(true, std::memory_order_relaxed);
                    break;
                }
                ::TranslateMessage(&msg);
                ::DispatchMessageW(&msg);
            }
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - start).count();
            if (elapsed >= timeout_ms) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        } else {
            BOOL ret = ::GetMessageW(&msg, nullptr, 0, 0);
            if (ret <= 0) break;
            ::TranslateMessage(&msg);
            ::DispatchMessageW(&msg);
        }
    }

    if (tray_added) {
        ::Shell_NotifyIconW(NIM_DELETE, &nid);
    }
    {
        std::lock_guard<std::recursive_mutex> lock(state_->mtx);
        state_->win32_hwnd = nullptr;
        state_->platform_eval = nullptr;
        state_->platform_set_title = nullptr;
        state_->platform_set_size = nullptr;
        state_->platform_set_dark_mode = nullptr;
        state_->platform_center = nullptr;
        state_->platform_fullscreen = nullptr;
        state_->platform_minimize = nullptr;
        state_->platform_maximize = nullptr;
        state_->platform_drag = nullptr;
        state_->platform_hide_window = nullptr;
        state_->platform_show_window = nullptr;
        state_->platform_quit = nullptr;
    }
#if defined(_WIN32)
    ctx.on_eval = nullptr;
    ctx.on_resize = nullptr;
    wv2.reset();
#endif
    ::CoUninitialize();
}

} // namespace cfire::webui::detail

#endif // defined(CFIRE_HAS_WIN32_BACKEND)

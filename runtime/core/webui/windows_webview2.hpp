#pragma once

#include "bridge.hpp"

#if defined(CFIRE_HAS_WIN32_BACKEND)

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

[[nodiscard]] inline auto win32_open_file_dialog(std::string_view dialog_title, HWND owner = nullptr) -> std::string {
    wchar_t file_buf[MAX_PATH] = {0};
    std::wstring wtitle = utf8_to_wide(dialog_title);
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = owner ? owner : ::GetActiveWindow();
    ofn.lpstrFile = file_buf;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrFilter = L"All Files (*.*)\0*.*\0";
    ofn.nFilterIndex = 1;
    ofn.lpstrTitle = wtitle.c_str();
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;
    if (::GetOpenFileNameW(&ofn)) {
        return wide_to_utf8(file_buf);
    }
    return "";
}

[[nodiscard]] inline auto win32_save_file_dialog(std::string_view dialog_title, std::string_view default_name, HWND owner = nullptr) -> std::string {
    wchar_t file_buf[MAX_PATH] = {0};
    if (!default_name.empty()) {
        std::wstring wdef = utf8_to_wide(default_name);
        std::wcsncpy(file_buf, wdef.c_str(), MAX_PATH - 1);
    }
    std::wstring wtitle = utf8_to_wide(dialog_title);
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = owner ? owner : ::GetActiveWindow();
    ofn.lpstrFile = file_buf;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrFilter = L"All Files (*.*)\0*.*\0";
    ofn.nFilterIndex = 1;
    ofn.lpstrTitle = wtitle.c_str();
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_NOCHANGEDIR;
    if (::GetSaveFileNameW(&ofn)) {
        return wide_to_utf8(file_buf);
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
};

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

#if __has_include(<WebView2.h>)
#include <WebView2.h>
#define CFIRE_HAS_WEBVIEW2_HEADER 1
#else
#define CFIRE_HAS_WEBVIEW2_HEADER 0
#endif

inline void run_win32_webview2(const std::shared_ptr<AppState>& state_, bool offscreen_test = false, std::int32_t timeout_ms = 3000) {
    (void)::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    HINSTANCE hInst = ::GetModuleHandleW(nullptr);
    const wchar_t* kClassName = L"CfireWebUIWindowClass";

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = cfire_win32_wndproc;
    wc.hInstance = hInst;
    wc.hCursor = ::LoadCursorW(nullptr, MAKEINTRESOURCEW(32512)); // IDC_ARROW
    wc.hbrBackground = ::CreateSolidBrush(RGB(13, 17, 23));
    wc.lpszClassName = kClassName;
    ::RegisterClassExW(&wc);

    Win32WindowContext ctx{state_, nullptr, nullptr};
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

#if !CFIRE_HAS_WEBVIEW2_HEADER
    if (!offscreen_test) {
        std::fprintf(stderr, "webui: WebView2 SDK header (<WebView2.h>) was not present at compile time; install the WebView2 SDK to render WebUI content on Windows.\n");
    }
#endif

    if (state_->dark_mode) {
        BOOL dark = TRUE;
        (void)::DwmSetWindowAttribute(hwnd, 20 /* DWMWA_USE_IMMERSIVE_DARK_MODE */, &dark, sizeof(dark));
    }

    if (!state_->icon_path.empty()) {
        std::wstring wicon = utf8_to_wide(state_->icon_path);
        HANDLE hIcon = ::LoadImageW(nullptr, wicon.c_str(), IMAGE_ICON, 0, 0, LR_LOADFROMFILE | LR_DEFAULTSIZE);
        if (hIcon) {
            ::SendMessageW(hwnd, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(hIcon));
            ::SendMessageW(hwnd, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(hIcon));
        }
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
            std::wstring wticon = utf8_to_wide(state_->tray_icon_path);
            nid.hIcon = static_cast<HICON>(::LoadImageW(nullptr, wticon.c_str(), IMAGE_ICON, 0, 0, LR_LOADFROMFILE | LR_DEFAULTSIZE));
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
        state_->platform_minimize = nullptr;
        state_->platform_maximize = nullptr;
        state_->platform_drag = nullptr;
        state_->platform_hide_window = nullptr;
        state_->platform_show_window = nullptr;
        state_->platform_quit = nullptr;
    }
    ::CoUninitialize();
}

} // namespace cfire::webui::detail

#endif // defined(CFIRE_HAS_WIN32_BACKEND)

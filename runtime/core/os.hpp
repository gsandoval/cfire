#pragma once
#include "../cfire_prelude.hpp"
#include "fs.hpp"

#include <span>
#include <string>
#include <string_view>
#include <vector>
#include <expected>
#include <tuple>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <format>
#include <functional>
#include <mutex>
#include <unordered_map>
#include <csignal>

#if !defined(_WIN32)
#include <spawn.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <poll.h>
#include <signal.h>
#include <termios.h>
#include <unistd.h>

#ifdef __APPLE__
#include <TargetConditionals.h>
#if !TARGET_OS_IPHONE
#include <crt_externs.h>
#endif
#else
extern "C" char** environ;
#endif
#else
#ifndef NOMINMAX
#define NOMINMAX 1
#endif
#include <windows.h>
#include <io.h>
#include <fcntl.h>
#include <process.h>
#ifndef STDIN_FILENO
#define STDIN_FILENO 0
#define STDOUT_FILENO 1
#define STDERR_FILENO 2
#endif
#endif

namespace cfire::os {

[[nodiscard]] inline auto args() -> std::vector<std::string> {
    return cfire::detail::process_args_storage();
}

[[noreturn]] inline void exit(std::int32_t code = 0) {
    std::exit(static_cast<int>(code));
}

[[nodiscard]] inline auto getenv(std::string_view key, std::string_view fallback = "") -> std::string {
    std::string k(key);
    if (const char* v = std::getenv(k.c_str())) {
        return std::string(v);
    }
    return std::string(fallback);
}

inline void setenv(std::string_view key, std::string_view val) {
    std::string k(key);
    std::string v(val);
#if defined(_WIN32)
    ::_putenv_s(k.c_str(), v.c_str());
#else
    ::setenv(k.c_str(), v.c_str(), 1);
#endif
}

[[nodiscard]] inline auto cwd() -> std::string {
    auto u8 = std::filesystem::current_path().u8string();
    return std::string(reinterpret_cast<const char*>(u8.data()), u8.size());
}

[[nodiscard]] inline auto getpid() noexcept -> std::int64_t {
#if defined(_WIN32)
    return static_cast<std::int64_t>(::GetCurrentProcessId());
#else
    return static_cast<std::int64_t>(::getpid());
#endif
}

#if defined(_WIN32)
namespace detail {
struct Win32Utf8ConsoleInit {
    Win32Utf8ConsoleInit() noexcept {
        ::SetConsoleOutputCP(CP_UTF8);
        ::SetConsoleCP(CP_UTF8);
    }
};
inline Win32Utf8ConsoleInit g_win32_utf8_console_init{};

[[nodiscard]] inline auto win32_utf8_to_wide(std::string_view s) -> std::wstring {
    if (s.empty()) return L"";
    int len = ::MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    if (len <= 0) return L"";
    std::wstring out(static_cast<std::size_t>(len), L'\0');
    ::MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), out.data(), len);
    return out;
}

// CommandLineToArgvW-compatible argument escaping (CF-010.d)
[[nodiscard]] inline auto quote_win32_arg(std::wstring_view arg) -> std::wstring {
    if (!arg.empty() && arg.find_first_of(L" \t\n\v\"") == std::wstring_view::npos) {
        return std::wstring(arg);
    }
    std::wstring out = L"\"";
    for (auto it = arg.begin(); ; ++it) {
        std::size_t num_backslashes = 0;
        while (it != arg.end() && *it == L'\\') {
            ++it;
            ++num_backslashes;
        }
        if (it == arg.end()) {
            out.append(num_backslashes * 2, L'\\');
            break;
        } else if (*it == L'"') {
            out.append(num_backslashes * 2 + 1, L'\\');
            out.push_back(L'"');
        } else {
            out.append(num_backslashes, L'\\');
            out.push_back(*it);
        }
    }
    out.push_back(L'"');
    return out;
}
} // namespace detail
#endif

[[nodiscard]] inline auto exec(std::string_view cmd) -> std::expected<std::string, cfire::Error> {
    std::string c(cmd);
#if defined(_WIN32)
    FILE* pipe = ::_popen((c + " 2>&1").c_str(), "r");
#else
    FILE* pipe = ::popen((c + " 2>&1").c_str(), "r");
#endif
    if (!pipe) {
        return cfire::make_error(std::format("failed to spawn command: {}", cmd));
    }
    std::string out;
    char buf[2048];
    while (std::fgets(buf, sizeof(buf), pipe) != nullptr) {
        out.append(buf);
    }
#if defined(_WIN32)
    int code = ::_pclose(pipe);
#else
    int status = ::pclose(pipe);
    int code = WIFEXITED(status) ? WEXITSTATUS(status) : status;
#endif
    if (code != 0) {
        return cfire::make_error(std::format("command exited with {}: {}", code, out));
    }
    return out;
}

[[nodiscard]] inline auto exec_output(std::string_view cmd) -> std::expected<std::string, cfire::Error> {
    return exec(cmd);
}

struct ExecResult {
    int exit_code = 0;
    std::string stdout_cf_{};
    std::string stderr_cf_{};
    [[nodiscard]] auto success() const noexcept -> bool { return exit_code == 0; }
};

// --- Standard Streams & Safe Pipe Handles (`CF-089`) ---

[[nodiscard]] inline auto stdin_file() noexcept -> cfire::fs::File {
    return cfire::fs::File(STDIN_FILENO, false);
}

[[nodiscard]] inline auto stdout_file() noexcept -> cfire::fs::File {
    return cfire::fs::File(STDOUT_FILENO, false);
}

[[nodiscard]] inline auto stderr_file() noexcept -> cfire::fs::File {
    return cfire::fs::File(STDERR_FILENO, false);
}

[[nodiscard]] inline auto is_tty(std::int32_t fd = STDIN_FILENO) noexcept -> bool {
    return cfire::detail::fd_is_tty(static_cast<int>(fd));
}

[[nodiscard]] inline auto pipe() -> std::expected<std::tuple<cfire::fs::File, cfire::fs::File>, cfire::Error> {
    int pfd[2]{-1, -1};
#if defined(_WIN32)
    if (::_pipe(pfd, 4096, _O_BINARY | _O_NOINHERIT) != 0) {
        return cfire::make_error(std::format("pipe failed: {}", std::strerror(errno)));
    }
#else
    if (::pipe(pfd) != 0) {
        return cfire::make_error(std::format("pipe failed: {}", std::strerror(errno)));
    }
    (void)::fcntl(pfd[0], F_SETFD, FD_CLOEXEC);
    (void)::fcntl(pfd[1], F_SETFD, FD_CLOEXEC);
#endif
    return std::tuple<cfire::fs::File, cfire::fs::File>{
        cfire::fs::File(pfd[0], true),
        cfire::fs::File(pfd[1], true)
    };
}

[[nodiscard]] inline auto command(std::string_view binary, std::span<const std::string> args = {})
    -> std::expected<ExecResult, cfire::Error>
{
#if defined(_WIN32)
    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(SECURITY_ATTRIBUTES);
    sa.bInheritHandle = TRUE;
    sa.lpSecurityDescriptor = nullptr;

    HANDLE out_read = nullptr, out_write = nullptr;
    HANDLE err_read = nullptr, err_write = nullptr;
    if (!::CreatePipe(&out_read, &out_write, &sa, 0) ||
        !::SetHandleInformation(out_read, HANDLE_FLAG_INHERIT, 0) ||
        !::CreatePipe(&err_read, &err_write, &sa, 0) ||
        !::SetHandleInformation(err_read, HANDLE_FLAG_INHERIT, 0)) {
        if (out_read) ::CloseHandle(out_read);
        if (out_write) ::CloseHandle(out_write);
        if (err_read) ::CloseHandle(err_read);
        if (err_write) ::CloseHandle(err_write);
        return cfire::make_error("failed to create Win32 subprocess pipes");
    }

    std::wstring cmd_line = detail::quote_win32_arg(detail::win32_utf8_to_wide(binary));
    for (const auto& a : args) {
        cmd_line.push_back(L' ');
        cmd_line += detail::quote_win32_arg(detail::win32_utf8_to_wide(a));
    }

    STARTUPINFOW si{};
    si.cb = sizeof(STARTUPINFOW);
    si.hStdOutput = out_write;
    si.hStdError = err_write;
    si.hStdInput = ::GetStdHandle(STD_INPUT_HANDLE);
    si.dwFlags |= STARTF_USESTDHANDLES;

    PROCESS_INFORMATION pi{};
    BOOL ok = ::CreateProcessW(
        nullptr,
        cmd_line.data(),
        nullptr,
        nullptr,
        TRUE,
        CREATE_NO_WINDOW,
        nullptr,
        nullptr,
        &si,
        &pi);
    ::CloseHandle(out_write);
    ::CloseHandle(err_write);

    if (!ok) {
        ::CloseHandle(out_read);
        ::CloseHandle(err_read);
        return cfire::make_error(std::format("CreateProcessW failed for '{}'", binary));
    }

    auto read_handle = [](HANDLE h) -> std::string {
        std::string out;
        char buf[4096];
        DWORD n = 0;
        while (::ReadFile(h, buf, sizeof(buf), &n, nullptr) && n > 0) {
            out.append(buf, static_cast<std::size_t>(n));
        }
        ::CloseHandle(h);
        return out;
    };

    ExecResult res;
    std::thread err_thread([&]() {
        res.stderr_cf_ = read_handle(err_read);
    });
    res.stdout_cf_ = read_handle(out_read);
    err_thread.join();
    ::WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD exit_code = 0;
    ::GetExitCodeProcess(pi.hProcess, &exit_code);
    ::CloseHandle(pi.hProcess);
    ::CloseHandle(pi.hThread);
    res.exit_code = static_cast<int>(exit_code);
    return res;
#else
    int stdout_pipe[2]{-1, -1};
    int stderr_pipe[2]{-1, -1};
    if (::pipe(stdout_pipe) != 0 || ::pipe(stderr_pipe) != 0) {
        if (stdout_pipe[0] >= 0) { ::close(stdout_pipe[0]); ::close(stdout_pipe[1]); }
        if (stderr_pipe[0] >= 0) { ::close(stderr_pipe[0]); ::close(stderr_pipe[1]); }
        return cfire::make_error("failed to create subprocess pipes");
    }

    posix_spawn_file_actions_t actions;
    ::posix_spawn_file_actions_init(&actions);
    ::posix_spawn_file_actions_addclose(&actions, stdout_pipe[0]);
    ::posix_spawn_file_actions_addclose(&actions, stderr_pipe[0]);
    ::posix_spawn_file_actions_adddup2(&actions, stdout_pipe[1], STDOUT_FILENO);
    ::posix_spawn_file_actions_adddup2(&actions, stderr_pipe[1], STDERR_FILENO);
    ::posix_spawn_file_actions_addclose(&actions, stdout_pipe[1]);
    ::posix_spawn_file_actions_addclose(&actions, stderr_pipe[1]);

    std::vector<char*> argv_ptrs;
    std::string bin_str(binary);
    argv_ptrs.push_back(bin_str.data());
    std::vector<std::string> args_storage(args.begin(), args.end());
    for (auto& a : args_storage) {
        argv_ptrs.push_back(a.data());
    }
    argv_ptrs.push_back(nullptr);

    pid_t pid = 0;
#if defined(__APPLE__) && !TARGET_OS_IPHONE
    char** env = *_NSGetEnviron();
#elif defined(__APPLE__) && TARGET_OS_IPHONE
    char** env = nullptr;
#else
    char** env = ::environ;
#endif

    int spawn_rc = ::posix_spawnp(&pid, bin_str.c_str(), &actions, nullptr, argv_ptrs.data(), env);
    ::posix_spawn_file_actions_destroy(&actions);
    ::close(stdout_pipe[1]);
    ::close(stderr_pipe[1]);

    if (spawn_rc != 0) {
        ::close(stdout_pipe[0]);
        ::close(stderr_pipe[0]);
        return cfire::make_error(std::format("posix_spawnp failed for '{}': {}", binary, std::strerror(spawn_rc)));
    }

    ExecResult res;
    struct pollfd pfds[2]{};
    pfds[0].fd = stdout_pipe[0];
    pfds[0].events = POLLIN;
    pfds[1].fd = stderr_pipe[0];
    pfds[1].events = POLLIN;
    int open_pipes = 2;
    char buf[4096];

    while (open_pipes > 0) {
        int prc = ::poll(pfds, 2, -1);
        if (prc < 0) {
            if (errno == EINTR) continue;
            break;
        }
        auto drain_one = [&](int idx, std::string& dst) {
            if (pfds[idx].fd < 0) return;
            if (pfds[idx].revents & (POLLIN | POLLHUP | POLLERR)) {
                ssize_t n = ::read(pfds[idx].fd, buf, sizeof(buf));
                if (n > 0) {
                    dst.append(buf, static_cast<std::size_t>(n));
                } else if (n == 0 || (n < 0 && errno != EINTR && errno != EAGAIN)) {
                    ::close(pfds[idx].fd);
                    pfds[idx].fd = -1;
                    --open_pipes;
                }
            }
        };
        drain_one(0, res.stdout_cf_);
        drain_one(1, res.stderr_cf_);
    }
    if (pfds[0].fd >= 0) ::close(pfds[0].fd);
    if (pfds[1].fd >= 0) ::close(pfds[1].fd);

    int status = 0;
    while (::waitpid(pid, &status, 0) < 0 && errno == EINTR) {}
    res.exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : status;
    return res;
#endif
}

[[nodiscard]] inline auto command(std::string_view binary, std::initializer_list<std::string_view> args)
    -> std::expected<ExecResult, cfire::Error>
{
    std::vector<std::string> vec;
    for (auto a : args) vec.emplace_back(a);
    return command(binary, std::span<const std::string>(vec));
}

// --- Terminal & Event Loop Primitives (`CF-043`, `CF-010`) ---
struct TermSize {
    std::int32_t cols = 80;
    std::int32_t rows = 24;
};

[[nodiscard]] inline auto term_size(std::int32_t fd = STDIN_FILENO) noexcept -> TermSize {
#if defined(_WIN32)
    (void)fd;
    CONSOLE_SCREEN_BUFFER_INFO csbi{};
    if (::GetConsoleScreenBufferInfo(::GetStdHandle(STD_OUTPUT_HANDLE), &csbi)) {
        std::int32_t cols = csbi.srWindow.Right - csbi.srWindow.Left + 1;
        std::int32_t rows = csbi.srWindow.Bottom - csbi.srWindow.Top + 1;
        if (cols > 0 && rows > 0) return TermSize{cols, rows};
    }
    return TermSize{80, 24};
#else
    struct winsize ws{};
    if (::ioctl(fd, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0 && ws.ws_row > 0) {
        return TermSize{static_cast<std::int32_t>(ws.ws_col), static_cast<std::int32_t>(ws.ws_row)};
    }
    return TermSize{80, 24};
#endif
}

namespace detail {
struct AtexitTermState {
#if defined(_WIN32)
    HANDLE handle = INVALID_HANDLE_VALUE;
    DWORD orig_mode = 0;
#else
    int fd = -1;
    struct termios orig{};
#endif
    bool active = false;
};

inline auto atexit_term_state() noexcept -> AtexitTermState& {
    static AtexitTermState st{};
    return st;
}

inline void atexit_restore_term() noexcept {
    auto& st = atexit_term_state();
    if (!st.active) return;
#if defined(_WIN32)
    if (st.handle != INVALID_HANDLE_VALUE) {
        ::SetConsoleMode(st.handle, st.orig_mode);
    }
#else
    if (st.fd >= 0) {
        ::tcsetattr(st.fd, TCSAFLUSH, &st.orig);
    }
#endif
    st.active = false;
}

inline void register_atexit_term(
#if defined(_WIN32)
    HANDLE h, DWORD orig
#else
    int fd, const struct termios& orig
#endif
) noexcept {
    static bool registered = false;
    auto& st = atexit_term_state();
#if defined(_WIN32)
    st.handle = h;
    st.orig_mode = orig;
#else
    st.fd = fd;
    st.orig = orig;
#endif
    st.active = true;
    if (!registered) {
        registered = true;
        std::atexit(atexit_restore_term);
    }
}
} // namespace detail

class RawTerminal {
public:
    RawTerminal() = default;
#if defined(_WIN32)
    RawTerminal(HANDLE h, DWORD orig) noexcept : handle_(h), active_(true), orig_mode_(orig) {
        detail::register_atexit_term(h, orig);
    }
#else
    RawTerminal(int fd, const struct termios& orig) noexcept : fd_(fd), active_(true), orig_(orig) {
        detail::register_atexit_term(fd, orig);
    }
#endif
    ~RawTerminal() { restore(); }
    RawTerminal(const RawTerminal&) = delete;
    auto operator=(const RawTerminal&) -> RawTerminal& = delete;
    RawTerminal(RawTerminal&& o) noexcept
#if defined(_WIN32)
        : handle_(o.handle_), active_(o.active_), orig_mode_(o.orig_mode_)
#else
        : fd_(o.fd_), active_(o.active_), orig_(o.orig_)
#endif
    {
        o.active_ = false;
    }
    auto operator=(RawTerminal&& o) noexcept -> RawTerminal& {
        if (this != &o) {
            restore();
#if defined(_WIN32)
            handle_ = o.handle_;
            orig_mode_ = o.orig_mode_;
#else
            fd_ = o.fd_;
            orig_ = o.orig_;
#endif
            active_ = o.active_;
            o.active_ = false;
        }
        return *this;
    }
    void restore() noexcept {
#if defined(_WIN32)
        if (active_ && handle_ != INVALID_HANDLE_VALUE) {
            ::SetConsoleMode(handle_, orig_mode_);
            active_ = false;
            detail::atexit_term_state().active = false;
        }
#else
        if (active_ && fd_ >= 0) {
            ::tcsetattr(fd_, TCSAFLUSH, &orig_);
            active_ = false;
            detail::atexit_term_state().active = false;
        }
#endif
    }
private:
#if defined(_WIN32)
    HANDLE handle_ = INVALID_HANDLE_VALUE;
    bool active_ = false;
    DWORD orig_mode_ = 0;
#else
    int fd_ = -1;
    bool active_ = false;
    struct termios orig_{};
#endif
};

[[nodiscard]] inline auto make_raw(std::int32_t fd = STDIN_FILENO) -> std::expected<RawTerminal, cfire::Error> {
#if defined(_WIN32)
    (void)fd;
    HANDLE hIn = ::GetStdHandle(STD_INPUT_HANDLE);
    DWORD orig = 0;
    if (hIn == INVALID_HANDLE_VALUE || !::GetConsoleMode(hIn, &orig)) {
        return cfire::make_error("GetConsoleMode failed (not a console)");
    }
#ifndef ENABLE_VIRTUAL_TERMINAL_INPUT
#define ENABLE_VIRTUAL_TERMINAL_INPUT 0x0200
#endif
    DWORD raw = (orig & ~static_cast<DWORD>(ENABLE_ECHO_INPUT | ENABLE_LINE_INPUT | ENABLE_PROCESSED_INPUT)) |
                static_cast<DWORD>(ENABLE_VIRTUAL_TERMINAL_INPUT);
    if (!::SetConsoleMode(hIn, raw)) {
        return cfire::make_error("SetConsoleMode failed");
    }
    return RawTerminal(hIn, orig);
#else
    struct termios orig{};
    if (::tcgetattr(fd, &orig) != 0) {
        return cfire::make_error("tcgetattr failed (not a tty)");
    }
    struct termios raw = orig;
    ::cfmakeraw(&raw);
    if (::tcsetattr(fd, TCSAFLUSH, &raw) != 0) {
        return cfire::make_error("tcsetattr failed");
    }
    return RawTerminal(fd, orig);
#endif
}

namespace detail {
inline auto signal_mutex() -> std::mutex& {
    static std::mutex mu;
    return mu;
}
inline auto signal_handlers() -> std::array<std::function<void(std::int32_t)>, 64>& {
    static std::array<std::function<void(std::int32_t)>, 64> handlers{};
    return handlers;
}
inline void signal_trampoline(int sig) {
    if (sig >= 0 && sig < 64) {
        auto& h = signal_handlers()[static_cast<std::size_t>(sig)];
        if (h) h(static_cast<std::int32_t>(sig));
    }
}
} // namespace detail

inline void on_signal(std::int32_t sig, std::function<void(std::int32_t)> handler) {
    if (sig >= 0 && sig < 64) {
        std::lock_guard<std::mutex> lk(detail::signal_mutex());
        detail::signal_handlers()[static_cast<std::size_t>(sig)] = std::move(handler);
        std::signal(static_cast<int>(sig), detail::signal_trampoline);
    }
}

} // namespace cfire::os

#pragma once
#include "../cfire_prelude.hpp"

#include <span>
#include <string>
#include <string_view>
#include <vector>
#include <expected>
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

[[nodiscard]] inline auto cwd() -> std::filesystem::path {
    return std::filesystem::current_path();
}

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

// --- Safe File Descriptor Operations (`CF-024`, `CF-038`, `CF-043`, `CF-010`) ---
inline constexpr std::int32_t STDIN = STDIN_FILENO;
inline constexpr std::int32_t STDOUT = STDOUT_FILENO;
inline constexpr std::int32_t STDERR = STDERR_FILENO;

[[nodiscard]] inline auto is_tty(std::int32_t fd = STDIN_FILENO) noexcept -> bool {
#if defined(_WIN32)
    return fd >= 0 && ::_isatty(static_cast<int>(fd)) != 0;
#else
    return fd >= 0 && ::isatty(fd) != 0;
#endif
}

[[nodiscard]] inline auto fd_read(std::int32_t fd, std::span<std::byte> buf)
    -> std::expected<std::size_t, cfire::Error>
{
    return cfire::detail::fd_read(static_cast<int>(fd), buf);
}

[[nodiscard]] inline auto fd_read(std::int32_t fd, std::span<std::uint8_t> buf)
    -> std::expected<std::size_t, cfire::Error>
{
    return cfire::detail::fd_read(static_cast<int>(fd), std::as_writable_bytes(buf));
}

[[nodiscard]] inline auto fd_read_bytes(std::int32_t fd, std::size_t max_bytes = 4096)
    -> std::expected<std::vector<std::byte>, cfire::Error>
{
    std::vector<std::byte> buf(max_bytes);
    auto n = fd_read(fd, std::span<std::byte>(buf));
    if (!n) return std::unexpected(n.error());
    buf.resize(*n);
    return buf;
}

[[nodiscard]] inline auto fd_write(std::int32_t fd, std::span<const std::byte> data)
    -> std::expected<std::size_t, cfire::Error>
{
    return cfire::detail::fd_write_all(static_cast<int>(fd), data);
}

[[nodiscard]] inline auto fd_write(std::int32_t fd, std::span<const std::uint8_t> data)
    -> std::expected<std::size_t, cfire::Error>
{
    return cfire::detail::fd_write_all(static_cast<int>(fd), std::as_bytes(data));
}

[[nodiscard]] inline auto fd_write(std::int32_t fd, std::string_view s)
    -> std::expected<std::size_t, cfire::Error>
{
    return cfire::detail::fd_write_all(static_cast<int>(fd), std::as_bytes(std::span(s.data(), s.size())));
}

[[nodiscard]] inline auto fd_close(std::int32_t fd) -> std::expected<void, cfire::Error> {
    if (fd < 0) return cfire::make_error("cannot close negative file descriptor");
#if defined(_WIN32)
    if (::_close(static_cast<int>(fd)) != 0) {
        return cfire::make_error(std::format("fd_close({}) failed: {}", fd, std::strerror(errno)));
    }
    return {};
#else
    while (::close(static_cast<int>(fd)) != 0) {
        if (errno == EINTR) continue;
        return cfire::make_error(std::format("fd_close({}) failed: {}", fd, std::strerror(errno)));
    }
    return {};
#endif
}

[[nodiscard]] inline auto pipe() -> std::expected<std::vector<std::int32_t>, cfire::Error> {
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
    return std::vector<std::int32_t>{pfd[0], pfd[1]};
}

[[nodiscard]] inline auto read_stdin_bytes(std::size_t max_bytes = 64ULL * 1024ULL * 1024ULL)
    -> std::expected<std::vector<std::byte>, cfire::Error>
{
    if (max_bytes == 0) {
        max_bytes = 64ULL * 1024ULL * 1024ULL;
    }
    return cfire::detail::fd_read_all_bounded(STDIN_FILENO, max_bytes);
}

[[nodiscard]] inline auto read_stdin(std::size_t max_bytes = 64ULL * 1024ULL * 1024ULL)
    -> std::expected<std::string, cfire::Error>
{
    auto b = read_stdin_bytes(max_bytes);
    if (!b) return std::unexpected(b.error());
    return std::string(reinterpret_cast<const char*>(b->data()), b->size());
}

[[nodiscard]] inline auto read_stdin_lines() -> std::expected<std::vector<std::string>, cfire::Error> {
    std::vector<std::string> lines;
    while (auto line = cfire::detail::fd_read_line(STDIN_FILENO)) {
        lines.push_back(std::move(*line));
    }
    return lines;
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

    std::string cmd_line = std::format("\"{}\"", binary);
    for (const auto& a : args) {
        cmd_line += std::format(" \"{}\"", a);
    }

    STARTUPINFOA si{};
    si.cb = sizeof(STARTUPINFOA);
    si.hStdOutput = out_write;
    si.hStdError = err_write;
    si.hStdInput = ::GetStdHandle(STD_INPUT_HANDLE);
    si.dwFlags |= STARTF_USESTDHANDLES;

    PROCESS_INFORMATION pi{};
    BOOL ok = ::CreateProcessA(
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
        return cfire::make_error(std::format("CreateProcessA failed for '{}'", binary));
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
    res.stdout_cf_ = read_handle(out_read);
    res.stderr_cf_ = read_handle(err_read);
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
    auto read_pipe = [](int fd) -> std::string {
        std::string out;
        char buf[4096];
        while (true) {
            ssize_t n = ::read(fd, buf, sizeof(buf));
            if (n < 0) {
                if (errno == EINTR) continue;
                break;
            }
            if (n == 0) break;
            out.append(buf, static_cast<std::size_t>(n));
        }
        ::close(fd);
        return out;
    };
    res.stdout_cf_ = read_pipe(stdout_pipe[0]);
    res.stderr_cf_ = read_pipe(stderr_pipe[0]);

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

class RawTerminal {
public:
    RawTerminal() = default;
#if defined(_WIN32)
    RawTerminal(HANDLE h, DWORD orig) noexcept : handle_(h), active_(true), orig_mode_(orig) {}
#else
    RawTerminal(int fd, const struct termios& orig) noexcept : fd_(fd), active_(true), orig_(orig) {}
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
        }
#else
        if (active_ && fd_ >= 0) {
            ::tcsetattr(fd_, TCSAFLUSH, &orig_);
            active_ = false;
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
    DWORD raw = orig & ~static_cast<DWORD>(ENABLE_ECHO_INPUT | ENABLE_LINE_INPUT | ENABLE_PROCESSED_INPUT);
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

[[nodiscard]] inline auto poll_fd(std::int32_t fd, std::int64_t timeout_ms = 0) noexcept -> bool {
    if (fd < 0) return false;
#if defined(_WIN32)
    intptr_t osf = ::_get_osfhandle(static_cast<int>(fd));
    if (osf == -1) return false;
    DWORD wait_ms = timeout_ms < 0 ? INFINITE : static_cast<DWORD>(timeout_ms);
    return ::WaitForSingleObject(reinterpret_cast<HANDLE>(osf), wait_ms) == WAIT_OBJECT_0;
#else
    struct pollfd pfd{};
    pfd.fd = fd;
    pfd.events = POLLIN;
    int rc = 0;
    while ((rc = ::poll(&pfd, 1, static_cast<int>(timeout_ms))) < 0 && errno == EINTR) {}
    return rc > 0 && (pfd.revents & (POLLIN | POLLHUP)) != 0;
#endif
}

[[nodiscard]] inline auto socketpair() -> std::expected<std::vector<std::int32_t>, cfire::Error> {
#if defined(_WIN32)
    return pipe();
#else
    int sv[2]{-1, -1};
    if (::socketpair(AF_UNIX, SOCK_STREAM, 0, sv) != 0) {
        return cfire::make_error("socketpair failed");
    }
    (void)::fcntl(sv[0], F_SETFD, FD_CLOEXEC);
    (void)::fcntl(sv[1], F_SETFD, FD_CLOEXEC);
    return std::vector<std::int32_t>{sv[0], sv[1]};
#endif
}

namespace detail {
inline auto signal_handlers() -> std::unordered_map<int, std::function<void(std::int32_t)>>& {
    static std::unordered_map<int, std::function<void(std::int32_t)>> map;
    return map;
}
inline void signal_trampoline(int sig) {
    auto& m = signal_handlers();
    if (auto it = m.find(sig); it != m.end() && it->second) {
        it->second(static_cast<std::int32_t>(sig));
    }
}
} // namespace detail

inline void on_signal(std::int32_t sig, std::function<void(std::int32_t)> handler) {
    detail::signal_handlers()[static_cast<int>(sig)] = std::move(handler);
    std::signal(static_cast<int>(sig), detail::signal_trampoline);
}

} // namespace cfire::os

#pragma once
#include "../../cfire_prelude.hpp"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <expected>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#if defined(__EMSCRIPTEN__) || defined(PLATFORM_WEB)
// Wasm stub — no OS local IPC sockets or named pipes in browser sandbox.
#elif defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX 1
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN 1
#endif
#include <windows.h>
#include <sddl.h>
#if defined(_MSC_VER)
#pragma comment(lib, "advapi32.lib")
#endif
#else
#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/un.h>
#include <unistd.h>
#if defined(__APPLE__)
#include <sys/ucred.h>
#ifndef SOL_LOCAL
#define SOL_LOCAL 0
#endif
#ifndef LOCAL_PEERPID
#define LOCAL_PEERPID 0x002
#endif
#endif
#endif

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

// --- `core:net/ipc` (Cross-Platform Local IPC & Peer Credentials: AF_UNIX on POSIX / Named Pipes on Windows) ---
namespace cfire::ipc {

struct PeerCred {
    std::int64_t pid = -1;
    bool same_user = false;
    std::int64_t uid = -1;

    friend constexpr auto operator==(const PeerCred&, const PeerCred&) noexcept -> bool = default;
};

namespace detail {
inline constexpr std::int32_t kErrExists = 17;
inline constexpr std::int32_t kErrNotFound = 2;
inline constexpr std::int32_t kErrPermission = 13;

#if !defined(_WIN32) && !defined(__EMSCRIPTEN__) && !defined(PLATFORM_WEB)
[[nodiscard]] inline auto classify_errno(int e) noexcept -> std::int32_t {
    if (e == EEXIST || e == EADDRINUSE) return kErrExists;
    if (e == ENOENT || e == ENOTDIR) return kErrNotFound;
    if (e == EACCES || e == EPERM) return kErrPermission;
    return static_cast<std::int32_t>(e);
}

inline void set_cloexec_and_nosigpipe(int fd) noexcept {
    if (fd < 0) return;
    (void)::fcntl(fd, F_SETFD, FD_CLOEXEC);
#if defined(__APPLE__) && defined(SO_NOSIGPIPE)
    int one = 1;
    (void)::setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof(one));
#endif
}

inline void set_nonblocking(int fd, bool on) noexcept {
    if (fd < 0) return;
    int flags = ::fcntl(fd, F_GETFL, 0);
    if (flags < 0) return;
    (void)::fcntl(fd, F_SETFL, on ? (flags | O_NONBLOCK) : (flags & ~O_NONBLOCK));
}

inline void set_fd_timeout_ms(int fd, int ms) noexcept {
    if (fd < 0) return;
    timeval tv{};
    if (ms > 0) {
        tv.tv_sec = ms / 1000;
        tv.tv_usec = (ms % 1000) * 1000;
    }
    (void)::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    (void)::setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
}

[[nodiscard]] inline auto sanitize_name(std::string_view raw) -> std::string {
    std::string out;
    out.reserve(raw.size());
    for (char c : raw) {
        bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.';
        out.push_back(ok ? c : '-');
    }
    if (out.empty() || out == "." || out == "..") return "cfire-ipc";
    if (out.ends_with(".sock") && out.size() > 5) out.resize(out.size() - 5);
    return out;
}

[[nodiscard]] inline auto ensure_private_dir(const std::string& dir) noexcept -> bool {
    if (dir.empty()) return false;
    struct stat st{};
    if (::lstat(dir.c_str(), &st) != 0) {
        if (errno != ENOENT) return false;
        if (::mkdir(dir.c_str(), 0700) != 0 && errno != EEXIST) return false;
        if (::lstat(dir.c_str(), &st) != 0) return false;
    }
    if (!S_ISDIR(st.st_mode) || S_ISLNK(st.st_mode) || st.st_uid != ::geteuid()) {
        return false;
    }
    (void)::chmod(dir.c_str(), 0700);
    return true;
}

[[nodiscard]] inline auto build_sockaddr(std::string_view ep, sockaddr_un& addr, socklen_t& addr_len) noexcept -> bool {
    if (ep.empty() || ep.size() >= sizeof(addr.sun_path)) return false;
    std::memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    std::memcpy(addr.sun_path, ep.data(), ep.size());
    addr.sun_path[ep.size()] = '\0';
    addr_len = static_cast<socklen_t>(offsetof(sockaddr_un, sun_path) + ep.size() + 1);
    return true;
}
#endif

#if defined(_WIN32)
[[nodiscard]] inline auto classify_win32_error(DWORD err) noexcept -> std::int32_t {
    if (err == ERROR_ACCESS_DENIED || err == ERROR_PIPE_BUSY || err == ERROR_ALREADY_EXISTS || err == ERROR_FILE_EXISTS) {
        return kErrExists;
    }
    if (err == ERROR_FILE_NOT_FOUND || err == ERROR_PATH_NOT_FOUND || err == ERROR_BAD_NETPATH) {
        return kErrNotFound;
    }
    if (err == ERROR_PRIVILEGE_NOT_HELD) return kErrPermission;
    return static_cast<std::int32_t>(err);
}

[[nodiscard]] inline auto utf8_to_wide(std::string_view s) -> std::wstring {
    if (s.empty()) return {};
    int needed = ::MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    if (needed <= 0) return {};
    std::wstring out(static_cast<std::size_t>(needed), L'\0');
    (void)::MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), out.data(), needed);
    return out;
}

[[nodiscard]] inline auto wide_to_utf8(const std::wstring& w) -> std::string {
    if (w.empty()) return {};
    int needed = ::WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
    if (needed <= 0) return {};
    std::string out(static_cast<std::size_t>(needed), '\0');
    (void)::WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), out.data(), needed, nullptr, nullptr);
    return out;
}

struct TokenUserHolder {
    std::vector<std::uint8_t> buf{};
    [[nodiscard]] auto sid() const noexcept -> PSID {
        if (buf.empty()) return nullptr;
        return reinterpret_cast<const TOKEN_USER*>(buf.data())->User.Sid;
    }
};

[[nodiscard]] inline auto query_token_user(HANDLE proc, TokenUserHolder& out) noexcept -> bool {
    HANDLE tok = nullptr;
    if (!::OpenProcessToken(proc, TOKEN_QUERY, &tok)) return false;
    DWORD needed = 0;
    (void)::GetTokenInformation(tok, TokenUser, nullptr, 0, &needed);
    if (needed == 0) {
        ::CloseHandle(tok);
        return false;
    }
    out.buf.resize(needed);
    bool ok = ::GetTokenInformation(tok, TokenUser, out.buf.data(), needed, &needed) != 0;
    ::CloseHandle(tok);
    return ok && out.sid() != nullptr;
}

[[nodiscard]] inline auto current_user_sid_wide() -> std::wstring {
    TokenUserHolder tu;
    if (!query_token_user(::GetCurrentProcess(), tu)) return L"S-1-5-18";
    LPWSTR sid_str = nullptr;
    if (!::ConvertSidToStringSidW(tu.sid(), &sid_str) || !sid_str) return L"S-1-5-18";
    std::wstring res(sid_str);
    ::LocalFree(sid_str);
    return res;
}

struct SecurityDescGuard {
    PSECURITY_DESCRIPTOR sd = nullptr;
    SECURITY_ATTRIBUTES sa{};
    ~SecurityDescGuard() {
        if (sd) ::LocalFree(sd);
    }
    SecurityDescGuard() = default;
    SecurityDescGuard(const SecurityDescGuard&) = delete;
    auto operator=(const SecurityDescGuard&) -> SecurityDescGuard& = delete;

    [[nodiscard]] auto init_owner_only() -> bool {
        if (sd) {
            ::LocalFree(sd);
            sd = nullptr;
        }
        std::wstring sddl = L"D:P(A;;GA;;;SY)(A;;GA;;;" + current_user_sid_wide() + L")";
        if (!::ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl.c_str(), SDDL_REVISION_1, &sd, nullptr)) {
            return false;
        }
        sa.nLength = sizeof(SECURITY_ATTRIBUTES);
        sa.lpSecurityDescriptor = sd;
        sa.bInheritHandle = FALSE;
        return true;
    }
};

struct EventGuard {
    HANDLE h = nullptr;
    EventGuard() noexcept : h(::CreateEventW(nullptr, TRUE, FALSE, nullptr)) {}
    ~EventGuard() {
        if (h) ::CloseHandle(h);
    }
    EventGuard(const EventGuard&) = delete;
    auto operator=(const EventGuard&) -> EventGuard& = delete;
};

[[nodiscard]] inline auto sanitize_win_name(std::string_view raw) -> std::string {
    std::string out;
    out.reserve(raw.size());
    for (char c : raw) {
        bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.';
        out.push_back(ok ? c : '-');
    }
    if (out.empty()) return "cfire-ipc";
    return out;
}

[[nodiscard]] inline auto normalize_pipe_endpoint(std::string_view ep) -> std::string {
    std::string s(ep);
    for (char& c : s) {
        if (c == '/') c = '\\';
    }
    return s;
}
#endif
} // namespace detail

class Conn {
public:
    Conn() noexcept = default;
#if defined(_WIN32)
    Conn(HANDLE pipe_handle, bool is_server) noexcept
        : pipe_(pipe_handle), is_server_(is_server) {}
#elif !defined(__EMSCRIPTEN__) && !defined(PLATFORM_WEB)
    Conn(int fd, std::string endpoint) noexcept
        : fd_(fd), endpoint_(std::move(endpoint)) {}
#endif

    ~Conn() { close(); }

    Conn(const Conn&) = delete;
    auto operator=(const Conn&) -> Conn& = delete;

    Conn(Conn&& other) noexcept
#if defined(_WIN32)
        : pipe_(std::exchange(other.pipe_, INVALID_HANDLE_VALUE)),
          is_server_(other.is_server_),
          timeout_ms_(other.timeout_ms_) {}
#elif !defined(__EMSCRIPTEN__) && !defined(PLATFORM_WEB)
        : fd_(std::exchange(other.fd_, -1)),
          endpoint_(std::move(other.endpoint_)),
          timeout_ms_(other.timeout_ms_) {}
#else
        {}
#endif

    auto operator=(Conn&& other) noexcept -> Conn& {
        if (this != &other) {
            close();
#if defined(_WIN32)
            pipe_ = std::exchange(other.pipe_, INVALID_HANDLE_VALUE);
            is_server_ = other.is_server_;
            timeout_ms_ = other.timeout_ms_;
#elif !defined(__EMSCRIPTEN__) && !defined(PLATFORM_WEB)
            fd_ = std::exchange(other.fd_, -1);
            endpoint_ = std::move(other.endpoint_);
            timeout_ms_ = other.timeout_ms_;
#endif
        }
        return *this;
    }

    [[nodiscard]] auto is_open() const noexcept -> bool {
#if defined(_WIN32)
        return pipe_ != INVALID_HANDLE_VALUE;
#elif !defined(__EMSCRIPTEN__) && !defined(PLATFORM_WEB)
        return fd_ >= 0;
#else
        return false;
#endif
    }

    void close() noexcept {
#if defined(_WIN32)
        HANDLE h = std::exchange(pipe_, INVALID_HANDLE_VALUE);
        if (h != INVALID_HANDLE_VALUE) {
            ::CloseHandle(h);
        }
#elif !defined(__EMSCRIPTEN__) && !defined(PLATFORM_WEB)
        int fd = std::exchange(fd_, -1);
        if (fd >= 0) {
            ::shutdown(fd, SHUT_RDWR);
            ::close(fd);
        }
#endif
    }

    void set_timeout_ms(int ms) noexcept {
        timeout_ms_ = ms > 0 ? ms : 0;
#if !defined(_WIN32) && !defined(__EMSCRIPTEN__) && !defined(PLATFORM_WEB)
        detail::set_fd_timeout_ms(fd_, timeout_ms_);
#endif
    }

    [[nodiscard]] auto poll(std::int64_t timeout_ms = 0) const noexcept -> bool {
#if defined(_WIN32)
        if (pipe_ == INVALID_HANDLE_VALUE) return false;
        using clock = std::chrono::steady_clock;
        auto deadline = clock::now() + std::chrono::milliseconds(timeout_ms > 0 ? timeout_ms : 0);
        for (;;) {
            DWORD avail = 0;
            if (!::PeekNamedPipe(pipe_, nullptr, 0, nullptr, &avail, nullptr)) {
                return true;  // broken pipe / EOF is readable
            }
            if (avail > 0) return true;
            if (timeout_ms == 0) return false;
            if (timeout_ms > 0 && clock::now() >= deadline) return false;
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
#elif !defined(__EMSCRIPTEN__) && !defined(PLATFORM_WEB)
        if (fd_ < 0) return false;
        int wait_ms = timeout_ms < 0 ? -1 : static_cast<int>(std::min<std::int64_t>(timeout_ms, 2000000000LL));
        for (;;) {
            pollfd pfd{.fd = fd_, .events = POLLIN, .revents = 0};
            int rc = ::poll(&pfd, 1, wait_ms);
            if (rc < 0 && errno == EINTR) continue;
            return rc > 0 && (pfd.revents & (POLLIN | POLLHUP | POLLERR)) != 0;
        }
#else
        (void)timeout_ms;
        return false;
#endif
    }

    [[nodiscard]] auto write_bytes(std::span<const std::uint8_t> payload) -> std::expected<std::size_t, cfire::Error> {
#if defined(__EMSCRIPTEN__) || defined(PLATFORM_WEB)
        (void)payload;
        return cfire::make_error("core:net/ipc is not supported on wasm targets");
#elif defined(_WIN32)
        if (pipe_ == INVALID_HANDLE_VALUE) return cfire::make_error("ipc connection is closed");
        std::size_t total = 0;
        while (total < payload.size()) {
            DWORD to_write = static_cast<DWORD>(std::min<std::size_t>(payload.size() - total, 65536));
            detail::EventGuard ev;
            if (!ev.h) return cfire::make_error("ipc write failed: CreateEventW failed");
            OVERLAPPED ov{};
            ov.hEvent = ev.h;
            DWORD written = 0;
            BOOL ok = ::WriteFile(pipe_, payload.data() + total, to_write, &written, &ov);
            if (!ok) {
                DWORD err = ::GetLastError();
                if (err != ERROR_IO_PENDING) {
                    return cfire::make_error(std::format("ipc write failed: win32 error {}", err), detail::classify_win32_error(err));
                }
                DWORD wait_ms = timeout_ms_ > 0 ? static_cast<DWORD>(timeout_ms_) : INFINITE;
                DWORD w = ::WaitForSingleObject(ev.h, wait_ms);
                if (w == WAIT_TIMEOUT) {
                    ::CancelIoEx(pipe_, &ov);
                    (void)::GetOverlappedResult(pipe_, &ov, &written, TRUE);
                    return cfire::make_error("ipc write timed out", 408);
                }
                if (w != WAIT_OBJECT_0 || !::GetOverlappedResult(pipe_, &ov, &written, TRUE)) {
                    DWORD gerr = ::GetLastError();
                    return cfire::make_error(std::format("ipc write failed: win32 error {}", gerr), detail::classify_win32_error(gerr));
                }
            }
            if (written == 0) {
                return cfire::make_error("ipc write wrote 0 bytes");
            }
            total += static_cast<std::size_t>(written);
        }
        return total;
#else
        if (fd_ < 0) return cfire::make_error("ipc connection is closed");
        std::size_t total = 0;
        while (total < payload.size()) {
            ssize_t sent = ::send(
                fd_,
                reinterpret_cast<const char*>(payload.data() + total),
                payload.size() - total,
                MSG_NOSIGNAL
            );
            if (sent < 0) {
                if (errno == EINTR) continue;
                if (errno == EAGAIN || errno == EWOULDBLOCK) return cfire::make_error("ipc write timed out", 408);
                int err_no = errno;
                return cfire::make_error(std::format("ipc write failed: {}", std::strerror(err_no)), detail::classify_errno(err_no));
            }
            if (sent == 0) return cfire::make_error("ipc write wrote 0 bytes");
            total += static_cast<std::size_t>(sent);
        }
        return total;
#endif
    }

    [[nodiscard]] auto write_text(std::string_view payload) -> std::expected<std::size_t, cfire::Error> {
        return write_bytes(cfire::as_u8(std::span(payload.data(), payload.size())));
    }

    [[nodiscard]] auto read_into(std::span<std::uint8_t> buf) -> std::expected<std::size_t, cfire::Error> {
#if defined(__EMSCRIPTEN__) || defined(PLATFORM_WEB)
        (void)buf;
        return cfire::make_error("core:net/ipc is not supported on wasm targets");
#elif defined(_WIN32)
        if (pipe_ == INVALID_HANDLE_VALUE) return cfire::make_error("ipc connection is closed");
        if (buf.empty()) return std::size_t{0};
        DWORD to_read = static_cast<DWORD>(std::min<std::size_t>(buf.size(), 0x7fffffffU));
        detail::EventGuard ev;
        if (!ev.h) return cfire::make_error("ipc read failed: CreateEventW failed");
        OVERLAPPED ov{};
        ov.hEvent = ev.h;
        DWORD got = 0;
        BOOL ok = ::ReadFile(pipe_, buf.data(), to_read, &got, &ov);
        if (!ok) {
            DWORD err = ::GetLastError();
            if (err == ERROR_MORE_DATA) {
                return static_cast<std::size_t>(got);
            }
            if (err == ERROR_BROKEN_PIPE || err == ERROR_PIPE_NOT_CONNECTED || err == ERROR_HANDLE_EOF) {
                return std::size_t{0};
            }
            if (err != ERROR_IO_PENDING) {
                return cfire::make_error(std::format("ipc read failed: win32 error {}", err), detail::classify_win32_error(err));
            }
            DWORD wait_ms = timeout_ms_ > 0 ? static_cast<DWORD>(timeout_ms_) : INFINITE;
            DWORD w = ::WaitForSingleObject(ev.h, wait_ms);
            if (w == WAIT_TIMEOUT) {
                ::CancelIoEx(pipe_, &ov);
                (void)::GetOverlappedResult(pipe_, &ov, &got, TRUE);
                if (got > 0) return static_cast<std::size_t>(got);
                return cfire::make_error("ipc read timed out", 408);
            }
            if (w != WAIT_OBJECT_0) {
                ::CancelIoEx(pipe_, &ov);
                (void)::GetOverlappedResult(pipe_, &ov, &got, TRUE);
                return cfire::make_error("ipc read wait failed");
            }
            if (!::GetOverlappedResult(pipe_, &ov, &got, TRUE)) {
                DWORD gerr = ::GetLastError();
                if (gerr == ERROR_MORE_DATA) return static_cast<std::size_t>(got);
                if (gerr == ERROR_BROKEN_PIPE || gerr == ERROR_PIPE_NOT_CONNECTED || gerr == ERROR_HANDLE_EOF) {
                    return std::size_t{0};
                }
                return cfire::make_error(std::format("ipc read failed: win32 error {}", gerr), detail::classify_win32_error(gerr));
            }
        }
        return static_cast<std::size_t>(got);
#else
        if (fd_ < 0) return cfire::make_error("ipc connection is closed");
        if (buf.empty()) return std::size_t{0};
        for (;;) {
            ssize_t n = ::recv(fd_, reinterpret_cast<char*>(buf.data()), buf.size(), 0);
            if (n < 0) {
                if (errno == EINTR) continue;
                if (errno == EAGAIN || errno == EWOULDBLOCK) return cfire::make_error("ipc read timed out", 408);
                int err_no = errno;
                return cfire::make_error(std::format("ipc read failed: {}", std::strerror(err_no)), detail::classify_errno(err_no));
            }
            return static_cast<std::size_t>(n);
        }
#endif
    }

    [[nodiscard]] auto read_bytes(std::size_t max_bytes = 4096) -> std::expected<std::vector<std::uint8_t>, cfire::Error> {
        if (!is_open()) return cfire::make_error("ipc connection is closed");
        std::vector<std::uint8_t> buf(max_bytes);
        auto n = read_into(std::span<std::uint8_t>(buf));
        if (!n) return std::unexpected(n.error());
        buf.resize(*n);
        return buf;
    }

    [[nodiscard]] auto read_exact(std::size_t exact_bytes) -> std::expected<std::vector<std::uint8_t>, cfire::Error> {
        if (!is_open()) return cfire::make_error("ipc connection is closed");
        std::vector<std::uint8_t> buf(exact_bytes);
        std::size_t total = 0;
        while (total < exact_bytes) {
            auto n = read_into(std::span<std::uint8_t>(buf.data() + total, exact_bytes - total));
            if (!n) return std::unexpected(n.error());
            if (*n == 0) {
                return cfire::make_error(std::format("unexpected EOF in ipc read_exact (expected {} bytes, got {})", exact_bytes, total));
            }
            total += *n;
        }
        return buf;
    }

    [[nodiscard]] auto read_text(std::size_t max_bytes = 4096) -> std::expected<std::string, cfire::Error> {
        auto b = read_bytes(max_bytes);
        if (!b) return std::unexpected(b.error());
        return std::string(reinterpret_cast<const char*>(b->data()), b->size());
    }

    [[nodiscard]] auto read_bytes_until(std::uint8_t delim, std::size_t max_bytes = 65536)
        -> std::expected<std::vector<std::uint8_t>, cfire::Error>
    {
        if (!is_open()) return cfire::make_error("ipc connection is closed");
        std::vector<std::uint8_t> out;
        std::uint8_t b = 0;
        while (out.size() < max_bytes) {
            auto n = read_into(std::span<std::uint8_t>(&b, 1));
            if (!n) return std::unexpected(n.error());
            if (*n == 0) break;
            out.push_back(b);
            if (b == delim) break;
        }
        return out;
    }

    [[nodiscard]] auto read_text_until(std::string_view delim, std::size_t max_bytes = 65536)
        -> std::expected<std::string, cfire::Error>
    {
        if (!is_open()) return cfire::make_error("ipc connection is closed");
        if (delim.empty()) return cfire::make_error("read_text_until requires non-empty delimiter");
        std::string out;
        std::uint8_t b = 0;
        while (out.size() < max_bytes) {
            auto n = read_into(std::span<std::uint8_t>(&b, 1));
            if (!n) return std::unexpected(n.error());
            if (*n == 0) break;
            out.push_back(static_cast<char>(b));
            if (out.ends_with(delim)) break;
        }
        return out;
    }

    [[nodiscard]] auto read_line(std::size_t max_bytes = 65536) -> std::optional<std::string> {
        auto b = read_bytes_until(static_cast<std::uint8_t>('\n'), max_bytes);
        if (!b || b->empty()) return std::nullopt;
        std::string out(reinterpret_cast<const char*>(b->data()), b->size());
        if (!out.empty() && out.back() == '\n') {
            out.pop_back();
            if (!out.empty() && out.back() == '\r') out.pop_back();
        }
        return out;
    }

    [[nodiscard]] auto peer_credentials() const -> std::expected<PeerCred, cfire::Error> {
#if defined(__EMSCRIPTEN__) || defined(PLATFORM_WEB)
        return cfire::make_error("core:net/ipc is not supported on wasm targets");
#elif defined(_WIN32)
        if (pipe_ == INVALID_HANDLE_VALUE) return cfire::make_error("ipc connection is closed");
        ULONG peer_pid_ul = 0;
        BOOL pid_ok = is_server_
            ? ::GetNamedPipeClientProcessId(pipe_, &peer_pid_ul)
            : ::GetNamedPipeServerProcessId(pipe_, &peer_pid_ul);
        if (!pid_ok) {
            DWORD err = ::GetLastError();
            return cfire::make_error(std::format("peer_credentials failed: win32 error {}", err), detail::classify_win32_error(err));
        }
        bool same_user = false;
        HANDLE proc = ::OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, static_cast<DWORD>(peer_pid_ul));
        if (proc != nullptr) {
            detail::TokenUserHolder self_tu;
            detail::TokenUserHolder peer_tu;
            if (detail::query_token_user(::GetCurrentProcess(), self_tu) && detail::query_token_user(proc, peer_tu)) {
                same_user = (::EqualSid(self_tu.sid(), peer_tu.sid()) != 0);
            }
            ::CloseHandle(proc);
        }
        PeerCred cred;
        cred.pid = static_cast<std::int64_t>(peer_pid_ul);
        cred.same_user = same_user;
        cred.uid = -1;
        return cred;
#elif defined(__linux__)
        if (fd_ < 0) return cfire::make_error("ipc connection is closed");
        ucred cr{};
        socklen_t len = sizeof(cr);
        if (::getsockopt(fd_, SOL_SOCKET, SO_PEERCRED, &cr, &len) != 0) {
            int err_no = errno;
            return cfire::make_error(std::format("peer_credentials failed: {}", std::strerror(err_no)), detail::classify_errno(err_no));
        }
        PeerCred cred;
        cred.pid = static_cast<std::int64_t>(cr.pid);
        cred.uid = static_cast<std::int64_t>(cr.uid);
        cred.same_user = (cr.uid == ::geteuid());
        return cred;
#elif defined(__APPLE__)
        if (fd_ < 0) return cfire::make_error("ipc connection is closed");
        uid_t euid = 0;
        gid_t egid = 0;
        if (::getpeereid(fd_, &euid, &egid) != 0) {
            int err_no = errno;
            return cfire::make_error(std::format("peer_credentials failed: {}", std::strerror(err_no)), detail::classify_errno(err_no));
        }
        pid_t peer_pid = -1;
        socklen_t plen = sizeof(peer_pid);
        if (::getsockopt(fd_, SOL_LOCAL, LOCAL_PEERPID, &peer_pid, &plen) != 0) {
            peer_pid = -1;
        }
        PeerCred cred;
        cred.pid = peer_pid > 0 ? static_cast<std::int64_t>(peer_pid) : -1;
        cred.uid = static_cast<std::int64_t>(euid);
        cred.same_user = (euid == ::geteuid());
        return cred;
#else
        return cfire::make_error("peer_credentials is unsupported on this OS");
#endif
    }

private:
#if defined(_WIN32)
    HANDLE pipe_ = INVALID_HANDLE_VALUE;
    bool is_server_ = false;
#elif !defined(__EMSCRIPTEN__) && !defined(PLATFORM_WEB)
    int fd_ = -1;
    std::string endpoint_{};
#endif
    int timeout_ms_ = 0;
};

class Listener {
public:
    struct State;

    Listener() noexcept = default;
    explicit Listener(std::unique_ptr<State> state) noexcept;
    ~Listener();

    Listener(const Listener&) = delete;
    auto operator=(const Listener&) -> Listener& = delete;
    Listener(Listener&&) noexcept;
    auto operator=(Listener&& other) noexcept -> Listener&;

    [[nodiscard]] auto is_open() const noexcept -> bool;
    [[nodiscard]] auto endpoint() const -> std::string;
    [[nodiscard]] auto accept() -> std::expected<Conn, cfire::Error>;
    void close() noexcept;

private:
    std::unique_ptr<State> state_{};
};

#if defined(__EMSCRIPTEN__) || defined(PLATFORM_WEB)

struct Listener::State {
    std::string endpoint_{};
};

inline auto Listener::is_open() const noexcept -> bool { return false; }
inline auto Listener::endpoint() const -> std::string { return state_ ? state_->endpoint_ : std::string{}; }
inline auto Listener::accept() -> std::expected<Conn, cfire::Error> {
    return cfire::make_error("core:net/ipc is not supported on wasm targets");
}
inline void Listener::close() noexcept { state_.reset(); }

[[nodiscard]] inline auto endpoint(std::string_view name) -> std::string {
    return std::string(name);
}

[[nodiscard]] inline auto listen(std::string_view endpoint_path) -> std::expected<Listener, cfire::Error> {
    (void)endpoint_path;
    return cfire::make_error("core:net/ipc is not supported on wasm targets");
}

[[nodiscard]] inline auto connect(std::string_view endpoint_path, int timeout_ms = 5000) -> std::expected<Conn, cfire::Error> {
    (void)endpoint_path;
    (void)timeout_ms;
    return cfire::make_error("core:net/ipc is not supported on wasm targets");
}

#elif defined(_WIN32)

struct Listener::State {
    std::string endpoint_{};
    std::wstring wpath_{};
    detail::SecurityDescGuard sd_guard_{};
    std::mutex mu_{};
    HANDLE next_pipe_ = INVALID_HANDLE_VALUE;
    HANDLE stop_event_ = nullptr;
    std::atomic<bool> closed_{false};

    ~State() {
        if (next_pipe_ != INVALID_HANDLE_VALUE) {
            ::CloseHandle(next_pipe_);
            next_pipe_ = INVALID_HANDLE_VALUE;
        }
        if (stop_event_) {
            ::CloseHandle(stop_event_);
            stop_event_ = nullptr;
        }
    }

    [[nodiscard]] auto create_pipe_instance(bool first) -> HANDLE {
        DWORD open_mode = PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED;
        if (first) open_mode |= FILE_FLAG_FIRST_PIPE_INSTANCE;
        return ::CreateNamedPipeW(
            wpath_.c_str(),
            open_mode,
            PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS,
            PIPE_UNLIMITED_INSTANCES,
            65536,
            65536,
            0,
            &sd_guard_.sa
        );
    }
};

inline auto Listener::is_open() const noexcept -> bool {
    return state_ && !state_->closed_.load();
}

inline auto Listener::endpoint() const -> std::string {
    return state_ ? state_->endpoint_ : std::string{};
}

inline void Listener::close() noexcept {
    if (!state_) return;
    bool was_closed = state_->closed_.exchange(true);
    if (was_closed) return;
    if (state_->stop_event_) {
        ::SetEvent(state_->stop_event_);
    }
    HANDLE pending = INVALID_HANDLE_VALUE;
    {
        std::lock_guard<std::mutex> lk(state_->mu_);
        pending = std::exchange(state_->next_pipe_, INVALID_HANDLE_VALUE);
    }
    if (pending != INVALID_HANDLE_VALUE) {
        ::CloseHandle(pending);
    }
}

inline auto Listener::accept() -> std::expected<Conn, cfire::Error> {
    if (!state_ || state_->closed_.load()) {
        return cfire::make_error("ipc listener is closed");
    }
    HANDLE cur = INVALID_HANDLE_VALUE;
    {
        std::lock_guard<std::mutex> lk(state_->mu_);
        if (state_->closed_.load() || state_->next_pipe_ == INVALID_HANDLE_VALUE) {
            return cfire::make_error("ipc listener is closed");
        }
        cur = std::exchange(state_->next_pipe_, INVALID_HANDLE_VALUE);
    }

    detail::EventGuard ev;
    if (!ev.h) {
        ::CloseHandle(cur);
        return cfire::make_error("ipc accept failed: CreateEventW failed");
    }
    OVERLAPPED ov{};
    ov.hEvent = ev.h;
    BOOL ok = ::ConnectNamedPipe(cur, &ov);
    if (!ok) {
        DWORD err = ::GetLastError();
        if (err == ERROR_PIPE_CONNECTED) {
            // Peer already connected before ConnectNamedPipe ran.
        } else if (err == ERROR_IO_PENDING) {
            HANDLE waits[2] = {ev.h, state_->stop_event_};
            DWORD wait_count = state_->stop_event_ ? 2U : 1U;
            DWORD w = ::WaitForMultipleObjects(wait_count, waits, FALSE, INFINITE);
            if (w != WAIT_OBJECT_0) {
                ::CancelIoEx(cur, &ov);
                DWORD dummy = 0;
                (void)::GetOverlappedResult(cur, &ov, &dummy, TRUE);
                ::CloseHandle(cur);
                return cfire::make_error("ipc listener closed");
            }
            DWORD dummy = 0;
            if (!::GetOverlappedResult(cur, &ov, &dummy, TRUE)) {
                DWORD gerr = ::GetLastError();
                ::CloseHandle(cur);
                return cfire::make_error(std::format("ipc accept failed: win32 error {}", gerr), detail::classify_win32_error(gerr));
            }
        } else {
            ::CloseHandle(cur);
            return cfire::make_error(std::format("ipc accept failed: win32 error {}", err), detail::classify_win32_error(err));
        }
    }

    // Pre-create the next listening instance before returning Conn so back-to-back clients never race.
    {
        std::lock_guard<std::mutex> lk(state_->mu_);
        if (state_->closed_.load()) {
            ::DisconnectNamedPipe(cur);
            ::CloseHandle(cur);
            return cfire::make_error("ipc listener closed");
        }
        state_->next_pipe_ = state_->create_pipe_instance(false);
    }

    return Conn(cur, true);
}

[[nodiscard]] inline auto endpoint(std::string_view name) -> std::string {
    std::string norm = detail::normalize_pipe_endpoint(name);
    if (norm.starts_with(R"(\\.\pipe\)")) {
        return norm;
    }
    std::string clean = detail::sanitize_win_name(name);
    std::string sid = detail::wide_to_utf8(detail::current_user_sid_wide());
    return std::format(R"(\\.\pipe\{}-{})", clean, sid);
}

[[nodiscard]] inline auto listen(std::string_view endpoint_path) -> std::expected<Listener, cfire::Error> {
    std::string norm = detail::normalize_pipe_endpoint(endpoint_path);
    if (!norm.starts_with(R"(\\.\pipe\)") || norm.size() <= 9) {
        return cfire::make_error(std::format("invalid Windows named pipe endpoint '{}'", endpoint_path));
    }
    auto st = std::make_unique<Listener::State>();
    st->endpoint_ = norm;
    st->wpath_ = detail::utf8_to_wide(norm);
    if (st->wpath_.empty() || !st->sd_guard_.init_owner_only()) {
        return cfire::make_error("failed to construct owner-only pipe security descriptor");
    }
    st->stop_event_ = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!st->stop_event_) {
        return cfire::make_error("failed to create ipc listener stop event");
    }
    st->next_pipe_ = st->create_pipe_instance(true);
    if (st->next_pipe_ == INVALID_HANDLE_VALUE) {
        DWORD err = ::GetLastError();
        return cfire::make_error(std::format("ipc listen failed for '{}': win32 error {}", endpoint_path, err), detail::classify_win32_error(err));
    }
    return Listener(std::move(st));
}

[[nodiscard]] inline auto connect(std::string_view endpoint_path, int timeout_ms = 5000)
    -> std::expected<Conn, cfire::Error>
{
    std::string norm = detail::normalize_pipe_endpoint(endpoint_path);
    if (!norm.starts_with(R"(\\.\pipe\)") || norm.size() <= 9) {
        return cfire::make_error(std::format("invalid Windows named pipe endpoint '{}'", endpoint_path));
    }
    std::wstring wpath = detail::utf8_to_wide(norm);
    if (wpath.empty()) {
        return cfire::make_error("invalid UTF-8 endpoint path");
    }

    using clock = std::chrono::steady_clock;
    const bool has_deadline = (timeout_ms > 0);
    const auto deadline = clock::now() + std::chrono::milliseconds(has_deadline ? timeout_ms : 0);

    for (;;) {
        HANDLE h = ::CreateFileW(
            wpath.c_str(),
            GENERIC_READ | GENERIC_WRITE,
            0,
            nullptr,
            OPEN_EXISTING,
            FILE_FLAG_OVERLAPPED | SECURITY_SQOS_PRESENT | SECURITY_IDENTIFICATION,
            nullptr
        );
        if (h != INVALID_HANDLE_VALUE) {
            return Conn(h, false);
        }
        DWORD err = ::GetLastError();
        if (err != ERROR_PIPE_BUSY) {
            return cfire::make_error(std::format("ipc connect failed for '{}': win32 error {}", endpoint_path, err), detail::classify_win32_error(err));
        }
        DWORD wait_slice = NMPWAIT_WAIT_FOREVER;
        if (has_deadline) {
            auto rem = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - clock::now()).count();
            if (rem <= 0) {
                return cfire::make_error(std::format("ipc connect timed out for '{}'", endpoint_path), 408);
            }
            wait_slice = static_cast<DWORD>(std::clamp<std::int64_t>(rem, 1, 50));
        }
        if (!::WaitNamedPipeW(wpath.c_str(), wait_slice)) {
            DWORD werr = ::GetLastError();
            if (werr == ERROR_FILE_NOT_FOUND) {
                return cfire::make_error(std::format("ipc connect failed for '{}': win32 error {}", endpoint_path, werr), detail::kErrNotFound);
            }
            if (has_deadline && clock::now() >= deadline) {
                return cfire::make_error(std::format("ipc connect timed out for '{}'", endpoint_path), 408);
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
}

#else // POSIX AF_UNIX (macOS & Linux)

struct Listener::State {
    std::mutex fd_mu_{};
    int fd_ = -1;
    int wake_read_ = -1;
    int wake_write_ = -1;
    int in_accept_ = 0;
    bool closed_ = false;
    std::string endpoint_{};
    bool unlinked_ = false;

    ~State() {
        if (wake_read_ >= 0) ::close(wake_read_);
        if (wake_write_ >= 0) ::close(wake_write_);
        if (fd_ >= 0) ::close(fd_);
    }
};

inline auto Listener::is_open() const noexcept -> bool {
    if (!state_) return false;
    std::lock_guard<std::mutex> lk(state_->fd_mu_);
    return !state_->closed_ && state_->fd_ >= 0;
}

inline auto Listener::endpoint() const -> std::string {
    return state_ ? state_->endpoint_ : std::string{};
}

inline void Listener::close() noexcept {
    if (!state_) return;
    int fd_to_close = -1;
    std::string ep_to_unlink;
    {
        std::lock_guard<std::mutex> lk(state_->fd_mu_);
        state_->closed_ = true;
        if (state_->wake_write_ >= 0) {
            char b = 1;
            (void)::write(state_->wake_write_, &b, 1);
        }
        if (state_->in_accept_ == 0 && state_->fd_ >= 0) {
            fd_to_close = std::exchange(state_->fd_, -1);
        }
        if (!state_->unlinked_ && !state_->endpoint_.empty()) {
            state_->unlinked_ = true;
            ep_to_unlink = state_->endpoint_;
        }
    }
    if (fd_to_close >= 0) {
        ::shutdown(fd_to_close, SHUT_RDWR);
        ::close(fd_to_close);
    }
    if (!ep_to_unlink.empty()) {
        struct stat st{};
        if (::lstat(ep_to_unlink.c_str(), &st) == 0 && S_ISSOCK(st.st_mode)) {
            (void)::unlink(ep_to_unlink.c_str());
        }
    }
}

inline auto Listener::accept() -> std::expected<Conn, cfire::Error> {
    if (!state_) return cfire::make_error("ipc listener is closed");
    int fd = -1;
    int wake_rd = -1;
    {
        std::lock_guard<std::mutex> lk(state_->fd_mu_);
        if (state_->closed_ || state_->fd_ < 0) {
            return cfire::make_error("ipc listener is closed");
        }
        fd = state_->fd_;
        wake_rd = state_->wake_read_;
        ++state_->in_accept_;
    }
    auto accept_guard = cfire::scope_exit([&]() {
        int fd_to_close = -1;
        {
            std::lock_guard<std::mutex> lk(state_->fd_mu_);
            --state_->in_accept_;
            if (state_->closed_ && state_->in_accept_ == 0 && state_->fd_ >= 0) {
                fd_to_close = std::exchange(state_->fd_, -1);
            }
        }
        if (fd_to_close >= 0) {
            ::shutdown(fd_to_close, SHUT_RDWR);
            ::close(fd_to_close);
        }
    });

    for (;;) {
        {
            std::lock_guard<std::mutex> lk(state_->fd_mu_);
            if (state_->closed_) return cfire::make_error("ipc listener is closed");
        }
        pollfd pfds[2]{
            {.fd = fd, .events = POLLIN, .revents = 0},
            {.fd = wake_rd, .events = POLLIN, .revents = 0},
        };
        int nfds = (wake_rd >= 0) ? 2 : 1;
        int rc = ::poll(pfds, static_cast<nfds_t>(nfds), -1);
        if (rc < 0) {
            if (errno == EINTR) continue;
            int err_no = errno;
            return cfire::make_error(std::format("ipc accept poll failed: {}", std::strerror(err_no)), detail::classify_errno(err_no));
        }
        {
            std::lock_guard<std::mutex> lk(state_->fd_mu_);
            if (state_->closed_) return cfire::make_error("ipc listener is closed");
        }
        if (nfds == 2 && (pfds[1].revents & (POLLIN | POLLHUP)) != 0) {
            return cfire::make_error("ipc listener is closed");
        }
        if ((pfds[0].revents & POLLNVAL) != 0) {
            return cfire::make_error("ipc listener is closed");
        }
        if ((pfds[0].revents & (POLLIN | POLLERR | POLLHUP)) != 0) {
            sockaddr_un peer{};
            socklen_t len = sizeof(peer);
            int cfd = ::accept(fd, reinterpret_cast<sockaddr*>(&peer), &len);
            if (cfd >= 0) {
                detail::set_cloexec_and_nosigpipe(cfd);
                return Conn(cfd, state_->endpoint_);
            }
            if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK) continue;
            {
                std::lock_guard<std::mutex> lk(state_->fd_mu_);
                if (state_->closed_) return cfire::make_error("ipc listener is closed");
            }
            int err_no = errno;
            return cfire::make_error(std::format("ipc accept failed: {}", std::strerror(err_no)), detail::classify_errno(err_no));
        }
    }
}

[[nodiscard]] inline auto endpoint(std::string_view name) -> std::string {
    if (name.find('/') != std::string_view::npos) {
        return std::string(name);
    }
    const std::string clean = detail::sanitize_name(name);
    const std::string file = clean + ".sock";
    constexpr std::size_t kMaxSunPath = sizeof(sockaddr_un::sun_path) - 1;

    std::vector<std::string> dirs;
    if (const char* xdg = std::getenv("XDG_RUNTIME_DIR"); xdg && *xdg) {
        dirs.emplace_back(xdg);
    }
    if (const char* tmpdir = std::getenv("TMPDIR"); tmpdir && *tmpdir) {
        dirs.emplace_back(tmpdir);
    }
    if (auto ucache = cfire::detail::user_cache_dir("ipc"); !ucache.empty()) {
        dirs.emplace_back(ucache.string());
    }

    for (std::string dir : dirs) {
        while (dir.size() > 1 && dir.back() == '/') dir.pop_back();
        if (dir.empty()) continue;
        std::string cand = dir + "/" + file;
        if (cand.size() < kMaxSunPath && detail::ensure_private_dir(dir)) {
            return cand;
        }
    }

    return file;
}

[[nodiscard]] inline auto listen(std::string_view endpoint_path) -> std::expected<Listener, cfire::Error> {
    std::string ep(endpoint_path);
    sockaddr_un addr{};
    socklen_t addr_len = 0;
    if (!detail::build_sockaddr(ep, addr, addr_len)) {
        return cfire::make_error(std::format("ipc endpoint path '{}' exceeds AF_UNIX limit", endpoint_path));
    }

    struct stat st{};
    if (::lstat(ep.c_str(), &st) == 0) {
        if (!S_ISSOCK(st.st_mode)) {
            return cfire::make_error(std::format("ipc endpoint '{}' exists and is not a socket", endpoint_path), detail::kErrExists);
        }
        if (st.st_uid != ::geteuid()) {
            return cfire::make_error(std::format("ipc endpoint '{}' is owned by another user", endpoint_path), detail::kErrPermission);
        }
        // Probe with non-blocking connect to distinguish a live listener from a stale socket file.
        int probe = ::socket(AF_UNIX, SOCK_STREAM, 0);
        if (probe < 0) {
            int err_no = errno;
            return cfire::make_error(std::format("ipc listen probe failed: {}", std::strerror(err_no)), detail::classify_errno(err_no));
        }
        detail::set_cloexec_and_nosigpipe(probe);
        detail::set_nonblocking(probe, true);
        int prc = ::connect(probe, reinterpret_cast<const sockaddr*>(&addr), addr_len);
        int p_err = (prc == 0) ? 0 : errno;
        ::close(probe);
        if (prc == 0 || p_err == EINPROGRESS || p_err == EAGAIN || p_err == EWOULDBLOCK || p_err == EISCONN) {
            return cfire::make_error(std::format("ipc endpoint '{}' is already in use", endpoint_path), detail::kErrExists);
        }
        if (p_err == ECONNREFUSED || p_err == ENOENT) {
            if (::unlink(ep.c_str()) != 0 && errno != ENOENT) {
                int err_no = errno;
                return cfire::make_error(std::format("failed to remove stale ipc socket '{}': {}", endpoint_path, std::strerror(err_no)), detail::classify_errno(err_no));
            }
        } else {
            return cfire::make_error(std::format("ipc endpoint '{}' is unavailable: {}", endpoint_path, std::strerror(p_err)), detail::classify_errno(p_err));
        }
    } else if (errno != ENOENT) {
        int err_no = errno;
        return cfire::make_error(std::format("ipc lstat failed for '{}': {}", endpoint_path, std::strerror(err_no)), detail::classify_errno(err_no));
    }

    int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        int err_no = errno;
        return cfire::make_error(std::format("ipc socket failed: {}", std::strerror(err_no)), detail::classify_errno(err_no));
    }
    detail::set_cloexec_and_nosigpipe(fd);
    (void)::fchmod(fd, 0600);

    if (::bind(fd, reinterpret_cast<const sockaddr*>(&addr), addr_len) != 0) {
        int err_no = errno;
        ::close(fd);
        return cfire::make_error(std::format("ipc bind failed for '{}': {}", endpoint_path, std::strerror(err_no)), detail::classify_errno(err_no));
    }
    if (::chmod(ep.c_str(), 0600) != 0) {
        int err_no = errno;
        ::close(fd);
        (void)::unlink(ep.c_str());
        return cfire::make_error(std::format("ipc chmod failed for '{}': {}", endpoint_path, std::strerror(err_no)), detail::classify_errno(err_no));
    }
    if (::listen(fd, 128) != 0) {
        int err_no = errno;
        ::close(fd);
        (void)::unlink(ep.c_str());
        return cfire::make_error(std::format("ipc listen failed for '{}': {}", endpoint_path, std::strerror(err_no)), detail::classify_errno(err_no));
    }

    detail::set_nonblocking(fd, true);
    int wpipe[2]{-1, -1};
    if (::pipe(wpipe) == 0) {
        detail::set_cloexec_and_nosigpipe(wpipe[0]);
        detail::set_cloexec_and_nosigpipe(wpipe[1]);
        detail::set_nonblocking(wpipe[1], true);
    }

    auto st_ptr = std::make_unique<Listener::State>();
    st_ptr->fd_ = fd;
    st_ptr->wake_read_ = wpipe[0];
    st_ptr->wake_write_ = wpipe[1];
    st_ptr->endpoint_ = std::move(ep);
    return Listener(std::move(st_ptr));
}

[[nodiscard]] inline auto connect(std::string_view endpoint_path, int timeout_ms = 5000)
    -> std::expected<Conn, cfire::Error>
{
    std::string ep(endpoint_path);
    sockaddr_un addr{};
    socklen_t addr_len = 0;
    if (!detail::build_sockaddr(ep, addr, addr_len)) {
        return cfire::make_error(std::format("ipc endpoint path '{}' exceeds AF_UNIX limit", endpoint_path));
    }
    if (timeout_ms < 0) {
        return cfire::make_error(std::format("ipc connect timed out for '{}'", endpoint_path), 408);
    }

    using clock = std::chrono::steady_clock;
    const bool has_deadline = (timeout_ms > 0);
    const auto deadline = clock::now() + std::chrono::milliseconds(has_deadline ? timeout_ms : 0);
    auto rem_ms = [&]() -> std::int64_t {
        if (!has_deadline) return -1;
        return std::chrono::duration_cast<std::chrono::milliseconds>(deadline - clock::now()).count();
    };

    for (;;) {
        int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
        if (fd < 0) {
            int err_no = errno;
            return cfire::make_error(std::format("ipc socket failed: {}", std::strerror(err_no)), detail::classify_errno(err_no));
        }
        detail::set_cloexec_and_nosigpipe(fd);
        detail::set_nonblocking(fd, true);

        if (::connect(fd, reinterpret_cast<const sockaddr*>(&addr), addr_len) == 0) {
            detail::set_nonblocking(fd, false);
            return Conn(fd, std::move(ep));
        }
        int err_no = errno;
        if (err_no == EINPROGRESS) {
            for (;;) {
                int wait_ms = -1;
                if (has_deadline) {
                    std::int64_t r = rem_ms();
                    if (r <= 0) {
                        ::close(fd);
                        return cfire::make_error(std::format("ipc connect timed out for '{}'", endpoint_path), 408);
                    }
                    wait_ms = static_cast<int>(std::clamp<std::int64_t>(r, 1, 2000000000LL));
                }
                pollfd pfd{.fd = fd, .events = POLLOUT, .revents = 0};
                int prc = ::poll(&pfd, 1, wait_ms);
                if (prc < 0 && errno == EINTR) continue;
                if (prc == 0) {
                    ::close(fd);
                    return cfire::make_error(std::format("ipc connect timed out for '{}'", endpoint_path), 408);
                }
                if (prc > 0) {
                    int so_err = 0;
                    socklen_t slen = sizeof(so_err);
                    if (::getsockopt(fd, SOL_SOCKET, SO_ERROR, &so_err, &slen) == 0 && so_err == 0) {
                        detail::set_nonblocking(fd, false);
                        return Conn(fd, std::move(ep));
                    }
                    err_no = so_err != 0 ? so_err : errno;
                }
                break;
            }
        }
        ::close(fd);
        // Transient listen-backlog full on AF_UNIX returns EAGAIN on some kernels; retry within deadline.
        if (err_no == EAGAIN || err_no == EWOULDBLOCK) {
            if (has_deadline && rem_ms() <= 0) {
                return cfire::make_error(std::format("ipc connect timed out for '{}'", endpoint_path), 408);
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            continue;
        }
        return cfire::make_error(std::format("ipc connect failed for '{}': {}", endpoint_path, std::strerror(err_no)), detail::classify_errno(err_no));
    }
}

#endif

inline Listener::Listener(std::unique_ptr<State> state) noexcept : state_(std::move(state)) {}
inline Listener::~Listener() { close(); }
inline Listener::Listener(Listener&&) noexcept = default;
inline auto Listener::operator=(Listener&& other) noexcept -> Listener& {
    if (this != &other) {
        close();
        state_ = std::move(other.state_);
    }
    return *this;
}

} // namespace cfire::ipc

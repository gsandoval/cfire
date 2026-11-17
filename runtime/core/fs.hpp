#pragma once
#include "../cfire_prelude.hpp"

#include <filesystem>
#include <fstream>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <span>
#include <string>
#include <string_view>
#include <vector>
#include <expected>
#if defined(__APPLE__)
#include <TargetConditionals.h>
extern "C" int _NSGetExecutablePath(char* buf, std::uint32_t* bufsize);
#endif

namespace cfire::fs {

inline constexpr std::int32_t ERR_EXISTS = 17;
inline constexpr std::int32_t ERR_NOT_FOUND = 2;
inline constexpr std::int32_t ERR_PERMISSION = 13;

struct FileInfo {
    std::uint64_t size = 0;
    std::int64_t mtime_ms = 0;
    std::uint32_t mode = 0;
    bool is_dir = false;
    bool is_symlink = false;

    friend constexpr auto operator==(const FileInfo&, const FileInfo&) noexcept -> bool = default;
};

namespace detail {
[[nodiscard]] inline auto classify_errno(int e) noexcept -> int {
    if (e == EEXIST) return ERR_EXISTS;
    if (e == ENOENT || e == ENOTDIR) return ERR_NOT_FOUND;
    if (e == EACCES || e == EPERM) return ERR_PERMISSION;
    return e;
}

[[nodiscard]] inline auto classify_error_code(const std::error_code& ec) noexcept -> int {
    if (ec == std::errc::file_exists) return ERR_EXISTS;
    if (ec == std::errc::no_such_file_or_directory || ec == std::errc::not_a_directory) return ERR_NOT_FOUND;
    if (ec == std::errc::permission_denied || ec == std::errc::operation_not_permitted) return ERR_PERMISSION;
    return ec.value();
}

#if defined(_WIN32)
[[nodiscard]] inline auto classify_win32_error(DWORD err) noexcept -> int {
    if (err == ERROR_FILE_EXISTS || err == ERROR_ALREADY_EXISTS) return ERR_EXISTS;
    if (err == ERROR_FILE_NOT_FOUND || err == ERROR_PATH_NOT_FOUND || err == ERROR_BAD_NETPATH) return ERR_NOT_FOUND;
    if (err == ERROR_ACCESS_DENIED || err == ERROR_PRIVILEGE_NOT_HELD || err == ERROR_SHARING_VIOLATION) return ERR_PERMISSION;
    return static_cast<int>(err);
}
#endif

template <typename St>
[[nodiscard]] inline auto stat_mtime_ms(const St& st) noexcept -> std::int64_t {
    if constexpr (requires { st.st_mtimespec.tv_sec; st.st_mtimespec.tv_nsec; }) {
        return static_cast<std::int64_t>(st.st_mtimespec.tv_sec) * 1000LL +
               static_cast<std::int64_t>(st.st_mtimespec.tv_nsec) / 1'000'000LL;
    } else if constexpr (requires { st.st_mtim.tv_sec; st.st_mtim.tv_nsec; }) {
        return static_cast<std::int64_t>(st.st_mtim.tv_sec) * 1000LL +
               static_cast<std::int64_t>(st.st_mtim.tv_nsec) / 1'000'000LL;
    } else {
        return static_cast<std::int64_t>(st.st_mtime) * 1000LL;
    }
}

[[nodiscard]] inline auto resolve_path(const std::filesystem::path& p, bool for_write = false) noexcept -> std::filesystem::path {
#if CFIRE_TARGET_IOS
    if (p.is_relative() && !p.empty() && p.string() != ":memory:") {
        std::error_code ec;
        if (!for_write && std::filesystem::exists(p, ec)) {
            return p;
        }
        if (const char* home = std::getenv("HOME")) {
            auto docs = std::filesystem::path(home) / "Documents";
            std::filesystem::create_directories(docs, ec);
            auto candidate = docs / p;
            if (for_write) {
                if (candidate.has_parent_path()) {
                    std::filesystem::create_directories(candidate.parent_path(), ec);
                }
                return candidate;
            }
            if (std::filesystem::exists(candidate, ec)) {
                return candidate;
            }
        }
        if (!for_write) {
            // Check iOS .app bundle directory (where bundled resources/assets reside)
            char exec_buf[2048];
            std::uint32_t exec_sz = sizeof(exec_buf);
            if (::_NSGetExecutablePath(exec_buf, &exec_sz) == 0) {
                auto bundle_dir = std::filesystem::path(exec_buf).parent_path();
                if (std::filesystem::exists(bundle_dir / p, ec)) {
                    return bundle_dir / p;
                }
            }
            // Check iOS sandbox TMPDIR
            if (const char* tmp_env = std::getenv("TMPDIR")) {
                auto tmp_cand = std::filesystem::path(tmp_env) / p;
                if (std::filesystem::exists(tmp_cand, ec)) {
                    return tmp_cand;
                }
            }
        }
    }
#else
    (void)for_write;
#endif
    return p;
}
} // namespace detail

[[nodiscard]] inline auto exists(const std::filesystem::path& p) noexcept -> bool {
    std::error_code ec;
    auto resolved = detail::resolve_path(p, false);
    if (std::filesystem::exists(resolved, ec)) return true;
#if defined(_WIN32)
    // On Windows, a reparse point that the OS cannot follow (such as an IO_REPARSE_TAG_AF_UNIX
    // listening socket file) causes std::filesystem::exists() to fail with ERROR_CANT_ACCESS_FILE
    // (1920). Fall back to symlink_status() so fs.exists() sees the file itself like POSIX lstat.
    if (ec) {
        std::error_code ec2;
        return std::filesystem::exists(std::filesystem::symlink_status(resolved, ec2));
    }
#endif
    return false;
}

[[nodiscard]] inline auto is_dir(const std::filesystem::path& p) noexcept -> bool {
    std::error_code ec;
    return std::filesystem::is_directory(detail::resolve_path(p, false), ec);
}

[[nodiscard]] inline auto is_file(const std::filesystem::path& p) noexcept -> bool {
    std::error_code ec;
    return std::filesystem::is_regular_file(detail::resolve_path(p, false), ec);
}

[[nodiscard]] inline auto mkdir_all(const std::filesystem::path& p) -> std::expected<void, cfire::Error> {
    std::error_code ec;
    std::filesystem::create_directories(detail::resolve_path(p, true), ec);
    if (ec) {
        return cfire::make_error(std::format("mkdir_all failed for {}: {}", p.string(), ec.message()), detail::classify_error_code(ec));
    }
    return {};
}

[[nodiscard]] inline auto remove(const std::filesystem::path& p) -> std::expected<void, cfire::Error> {
    std::error_code ec;
    std::filesystem::remove_all(detail::resolve_path(p, true), ec);
    if (ec) {
        return cfire::make_error(std::format("remove failed for {}: {}", p.string(), ec.message()), detail::classify_error_code(ec));
    }
    return {};
}

[[nodiscard]] inline auto rename(const std::filesystem::path& from, const std::filesystem::path& to) -> std::expected<void, cfire::Error> {
    std::error_code ec;
    std::filesystem::rename(detail::resolve_path(from, false), detail::resolve_path(to, true), ec);
    if (ec) {
        return cfire::make_error(std::format("rename failed from {} to {}: {}", from.string(), to.string(), ec.message()), detail::classify_error_code(ec));
    }
    return {};
}

[[nodiscard]] inline auto copy_file(const std::filesystem::path& from, const std::filesystem::path& to) -> std::expected<void, cfire::Error> {
    std::error_code ec;
    std::filesystem::copy_file(detail::resolve_path(from, false), detail::resolve_path(to, true), std::filesystem::copy_options::overwrite_existing, ec);
    if (ec) {
        return cfire::make_error(std::format("copy_file failed from {} to {}: {}", from.string(), to.string(), ec.message()), detail::classify_error_code(ec));
    }
    return {};
}

[[nodiscard]] inline auto file_size(const std::filesystem::path& p) -> std::expected<std::uint64_t, cfire::Error> {
    std::error_code ec;
    auto sz = std::filesystem::file_size(detail::resolve_path(p, false), ec);
    if (ec) {
        return cfire::make_error(std::format("file_size failed for {}: {}", p.string(), ec.message()), detail::classify_error_code(ec));
    }
    return static_cast<std::uint64_t>(sz);
}

[[nodiscard]] inline auto list_dir(const std::filesystem::path& p) -> std::expected<std::vector<std::string>, cfire::Error> {
    std::error_code ec;
    std::vector<std::string> names;
    for (const auto& entry : std::filesystem::directory_iterator(detail::resolve_path(p, false), ec)) {
        names.push_back(entry.path().filename().string());
    }
    if (ec) {
        return cfire::make_error(std::format("list_dir failed for {}: {}", p.string(), ec.message()), detail::classify_error_code(ec));
    }
    std::sort(names.begin(), names.end());
    return names;
}

[[nodiscard]] inline auto read_bytes(const std::filesystem::path& p)
    -> std::expected<std::vector<std::uint8_t>, cfire::Error>
{
    auto rp = detail::resolve_path(p, false);
    errno = 0;
    std::ifstream f(rp, std::ios::binary | std::ios::ate);
    if (!f.is_open()) {
        int err_no = errno != 0 ? errno : (cfire::fs::exists(rp) ? EACCES : ENOENT);
        return cfire::make_error(std::format("failed to open file: {}", p.string()), detail::classify_errno(err_no));
    }
    auto sz = static_cast<std::size_t>(f.tellg());
    f.seekg(0, std::ios::beg);
    std::vector<std::uint8_t> buf(sz);
    if (sz > 0 && !f.read(reinterpret_cast<char*>(buf.data()), static_cast<std::streamsize>(sz))) {
        int err_no = errno;
        return cfire::make_error(std::format("failed to read file: {}", p.string()), detail::classify_errno(err_no));
    }
    return buf;
}

[[nodiscard]] inline auto read_text(const std::filesystem::path& p)
    -> std::expected<std::string, cfire::Error>
{
    auto rp = detail::resolve_path(p, false);
    errno = 0;
    std::ifstream f(rp, std::ios::in | std::ios::binary);
    if (!f.is_open()) {
        int err_no = errno != 0 ? errno : (cfire::fs::exists(rp) ? EACCES : ENOENT);
        return cfire::make_error(std::format("failed to open file: {}", p.string()), detail::classify_errno(err_no));
    }
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

[[nodiscard]] inline auto write_bytes(
    const std::filesystem::path& p,
    std::span<const std::uint8_t> data
) -> std::expected<void, cfire::Error> {
    auto rp = detail::resolve_path(p, true);
    errno = 0;
    std::ofstream f(rp, std::ios::binary | std::ios::trunc);
    if (!f.is_open()) {
        int err_no = errno != 0 ? errno : EACCES;
        return cfire::make_error(std::format("failed to open file for write: {}", p.string()), detail::classify_errno(err_no));
    }
    if (!data.empty()) {
        f.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
    }
    if (!f.good()) {
        int err_no = errno;
        return cfire::make_error(std::format("failed to write bytes to: {}", p.string()), detail::classify_errno(err_no));
    }
    return {};
}


[[nodiscard]] inline auto write_text(
    const std::filesystem::path& p,
    std::string_view text
) -> std::expected<void, cfire::Error> {
    return write_bytes(p, to_bytes(text));
}

[[nodiscard]] inline auto append_bytes(
    const std::filesystem::path& p,
    std::span<const std::uint8_t> data
) -> std::expected<void, cfire::Error> {
    auto rp = detail::resolve_path(p, true);
    errno = 0;
    std::ofstream f(rp, std::ios::binary | std::ios::app);
    if (!f.is_open()) {
        int err_no = errno != 0 ? errno : EACCES;
        return cfire::make_error(std::format("failed to open file for append: {}", p.string()), detail::classify_errno(err_no));
    }
    if (!data.empty()) {
        f.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
    }
    if (!f.good()) {
        int err_no = errno;
        return cfire::make_error(std::format("failed to append bytes to: {}", p.string()), detail::classify_errno(err_no));
    }
    return {};
}


[[nodiscard]] inline auto append_text(
    const std::filesystem::path& p,
    std::string_view text
) -> std::expected<void, cfire::Error> {
    return append_bytes(p, to_bytes(text));
}

enum class OpenMode : int {
    Read = 0,
    Write = 1,
    Append = 2,
    ReadWrite = 3,
};

enum class SeekWhence : int {
    Start = 0,
    Current = 1,
    End = 2,
};

class File {
public:
    File() noexcept = default;
    explicit File(int fd, bool owned = true) noexcept : fd_(fd), owned_(owned) {}
    ~File() { close(); }

    File(const File&) = delete;
    auto operator=(const File&) -> File& = delete;

    File(File&& other) noexcept
        : fd_(std::exchange(other.fd_, -1)),
          owned_(std::exchange(other.owned_, false)) {}

    auto operator=(File&& other) noexcept -> File& {
        if (this != &other) {
            close();
            fd_ = std::exchange(other.fd_, -1);
            owned_ = std::exchange(other.owned_, false);
        }
        return *this;
    }

    [[nodiscard]] auto is_open() const noexcept -> bool {
        return fd_ >= 0;
    }

    [[nodiscard]] auto fd() const noexcept -> std::int32_t {
        return static_cast<std::int32_t>(fd_);
    }

    [[nodiscard]] auto release_fd() noexcept -> int {
        owned_ = false;
        return std::exchange(fd_, -1);
    }

    [[nodiscard]] auto is_tty() const noexcept -> bool {
        return cfire::detail::fd_is_tty(fd_);
    }

    [[nodiscard]] auto poll(std::int64_t timeout_ms = 0) const noexcept -> bool {
        return cfire::detail::fd_poll(fd_, timeout_ms);
    }

    void close() noexcept {
        if (fd_ >= 0) {
            int f = std::exchange(fd_, -1);
            bool own = std::exchange(owned_, false);
            if (own) {
                (void)cfire::detail::fd_close(f);
            }
        }
    }

    [[nodiscard]] auto read_into(std::span<std::uint8_t> buf) -> std::expected<std::size_t, cfire::Error> {
        if (fd_ < 0) return cfire::make_error("file is not open");
        return cfire::detail::fd_read(fd_, buf);
    }


    [[nodiscard]] auto read_exact(std::size_t n) -> std::expected<std::vector<std::uint8_t>, cfire::Error> {
        if (fd_ < 0) return cfire::make_error("file is not open");
        return cfire::detail::fd_read_exact(fd_, n);
    }

    [[nodiscard]] auto read_bytes(std::size_t max_bytes = 64ULL * 1024ULL * 1024ULL)
        -> std::expected<std::vector<std::uint8_t>, cfire::Error>
    {
        if (fd_ < 0) return cfire::make_error("file is not open");
        if (max_bytes == 0) return std::vector<std::uint8_t>{};
        if (max_bytes < (64ULL * 1024ULL * 1024ULL) && !is_seekable()) {
            std::vector<std::uint8_t> buf(max_bytes);
            auto n = cfire::detail::fd_read(fd_, std::span<std::uint8_t>(buf));
            if (!n) return std::unexpected(n.error());
            buf.resize(*n);
            return buf;
        }
        return cfire::detail::fd_read_all_bounded(fd_, max_bytes);
    }

    [[nodiscard]] auto read_text(std::size_t max_bytes = 64ULL * 1024ULL * 1024ULL)
        -> std::expected<std::string, cfire::Error>
    {
        auto b = read_bytes(max_bytes);
        if (!b) return std::unexpected(b.error());
        return std::string(reinterpret_cast<const char*>(b->data()), b->size());
    }

    [[nodiscard]] auto read_bytes_until(std::uint8_t delim, std::size_t max_bytes = 65536)
        -> std::expected<std::vector<std::uint8_t>, cfire::Error>
    {
        if (fd_ < 0) return cfire::make_error("file is not open");
        return cfire::detail::fd_read_until(fd_, delim, max_bytes);
    }


    [[nodiscard]] auto read_text_until(std::string_view delim, std::size_t max_bytes = 65536)
        -> std::expected<std::string, cfire::Error>
    {
        if (fd_ < 0) return cfire::make_error("file is not open");
        if (delim.empty()) return cfire::make_error("read_text_until requires non-empty delimiter");
        std::string out;
        std::uint8_t ch{0};
        while (out.size() < max_bytes) {
            auto n = cfire::detail::fd_read(fd_, std::span<std::uint8_t>(&ch, 1));
            if (!n) return std::unexpected(n.error());
            if (*n == 0) break;
            out.push_back(static_cast<char>(ch));
            if (out.ends_with(delim)) break;
        }
        return out;
    }

    [[nodiscard]] auto read_line(std::size_t max_bytes = 65536) -> std::optional<std::string> {
        if (fd_ < 0) return std::nullopt;
        return cfire::detail::fd_read_line(fd_, max_bytes);
    }

    [[nodiscard]] auto read_lines() -> std::expected<std::vector<std::string>, cfire::Error> {
        if (fd_ < 0) return cfire::make_error("file is not open");
        std::vector<std::string> lines;
        while (auto line = cfire::detail::fd_read_line(fd_)) {
            lines.push_back(std::move(*line));
        }
        return lines;
    }

    [[nodiscard]] auto write_bytes(std::span<const std::uint8_t> data) -> std::expected<std::size_t, cfire::Error> {
        if (fd_ < 0) return cfire::make_error("file is not open");
        return cfire::detail::fd_write_all(fd_, data);
    }


    [[nodiscard]] auto write_text(std::string_view s) -> std::expected<std::size_t, cfire::Error> {
        if (fd_ < 0) return cfire::make_error("file is not open");
        return cfire::detail::fd_write_all(fd_, cfire::as_u8(std::span(s.data(), s.size())));
    }

    [[nodiscard]] auto seek(std::int64_t offset, SeekWhence whence = SeekWhence::Start) -> std::expected<std::int64_t, cfire::Error> {
        if (fd_ < 0) return cfire::make_error("file is not open");
        int origin = SEEK_SET;
        if (whence == SeekWhence::Current) origin = SEEK_CUR;
        else if (whence == SeekWhence::End) origin = SEEK_END;
#if defined(_WIN32)
        auto pos = ::_lseeki64(fd_, offset, origin);
#else
        auto pos = ::lseek(fd_, static_cast<off_t>(offset), origin);
#endif
        if (pos < 0) {
            return cfire::make_error("file seek error");
        }
        return static_cast<std::int64_t>(pos);
    }

    [[nodiscard]] auto tell() -> std::expected<std::int64_t, cfire::Error> {
        return seek(0, SeekWhence::Current);
    }

    [[nodiscard]] auto flush() -> std::expected<void, cfire::Error> {
        if (fd_ < 0) return cfire::make_error("file is not open");
        if (fd_ == 1) {
            if (std::fflush(stdout) != 0) {
                int err_no = errno;
                return cfire::make_error("stdout flush failed", detail::classify_errno(err_no));
            }
        } else if (fd_ == 2) {
            if (std::fflush(stderr) != 0) {
                int err_no = errno;
                return cfire::make_error("stderr flush failed", detail::classify_errno(err_no));
            }
        }
        return {};
    }

    [[nodiscard]] auto sync() -> std::expected<void, cfire::Error> {
        if (fd_ < 0) return cfire::make_error("file is not open");
#if defined(_WIN32)
        std::intptr_t h = ::_get_osfhandle(fd_);
        if (h == -1 || h == -2) {
            return cfire::make_error("file sync failed: invalid OS handle");
        }
        if (!::FlushFileBuffers(reinterpret_cast<HANDLE>(h))) {
            DWORD err = ::GetLastError();
            return cfire::make_error(std::format("file sync failed: win32 error {}", err), detail::classify_win32_error(err));
        }
        return {};
#else
#if defined(__APPLE__) && defined(F_FULLFSYNC)
        if (::fcntl(fd_, F_FULLFSYNC) == 0) {
            return {};
        }
#endif
        while (::fsync(fd_) != 0) {
            if (errno == EINTR) continue;
            int err_no = errno;
            return cfire::make_error(std::format("file sync failed: {}", std::strerror(err_no)), detail::classify_errno(err_no));
        }
        return {};
#endif
    }

private:
    [[nodiscard]] auto is_seekable() const noexcept -> bool {
        if (fd_ < 0) return false;
#if defined(_WIN32)
        return ::_lseeki64(fd_, 0, SEEK_CUR) >= 0;
#else
        return ::lseek(fd_, 0, SEEK_CUR) >= 0;
#endif
    }

    int fd_ = -1;
    bool owned_ = true;
};

[[nodiscard]] inline auto open(const std::filesystem::path& p, OpenMode mode = OpenMode::Read)
    -> std::expected<File, cfire::Error>
{
    auto rp = detail::resolve_path(p, mode != OpenMode::Read);
#if defined(_WIN32)
    int flags = _O_BINARY | _O_NOINHERIT;
    switch (mode) {
        case OpenMode::Read: flags |= _O_RDONLY; break;
        case OpenMode::Write: flags |= _O_RDWR | _O_CREAT | _O_TRUNC; break;
        case OpenMode::Append: flags |= _O_RDWR | _O_CREAT | _O_APPEND; break;
        case OpenMode::ReadWrite: flags |= _O_RDWR | _O_CREAT; break;
    }
    int fd = ::_wopen(rp.wstring().c_str(), flags, 0666);
#else
    int flags = O_CLOEXEC;
    switch (mode) {
        case OpenMode::Read: flags |= O_RDONLY; break;
        case OpenMode::Write: flags |= O_RDWR | O_CREAT | O_TRUNC; break;
        case OpenMode::Append: flags |= O_RDWR | O_CREAT | O_APPEND; break;
        case OpenMode::ReadWrite: flags |= O_RDWR | O_CREAT; break;
    }
    int fd = ::open(rp.string().c_str(), flags, 0644);
#endif
    if (fd < 0) {
        int err_no = errno;
        return cfire::make_error(std::format("failed to open file {}: {}", p.string(), std::strerror(err_no)), detail::classify_errno(err_no));
    }
    return File(fd, true);
}

[[nodiscard]] inline auto create_new(const std::filesystem::path& p, std::uint32_t mode = 0600)
    -> std::expected<File, cfire::Error>
{
    auto rp = detail::resolve_path(p, true);
#if defined(_WIN32)
    DWORD attrs = ((mode & 0200U) != 0) ? FILE_ATTRIBUTE_NORMAL : FILE_ATTRIBUTE_READONLY;
    HANDLE h = ::CreateFileW(
        rp.wstring().c_str(),
        GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ,
        nullptr,
        CREATE_NEW,
        attrs,
        nullptr
    );
    if (h == INVALID_HANDLE_VALUE) {
        DWORD err = ::GetLastError();
        return cfire::make_error(std::format("create_new failed for {}: win32 error {}", p.string(), err), detail::classify_win32_error(err));
    }
    int fd = ::_open_osfhandle(reinterpret_cast<std::intptr_t>(h), _O_RDWR | _O_BINARY);
    if (fd < 0) {
        int err_no = errno;
        ::CloseHandle(h);
        return cfire::make_error(std::format("create_new failed to wrap handle for {}: {}", p.string(), std::strerror(err_no)), detail::classify_errno(err_no));
    }
    return File(fd, true);
#else
    int fd = ::open(rp.string().c_str(), O_RDWR | O_CREAT | O_EXCL | O_CLOEXEC, static_cast<mode_t>(mode & 07777U));
    if (fd < 0) {
        int err_no = errno;
        return cfire::make_error(std::format("create_new failed for {}: {}", p.string(), std::strerror(err_no)), detail::classify_errno(err_no));
    }
    (void)::fchmod(fd, static_cast<mode_t>(mode & 0777U));
    return File(fd, true);
#endif
}

[[nodiscard]] inline auto stat(const std::filesystem::path& p)
    -> std::expected<FileInfo, cfire::Error>
{
    auto rp = detail::resolve_path(p, false);
#if defined(_WIN32)
    WIN32_FILE_ATTRIBUTE_DATA fad{};
    if (!::GetFileAttributesExW(rp.wstring().c_str(), GetFileExInfoStandard, &fad)) {
        DWORD err = ::GetLastError();
        return cfire::make_error(std::format("stat failed for {}: win32 error {}", p.string(), err), detail::classify_win32_error(err));
    }
    std::error_code lsec;
    auto lstatus = std::filesystem::symlink_status(rp, lsec);
    const bool is_sym = !lsec && std::filesystem::is_symlink(lstatus);
    bool is_dir = (fad.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
    std::uint64_t sz = (static_cast<std::uint64_t>(fad.nFileSizeHigh) << 32) |
                        static_cast<std::uint64_t>(fad.nFileSizeLow);
    FILETIME ft = fad.ftLastWriteTime;
    DWORD attrs = fad.dwFileAttributes;
    if (is_sym) {
        HANDLE th = ::CreateFileW(
            rp.wstring().c_str(),
            0,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
            nullptr,
            OPEN_EXISTING,
            FILE_FLAG_BACKUP_SEMANTICS,
            nullptr
        );
        if (th != INVALID_HANDLE_VALUE) {
            BY_HANDLE_FILE_INFORMATION bhfi{};
            if (::GetFileInformationByHandle(th, &bhfi)) {
                is_dir = (bhfi.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
                sz = (static_cast<std::uint64_t>(bhfi.nFileSizeHigh) << 32) |
                     static_cast<std::uint64_t>(bhfi.nFileSizeLow);
                ft = bhfi.ftLastWriteTime;
                attrs = bhfi.dwFileAttributes;
            }
            ::CloseHandle(th);
        }
    }
    ULARGE_INTEGER uli{};
    uli.LowPart = ft.dwLowDateTime;
    uli.HighPart = ft.dwHighDateTime;
    constexpr std::uint64_t kEpochDiff100Ns = 116444736000000000ULL;
    std::int64_t mtime_ms = (uli.QuadPart >= kEpochDiff100Ns)
        ? static_cast<std::int64_t>((uli.QuadPart - kEpochDiff100Ns) / 10000ULL)
        : -static_cast<std::int64_t>((kEpochDiff100Ns - uli.QuadPart) / 10000ULL);
    const bool readonly = (attrs & FILE_ATTRIBUTE_READONLY) != 0;
    std::uint32_t mode = is_dir ? (readonly ? 0555U : 0755U) : (readonly ? 0444U : 0644U);
    FileInfo out;
    out.size = is_dir ? 0ULL : sz;
    out.mtime_ms = mtime_ms;
    out.mode = mode;
    out.is_dir = is_dir;
    out.is_symlink = is_sym;
    return out;
#else
    struct stat lst{};
    if (::lstat(rp.string().c_str(), &lst) != 0) {
        int err_no = errno;
        return cfire::make_error(std::format("stat failed for {}: {}", p.string(), std::strerror(err_no)), detail::classify_errno(err_no));
    }
    const bool is_sym = S_ISLNK(lst.st_mode);
    struct stat info_st = lst;
    if (is_sym) {
        struct stat tst{};
        if (::stat(rp.string().c_str(), &tst) == 0) {
            info_st = tst;
        }
    }
    FileInfo out;
    out.size = static_cast<std::uint64_t>(info_st.st_size < 0 ? 0 : info_st.st_size);
    out.mtime_ms = detail::stat_mtime_ms(info_st);
    out.mode = static_cast<std::uint32_t>(info_st.st_mode & 07777U);
    out.is_dir = S_ISDIR(info_st.st_mode);
    out.is_symlink = is_sym;
    return out;
#endif
}

[[nodiscard]] inline auto realpath(const std::filesystem::path& p)
    -> std::expected<std::string, cfire::Error>
{
    auto rp = detail::resolve_path(p, false);
#if defined(_WIN32)
    HANDLE h = ::CreateFileW(
        rp.wstring().c_str(),
        0,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr,
        OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS,
        nullptr
    );
    if (h == INVALID_HANDLE_VALUE) {
        DWORD err = ::GetLastError();
        return cfire::make_error(std::format("realpath failed for {}: win32 error {}", p.string(), err), detail::classify_win32_error(err));
    }
    DWORD needed = ::GetFinalPathNameByHandleW(h, nullptr, 0, FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
    std::wstring wres;
    if (needed > 0) {
        wres.resize(static_cast<std::size_t>(needed));
        DWORD written = ::GetFinalPathNameByHandleW(h, wres.data(), needed, FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
        ::CloseHandle(h);
        if (written > 0 && written < needed) {
            wres.resize(static_cast<std::size_t>(written));
        } else {
            wres.clear();
        }
    } else {
        ::CloseHandle(h);
    }
    if (!wres.empty()) {
        if (wres.starts_with(L"\\\\?\\UNC\\")) {
            wres = L"\\\\" + wres.substr(8);
        } else if (wres.starts_with(L"\\\\?\\")) {
            wres = wres.substr(4);
        }
        auto u8 = std::filesystem::path(wres).u8string();
        return std::string(reinterpret_cast<const char*>(u8.data()), u8.size());
    }
    wchar_t* full = ::_wfullpath(nullptr, rp.wstring().c_str(), 0);
    if (!full) {
        int err_no = errno;
        return cfire::make_error(std::format("realpath failed for {}: {}", p.string(), std::strerror(err_no)), detail::classify_errno(err_no));
    }
    std::wstring fallback_w(full);
    std::free(full);
    auto u8 = std::filesystem::path(fallback_w).u8string();
    return std::string(reinterpret_cast<const char*>(u8.data()), u8.size());
#else
    char* resolved = ::realpath(rp.string().c_str(), nullptr);
    if (!resolved) {
        int err_no = errno;
        return cfire::make_error(std::format("realpath failed for {}: {}", p.string(), std::strerror(err_no)), detail::classify_errno(err_no));
    }
    std::string out(resolved);
    std::free(resolved);
    return out;
#endif
}

[[nodiscard]] inline auto dirname(std::string_view p) -> std::string {
    std::filesystem::path fp{p};
    return fp.parent_path().string();
}

[[nodiscard]] inline auto basename(std::string_view p) -> std::string {
    std::filesystem::path fp{p};
    return fp.filename().string();
}

[[nodiscard]] inline auto extension(std::string_view p) -> std::string {
    std::filesystem::path fp{p};
    return fp.extension().string();
}

} // namespace cfire::fs


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

namespace detail {
[[nodiscard]] inline auto resolve_path(const std::filesystem::path& p, bool for_write = false) noexcept -> std::filesystem::path {
#if defined(PLATFORM_IOS) || (defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE)
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
    return std::filesystem::exists(detail::resolve_path(p, false), ec);
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
        return cfire::make_error(std::format("mkdir_all failed for {}: {}", p.string(), ec.message()));
    }
    return {};
}

[[nodiscard]] inline auto remove(const std::filesystem::path& p) -> std::expected<void, cfire::Error> {
    std::error_code ec;
    std::filesystem::remove_all(detail::resolve_path(p, true), ec);
    if (ec) {
        return cfire::make_error(std::format("remove failed for {}: {}", p.string(), ec.message()));
    }
    return {};
}

[[nodiscard]] inline auto rename(const std::filesystem::path& from, const std::filesystem::path& to) -> std::expected<void, cfire::Error> {
    std::error_code ec;
    std::filesystem::rename(detail::resolve_path(from, false), detail::resolve_path(to, true), ec);
    if (ec) {
        return cfire::make_error(std::format("rename failed from {} to {}: {}", from.string(), to.string(), ec.message()));
    }
    return {};
}

[[nodiscard]] inline auto copy_file(const std::filesystem::path& from, const std::filesystem::path& to) -> std::expected<void, cfire::Error> {
    std::error_code ec;
    std::filesystem::copy_file(detail::resolve_path(from, false), detail::resolve_path(to, true), std::filesystem::copy_options::overwrite_existing, ec);
    if (ec) {
        return cfire::make_error(std::format("copy_file failed from {} to {}: {}", from.string(), to.string(), ec.message()));
    }
    return {};
}

[[nodiscard]] inline auto file_size(const std::filesystem::path& p) -> std::expected<std::uint64_t, cfire::Error> {
    std::error_code ec;
    auto sz = std::filesystem::file_size(detail::resolve_path(p, false), ec);
    if (ec) {
        return cfire::make_error(std::format("file_size failed for {}: {}", p.string(), ec.message()));
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
        return cfire::make_error(std::format("list_dir failed for {}: {}", p.string(), ec.message()));
    }
    std::sort(names.begin(), names.end());
    return names;
}

[[nodiscard]] inline auto read_bytes(const std::filesystem::path& p)
    -> std::expected<std::vector<std::byte>, cfire::Error>
{
    auto rp = detail::resolve_path(p, false);
    std::ifstream f(rp, std::ios::binary | std::ios::ate);
    if (!f.is_open()) {
        return cfire::make_error(std::format("failed to open file: {}", p.string()));
    }
    auto sz = static_cast<std::size_t>(f.tellg());
    f.seekg(0, std::ios::beg);
    std::vector<std::byte> buf(sz);
    if (sz > 0 && !f.read(reinterpret_cast<char*>(buf.data()), static_cast<std::streamsize>(sz))) {
        return cfire::make_error(std::format("failed to read file: {}", p.string()));
    }
    return buf;
}

[[nodiscard]] inline auto read_text(const std::filesystem::path& p)
    -> std::expected<std::string, cfire::Error>
{
    auto rp = detail::resolve_path(p, false);
    std::ifstream f(rp, std::ios::in | std::ios::binary);
    if (!f.is_open()) {
        return cfire::make_error(std::format("failed to open file: {}", p.string()));
    }
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

[[nodiscard]] inline auto write_bytes(
    const std::filesystem::path& p,
    std::span<const std::byte> data
) -> std::expected<void, cfire::Error> {
    auto rp = detail::resolve_path(p, true);
    std::ofstream f(rp, std::ios::binary | std::ios::trunc);
    if (!f.is_open()) {
        return cfire::make_error(std::format("failed to open file for write: {}", p.string()));
    }
    if (!data.empty()) {
        f.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
    }
    if (!f.good()) {
        return cfire::make_error(std::format("failed to write bytes to: {}", p.string()));
    }
    return {};
}

[[nodiscard]] inline auto write_bytes(
    const std::filesystem::path& p,
    std::span<const std::uint8_t> data
) -> std::expected<void, cfire::Error> {
    return write_bytes(p, std::as_bytes(data));
}

[[nodiscard]] inline auto write_text(
    const std::filesystem::path& p,
    std::string_view text
) -> std::expected<void, cfire::Error> {
    return write_bytes(p, to_bytes(text));
}

[[nodiscard]] inline auto append_bytes(
    const std::filesystem::path& p,
    std::span<const std::byte> data
) -> std::expected<void, cfire::Error> {
    auto rp = detail::resolve_path(p, true);
    std::ofstream f(rp, std::ios::binary | std::ios::app);
    if (!f.is_open()) {
        return cfire::make_error(std::format("failed to open file for append: {}", p.string()));
    }
    if (!data.empty()) {
        f.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
    }
    if (!f.good()) {
        return cfire::make_error(std::format("failed to append bytes to: {}", p.string()));
    }
    return {};
}

[[nodiscard]] inline auto append_bytes(
    const std::filesystem::path& p,
    std::span<const std::uint8_t> data
) -> std::expected<void, cfire::Error> {
    return append_bytes(p, std::as_bytes(data));
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

    [[nodiscard]] auto read_into(std::span<std::byte> buf) -> std::expected<std::size_t, cfire::Error> {
        if (fd_ < 0) return cfire::make_error("file is not open");
        return cfire::detail::fd_read(fd_, buf);
    }

    [[nodiscard]] auto read_into(std::span<std::uint8_t> buf) -> std::expected<std::size_t, cfire::Error> {
        return read_into(std::as_writable_bytes(buf));
    }

    [[nodiscard]] auto read_exact(std::size_t n) -> std::expected<std::vector<std::byte>, cfire::Error> {
        if (fd_ < 0) return cfire::make_error("file is not open");
        return cfire::detail::fd_read_exact(fd_, n);
    }

    [[nodiscard]] auto read_bytes(std::size_t max_bytes = 64ULL * 1024ULL * 1024ULL)
        -> std::expected<std::vector<std::byte>, cfire::Error>
    {
        if (fd_ < 0) return cfire::make_error("file is not open");
        if (max_bytes == 0) return std::vector<std::byte>{};
        if (max_bytes < (64ULL * 1024ULL * 1024ULL) && !is_seekable()) {
            std::vector<std::byte> buf(max_bytes);
            auto n = cfire::detail::fd_read(fd_, std::span<std::byte>(buf));
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

    [[nodiscard]] auto read_bytes_until(std::byte delim, std::size_t max_bytes = 65536)
        -> std::expected<std::vector<std::byte>, cfire::Error>
    {
        if (fd_ < 0) return cfire::make_error("file is not open");
        return cfire::detail::fd_read_until(fd_, delim, max_bytes);
    }

    [[nodiscard]] auto read_bytes_until(std::uint8_t delim, std::size_t max_bytes = 65536)
        -> std::expected<std::vector<std::byte>, cfire::Error>
    {
        return read_bytes_until(static_cast<std::byte>(delim), max_bytes);
    }

    [[nodiscard]] auto read_text_until(std::string_view delim, std::size_t max_bytes = 65536)
        -> std::expected<std::string, cfire::Error>
    {
        if (fd_ < 0) return cfire::make_error("file is not open");
        if (delim.empty()) return cfire::make_error("read_text_until requires non-empty delimiter");
        std::string out;
        std::byte ch{0};
        while (out.size() < max_bytes) {
            auto n = cfire::detail::fd_read(fd_, std::span<std::byte>(&ch, 1));
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

    [[nodiscard]] auto write_bytes(std::span<const std::byte> data) -> std::expected<std::size_t, cfire::Error> {
        if (fd_ < 0) return cfire::make_error("file is not open");
        return cfire::detail::fd_write_all(fd_, data);
    }

    [[nodiscard]] auto write_bytes(std::span<const std::uint8_t> data) -> std::expected<std::size_t, cfire::Error> {
        return write_bytes(std::as_bytes(data));
    }

    [[nodiscard]] auto write_text(std::string_view s) -> std::expected<std::size_t, cfire::Error> {
        if (fd_ < 0) return cfire::make_error("file is not open");
        return cfire::detail::fd_write_all(fd_, std::as_bytes(std::span(s.data(), s.size())));
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
        return {};
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
        return cfire::make_error(std::format("failed to open file {}: {}", p.string(), std::strerror(errno)));
    }
    return File(fd, true);
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


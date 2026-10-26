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
#endif

namespace cfire::fs {

namespace detail {
[[nodiscard]] inline auto resolve_path(const std::filesystem::path& p, bool for_write) noexcept -> std::filesystem::path {
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

[[nodiscard]] inline auto read_all(const std::filesystem::path& p)
    -> std::expected<std::vector<std::byte>, cfire::Error>
{
    return read_bytes(p);
}

[[nodiscard]] inline auto write_all(
    const std::filesystem::path& p,
    std::span<const std::byte> data
) -> std::expected<void, cfire::Error> {
    return write_bytes(p, data);
}

[[nodiscard]] inline auto write_all(
    const std::filesystem::path& p,
    std::string_view text
) -> std::expected<void, cfire::Error> {
    return write_bytes(p, to_bytes(text));
}

[[nodiscard]] inline auto write_text(
    const std::filesystem::path& p,
    std::string_view text
) -> std::expected<void, cfire::Error> {
    return write_bytes(p, to_bytes(text));
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
    File() noexcept : fp_(nullptr) {}
    explicit File(std::FILE* fp) noexcept : fp_(fp) {}
    ~File() { close(); }

    File(const File&) = delete;
    auto operator=(const File&) -> File& = delete;

    File(File&& other) noexcept : fp_(std::exchange(other.fp_, nullptr)) {}
    auto operator=(File&& other) noexcept -> File& {
        if (this != &other) {
            close();
            fp_ = std::exchange(other.fp_, nullptr);
        }
        return *this;
    }

    [[nodiscard]] auto is_open() const noexcept -> bool {
        return fp_ != nullptr;
    }

    void close() noexcept {
        if (fp_) {
            std::fclose(fp_);
            fp_ = nullptr;
        }
    }

    [[nodiscard]] auto read(std::span<std::byte> buf) -> std::expected<std::size_t, cfire::Error> {
        if (!fp_) return cfire::make_error("file is not open");
        if (buf.empty()) return std::size_t{0};
        std::size_t n = std::fread(buf.data(), 1, buf.size(), fp_);
        if (n == 0 && std::ferror(fp_)) {
            return cfire::make_error("file read error");
        }
        return n;
    }

    [[nodiscard]] auto read(std::span<uint8_t> buf) -> std::expected<std::size_t, cfire::Error> {
        return read(std::as_writable_bytes(buf));
    }

    [[nodiscard]] auto read_all() -> std::expected<std::vector<std::byte>, cfire::Error> {
        if (!fp_) return cfire::make_error("file is not open");
        std::vector<std::byte> result;
        std::byte chunk[4096];
        while (true) {
            std::size_t n = std::fread(chunk, 1, sizeof(chunk), fp_);
            if (n > 0) {
                result.insert(result.end(), chunk, chunk + n);
            }
            if (n < sizeof(chunk)) {
                if (std::ferror(fp_)) return cfire::make_error("file read error");
                break;
            }
        }
        return result;
    }

    [[nodiscard]] auto write(std::span<const std::byte> data) -> std::expected<std::size_t, cfire::Error> {
        if (!fp_) return cfire::make_error("file is not open");
        if (data.empty()) return std::size_t{0};
        std::size_t n = std::fwrite(data.data(), 1, data.size(), fp_);
        if (n < data.size() && std::ferror(fp_)) {
            return cfire::make_error("file write error");
        }
        return n;
    }

    [[nodiscard]] auto write(std::span<const uint8_t> data) -> std::expected<std::size_t, cfire::Error> {
        return write(std::as_bytes(data));
    }

    [[nodiscard]] auto write_str(std::string_view s) -> std::expected<std::size_t, cfire::Error> {
        return write(std::as_bytes(std::span(s.data(), s.size())));
    }

    [[nodiscard]] auto seek(int64_t offset, SeekWhence whence = SeekWhence::Start) -> std::expected<int64_t, cfire::Error> {
        if (!fp_) return cfire::make_error("file is not open");
        int origin = SEEK_SET;
        if (whence == SeekWhence::Current) origin = SEEK_CUR;
        else if (whence == SeekWhence::End) origin = SEEK_END;
        if (std::fseek(fp_, static_cast<long>(offset), origin) != 0) {
            return cfire::make_error("file seek error");
        }
        long pos = std::ftell(fp_);
        if (pos < 0) return cfire::make_error("file ftell error");
        return static_cast<int64_t>(pos);
    }

    [[nodiscard]] auto tell() -> std::expected<int64_t, cfire::Error> {
        if (!fp_) return cfire::make_error("file is not open");
        long pos = std::ftell(fp_);
        if (pos < 0) return cfire::make_error("file ftell error");
        return static_cast<int64_t>(pos);
    }

    [[nodiscard]] auto flush() -> std::expected<void, cfire::Error> {
        if (!fp_) return cfire::make_error("file is not open");
        if (std::fflush(fp_) != 0) return cfire::make_error("file flush error");
        return {};
    }

private:
    std::FILE* fp_ = nullptr;
};

[[nodiscard]] inline auto open(const std::filesystem::path& p, OpenMode mode = OpenMode::Read)
    -> std::expected<File, cfire::Error>
{
    auto rp = detail::resolve_path(p, mode != OpenMode::Read);
    const char* m = "rb";
    switch (mode) {
        case OpenMode::Read: m = "rb"; break;
        case OpenMode::Write: m = "wb+"; break;
        case OpenMode::Append: m = "ab+"; break;
        case OpenMode::ReadWrite: m = "r+b"; break;
    }
    std::FILE* fp = std::fopen(rp.string().c_str(), m);
    if (!fp && mode == OpenMode::ReadWrite) {
        fp = std::fopen(rp.string().c_str(), "w+b");
    }
    if (!fp) {
        return cfire::make_error(std::format("failed to open file {}: {}", p.string(), std::strerror(errno)));
    }
    return File(fp);
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


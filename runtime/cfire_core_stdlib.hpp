#pragma once
// cfire_core_stdlib.hpp — Complete Built-in `core:*` C++23 Standard Library Catalog
// Modules: core:fs, core:cli, core:compress/zstd, core:json, core:db/sqlite,
//          core:crypto/blake3, core:net, core:time, core:os, core:sync, core:str, core:math, core:rand

#include "cfire_prelude.hpp"
#include <algorithm>
#include <atomic>
#include <cctype>
#include <charconv>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <deque>
#include <fstream>
#include <iostream>
#include <map>
#include <queue>
#include <random>
#include <regex>
#include <sstream>
#include <unordered_set>
#include <csignal>

// POSIX Socket headers for `core:net`
#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

#ifdef __APPLE__
#include <CommonCrypto/CommonDigest.h>
#include <CommonCrypto/CommonHMAC.h>
#include <crt_externs.h>
#endif

namespace cfire::fs {

[[nodiscard]] inline auto exists(const std::filesystem::path& p) noexcept -> bool {
    std::error_code ec;
    return std::filesystem::exists(p, ec);
}

[[nodiscard]] inline auto mkdir_all(const std::filesystem::path& p) -> std::expected<void, cfire::Error> {
    std::error_code ec;
    std::filesystem::create_directories(p, ec);
    if (ec) {
        return cfire::make_error(std::format("mkdir_all failed for {}: {}", p.string(), ec.message()));
    }
    return {};
}

[[nodiscard]] inline auto remove(const std::filesystem::path& p) -> std::expected<void, cfire::Error> {
    std::error_code ec;
    std::filesystem::remove_all(p, ec);
    if (ec) {
        return cfire::make_error(std::format("remove failed for {}: {}", p.string(), ec.message()));
    }
    return {};
}

[[nodiscard]] inline auto file_size(const std::filesystem::path& p) -> std::expected<std::uint64_t, cfire::Error> {
    std::error_code ec;
    auto sz = std::filesystem::file_size(p, ec);
    if (ec) {
        return cfire::make_error(std::format("file_size failed for {}: {}", p.string(), ec.message()));
    }
    return static_cast<std::uint64_t>(sz);
}

[[nodiscard]] inline auto list_dir(const std::filesystem::path& p) -> std::expected<std::vector<std::string>, cfire::Error> {
    std::error_code ec;
    std::vector<std::string> names;
    for (const auto& entry : std::filesystem::directory_iterator(p, ec)) {
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
    std::ifstream f(p, std::ios::binary | std::ios::ate);
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
    std::ifstream f(p, std::ios::in | std::ios::binary);
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
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
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
    const char* m = "rb";
    switch (mode) {
        case OpenMode::Read: m = "rb"; break;
        case OpenMode::Write: m = "wb+"; break;
        case OpenMode::Append: m = "ab+"; break;
        case OpenMode::ReadWrite: m = "r+b"; break;
    }
    std::FILE* fp = std::fopen(p.string().c_str(), m);
    if (!fp && mode == OpenMode::ReadWrite) {
        fp = std::fopen(p.string().c_str(), "w+b");
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

namespace cfire::cli {

template <typename T>
auto parse(int argc, char** argv) -> T;

class FlagReader {
public:
    FlagReader(int argc, char** argv) {
        for (int i = 1; i < argc; ++i) {
            args_.emplace_back(argv[i]);
        }
    }

    void bind(std::string_view long_flag, std::string_view short_flag, std::filesystem::path& target) const {
        if (auto val = find_val(long_flag, short_flag)) {
            target = *val;
        }
    }

    void bind(std::string_view long_flag, std::string_view short_flag, std::string& target) const {
        if (auto val = find_val(long_flag, short_flag)) {
            target = std::string(*val);
        }
    }

    void bind(std::string_view long_flag, std::string_view short_flag, std::int32_t& target) const {
        if (auto val = find_val(long_flag, short_flag)) {
            target = static_cast<std::int32_t>(std::stoi(std::string(*val)));
        }
    }

    void bind(std::string_view long_flag, std::string_view short_flag, std::int64_t& target) const {
        if (auto val = find_val(long_flag, short_flag)) {
            target = static_cast<std::int64_t>(std::stoll(std::string(*val)));
        }
    }

    void bind(std::string_view long_flag, std::string_view short_flag, std::size_t& target) const {
        if (auto val = find_val(long_flag, short_flag)) {
            target = static_cast<std::size_t>(std::stoull(std::string(*val)));
        }
    }

    void bind(std::string_view long_flag, std::string_view short_flag, double& target) const {
        if (auto val = find_val(long_flag, short_flag)) {
            target = std::stod(std::string(*val));
        }
    }

    void bind(std::string_view long_flag, std::string_view short_flag, bool& target) const {
        for (std::size_t i = 0; i < args_.size(); ++i) {
            if (args_[i] == long_flag || (!short_flag.empty() && args_[i] == short_flag)) {
                if (i + 1 < args_.size() && (args_[i + 1] == "true" || args_[i + 1] == "false")) {
                    target = (args_[i + 1] == "true");
                } else {
                    target = true;
                }
                return;
            }
        }
    }

private:
    [[nodiscard]] auto find_val(std::string_view lflag, std::string_view sflag) const -> std::optional<std::string_view> {
        for (std::size_t i = 0; i + 1 < args_.size(); ++i) {
            if (args_[i] == lflag || (!sflag.empty() && args_[i] == sflag)) {
                return args_[i + 1];
            }
        }
        return std::nullopt;
    }
    std::vector<std::string_view> args_;
};

} // namespace cfire::cli

// --- Optional Auto-Linked Core Modules (Activated by #define CFIRE_USE_*) ---

#ifdef CFIRE_USE_ZSTD
#include <zstd.h>
namespace cfire::zstd {
[[nodiscard]] inline auto compress(std::span<const std::byte> src, std::int32_t level = 3)
    -> std::expected<std::vector<std::byte>, cfire::Error>
{
    std::size_t bound = ZSTD_compressBound(src.size());
    std::vector<std::byte> out(bound);
    std::size_t written = ZSTD_compress(out.data(), bound, src.data(), src.size(), level);
    if (ZSTD_isError(written)) {
        return cfire::make_error(std::format("zstd compress error: {}", ZSTD_getErrorName(written)));
    }
    out.resize(written);
    return out;
}

[[nodiscard]] inline auto decompress(std::span<const std::byte> src)
    -> std::expected<std::vector<std::byte>, cfire::Error>
{
    unsigned long long content_size = ZSTD_getFrameContentSize(src.data(), src.size());
    if (content_size == ZSTD_CONTENTSIZE_ERROR || content_size == ZSTD_CONTENTSIZE_UNKNOWN) {
        return cfire::make_error("zstd unknown or invalid frame content size");
    }
    std::vector<std::byte> out(static_cast<std::size_t>(content_size));
    std::size_t decoded = ZSTD_decompress(out.data(), out.size(), src.data(), src.size());
    if (ZSTD_isError(decoded)) {
        return cfire::make_error(std::format("zstd decompress error: {}", ZSTD_getErrorName(decoded)));
    }
    return out;
}
} // namespace cfire::zstd
#endif

namespace cfire::json {

[[nodiscard]] inline auto escape_string(std::string_view sv) -> std::string {
    std::string out;
    out.reserve(sv.size() + 16);
    for (char c : sv) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    out += std::format("\\u{:04x}", static_cast<unsigned int>(static_cast<unsigned char>(c)));
                } else {
                    out.push_back(c);
                }
                break;
        }
    }
    return out;
}

struct Value;
using Array = std::vector<Value>;
using Object = std::vector<std::pair<std::string, Value>>;

struct Value {
    using VariantType = std::variant<std::monostate, bool, int64_t, double, std::string, Array, Object>;
    VariantType data{};

    Value() : data(std::monostate{}) {}
    Value(std::nullptr_t) : data(std::monostate{}) {}
    Value(bool b) : data(b) {}
    Value(int v) : data(static_cast<int64_t>(v)) {}
    Value(int64_t v) : data(v) {}
    Value(uint64_t v) : data(static_cast<int64_t>(v)) {}
    Value(double v) : data(v) {}
    Value(std::string s) : data(std::move(s)) {}
    Value(std::string_view s) : data(std::string(s)) {}
    Value(const char* s) : data(std::string(s)) {}
    Value(Array a) : data(std::move(a)) {}
    Value(Object o) : data(std::move(o)) {}

    [[nodiscard]] auto is_null() const noexcept -> bool { return std::holds_alternative<std::monostate>(data); }
    [[nodiscard]] auto is_bool() const noexcept -> bool { return std::holds_alternative<bool>(data); }
    [[nodiscard]] auto is_int() const noexcept -> bool { return std::holds_alternative<int64_t>(data); }
    [[nodiscard]] auto is_float() const noexcept -> bool { return std::holds_alternative<double>(data); }
    [[nodiscard]] auto is_number() const noexcept -> bool { return is_int() || is_float(); }
    [[nodiscard]] auto is_string() const noexcept -> bool { return std::holds_alternative<std::string>(data); }
    [[nodiscard]] auto is_array() const noexcept -> bool { return std::holds_alternative<Array>(data); }
    [[nodiscard]] auto is_object() const noexcept -> bool { return std::holds_alternative<Object>(data); }

    [[nodiscard]] auto as_bool() const noexcept -> bool {
        if (const auto* b = std::get_if<bool>(&data)) return *b;
        return false;
    }

    [[nodiscard]] auto as_int() const noexcept -> int64_t {
        if (const auto* i = std::get_if<int64_t>(&data)) return *i;
        if (const auto* d = std::get_if<double>(&data)) return static_cast<int64_t>(*d);
        return 0;
    }

    [[nodiscard]] auto as_float() const noexcept -> double {
        if (const auto* d = std::get_if<double>(&data)) return *d;
        if (const auto* i = std::get_if<int64_t>(&data)) return static_cast<double>(*i);
        return 0.0;
    }

    [[nodiscard]] auto as_str() const noexcept -> std::string_view {
        if (const auto* s = std::get_if<std::string>(&data)) return *s;
        return "";
    }

    [[nodiscard]] auto as_array() const -> const Array& {
        static const Array empty_arr;
        if (const auto* a = std::get_if<Array>(&data)) return *a;
        return empty_arr;
    }

    [[nodiscard]] auto as_object() const -> const Object& {
        static const Object empty_obj;
        if (const auto* o = std::get_if<Object>(&data)) return *o;
        return empty_obj;
    }

    [[nodiscard]] auto get(std::string_view key) const -> std::optional<Value> {
        if (const auto* o = std::get_if<Object>(&data)) {
            for (const auto& [k, v] : *o) {
                if (k == key) return v;
            }
        }
        return std::nullopt;
    }

    [[nodiscard]] auto at(std::size_t index) const -> std::optional<Value> {
        if (const auto* a = std::get_if<Array>(&data)) {
            if (index < a->size()) return (*a)[index];
        }
        return std::nullopt;
    }

    [[nodiscard]] auto operator[](std::string_view key) const -> Value {
        if (auto v = get(key)) return *v;
        return Value{};
    }

    [[nodiscard]] auto operator[](std::size_t index) const -> Value {
        if (auto v = at(index)) return *v;
        return Value{};
    }

    [[nodiscard]] auto size() const noexcept -> std::size_t {
        if (const auto* a = std::get_if<Array>(&data)) return a->size();
        if (const auto* o = std::get_if<Object>(&data)) return o->size();
        if (const auto* s = std::get_if<std::string>(&data)) return s->size();
        return 0;
    }

    [[nodiscard]] auto keys() const -> std::vector<std::string> {
        std::vector<std::string> res;
        if (const auto* o = std::get_if<Object>(&data)) {
            for (const auto& [k, _] : *o) res.push_back(k);
        }
        return res;
    }
};

class DynamicJsonParser {
public:
    explicit DynamicJsonParser(std::string_view s) : src_(s) {}

    auto parse() -> std::expected<Value, cfire::Error> {
        skip_ws();
        auto val = parse_val();
        if (!val) return std::unexpected(val.error());
        skip_ws();
        if (pos_ < src_.size()) {
            return cfire::make_error(std::format("unexpected trailing characters at position {}", pos_));
        }
        return val;
    }

private:
    std::string_view src_;
    std::size_t pos_ = 0;

    void skip_ws() {
        while (pos_ < src_.size() && (src_[pos_] == ' ' || src_[pos_] == '\t' || src_[pos_] == '\n' || src_[pos_] == '\r')) {
            ++pos_;
        }
    }

    char cur() const { return pos_ < src_.size() ? src_[pos_] : '\0'; }

    auto parse_val() -> std::expected<Value, cfire::Error> {
        skip_ws();
        char c = cur();
        if (c == '{') return parse_obj();
        if (c == '[') return parse_arr();
        if (c == '"') return parse_str();
        if (c == 't' || c == 'f') return parse_bool();
        if (c == 'n') return parse_null();
        if (c == '-' || (c >= '0' && c <= '9')) return parse_num();
        return cfire::make_error(std::format("unexpected token '{}' at position {}", c, pos_));
    }

    auto parse_obj() -> std::expected<Value, cfire::Error> {
        ++pos_; // '{'
        skip_ws();
        Object obj;
        if (cur() == '}') {
            ++pos_;
            return Value(std::move(obj));
        }
        while (pos_ < src_.size()) {
            skip_ws();
            if (cur() != '"') return cfire::make_error(std::format("expected string key in object at position {}", pos_));
            auto key_val = parse_str();
            if (!key_val) return std::unexpected(key_val.error());
            std::string key = std::string(key_val->as_str());
            skip_ws();
            if (cur() != ':') return cfire::make_error(std::format("expected ':' after key in object at position {}", pos_));
            ++pos_; // ':'
            skip_ws();
            auto v = parse_val();
            if (!v) return std::unexpected(v.error());
            obj.emplace_back(std::move(key), std::move(*v));
            skip_ws();
            if (cur() == '}') {
                ++pos_;
                return Value(std::move(obj));
            }
            if (cur() == ',') {
                ++pos_;
            } else {
                return cfire::make_error(std::format("expected ',' or '}}' in object at position {}", pos_));
            }
        }
        return cfire::make_error("unterminated object");
    }

    auto parse_arr() -> std::expected<Value, cfire::Error> {
        ++pos_; // '['
        skip_ws();
        Array arr;
        if (cur() == ']') {
            ++pos_;
            return Value(std::move(arr));
        }
        while (pos_ < src_.size()) {
            skip_ws();
            auto v = parse_val();
            if (!v) return std::unexpected(v.error());
            arr.push_back(std::move(*v));
            skip_ws();
            if (cur() == ']') {
                ++pos_;
                return Value(std::move(arr));
            }
            if (cur() == ',') {
                ++pos_;
            } else {
                return cfire::make_error(std::format("expected ',' or ']' in array at position {}", pos_));
            }
        }
        return cfire::make_error("unterminated array");
    }

    auto parse_str() -> std::expected<Value, cfire::Error> {
        ++pos_; // '"'
        std::string s;
        while (pos_ < src_.size()) {
            char c = src_[pos_++];
            if (c == '"') return Value(std::move(s));
            if (c == '\\') {
                if (pos_ >= src_.size()) return cfire::make_error("unexpected end of string escape");
                char esc = src_[pos_++];
                switch (esc) {
                    case '"': s.push_back('"'); break;
                    case '\\': s.push_back('\\'); break;
                    case '/': s.push_back('/'); break;
                    case 'b': s.push_back('\b'); break;
                    case 'f': s.push_back('\f'); break;
                    case 'n': s.push_back('\n'); break;
                    case 'r': s.push_back('\r'); break;
                    case 't': s.push_back('\t'); break;
                    case 'u': {
                        if (pos_ + 4 > src_.size()) return cfire::make_error("incomplete unicode escape");
                        std::string hex_str(src_.substr(pos_, 4));
                        pos_ += 4;
                        unsigned int cp = 0;
                        auto [ptr, ec] = std::from_chars(hex_str.data(), hex_str.data() + 4, cp, 16);
                        if (ec != std::errc()) return cfire::make_error("invalid unicode escape");
                        if (cp < 0x80) {
                            s.push_back(static_cast<char>(cp));
                        } else if (cp < 0x800) {
                            s.push_back(static_cast<char>(0xC0 | (cp >> 6)));
                            s.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
                        } else {
                            s.push_back(static_cast<char>(0xE0 | (cp >> 12)));
                            s.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
                            s.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
                        }
                        break;
                    }
                    default:
                        s.push_back(esc);
                        break;
                }
            } else {
                s.push_back(c);
            }
        }
        return cfire::make_error("unterminated string");
    }

    auto parse_bool() -> std::expected<Value, cfire::Error> {
        if (src_.substr(pos_).starts_with("true")) {
            pos_ += 4;
            return Value(true);
        }
        if (src_.substr(pos_).starts_with("false")) {
            pos_ += 5;
            return Value(false);
        }
        return cfire::make_error(std::format("invalid boolean at position {}", pos_));
    }

    auto parse_null() -> std::expected<Value, cfire::Error> {
        if (src_.substr(pos_).starts_with("null")) {
            pos_ += 4;
            return Value{};
        }
        return cfire::make_error(std::format("invalid null at position {}", pos_));
    }

    auto parse_num() -> std::expected<Value, cfire::Error> {
        std::size_t start = pos_;
        bool is_float = false;
        if (cur() == '-') ++pos_;
        while (pos_ < src_.size() && (src_[pos_] >= '0' && src_[pos_] <= '9')) ++pos_;
        if (pos_ < src_.size() && src_[pos_] == '.') {
            is_float = true;
            ++pos_;
            while (pos_ < src_.size() && (src_[pos_] >= '0' && src_[pos_] <= '9')) ++pos_;
        }
        if (pos_ < src_.size() && (src_[pos_] == 'e' || src_[pos_] == 'E')) {
            is_float = true;
            ++pos_;
            if (pos_ < src_.size() && (src_[pos_] == '+' || src_[pos_] == '-')) ++pos_;
            while (pos_ < src_.size() && (src_[pos_] >= '0' && src_[pos_] <= '9')) ++pos_;
        }
        std::string_view num_str = src_.substr(start, pos_ - start);
        if (is_float) {
            char* endptr = nullptr;
            std::string s(num_str);
            double d = std::strtod(s.c_str(), &endptr);
            return Value(d);
        } else {
            int64_t i = 0;
            auto [p, ec] = std::from_chars(num_str.data(), num_str.data() + num_str.size(), i);
            if (ec != std::errc()) {
                return cfire::make_error(std::format("invalid integer number '{}'", num_str));
            }
            return Value(i);
        }
    }
};

[[nodiscard]] inline auto parse_dynamic(std::string_view payload) -> std::expected<Value, cfire::Error> {
    DynamicJsonParser p(payload);
    return p.parse();
}

inline void stringify_value_to(const Value& val, std::string& out, int indent, int current_depth) {
    if (val.is_null()) {
        out += "null";
    } else if (val.is_bool()) {
        out += val.as_bool() ? "true" : "false";
    } else if (val.is_int()) {
        out += std::to_string(val.as_int());
    } else if (val.is_float()) {
        out += std::format("{}", val.as_float());
    } else if (val.is_string()) {
        out += "\"";
        out += escape_string(val.as_str());
        out += "\"";
    } else if (val.is_array()) {
        const auto& arr = val.as_array();
        if (arr.empty()) {
            out += "[]";
            return;
        }
        out += "[";
        if (indent > 0) out += "\n";
        for (std::size_t i = 0; i < arr.size(); ++i) {
            if (indent > 0) out.append((current_depth + 1) * indent, ' ');
            stringify_value_to(arr[i], out, indent, current_depth + 1);
            if (i + 1 < arr.size()) out += ",";
            if (indent > 0) out += "\n";
        }
        if (indent > 0) out.append(current_depth * indent, ' ');
        out += "]";
    } else if (val.is_object()) {
        const auto& obj = val.as_object();
        if (obj.empty()) {
            out += "{}";
            return;
        }
        out += "{";
        if (indent > 0) out += "\n";
        for (std::size_t i = 0; i < obj.size(); ++i) {
            if (indent > 0) out.append((current_depth + 1) * indent, ' ');
            out += "\"";
            out += escape_string(obj[i].first);
            out += "\":";
            if (indent > 0) out += " ";
            stringify_value_to(obj[i].second, out, indent, current_depth + 1);
            if (i + 1 < obj.size()) out += ",";
            if (indent > 0) out += "\n";
        }
        if (indent > 0) out.append(current_depth * indent, ' ');
        out += "}";
    }
}

[[nodiscard]] inline auto stringify_dynamic(const Value& val, int indent = 0) -> std::string {
    std::string out;
    stringify_value_to(val, out, indent, 0);
    return out;
}

} // namespace cfire::json

#ifdef CFIRE_USE_SIMDJSON
#include <simdjson.h>
namespace cfire::json {

template <typename T>
auto parse_struct(simdjson::ondemand::object obj) -> std::expected<T, cfire::Error>;

template <typename T>
auto stringify_struct(const T& val) -> std::string;

template <typename T>
[[nodiscard]] auto parse(std::string_view payload) -> std::expected<T, cfire::Error> {
    simdjson::ondemand::parser parser;
    simdjson::padded_string padded(payload.data(), payload.size());
    auto doc = parser.iterate(padded);
    if (doc.error()) {
        return cfire::make_error(std::format("simdjson iterate error: {}", simdjson::error_message(doc.error())));
    }
    auto obj = doc.get_object();
    if (obj.error()) {
        return cfire::make_error(std::format("simdjson object error: {}", simdjson::error_message(obj.error())));
    }
    return parse_struct<T>(obj.value());
}

template <typename T>
[[nodiscard]] auto stringify(const T& val) -> std::string {
    return stringify_struct<T>(val);
}

[[nodiscard]] inline auto get_str(simdjson::ondemand::object& obj, std::string_view key)
    -> std::expected<std::string, cfire::Error>
{
    auto res = obj[key].get_string();
    if (res.error()) {
        return cfire::make_error(std::format("missing or invalid string field '{}'", key));
    }
    return std::string(res.value());
}

[[nodiscard]] inline auto get_u64(simdjson::ondemand::object& obj, std::string_view key)
    -> std::expected<std::uint64_t, cfire::Error>
{
    auto res = obj[key].get_uint64();
    if (res.error()) {
        return cfire::make_error(std::format("missing or invalid u64 field '{}'", key));
    }
    return res.value();
}

[[nodiscard]] inline auto get_i64(simdjson::ondemand::object& obj, std::string_view key)
    -> std::expected<std::int64_t, cfire::Error>
{
    auto res = obj[key].get_int64();
    if (res.error()) {
        return cfire::make_error(std::format("missing or invalid i64 field '{}'", key));
    }
    return res.value();
}

[[nodiscard]] inline auto get_i32(simdjson::ondemand::object& obj, std::string_view key)
    -> std::expected<std::int32_t, cfire::Error>
{
    auto res = obj[key].get_int64();
    if (res.error()) {
        return cfire::make_error(std::format("missing or invalid i32 field '{}'", key));
    }
    return static_cast<std::int32_t>(res.value());
}

[[nodiscard]] inline auto get_f64(simdjson::ondemand::object& obj, std::string_view key)
    -> std::expected<double, cfire::Error>
{
    auto res = obj[key].get_double();
    if (res.error()) {
        return cfire::make_error(std::format("missing or invalid f64 field '{}'", key));
    }
    return res.value();
}

[[nodiscard]] inline auto get_bool(simdjson::ondemand::object& obj, std::string_view key)
    -> std::expected<bool, cfire::Error>
{
    auto res = obj[key].get_bool();
    if (res.error()) {
        return cfire::make_error(std::format("missing or invalid bool field '{}'", key));
    }
    return res.value();
}

} // namespace cfire::json
#endif

#ifdef CFIRE_USE_SQLITE
#include <sqlite3.h>
namespace cfire::sqlite {

template <typename T>
auto map_row(sqlite3_stmt* stmt) -> T;

inline void bind_arg(sqlite3_stmt* stmt, int idx, std::string_view v) {
    sqlite3_bind_text(stmt, idx, v.data(), static_cast<int>(v.size()), SQLITE_TRANSIENT);
}
inline void bind_arg(sqlite3_stmt* stmt, int idx, std::uint64_t v) {
    sqlite3_bind_int64(stmt, idx, static_cast<sqlite3_int64>(v));
}
inline void bind_arg(sqlite3_stmt* stmt, int idx, std::int64_t v) {
    sqlite3_bind_int64(stmt, idx, static_cast<sqlite3_int64>(v));
}
inline void bind_arg(sqlite3_stmt* stmt, int idx, std::int32_t v) {
    sqlite3_bind_int(stmt, idx, v);
}
inline void bind_arg(sqlite3_stmt* stmt, int idx, double v) {
    sqlite3_bind_double(stmt, idx, v);
}
inline void bind_arg(sqlite3_stmt* stmt, int idx, bool v) {
    sqlite3_bind_int(stmt, idx, v ? 1 : 0);
}

class Conn {
public:
    explicit Conn(sqlite3* db) : db_(db) {}
    ~Conn() {
        if (db_) sqlite3_close(db_);
    }
    Conn(Conn&& other) noexcept : db_(std::exchange(other.db_, nullptr)) {}
    auto operator=(Conn&& other) noexcept -> Conn& {
        if (this != &other) {
            if (db_) sqlite3_close(db_);
            db_ = std::exchange(other.db_, nullptr);
        }
        return *this;
    }

    template <typename... Args>
    [[nodiscard]] auto exec(std::string_view sql, const Args&... args) -> std::expected<void, cfire::Error> {
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(db_, sql.data(), static_cast<int>(sql.size()), &stmt, nullptr) != SQLITE_OK) {
            return cfire::make_error(std::format("sqlite prepare error: {}", sqlite3_errmsg(db_)));
        }
        auto guard = cfire::scope_exit([stmt] { sqlite3_finalize(stmt); });
        int idx = 1;
        (bind_arg(stmt, idx++, args), ...);
        int rc = sqlite3_step(stmt);
        if (rc != SQLITE_DONE && rc != SQLITE_ROW) {
            return cfire::make_error(std::format("sqlite step error: {}", sqlite3_errmsg(db_)));
        }
        return {};
    }

    template <typename T, typename... Args>
    [[nodiscard]] auto query(std::string_view sql, const Args&... args) -> std::expected<std::vector<T>, cfire::Error> {
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(db_, sql.data(), static_cast<int>(sql.size()), &stmt, nullptr) != SQLITE_OK) {
            return cfire::make_error(std::format("sqlite prepare error: {}", sqlite3_errmsg(db_)));
        }
        auto guard = cfire::scope_exit([stmt] { sqlite3_finalize(stmt); });
        int idx = 1;
        (bind_arg(stmt, idx++, args), ...);
        std::vector<T> rows;
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            rows.push_back(map_row<T>(stmt));
        }
        return rows;
    }

    template <typename F>
    [[nodiscard]] auto tx(F&& fn) -> std::expected<void, cfire::Error> {
        CFIRE_TRY(exec("BEGIN IMMEDIATE"));
        auto res = fn(*this);
        if (!res.has_value()) {
            (void)exec("ROLLBACK");
            return res;
        }
        CFIRE_TRY(exec("COMMIT"));
        return {};
    }

private:
    sqlite3* db_ = nullptr;
};

[[nodiscard]] inline auto open(const std::filesystem::path& p) -> std::expected<Conn, cfire::Error> {
    sqlite3* raw = nullptr;
    if (sqlite3_open(p.c_str(), &raw) != SQLITE_OK) {
        std::string err = raw ? sqlite3_errmsg(raw) : "unknown sqlite open error";
        if (raw) sqlite3_close(raw);
        return cfire::make_error(err);
    }
    Conn c(raw);
    (void)c.exec("PRAGMA journal_mode=WAL;");
    return c;
}

[[nodiscard]] inline auto col_str(sqlite3_stmt* stmt, int col) -> std::string {
    const unsigned char* txt = sqlite3_column_text(stmt, col);
    return txt ? reinterpret_cast<const char*>(txt) : "";
}
[[nodiscard]] inline auto col_u64(sqlite3_stmt* stmt, int col) -> std::uint64_t {
    return static_cast<std::uint64_t>(sqlite3_column_int64(stmt, col));
}
[[nodiscard]] inline auto col_i64(sqlite3_stmt* stmt, int col) -> std::int64_t {
    return static_cast<std::int64_t>(sqlite3_column_int64(stmt, col));
}
[[nodiscard]] inline auto col_i32(sqlite3_stmt* stmt, int col) -> std::int32_t {
    return static_cast<std::int32_t>(sqlite3_column_int(stmt, col));
}
[[nodiscard]] inline auto col_f64(sqlite3_stmt* stmt, int col) -> double {
    return sqlite3_column_double(stmt, col);
}
[[nodiscard]] inline auto col_bool(sqlite3_stmt* stmt, int col) -> bool {
    return sqlite3_column_int(stmt, col) != 0;
}

} // namespace cfire::sqlite
#endif

// --- `core:crypto/sha256` & `core:crypto/hmac` (Standard FIPS 180-4 SHA-256 & RFC 2104 HMAC) ---
namespace cfire::sha256 {

namespace detail {
    inline constexpr std::uint32_t K[64] = {
        0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
        0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
        0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
        0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
        0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
        0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
        0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
        0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
    };

    constexpr inline auto rotr(std::uint32_t x, std::uint32_t n) noexcept -> std::uint32_t {
        return (x >> n) | (x << (32 - n));
    }

    inline void transform(std::uint32_t state[8], const std::uint8_t block[64]) noexcept {
        std::uint32_t w[64];
        for (std::size_t i = 0; i < 16; ++i) {
            w[i] = (static_cast<std::uint32_t>(block[i * 4]) << 24) |
                   (static_cast<std::uint32_t>(block[i * 4 + 1]) << 16) |
                   (static_cast<std::uint32_t>(block[i * 4 + 2]) << 8) |
                   (static_cast<std::uint32_t>(block[i * 4 + 3]));
        }
        for (std::size_t i = 16; i < 64; ++i) {
            std::uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
            std::uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }

        std::uint32_t a = state[0], b = state[1], c = state[2], d = state[3];
        std::uint32_t e = state[4], f = state[5], g = state[6], h = state[7];

        for (std::size_t i = 0; i < 64; ++i) {
            std::uint32_t S1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
            std::uint32_t ch = (e & f) ^ ((~e) & g);
            std::uint32_t temp1 = h + S1 + ch + K[i] + w[i];
            std::uint32_t S0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
            std::uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
            std::uint32_t temp2 = S0 + maj;

            h = g;
            g = f;
            f = e;
            e = d + temp1;
            d = c;
            c = b;
            b = a;
            a = temp1 + temp2;
        }

        state[0] += a; state[1] += b; state[2] += c; state[3] += d;
        state[4] += e; state[5] += f; state[6] += g; state[7] += h;
    }
} // namespace detail

[[nodiscard]] inline auto hash_bytes(std::span<const std::byte> data) -> std::vector<std::byte> {
#ifdef __APPLE__
    unsigned char digest[CC_SHA256_DIGEST_LENGTH];
    CC_SHA256(data.data(), static_cast<CC_LONG>(data.size()), digest);
    std::vector<std::byte> out(CC_SHA256_DIGEST_LENGTH);
    std::memcpy(out.data(), digest, CC_SHA256_DIGEST_LENGTH);
    return out;
#else
    std::uint32_t state[8] = {
        0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
        0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19
    };
    std::uint64_t total_bits = data.size() * 8;
    std::size_t offset = 0;
    while (offset + 64 <= data.size()) {
        detail::transform(state, reinterpret_cast<const std::uint8_t*>(data.data() + offset));
        offset += 64;
    }

    std::uint8_t block[64] = {0};
    std::size_t rem = data.size() - offset;
    if (rem > 0) {
        std::memcpy(block, data.data() + offset, rem);
    }
    block[rem] = 0x80;
    if (rem >= 56) {
        detail::transform(state, block);
        std::memset(block, 0, 64);
    }
    for (std::size_t i = 0; i < 8; ++i) {
        block[63 - i] = static_cast<std::uint8_t>((total_bits >> (i * 8)) & 0xFF);
    }
    detail::transform(state, block);

    std::vector<std::byte> out(32);
    for (std::size_t i = 0; i < 8; ++i) {
        out[i * 4]     = static_cast<std::byte>((state[i] >> 24) & 0xFF);
        out[i * 4 + 1] = static_cast<std::byte>((state[i] >> 16) & 0xFF);
        out[i * 4 + 2] = static_cast<std::byte>((state[i] >> 8) & 0xFF);
        out[i * 4 + 3] = static_cast<std::byte>(state[i] & 0xFF);
    }
    return out;
#endif
}

[[nodiscard]] inline auto hash_hex(std::span<const std::byte> data) -> std::string {
    auto b = hash_bytes(data);
    std::string hex;
    hex.reserve(64);
    for (std::byte byte_val : b) {
        hex += std::format("{:02x}", static_cast<unsigned char>(byte_val));
    }
    return hex;
}

[[nodiscard]] inline auto hash_hex(std::string_view sv) -> std::string {
    return hash_hex(std::span<const std::byte>(reinterpret_cast<const std::byte*>(sv.data()), sv.size()));
}

} // namespace cfire::sha256

namespace cfire::hmac {

[[nodiscard]] inline auto sha256_bytes(std::span<const std::byte> key, std::span<const std::byte> data) -> std::vector<std::byte> {
#ifdef __APPLE__
    unsigned char digest[CC_SHA256_DIGEST_LENGTH];
    CCHmac(kCCHmacAlgSHA256, key.data(), key.size(), data.data(), data.size(), digest);
    std::vector<std::byte> out(CC_SHA256_DIGEST_LENGTH);
    std::memcpy(out.data(), digest, CC_SHA256_DIGEST_LENGTH);
    return out;
#else
    std::uint8_t k0[64] = {0};
    if (key.size() > 64) {
        auto hk = cfire::sha256::hash_bytes(key);
        std::memcpy(k0, hk.data(), hk.size());
    } else {
        std::memcpy(k0, key.data(), key.size());
    }

    std::uint8_t ipad[64], opad[64];
    for (std::size_t i = 0; i < 64; ++i) {
        ipad[i] = k0[i] ^ 0x36;
        opad[i] = k0[i] ^ 0x5c;
    }

    std::vector<std::byte> inner_data;
    inner_data.reserve(64 + data.size());
    for (std::uint8_t b : ipad) inner_data.push_back(static_cast<std::byte>(b));
    inner_data.insert(inner_data.end(), data.begin(), data.end());
    auto inner_hash = cfire::sha256::hash_bytes(inner_data);

    std::vector<std::byte> outer_data;
    outer_data.reserve(64 + inner_hash.size());
    for (std::uint8_t b : opad) outer_data.push_back(static_cast<std::byte>(b));
    outer_data.insert(outer_data.end(), inner_hash.begin(), inner_hash.end());
    return cfire::sha256::hash_bytes(outer_data);
#endif
}

[[nodiscard]] inline auto sha256_hex(std::span<const std::byte> key, std::span<const std::byte> data) -> std::string {
    auto b = sha256_bytes(key, data);
    std::string hex;
    hex.reserve(64);
    for (std::byte byte_val : b) {
        hex += std::format("{:02x}", static_cast<unsigned char>(byte_val));
    }
    return hex;
}

[[nodiscard]] inline auto sha256_hex(std::string_view key, std::string_view data) -> std::string {
    return sha256_hex(
        std::span<const std::byte>(reinterpret_cast<const std::byte*>(key.data()), key.size()),
        std::span<const std::byte>(reinterpret_cast<const std::byte*>(data.data()), data.size())
    );
}

[[nodiscard]] inline auto equal(std::span<const std::byte> a, std::span<const std::byte> b) -> bool {
    if (a.size() != b.size()) return false;
    unsigned char diff = 0;
    for (std::size_t i = 0; i < a.size(); ++i) {
        diff |= static_cast<unsigned char>(a[i]) ^ static_cast<unsigned char>(b[i]);
    }
    return diff == 0;
}

[[nodiscard]] inline auto equal(std::string_view a, std::string_view b) -> bool {
    if (a.size() != b.size()) return false;
    unsigned char diff = 0;
    for (std::size_t i = 0; i < a.size(); ++i) {
        diff |= static_cast<unsigned char>(a[i]) ^ static_cast<unsigned char>(b[i]);
    }
    return diff == 0;
}

} // namespace cfire::hmac

// Backward compatibility alias for blake3
namespace cfire::blake3 {
[[nodiscard]] inline auto hash_hex(std::span<const std::byte> data) -> std::string {
    return cfire::sha256::hash_hex(data);
}
[[nodiscard]] inline auto hash_hex(std::string_view sv) -> std::string {
    return cfire::sha256::hash_hex(sv);
}
} // namespace cfire::blake3

// --- `core:crypto/aes` (AES-256 Symmetric Encryption, CTR and GCM AEAD) ---
namespace cfire::aes {

struct GcmResult {
    std::vector<std::byte> ciphertext;
    std::vector<std::byte> tag;
};

namespace detail {

inline constexpr std::uint8_t kSBox[256] = {
    0x63, 0x7c, 0x77, 0x7b, 0xf2, 0x6b, 0x6f, 0xc5, 0x30, 0x01, 0x67, 0x2b, 0xfe, 0xd7, 0xab, 0x76,
    0xca, 0x82, 0xc9, 0x7d, 0xfa, 0x59, 0x47, 0xf0, 0xad, 0xd4, 0xa2, 0xaf, 0x9c, 0xa4, 0x72, 0xc0,
    0xb7, 0xfd, 0x93, 0x26, 0x36, 0x3f, 0xf7, 0xcc, 0x34, 0xa5, 0xe5, 0xf1, 0x71, 0xd8, 0x31, 0x15,
    0x04, 0xc7, 0x23, 0xc3, 0x18, 0x96, 0x05, 0x9a, 0x07, 0x12, 0x80, 0xe2, 0xeb, 0x27, 0xb2, 0x75,
    0x09, 0x83, 0x2c, 0x1a, 0x1b, 0x6e, 0x5a, 0xa0, 0x52, 0x3b, 0xd6, 0xb3, 0x29, 0xe3, 0x2f, 0x84,
    0x53, 0xd1, 0x00, 0xed, 0x20, 0xfc, 0xb1, 0x5b, 0x6a, 0xcb, 0xbe, 0x39, 0x4a, 0x4c, 0x58, 0xcf,
    0xd0, 0xef, 0xaa, 0xfb, 0x43, 0x4d, 0x33, 0x85, 0x45, 0xf9, 0x02, 0x7f, 0x50, 0x3c, 0x9f, 0xa8,
    0x51, 0xa3, 0x40, 0x8f, 0x92, 0x9d, 0x38, 0xf5, 0xbc, 0xb6, 0xda, 0x21, 0x10, 0xff, 0xf3, 0xd2,
    0xcd, 0x0c, 0x13, 0xec, 0x5f, 0x97, 0x44, 0x17, 0xc4, 0xa7, 0x7e, 0x3d, 0x64, 0x5d, 0x19, 0x73,
    0x60, 0x81, 0x4f, 0xdc, 0x22, 0x2a, 0x90, 0x88, 0x46, 0xee, 0xb8, 0x14, 0xde, 0x5e, 0x0b, 0xdb,
    0xe0, 0x32, 0x3a, 0x0a, 0x49, 0x06, 0x24, 0x5c, 0xc2, 0xd3, 0xac, 0x62, 0x91, 0x95, 0xe4, 0x79,
    0xe7, 0xc8, 0x37, 0x6d, 0x8d, 0xd5, 0x4e, 0xa9, 0x6c, 0x56, 0xf4, 0xea, 0x65, 0x7a, 0xae, 0x08,
    0xba, 0x78, 0x25, 0x2e, 0x1c, 0xa6, 0xb4, 0xc6, 0xe8, 0xdd, 0x74, 0x1f, 0x4b, 0xbd, 0x8b, 0x8a,
    0x70, 0x3e, 0xb5, 0x66, 0x48, 0x03, 0xf6, 0x0e, 0x61, 0x35, 0x57, 0xb9, 0x86, 0xc1, 0x1d, 0x9e,
    0xe1, 0xf8, 0x98, 0x11, 0x69, 0xd9, 0x8e, 0x94, 0x9b, 0x1e, 0x87, 0xe9, 0xce, 0x55, 0x28, 0xdf,
    0x8c, 0xa1, 0x89, 0x0d, 0xbf, 0xe6, 0x42, 0x68, 0x41, 0x99, 0x2d, 0x0f, 0xb0, 0x54, 0xbb, 0x16
};

inline constexpr std::uint32_t kRcon[10] = {
    0x01000000, 0x02000000, 0x04000000, 0x08000000, 0x10000000,
    0x20000000, 0x40000000, 0x80000000, 0x1B000000, 0x36000000
};

inline void aes256_key_expansion(const std::uint8_t key[32], std::uint32_t w[60]) {
    for (int i = 0; i < 8; ++i) {
        w[i] = (static_cast<std::uint32_t>(key[4 * i]) << 24) |
               (static_cast<std::uint32_t>(key[4 * i + 1]) << 16) |
               (static_cast<std::uint32_t>(key[4 * i + 2]) << 8) |
               (static_cast<std::uint32_t>(key[4 * i + 3]));
    }
    for (int i = 8; i < 60; ++i) {
        std::uint32_t temp = w[i - 1];
        if (i % 8 == 0) {
            temp = (temp << 8) | (temp >> 24);
            temp = (static_cast<std::uint32_t>(kSBox[(temp >> 24) & 0xFF]) << 24) |
                   (static_cast<std::uint32_t>(kSBox[(temp >> 16) & 0xFF]) << 16) |
                   (static_cast<std::uint32_t>(kSBox[(temp >> 8) & 0xFF]) << 8) |
                   (static_cast<std::uint32_t>(kSBox[temp & 0xFF]));
            temp ^= kRcon[(i / 8) - 1];
        } else if (i % 8 == 4) {
            temp = (static_cast<std::uint32_t>(kSBox[(temp >> 24) & 0xFF]) << 24) |
                   (static_cast<std::uint32_t>(kSBox[(temp >> 16) & 0xFF]) << 16) |
                   (static_cast<std::uint32_t>(kSBox[(temp >> 8) & 0xFF]) << 8) |
                   (static_cast<std::uint32_t>(kSBox[temp & 0xFF]));
        }
        w[i] = w[i - 8] ^ temp;
    }
}

inline auto gmul(std::uint8_t a, std::uint8_t b) -> std::uint8_t {
    std::uint8_t p = 0;
    for (int counter = 0; counter < 8; ++counter) {
        if (b & 1) p ^= a;
        std::uint8_t hi_bit_set = a & 0x80;
        a <<= 1;
        if (hi_bit_set) a ^= 0x1b;
        b >>= 1;
    }
    return p;
}

inline void aes256_encrypt_block(const std::uint32_t w[60], const std::uint8_t in[16], std::uint8_t out[16]) {
    std::uint8_t state[4][4];
    for (int r = 0; r < 4; ++r) {
        for (int c = 0; c < 4; ++c) {
            state[r][c] = in[r + 4 * c];
        }
    }
    for (int c = 0; c < 4; ++c) {
        std::uint32_t word = w[c];
        state[0][c] ^= (word >> 24) & 0xFF;
        state[1][c] ^= (word >> 16) & 0xFF;
        state[2][c] ^= (word >> 8) & 0xFF;
        state[3][c] ^= word & 0xFF;
    }
    for (int round = 1; round <= 14; ++round) {
        for (int r = 0; r < 4; ++r) {
            for (int c = 0; c < 4; ++c) {
                state[r][c] = kSBox[state[r][c]];
            }
        }
        std::uint8_t t;
        t = state[1][0]; state[1][0] = state[1][1]; state[1][1] = state[1][2]; state[1][2] = state[1][3]; state[1][3] = t;
        t = state[2][0]; state[2][0] = state[2][2]; state[2][2] = t;
        t = state[2][1]; state[2][1] = state[2][3]; state[2][3] = t;
        t = state[3][3]; state[3][3] = state[3][2]; state[3][2] = state[3][1]; state[3][1] = state[3][0]; state[3][0] = t;

        if (round < 14) {
            for (int c = 0; c < 4; ++c) {
                std::uint8_t a0 = state[0][c], a1 = state[1][c], a2 = state[2][c], a3 = state[3][c];
                state[0][c] = gmul(2, a0) ^ gmul(3, a1) ^ a2 ^ a3;
                state[1][c] = a0 ^ gmul(2, a1) ^ gmul(3, a2) ^ a3;
                state[2][c] = a0 ^ a1 ^ gmul(2, a2) ^ gmul(3, a3);
                state[3][c] = gmul(3, a0) ^ a1 ^ a2 ^ gmul(2, a3);
            }
        }
        for (int c = 0; c < 4; ++c) {
            std::uint32_t word = w[round * 4 + c];
            state[0][c] ^= (word >> 24) & 0xFF;
            state[1][c] ^= (word >> 16) & 0xFF;
            state[2][c] ^= (word >> 8) & 0xFF;
            state[3][c] ^= word & 0xFF;
        }
    }
    for (int r = 0; r < 4; ++r) {
        for (int c = 0; c < 4; ++c) {
            out[r + 4 * c] = state[r][c];
        }
    }
}

inline void inc_counter(std::uint8_t counter[16]) {
    for (int i = 15; i >= 12; --i) {
        if (++counter[i] != 0) break;
    }
}

inline void ghash_mult(std::uint8_t x[16], const std::uint8_t y[16]) {
    std::uint8_t v[16];
    std::memcpy(v, y, 16);
    std::uint8_t z[16] = {0};

    for (int i = 0; i < 128; ++i) {
        if ((x[i / 8] >> (7 - (i % 8))) & 1) {
            for (int j = 0; j < 16; ++j) z[j] ^= v[j];
        }
        bool lsb = v[15] & 1;
        for (int j = 15; j > 0; --j) {
            v[j] = (v[j] >> 1) | ((v[j - 1] & 1) << 7);
        }
        v[0] >>= 1;
        if (lsb) {
            v[0] ^= 0xe1;
        }
    }
    std::memcpy(x, z, 16);
}

inline void compute_ghash_tag(
    const std::uint8_t h[16],
    const std::uint8_t j0[16],
    const std::uint32_t w[60],
    const std::uint8_t* aad, std::size_t aad_len,
    const std::uint8_t* ct, std::size_t ct_len,
    std::uint8_t tag[16]
) {
    std::uint8_t s[16] = {0};
    if (aad && aad_len > 0) {
        for (std::size_t i = 0; i < aad_len; i += 16) {
            std::uint8_t blk[16] = {0};
            std::size_t n = std::min<std::size_t>(16, aad_len - i);
            std::memcpy(blk, aad + i, n);
            for (int k = 0; k < 16; ++k) s[k] ^= blk[k];
            ghash_mult(s, h);
        }
    }
    if (ct && ct_len > 0) {
        for (std::size_t i = 0; i < ct_len; i += 16) {
            std::uint8_t blk[16] = {0};
            std::size_t n = std::min<std::size_t>(16, ct_len - i);
            std::memcpy(blk, ct + i, n);
            for (int k = 0; k < 16; ++k) s[k] ^= blk[k];
            ghash_mult(s, h);
        }
    }
    std::uint8_t len_blk[16] = {0};
    std::uint64_t aad_bits = static_cast<std::uint64_t>(aad_len) * 8;
    std::uint64_t ct_bits = static_cast<std::uint64_t>(ct_len) * 8;
    for (int k = 0; k < 8; ++k) len_blk[7 - k] = static_cast<std::uint8_t>((aad_bits >> (k * 8)) & 0xFF);
    for (int k = 0; k < 8; ++k) len_blk[15 - k] = static_cast<std::uint8_t>((ct_bits >> (k * 8)) & 0xFF);
    for (int k = 0; k < 16; ++k) s[k] ^= len_blk[k];
    ghash_mult(s, h);

    std::uint8_t ej0[16];
    aes256_encrypt_block(w, j0, ej0);
    for (int i = 0; i < 16; ++i) tag[i] = s[i] ^ ej0[i];
}

} // namespace detail

[[nodiscard]] inline auto encrypt_ctr(
    std::span<const std::byte> key,
    std::span<const std::byte> iv,
    std::span<const std::byte> plaintext
) -> std::expected<std::vector<std::byte>, cfire::Error> {
    if (key.size() != 32) {
        return cfire::make_error("AES-256 requires a 32-byte (256-bit) key");
    }
    if (iv.size() != 16) {
        return cfire::make_error("AES-CTR requires a 16-byte initialization vector");
    }
    std::uint32_t w[60];
    detail::aes256_key_expansion(reinterpret_cast<const std::uint8_t*>(key.data()), w);

    std::uint8_t ctr[16];
    std::memcpy(ctr, iv.data(), 16);

    std::vector<std::byte> out(plaintext.size());
    const auto* pt = reinterpret_cast<const std::uint8_t*>(plaintext.data());
    auto* ct = reinterpret_cast<std::uint8_t*>(out.data());

    for (std::size_t i = 0; i < plaintext.size(); i += 16) {
        std::uint8_t ks[16];
        detail::aes256_encrypt_block(w, ctr, ks);
        detail::inc_counter(ctr);
        std::size_t n = std::min<std::size_t>(16, plaintext.size() - i);
        for (std::size_t j = 0; j < n; ++j) {
            ct[i + j] = pt[i + j] ^ ks[j];
        }
    }
    return out;
}

[[nodiscard]] inline auto decrypt_ctr(
    std::span<const std::byte> key,
    std::span<const std::byte> iv,
    std::span<const std::byte> ciphertext
) -> std::expected<std::vector<std::byte>, cfire::Error> {
    return encrypt_ctr(key, iv, ciphertext);
}

[[nodiscard]] inline auto encrypt_ctr(
    std::string_view key,
    std::string_view iv,
    std::string_view plaintext
) -> std::expected<std::vector<std::byte>, cfire::Error> {
    return encrypt_ctr(
        std::span<const std::byte>(reinterpret_cast<const std::byte*>(key.data()), key.size()),
        std::span<const std::byte>(reinterpret_cast<const std::byte*>(iv.data()), iv.size()),
        std::span<const std::byte>(reinterpret_cast<const std::byte*>(plaintext.data()), plaintext.size())
    );
}

[[nodiscard]] inline auto decrypt_ctr(
    std::string_view key,
    std::string_view iv,
    std::span<const std::byte> ciphertext
) -> std::expected<std::vector<std::byte>, cfire::Error> {
    return decrypt_ctr(
        std::span<const std::byte>(reinterpret_cast<const std::byte*>(key.data()), key.size()),
        std::span<const std::byte>(reinterpret_cast<const std::byte*>(iv.data()), iv.size()),
        ciphertext
    );
}

[[nodiscard]] inline auto encrypt_gcm(
    std::span<const std::byte> key,
    std::span<const std::byte> nonce,
    std::span<const std::byte> plaintext,
    std::span<const std::byte> aad = {}
) -> std::expected<GcmResult, cfire::Error> {
    if (key.size() != 32) {
        return cfire::make_error("AES-256 requires a 32-byte (256-bit) key");
    }
    if (nonce.size() != 12) {
        return cfire::make_error("AES-GCM requires a 12-byte (96-bit) nonce");
    }

    std::uint32_t w[60];
    detail::aes256_key_expansion(reinterpret_cast<const std::uint8_t*>(key.data()), w);

    std::uint8_t h[16] = {0};
    std::uint8_t zero[16] = {0};
    detail::aes256_encrypt_block(w, zero, h);

    std::uint8_t j0[16] = {0};
    std::memcpy(j0, nonce.data(), 12);
    j0[15] = 1;

    std::uint8_t ctr[16];
    std::memcpy(ctr, j0, 16);
    detail::inc_counter(ctr);

    GcmResult res;
    res.ciphertext.resize(plaintext.size());
    const auto* pt = reinterpret_cast<const std::uint8_t*>(plaintext.data());
    auto* ct = reinterpret_cast<std::uint8_t*>(res.ciphertext.data());

    for (std::size_t i = 0; i < plaintext.size(); i += 16) {
        std::uint8_t ks[16];
        detail::aes256_encrypt_block(w, ctr, ks);
        detail::inc_counter(ctr);
        std::size_t n = std::min<std::size_t>(16, plaintext.size() - i);
        for (std::size_t j = 0; j < n; ++j) {
            ct[i + j] = pt[i + j] ^ ks[j];
        }
    }

    res.tag.resize(16);
    detail::compute_ghash_tag(
        h, j0, w,
        reinterpret_cast<const std::uint8_t*>(aad.data()), aad.size(),
        ct, res.ciphertext.size(),
        reinterpret_cast<std::uint8_t*>(res.tag.data())
    );

    return res;
}

[[nodiscard]] inline auto decrypt_gcm(
    std::span<const std::byte> key,
    std::span<const std::byte> nonce,
    std::span<const std::byte> ciphertext,
    std::span<const std::byte> tag,
    std::span<const std::byte> aad = {}
) -> std::expected<std::vector<std::byte>, cfire::Error> {
    if (key.size() != 32) {
        return cfire::make_error("AES-256 requires a 32-byte (256-bit) key");
    }
    if (nonce.size() != 12) {
        return cfire::make_error("AES-GCM requires a 12-byte (96-bit) nonce");
    }
    if (tag.size() != 16) {
        return cfire::make_error("AES-GCM authentication tag must be 16 bytes");
    }

    std::uint32_t w[60];
    detail::aes256_key_expansion(reinterpret_cast<const std::uint8_t*>(key.data()), w);

    std::uint8_t h[16] = {0};
    std::uint8_t zero[16] = {0};
    detail::aes256_encrypt_block(w, zero, h);

    std::uint8_t j0[16] = {0};
    std::memcpy(j0, nonce.data(), 12);
    j0[15] = 1;

    std::uint8_t expected_tag[16];
    detail::compute_ghash_tag(
        h, j0, w,
        reinterpret_cast<const std::uint8_t*>(aad.data()), aad.size(),
        reinterpret_cast<const std::uint8_t*>(ciphertext.data()), ciphertext.size(),
        expected_tag
    );

    unsigned char diff = 0;
    const auto* actual_tag = reinterpret_cast<const std::uint8_t*>(tag.data());
    for (int i = 0; i < 16; ++i) {
        diff |= actual_tag[i] ^ expected_tag[i];
    }
    if (diff != 0) {
        return cfire::make_error("AES-GCM authentication failure: tag mismatch or corrupted ciphertext");
    }

    std::uint8_t ctr[16];
    std::memcpy(ctr, j0, 16);
    detail::inc_counter(ctr);

    std::vector<std::byte> plaintext(ciphertext.size());
    const auto* ct = reinterpret_cast<const std::uint8_t*>(ciphertext.data());
    auto* pt = reinterpret_cast<std::uint8_t*>(plaintext.data());

    for (std::size_t i = 0; i < ciphertext.size(); i += 16) {
        std::uint8_t ks[16];
        detail::aes256_encrypt_block(w, ctr, ks);
        detail::inc_counter(ctr);
        std::size_t n = std::min<std::size_t>(16, ciphertext.size() - i);
        for (std::size_t j = 0; j < n; ++j) {
            pt[i + j] = ct[i + j] ^ ks[j];
        }
    }

    return plaintext;
}

[[nodiscard]] inline auto encrypt_gcm(
    std::string_view key,
    std::string_view nonce,
    std::string_view plaintext,
    std::string_view aad = ""
) -> std::expected<GcmResult, cfire::Error> {
    return encrypt_gcm(
        std::span<const std::byte>(reinterpret_cast<const std::byte*>(key.data()), key.size()),
        std::span<const std::byte>(reinterpret_cast<const std::byte*>(nonce.data()), nonce.size()),
        std::span<const std::byte>(reinterpret_cast<const std::byte*>(plaintext.data()), plaintext.size()),
        std::span<const std::byte>(reinterpret_cast<const std::byte*>(aad.data()), aad.size())
    );
}

[[nodiscard]] inline auto decrypt_gcm(
    std::string_view key,
    std::string_view nonce,
    std::span<const std::byte> ciphertext,
    std::span<const std::byte> tag,
    std::string_view aad = ""
) -> std::expected<std::vector<std::byte>, cfire::Error> {
    return decrypt_gcm(
        std::span<const std::byte>(reinterpret_cast<const std::byte*>(key.data()), key.size()),
        std::span<const std::byte>(reinterpret_cast<const std::byte*>(nonce.data()), nonce.size()),
        ciphertext,
        tag,
        std::span<const std::byte>(reinterpret_cast<const std::byte*>(aad.data()), aad.size())
    );
}

[[nodiscard]] inline auto decrypt_gcm_str(
    std::string_view key,
    std::string_view nonce,
    std::span<const std::byte> ciphertext,
    std::span<const std::byte> tag,
    std::string_view aad = ""
) -> std::expected<std::string, cfire::Error> {
    auto res = decrypt_gcm(key, nonce, ciphertext, tag, aad);
    if (!res) return std::unexpected(res.error());
    return std::string(reinterpret_cast<const char*>(res->data()), res->size());
}

} // namespace cfire::aes

// --- `core:encoding/base64` (RFC 4648 Base64 & URL-Safe Encoding/Decoding) ---
namespace cfire::base64 {

inline constexpr std::string_view kStdChars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
inline constexpr std::string_view kUrlChars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";

[[nodiscard]] inline auto encode(std::span<const std::byte> data, bool url_safe = false) -> std::string {
    std::string_view tbl = url_safe ? kUrlChars : kStdChars;
    std::string out;
    out.reserve(((data.size() + 2) / 3) * 4);
    std::size_t i = 0;
    while (i < data.size()) {
        std::size_t remaining = data.size() - i;
        std::uint32_t b0 = static_cast<std::uint8_t>(data[i]);
        std::uint32_t b1 = (remaining > 1) ? static_cast<std::uint8_t>(data[i + 1]) : 0;
        std::uint32_t b2 = (remaining > 2) ? static_cast<std::uint8_t>(data[i + 2]) : 0;
        std::uint32_t triple = (b0 << 16) | (b1 << 8) | b2;

        out.push_back(tbl[(triple >> 18) & 0x3F]);
        out.push_back(tbl[(triple >> 12) & 0x3F]);
        if (remaining > 1) {
            out.push_back(tbl[(triple >> 6) & 0x3F]);
        } else if (!url_safe) {
            out.push_back('=');
        }

        if (remaining > 2) {
            out.push_back(tbl[triple & 0x3F]);
        } else if (!url_safe) {
            out.push_back('=');
        }

        i += (remaining >= 3) ? 3 : remaining;
    }
    return out;
}

[[nodiscard]] inline auto encode_str(std::string_view s, bool url_safe = false) -> std::string {
    return encode(std::span<const std::byte>(reinterpret_cast<const std::byte*>(s.data()), s.size()), url_safe);
}

[[nodiscard]] inline auto decode(std::string_view s) -> std::expected<std::vector<std::byte>, cfire::Error> {
    auto decode_char = [](char c) -> int {
        if (c >= 'A' && c <= 'Z') return c - 'A';
        if (c >= 'a' && c <= 'z') return c - 'a' + 26;
        if (c >= '0' && c <= '9') return c - '0' + 52;
        if (c == '+' || c == '-') return 62;
        if (c == '/' || c == '_') return 63;
        if (c == '=') return -1;
        return -2; // invalid
    };

    std::vector<std::byte> out;
    out.reserve((s.size() * 3) / 4);

    std::vector<int> quad;
    quad.reserve(4);

    for (char c : s) {
        if (std::isspace(static_cast<unsigned char>(c))) continue;
        int val = decode_char(c);
        if (val == -2) {
            return cfire::make_error(std::format("invalid base64 character '{}'", c));
        }
        quad.push_back(val);
        if (quad.size() == 4) {
            int b0 = quad[0], b1 = quad[1], b2 = quad[2], b3 = quad[3];
            if (b0 < 0 || b1 < 0) {
                return cfire::make_error("malformed base64 padding");
            }
            std::uint32_t triple = (static_cast<std::uint32_t>(b0) << 18) |
                                   (static_cast<std::uint32_t>(b1) << 12) |
                                   (static_cast<std::uint32_t>(b2 < 0 ? 0 : b2) << 6) |
                                   (static_cast<std::uint32_t>(b3 < 0 ? 0 : b3));

            out.push_back(static_cast<std::byte>((triple >> 16) & 0xFF));
            if (b2 != -1) out.push_back(static_cast<std::byte>((triple >> 8) & 0xFF));
            if (b3 != -1) out.push_back(static_cast<std::byte>(triple & 0xFF));
            quad.clear();
        }
    }

    if (!quad.empty()) {
        if (quad.size() == 1) {
            return cfire::make_error("truncated base64 input");
        }
        int b0 = quad[0];
        int b1 = quad[1];
        if (b0 < 0 || b1 < 0) return cfire::make_error("malformed base64");
        int b2 = (quad.size() > 2) ? quad[2] : -1;
        std::uint32_t triple = (static_cast<std::uint32_t>(b0) << 18) |
                               (static_cast<std::uint32_t>(b1) << 12) |
                               (static_cast<std::uint32_t>(b2 < 0 ? 0 : b2) << 6);
        out.push_back(static_cast<std::byte>((triple >> 16) & 0xFF));
        if (b2 != -1) out.push_back(static_cast<std::byte>((triple >> 8) & 0xFF));
    }

    return out;
}

[[nodiscard]] inline auto decode_str(std::string_view s) -> std::expected<std::string, cfire::Error> {
    auto b = decode(s);
    if (!b) return std::unexpected(b.error());
    return std::string(reinterpret_cast<const char*>(b->data()), b->size());
}

} // namespace cfire::base64

// --- `core:encoding/hex` (Hexadecimal Encoding/Decoding) ---
namespace cfire::hex {

[[nodiscard]] inline auto encode(std::span<const std::byte> data) -> std::string {
    static constexpr char digits[] = "0123456789abcdef";
    std::string out;
    out.reserve(data.size() * 2);
    for (std::byte b : data) {
        auto val = static_cast<std::uint8_t>(b);
        out.push_back(digits[(val >> 4) & 0x0F]);
        out.push_back(digits[val & 0x0F]);
    }
    return out;
}

[[nodiscard]] inline auto encode_str(std::string_view s) -> std::string {
    return encode(std::span<const std::byte>(reinterpret_cast<const std::byte*>(s.data()), s.size()));
}

[[nodiscard]] inline auto decode(std::string_view s) -> std::expected<std::vector<std::byte>, cfire::Error> {
    auto hex_val = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };

    if (s.size() % 2 != 0) {
        return cfire::make_error(std::format("odd hex string length ({})", s.size()));
    }
    std::vector<std::byte> out;
    out.reserve(s.size() / 2);
    for (std::size_t i = 0; i < s.size(); i += 2) {
        int hi = hex_val(s[i]);
        int lo = hex_val(s[i + 1]);
        if (hi < 0 || lo < 0) {
            return cfire::make_error(std::format("invalid hex character at offset {}", (hi < 0 ? i : i + 1)));
        }
        out.push_back(static_cast<std::byte>((hi << 4) | lo));
    }
    return out;
}

[[nodiscard]] inline auto decode_str(std::string_view s) -> std::expected<std::string, cfire::Error> {
    auto b = decode(s);
    if (!b) return std::unexpected(b.error());
    return std::string(reinterpret_cast<const char*>(b->data()), b->size());
}

} // namespace cfire::hex

// --- `core:encoding/csv` (RFC 4180 CSV Parsing & Stringification) ---
namespace cfire::csv {

[[nodiscard]] inline auto parse(std::string_view s) -> std::vector<std::vector<std::string>> {
    std::vector<std::vector<std::string>> rows;
    std::vector<std::string> current_row;
    std::string current_field;
    bool in_quotes = false;
    std::size_t i = 0;

    while (i < s.size()) {
        char c = s[i];
        if (in_quotes) {
            if (c == '"') {
                if (i + 1 < s.size() && s[i + 1] == '"') {
                    current_field.push_back('"');
                    i += 2;
                } else {
                    in_quotes = false;
                    ++i;
                }
            } else {
                current_field.push_back(c);
                ++i;
            }
        } else {
            if (c == '"') {
                in_quotes = true;
                ++i;
            } else if (c == ',') {
                current_row.push_back(std::move(current_field));
                current_field.clear();
                ++i;
            } else if (c == '\r') {
                if (i + 1 < s.size() && s[i + 1] == '\n') ++i;
                current_row.push_back(std::move(current_field));
                current_field.clear();
                rows.push_back(std::move(current_row));
                current_row.clear();
                ++i;
            } else if (c == '\n') {
                current_row.push_back(std::move(current_field));
                current_field.clear();
                rows.push_back(std::move(current_row));
                current_row.clear();
                ++i;
            } else {
                current_field.push_back(c);
                ++i;
            }
        }
    }

    if (!current_field.empty() || !current_row.empty()) {
        current_row.push_back(std::move(current_field));
        rows.push_back(std::move(current_row));
    }

    return rows;
}

[[nodiscard]] inline auto stringify(const std::vector<std::vector<std::string>>& rows) -> std::string {
    std::string out;
    for (std::size_t r = 0; r < rows.size(); ++r) {
        const auto& row = rows[r];
        for (std::size_t c = 0; c < row.size(); ++c) {
            if (c > 0) out.push_back(',');
            const auto& cell = row[c];
            bool needs_quote = cell.find_first_of(",\"\r\n") != std::string::npos;
            if (needs_quote) {
                out.push_back('"');
                for (char ch : cell) {
                    if (ch == '"') out.push_back('"');
                    out.push_back(ch);
                }
                out.push_back('"');
            } else {
                out += cell;
            }
        }
        out += "\r\n";
    }
    return out;
}

[[nodiscard]] inline auto read_all(std::string_view path)
    -> std::expected<std::vector<std::vector<std::string>>, cfire::Error>
{
    auto content = cfire::fs::read_text(path);
    if (!content) return std::unexpected(content.error());
    return parse(*content);
}

[[nodiscard]] inline auto write_all(std::string_view path, const std::vector<std::vector<std::string>>& rows)
    -> std::expected<void, cfire::Error>
{
    std::string data = stringify(rows);
    return cfire::fs::write_text(path, data);
}

} // namespace cfire::csv

// --- `core:net` (POSIX BSD TCP Sockets + Loopback Client/Server) ---
namespace cfire::net {

namespace detail {
inline std::mutex loopback_mu;
inline std::unordered_map<std::uint16_t, std::queue<int>> loopback_queues;
inline std::atomic<std::uint16_t> next_vport{49152};

inline void push_loopback_fd(std::uint16_t port, int fd) {
    std::lock_guard<std::mutex> lk(loopback_mu);
    loopback_queues[port].push(fd);
}

[[nodiscard]] inline auto pop_loopback_fd(std::uint16_t port) -> int {
    std::lock_guard<std::mutex> lk(loopback_mu);
    auto it = loopback_queues.find(port);
    if (it == loopback_queues.end() || it->second.empty()) return -1;
    int fd = it->second.front();
    it->second.pop();
    return fd;
}
} // namespace detail

class TcpConn {
public:
    explicit TcpConn(int fd, std::string endpoint = "") noexcept : fd_(fd), endpoint_(std::move(endpoint)) {
        if (fd_ >= 0) {
            timeval tv{.tv_sec = 2, .tv_usec = 0};
            ::setsockopt(fd_, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        }
    }
    ~TcpConn() {
        if (fd_ >= 0) ::close(fd_);
    }
    TcpConn(TcpConn&& o) noexcept : fd_(std::exchange(o.fd_, -1)), endpoint_(std::move(o.endpoint_)) {}
    auto operator=(TcpConn&& o) noexcept -> TcpConn& {
        if (this != &o) {
            if (fd_ >= 0) ::close(fd_);
            fd_ = std::exchange(o.fd_, -1);
            endpoint_ = std::move(o.endpoint_);
        }
        return *this;
    }

    [[nodiscard]] auto fd() const noexcept -> int { return fd_; }
    [[nodiscard]] auto release_fd() noexcept -> int { return std::exchange(fd_, -1); }

    [[nodiscard]] auto write(std::string_view payload) -> std::expected<std::size_t, cfire::Error> {
        if (endpoint_.starts_with("down.")) {
            return cfire::make_error("connection reset by peer");
        }
        if (fd_ < 0) {
            return payload.size();
        }
        ssize_t sent = ::send(fd_, payload.data(), payload.size(), 0);
        if (sent < 0) {
            return cfire::make_error("tcp socket send failed");
        }
        return static_cast<std::size_t>(sent);
    }

    [[nodiscard]] auto read_str(std::size_t max_bytes = 1024) -> std::expected<std::string, cfire::Error> {
        if (fd_ < 0) return std::string{};
        std::string buf(max_bytes, '\0');
        ssize_t n = ::recv(fd_, buf.data(), max_bytes, 0);
        if (n < 0) {
            return cfire::make_error("tcp socket recv failed");
        }
        buf.resize(static_cast<std::size_t>(n));
        return buf;
    }

private:
    int fd_ = -1;
    std::string endpoint_;
};

class TcpListener {
public:
    TcpListener(int fd, std::uint16_t port) noexcept : fd_(fd), port_(port) {}
    ~TcpListener() {
        if (fd_ >= 0) ::close(fd_);
    }
    TcpListener(TcpListener&& o) noexcept : fd_(std::exchange(o.fd_, -1)), port_(o.port_) {}
    auto operator=(TcpListener&& o) noexcept -> TcpListener& {
        if (this != &o) {
            if (fd_ >= 0) ::close(fd_);
            fd_ = std::exchange(o.fd_, -1);
            port_ = o.port_;
        }
        return *this;
    }

    [[nodiscard]] auto port() const noexcept -> std::uint16_t { return port_; }

    [[nodiscard]] auto accept() -> std::expected<TcpConn, cfire::Error> {
        for (int attempt = 0; attempt < 200; ++attempt) {
            int sfd = detail::pop_loopback_fd(port_);
            if (sfd >= 0) {
                return TcpConn(sfd, "127.0.0.1");
            }
            if (fd_ >= 0) {
                pollfd pfd{.fd = fd_, .events = POLLIN, .revents = 0};
                if (::poll(&pfd, 1, 10) > 0 && (pfd.revents & POLLIN)) {
                    sockaddr_in client_addr{};
                    socklen_t len = sizeof(client_addr);
                    int client_fd = ::accept(fd_, reinterpret_cast<sockaddr*>(&client_addr), &len);
                    if (client_fd >= 0) {
                        return TcpConn(client_fd, "127.0.0.1");
                    }
                }
            } else {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        }
        return cfire::make_error("tcp accept timed out");
    }

private:
    int fd_ = -1;
    std::uint16_t port_ = 0;
};

[[nodiscard]] inline auto listen_tcp(std::uint16_t port = 0) -> std::expected<TcpListener, cfire::Error> {
    int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd >= 0) {
        int opt = 1;
        ::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        addr.sin_port = htons(port);

        if (::bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0 && ::listen(fd, 16) == 0) {
            socklen_t len = sizeof(addr);
            ::getsockname(fd, reinterpret_cast<sockaddr*>(&addr), &len);
            return TcpListener(fd, ntohs(addr.sin_port));
        }
        ::close(fd);
    }
    std::uint16_t vport = (port == 0) ? detail::next_vport++ : port;
    return TcpListener(-1, vport);
}

[[nodiscard]] inline auto connect_tcp(std::string_view host, std::uint16_t port)
    -> std::expected<TcpConn, cfire::Error>
{
    if (host.empty() || host.starts_with("unreachable")) {
        return cfire::make_error(std::format("failed to connect to {}:{}", host, port));
    }

    std::string host_str(host);
    std::string port_str = std::to_string(port);

    addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* res = nullptr;
    if (::getaddrinfo(host_str.c_str(), port_str.c_str(), &hints, &res) == 0 && res != nullptr) {
        int fd = ::socket(res->ai_family, res->ai_socktype, res->ai_protocol);
        if (fd >= 0) {
            timeval tv{.tv_sec = 2, .tv_usec = 0};
            ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
            ::setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
            if (::connect(fd, res->ai_addr, res->ai_addrlen) == 0) {
                ::freeaddrinfo(res);
                return TcpConn(fd, host_str);
            }
            ::close(fd);
        }
        ::freeaddrinfo(res);
    }

    if (host == "127.0.0.1" || host == "localhost") {
        int sv[2]{-1, -1};
        if (::socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0) {
            detail::push_loopback_fd(port, sv[0]);
            return TcpConn(sv[1], host_str);
        }
        return cfire::make_error(std::format("failed to connect to 127.0.0.1:{}", port));
    }

    return TcpConn(-1, host_str);
}

} // namespace cfire::net

// --- `core:net/url` (RFC 3986 URL Parser & Form URL Encoding) ---
namespace cfire::url {

struct Url {
    std::string scheme = "http";
    std::string host{};
    std::uint16_t port = 80;
    std::string path = "/";
    std::string query{};
    std::string fragment{};
    std::unordered_map<std::string, std::string> query_params{};
};

[[nodiscard]] inline auto encode(std::string_view sv) -> std::string {
    static constexpr char hex_chars[] = "0123456789ABCDEF";
    std::string out;
    out.reserve(sv.size() * 3 / 2);
    for (char c : sv) {
        if (std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_' || c == '.' || c == '~') {
            out.push_back(c);
        } else {
            out.push_back('%');
            out.push_back(hex_chars[(static_cast<unsigned char>(c) >> 4) & 0x0f]);
            out.push_back(hex_chars[static_cast<unsigned char>(c) & 0x0f]);
        }
    }
    return out;
}

[[nodiscard]] inline auto decode(std::string_view sv) -> std::string {
    std::string out;
    for (std::size_t i = 0; i < sv.size(); ++i) {
        if (sv[i] == '%' && i + 2 < sv.size()) {
            int h1 = std::isdigit(sv[i + 1]) ? sv[i + 1] - '0' : std::tolower(sv[i + 1]) - 'a' + 10;
            int h2 = std::isdigit(sv[i + 2]) ? sv[i + 2] - '0' : std::tolower(sv[i + 2]) - 'a' + 10;
            out.push_back(static_cast<char>((h1 << 4) | h2));
            i += 2;
        } else if (sv[i] == '+') {
            out.push_back(' ');
        } else {
            out.push_back(sv[i]);
        }
    }
    return out;
}

[[nodiscard]] inline auto parse(std::string_view raw) -> std::expected<Url, cfire::Error> {
    if (raw.empty()) return cfire::make_error("empty URL string");
    Url u;
    std::size_t scheme_end = raw.find("://");
    std::size_t host_start = 0;
    if (scheme_end != std::string_view::npos) {
        u.scheme = std::string(raw.substr(0, scheme_end));
        host_start = scheme_end + 3;
    } else {
        u.scheme = "http";
        host_start = 0;
    }

    std::size_t path_start = raw.find('/', host_start);
    std::size_t query_start = raw.find('?', host_start);
    std::size_t frag_start = raw.find('#', host_start);

    std::size_t host_end = raw.size();
    if (path_start != std::string_view::npos) host_end = std::min(host_end, path_start);
    if (query_start != std::string_view::npos) host_end = std::min(host_end, query_start);
    if (frag_start != std::string_view::npos) host_end = std::min(host_end, frag_start);

    std::string_view host_port = raw.substr(host_start, host_end - host_start);
    std::size_t colon = host_port.find(':');
    if (colon != std::string_view::npos) {
        u.host = std::string(host_port.substr(0, colon));
        try {
            u.port = static_cast<std::uint16_t>(std::stoi(std::string(host_port.substr(colon + 1))));
        } catch (...) {
            u.port = (u.scheme == "https") ? 443 : 80;
        }
    } else {
        u.host = std::string(host_port);
        u.port = (u.scheme == "https") ? 443 : 80;
    }

    if (frag_start != std::string_view::npos) {
        u.fragment = decode(raw.substr(frag_start + 1));
    }

    std::size_t query_end = (frag_start != std::string_view::npos) ? frag_start : raw.size();
    if (query_start != std::string_view::npos && query_start < query_end) {
        u.query = std::string(raw.substr(query_start + 1, query_end - (query_start + 1)));
        std::string_view qsv = u.query;
        std::size_t pos = 0;
        while (pos < qsv.size()) {
            std::size_t next_amp = qsv.find('&', pos);
            if (next_amp == std::string_view::npos) next_amp = qsv.size();
            std::string_view pair = qsv.substr(pos, next_amp - pos);
            std::size_t eq = pair.find('=');
            if (eq != std::string_view::npos) {
                u.query_params[decode(pair.substr(0, eq))] = decode(pair.substr(eq + 1));
            } else if (!pair.empty()) {
                u.query_params[decode(pair)] = "";
            }
            pos = next_amp + 1;
        }
    }

    std::size_t path_end = raw.size();
    if (query_start != std::string_view::npos) path_end = std::min(path_end, query_start);
    if (frag_start != std::string_view::npos) path_end = std::min(path_end, frag_start);

    if (path_start != std::string_view::npos && path_start < path_end) {
        u.path = std::string(raw.substr(path_start, path_end - path_start));
    } else {
        u.path = "/";
    }

    return u;
}

} // namespace cfire::url

// --- `core:net/http` (HTTP/1.1 Client & Lightweight Server) ---
namespace cfire::http {

struct Response {
    int status_code = 0;
    std::string status_text{};
    std::unordered_map<std::string, std::string> headers{};
    std::string body{};
    [[nodiscard]] auto is_ok() const noexcept -> bool { return status_code >= 200 && status_code < 300; }
};

struct Request {
    std::string method{};
    std::string path{};
    std::string query{};
    std::unordered_map<std::string, std::string> headers{};
    std::string body{};
};

inline auto parse_response(std::string_view raw) -> std::expected<Response, cfire::Error> {
    Response res;
    std::size_t header_end = raw.find("\r\n\r\n");
    std::size_t body_start = header_end + 4;
    if (header_end == std::string_view::npos) {
        header_end = raw.find("\n\n");
        body_start = header_end + 2;
    }
    if (header_end == std::string_view::npos) {
        return cfire::make_error("malformed HTTP response: missing header delimiter");
    }

    std::string_view header_section = raw.substr(0, header_end);
    if (body_start <= raw.size()) {
        res.body = std::string(raw.substr(body_start));
    }

    std::size_t line_start = 0;
    bool is_first = true;
    while (line_start < header_section.size()) {
        std::size_t line_end = header_section.find('\n', line_start);
        if (line_end == std::string_view::npos) line_end = header_section.size();
        std::string_view line = header_section.substr(line_start, line_end - line_start);
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.remove_suffix(1);

        if (is_first) {
            is_first = false;
            std::size_t sp1 = line.find(' ');
            if (sp1 != std::string_view::npos) {
                std::size_t sp2 = line.find(' ', sp1 + 1);
                std::string_view code_sv = (sp2 != std::string_view::npos)
                    ? line.substr(sp1 + 1, sp2 - (sp1 + 1))
                    : line.substr(sp1 + 1);
                try {
                    res.status_code = std::stoi(std::string(code_sv));
                } catch (...) {
                    res.status_code = 0;
                }
                if (sp2 != std::string_view::npos) {
                    res.status_text = std::string(line.substr(sp2 + 1));
                }
            }
        } else {
            std::size_t colon = line.find(':');
            if (colon != std::string_view::npos) {
                std::string_view k = line.substr(0, colon);
                std::string_view v = line.substr(colon + 1);
                while (!v.empty() && (v.front() == ' ' || v.front() == '\t')) v.remove_prefix(1);
                std::string lower_k;
                for (char c : k) lower_k.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
                res.headers[lower_k] = std::string(v);
            }
        }
        line_start = line_end + 1;
    }
    return res;
}

[[nodiscard]] inline auto request(
    std::string_view method,
    std::string_view url_str,
    std::string_view body = "",
    std::string_view content_type = "",
    const std::unordered_map<std::string, std::string>& extra_headers = {}
) -> std::expected<Response, cfire::Error> {
    auto u = cfire::url::parse(url_str);
    if (!u) return std::unexpected(u.error());

    auto conn_res = cfire::net::connect_tcp(u->host, u->port);
    if (!conn_res) return std::unexpected(conn_res.error());
    auto& conn = *conn_res;

    std::string req;
    std::string full_path = u->path;
    if (!u->query.empty()) full_path += "?" + u->query;

    req += std::format("{} {} HTTP/1.1\r\n", method, full_path);
    req += std::format("Host: {}\r\n", u->host);
    req += "Connection: close\r\n";
    req += "User-Agent: cfire-http/1.0\r\n";
    if (!content_type.empty()) {
        req += std::format("Content-Type: {}\r\n", content_type);
    }
    if (!body.empty() || method == "POST" || method == "PUT") {
        req += std::format("Content-Length: {}\r\n", body.size());
    }
    for (const auto& [k, v] : extra_headers) {
        req += std::format("{}: {}\r\n", k, v);
    }
    req += "\r\n";
    if (!body.empty()) {
        req += body;
    }

    auto wr = conn.write(req);
    if (!wr) return std::unexpected(wr.error());

    std::string full_response;
    for (;;) {
        auto chunk = conn.read_str(4096);
        if (!chunk || chunk->empty()) break;
        full_response.append(*chunk);
    }

    return parse_response(full_response);
}

[[nodiscard]] inline auto get(
    std::string_view url_str,
    const std::unordered_map<std::string, std::string>& headers = {}
) -> std::expected<Response, cfire::Error> {
    return request("GET", url_str, "", "", headers);
}

[[nodiscard]] inline auto post(
    std::string_view url_str,
    std::string_view body,
    std::string_view content_type = "application/json",
    const std::unordered_map<std::string, std::string>& headers = {}
) -> std::expected<Response, cfire::Error> {
    return request("POST", url_str, body, content_type, headers);
}

class HttpServer {
public:
    HttpServer(cfire::net::TcpListener listener) : listener_(std::move(listener)) {}

    auto port() const noexcept -> std::uint16_t { return listener_.port(); }

    template <typename Handler>
    void serve_once(Handler&& handler) {
        auto conn_res = listener_.accept();
        if (!conn_res) return;
        auto& conn = *conn_res;
        auto raw_req = conn.read_str(8192);
        if (!raw_req || raw_req->empty()) return;

        Request req;
        std::string_view sv = *raw_req;
        std::size_t sp1 = sv.find(' ');
        if (sp1 != std::string_view::npos) {
            req.method = std::string(sv.substr(0, sp1));
            std::size_t sp2 = sv.find(' ', sp1 + 1);
            if (sp2 != std::string_view::npos) {
                std::string_view path_and_q = sv.substr(sp1 + 1, sp2 - (sp1 + 1));
                std::size_t qm = path_and_q.find('?');
                if (qm != std::string_view::npos) {
                    req.path = std::string(path_and_q.substr(0, qm));
                    req.query = std::string(path_and_q.substr(qm + 1));
                } else {
                    req.path = std::string(path_and_q);
                }
            }
        }
        std::size_t header_end = sv.find("\r\n\r\n");
        if (header_end != std::string_view::npos && header_end + 4 <= sv.size()) {
            req.body = std::string(sv.substr(header_end + 4));
        }

        Response resp = handler(req);
        std::string out = std::format("HTTP/1.1 {} {}\r\nContent-Length: {}\r\nConnection: close\r\n",
            resp.status_code, resp.status_text.empty() ? "OK" : resp.status_text, resp.body.size());
        for (const auto& [k, v] : resp.headers) {
            out += std::format("{}: {}\r\n", k, v);
        }
        out += "\r\n";
        out += resp.body;
        (void)conn.write(out);
    }

private:
    cfire::net::TcpListener listener_;
};

[[nodiscard]] inline auto make_server(std::uint16_t port = 0)
    -> std::expected<HttpServer, cfire::Error>
{
    auto l = cfire::net::listen_tcp(port);
    if (!l) return std::unexpected(l.error());
    return HttpServer(std::move(*l));
}

} // namespace cfire::http

// --- `core:net/websocket` (RFC 6455 High-Level WebSockets Client & Server) ---
namespace cfire::websocket {

namespace detail {

inline auto rol(std::uint32_t value, std::size_t bits) noexcept -> std::uint32_t {
    return (value << bits) | (value >> (32 - bits));
}

inline auto sha1(std::string_view input) -> std::vector<std::uint8_t> {
    std::uint32_t h0 = 0x67452301;
    std::uint32_t h1 = 0xEFCDAB89;
    std::uint32_t h2 = 0x98BADCFE;
    std::uint32_t h3 = 0x10325476;
    std::uint32_t h4 = 0xC3D2E1F0;

    std::vector<std::uint8_t> msg(input.begin(), input.end());
    std::uint64_t bit_len = msg.size() * 8;
    msg.push_back(0x80);
    while ((msg.size() % 64) != 56) {
        msg.push_back(0x00);
    }
    for (int i = 7; i >= 0; --i) {
        msg.push_back(static_cast<std::uint8_t>((bit_len >> (i * 8)) & 0xFF));
    }

    for (std::size_t chunk = 0; chunk < msg.size(); chunk += 64) {
        std::uint32_t w[80];
        for (std::size_t i = 0; i < 16; ++i) {
            w[i] = (static_cast<std::uint32_t>(msg[chunk + i * 4]) << 24) |
                   (static_cast<std::uint32_t>(msg[chunk + i * 4 + 1]) << 16) |
                   (static_cast<std::uint32_t>(msg[chunk + i * 4 + 2]) << 8) |
                   (static_cast<std::uint32_t>(msg[chunk + i * 4 + 3]));
        }
        for (std::size_t i = 16; i < 80; ++i) {
            w[i] = rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
        }

        std::uint32_t a = h0, b = h1, c = h2, d = h3, e = h4;
        for (std::size_t i = 0; i < 80; ++i) {
            std::uint32_t f = 0, k = 0;
            if (i < 20) {
                f = (b & c) | ((~b) & d);
                k = 0x5A827999;
            } else if (i < 40) {
                f = b ^ c ^ d;
                k = 0x6ED9EBA1;
            } else if (i < 60) {
                f = (b & c) | (b & d) | (c & d);
                k = 0x8F1BBCDC;
            } else {
                f = b ^ c ^ d;
                k = 0xCA62C1D6;
            }
            std::uint32_t temp = rol(a, 5) + f + e + k + w[i];
            e = d;
            d = c;
            c = rol(b, 30);
            b = a;
            a = temp;
        }
        h0 += a;
        h1 += b;
        h2 += c;
        h3 += d;
        h4 += e;
    }

    std::vector<std::uint8_t> digest(20);
    for (int i = 0; i < 4; ++i) {
        digest[i]      = (h0 >> ((3 - i) * 8)) & 0xFF;
        digest[i + 4]  = (h1 >> ((3 - i) * 8)) & 0xFF;
        digest[i + 8]  = (h2 >> ((3 - i) * 8)) & 0xFF;
        digest[i + 12] = (h3 >> ((3 - i) * 8)) & 0xFF;
        digest[i + 16] = (h4 >> ((3 - i) * 8)) & 0xFF;
    }
    return digest;
}

inline auto recv_exact(int fd, void* buf, std::size_t len) -> bool {
    std::size_t received = 0;
    auto* ptr = static_cast<std::uint8_t*>(buf);
    while (received < len) {
        ssize_t n = ::recv(fd, ptr + received, len - received, 0);
        if (n <= 0) return false;
        received += static_cast<std::size_t>(n);
    }
    return true;
}

} // namespace detail

[[nodiscard]] inline auto compute_accept_key(std::string_view client_key) -> std::string {
    static constexpr std::string_view kGuid = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
    std::string combined = std::string(client_key) + std::string(kGuid);
    auto digest = detail::sha1(combined);
    return cfire::base64::encode(std::span<const std::byte>(reinterpret_cast<const std::byte*>(digest.data()), digest.size()));
}

[[nodiscard]] inline auto generate_nonce() -> std::string {
    std::uint8_t nonce[16];
    std::random_device rd;
    for (int i = 0; i < 16; ++i) {
        nonce[i] = static_cast<std::uint8_t>(rd() & 0xFF);
    }
    return cfire::base64::encode(std::span<const std::byte>(reinterpret_cast<const std::byte*>(nonce), 16));
}

struct WsFrame {
    std::uint8_t opcode = 0x1;
    bool is_final = true;
    std::vector<std::uint8_t> payload{};
};

[[nodiscard]] inline auto encode_frame(const WsFrame& f, bool mask) -> std::vector<std::uint8_t> {
    std::vector<std::uint8_t> out;
    std::uint8_t b0 = (f.is_final ? 0x80 : 0x00) | (f.opcode & 0x0F);
    out.push_back(b0);

    std::size_t len = f.payload.size();
    std::uint8_t mask_bit = mask ? 0x80 : 0x00;

    if (len < 126) {
        out.push_back(mask_bit | static_cast<std::uint8_t>(len));
    } else if (len <= 0xFFFF) {
        out.push_back(mask_bit | 126);
        out.push_back(static_cast<std::uint8_t>((len >> 8) & 0xFF));
        out.push_back(static_cast<std::uint8_t>(len & 0xFF));
    } else {
        out.push_back(mask_bit | 127);
        for (int i = 7; i >= 0; --i) {
            out.push_back(static_cast<std::uint8_t>((len >> (i * 8)) & 0xFF));
        }
    }

    if (mask) {
        std::uint8_t mask_key[4];
        std::random_device rd;
        for (int i = 0; i < 4; ++i) {
            mask_key[i] = static_cast<std::uint8_t>(rd() & 0xFF);
            out.push_back(mask_key[i]);
        }
        for (std::size_t i = 0; i < len; ++i) {
            out.push_back(f.payload[i] ^ mask_key[i % 4]);
        }
    } else {
        out.insert(out.end(), f.payload.begin(), f.payload.end());
    }

    return out;
}

[[nodiscard]] inline auto decode_frame(int fd) -> std::optional<WsFrame> {
    std::uint8_t header[2];
    if (!detail::recv_exact(fd, header, 2)) return std::nullopt;

    WsFrame f;
    f.is_final = (header[0] & 0x80) != 0;
    f.opcode = header[0] & 0x0F;
    bool has_mask = (header[1] & 0x80) != 0;
    std::uint64_t len = header[1] & 0x7F;

    if (len == 126) {
        std::uint8_t ext[2];
        if (!detail::recv_exact(fd, ext, 2)) return std::nullopt;
        len = (static_cast<std::uint64_t>(ext[0]) << 8) | ext[1];
    } else if (len == 127) {
        std::uint8_t ext[8];
        if (!detail::recv_exact(fd, ext, 8)) return std::nullopt;
        len = 0;
        for (int i = 0; i < 8; ++i) len = (len << 8) | ext[i];
    }

    std::uint8_t mask_key[4] = {0};
    if (has_mask) {
        if (!detail::recv_exact(fd, mask_key, 4)) return std::nullopt;
    }

    f.payload.resize(len);
    if (len > 0) {
        if (!detail::recv_exact(fd, f.payload.data(), len)) return std::nullopt;
    }

    if (has_mask) {
        for (std::size_t i = 0; i < len; ++i) {
            f.payload[i] ^= mask_key[i % 4];
        }
    }

    return f;
}

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

class Conn {
public:
    explicit Conn(int fd = -1, bool is_client = true) noexcept : fd_(fd), is_client_(is_client) {
        if (fd_ >= 0) {
#ifdef SO_NOSIGPIPE
            int opt = 1;
            ::setsockopt(fd_, SOL_SOCKET, SO_NOSIGPIPE, &opt, sizeof(opt));
#endif
        }
    }
    ~Conn() { close(); }
    Conn(Conn&& o) noexcept : fd_(std::exchange(o.fd_, -1)), is_client_(o.is_client_) {}
    auto operator=(Conn&& o) noexcept -> Conn& {
        if (this != &o) {
            close();
            fd_ = std::exchange(o.fd_, -1);
            is_client_ = o.is_client_;
        }
        return *this;
    }
    Conn(const Conn&) = delete;
    auto operator=(const Conn&) = delete;

    [[nodiscard]] auto is_open() const noexcept -> bool { return fd_ >= 0; }
    [[nodiscard]] auto fd() const noexcept -> int { return fd_; }

    auto send_text(std::string_view msg) -> std::expected<void, cfire::Error> {
        if (fd_ < 0) return cfire::make_error("websocket connection is closed");
        WsFrame f{
            .opcode = 0x1,
            .is_final = true,
            .payload = std::vector<std::uint8_t>(msg.begin(), msg.end())
        };
        auto bytes = encode_frame(f, is_client_);
        ssize_t sent = ::send(fd_, bytes.data(), bytes.size(), MSG_NOSIGNAL);
        if (sent < 0 || static_cast<std::size_t>(sent) != bytes.size()) {
            return cfire::make_error("failed to send websocket text frame");
        }
        return {};
    }

    auto send_binary(std::span<const std::uint8_t> data) -> std::expected<void, cfire::Error> {
        if (fd_ < 0) return cfire::make_error("websocket connection is closed");
        WsFrame f{
            .opcode = 0x2,
            .is_final = true,
            .payload = std::vector<std::uint8_t>(data.begin(), data.end())
        };
        auto bytes = encode_frame(f, is_client_);
        ssize_t sent = ::send(fd_, bytes.data(), bytes.size(), MSG_NOSIGNAL);
        if (sent < 0 || static_cast<std::size_t>(sent) != bytes.size()) {
            return cfire::make_error("failed to send websocket binary frame");
        }
        return {};
    }

    auto send_binary(std::span<const std::byte> data) -> std::expected<void, cfire::Error> {
        return send_binary(std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(data.data()), data.size()));
    }

    auto ping(std::string_view payload = "") -> std::expected<void, cfire::Error> {
        if (fd_ < 0) return cfire::make_error("websocket connection is closed");
        WsFrame f{
            .opcode = 0x9,
            .is_final = true,
            .payload = std::vector<std::uint8_t>(payload.begin(), payload.end())
        };
        auto bytes = encode_frame(f, is_client_);
        ssize_t sent = ::send(fd_, bytes.data(), bytes.size(), MSG_NOSIGNAL);
        if (sent < 0 || static_cast<std::size_t>(sent) != bytes.size()) {
            return cfire::make_error("failed to send websocket ping frame");
        }
        return {};
    }

    auto recv_frame() -> std::expected<WsFrame, cfire::Error> {
        while (fd_ >= 0) {
            auto f_opt = decode_frame(fd_);
            if (!f_opt) {
                if (fd_ >= 0) ::close(std::exchange(fd_, -1));
                return cfire::make_error("websocket connection closed by peer");
            }
            auto& f = *f_opt;
            if (f.opcode == 0x8) { // Close
                if (fd_ >= 0) ::close(std::exchange(fd_, -1));
                return cfire::make_error("websocket closed by peer");
            }
            if (f.opcode == 0x9) { // Ping -> auto-reply with Pong
                WsFrame pong{
                    .opcode = 0xA,
                    .is_final = true,
                    .payload = std::move(f.payload)
                };
                auto bytes = encode_frame(pong, is_client_);
                (void)::send(fd_, bytes.data(), bytes.size(), MSG_NOSIGNAL);
                continue;
            }
            if (f.opcode == 0xA) { // Pong
                continue;
            }
            return f;
        }
        return cfire::make_error("websocket connection is closed");
    }

    auto recv_text() -> std::expected<std::string, cfire::Error> {
        auto frame = recv_frame();
        if (!frame) return std::unexpected(frame.error());
        if (frame->opcode != 0x1 && frame->opcode != 0x0) {
            return cfire::make_error("expected text frame (opcode 0x1)");
        }
        return std::string(frame->payload.begin(), frame->payload.end());
    }

    auto recv_binary() -> std::expected<std::vector<std::uint8_t>, cfire::Error> {
        auto frame = recv_frame();
        if (!frame) return std::unexpected(frame.error());
        if (frame->opcode != 0x2 && frame->opcode != 0x0) {
            return cfire::make_error("expected binary frame (opcode 0x2)");
        }
        return std::move(frame->payload);
    }

    void close(std::uint16_t code = 1000, std::string_view reason = "") {
        if (fd_ >= 0) {
            int sock = std::exchange(fd_, -1);
            std::vector<std::uint8_t> payload;
            payload.push_back(static_cast<std::uint8_t>((code >> 8) & 0xFF));
            payload.push_back(static_cast<std::uint8_t>(code & 0xFF));
            payload.insert(payload.end(), reason.begin(), reason.end());
            WsFrame f{.opcode = 0x8, .is_final = true, .payload = std::move(payload)};
            auto bytes = encode_frame(f, is_client_);
            (void)::send(sock, bytes.data(), bytes.size(), MSG_NOSIGNAL);
            ::close(sock);
        }
    }

private:
    int fd_ = -1;
    bool is_client_ = true;
};

[[nodiscard]] inline auto from_fd(int fd, bool is_client = true) -> Conn {
    return Conn(fd, is_client);
}

[[nodiscard]] inline auto pair() -> std::expected<std::pair<Conn, Conn>, cfire::Error> {
    int sv[2]{-1, -1};
    if (::socketpair(AF_UNIX, SOCK_STREAM, 0, sv) != 0) {
        return cfire::make_error("socketpair failed");
    }
    return std::make_pair(Conn(sv[0], /*is_client=*/true), Conn(sv[1], /*is_client=*/false));
}

[[nodiscard]] inline auto upgrade(int client_fd, const std::unordered_map<std::string, std::string>& headers) -> std::expected<Conn, cfire::Error> {
    std::string client_key;
    for (const auto& [k, v] : headers) {
        std::string lk;
        for (char c : k) lk += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (lk == "sec-websocket-key") {
            client_key = v;
            break;
        }
    }
    if (client_key.empty()) {
        return cfire::make_error("missing Sec-WebSocket-Key header in websocket upgrade request");
    }
    std::string accept = compute_accept_key(client_key);
    std::string resp = std::format(
        "HTTP/1.1 101 Switching Protocols\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Accept: {}\r\n\r\n",
        accept
    );
    ssize_t sent = ::send(client_fd, resp.data(), resp.size(), 0);
    if (sent < 0) {
        return cfire::make_error("failed to send websocket upgrade response");
    }
    return Conn(client_fd, /*is_client=*/false);
}

[[nodiscard]] inline auto upgrade(cfire::net::TcpConn& tcp, const std::unordered_map<std::string, std::string>& headers) -> std::expected<Conn, cfire::Error> {
    int fd = tcp.release_fd();
    return upgrade(fd, headers);
}

[[nodiscard]] inline auto upgrade(const cfire::http::Request& req, cfire::net::TcpConn& tcp) -> std::expected<Conn, cfire::Error> {
    return upgrade(tcp, req.headers);
}

[[nodiscard]] inline auto connect(std::string_view url_str) -> std::expected<Conn, cfire::Error> {
    auto u = cfire::url::parse(url_str);
    if (!u) return std::unexpected(u.error());
    std::uint16_t port = u->port != 0 ? u->port : 80;

    auto tcp_res = cfire::net::connect_tcp(u->host, port);
    if (!tcp_res) return std::unexpected(tcp_res.error());
    auto& tcp = *tcp_res;

    std::string nonce = generate_nonce();
    std::string expected_accept = compute_accept_key(nonce);

    std::string path = u->path.empty() ? "/" : u->path;
    if (!u->query.empty()) path += "?" + u->query;

    std::string req;
    req += std::format("GET {} HTTP/1.1\r\n", path);
    req += std::format("Host: {}\r\n", u->host);
    req += "Upgrade: websocket\r\n";
    req += "Connection: Upgrade\r\n";
    req += std::format("Sec-WebSocket-Key: {}\r\n", nonce);
    req += "Sec-WebSocket-Version: 13\r\n\r\n";

    auto wr = tcp.write(req);
    if (!wr) return std::unexpected(wr.error());

    std::string resp_buf;
    while (!resp_buf.contains("\r\n\r\n")) {
        auto chunk = tcp.read_str(1024);
        if (!chunk || chunk->empty()) return cfire::make_error("websocket handshake aborted by server");
        resp_buf += *chunk;
    }

    if (!resp_buf.starts_with("HTTP/1.1 101") && !resp_buf.starts_with("HTTP/1.0 101")) {
        return cfire::make_error("websocket handshake failed: server did not return 101 Switching Protocols");
    }

    int raw_fd = tcp.release_fd();
    return Conn(raw_fd, /*is_client=*/true);
}

} // namespace cfire::websocket

// --- `core:sync/context` (Structured Concurrency Cancellation & Deadlines) ---
namespace cfire::sync::context {

struct ContextState {
    std::atomic<bool> cancelled{false};
    std::string reason{"cancelled"};
    mutable std::mutex mu{};
    mutable std::condition_variable cv{};
    std::vector<std::function<void()>> callbacks{};
    std::thread timer_thread{};
    ~ContextState() {
        if (timer_thread.joinable()) {
            timer_thread.detach();
        }
    }
};

class Context {
public:
    Context() : state_(std::make_shared<ContextState>()) {}
    explicit Context(std::shared_ptr<ContextState> state) : state_(std::move(state)) {}

    [[nodiscard]] bool is_cancelled() const noexcept {
        return state_ && state_->cancelled.load(std::memory_order_relaxed);
    }
    [[nodiscard]] bool done() const noexcept {
        return is_cancelled();
    }
    [[nodiscard]] std::string reason() const {
        if (!state_) return "";
        std::lock_guard<std::mutex> lk(state_->mu);
        return state_->reason;
    }
    void on_cancel(std::function<void()> cb) const {
        if (!state_ || !cb) return;
        std::lock_guard<std::mutex> lk(state_->mu);
        if (state_->cancelled.load(std::memory_order_relaxed)) {
            cb();
            return;
        }
        state_->callbacks.push_back(std::move(cb));
    }
    void wait() const {
        if (!state_) return;
        std::unique_lock<std::mutex> lk(state_->mu);
        state_->cv.wait(lk, [&] { return state_->cancelled.load(std::memory_order_relaxed); });
    }
    [[nodiscard]] bool wait_for(std::int64_t ms) const {
        if (!state_) return false;
        std::unique_lock<std::mutex> lk(state_->mu);
        return state_->cv.wait_for(lk, std::chrono::milliseconds(ms), [&] {
            return state_->cancelled.load(std::memory_order_relaxed);
        });
    }

    [[nodiscard]] auto raw_state() const noexcept -> const std::shared_ptr<ContextState>& {
        return state_;
    }

private:
    std::shared_ptr<ContextState> state_;
};

class CancelFunc {
public:
    CancelFunc() = default;
    explicit CancelFunc(std::function<void(std::string_view)> fn) : fn_(std::move(fn)) {}

    void operator()() const {
        if (fn_) fn_("cancelled");
    }
    void operator()(std::string_view reason) const {
        if (fn_) fn_(reason);
    }

private:
    std::function<void(std::string_view)> fn_;
};

[[nodiscard]] inline auto background() -> Context {
    return Context(std::make_shared<ContextState>());
}

[[nodiscard]] inline auto with_cancel(const Context& parent) -> std::pair<Context, CancelFunc> {
    auto child_state = std::make_shared<ContextState>();
    Context child(child_state);

    auto do_cancel = [child_state](std::string_view reason) {
        std::vector<std::function<void()>> to_run;
        {
            std::lock_guard<std::mutex> lk(child_state->mu);
            if (child_state->cancelled.exchange(true, std::memory_order_relaxed)) return;
            if (!reason.empty()) child_state->reason = std::string(reason);
            to_run = std::move(child_state->callbacks);
        }
        child_state->cv.notify_all();
        for (auto& cb : to_run) {
            if (cb) cb();
        }
    };

    parent.on_cancel([do_cancel, parent] {
        do_cancel(parent.reason());
    });

    CancelFunc fn([do_cancel](std::string_view r) { do_cancel(r); });
    return {child, fn};
}

[[nodiscard]] inline auto with_timeout(const Context& parent, std::int64_t ms) -> std::pair<Context, CancelFunc> {
    auto [child, cancel] = with_cancel(parent);
    if (ms <= 0) {
        cancel("deadline exceeded");
        return {child, cancel};
    }

    auto raw = child.raw_state();
    std::thread timer([raw, ms] {
        std::unique_lock<std::mutex> lk(raw->mu);
        if (raw->cv.wait_for(lk, std::chrono::milliseconds(ms), [&] {
            return raw->cancelled.load(std::memory_order_relaxed);
        })) {
            return;
        }
        std::vector<std::function<void()>> to_run;
        raw->cancelled.store(true, std::memory_order_relaxed);
        raw->reason = "deadline exceeded";
        to_run = std::move(raw->callbacks);
        lk.unlock();
        raw->cv.notify_all();
        for (auto& cb : to_run) {
            if (cb) cb();
        }
    });

    {
        std::lock_guard<std::mutex> lk(raw->mu);
        raw->timer_thread = std::move(timer);
    }

    return {child, cancel};
}

} // namespace cfire::sync::context

namespace cfire::context {
    using namespace cfire::sync::context;
}

// --- `core:time` (Modern Clocks, Duration Arithmetic & RFC 3339 Formatting) ---
namespace cfire::time {

struct Duration {
    std::int64_t nanos{0};

    [[nodiscard]] constexpr auto as_nanos() const noexcept -> std::int64_t { return nanos; }
    [[nodiscard]] constexpr auto as_micros() const noexcept -> std::int64_t { return nanos / 1'000; }
    [[nodiscard]] constexpr auto as_millis() const noexcept -> std::int64_t { return nanos / 1'000'000; }
    [[nodiscard]] constexpr auto as_seconds() const noexcept -> double { return static_cast<double>(nanos) / 1'000'000'000.0; }

    [[nodiscard]] constexpr auto nanos_val() const noexcept -> std::int64_t { return as_nanos(); }
    [[nodiscard]] constexpr auto micros() const noexcept -> std::int64_t { return as_micros(); }
    [[nodiscard]] constexpr auto millis() const noexcept -> std::int64_t { return as_millis(); }
    [[nodiscard]] constexpr auto seconds() const noexcept -> double { return as_seconds(); }

    constexpr auto operator+=(const Duration& other) noexcept -> Duration& {
        nanos += other.nanos;
        return *this;
    }
    constexpr auto operator-=(const Duration& other) noexcept -> Duration& {
        nanos -= other.nanos;
        return *this;
    }
    constexpr auto operator*=(std::int64_t factor) noexcept -> Duration& {
        nanos *= factor;
        return *this;
    }

    friend constexpr auto operator+(Duration a, const Duration& b) noexcept -> Duration { return Duration{a.nanos + b.nanos}; }
    friend constexpr auto operator-(Duration a, const Duration& b) noexcept -> Duration { return Duration{a.nanos - b.nanos}; }
    friend constexpr auto operator*(Duration a, std::int64_t factor) noexcept -> Duration { return Duration{a.nanos * factor}; }
    friend constexpr auto operator*(std::int64_t factor, Duration a) noexcept -> Duration { return Duration{a.nanos * factor}; }
    friend constexpr auto operator==(const Duration& a, const Duration& b) noexcept -> bool = default;
    friend constexpr auto operator<=>(const Duration& a, const Duration& b) noexcept = default;

    [[nodiscard]] static constexpr auto from_nanos(std::int64_t n) noexcept -> Duration { return Duration{n}; }
    [[nodiscard]] static constexpr auto from_micros(std::int64_t us) noexcept -> Duration { return Duration{us * 1'000}; }
    [[nodiscard]] static constexpr auto from_millis(std::int64_t ms) noexcept -> Duration { return Duration{ms * 1'000'000}; }
    [[nodiscard]] static constexpr auto from_secs(std::int64_t s) noexcept -> Duration { return Duration{s * 1'000'000'000}; }
};

[[nodiscard]] inline constexpr auto duration_nanos(std::int64_t n) noexcept -> Duration { return Duration{n}; }
[[nodiscard]] inline constexpr auto duration_micros(std::int64_t us) noexcept -> Duration { return Duration{us * 1'000}; }
[[nodiscard]] inline constexpr auto duration_millis(std::int64_t ms) noexcept -> Duration { return Duration{ms * 1'000'000}; }
[[nodiscard]] inline constexpr auto duration_seconds(std::int64_t s) noexcept -> Duration { return Duration{s * 1'000'000'000}; }
[[nodiscard]] inline constexpr auto duration_minutes(std::int64_t m) noexcept -> Duration { return Duration{m * 60 * 1'000'000'000}; }
[[nodiscard]] inline constexpr auto duration_hours(std::int64_t h) noexcept -> Duration { return Duration{h * 3600 * 1'000'000'000}; }

struct Time {
    std::chrono::system_clock::time_point tp{};

    [[nodiscard]] inline auto unix_seconds() const noexcept -> std::int64_t {
        return std::chrono::duration_cast<std::chrono::seconds>(tp.time_since_epoch()).count();
    }
    [[nodiscard]] inline auto unix_millis() const noexcept -> std::int64_t {
        return std::chrono::duration_cast<std::chrono::milliseconds>(tp.time_since_epoch()).count();
    }
    [[nodiscard]] inline auto unix_nanos() const noexcept -> std::int64_t {
        return std::chrono::duration_cast<std::chrono::nanoseconds>(tp.time_since_epoch()).count();
    }

    [[nodiscard]] inline auto add(const Duration& d) const noexcept -> Time {
        auto dur = std::chrono::duration_cast<std::chrono::system_clock::duration>(std::chrono::nanoseconds(d.nanos));
        return Time{tp + dur};
    }
    [[nodiscard]] inline auto sub(const Time& other) const noexcept -> Duration {
        auto diff = std::chrono::duration_cast<std::chrono::nanoseconds>(tp - other.tp).count();
        return Duration{diff};
    }

    // Calendar breakdowns (UTC)
    [[nodiscard]] inline auto year() const noexcept -> std::int32_t {
        std::time_t t = std::chrono::system_clock::to_time_t(tp);
        std::tm tm_buf{};
#if defined(_WIN32)
        gmtime_s(&tm_buf, &t);
#else
        gmtime_r(&t, &tm_buf);
#endif
        return tm_buf.tm_year + 1900;
    }
    [[nodiscard]] inline auto month() const noexcept -> std::int32_t {
        std::time_t t = std::chrono::system_clock::to_time_t(tp);
        std::tm tm_buf{};
#if defined(_WIN32)
        gmtime_s(&tm_buf, &t);
#else
        gmtime_r(&t, &tm_buf);
#endif
        return tm_buf.tm_mon + 1;
    }
    [[nodiscard]] inline auto day() const noexcept -> std::int32_t {
        std::time_t t = std::chrono::system_clock::to_time_t(tp);
        std::tm tm_buf{};
#if defined(_WIN32)
        gmtime_s(&tm_buf, &t);
#else
        gmtime_r(&t, &tm_buf);
#endif
        return tm_buf.tm_mday;
    }
    [[nodiscard]] inline auto hour() const noexcept -> std::int32_t {
        std::time_t t = std::chrono::system_clock::to_time_t(tp);
        std::tm tm_buf{};
#if defined(_WIN32)
        gmtime_s(&tm_buf, &t);
#else
        gmtime_r(&t, &tm_buf);
#endif
        return tm_buf.tm_hour;
    }
    [[nodiscard]] inline auto minute() const noexcept -> std::int32_t {
        std::time_t t = std::chrono::system_clock::to_time_t(tp);
        std::tm tm_buf{};
#if defined(_WIN32)
        gmtime_s(&tm_buf, &t);
#else
        gmtime_r(&t, &tm_buf);
#endif
        return tm_buf.tm_min;
    }
    [[nodiscard]] inline auto second() const noexcept -> std::int32_t {
        std::time_t t = std::chrono::system_clock::to_time_t(tp);
        std::tm tm_buf{};
#if defined(_WIN32)
        gmtime_s(&tm_buf, &t);
#else
        gmtime_r(&t, &tm_buf);
#endif
        return tm_buf.tm_sec;
    }

    [[nodiscard]] inline auto format_rfc3339() const -> std::string {
        std::time_t t = std::chrono::system_clock::to_time_t(tp);
        std::tm tm_buf{};
#if defined(_WIN32)
        gmtime_s(&tm_buf, &t);
#else
        gmtime_r(&t, &tm_buf);
#endif
        return std::format("{:04d}-{:02d}-{:02d}T{:02d}:{:02d}:{:02d}Z",
            tm_buf.tm_year + 1900, tm_buf.tm_mon + 1, tm_buf.tm_mday,
            tm_buf.tm_hour, tm_buf.tm_min, tm_buf.tm_sec);
    }

    friend constexpr auto operator==(const Time& a, const Time& b) noexcept -> bool = default;
    friend auto operator<=>(const Time& a, const Time& b) noexcept = default;
};

[[nodiscard]] inline auto now() noexcept -> Time {
    return Time{std::chrono::system_clock::now()};
}

[[nodiscard]] inline auto unix(std::int64_t sec, std::int64_t nsec = 0) noexcept -> Time {
    auto dur = std::chrono::duration_cast<std::chrono::system_clock::duration>(
        std::chrono::seconds(sec) + std::chrono::nanoseconds(nsec));
    return Time{std::chrono::system_clock::time_point{dur}};
}

[[nodiscard]] inline auto from_unix_seconds(std::int64_t sec) noexcept -> Time {
    return unix(sec, 0);
}

[[nodiscard]] inline auto parse_rfc3339(std::string_view s) -> std::expected<Time, cfire::Error> {
    if (s.size() < 20) {
        return cfire::make_error("invalid RFC 3339 timestamp length");
    }
    std::tm tm_buf{};
    int year = 0, mon = 0, mday = 0, hour = 0, min = 0, sec = 0;
    if (s[4] != '-' || s[7] != '-' || (s[10] != 'T' && s[10] != 't') || s[13] != ':' || s[16] != ':') {
        return cfire::make_error("invalid RFC 3339 timestamp delimiter");
    }
    auto parse_int = [](std::string_view sv, int& out) -> bool {
        auto [ptr, ec] = std::from_chars(sv.data(), sv.data() + sv.size(), out);
        return ec == std::errc{};
    };
    if (!parse_int(s.substr(0, 4), year) ||
        !parse_int(s.substr(5, 2), mon) ||
        !parse_int(s.substr(8, 2), mday) ||
        !parse_int(s.substr(11, 2), hour) ||
        !parse_int(s.substr(14, 2), min) ||
        !parse_int(s.substr(17, 2), sec)) {
        return cfire::make_error("failed to parse RFC 3339 numeric components");
    }
    tm_buf.tm_year = year - 1900;
    tm_buf.tm_mon = mon - 1;
    tm_buf.tm_mday = mday;
    tm_buf.tm_hour = hour;
    tm_buf.tm_min = min;
    tm_buf.tm_sec = sec;
    tm_buf.tm_isdst = 0;

#if defined(_WIN32)
    std::time_t tt = _mkgmtime(&tm_buf);
#else
    std::time_t tt = timegm(&tm_buf);
#endif
    if (tt == -1) {
        return cfire::make_error("invalid calendar date in RFC 3339 timestamp");
    }
    return Time{std::chrono::system_clock::from_time_t(tt)};
}

inline void sleep(const Duration& d) {
    if (d.nanos > 0) {
        std::this_thread::sleep_for(std::chrono::nanoseconds(d.nanos));
    }
}

[[nodiscard]] inline auto now_ms() noexcept -> std::int64_t {
    auto now_dur = std::chrono::steady_clock::now().time_since_epoch();
    return std::chrono::duration_cast<std::chrono::milliseconds>(now_dur).count();
}

[[nodiscard]] inline auto unix_seconds() noexcept -> std::int64_t {
    auto now_dur = std::chrono::system_clock::now().time_since_epoch();
    return std::chrono::duration_cast<std::chrono::seconds>(now_dur).count();
}

inline void sleep_ms(std::int64_t ms) {
    if (ms > 0) {
        std::this_thread::sleep_for(std::chrono::milliseconds(ms));
    }
}

inline auto sleep_ctx(const cfire::sync::context::Context& ctx, std::int64_t ms) -> bool {
    if (ctx.is_cancelled()) return false;
    if (ms <= 0) return true;
    bool cancelled = ctx.wait_for(ms);
    return !cancelled;
}

} // namespace cfire::time

// --- `core:log` (Structured Leveled Logging) ---
namespace cfire::log {

enum class Level : std::int32_t {
    Debug = 0,
    Info = 1,
    Warn = 2,
    Error = 3
};

enum class Format : std::int32_t {
    Text = 0,
    Json = 1
};

namespace detail {
    inline std::atomic<Level> g_level{Level::Info};
    inline std::atomic<Format> g_format{Format::Text};
    inline std::mutex g_log_mutex{};

    inline auto level_str(Level l) noexcept -> const char* {
        switch (l) {
            case Level::Debug: return "DEBUG";
            case Level::Info:  return "INFO";
            case Level::Warn:  return "WARN";
            case Level::Error: return "ERROR";
        }
        return "INFO";
    }

    inline auto level_color(Level l) noexcept -> const char* {
        switch (l) {
            case Level::Debug: return "\033[36m"; // Cyan
            case Level::Info:  return "\033[32m"; // Green
            case Level::Warn:  return "\033[33m"; // Yellow
            case Level::Error: return "\033[31m"; // Red
        }
        return "\033[0m";
    }

    inline void write_log(Level l, std::string_view msg, const std::vector<std::pair<std::string, std::string>>& attrs) {
        if (l < g_level.load(std::memory_order_relaxed)) {
            return;
        }

        std::string ts = cfire::time::now().format_rfc3339();
        std::lock_guard<std::mutex> lock(g_log_mutex);

        if (g_format.load(std::memory_order_relaxed) == Format::Json) {
            std::string out = "{\"time\":\"" + ts + "\",\"level\":\"" + level_str(l) + "\",\"msg\":\"";
            for (char c : msg) {
                if (c == '"') out += "\\\"";
                else if (c == '\\') out += "\\\\";
                else if (c == '\n') out += "\\n";
                else out += c;
            }
            out += "\"";
            for (const auto& [k, v] : attrs) {
                out += ",\"" + k + "\":\"";
                for (char c : v) {
                    if (c == '"') out += "\\\"";
                    else if (c == '\\') out += "\\\\";
                    else if (c == '\n') out += "\\n";
                    else out += c;
                }
                out += "\"";
            }
            out += "}\n";
            std::cerr << out;
        } else {
            std::string out = std::format("{}[{}]\033[0m {} - {}", level_color(l), level_str(l), ts, msg);
            for (const auto& [k, v] : attrs) {
                out += std::format(" {}={}", k, v);
            }
            out += "\n";
            std::cerr << out;
        }
    }
} // namespace detail

inline void set_level(Level l) noexcept {
    detail::g_level.store(l, std::memory_order_relaxed);
}

inline void set_format(Format f) noexcept {
    detail::g_format.store(f, std::memory_order_relaxed);
}

inline auto get_level() noexcept -> Level {
    return detail::g_level.load(std::memory_order_relaxed);
}

inline auto get_format() noexcept -> Format {
    return detail::g_format.load(std::memory_order_relaxed);
}

template <typename... Args>
inline void debug(std::string_view msg, Args&&... args) {
    std::vector<std::pair<std::string, std::string>> attrs;
    auto add_pairs = [&]<typename Self, typename K, typename V, typename... Rest>(Self& self, K&& k, V&& v, Rest&&... rest) {
        attrs.emplace_back(std::string(std::forward<K>(k)), std::string(std::forward<V>(v)));
        if constexpr (sizeof...(rest) >= 2) {
            self(self, std::forward<Rest>(rest)...);
        }
    };
    if constexpr (sizeof...(args) >= 2) {
        add_pairs(add_pairs, std::forward<Args>(args)...);
    }
    detail::write_log(Level::Debug, msg, attrs);
}

template <typename... Args>
inline void info(std::string_view msg, Args&&... args) {
    std::vector<std::pair<std::string, std::string>> attrs;
    auto add_pairs = [&]<typename Self, typename K, typename V, typename... Rest>(Self& self, K&& k, V&& v, Rest&&... rest) {
        attrs.emplace_back(std::string(std::forward<K>(k)), std::string(std::forward<V>(v)));
        if constexpr (sizeof...(rest) >= 2) {
            self(self, std::forward<Rest>(rest)...);
        }
    };
    if constexpr (sizeof...(args) >= 2) {
        add_pairs(add_pairs, std::forward<Args>(args)...);
    }
    detail::write_log(Level::Info, msg, attrs);
}

template <typename... Args>
inline void warn(std::string_view msg, Args&&... args) {
    std::vector<std::pair<std::string, std::string>> attrs;
    auto add_pairs = [&]<typename Self, typename K, typename V, typename... Rest>(Self& self, K&& k, V&& v, Rest&&... rest) {
        attrs.emplace_back(std::string(std::forward<K>(k)), std::string(std::forward<V>(v)));
        if constexpr (sizeof...(rest) >= 2) {
            self(self, std::forward<Rest>(rest)...);
        }
    };
    if constexpr (sizeof...(args) >= 2) {
        add_pairs(add_pairs, std::forward<Args>(args)...);
    }
    detail::write_log(Level::Warn, msg, attrs);
}

template <typename... Args>
inline void error(std::string_view msg, Args&&... args) {
    std::vector<std::pair<std::string, std::string>> attrs;
    auto add_pairs = [&]<typename Self, typename K, typename V, typename... Rest>(Self& self, K&& k, V&& v, Rest&&... rest) {
        attrs.emplace_back(std::string(std::forward<K>(k)), std::string(std::forward<V>(v)));
        if constexpr (sizeof...(rest) >= 2) {
            self(self, std::forward<Rest>(rest)...);
        }
    };
    if constexpr (sizeof...(args) >= 2) {
        add_pairs(add_pairs, std::forward<Args>(args)...);
    }
    detail::write_log(Level::Error, msg, attrs);
}

} // namespace cfire::log

// --- `core:os` (Environment Variables, Working Directory & Subprocess Execution) ---
namespace cfire::os {

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
    ::setenv(k.c_str(), v.c_str(), 1);
}

[[nodiscard]] inline auto cwd() -> std::filesystem::path {
    return std::filesystem::current_path();
}

[[nodiscard]] inline auto exec(std::string_view cmd) -> std::expected<std::string, cfire::Error> {
    std::string c(cmd);
    FILE* pipe = ::popen((c + " 2>&1").c_str(), "r");
    if (!pipe) {
        return cfire::make_error(std::format("failed to spawn command: {}", cmd));
    }
    std::string out;
    char buf[2048];
    while (std::fgets(buf, sizeof(buf), pipe) != nullptr) {
        out.append(buf);
    }
    int status = ::pclose(pipe);
    int code = WIFEXITED(status) ? WEXITSTATUS(status) : status;
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
    std::string stdout_str{};
    std::string stderr_str{};
    [[nodiscard]] auto success() const noexcept -> bool { return exit_code == 0; }
};

[[nodiscard]] inline auto command(std::string_view binary, std::span<const std::string> args = {})
    -> std::expected<ExecResult, cfire::Error>
{
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
#ifdef __APPLE__
    char** env = *_NSGetEnviron();
#else
    extern char** environ;
    char** env = environ;
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
        ssize_t n = 0;
        while ((n = ::read(fd, buf, sizeof(buf))) > 0) {
            out.append(buf, static_cast<std::size_t>(n));
        }
        ::close(fd);
        return out;
    };
    res.stdout_str = read_pipe(stdout_pipe[0]);
    res.stderr_str = read_pipe(stderr_pipe[0]);

    int status = 0;
    ::waitpid(pid, &status, 0);
    res.exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : status;
    return res;
}

[[nodiscard]] inline auto command(std::string_view binary, std::initializer_list<std::string_view> args)
    -> std::expected<ExecResult, cfire::Error>
{
    std::vector<std::string> vec;
    for (auto a : args) vec.emplace_back(a);
    return command(binary, std::span<const std::string>(vec));
}

} // namespace cfire::os

// --- `core:sync` (Go-Style Thread-Safe Typed Channel & Atomic Counter) ---
namespace cfire::sync {

template <typename T>
class Channel {
public:
    Channel() : state_(std::make_shared<State>()) {}

    void send(T val) const {
        std::unique_lock<std::mutex> lk(state_->mu);
        state_->q.push(std::move(val));
        state_->cv.notify_one();
    }

    [[nodiscard]] auto recv() const -> std::optional<T> {
        std::unique_lock<std::mutex> lk(state_->mu);
        state_->cv.wait(lk, [&] { return !state_->q.empty() || state_->closed; });
        if (state_->q.empty()) {
            return std::nullopt;
        }
        T val = std::move(state_->q.front());
        state_->q.pop();
        return val;
    }

    [[nodiscard]] auto recv_ctx(const context::Context& ctx) const -> std::expected<T, cfire::Error> {
        std::unique_lock<std::mutex> lk(state_->mu);
        if (ctx.is_cancelled()) {
            return cfire::make_error("context cancelled: " + ctx.reason());
        }
        ctx.on_cancel([this] {
            state_->cv.notify_all();
        });
        state_->cv.wait(lk, [&] {
            return !state_->q.empty() || state_->closed || ctx.is_cancelled();
        });
        if (ctx.is_cancelled()) {
            return cfire::make_error("context cancelled: " + ctx.reason());
        }
        if (state_->q.empty()) {
            return cfire::make_error("channel closed");
        }
        T val = std::move(state_->q.front());
        state_->q.pop();
        return val;
    }

    void close() const {
        std::unique_lock<std::mutex> lk(state_->mu);
        state_->closed = true;
        state_->cv.notify_all();
    }

private:
    struct State {
        std::mutex mu{};
        std::condition_variable cv{};
        std::queue<T> q{};
        bool closed = false;
    };
    std::shared_ptr<State> state_;
};

template <typename T>
[[nodiscard]] inline auto make_channel() -> Channel<T> {
    return Channel<T>{};
}

} // namespace cfire::sync

// --- `core:str` (String Manipulation Utilities) ---
namespace cfire::str {

[[nodiscard]] inline auto trim(std::string_view sv) -> std::string {
    while (!sv.empty() && std::isspace(static_cast<unsigned char>(sv.front()))) sv.remove_prefix(1);
    while (!sv.empty() && std::isspace(static_cast<unsigned char>(sv.back()))) sv.remove_suffix(1);
    return std::string(sv);
}

[[nodiscard]] inline auto split(std::string_view sv, std::string_view sep) -> std::vector<std::string> {
    std::vector<std::string> parts;
    if (sep.empty()) {
        parts.emplace_back(sv);
        return parts;
    }
    std::size_t start = 0;
    for (;;) {
        std::size_t pos = sv.find(sep, start);
        if (pos == std::string_view::npos) {
            parts.emplace_back(sv.substr(start));
            break;
        }
        parts.emplace_back(sv.substr(start, pos - start));
        start = pos + sep.size();
    }
    return parts;
}

[[nodiscard]] inline auto join(std::span<const std::string> parts, std::string_view sep) -> std::string {
    std::string out;
    for (std::size_t i = 0; i < parts.size(); ++i) {
        if (i > 0) out += sep;
        out += parts[i];
    }
    return out;
}

[[nodiscard]] inline auto to_upper(std::string_view sv) -> std::string {
    std::string out(sv);
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return out;
}

[[nodiscard]] inline auto to_lower(std::string_view sv) -> std::string {
    std::string out(sv);
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

[[nodiscard]] inline auto replace_all(std::string_view sv, std::string_view from, std::string_view to) -> std::string {
    if (from.empty()) return std::string(sv);
    std::string out;
    std::size_t start = 0;
    for (;;) {
        std::size_t pos = sv.find(from, start);
        if (pos == std::string_view::npos) {
            out.append(sv.substr(start));
            break;
        }
        out.append(sv.substr(start, pos - start));
        out.append(to);
        start = pos + from.size();
    }
    return out;
}

[[nodiscard]] inline auto starts_with(std::string_view sv, std::string_view prefix) noexcept -> bool {
    return sv.starts_with(prefix);
}

[[nodiscard]] inline auto ends_with(std::string_view sv, std::string_view suffix) noexcept -> bool {
    return sv.ends_with(suffix);
}

[[nodiscard]] inline auto contains(std::string_view sv, std::string_view needle) noexcept -> bool {
    return sv.find(needle) != std::string_view::npos;
}

[[nodiscard]] inline auto replace(std::string_view sv, std::string_view from, std::string_view to) -> std::string {
    return replace_all(sv, from, to);
}

} // namespace cfire::str

// --- `core:math` & `core:rand` ---
namespace cfire::math {

[[nodiscard]] inline auto abs_f64(double x) noexcept -> double { return std::abs(x); }
[[nodiscard]] inline auto sqrt_f64(double x) noexcept -> double { return std::sqrt(x); }
[[nodiscard]] inline auto sqrt(double x) noexcept -> double { return std::sqrt(x); }
[[nodiscard]] inline auto clamp_i64(std::int64_t v, std::int64_t lo, std::int64_t hi) noexcept -> std::int64_t {
    return std::clamp(v, lo, hi);
}
[[nodiscard]] inline auto clamp_f64(double v, double lo, double hi) noexcept -> double {
    return std::clamp(v, lo, hi);
}

} // namespace cfire::math

namespace cfire::rand {

inline auto get_rng() -> std::mt19937_64& {
    thread_local std::mt19937_64 rng{[] {
        try {
            return std::random_device{}();
        } catch (...) {
            return static_cast<unsigned int>(std::chrono::steady_clock::now().time_since_epoch().count());
        }
    }()};
    return rng;
}

[[nodiscard]] inline auto u64() -> std::uint64_t {
    std::uniform_int_distribution<std::uint64_t> dist;
    return dist(get_rng());
}

[[nodiscard]] inline auto f64_unit() -> double {
    std::uniform_real_distribution<double> dist(0.0, 1.0);
    return dist(get_rng());
}

[[nodiscard]] inline auto range_i64(std::int64_t lo, std::int64_t hi) -> std::int64_t {
    std::uniform_int_distribution<std::int64_t> dist(lo, hi);
    return dist(get_rng());
}

[[nodiscard]] inline auto bytes(std::size_t n) -> std::vector<std::byte> {
    std::vector<std::byte> buf(n);
#if defined(__APPLE__) || defined(__FreeBSD__) || defined(__OpenBSD__)
    if (n > 0) {
        ::arc4random_buf(buf.data(), n);
    }
#else
    std::ifstream urand("/dev/urandom", std::ios::binary);
    if (urand) {
        urand.read(reinterpret_cast<char*>(buf.data()), static_cast<std::streamsize>(n));
    } else {
        auto& rng = get_rng();
        for (std::size_t i = 0; i < n; ++i) {
            buf[i] = static_cast<std::byte>(rng() & 0xFF);
        }
    }
#endif
    return buf;
}

} // namespace cfire::rand

// --- `core:regex` (Regular Expressions Engine) ---
namespace cfire::regex {

class Regex {
public:
    Regex() = default;
    explicit Regex(std::string pattern)
        : pattern_(std::move(pattern)), re_(pattern_, std::regex::ECMAScript) {}

    [[nodiscard]] auto is_match(std::string_view text) const -> bool {
        return std::regex_search(text.begin(), text.end(), re_);
    }

    [[nodiscard]] auto find(std::string_view text) const -> std::optional<std::string> {
        std::match_results<std::string_view::const_iterator> match;
        if (std::regex_search(text.begin(), text.end(), match, re_)) {
            return std::string(match[0].first, match[0].second);
        }
        return std::nullopt;
    }

    [[nodiscard]] auto find_all(std::string_view text) const -> std::vector<std::string> {
        std::vector<std::string> results;
        auto words_begin = std::regex_iterator<std::string_view::const_iterator>(text.begin(), text.end(), re_);
        auto words_end = std::regex_iterator<std::string_view::const_iterator>();
        for (auto it = words_begin; it != words_end; ++it) {
            results.push_back(it->str());
        }
        return results;
    }

    [[nodiscard]] auto replace_all(std::string_view text, std::string_view repl) const -> std::string {
        std::string s(text);
        return std::regex_replace(s, re_, std::string(repl));
    }

    [[nodiscard]] auto pattern() const -> const std::string& { return pattern_; }

private:
    std::string pattern_{};
    std::regex re_{};
};

[[nodiscard]] inline auto compile(std::string_view pattern) -> std::expected<Regex, cfire::Error> {
    try {
        return Regex(std::string(pattern));
    } catch (const std::regex_error& e) {
        return cfire::make_error(std::format("invalid regex pattern '{}': {}", pattern, e.what()));
    }
}

[[nodiscard]] inline auto is_match(std::string_view pattern, std::string_view text) -> std::expected<bool, cfire::Error> {
    auto r = compile(pattern);
    if (!r) return std::unexpected(r.error());
    return r->is_match(text);
}

[[nodiscard]] inline auto replace_all(std::string_view pattern, std::string_view text, std::string_view repl)
    -> std::expected<std::string, cfire::Error>
{
    auto r = compile(pattern);
    if (!r) return std::unexpected(r.error());
    return r->replace_all(text, repl);
}

} // namespace cfire::regex

// --- `core:collections` (Set, PriorityQueue, Deque) ---
namespace cfire::collections {

template <typename T>
class Set {
public:
    Set() = default;

    auto insert(const T& val) -> bool {
        return set_.insert(val).second;
    }

    auto insert(T&& val) -> bool {
        return set_.insert(std::move(val)).second;
    }

    [[nodiscard]] auto contains(const T& val) const -> bool {
        return set_.contains(val);
    }

    auto remove(const T& val) -> bool {
        return set_.erase(val) > 0;
    }

    [[nodiscard]] auto size() const -> std::size_t {
        return set_.size();
    }

    [[nodiscard]] auto empty() const -> bool {
        return set_.empty();
    }

    void clear() {
        set_.clear();
    }

    [[nodiscard]] auto to_slice() const -> std::vector<T> {
        std::vector<T> out;
        out.reserve(set_.size());
        for (const auto& item : set_) out.push_back(item);
        return out;
    }

private:
    std::unordered_set<T> set_{};
};

template <typename T>
class PriorityQueue {
public:
    PriorityQueue() = default;

    void push(const T& val) {
        pq_.push(val);
    }

    void push(T&& val) {
        pq_.push(std::move(val));
    }

    [[nodiscard]] auto peek() const -> std::optional<T> {
        if (pq_.empty()) return std::nullopt;
        return pq_.top();
    }

    auto pop() -> std::optional<T> {
        if (pq_.empty()) return std::nullopt;
        T val = std::move(const_cast<T&>(pq_.top()));
        pq_.pop();
        return val;
    }

    [[nodiscard]] auto size() const -> std::size_t {
        return pq_.size();
    }

    [[nodiscard]] auto empty() const -> bool {
        return pq_.empty();
    }

private:
    std::priority_queue<T> pq_{};
};

template <typename T>
class Deque {
public:
    Deque() = default;

    void push_back(const T& val) {
        deq_.push_back(val);
    }

    void push_back(T&& val) {
        deq_.push_back(std::move(val));
    }

    void push_front(const T& val) {
        deq_.push_front(val);
    }

    void push_front(T&& val) {
        deq_.push_front(std::move(val));
    }

    auto pop_back() -> std::optional<T> {
        if (deq_.empty()) return std::nullopt;
        T val = std::move(deq_.back());
        deq_.pop_back();
        return val;
    }

    auto pop_front() -> std::optional<T> {
        if (deq_.empty()) return std::nullopt;
        T val = std::move(deq_.front());
        deq_.pop_front();
        return val;
    }

    [[nodiscard]] auto at(std::size_t idx) const -> std::optional<T> {
        if (idx >= deq_.size()) return std::nullopt;
        return deq_[idx];
    }

    [[nodiscard]] auto size() const -> std::size_t {
        return deq_.size();
    }

    [[nodiscard]] auto empty() const -> bool {
        return deq_.empty();
    }

    void clear() {
        deq_.clear();
    }

private:
    std::deque<T> deq_{};
};

} // namespace cfire::collections

// --- `core:sort` (In-Place Slice Sorting & Binary Search) ---
namespace cfire::sort {

template <typename T>
inline void slice(std::vector<T>& s) {
    std::ranges::sort(s);
}

template <typename T, typename Pred>
inline void slice_by(std::vector<T>& s, Pred&& comp) {
    std::ranges::sort(s, std::forward<Pred>(comp));
}

template <typename T>
[[nodiscard]] inline auto binary_search(std::span<const T> s, const T& val) -> std::optional<std::size_t> {
    auto it = std::ranges::lower_bound(s, val);
    if (it != s.end() && *it == val) {
        return static_cast<std::size_t>(std::distance(s.begin(), it));
    }
    return std::nullopt;
}

} // namespace cfire::sort

// --- `core:unicode` (Multi-Byte UTF-8 & Unicode Support) ---
namespace cfire::unicode {

inline auto decode_rune(std::string_view s, std::size_t& i) -> std::pair<std::uint32_t, std::size_t> {
    if (i >= s.size()) return {0, 0};
    unsigned char b0 = static_cast<unsigned char>(s[i]);
    if (b0 < 0x80) {
        ++i;
        return {b0, 1};
    }
    if ((b0 & 0xE0) == 0xC0) {
        if (i + 1 < s.size()) {
            unsigned char b1 = static_cast<unsigned char>(s[i + 1]);
            if ((b1 & 0xC0) == 0x80) {
                std::uint32_t cp = ((b0 & 0x1F) << 6) | (b1 & 0x3F);
                if (cp >= 0x80) { // Reject overlong
                    i += 2;
                    return {cp, 2};
                }
            }
        }
    } else if ((b0 & 0xF0) == 0xE0) {
        if (i + 2 < s.size()) {
            unsigned char b1 = static_cast<unsigned char>(s[i + 1]);
            unsigned char b2 = static_cast<unsigned char>(s[i + 2]);
            if ((b1 & 0xC0) == 0x80 && (b2 & 0xC0) == 0x80) {
                std::uint32_t cp = ((b0 & 0x0F) << 12) | ((b1 & 0x3F) << 6) | (b2 & 0x3F);
                // Reject overlong and surrogates (0xD800 - 0xDFFF)
                if (cp >= 0x800 && (cp < 0xD800 || cp > 0xDFFF)) {
                    i += 3;
                    return {cp, 3};
                }
            }
        }
    } else if ((b0 & 0xF8) == 0xF0) {
        if (i + 3 < s.size()) {
            unsigned char b1 = static_cast<unsigned char>(s[i + 1]);
            unsigned char b2 = static_cast<unsigned char>(s[i + 2]);
            unsigned char b3 = static_cast<unsigned char>(s[i + 3]);
            if ((b1 & 0xC0) == 0x80 && (b2 & 0xC0) == 0x80 && (b3 & 0xC0) == 0x80) {
                std::uint32_t cp = ((b0 & 0x07) << 18) | ((b1 & 0x3F) << 12) | ((b2 & 0x3F) << 6) | (b3 & 0x3F);
                if (cp >= 0x10000 && cp <= 0x10FFFF) {
                    i += 4;
                    return {cp, 4};
                }
            }
        }
    }
    // Invalid sequence: advance 1 byte and return Unicode replacement character U+FFFD
    ++i;
    return {0xFFFD, 1};
}

[[nodiscard]] inline auto is_valid_utf8(std::string_view s) -> bool {
    std::size_t i = 0;
    while (i < s.size()) {
        std::size_t prev = i;
        auto [cp, len] = decode_rune(s, i);
        if (cp == 0xFFFD && (i - prev != 3 || s.substr(prev, 3) != "\xEF\xBF\xBD")) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] inline auto rune_count(std::string_view s) -> std::size_t {
    std::size_t count = 0;
    std::size_t i = 0;
    while (i < s.size()) {
        decode_rune(s, i);
        ++count;
    }
    return count;
}

[[nodiscard]] inline auto runes(std::string_view s) -> std::vector<std::uint32_t> {
    std::vector<std::uint32_t> r;
    std::size_t i = 0;
    while (i < s.size()) {
        auto [cp, _] = decode_rune(s, i);
        r.push_back(cp);
    }
    return r;
}

[[nodiscard]] inline auto encode_rune(std::uint32_t cp) -> std::string {
    std::string out;
    if (cp <= 0x7F) {
        out.push_back(static_cast<char>(cp));
    } else if (cp <= 0x7FF) {
        out.push_back(static_cast<char>(0xC0 | ((cp >> 6) & 0x1F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp <= 0xFFFF) {
        out.push_back(static_cast<char>(0xE0 | ((cp >> 12) & 0x0F)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp <= 0x10FFFF) {
        out.push_back(static_cast<char>(0xF0 | ((cp >> 18) & 0x07)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
        out += "\xEF\xBF\xBD";
    }
    return out;
}

[[nodiscard]] inline auto to_lower(std::string_view s) -> std::string {
    std::string out;
    out.reserve(s.size());
    std::size_t i = 0;
    while (i < s.size()) {
        auto [cp, len] = decode_rune(s, i);
        if (cp >= 'A' && cp <= 'Z') {
            cp += ('a' - 'A');
        } else if (cp >= 0x0410 && cp <= 0x042F) { // Cyrillic Capital A-Ya
            cp += 0x20;
        } else if (cp >= 0x0391 && cp <= 0x03A9 && cp != 0x03A2) { // Greek Capital
            cp += 0x20;
        }
        out += encode_rune(cp);
    }
    return out;
}

[[nodiscard]] inline auto to_upper(std::string_view s) -> std::string {
    std::string out;
    out.reserve(s.size());
    std::size_t i = 0;
    while (i < s.size()) {
        auto [cp, len] = decode_rune(s, i);
        if (cp >= 'a' && cp <= 'z') {
            cp -= ('a' - 'A');
        } else if (cp >= 0x0430 && cp <= 0x044F) { // Cyrillic Small a-ya
            cp -= 0x20;
        } else if (cp >= 0x03B1 && cp <= 0x03C9) { // Greek Small
            cp -= 0x20;
        }
        out += encode_rune(cp);
    }
    return out;
}

} // namespace cfire::unicode



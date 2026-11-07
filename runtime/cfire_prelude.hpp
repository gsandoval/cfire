#pragma once
// cfire_prelude.hpp — Zero-overhead C++23 Runtime Prelude for cfire (.cf)

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <charconv>
#include <cinttypes>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <expected>
#include <filesystem>
#include <format>
#include <functional>
#include <future>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#if __has_include(<print>)
#include <print>
#endif
#include <cstdio>
#include <cerrno>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>
#if defined(__AVX2__) || defined(__SSE2__)
#include <immintrin.h>
#elif defined(__ARM_NEON) || defined(__ARM_NEON__)
#include <arm_neon.h>
#elif defined(__wasm_simd128__)
#include <wasm_simd128.h>
#endif
#if !defined(_WIN32)
#include <unistd.h>
#if !defined(__EMSCRIPTEN__)
#include <termios.h>
#endif
#else
#ifndef NOMINMAX
#define NOMINMAX 1
#endif
#include <io.h>
#include <fcntl.h>
#include <windows.h>
#endif

#if !__has_include(<print>)
namespace std {
    template <typename... Args>
    inline void print(std::format_string<Args...> fmt, Args&&... args) {
        std::printf("%s", std::format(fmt, std::forward<Args>(args)...).c_str());
    }
    template <typename... Args>
    inline void print(std::FILE* stream, std::format_string<Args...> fmt, Args&&... args) {
        std::fprintf(stream, "%s", std::format(fmt, std::forward<Args>(args)...).c_str());
    }
    template <typename... Args>
    inline void println(std::format_string<Args...> fmt, Args&&... args) {
        std::printf("%s\n", std::format(fmt, std::forward<Args>(args)...).c_str());
    }
    template <typename... Args>
    inline void println(std::FILE* stream, std::format_string<Args...> fmt, Args&&... args) {
        std::fprintf(stream, "%s\n", std::format(fmt, std::forward<Args>(args)...).c_str());
    }
    inline void println() {
        std::printf("\n");
    }
    inline void println(std::FILE* stream) {
        std::fprintf(stream, "\n");
    }
}
#endif

#if defined(__clang__)
#define CFIRE_LIFETIMEBOUND [[clang::lifetimebound]]
#else
#define CFIRE_LIFETIMEBOUND
#endif

inline auto operator+(const std::string& lhs, std::string_view rhs) -> std::string {
    std::string res = lhs;
    res.append(rhs);
    return res;
}

inline auto operator+(std::string_view lhs, const std::string& rhs) -> std::string {
    std::string res(lhs);
    res.append(rhs);
    return res;
}

inline auto operator+(std::string_view lhs, std::string_view rhs) -> std::string {
    std::string res(lhs);
    res.append(rhs);
    return res;
}

inline auto operator+(const char* lhs, std::string_view rhs) -> std::string {
    std::string res(lhs);
    res.append(rhs);
    return res;
}

inline auto operator+(std::string_view lhs, const char* rhs) -> std::string {
    std::string res(lhs);
    res.append(rhs);
    return res;
}

// Allow comparison between smart pointers and `none` (std::nullopt)
template <typename T>
inline auto operator==(const std::shared_ptr<T>& ptr, std::nullopt_t) noexcept -> bool {
    return ptr == nullptr;
}
template <typename T>
inline auto operator!=(const std::shared_ptr<T>& ptr, std::nullopt_t) noexcept -> bool {
    return ptr != nullptr;
}
template <typename T>
inline auto operator==(std::nullopt_t, const std::shared_ptr<T>& ptr) noexcept -> bool {
    return ptr == nullptr;
}
template <typename T>
inline auto operator!=(std::nullopt_t, const std::shared_ptr<T>& ptr) noexcept -> bool {
    return ptr != nullptr;
}

template <typename T>
inline auto operator==(const std::unique_ptr<T>& ptr, std::nullopt_t) noexcept -> bool {
    return ptr == nullptr;
}
template <typename T>
inline auto operator!=(const std::unique_ptr<T>& ptr, std::nullopt_t) noexcept -> bool {
    return ptr != nullptr;
}
template <typename T>
inline auto operator==(std::nullopt_t, const std::unique_ptr<T>& ptr) noexcept -> bool {
    return ptr == nullptr;
}
template <typename T>
inline auto operator!=(std::nullopt_t, const std::unique_ptr<T>& ptr) noexcept -> bool {
    return ptr != nullptr;
}

template <typename T>
inline auto operator==(T* ptr, std::nullopt_t) noexcept -> bool {
    return ptr == nullptr;
}
template <typename T>
inline auto operator!=(T* ptr, std::nullopt_t) noexcept -> bool {
    return ptr != nullptr;
}
template <typename T>
inline auto operator==(std::nullopt_t, T* ptr) noexcept -> bool {
    return ptr == nullptr;
}
template <typename T>
inline auto operator!=(std::nullopt_t, T* ptr) noexcept -> bool {
    return ptr != nullptr;
}

template <std::integral I>
constexpr auto operator==(std::byte lhs, I rhs) noexcept -> bool {
    return std::to_integer<I>(lhs) == rhs;
}
template <std::integral I>
constexpr auto operator==(I lhs, std::byte rhs) noexcept -> bool {
    return lhs == std::to_integer<I>(rhs);
}
template <std::integral I>
constexpr auto operator!=(std::byte lhs, I rhs) noexcept -> bool {
    return std::to_integer<I>(lhs) != rhs;
}
template <std::integral I>
constexpr auto operator!=(I lhs, std::byte rhs) noexcept -> bool {
    return lhs != std::to_integer<I>(rhs);
}

namespace cfire {

// --- Error Type for T! (std::expected<T, cfire::Error>) ---
class Error {
public:
    Error(std::string msg) : msg_(std::move(msg)) {}
    Error(std::string_view msg) : msg_(msg) {}
    Error(const char* msg) : msg_(msg) {}
    [[nodiscard]] auto message() const noexcept -> const std::string& { return msg_; }
    [[nodiscard]] auto size() const noexcept -> std::size_t { return msg_.size(); }
    [[nodiscard]] auto data() const noexcept -> const char* { return msg_.data(); }
    [[nodiscard]] auto c_str() const noexcept -> const char* { return msg_.c_str(); }
private:
    std::string msg_;
};

[[nodiscard]] inline auto make_error(std::string msg) -> std::unexpected<Error> {
    return std::unexpected<Error>(Error(std::move(msg)));
}

[[nodiscard]] inline auto make_error(std::string_view msg) -> std::unexpected<Error> {
    return std::unexpected<Error>(Error(std::string(msg)));
}

[[nodiscard]] inline auto make_error(const char* msg) -> std::unexpected<Error> {
    return std::unexpected<Error>(Error(msg ? std::string(msg) : std::string{}));
}

[[nodiscard]] inline auto make_error(Error err) -> std::unexpected<Error> {
    return std::unexpected<Error>(std::move(err));
}

[[nodiscard]] inline auto make_error(std::unexpected<Error> err) -> std::unexpected<Error> {
    return err;
}

// --- Overloaded Visitor Helper for `match` on Tagged Enums (`std::variant`) ---
template <class... Ts>
struct overloaded : Ts... {
    using Ts::operator()...;
};
template <class... Ts>
overloaded(Ts...) -> overloaded<Ts...>;

// --- Zero-Cost RAII Scope Guard for `defer` ---
template <typename F>
class ScopeExit {
public:
    explicit ScopeExit(F&& fn) noexcept : fn_(std::forward<F>(fn)) {}
    ~ScopeExit() noexcept { fn_(); }
    ScopeExit(const ScopeExit&) = delete;
    auto operator=(const ScopeExit&) -> ScopeExit& = delete;
private:
    F fn_;
};

template <typename F>
[[nodiscard]] auto scope_exit(F&& fn) noexcept -> ScopeExit<std::decay_t<F>> {
    return ScopeExit<std::decay_t<F>>(std::forward<F>(fn));
}

// --- Transparent Hash Map (`map[K]V`) with Zero-Allocation String Lookup ---
struct StringHash {
    using is_transparent = void;
    [[nodiscard]] auto operator()(std::string_view sv) const noexcept -> std::size_t {
        return std::hash<std::string_view>{}(sv);
    }
    [[nodiscard]] auto operator()(const char* s) const noexcept -> std::size_t {
        return std::hash<std::string_view>{}(s);
    }
    [[nodiscard]] auto operator()(const std::string& s) const noexcept -> std::size_t {
        return std::hash<std::string_view>{}(s);
    }
};

template <typename K, typename V>
class Map {
public:
    using underlying_t = std::conditional_t<
        std::is_same_v<K, std::string>,
        std::unordered_map<std::string, V, StringHash, std::equal_to<>>,
        std::unordered_map<K, V>>;

    Map() = default;

    template <typename KeyArg>
    auto operator[](KeyArg&& k) -> V& {
        if constexpr (std::is_same_v<K, std::string> && std::is_convertible_v<KeyArg, std::string_view>) {
            std::string_view sv(k);
            if (auto it = map_.find(sv); it != map_.end()) {
                return it->second;
            }
            return map_.emplace(std::string(sv), V{}).first->second;
        } else {
            return map_[std::forward<KeyArg>(k)];
        }
    }

    template <typename KeyArg>
    [[nodiscard]] auto contains(const KeyArg& k) const -> bool {
        return map_.find(k) != map_.end();
    }

    template <typename KeyArg>
    [[nodiscard]] auto get(const KeyArg& k) const -> std::optional<V> {
        if (auto it = map_.find(k); it != map_.end()) {
            return it->second;
        }
        return std::nullopt;
    }

    template <typename KeyArg>
    auto erase(const KeyArg& k) -> std::size_t {
        if constexpr (std::is_same_v<K, std::string> && std::is_convertible_v<KeyArg, std::string_view>) {
            std::string_view sv(k);
            if (auto it = map_.find(sv); it != map_.end()) {
                map_.erase(it);
                return 1;
            }
            return 0;
        } else {
            return map_.erase(k);
        }
    }

    template <typename KeyArg>
    auto remove(const KeyArg& k) -> std::size_t {
        return erase(k);
    }

    [[nodiscard]] auto size() const noexcept -> std::size_t { return map_.size(); }
    [[nodiscard]] auto empty() const noexcept -> bool { return map_.empty(); }
    auto begin() noexcept { return map_.begin(); }
    auto end() noexcept { return map_.end(); }
    auto begin() const noexcept { return map_.begin(); }
    auto end() const noexcept { return map_.end(); }

private:
    underlying_t map_{};
};

// --- Zero-Copy Slicing (`s[start..end]`) for `str`, `[]T`, `[N]T`, and `bytes` ---
[[nodiscard]] inline auto slice(std::string_view sv CFIRE_LIFETIMEBOUND, std::size_t start, std::size_t end) noexcept -> std::string_view {
    if (start > sv.size()) start = sv.size();
    if (end > sv.size()) end = sv.size();
    if (end < start) end = start;
    return sv.substr(start, end - start);
}

template <typename T>
[[nodiscard]] inline auto slice(const std::vector<T>& vec CFIRE_LIFETIMEBOUND, std::size_t start, std::size_t end) noexcept -> std::span<const T> {
    if (start > vec.size()) start = vec.size();
    if (end > vec.size()) end = vec.size();
    if (end < start) end = start;
    return std::span<const T>(vec.data() + start, end - start);
}

template <typename T>
[[nodiscard]] inline auto slice(std::span<const T> sp CFIRE_LIFETIMEBOUND, std::size_t start, std::size_t end) noexcept -> std::span<const T> {
    if (start > sp.size()) start = sp.size();
    if (end > sp.size()) end = sp.size();
    if (end < start) end = start;
    return sp.subspan(start, end - start);
}

template <typename T, std::size_t N>
[[nodiscard]] inline auto slice(const std::array<T, N>& arr CFIRE_LIFETIMEBOUND, std::size_t start, std::size_t end) noexcept -> std::span<const T> {
    if (start > N) start = N;
    if (end > N) end = N;
    if (end < start) end = start;
    return std::span<const T>(arr.data() + start, end - start);
}

// --- Smart Pointer Factories (`make(own[T], ...)` & `make(shared[T], ...)`) ---
template <typename T, typename... Args>
[[nodiscard]] inline auto make_own(Args&&... args) -> std::unique_ptr<T> {
    return std::make_unique<T>(std::forward<Args>(args)...);
}

template <typename T, typename... Args>
[[nodiscard]] inline auto make_shared_ptr(Args&&... args) -> std::shared_ptr<T> {
    return std::make_shared<T>(std::forward<Args>(args)...);
}

} // namespace cfire

#include "cfire_test_runtime.hpp"

namespace cfire {

// --- Zero-Overhead Inline Bounds-Checked Indexing (`arr[i]`, `slice[i]`, `str[i]`) (SAFE-P0-03) ---
template <typename Container>
concept CfireIndexableSeq = requires(Container& c, std::size_t i) {
    c.size();
    c.data();
    c[i];
};

template <CfireIndexableSeq Container, std::integral Idx>
[[nodiscard]] inline decltype(auto) index_at(
    Container&& c,
    Idx idx,
    std::string_view file = "",
    int line = 0
) {
    if constexpr (std::is_signed_v<Idx>) {
        if (idx < 0) [[unlikely]] {
            assert_true_at(
                false,
                std::format("negative index {} out of bounds for length {}", idx, c.size()),
                file,
                line,
                "index out of bounds"
            );
        }
    }
    const auto u_idx = static_cast<std::size_t>(idx);
    if (u_idx >= c.size()) [[unlikely]] {
        assert_true_at(
            false,
            std::format("index {} out of bounds for length {}", u_idx, c.size()),
            file,
            line,
            "index out of bounds"
        );
    }
    return std::forward<Container>(c)[u_idx];
}

template <typename Container, typename Key>
    requires (!CfireIndexableSeq<Container>)
[[nodiscard]] inline decltype(auto) index_at(
    Container&& c,
    Key&& k,
    std::string_view = "",
    int = 0
) {
    return std::forward<Container>(c)[std::forward<Key>(k)];
}

// --- String / Path / C-String Boundary Coercions ---
// Safely converts `std::string_view` (including non-null-terminated slices `s[0..4]`)
// into a null-terminated `const char*` valid for the duration of the full C call expression.
class CStrViewHolder {
public:
    explicit CStrViewHolder(std::string_view sv) noexcept {
        if (sv.size() < small_buf_.size()) {
            std::copy_n(sv.data(), sv.size(), small_buf_.data());
            small_buf_[sv.size()] = '\0';
            ptr_ = small_buf_.data();
        } else {
            heap_str_.assign(sv);
            ptr_ = heap_str_.c_str();
        }
    }
    CStrViewHolder(const CStrViewHolder& other)
        : small_buf_(other.small_buf_), heap_str_(other.heap_str_) {
        ptr_ = heap_str_.empty() ? small_buf_.data() : heap_str_.c_str();
    }
    CStrViewHolder(CStrViewHolder&& other) noexcept
        : small_buf_(other.small_buf_), heap_str_(std::move(other.heap_str_)) {
        ptr_ = heap_str_.empty() ? small_buf_.data() : heap_str_.c_str();
    }
    auto operator=(const CStrViewHolder& other) -> CStrViewHolder& {
        if (this != &other) {
            small_buf_ = other.small_buf_;
            heap_str_ = other.heap_str_;
            ptr_ = heap_str_.empty() ? small_buf_.data() : heap_str_.c_str();
        }
        return *this;
    }
    auto operator=(CStrViewHolder&& other) noexcept -> CStrViewHolder& {
        if (this != &other) {
            small_buf_ = other.small_buf_;
            heap_str_ = std::move(other.heap_str_);
            ptr_ = heap_str_.empty() ? small_buf_.data() : heap_str_.c_str();
        }
        return *this;
    }
    // Implicit conversion to `const char*` for C function arguments
    [[nodiscard]] operator const char*() const noexcept CFIRE_LIFETIMEBOUND { return ptr_; }
    [[nodiscard]] auto get() const noexcept CFIRE_LIFETIMEBOUND -> const char* { return ptr_; }

private:
    std::array<char, 256> small_buf_{};
    std::string heap_str_{};
    const char* ptr_ = nullptr;
};

[[nodiscard]] inline auto c_str(const std::string& s CFIRE_LIFETIMEBOUND) noexcept -> const char* {
    return s.c_str();
}
[[nodiscard]] inline auto c_str(const std::filesystem::path& p CFIRE_LIFETIMEBOUND) noexcept -> const char* {
    return p.c_str();
}
[[nodiscard]] inline auto c_str(const char* s CFIRE_LIFETIMEBOUND) noexcept -> const char* {
    return s;
}
[[nodiscard]] inline auto c_str(std::string_view sv) noexcept -> CStrViewHolder {
    return CStrViewHolder(sv);
}
[[nodiscard]] inline auto c_arg(const std::string& s CFIRE_LIFETIMEBOUND) noexcept -> const char* {
    return s.c_str();
}
[[nodiscard]] inline auto c_arg(const std::filesystem::path& p CFIRE_LIFETIMEBOUND) noexcept -> const char* {
    return p.c_str();
}
[[nodiscard]] inline auto c_arg(std::string_view sv) noexcept -> CStrViewHolder {
    return CStrViewHolder(sv);
}
[[nodiscard]] inline auto c_arg(std::byte* p) noexcept -> unsigned char* {
    return reinterpret_cast<unsigned char*>(p);
}
[[nodiscard]] inline auto c_arg(const std::byte* p) noexcept -> const unsigned char* {
    return reinterpret_cast<const unsigned char*>(p);
}
template <typename T>
    requires (!std::is_same_v<std::remove_cvref_t<T>, std::string> &&
              !std::is_same_v<std::remove_cvref_t<T>, std::string_view> &&
              !std::is_same_v<std::remove_cvref_t<T>, std::filesystem::path> &&
              !std::is_same_v<std::remove_cvref_t<T>, std::byte*> &&
              !std::is_same_v<std::remove_cvref_t<T>, const std::byte*>)
[[nodiscard]] constexpr auto c_arg(T&& v) noexcept -> T&& {
    return std::forward<T>(v);
}
[[nodiscard]] inline auto path_with_ext(std::filesystem::path p, std::string_view ext) -> std::filesystem::path {
    p.replace_extension(ext);
    return p;
}
[[nodiscard]] inline auto to_bytes(std::string_view sv CFIRE_LIFETIMEBOUND) noexcept -> std::span<const std::byte> {
    return std::as_bytes(std::span<const char>(sv.data(), sv.size()));
}
[[nodiscard]] inline auto to_bytes(std::span<const std::uint8_t> sp) noexcept -> std::span<const std::byte> {
    return std::as_bytes(sp);
}
[[nodiscard]] inline auto to_bytes(std::span<const std::byte> sp) noexcept -> std::span<const std::byte> {
    return sp;
}
[[nodiscard]] inline auto to_u8_vec(std::span<const std::byte> sp) -> std::vector<std::uint8_t> {
    std::vector<std::uint8_t> out(sp.size());
    if (!sp.empty()) {
        std::memcpy(out.data(), sp.data(), sp.size());
    }
    return out;
}
[[nodiscard]] inline auto to_u8_vec(std::span<const std::uint8_t> sp) -> std::vector<std::uint8_t> {
    return std::vector<std::uint8_t>(sp.begin(), sp.end());
}
[[nodiscard]] inline auto to_u8_vec(std::string_view sv) -> std::vector<std::uint8_t> {
    std::vector<std::uint8_t> out(sv.size());
    if (!sv.empty()) {
        std::memcpy(out.data(), sv.data(), sv.size());
    }
    return out;
}
[[nodiscard]] inline auto to_u8_vec(std::initializer_list<std::uint8_t> init) -> std::vector<std::uint8_t> {
    return std::vector<std::uint8_t>(init.begin(), init.end());
}
[[nodiscard]] inline auto to_byte_vec(std::span<const std::uint8_t> sp) -> std::vector<std::byte> {
    std::vector<std::byte> out(sp.size());
    if (!sp.empty()) {
        std::memcpy(out.data(), sp.data(), sp.size());
    }
    return out;
}
[[nodiscard]] inline auto to_byte_vec(std::span<const std::byte> sp) -> std::vector<std::byte> {
    return std::vector<std::byte>(sp.begin(), sp.end());
}
[[nodiscard]] inline auto to_byte_vec(std::string_view sv) -> std::vector<std::byte> {
    std::vector<std::byte> out(sv.size());
    if (!sv.empty()) {
        std::memcpy(out.data(), sv.data(), sv.size());
    }
    return out;
}
[[nodiscard]] inline auto to_byte_vec(std::initializer_list<std::uint8_t> init) -> std::vector<std::byte> {
    std::vector<std::byte> out;
    out.reserve(init.size());
    for (auto b : init) out.push_back(static_cast<std::byte>(b));
    return out;
}
[[nodiscard]] inline auto to_str(std::span<const std::byte> sp) -> std::string {
    return std::string(reinterpret_cast<const char*>(sp.data()), sp.size());
}
[[nodiscard]] inline auto to_str(std::span<const std::uint8_t> sp) -> std::string {
    return std::string(reinterpret_cast<const char*>(sp.data()), sp.size());
}

template <typename T, typename U>
inline void vec_push(std::vector<T>& vec, U&& val) {
    if constexpr (std::is_same_v<T, std::byte> && std::is_integral_v<std::remove_cvref_t<U>>) {
        vec.push_back(static_cast<std::byte>(val));
    } else if constexpr (std::is_same_v<T, std::uint8_t> && std::is_same_v<std::remove_cvref_t<U>, std::byte>) {
        vec.push_back(static_cast<std::uint8_t>(val));
    } else {
        vec.push_back(std::forward<U>(val));
    }
}

template <typename T>
[[nodiscard]] inline auto str_ctor(const T& val) -> std::string {
    if constexpr (std::is_convertible_v<T, std::string_view>) {
        return std::string(val);
    } else if constexpr (std::is_convertible_v<T, std::span<const std::byte>>) {
        return to_str(std::span<const std::byte>(val));
    } else if constexpr (std::is_convertible_v<T, std::span<const std::uint8_t>>) {
        return to_str(std::span<const std::uint8_t>(val));
    } else {
        return std::format("{}", val);
    }
}

// --- Safe, EINTR-Resilient POSIX File Descriptor Primitives & Console Input ---
namespace detail {

[[nodiscard]] inline auto fd_read(int fd, std::span<std::byte> buf) -> std::expected<std::size_t, Error> {
    if (fd < 0) return make_error("invalid file descriptor");
    if (buf.empty()) return std::size_t{0};
#if !defined(_WIN32)
    while (true) {
        ssize_t n = ::read(fd, buf.data(), buf.size());
        if (n < 0) {
            if (errno == EINTR) continue;
            return make_error(std::format("fd_read({}) failed: {}", fd, std::strerror(errno)));
        }
        return static_cast<std::size_t>(n);
    }
#else
    int n = ::_read(fd, buf.data(), static_cast<unsigned int>(std::min<std::size_t>(buf.size(), 0x7fffffffU)));
    if (n < 0) {
        return make_error(std::format("fd_read({}) failed: {}", fd, std::strerror(errno)));
    }
    return static_cast<std::size_t>(n);
#endif
}

[[nodiscard]] inline auto fd_write_all(int fd, std::span<const std::byte> data) -> std::expected<std::size_t, Error> {
    if (fd < 0) return make_error("invalid file descriptor");
    if (data.empty()) return std::size_t{0};
#if !defined(_WIN32)
    std::size_t total = 0;
    while (total < data.size()) {
        ssize_t n = ::write(fd, data.data() + total, data.size() - total);
        if (n < 0) {
            if (errno == EINTR) continue;
            return make_error(std::format("fd_write({}) failed: {}", fd, std::strerror(errno)));
        }
        if (n == 0) {
            return make_error(std::format("fd_write({}) wrote 0 bytes", fd));
        }
        total += static_cast<std::size_t>(n);
    }
    return total;
#else
    std::size_t total = 0;
    while (total < data.size()) {
        unsigned int chunk = static_cast<unsigned int>(std::min<std::size_t>(data.size() - total, 0x7fffffffU));
        int n = ::_write(fd, data.data() + total, chunk);
        if (n <= 0) {
            return make_error(std::format("fd_write({}) failed: {}", fd, std::strerror(errno)));
        }
        total += static_cast<std::size_t>(n);
    }
    return total;
#endif
}

[[nodiscard]] inline auto fd_read_all_bounded(
    int fd,
    std::size_t max_bytes = 64ULL * 1024ULL * 1024ULL
) -> std::expected<std::vector<std::byte>, Error> {
    if (fd < 0) return make_error("invalid file descriptor");
    std::vector<std::byte> out;
    std::array<std::byte, 8192> chunk{};
    while (true) {
        auto n = fd_read(fd, chunk);
        if (!n) return std::unexpected(n.error());
        if (*n == 0) break;
        if (out.size() + *n > max_bytes) {
            return make_error(std::format("fd_read_all exceeded max_bytes limit ({})", max_bytes));
        }
        out.insert(out.end(), chunk.begin(), chunk.begin() + static_cast<std::ptrdiff_t>(*n));
    }
    return out;
}

[[nodiscard]] inline auto fd_read_line(
    int fd,
    std::size_t max_bytes = 1024ULL * 1024ULL
) -> std::optional<std::string> {
    if (fd < 0) return std::nullopt;
    std::string line;
    char ch = '\0';
    bool got_any = false;
    while (line.size() < max_bytes) {
#if !defined(_WIN32)
        ssize_t n = ::read(fd, &ch, 1);
        if (n < 0) {
            if (errno == EINTR) continue;
            break;
        }
#else
        int n = ::_read(fd, &ch, 1);
        if (n < 0) break;
#endif
        if (n == 0) break;
        got_any = true;
        if (ch == '\n') {
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }
            return line;
        }
        line.push_back(ch);
    }
    if (!got_any) return std::nullopt;
    if (!line.empty() && line.back() == '\r') {
        line.pop_back();
    }
    return line;
}

[[nodiscard]] inline auto readln_noecho(std::string_view prompt = "") -> std::optional<std::string> {
#if !defined(_WIN32)
    if (!prompt.empty()) {
        std::fflush(stderr);
        (void)fd_write_all(STDERR_FILENO, std::as_bytes(std::span(prompt.data(), prompt.size())));
    }
#if !defined(__EMSCRIPTEN__)
    struct TermiosEchoGuard {
        struct termios orig{};
        bool active = false;
        TermiosEchoGuard() noexcept {
            if (::tcgetattr(STDIN_FILENO, &orig) == 0) {
                struct termios noecho = orig;
                noecho.c_lflag &= ~static_cast<tcflag_t>(ECHO);
                if (::tcsetattr(STDIN_FILENO, TCSANOW, &noecho) == 0) {
                    active = true;
                }
            }
        }
        ~TermiosEchoGuard() noexcept {
            if (active) {
                (void)::tcsetattr(STDIN_FILENO, TCSANOW, &orig);
                const char nl = '\n';
                (void)fd_write_all(STDERR_FILENO, std::as_bytes(std::span(&nl, 1)));
            }
        }
    } echo_guard;
#endif
    return fd_read_line(STDIN_FILENO);
#else
    if (!prompt.empty()) {
        std::fflush(stderr);
        (void)fd_write_all(2, std::as_bytes(std::span(prompt.data(), prompt.size())));
    }
    HANDLE hIn = ::GetStdHandle(STD_INPUT_HANDLE);
    DWORD origMode = 0;
    bool hasMode = (hIn != INVALID_HANDLE_VALUE && ::GetConsoleMode(hIn, &origMode) != 0);
    if (hasMode) {
        ::SetConsoleMode(hIn, origMode & ~static_cast<DWORD>(ENABLE_ECHO_INPUT));
    }
    auto res = fd_read_line(0);
    if (hasMode) {
        ::SetConsoleMode(hIn, origMode);
        const char nl = '\n';
        (void)fd_write_all(2, std::as_bytes(std::span(&nl, 1)));
    }
    return res;
#endif
}

} // namespace detail

// --- Built-in Console Line Input (`readln`) ---
[[nodiscard]] inline auto readln(std::string_view prompt = "") -> std::optional<std::string> {
#if !defined(_WIN32)
    if (!prompt.empty()) {
        std::fflush(stdout);
        (void)detail::fd_write_all(STDOUT_FILENO, std::as_bytes(std::span(prompt.data(), prompt.size())));
    }
    return detail::fd_read_line(STDIN_FILENO);
#else
    if (!prompt.empty()) {
        std::fflush(stdout);
        (void)detail::fd_write_all(1, std::as_bytes(std::span(prompt.data(), prompt.size())));
    }
    return detail::fd_read_line(0);
#endif
}

// --- High-Performance Zero-Allocation Exception-Free Integer Parsing (<charconv>) ---
namespace detail {

inline auto process_args_storage() -> std::vector<std::string>& {
    static std::vector<std::string> s_args;
    return s_args;
}

inline void set_process_args(int argc, char** argv) {
    auto& out = process_args_storage();
    out.clear();
    if (argc > 0 && argv) {
        out.reserve(static_cast<std::size_t>(argc));
        for (int i = 0; i < argc; ++i) {
            if (argv[i]) out.emplace_back(argv[i]);
        }
    }
}

template <typename T>
[[nodiscard]] inline auto parse_int(std::string_view sv) noexcept -> std::optional<T> {
    while (!sv.empty() && (sv.front() == ' ' || sv.front() == '\t')) sv.remove_prefix(1);
    while (!sv.empty() && (sv.back() == ' ' || sv.back() == '\t' || sv.back() == '\r' || sv.back() == '\n')) sv.remove_suffix(1);
    if (sv.empty()) return std::nullopt;
    T val{};
    auto [ptr, ec] = std::from_chars(sv.data(), sv.data() + sv.size(), val);
    if (ec == std::errc{} && ptr == sv.data() + sv.size()) {
        return val;
    }
    return std::nullopt;
}

template <typename T>
[[nodiscard]] inline auto parse_float(std::string_view sv) noexcept -> std::optional<T> {
    while (!sv.empty() && (sv.front() == ' ' || sv.front() == '\t')) sv.remove_prefix(1);
    while (!sv.empty() && (sv.back() == ' ' || sv.back() == '\t' || sv.back() == '\r' || sv.back() == '\n')) sv.remove_suffix(1);
    if (sv.empty()) return std::nullopt;
    std::string tmp(sv);
    char* end_ptr = nullptr;
    errno = 0;
    double d = std::strtod(tmp.c_str(), &end_ptr);
    if (errno == 0 && end_ptr == tmp.c_str() + tmp.size()) {
        return static_cast<T>(d);
    }
    return std::nullopt;
}

[[nodiscard]] inline auto json_escape(std::string_view sv) -> std::string {
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

[[nodiscard]] inline auto bytes_to_hex(std::span<const std::byte> data) -> std::string {
    static constexpr char kDigits[] = "0123456789abcdef";
    std::string out;
    out.reserve(data.size() * 2);
    for (std::byte b : data) {
        auto val = static_cast<std::uint8_t>(b);
        out.push_back(kDigits[(val >> 4) & 0x0F]);
        out.push_back(kDigits[val & 0x0F]);
    }
    return out;
}

[[nodiscard]] inline auto bytes_to_hex(std::span<const std::uint8_t> data) -> std::string {
    return bytes_to_hex(std::as_bytes(data));
}

[[nodiscard]] inline auto base64_encode(std::span<const std::byte> data, bool url_safe = false) -> std::string {
    static constexpr std::string_view kStdChars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    static constexpr std::string_view kUrlChars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
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

[[nodiscard]] inline auto simd_dot_product(std::span<const float> a, std::span<const float> b) noexcept -> double {
    std::size_t n = std::min(a.size(), b.size());
    std::size_t i = 0;
    double sum = 0.0;
#if defined(__AVX2__)
    __m256 v_sum = _mm256_setzero_ps();
    for (; i + 8 <= n; i += 8) {
        v_sum = _mm256_add_ps(v_sum, _mm256_mul_ps(_mm256_loadu_ps(a.data() + i), _mm256_loadu_ps(b.data() + i)));
    }
    alignas(32) float tmp[8];
    _mm256_store_ps(tmp, v_sum);
    for (int k = 0; k < 8; ++k) sum += tmp[k];
#elif defined(__ARM_NEON) || defined(__ARM_NEON__)
    float32x4_t v_sum = vdupq_n_f32(0.0f);
    for (; i + 4 <= n; i += 4) {
        v_sum = vfmaq_f32(v_sum, vld1q_f32(a.data() + i), vld1q_f32(b.data() + i));
    }
    sum += static_cast<double>(vaddvq_f32(v_sum));
#elif defined(__wasm_simd128__)
    v128_t v_sum = wasm_f32x4_splat(0.0f);
    for (; i + 4 <= n; i += 4) {
        v_sum = wasm_f32x4_add(v_sum, wasm_f32x4_mul(wasm_v128_load(a.data() + i), wasm_v128_load(b.data() + i)));
    }
    alignas(16) float tmp[4];
    wasm_v128_store(tmp, v_sum);
    for (int k = 0; k < 4; ++k) sum += tmp[k];
#endif
    for (; i < n; ++i) {
        sum += static_cast<double>(a[i]) * static_cast<double>(b[i]);
    }
    return sum;
}

[[nodiscard]] inline auto simd_cosine_similarity(std::span<const float> a, std::span<const float> b) noexcept -> double {
    std::size_t n = std::min(a.size(), b.size());
    if (n == 0) return 0.0;
    std::size_t i = 0;
    double dot = 0.0;
    double na = 0.0;
    double nb = 0.0;
#if defined(__AVX2__)
    __m256 v_dot = _mm256_setzero_ps();
    __m256 v_na  = _mm256_setzero_ps();
    __m256 v_nb  = _mm256_setzero_ps();
    for (; i + 8 <= n; i += 8) {
        __m256 va = _mm256_loadu_ps(a.data() + i);
        __m256 vb = _mm256_loadu_ps(b.data() + i);
        v_dot = _mm256_add_ps(v_dot, _mm256_mul_ps(va, vb));
        v_na  = _mm256_add_ps(v_na,  _mm256_mul_ps(va, va));
        v_nb  = _mm256_add_ps(v_nb,  _mm256_mul_ps(vb, vb));
    }
    alignas(32) float t_dot[8], t_na[8], t_nb[8];
    _mm256_store_ps(t_dot, v_dot);
    _mm256_store_ps(t_na,  v_na);
    _mm256_store_ps(t_nb,  v_nb);
    for (int k = 0; k < 8; ++k) { dot += t_dot[k]; na += t_na[k]; nb += t_nb[k]; }
#elif defined(__ARM_NEON) || defined(__ARM_NEON__)
    float32x4_t v_dot = vdupq_n_f32(0.0f);
    float32x4_t v_na  = vdupq_n_f32(0.0f);
    float32x4_t v_nb  = vdupq_n_f32(0.0f);
    for (; i + 4 <= n; i += 4) {
        float32x4_t va = vld1q_f32(a.data() + i);
        float32x4_t vb = vld1q_f32(b.data() + i);
        v_dot = vfmaq_f32(v_dot, va, vb);
        v_na  = vfmaq_f32(v_na,  va, va);
        v_nb  = vfmaq_f32(v_nb,  vb, vb);
    }
    dot += static_cast<double>(vaddvq_f32(v_dot));
    na  += static_cast<double>(vaddvq_f32(v_na));
    nb  += static_cast<double>(vaddvq_f32(v_nb));
#elif defined(__wasm_simd128__)
    v128_t v_dot = wasm_f32x4_splat(0.0f);
    v128_t v_na  = wasm_f32x4_splat(0.0f);
    v128_t v_nb  = wasm_f32x4_splat(0.0f);
    for (; i + 4 <= n; i += 4) {
        v128_t va = wasm_v128_load(a.data() + i);
        v128_t vb = wasm_v128_load(b.data() + i);
        v_dot = wasm_f32x4_add(v_dot, wasm_f32x4_mul(va, vb));
        v_na  = wasm_f32x4_add(v_na,  wasm_f32x4_mul(va, va));
        v_nb  = wasm_f32x4_add(v_nb,  wasm_f32x4_mul(vb, vb));
    }
    alignas(16) float t_dot[4], t_na[4], t_nb[4];
    wasm_v128_store(t_dot, v_dot);
    wasm_v128_store(t_na,  v_na);
    wasm_v128_store(t_nb,  v_nb);
    for (int k = 0; k < 4; ++k) { dot += t_dot[k]; na += t_na[k]; nb += t_nb[k]; }
#endif
    for (; i < n; ++i) {
        double va = static_cast<double>(a[i]);
        double vb = static_cast<double>(b[i]);
        dot += va * vb;
        na += va * va;
        nb += vb * vb;
    }
    double denom = std::sqrt(na) * std::sqrt(nb);
    return denom > 1e-12 ? (dot / denom) : 0.0;
}

[[nodiscard]] inline auto simd_l2_distance(std::span<const float> a, std::span<const float> b) noexcept -> double {
    std::size_t n = std::min(a.size(), b.size());
    std::size_t i = 0;
    double sum_sq = 0.0;
#if defined(__AVX2__)
    __m256 v_sq = _mm256_setzero_ps();
    for (; i + 8 <= n; i += 8) {
        __m256 d = _mm256_sub_ps(_mm256_loadu_ps(a.data() + i), _mm256_loadu_ps(b.data() + i));
        v_sq = _mm256_add_ps(v_sq, _mm256_mul_ps(d, d));
    }
    alignas(32) float tmp[8];
    _mm256_store_ps(tmp, v_sq);
    for (int k = 0; k < 8; ++k) sum_sq += tmp[k];
#elif defined(__ARM_NEON) || defined(__ARM_NEON__)
    float32x4_t v_sq = vdupq_n_f32(0.0f);
    for (; i + 4 <= n; i += 4) {
        float32x4_t d = vsubq_f32(vld1q_f32(a.data() + i), vld1q_f32(b.data() + i));
        v_sq = vfmaq_f32(v_sq, d, d);
    }
    sum_sq += static_cast<double>(vaddvq_f32(v_sq));
#elif defined(__wasm_simd128__)
    v128_t v_sq = wasm_f32x4_splat(0.0f);
    for (; i + 4 <= n; i += 4) {
        v128_t d = wasm_f32x4_sub(wasm_v128_load(a.data() + i), wasm_v128_load(b.data() + i));
        v_sq = wasm_f32x4_add(v_sq, wasm_f32x4_mul(d, d));
    }
    alignas(16) float tmp[4];
    wasm_v128_store(tmp, v_sq);
    for (int k = 0; k < 4; ++k) sum_sq += tmp[k];
#endif
    for (; i < n; ++i) {
        double d = static_cast<double>(a[i]) - static_cast<double>(b[i]);
        sum_sq += d * d;
    }
    return std::sqrt(sum_sq);
}

} // namespace detail

// --- Structured Concurrency (`async.scope`) ---
namespace async {

class Scope {
public:
    explicit Scope(std::size_t max_workers = 4)
        : limiter_(std::make_shared<WorkerLimiter>(max_workers == 0 ? 4 : max_workers)) {}
    ~Scope() { join_all(); }

    template <typename F>
    void spawn(F&& task) {
        auto lim = limiter_;
        futures_.emplace_back(std::async(std::launch::async, [lim, fn = std::forward<F>(task)]() mutable {
            lim->acquire();
            auto rel = cfire::scope_exit([&] { lim->release(); });
            fn();
        }));
    }

    template <typename F>
    void defer(F&& fn) {
        defers_.emplace_back(std::forward<F>(fn));
    }

    void join_all() {
        for (auto& f : futures_) {
            if (f.valid()) {
                f.get();
            }
        }
        futures_.clear();

        while (!defers_.empty()) {
            auto fn = std::move(defers_.back());
            defers_.pop_back();
            fn();
        }
    }

private:
    struct WorkerLimiter {
        std::mutex mu;
        std::condition_variable cv;
        std::size_t active = 0;
        std::size_t limit = 4;
        explicit WorkerLimiter(std::size_t lim) : limit(lim) {}
        void acquire() {
            std::unique_lock<std::mutex> lk(mu);
            cv.wait(lk, [&] { return active < limit; });
            ++active;
        }
        void release() {
            {
                std::lock_guard<std::mutex> lk(mu);
                --active;
            }
            cv.notify_one();
        }
    };

    std::shared_ptr<WorkerLimiter> limiter_;
    std::vector<std::future<void>> futures_;
    std::vector<std::function<void()>> defers_;
};

template <typename F>
void run_scope(std::size_t workers, F&& body) {
    Scope s(workers);
    body(s);
    s.join_all();
}

} // namespace async

template <typename E>
decltype(auto) unwrap_val(E&& exp) {
    if constexpr (std::is_void_v<typename std::decay_t<E>::value_type>) {
        if (!exp.has_value()) {
            std::println(stderr, "panic: unwrap_val called on unexpected error: {}", exp.error().message());
            std::abort();
        }
        return;
    } else {
        if (!exp.has_value()) {
            std::println(stderr, "panic: unwrap_val called on unexpected error: {}", exp.error().message());
            std::abort();
        }
        return *std::forward<E>(exp);
    }
}

template <typename Exp>
auto unwrap_or_default(Exp&& exp) {
    using V = typename std::decay_t<Exp>::value_type;
    if constexpr (std::is_void_v<V>) {
        (void)exp;
        return;
    } else {
        if (exp.has_value()) return std::move(*exp);
        return V{};
    }
}

template <typename Exp, typename U>
auto unwrap_or(Exp&& exp, U&& fallback) {
    using V = typename std::decay_t<Exp>::value_type;
    if constexpr (std::is_void_v<V>) {
        (void)exp;
        return std::forward<U>(fallback);
    } else {
        if (exp.has_value()) return std::move(*exp);
        return static_cast<V>(std::forward<U>(fallback));
    }
}

template <typename T>
inline auto extract_or_error(const std::expected<T, Error>& res) -> const Error& {
    return res.error();
}

template <typename T>
struct reflect_struct {
    static constexpr std::string_view name = "";
    static constexpr std::size_t field_count = 0;
};

// --- Built-in Numeric & Primitive Type Conversion Constructors (LANG-P0-01) ---
template <typename T>
concept CfireScalarCastable =
    std::is_arithmetic_v<std::remove_cvref_t<T>> ||
    std::is_same_v<std::remove_cvref_t<T>, std::byte> ||
    std::is_enum_v<std::remove_cvref_t<T>>;

template <CfireScalarCastable T>
[[nodiscard]] constexpr auto i8(T val) noexcept -> std::int8_t {
    return static_cast<std::int8_t>(val);
}
template <CfireScalarCastable T>
[[nodiscard]] constexpr auto i16(T val) noexcept -> std::int16_t {
    return static_cast<std::int16_t>(val);
}
template <CfireScalarCastable T>
[[nodiscard]] constexpr auto i32(T val) noexcept -> std::int32_t {
    return static_cast<std::int32_t>(val);
}
template <CfireScalarCastable T>
[[nodiscard]] constexpr auto i64(T val) noexcept -> std::int64_t {
    return static_cast<std::int64_t>(val);
}
template <CfireScalarCastable T>
[[nodiscard]] constexpr auto u8(T val) noexcept -> std::uint8_t {
    return static_cast<std::uint8_t>(val);
}
template <CfireScalarCastable T>
[[nodiscard]] constexpr auto byte(T val) noexcept -> std::byte {
    return static_cast<std::byte>(static_cast<std::uint8_t>(val));
}
template <CfireScalarCastable T>
[[nodiscard]] constexpr auto u16(T val) noexcept -> std::uint16_t {
    return static_cast<std::uint16_t>(val);
}
template <CfireScalarCastable T>
[[nodiscard]] constexpr auto u32(T val) noexcept -> std::uint32_t {
    return static_cast<std::uint32_t>(val);
}
template <CfireScalarCastable T>
[[nodiscard]] constexpr auto u64(T val) noexcept -> std::uint64_t {
    return static_cast<std::uint64_t>(val);
}
template <CfireScalarCastable T>
[[nodiscard]] constexpr auto usize(T val) noexcept -> std::size_t {
    return static_cast<std::size_t>(val);
}
template <CfireScalarCastable T>
[[nodiscard]] constexpr auto isize(T val) noexcept -> std::ptrdiff_t {
    return static_cast<std::ptrdiff_t>(val);
}
template <CfireScalarCastable T>
[[nodiscard]] constexpr auto f32(T val) noexcept -> float {
    return static_cast<float>(val);
}
template <CfireScalarCastable T>
[[nodiscard]] constexpr auto f64(T val) noexcept -> double {
    return static_cast<double>(val);
}
template <CfireScalarCastable T>
[[nodiscard]] constexpr auto bool_(T val) noexcept -> bool {
    return static_cast<bool>(val);
}

// --- Bitwise Operator Overloads for Imported Vendor/C Enums (LANG-P1-03) ---
namespace enum_operators {

template <typename E>
concept CfireIsEnum = std::is_enum_v<E>;

template <CfireIsEnum E>
[[nodiscard]] constexpr auto operator|(E a, E b) noexcept -> E {
    using U = std::underlying_type_t<E>;
    return static_cast<E>(static_cast<U>(a) | static_cast<U>(b));
}
template <CfireIsEnum E>
[[nodiscard]] constexpr auto operator&(E a, E b) noexcept -> E {
    using U = std::underlying_type_t<E>;
    return static_cast<E>(static_cast<U>(a) & static_cast<U>(b));
}
template <CfireIsEnum E>
[[nodiscard]] constexpr auto operator^(E a, E b) noexcept -> E {
    using U = std::underlying_type_t<E>;
    return static_cast<E>(static_cast<U>(a) ^ static_cast<U>(b));
}
template <CfireIsEnum E>
[[nodiscard]] constexpr auto operator~(E a) noexcept -> E {
    using U = std::underlying_type_t<E>;
    return static_cast<E>(~static_cast<U>(a));
}
template <CfireIsEnum E>
constexpr auto operator|=(E& a, E b) noexcept -> E& {
    a = a | b;
    return a;
}
template <CfireIsEnum E>
constexpr auto operator&=(E& a, E b) noexcept -> E& {
    a = a & b;
    return a;
}
template <CfireIsEnum E>
constexpr auto operator^=(E& a, E b) noexcept -> E& {
    a = a ^ b;
    return a;
}

// Mixed Enum & Integral Bitwise Operations
template <CfireIsEnum E, std::integral I>
[[nodiscard]] constexpr auto operator|(E a, I b) noexcept -> E {
    using U = std::underlying_type_t<E>;
    return static_cast<E>(static_cast<U>(a) | static_cast<U>(b));
}
template <CfireIsEnum E, std::integral I>
[[nodiscard]] constexpr auto operator|(I a, E b) noexcept -> E {
    using U = std::underlying_type_t<E>;
    return static_cast<E>(static_cast<U>(a) | static_cast<U>(b));
}
template <CfireIsEnum E, std::integral I>
[[nodiscard]] constexpr auto operator&(E a, I b) noexcept -> E {
    using U = std::underlying_type_t<E>;
    return static_cast<E>(static_cast<U>(a) & static_cast<U>(b));
}
template <CfireIsEnum E, std::integral I>
[[nodiscard]] constexpr auto operator&(I a, E b) noexcept -> E {
    using U = std::underlying_type_t<E>;
    return static_cast<E>(static_cast<U>(a) & static_cast<U>(b));
}
template <CfireIsEnum E, std::integral I>
constexpr auto operator|=(E& a, I b) noexcept -> E& {
    using U = std::underlying_type_t<E>;
    a = static_cast<E>(static_cast<U>(a) | static_cast<U>(b));
    return a;
}
template <CfireIsEnum E, std::integral I>
constexpr auto operator&=(E& a, I b) noexcept -> E& {
    using U = std::underlying_type_t<E>;
    a = static_cast<E>(static_cast<U>(a) & static_cast<U>(b));
    return a;
}

} // namespace enum_operators

} // namespace cfire

template <typename T>
[[nodiscard]] inline auto str(const T& val) -> std::string {
    return cfire::str_ctor(val);
}
using cfire::readln;
using cfire::i8;
using cfire::i16;
using cfire::i32;
using cfire::i64;
using cfire::u8;
using cfire::byte;
using cfire::u16;
using cfire::u32;
using cfire::u64;
using cfire::usize;
using cfire::isize;
using cfire::f32;
using cfire::f64;
using cfire::bool_;

using namespace cfire::enum_operators;

// --- Formatter specialization for Enums (enables std::print / std::println) ---
template <cfire::enum_operators::CfireIsEnum E, typename CharT>
struct std::formatter<E, CharT> : std::formatter<std::underlying_type_t<E>, CharT> {
    template <typename FormatContext>
    auto format(E e, FormatContext& ctx) const {
        return std::formatter<std::underlying_type_t<E>, CharT>::format(
            static_cast<std::underlying_type_t<E>>(e), ctx);
    }
};

// --- Monadic `try` Propagation Macros (Statement-Expression / Early Return) ---
#define CFIRE_CONCAT_INNER(a, b) a##b
#define CFIRE_CONCAT(a, b) CFIRE_CONCAT_INNER(a, b)

#define CFIRE_TRY_ASSIGN(lhs, expr)                                      \
    auto CFIRE_CONCAT(_cf_try_, __LINE__) = (expr);                      \
    if (!CFIRE_CONCAT(_cf_try_, __LINE__).has_value()) {                 \
        return std::unexpected(std::move(CFIRE_CONCAT(_cf_try_, __LINE__).error())); \
    }                                                                    \
    lhs = std::move(*CFIRE_CONCAT(_cf_try_, __LINE__))

#define CFIRE_TRY(expr)                                                  \
    do {                                                                 \
        auto CFIRE_CONCAT(_cf_try_v_, __LINE__) = (expr);                \
        if (!CFIRE_CONCAT(_cf_try_v_, __LINE__).has_value()) {           \
            return std::unexpected(std::move(CFIRE_CONCAT(_cf_try_v_, __LINE__).error())); \
        }                                                                \
    } while (0)

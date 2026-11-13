#pragma once
// cfire_prelude.hpp — Zero-overhead C++23 Runtime Prelude for cfire (.cf)
// CFIRE_RUNTIME_VERSION: 0.11.0
// CFIRE_RUNTIME_ARCHIVE_SHA: $Format:%h$

#if defined(__clang__) && !defined(__apple_build_version__)
#  if !defined(__cpp_concepts) || (__cpp_concepts < 202002L)
#    pragma clang diagnostic push
#    pragma clang diagnostic ignored "-Wbuiltin-macro-redefined"
#    undef __cpp_concepts
#    define __cpp_concepts 202002L
#    pragma clang diagnostic pop
#  endif
#endif

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <cmath>
#include <charconv>
#include <cinttypes>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <expected>
#include <filesystem>
#include <random>
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
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <source_location>
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
#include <fcntl.h>
#include <sys/stat.h>
#if !defined(__EMSCRIPTEN__)
#include <poll.h>
#include <termios.h>
#endif
#else
#ifndef NOMINMAX
#define NOMINMAX 1
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN 1
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <io.h>
#include <fcntl.h>
#include <windows.h>
#include <shellapi.h>
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

// Q36 (CF-182): the lowering of a read-only `str` parameter and of every `&str` view. It is a
// `std::string_view` that also converts implicitly to an owned `std::string`, so a view passed where
// cfire requires an owned `str` (a `move` parameter, field, variable or return value) is copied there.
namespace cfire {
struct str_ref : std::string_view {
    constexpr str_ref() noexcept = default;
    constexpr str_ref(std::string_view sv) noexcept : std::string_view(sv) {}
    str_ref(const std::string& s CFIRE_LIFETIMEBOUND) noexcept : std::string_view(s) {}
    constexpr str_ref(const char* s) noexcept : std::string_view(s) {}
    constexpr str_ref(const char* s, std::size_t n) noexcept : std::string_view(s, n) {}
    operator std::string() const { return std::string(data(), size()); }
    operator std::filesystem::path() const { return std::filesystem::path(std::string_view(*this)); }
};
inline auto operator+(str_ref lhs, str_ref rhs) -> std::string { std::string r(lhs); r.append(rhs); return r; }
inline auto operator+(str_ref lhs, const std::string& rhs) -> std::string { std::string r(lhs); r.append(rhs); return r; }
inline auto operator+(const std::string& lhs, str_ref rhs) -> std::string { std::string r(lhs); r.append(rhs); return r; }
inline auto operator+(str_ref lhs, const char* rhs) -> std::string { std::string r(lhs); r.append(rhs); return r; }
inline auto operator+(const char* lhs, str_ref rhs) -> std::string { std::string r(lhs); r.append(rhs); return r; }
inline auto operator+(str_ref lhs, char rhs) -> std::string { std::string r(lhs); r.push_back(rhs); return r; }
inline auto operator+(char lhs, str_ref rhs) -> std::string { std::string r(1, lhs); r.append(rhs); return r; }
} // namespace cfire
template <>
struct std::formatter<cfire::str_ref, char> : std::formatter<std::string_view, char> {
    auto format(cfire::str_ref s, std::format_context& ctx) const { return std::formatter<std::string_view, char>::format(s, ctx); }
};
template <>
struct std::hash<cfire::str_ref> : std::hash<std::string_view> {};

// Allow comparison between smart pointers, raw pointers, optionals, and `none`
namespace cfire {
struct None_t {
    constexpr operator std::nullopt_t() const noexcept { return std::nullopt; }
    template <typename T>
    constexpr operator std::optional<T>() const noexcept { return std::nullopt; }
    template <typename T>
    constexpr operator T*() const noexcept { return nullptr; }
    template <typename T>
    constexpr operator std::shared_ptr<T>() const noexcept { return nullptr; }
    template <typename T>
    constexpr operator std::unique_ptr<T>() const noexcept { return nullptr; }
    constexpr auto operator==(std::nullopt_t) const noexcept -> bool { return true; }
    constexpr auto operator!=(std::nullopt_t) const noexcept -> bool { return false; }
};
inline constexpr None_t none{};

[[noreturn]] inline void panic(std::string_view msg) {
    std::println(stderr, "panic: {}", msg);
    std::abort();
}

template <typename A, typename B>
[[nodiscard]] constexpr auto div(A a, B b) {
    if constexpr (std::is_integral_v<A> && std::is_integral_v<B>) {
        if (b == 0) [[unlikely]] {
            cfire::panic("division by zero");
        }
        using R = decltype(a / b);
        if constexpr (std::is_signed_v<R>) {
            // MIN / -1 overflows (UB, traps on x86); release wraps like every other overflow (Q4, CF-166).
            if (static_cast<R>(b) == R{-1} && static_cast<R>(a) == std::numeric_limits<R>::min()) [[unlikely]] {
                return static_cast<R>(a);
            }
        }
    }
    return a / b;
}

template <typename A, typename B>
[[nodiscard]] constexpr auto mod(A a, B b) {
    if constexpr (std::is_floating_point_v<A> || std::is_floating_point_v<B>) {
        // W4 (CF-162): float `%` is IEEE `fmod` (sign of the dividend; `x % 0.0` is NaN, not a panic).
        using R = std::common_type_t<A, B>;
        return std::fmod(static_cast<R>(a), static_cast<R>(b));
    } else {
        if (b == 0) [[unlikely]] {
            cfire::panic("division by zero");
        }
        if constexpr (std::is_integral_v<A> && std::is_integral_v<B>) {
            using R = decltype(a % b);
            if constexpr (std::is_signed_v<R>) {
                if (static_cast<R>(b) == R{-1}) [[unlikely]] return R{0};  // MIN % -1 is UB in C++
            }
        }
        return a % b;
    }
}
} // namespace cfire

using cfire::none;
using cfire::None_t;
using cfire::panic;

template <typename T>
inline auto operator==(const std::shared_ptr<T>& ptr, cfire::None_t) noexcept -> bool { return ptr == nullptr; }
template <typename T>
inline auto operator!=(const std::shared_ptr<T>& ptr, cfire::None_t) noexcept -> bool { return ptr != nullptr; }
template <typename T>
inline auto operator==(cfire::None_t, const std::shared_ptr<T>& ptr) noexcept -> bool { return ptr == nullptr; }
template <typename T>
inline auto operator!=(cfire::None_t, const std::shared_ptr<T>& ptr) noexcept -> bool { return ptr != nullptr; }

template <typename T>
inline auto operator==(const std::unique_ptr<T>& ptr, cfire::None_t) noexcept -> bool { return ptr == nullptr; }
template <typename T>
inline auto operator!=(const std::unique_ptr<T>& ptr, cfire::None_t) noexcept -> bool { return ptr != nullptr; }
template <typename T>
inline auto operator==(cfire::None_t, const std::unique_ptr<T>& ptr) noexcept -> bool { return ptr == nullptr; }
template <typename T>
inline auto operator!=(cfire::None_t, const std::unique_ptr<T>& ptr) noexcept -> bool { return ptr != nullptr; }

template <typename T>
inline auto operator==(T* ptr, cfire::None_t) noexcept -> bool { return ptr == nullptr; }
template <typename T>
inline auto operator!=(T* ptr, cfire::None_t) noexcept -> bool { return ptr != nullptr; }
template <typename T>
inline auto operator==(cfire::None_t, T* ptr) noexcept -> bool { return ptr == nullptr; }
template <typename T>
inline auto operator!=(cfire::None_t, T* ptr) noexcept -> bool { return ptr != nullptr; }

template <typename T>
inline auto operator==(const std::optional<T>& opt, cfire::None_t) noexcept -> bool { return !opt.has_value(); }
template <typename T>
inline auto operator!=(const std::optional<T>& opt, cfire::None_t) noexcept -> bool { return opt.has_value(); }
template <typename T>
inline auto operator==(cfire::None_t, const std::optional<T>& opt) noexcept -> bool { return !opt.has_value(); }
template <typename T>
inline auto operator!=(cfire::None_t, const std::optional<T>& opt) noexcept -> bool { return opt.has_value(); }

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

namespace cfire::detail {

// --- Secure temporary storage & per-user caches (CF-153) ---
// Never use a predictable or shared name under the temp directory: on a multi-user machine another
// account can pre-create it (symlink attacks, cache poisoning, reading private documents). Temp data
// lives in a private directory created atomically (mkdtemp, mode 0700) that is removed on scope exit.
class SecureTempDir {
public:
    explicit SecureTempDir(std::string_view prefix = "cfire") {
        std::error_code ec;
        auto base = std::filesystem::temp_directory_path(ec);
        if (ec || base.empty()) base = std::filesystem::current_path(ec);
#if !defined(_WIN32)
        std::string tmpl = (base / (std::string(prefix) + "_XXXXXX")).string();
        if (char* res = ::mkdtemp(tmpl.data())) path_ = res;
#else
        std::random_device rd;
        for (int attempt = 0; attempt < 64 && path_.empty(); ++attempt) {
            auto cand = base / std::format("{}_{:016x}", prefix, (static_cast<std::uint64_t>(rd()) << 32) ^ rd());
            if (std::filesystem::create_directory(cand, ec)) path_ = cand;  // false if it already exists
        }
#endif
    }
    ~SecureTempDir() {
        if (!path_.empty() && !keep_) {
            std::error_code ec;
            std::filesystem::remove_all(path_, ec);
        }
    }
    SecureTempDir(const SecureTempDir&) = delete;
    SecureTempDir& operator=(const SecureTempDir&) = delete;
    SecureTempDir(SecureTempDir&& o) noexcept : path_(std::exchange(o.path_, {})), keep_(o.keep_) {}

    [[nodiscard]] auto ok() const noexcept -> bool { return !path_.empty(); }
    [[nodiscard]] auto path() const noexcept -> const std::filesystem::path& { return path_; }
    [[nodiscard]] auto file(std::string_view name) const -> std::filesystem::path { return path_ / name; }
    // Leaves the directory in place (e.g. a build output the caller hands to another process).
    auto release() noexcept -> std::filesystem::path { keep_ = true; return path_; }

private:
    std::filesystem::path path_{};
    bool keep_ = false;
};

// Per-user cache root for `sub` ($XDG_CACHE_HOME, ~/Library/Caches, %LOCALAPPDATA%), created with
// mode 0700. Returns an empty path when no per-user location exists; callers must then fall back to a
// per-process SecureTempDir rather than a shared temp-dir name.
[[nodiscard]] inline auto user_cache_dir(std::string_view sub) -> std::filesystem::path {
    auto env = [](const char* k) -> std::string {
        const char* v = std::getenv(k);
        return v ? std::string(v) : std::string();
    };
    std::filesystem::path root;
#if defined(_WIN32)
    if (auto la = env("LOCALAPPDATA"); !la.empty()) root = std::filesystem::path(la) / "cfire";
#else
    if (auto xdg = env("XDG_CACHE_HOME"); !xdg.empty() && xdg.starts_with("/")) {
        root = std::filesystem::path(xdg) / "cfire";
    } else if (auto home = env("HOME"); !home.empty()) {
#if defined(__APPLE__)
        root = std::filesystem::path(home) / "Library" / "Caches" / "cfire";
#else
        root = std::filesystem::path(home) / ".cache" / "cfire";
#endif
    }
#endif
    if (root.empty()) return {};
    auto dir = sub.empty() ? root : root / sub;
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    if (ec || !std::filesystem::is_directory(dir, ec)) return {};
#if !defined(_WIN32)
    ::chmod(root.c_str(), 0700);
    ::chmod(dir.c_str(), 0700);
#endif
    return dir;
}

} // namespace cfire::detail

namespace cfire {

// --- Error Type for T! (std::expected<T, cfire::Error>) ---
class Error {
public:
    Error(std::string msg, int code = 0) : msg_(std::move(msg)), code_(code) {}
    Error(std::string_view msg, int code = 0) : msg_(msg), code_(code) {}
    Error(const char* msg, int code = 0) : msg_(msg ? msg : ""), code_(code) {}
    [[nodiscard]] auto message() const noexcept -> const std::string& { return msg_; }
    [[nodiscard]] auto code() const noexcept -> int { return code_; }
    [[nodiscard]] auto size() const noexcept -> std::size_t { return msg_.size(); }
    [[nodiscard]] auto data() const noexcept -> const char* { return msg_.data(); }
    [[nodiscard]] auto c_str() const noexcept -> const char* { return msg_.c_str(); }
private:
    std::string msg_;
    int code_ = 0;
};

[[nodiscard]] inline auto make_error(std::string msg, int code = 0) -> std::unexpected<Error> {
    return std::unexpected<Error>(Error(std::move(msg), code));
}

[[nodiscard]] inline auto make_error(std::string_view msg, int code = 0) -> std::unexpected<Error> {
    return std::unexpected<Error>(Error(std::string(msg), code));
}

[[nodiscard]] inline auto make_error(const char* msg, int code = 0) -> std::unexpected<Error> {
    return std::unexpected<Error>(Error(msg ? std::string(msg) : std::string{}, code));
}

[[nodiscard]] inline auto make_error(Error err) -> std::unexpected<Error> {
    return std::unexpected<Error>(std::move(err));
}

[[nodiscard]] inline auto make_error(std::unexpected<Error> err) -> std::unexpected<Error> {
    return err;
}

[[nodiscard]] inline auto error(std::string msg, int code = 0) -> std::unexpected<Error> {
    return make_error(std::move(msg), code);
}

[[nodiscard]] inline auto error(std::string_view msg, int code = 0) -> std::unexpected<Error> {
    return make_error(msg, code);
}

[[nodiscard]] inline auto error(const char* msg, int code = 0) -> std::unexpected<Error> {
    return make_error(msg, code);
}

// --- Runtime-evaluated format / print fallback for non-literal format strings ---
template <typename... Args>
inline void runtime_println(std::string_view fmt, Args&&... args) {
    std::string s = std::vformat(fmt, std::make_format_args(args...));
    std::println("{}", s);
}

template <typename... Args>
inline void runtime_println(std::FILE* stream, std::string_view fmt, Args&&... args) {
    std::string s = std::vformat(fmt, std::make_format_args(args...));
    std::println(stream, "{}", s);
}

template <typename... Args>
inline void runtime_print(std::string_view fmt, Args&&... args) {
    std::string s = std::vformat(fmt, std::make_format_args(args...));
    std::print("{}", s);
}

template <typename... Args>
inline void runtime_print(std::FILE* stream, std::string_view fmt, Args&&... args) {
    std::string s = std::vformat(fmt, std::make_format_args(args...));
    std::print(stream, "{}", s);
}

template <typename... Args>
inline auto runtime_format(std::string_view fmt, Args&&... args) -> std::string {
    return std::vformat(fmt, std::make_format_args(args...));
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
    using key_type = K;
    using mapped_type = V;
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
    void clear() noexcept { map_.clear(); }
    auto begin() noexcept { return map_.begin(); }
    auto end() noexcept { return map_.end(); }
    auto begin() const noexcept { return map_.begin(); }
    auto end() const noexcept { return map_.end(); }

private:
    underlying_t map_{};
};

// --- Zero-Copy Slicing (`s[start..end]`) for `str`, `[]T`, `[N]T`, and `bytes` ---
// Q12 (CF-147): `xs[a..b]` panics on an invalid range (a > b, b > len, negative bound), exactly like
// indexing; `str` slices also panic when a bound falls inside a UTF-8 code point. The location is the
// caller's `.cf` line via #line + std::source_location.
inline void assert_true_at(bool cond, std::string_view msg, std::string_view file, int line, std::string_view expr_text);

// --- `make([]T, len[, cap])` (Q25): `len` zero-filled elements, `cap` reserved; negative sizes panic ---
template <typename T, std::integral L>
[[nodiscard]] inline auto make_slice(L len, const std::source_location& loc = std::source_location::current()) -> std::vector<T> {
    if (std::cmp_less(len, 0)) [[unlikely]] {
        assert_true_at(false, std::format("make: negative slice length {}", len), loc.file_name(), static_cast<int>(loc.line()), "negative slice length");
    }
    return std::vector<T>(static_cast<std::size_t>(len));
}
template <typename T, std::integral L, std::integral C>
[[nodiscard]] inline auto make_slice(L len, C cap, const std::source_location& loc = std::source_location::current()) -> std::vector<T> {
    if (std::cmp_less(len, 0) || std::cmp_less(cap, len)) [[unlikely]] {
        assert_true_at(false, std::format("make: invalid slice length {} / capacity {}", len, cap), loc.file_name(), static_cast<int>(loc.line()), "invalid slice capacity");
    }
    std::vector<T> v;
    v.reserve(static_cast<std::size_t>(cap));
    v.resize(static_cast<std::size_t>(len));
    return v;
}


namespace detail {
template <std::integral A, std::integral B>
inline auto checked_slice_bounds(A a, B b, std::size_t len, const std::source_location& loc) -> std::pair<std::size_t, std::size_t> {
    if (std::cmp_less(a, 0) || std::cmp_less(b, 0) || std::cmp_greater(a, b) || std::cmp_greater(b, len)) [[unlikely]] {
        assert_true_at(false, std::format("slice range {}..{} out of bounds for length {}", a, b, len),
            loc.file_name(), static_cast<int>(loc.line()), "slice range out of bounds");
    }
    return {static_cast<std::size_t>(a), static_cast<std::size_t>(b)};
}
inline void check_utf8_boundary(std::string_view sv, std::size_t pos, const std::source_location& loc) {
    // 0 and len() are always boundaries, even for data that is not valid UTF-8.
    if (pos > 0 && pos < sv.size() && (static_cast<unsigned char>(sv[pos]) & 0xC0u) == 0x80u) [[unlikely]] {
        assert_true_at(false, std::format("str slice bound {} is not on a UTF-8 code point boundary", pos),
            loc.file_name(), static_cast<int>(loc.line()), "slice splits a UTF-8 code point");
    }
}
} // namespace detail

template <std::integral A, std::integral B>
[[nodiscard]] inline auto slice(std::string_view sv CFIRE_LIFETIMEBOUND, A start, B end,
                                std::source_location loc = std::source_location::current()) -> str_ref {
    auto [s, e] = detail::checked_slice_bounds(start, end, sv.size(), loc);
    detail::check_utf8_boundary(sv, s, loc);
    detail::check_utf8_boundary(sv, e, loc);
    return sv.substr(s, e - s);
}

template <typename T, std::integral A, std::integral B>
[[nodiscard]] inline auto slice(const std::vector<T>& vec CFIRE_LIFETIMEBOUND, A start, B end,
                                std::source_location loc = std::source_location::current()) -> std::span<const T> {
    auto [s, e] = detail::checked_slice_bounds(start, end, vec.size(), loc);
    return std::span<const T>(vec.data() + s, e - s);
}

template <typename T, std::integral A, std::integral B>
[[nodiscard]] inline auto slice(std::span<const T> sp CFIRE_LIFETIMEBOUND, A start, B end,
                                std::source_location loc = std::source_location::current()) -> std::span<const T> {
    auto [s, e] = detail::checked_slice_bounds(start, end, sp.size(), loc);
    return sp.subspan(s, e - s);
}

template <typename T, std::size_t N, std::integral A, std::integral B>
[[nodiscard]] inline auto slice(const std::array<T, N>& arr CFIRE_LIFETIMEBOUND, A start, B end,
                                std::source_location loc = std::source_location::current()) -> std::span<const T> {
    auto [s, e] = detail::checked_slice_bounds(start, end, N, loc);
    return std::span<const T>(arr.data() + s, e - s);
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
#if defined(_WIN32)
[[nodiscard]] inline auto c_str(const std::filesystem::path& p) noexcept -> CStrViewHolder {
    auto u8 = p.u8string();
    return CStrViewHolder(std::string_view(reinterpret_cast<const char*>(u8.data()), u8.size()));
}
#else
[[nodiscard]] inline auto c_str(const std::filesystem::path& p CFIRE_LIFETIMEBOUND) noexcept -> const char* {
    return p.c_str();
}
#endif
[[nodiscard]] inline auto c_str(const char* s CFIRE_LIFETIMEBOUND) noexcept -> const char* {
    return s;
}
[[nodiscard]] inline auto c_str(std::string_view sv) noexcept -> CStrViewHolder {
    return CStrViewHolder(sv);
}
[[nodiscard]] inline auto c_arg(const std::string& s CFIRE_LIFETIMEBOUND) noexcept -> const char* {
    return s.c_str();
}
#if defined(_WIN32)
[[nodiscard]] inline auto c_arg(const std::filesystem::path& p) noexcept -> CStrViewHolder {
    auto u8 = p.u8string();
    return CStrViewHolder(std::string_view(reinterpret_cast<const char*>(u8.data()), u8.size()));
}
#else
[[nodiscard]] inline auto c_arg(const std::filesystem::path& p CFIRE_LIFETIMEBOUND) noexcept -> const char* {
    return p.c_str();
}
#endif
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
              !std::is_same_v<std::remove_cvref_t<T>, cfire::str_ref> &&
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

template <typename T, typename U = T>
inline void vec_push(std::vector<T>& vec, U&& val) {
    if constexpr (std::is_same_v<T, std::byte> && std::is_integral_v<std::remove_cvref_t<U>>) {
        vec.push_back(static_cast<std::byte>(val));
    } else if constexpr (std::is_same_v<T, std::uint8_t> && std::is_same_v<std::remove_cvref_t<U>, std::byte>) {
        vec.push_back(static_cast<std::uint8_t>(val));
    } else {
        vec.push_back(std::forward<U>(val));
    }
}

template <typename C, typename U>
requires requires(C& c, U&& u) { c.push(std::forward<U>(u)); }
inline void vec_push(C& c, U&& val) {
    c.push(std::forward<U>(val));
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

#if defined(_WIN32)
inline void ensure_winsock() noexcept {
    static const bool initialized = []() noexcept {
        WSADATA wsa{};
        return ::WSAStartup(MAKEWORD(2, 2), &wsa) == 0;
    }();
    (void)initialized;
}

inline auto win32_socket_table_mu() -> std::mutex& {
    static std::mutex mu;
    return mu;
}

inline auto win32_socket_table() -> std::unordered_map<int, SOCKET>& {
    static std::unordered_map<int, SOCKET> tbl;
    return tbl;
}

[[nodiscard]] inline auto register_win32_socket_fd(SOCKET s) -> int {
    static std::atomic<int> next_fd{100000};
    int fd = next_fd.fetch_add(1, std::memory_order_relaxed);
    std::lock_guard<std::mutex> lk(win32_socket_table_mu());
    win32_socket_table()[fd] = s;
    return fd;
}

[[nodiscard]] inline auto lookup_win32_socket_fd(int fd, SOCKET& out) -> bool {
    std::lock_guard<std::mutex> lk(win32_socket_table_mu());
    auto it = win32_socket_table().find(fd);
    if (it == win32_socket_table().end()) return false;
    out = it->second;
    return true;
}

[[nodiscard]] inline auto take_win32_socket_fd(int fd, SOCKET& out) -> bool {
    std::lock_guard<std::mutex> lk(win32_socket_table_mu());
    auto it = win32_socket_table().find(fd);
    if (it == win32_socket_table().end()) return false;
    out = it->second;
    win32_socket_table().erase(it);
    return true;
}

[[nodiscard]] inline auto win32_create_socketpair(SOCKET out_pair[2]) noexcept -> bool {
    ensure_winsock();
    SOCKET listener = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listener == INVALID_SOCKET) return false;
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;
    if (::bind(listener, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0 ||
        ::listen(listener, 1) != 0) {
        ::closesocket(listener);
        return false;
    }
    int len = sizeof(addr);
    if (::getsockname(listener, reinterpret_cast<sockaddr*>(&addr), &len) != 0) {
        ::closesocket(listener);
        return false;
    }
    SOCKET client = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (client == INVALID_SOCKET) {
        ::closesocket(listener);
        return false;
    }
    if (::connect(client, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        ::closesocket(client);
        ::closesocket(listener);
        return false;
    }
    SOCKET server = ::accept(listener, nullptr, nullptr);
    ::closesocket(listener);
    if (server == INVALID_SOCKET) {
        ::closesocket(client);
        return false;
    }
    out_pair[0] = client;
    out_pair[1] = server;
    return true;
}
#endif

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
    SOCKET s = INVALID_SOCKET;
    if (lookup_win32_socket_fd(fd, s)) {
        int chunk = static_cast<int>(std::min<std::size_t>(buf.size(), 0x7fffffffU));
        int n = ::recv(s, reinterpret_cast<char*>(buf.data()), chunk, 0);
        if (n < 0) {
            return make_error(std::format("fd_read({}) socket recv failed: {}", fd, ::WSAGetLastError()));
        }
        return static_cast<std::size_t>(n);
    }
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
    SOCKET s = INVALID_SOCKET;
    if (lookup_win32_socket_fd(fd, s)) {
        std::size_t total = 0;
        while (total < data.size()) {
            int chunk = static_cast<int>(std::min<std::size_t>(data.size() - total, 0x7fffffffU));
            int n = ::send(s, reinterpret_cast<const char*>(data.data() + total), chunk, 0);
            if (n <= 0) {
                return make_error(std::format("fd_write({}) socket send failed: {}", fd, ::WSAGetLastError()));
            }
            total += static_cast<std::size_t>(n);
        }
        return total;
    }
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

[[nodiscard]] inline auto fd_read_exact(
    int fd,
    std::size_t exact_bytes
) -> std::expected<std::vector<std::byte>, Error> {
    if (fd < 0) return make_error("invalid file descriptor");
    std::vector<std::byte> buf(exact_bytes);
    std::size_t total = 0;
    while (total < exact_bytes) {
        auto n = fd_read(fd, std::span<std::byte>(buf.data() + total, exact_bytes - total));
        if (!n) return std::unexpected(n.error());
        if (*n == 0) {
            return make_error(std::format("unexpected EOF in read_exact (expected {} bytes, got {})", exact_bytes, total));
        }
        total += *n;
    }
    return buf;
}

[[nodiscard]] inline auto fd_read_until(
    int fd,
    std::byte delim,
    std::size_t max_bytes = 65536
) -> std::expected<std::vector<std::byte>, Error> {
    if (fd < 0) return make_error("invalid file descriptor");
    std::vector<std::byte> out;
    std::byte ch{0};
    while (out.size() < max_bytes) {
        auto n = fd_read(fd, std::span<std::byte>(&ch, 1));
        if (!n) return std::unexpected(n.error());
        if (*n == 0) break;
        out.push_back(ch);
        if (ch == delim) break;
    }
    return out;
}

[[nodiscard]] inline auto fd_close(int fd) -> std::expected<void, Error> {
    if (fd < 0) return make_error("cannot close negative file descriptor");
#if defined(_WIN32)
    SOCKET s = INVALID_SOCKET;
    if (take_win32_socket_fd(fd, s)) {
        if (::closesocket(s) != 0) {
            return make_error(std::format("fd_close({}) closesocket failed: {}", fd, ::WSAGetLastError()));
        }
        return {};
    }
    if (::_close(fd) != 0) {
        return make_error(std::format("fd_close({}) failed: {}", fd, std::strerror(errno)));
    }
    return {};
#else
    while (::close(fd) != 0) {
        if (errno == EINTR) continue;
        return make_error(std::format("fd_close({}) failed: {}", fd, std::strerror(errno)));
    }
    return {};
#endif
}

[[nodiscard]] inline auto fd_is_tty(int fd) noexcept -> bool {
#if defined(_WIN32)
    return fd >= 0 && ::_isatty(fd) != 0;
#else
    return fd >= 0 && ::isatty(fd) != 0;
#endif
}

[[nodiscard]] inline auto fd_poll(int fd, std::int64_t timeout_ms = 0) noexcept -> bool {
    if (fd < 0) return false;
#if defined(_WIN32)
    SOCKET s = INVALID_SOCKET;
    if (lookup_win32_socket_fd(fd, s)) {
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(s, &rfds);
        timeval tv{};
        timeval* tvp = nullptr;
        if (timeout_ms >= 0) {
            tv.tv_sec = static_cast<long>(timeout_ms / 1000);
            tv.tv_usec = static_cast<long>((timeout_ms % 1000) * 1000);
            tvp = &tv;
        }
        int rc = ::select(0, &rfds, nullptr, nullptr, tvp);
        return rc > 0;
    }
    intptr_t osf = ::_get_osfhandle(fd);
    if (osf == -1) return false;
    HANDLE h = reinterpret_cast<HANDLE>(osf);
    if (::GetFileType(h) == FILE_TYPE_PIPE) {
        auto start = std::chrono::steady_clock::now();
        while (true) {
            DWORD avail = 0;
            if (!::PeekNamedPipe(h, nullptr, 0, nullptr, &avail, nullptr)) {
                return true; // Pipe broken / EOF -> readable
            }
            if (avail > 0) return true;
            if (timeout_ms == 0) return false;
            if (timeout_ms > 0) {
                auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - start).count();
                if (elapsed >= timeout_ms) return false;
            }
            ::Sleep(1);
        }
    }
    DWORD wait_ms = timeout_ms < 0 ? INFINITE : static_cast<DWORD>(timeout_ms);
    return ::WaitForSingleObject(h, wait_ms) == WAIT_OBJECT_0;
#elif defined(__EMSCRIPTEN__)
    (void)timeout_ms;
    return true;
#else
    struct pollfd pfd{};
    pfd.fd = fd;
    pfd.events = POLLIN;
    int rc = 0;
    while ((rc = ::poll(&pfd, 1, static_cast<int>(timeout_ms))) < 0 && errno == EINTR) {}
    return rc > 0 && (pfd.revents & (POLLIN | POLLHUP)) != 0;
#endif
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
        SOCKET s = INVALID_SOCKET;
        int n = lookup_win32_socket_fd(fd, s) ? ::recv(s, &ch, 1, 0) : ::_read(fd, &ch, 1);
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

// --- High-Performance Zero-Allocation Exception-Free Integer Parsing (<charconv>) ---
namespace detail {

#if defined(_WIN32)
struct Win32ConsoleAndStdioInit {
    Win32ConsoleAndStdioInit() noexcept {
        ::SetConsoleOutputCP(CP_UTF8);
        ::SetConsoleCP(CP_UTF8);
        (void)::_setmode(0, _O_BINARY);
        (void)::_setmode(1, _O_BINARY);
        (void)::_setmode(2, _O_BINARY);
    }
};
inline Win32ConsoleAndStdioInit g_win32_console_and_stdio_init{};
#endif

inline auto process_args_storage() -> std::vector<std::string>& {
    static std::vector<std::string> s_args;
    return s_args;
}

inline void set_process_args(int argc, char** argv) {
    auto& out = process_args_storage();
    out.clear();
#if defined(_WIN32)
    // argv from main() on Windows is encoded in the legacy ANSI codepage. Read the UTF-16
    // command line via CommandLineToArgvW(GetCommandLineW()) and convert each arg to UTF-8.
    int wargc = 0;
    if (LPWSTR* wargv = ::CommandLineToArgvW(::GetCommandLineW(), &wargc)) {
        out.reserve(static_cast<std::size_t>(wargc));
        for (int i = 0; i < wargc; ++i) {
            int wlen = static_cast<int>(::wcslen(wargv[i]));
            int len = ::WideCharToMultiByte(CP_UTF8, 0, wargv[i], wlen, nullptr, 0, nullptr, nullptr);
            std::string a(static_cast<std::size_t>(len > 0 ? len : 0), '\0');
            if (len > 0) {
                ::WideCharToMultiByte(CP_UTF8, 0, wargv[i], wlen, a.data(), len, nullptr, nullptr);
            }
            out.push_back(std::move(a));
        }
        ::LocalFree(wargv);
        return;
    }
#endif
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

template <typename C>
struct is_map_like {
private:
    template <typename T, typename = typename std::remove_cvref_t<T>::mapped_type>
    static auto test(int) -> std::true_type;
    template <typename>
    static auto test(...) -> std::false_type;
public:
    static constexpr bool value = decltype(test<C>(0))::value;
};

template <typename C>
inline constexpr bool is_map_like_v = is_map_like<C>::value;

template <typename Iter>
struct EnumerateIterator {
    std::size_t index = 0;
    Iter it;
    bool operator!=(const EnumerateIterator& o) const { return it != o.it; }
    EnumerateIterator& operator++() { ++index; ++it; return *this; }
    auto operator*() const {
        return std::pair<std::size_t, decltype(*it)>(index, *it);
    }
};

template <typename C>
struct EnumerateView {
    C& c;
    auto begin() { return EnumerateIterator<decltype(std::begin(c))>{0, std::begin(c)}; }
    auto end() { return EnumerateIterator<decltype(std::end(c))>{0, std::end(c)}; }
    auto begin() const { return EnumerateIterator<decltype(std::begin(c))>{0, std::begin(c)}; }
    auto end() const { return EnumerateIterator<decltype(std::end(c))>{0, std::end(c)}; }
};

template <typename C>
decltype(auto) iter_pairs(C& c) {
    if constexpr (is_map_like_v<C>) {
        return (c);
    } else {
        return EnumerateView<C>{c};
    }
}

} // namespace detail

// --- Structured Concurrency (`async.scope`) ---
namespace async {

struct ScopeState;

template <typename T>
class Task;

struct ScopeState : public std::enable_shared_from_this<ScopeState> {
    std::mutex queue_mu;
    std::condition_variable cv;
    std::condition_variable done_cv;
    std::deque<std::function<void()>> queue;
    std::vector<std::thread> workers;
    std::atomic<std::size_t> active_tasks{0};
    std::atomic<bool> stopping{false};
    std::vector<std::function<void()>> defers;
    std::mutex state_mu;
    std::exception_ptr first_error{nullptr};

    inline static thread_local ScopeState* current_scope_state = nullptr;

    explicit ScopeState(std::size_t limit) {
        if (limit == 0) {
            limit = std::max(1u, std::thread::hardware_concurrency());
        }
        for (std::size_t i = 0; i < limit; ++i) {
            workers.emplace_back([this] {
                current_scope_state = this;
                while (true) {
                    std::function<void()> task;
                    {
                        std::unique_lock<std::mutex> lk(queue_mu);
                        cv.wait(lk, [this] { return stopping.load() || !queue.empty(); });
                        if (stopping.load() && queue.empty()) {
                            break;
                        }
                        if (!queue.empty()) {
                            task = std::move(queue.front());
                            queue.pop_front();
                        }
                    }
                    if (task) {
                        task();
                        --active_tasks;
                        done_cv.notify_all();
                    }
                }
            });
        }
    }

    ~ScopeState() {
        stop_and_join();
    }

    void post(std::function<void()> task) {
        {
            std::lock_guard<std::mutex> lk(queue_mu);
            ++active_tasks;
            queue.push_back(std::move(task));
        }
        cv.notify_one();
    }

    auto help_one_task() -> bool {
        std::function<void()> task;
        {
            std::lock_guard<std::mutex> lk(queue_mu);
            if (queue.empty()) return false;
            task = std::move(queue.front());
            queue.pop_front();
        }
        if (task) {
            task();
            --active_tasks;
            done_cv.notify_all();
            return true;
        }
        return false;
    }

    void record_error(std::exception_ptr ep) {
        if (!ep) return;
        std::lock_guard<std::mutex> lk(state_mu);
        if (!first_error) first_error = ep;
    }

    void stop_and_join() {
        {
            std::lock_guard<std::mutex> lk(queue_mu);
            stopping.store(true);
        }
        cv.notify_all();
        for (auto& w : workers) {
            if (w.joinable()) {
                w.join();
            }
        }
        workers.clear();
    }
};

template <typename T>
class Task {
public:
    struct Shared {
        std::mutex mu;
        std::condition_variable cv;
        bool done = false;
        std::exception_ptr err = nullptr;
        std::optional<T> value;
        std::shared_ptr<ScopeState> scope_state;
    };

    Task() = default;
    explicit Task(std::shared_ptr<Shared> st) : state_(std::move(st)) {}

    auto join() -> T {
        if (!state_) return T{};
        auto st = state_;
        while (!st->done) {
            if (st->scope_state && ScopeState::current_scope_state == st->scope_state.get()) {
                if (!st->scope_state->help_one_task()) {
                    std::unique_lock<std::mutex> lk(st->mu);
                    st->cv.wait_for(lk, std::chrono::milliseconds(2), [&] { return st->done; });
                }
            } else {
                std::unique_lock<std::mutex> lk(st->mu);
                st->cv.wait(lk, [&] { return st->done; });
            }
        }
        if (st->err) {
            std::rethrow_exception(st->err);
        }
        return std::move(*st->value);
    }

private:
    std::shared_ptr<Shared> state_;
};

template <>
class Task<void> {
public:
    struct Shared {
        std::mutex mu;
        std::condition_variable cv;
        bool done = false;
        std::exception_ptr err = nullptr;
        std::shared_ptr<ScopeState> scope_state;
    };

    Task() = default;
    explicit Task(std::shared_ptr<Shared> st) : state_(std::move(st)) {}

    void join() {
        if (!state_) return;
        auto st = state_;
        while (!st->done) {
            if (st->scope_state && ScopeState::current_scope_state == st->scope_state.get()) {
                if (!st->scope_state->help_one_task()) {
                    std::unique_lock<std::mutex> lk(st->mu);
                    st->cv.wait_for(lk, std::chrono::milliseconds(2), [&] { return st->done; });
                }
            } else {
                std::unique_lock<std::mutex> lk(st->mu);
                st->cv.wait(lk, [&] { return st->done; });
            }
        }
        if (st->err) {
            std::rethrow_exception(st->err);
        }
    }

private:
    std::shared_ptr<Shared> state_;
};

class Scope {
public:
    explicit Scope(std::size_t max_workers = 0)
        : state_(std::make_shared<ScopeState>(max_workers)),
          is_owner_(true) {}

    Scope(const Scope& other) noexcept
        : state_(other.state_), is_owner_(false) {}

    auto operator=(const Scope& other) noexcept -> Scope& {
        if (this != &other) {
            if (is_owner_) join_all();
            state_ = other.state_;
            is_owner_ = false;
        }
        return *this;
    }

    Scope(Scope&& other) noexcept
        : state_(std::move(other.state_)),
          is_owner_(std::exchange(other.is_owner_, false)) {}

    auto operator=(Scope&& other) noexcept -> Scope& {
        if (this != &other) {
            if (is_owner_) join_all();
            state_ = std::move(other.state_);
            is_owner_ = std::exchange(other.is_owner_, false);
        }
        return *this;
    }

    ~Scope() {
        if (is_owner_) {
            join_all();
        }
    }

    template <typename F>
    auto spawn(F&& task) {
        using R = std::decay_t<std::invoke_result_t<std::decay_t<F>&>>;
        if constexpr (std::is_void_v<R>) {
            auto task_state = std::make_shared<typename Task<void>::Shared>();
            task_state->scope_state = state_;
            if (state_) {
                state_->post([task_state, fn = std::forward<F>(task)]() mutable {
                    try {
                        fn();
                    } catch (...) {
                        task_state->err = std::current_exception();
                        if (task_state->scope_state) {
                            task_state->scope_state->record_error(task_state->err);
                        }
                    }
                    {
                        std::lock_guard<std::mutex> lk(task_state->mu);
                        task_state->done = true;
                    }
                    task_state->cv.notify_all();
                });
            }
            return Task<void>(task_state);
        } else {
            auto task_state = std::make_shared<typename Task<R>::Shared>();
            task_state->scope_state = state_;
            if (state_) {
                state_->post([task_state, fn = std::forward<F>(task)]() mutable {
                    try {
                        task_state->value = fn();
                    } catch (...) {
                        task_state->err = std::current_exception();
                        if (task_state->scope_state) {
                            task_state->scope_state->record_error(task_state->err);
                        }
                    }
                    {
                        std::lock_guard<std::mutex> lk(task_state->mu);
                        task_state->done = true;
                    }
                    task_state->cv.notify_all();
                });
            }
            return Task<R>(task_state);
        }
    }

    template <typename F>
    void defer(F&& fn) {
        if (!state_) return;
        std::lock_guard<std::mutex> lk(state_->state_mu);
        state_->defers.emplace_back(std::forward<F>(fn));
    }

    void join_all() {
        if (!state_) return;
        while (state_->active_tasks.load() > 0) {
            if (!state_->help_one_task()) {
                std::unique_lock<std::mutex> lk(state_->queue_mu);
                state_->done_cv.wait_for(lk, std::chrono::milliseconds(2), [this] {
                    return state_->active_tasks.load() == 0;
                });
            }
        }
        state_->stop_and_join();

        std::vector<std::function<void()>> defers;
        {
            std::lock_guard<std::mutex> lk(state_->state_mu);
            defers.swap(state_->defers);
        }
        while (!defers.empty()) {
            auto fn = std::move(defers.back());
            defers.pop_back();
            fn();
        }

        std::exception_ptr ep = nullptr;
        {
            std::lock_guard<std::mutex> lk(state_->state_mu);
            ep = state_->first_error;
            state_->first_error = nullptr;
        }
        if (ep) {
            std::rethrow_exception(ep);
        }
    }

private:
    std::shared_ptr<ScopeState> state_;
    bool is_owner_ = false;
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
    static_assert(requires { exp.has_value(); }, "cfire::unwrap_val requires an optional (T?) or fallible (T!) value");
    if constexpr (std::is_void_v<typename std::decay_t<E>::value_type>) {
        if (!exp.has_value()) {
            if constexpr (requires { exp.error().message(); }) {
                std::println(stderr, "panic: unwrap_val called on unexpected error: {}", exp.error().message());
            } else {
                std::println(stderr, "panic: unwrap_val called on empty optional (none)");
            }
            std::abort();
        }
        return;
    } else {
        if (!exp.has_value()) {
            if constexpr (requires { exp.error().message(); }) {
                std::println(stderr, "panic: unwrap_val called on unexpected error: {}", exp.error().message());
            } else {
                std::println(stderr, "panic: unwrap_val called on empty optional (none)");
            }
            std::abort();
        }
        return *std::forward<E>(exp);
    }
}

template <typename Exp>
auto unwrap_or_default(Exp&& exp) {
    static_assert(requires { exp.has_value(); }, "cfire::unwrap_or_default requires an optional (T?) or fallible (T!) value");
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
    static_assert(requires { exp.has_value(); }, "cfire::unwrap_or requires an optional (T?) or fallible (T!) value");
    using V = typename std::decay_t<Exp>::value_type;
    if constexpr (requires { fallback.has_value(); }) {
        if (exp.has_value()) return std::forward<Exp>(exp);
        return std::forward<U>(fallback);
    } else if constexpr (std::is_void_v<V>) {
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

template <typename T>
struct reflect_enum {
    static constexpr bool is_enum = false;
    static constexpr std::string_view name = "";
    static constexpr std::size_t variant_count = 0;
};

// --- Built-in Numeric & Primitive Type Conversion Constructors (LANG-P0-01) ---
template <typename T>
concept CfireScalarCastable =
    std::is_arithmetic_v<std::remove_cvref_t<T>> ||
    std::is_same_v<std::remove_cvref_t<T>, std::byte> ||
    std::is_enum_v<std::remove_cvref_t<T>> ||
    reflect_enum<std::remove_cvref_t<T>>::is_enum ||
    requires(const std::remove_cvref_t<T>& v) {
        { v.to_i32() } -> std::convertible_to<std::int32_t>;
    };

namespace detail {
// Q9/Q19 (CF-165): `T(x)` preserves the value or panics. Integer narrowing and sign changes that don't fit,
// and float->int that is NaN or out of range (UB in C++), panic at the caller's `.cf` location; the
// `#line` directives emitted before each statement make std::source_location report the .cf file and line.
// Widening, int->float and float->float conversions are IEEE-defined and never panic.
template <typename T>
inline constexpr bool is_char_like_v = std::is_same_v<T, char> || std::is_same_v<T, wchar_t> ||
    std::is_same_v<T, char8_t> || std::is_same_v<T, char16_t> || std::is_same_v<T, char32_t>;

// std::in_range / std::cmp_* reject character types. A `char` is a byte of a `str` (`s[i]`), so it is read
// as an unsigned 0..255 value on every platform: `u8(s[i])` never panics, and the result no longer depends
// on whether the target's `char` is signed (Apple arm64, x86) or unsigned (most other ARM targets).
template <typename T>
[[nodiscard]] constexpr auto as_comparable_int(T v) noexcept {
    using U = std::remove_cvref_t<T>;
    if constexpr (std::is_same_v<U, char>) {
        return static_cast<unsigned long long>(static_cast<unsigned char>(v));
    } else if constexpr (is_char_like_v<U>) {
        return static_cast<unsigned long long>(static_cast<std::make_unsigned_t<U>>(v));
    } else {
        return v;
    }
}

template <typename To, typename From>
[[nodiscard]] constexpr auto checked_numeric_cast(From val, const std::source_location& loc) -> To {
    using F = std::remove_cvref_t<From>;
    if constexpr (is_char_like_v<F> || is_char_like_v<To>) {
        if constexpr (is_char_like_v<F> && !is_char_like_v<To>) {
            return checked_numeric_cast<To>(as_comparable_int(val), loc);
        } else {
            return static_cast<To>(val);
        }
    } else if constexpr (std::is_enum_v<F>) {
        return checked_numeric_cast<To>(static_cast<std::underlying_type_t<F>>(val), loc);
    } else if constexpr (std::is_integral_v<To> && !std::is_same_v<To, bool> && std::is_integral_v<F> && !std::is_same_v<F, bool>) {
        if (!std::in_range<To>(val)) [[unlikely]] {
            assert_true_at(false, std::format("value {} out of range for {}-bit {} integer",
                    val, sizeof(To) * 8, std::is_signed_v<To> ? "signed" : "unsigned"),
                loc.file_name(), static_cast<int>(loc.line()), "numeric conversion out of range");
        }
        return static_cast<To>(val);
    } else if constexpr (std::is_integral_v<To> && !std::is_same_v<To, bool> && std::is_floating_point_v<F>) {
        const long double t = std::trunc(static_cast<long double>(val));
        constexpr long double lo = static_cast<long double>(std::numeric_limits<To>::min());
        // Exclusive upper bound 2^(bits-1) or 2^bits; both bounds are exact powers of two in long double.
        constexpr long double hi = std::is_signed_v<To>
            ? -lo
            : 2.0L * (static_cast<long double>(std::numeric_limits<To>::max() / 2) + 1.0L);
        if (!(t >= lo && t < hi)) [[unlikely]] {
            assert_true_at(false, std::format("value {} out of range for {}-bit {} integer",
                    val, sizeof(To) * 8, std::is_signed_v<To> ? "signed" : "unsigned"),
                loc.file_name(), static_cast<int>(loc.line()), "numeric conversion out of range");
        }
        return static_cast<To>(val);
    } else {
        return static_cast<To>(val);
    }
}
} // namespace detail

// --- Q11 (CF-166): shifts. `x << n` keeps C++'s promoted result type, but a count outside
// [0, bit width) panics at the caller's .cf location (via #line) instead of being undefined behavior.
template <typename A, typename B>
[[nodiscard]] constexpr auto shl(A a, B n, std::source_location loc = std::source_location::current()) {
    if constexpr (std::is_integral_v<A> && std::is_integral_v<B>) {
        using R = decltype(+a);
        constexpr int bits = static_cast<int>(sizeof(R) * 8);
        if (std::cmp_less(n, 0) || std::cmp_greater_equal(n, bits)) [[unlikely]] {
            assert_true_at(false, std::format("shift count {} out of range for {}-bit integer", n, bits),
                loc.file_name(), static_cast<int>(loc.line()), "shift out of range");
        }
        return static_cast<R>(static_cast<std::make_unsigned_t<R>>(a) << n);  // two's-complement wrap, as in C++20
    } else {
        return a << n;
    }
}
template <typename A, typename B>
[[nodiscard]] constexpr auto shr(A a, B n, std::source_location loc = std::source_location::current()) {
    if constexpr (std::is_integral_v<A> && std::is_integral_v<B>) {
        using R = decltype(+a);
        constexpr int bits = static_cast<int>(sizeof(R) * 8);
        if (std::cmp_less(n, 0) || std::cmp_greater_equal(n, bits)) [[unlikely]] {
            assert_true_at(false, std::format("shift count {} out of range for {}-bit integer", n, bits),
                loc.file_name(), static_cast<int>(loc.line()), "shift out of range");
        }
        return static_cast<R>(static_cast<R>(a) >> n);  // arithmetic for signed, as in C++20
    } else {
        return a >> n;
    }
}
template <std::integral A, std::integral B>
[[nodiscard]] constexpr auto wrapping_shl(A a, B n) noexcept {
    using R = decltype(+a);
    constexpr unsigned bits = sizeof(R) * 8;
    return static_cast<R>(static_cast<std::make_unsigned_t<R>>(a) << (static_cast<unsigned>(n) % bits));
}
template <std::integral A, std::integral B>
[[nodiscard]] constexpr auto wrapping_shr(A a, B n) noexcept {
    using R = decltype(+a);
    constexpr unsigned bits = sizeof(R) * 8;
    return static_cast<R>(static_cast<R>(a) >> (static_cast<unsigned>(n) % bits));
}
template <std::integral A, std::integral B>
[[nodiscard]] constexpr auto checked_shl(A a, B n) noexcept -> std::optional<decltype(+a)> {
    using R = decltype(+a);
    if (std::cmp_less(n, 0) || std::cmp_greater_equal(n, sizeof(R) * 8)) return std::nullopt;
    return static_cast<R>(static_cast<std::make_unsigned_t<R>>(a) << n);
}
template <std::integral A, std::integral B>
[[nodiscard]] constexpr auto checked_shr(A a, B n) noexcept -> std::optional<decltype(+a)> {
    using R = decltype(+a);
    if (std::cmp_less(n, 0) || std::cmp_greater_equal(n, sizeof(R) * 8)) return std::nullopt;
    return static_cast<R>(static_cast<R>(a) >> n);
}

// Explicit conversions with defined behavior when the value does not fit (`x.wrapping_as[u8]()` etc.).
template <typename To, typename From>
    requires std::is_integral_v<To> && std::is_integral_v<std::remove_cvref_t<From>>
[[nodiscard]] constexpr auto wrapping_as(From val) noexcept -> To {
    return static_cast<To>(val);  // modular since C++20
}
template <typename To, typename From>
    requires std::is_arithmetic_v<To> && std::is_arithmetic_v<std::remove_cvref_t<From>>
[[nodiscard]] constexpr auto saturating_as(From val) noexcept -> To {
    using F = std::remove_cvref_t<From>;
    if constexpr (std::is_integral_v<To> && std::is_floating_point_v<F>) {
        if (std::isnan(val)) return To{0};
        const long double t = std::trunc(static_cast<long double>(val));
        if (t <= static_cast<long double>(std::numeric_limits<To>::min())) return std::numeric_limits<To>::min();
        if (t >= static_cast<long double>(std::numeric_limits<To>::max())) return std::numeric_limits<To>::max();
        return static_cast<To>(t);
    } else if constexpr (std::is_integral_v<To> && std::is_integral_v<F> && !std::is_same_v<To, bool> && !std::is_same_v<F, bool>) {
        auto v = detail::as_comparable_int(val);
        if (std::cmp_less(v, std::numeric_limits<To>::min())) return std::numeric_limits<To>::min();
        if (std::cmp_greater(v, std::numeric_limits<To>::max())) return std::numeric_limits<To>::max();
        return static_cast<To>(val);
    } else {
        return static_cast<To>(val);
    }
}
template <typename To, typename From>
    requires std::is_arithmetic_v<To> && std::is_arithmetic_v<std::remove_cvref_t<From>>
[[nodiscard]] constexpr auto try_as(From val) noexcept -> std::optional<To> {
    using F = std::remove_cvref_t<From>;
    if constexpr (std::is_integral_v<To> && std::is_integral_v<F> && !std::is_same_v<To, bool> && !std::is_same_v<F, bool> &&
                  !detail::is_char_like_v<To>) {
        if (!std::in_range<To>(detail::as_comparable_int(val))) return std::nullopt;
    } else if constexpr (std::is_integral_v<To> && std::is_floating_point_v<F>) {
        const long double t = std::trunc(static_cast<long double>(val));
        constexpr long double lo = static_cast<long double>(std::numeric_limits<To>::min());
        constexpr long double hi = std::is_signed_v<To>
            ? -lo
            : 2.0L * (static_cast<long double>(std::numeric_limits<To>::max() / 2) + 1.0L);
        if (!(t >= lo && t < hi) || t != static_cast<long double>(val)) return std::nullopt;
    }
    return static_cast<To>(val);
}

#define CFIRE_CHECKED_SCALAR_CTOR(NAME, TYPE)                                                   \
    template <CfireScalarCastable T>                                                            \
    [[nodiscard]] constexpr auto NAME(T val, std::source_location loc = std::source_location::current()) -> TYPE { \
        return detail::checked_numeric_cast<TYPE>(val, loc);                                   \
    }
CFIRE_CHECKED_SCALAR_CTOR(i8, std::int8_t)
CFIRE_CHECKED_SCALAR_CTOR(i16, std::int16_t)
CFIRE_CHECKED_SCALAR_CTOR(i32, std::int32_t)
CFIRE_CHECKED_SCALAR_CTOR(i64, std::int64_t)
CFIRE_CHECKED_SCALAR_CTOR(u8, std::uint8_t)
CFIRE_CHECKED_SCALAR_CTOR(u16, std::uint16_t)
CFIRE_CHECKED_SCALAR_CTOR(u32, std::uint32_t)
CFIRE_CHECKED_SCALAR_CTOR(u64, std::uint64_t)
CFIRE_CHECKED_SCALAR_CTOR(usize, std::size_t)
CFIRE_CHECKED_SCALAR_CTOR(isize, std::ptrdiff_t)
#undef CFIRE_CHECKED_SCALAR_CTOR

template <CfireScalarCastable T>
[[nodiscard]] constexpr auto byte(T val, std::source_location loc = std::source_location::current()) -> std::byte {
    return static_cast<std::byte>(detail::checked_numeric_cast<std::uint8_t>(val, loc));
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
// Q18 (CF-179): only scoped enums (always a fixed underlying type) map `~E -> E`. For unscoped C enums
// the complement lies outside the enum's value range (UB on the cast), so they use the built-in `~`,
// which yields the integer; `flags &= ~FLAG` then goes through the mixed enum/integer operators below.
template <CfireIsEnum E>
    requires std::is_scoped_enum_v<E>
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
using cfire::i8;
using cfire::i16;
using cfire::i32;
using cfire::i64;
using cfire::u8;
#if !defined(_WIN32)
using cfire::byte;
#endif
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

// --- Formatter specialization for cfire::Error ---
template <typename CharT>
struct std::formatter<cfire::Error, CharT> : std::formatter<std::string, CharT> {
    template <typename FormatContext>
    auto format(const cfire::Error& e, FormatContext& ctx) const {
        return std::formatter<std::string, CharT>::format(e.message(), ctx);
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

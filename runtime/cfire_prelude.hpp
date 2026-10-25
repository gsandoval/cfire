#pragma once
// cfire_prelude.hpp — Zero-overhead C++23 Runtime Prelude for cfire (.cf)

#include <algorithm>
#include <array>
#include <csetjmp>
#include <charconv>
#include <cinttypes>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <format>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <optional>
#if __has_include(<print>)
#include <print>
#endif
#include <cstdio>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

#if !defined(__cpp_lib_print)
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

namespace cfire {

// --- Error Type for T! (std::expected<T, cfire::Error>) ---
class Error {
public:
    Error(std::string msg) : msg_(std::move(msg)) {}
    Error(std::string_view msg) : msg_(msg) {}
    Error(const char* msg) : msg_(msg) {}
    [[nodiscard]] auto message() const noexcept CFIRE_LIFETIMEBOUND -> std::string_view { return msg_; }
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

// --- Built-in `assert(cond)` for `test "..."` Blocks & Contracts ---
namespace testing_detail {
inline bool in_test_harness = false;
inline std::jmp_buf test_env;
inline bool current_test_failed = false;
inline std::string failure_file;
inline int failure_line = 0;
inline std::string failure_expr;
inline std::string failure_msg;
} // namespace testing_detail

inline void assert_true_at(
    bool cond,
    std::string_view msg,
    std::string_view file,
    int line,
    std::string_view expr_text
) {
    if (!cond) {
        if (testing_detail::in_test_harness) {
            testing_detail::current_test_failed = true;
            testing_detail::failure_file = std::string(file);
            testing_detail::failure_line = line;
            testing_detail::failure_expr = std::string(expr_text);
            testing_detail::failure_msg = std::string(msg);
            std::longjmp(testing_detail::test_env, 1);
        } else {
            if (!file.empty() && line > 0) {
                std::println(stderr, "{}:{}: panic: assertion failed: {} ({})", file, line, expr_text, msg);
            } else {
                std::println(stderr, "panic: {}", msg);
            }
            std::abort();
        }
    }
}

inline void assert_true(bool cond, std::string_view msg = "assertion failed") {
    assert_true_at(cond, msg, "", 0, "");
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
template <typename T>
    requires (!std::is_same_v<std::remove_cvref_t<T>, std::string> &&
              !std::is_same_v<std::remove_cvref_t<T>, std::string_view> &&
              !std::is_same_v<std::remove_cvref_t<T>, std::filesystem::path>)
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

template <typename T>
[[nodiscard]] inline auto str(const T& val) -> std::string {
    if constexpr (std::is_convertible_v<T, std::string_view>) {
        return std::string(val);
    } else {
        return std::format("{}", val);
    }
}

// --- High-Performance Zero-Allocation Exception-Free Integer Parsing (<charconv>) ---
namespace detail {

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

} // namespace detail

// --- Structured Concurrency (`async.scope`) ---
namespace async {

class Scope {
public:
    explicit Scope(std::size_t max_workers = 4) : max_workers_(max_workers) {}
    ~Scope() { join_all(); }

    template <typename F>
    void spawn(F&& task) {
        futures_.emplace_back(std::async(std::launch::async, std::forward<F>(task)));
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
    [[maybe_unused]] std::size_t max_workers_;
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
        return;
    } else {
        return std::forward<E>(exp).value();
    }
}

template <typename T>
struct reflect_struct {
    static constexpr std::string_view name = "";
    static constexpr std::size_t field_count = 0;
};

// --- Built-in Numeric & Primitive Type Conversion Constructors (LANG-P0-01) ---
template <typename T>
[[nodiscard]] constexpr auto i8(T val) noexcept -> std::int8_t {
    return static_cast<std::int8_t>(val);
}
template <typename T>
[[nodiscard]] constexpr auto i16(T val) noexcept -> std::int16_t {
    return static_cast<std::int16_t>(val);
}
template <typename T>
[[nodiscard]] constexpr auto i32(T val) noexcept -> std::int32_t {
    return static_cast<std::int32_t>(val);
}
template <typename T>
[[nodiscard]] constexpr auto i64(T val) noexcept -> std::int64_t {
    return static_cast<std::int64_t>(val);
}
template <typename T>
[[nodiscard]] constexpr auto u8(T val) noexcept -> std::uint8_t {
    return static_cast<std::uint8_t>(val);
}
template <typename T>
[[nodiscard]] constexpr auto byte(T val) noexcept -> std::uint8_t {
    return static_cast<std::uint8_t>(val);
}
template <typename T>
[[nodiscard]] constexpr auto u16(T val) noexcept -> std::uint16_t {
    return static_cast<std::uint16_t>(val);
}
template <typename T>
[[nodiscard]] constexpr auto u32(T val) noexcept -> std::uint32_t {
    return static_cast<std::uint32_t>(val);
}
template <typename T>
[[nodiscard]] constexpr auto u64(T val) noexcept -> std::uint64_t {
    return static_cast<std::uint64_t>(val);
}
template <typename T>
[[nodiscard]] constexpr auto usize(T val) noexcept -> std::size_t {
    return static_cast<std::size_t>(val);
}
template <typename T>
[[nodiscard]] constexpr auto isize(T val) noexcept -> std::ptrdiff_t {
    return static_cast<std::ptrdiff_t>(val);
}
template <typename T>
[[nodiscard]] constexpr auto f32(T val) noexcept -> float {
    return static_cast<float>(val);
}
template <typename T>
[[nodiscard]] constexpr auto f64(T val) noexcept -> double {
    return static_cast<double>(val);
}
template <typename T>
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

using cfire::str;
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
template <cfire::enum_operators::CfireIsEnum E>
struct std::formatter<E> : std::formatter<std::underlying_type_t<E>> {
    auto format(E e, std::format_context& ctx) const {
        return std::formatter<std::underlying_type_t<E>>::format(static_cast<std::underlying_type_t<E>>(e), ctx);
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

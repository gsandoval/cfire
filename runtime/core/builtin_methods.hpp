#pragma once
// runtime/core/builtin_methods.hpp — Methods on builtin types (Q7/Q8, W4).
//
// Every row of src/sema/builtin_methods.hpp with a runtime lowering is an overload set here:
// `xs.reverse()` lowers to `cfire::bm::reverse(xs)`. Operations that would be UB in C++ (`std::clamp` with
// lo > hi, `INT_MIN / -1`, out-of-range `insert`/`remove`) panic with the caller's `.cf` location instead.

#include "../cfire_prelude.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <concepts>
#include <initializer_list>
#include <limits>
#include <numbers>
#include <optional>
#include <source_location>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace cfire::bm {

namespace detail {
[[noreturn]] inline void fail(std::string_view msg, const std::source_location& loc) {
    cfire::assert_true_at(false, msg, loc.file_name(), static_cast<int>(loc.line()), msg);
    std::abort();
}

template <typename T>
concept Int = std::integral<T> && !std::same_as<T, bool>;
template <typename T>
concept Float = std::floating_point<T>;
template <typename T>
concept Num = Int<T> || Float<T>;

template <typename T>
struct is_vector : std::false_type {};
template <typename T, typename A>
struct is_vector<std::vector<T, A>> : std::true_type {};
template <typename T>
struct is_std_array : std::false_type {};
template <typename T, std::size_t N>
struct is_std_array<std::array<T, N>> : std::true_type {};
template <typename T>
concept Seq = is_vector<std::remove_cvref_t<T>>::value || is_std_array<std::remove_cvref_t<T>>::value;
template <typename T>
concept Vec = is_vector<std::remove_cvref_t<T>>::value;

template <typename T>
struct is_map : std::false_type {};
template <typename K, typename V>
struct is_map<cfire::Map<K, V>> : std::true_type {};
template <typename T>
concept MapLike = is_map<std::remove_cvref_t<T>>::value;

template <typename T>
concept StrLike = std::convertible_to<const T&, std::string_view> && !Seq<T> && !MapLike<T>;
} // namespace detail

// --- Size ---------------------------------------------------------------------------------------------
template <typename C>
    requires requires(const C& c) { c.size(); }
[[nodiscard]] constexpr auto len(const C& c) noexcept -> std::size_t { return c.size(); }
template <typename C>
    requires requires(const C& c) { c.size(); }
[[nodiscard]] constexpr auto is_empty(const C& c) noexcept -> bool { return c.size() == 0; }

// --- Numbers ------------------------------------------------------------------------------------------
template <detail::Int T>
[[nodiscard]] constexpr auto abs(T x, const std::source_location& loc = std::source_location::current()) -> T {
    if constexpr (std::is_signed_v<T>) {
        if (x == std::numeric_limits<T>::min()) [[unlikely]] detail::fail("integer overflow in 'abs' of the minimum value", loc);
        return x < 0 ? static_cast<T>(-x) : x;
    } else {
        return x;
    }
}
template <detail::Float T>
[[nodiscard]] inline auto abs(T x) noexcept -> T { return std::fabs(x); }

template <detail::Num T, detail::Num U>
[[nodiscard]] constexpr auto min(T a, U b) noexcept -> T {
    if constexpr (detail::Float<T>) return std::fmin(a, static_cast<T>(b));
    else return std::min(a, static_cast<T>(b));
}
template <detail::Num T, detail::Num U>
[[nodiscard]] constexpr auto max(T a, U b) noexcept -> T {
    if constexpr (detail::Float<T>) return std::fmax(a, static_cast<T>(b));
    else return std::max(a, static_cast<T>(b));
}
template <detail::Num T, detail::Num L, detail::Num H>
[[nodiscard]] constexpr auto clamp(T x, L lo_in, H hi_in, const std::source_location& loc = std::source_location::current()) -> T {
    const T lo = static_cast<T>(lo_in);
    const T hi = static_cast<T>(hi_in);
    if constexpr (detail::Float<T>) {
        if (std::isnan(lo) || std::isnan(hi)) [[unlikely]] detail::fail("'clamp' bound is NaN", loc);
    }
    if (lo > hi) [[unlikely]] detail::fail(std::format("'clamp' called with lo > hi ({} > {})", lo, hi), loc);
    if constexpr (detail::Float<T>) {
        if (std::isnan(x)) return x;
    }
    return x < lo ? lo : (hi < x ? hi : x);
}
template <detail::Num T>
[[nodiscard]] constexpr auto signum(T x) noexcept -> T {
    if constexpr (detail::Float<T>) {
        if (std::isnan(x)) return x;
        return x > 0 ? T(1) : (x < 0 ? T(-1) : x);
    } else if constexpr (std::is_signed_v<T>) {
        return static_cast<T>((x > 0) - (x < 0));
    } else {
        return static_cast<T>(x > 0);
    }
}
template <detail::Int T, detail::Int E>
[[nodiscard]] constexpr auto pow(T base, E exp_in, const std::source_location& loc = std::source_location::current()) -> T {
    if (exp_in < 0) [[unlikely]] detail::fail("integer 'pow' with a negative exponent", loc);
    T result = 1;
    T b = base;
    auto e = static_cast<std::uint64_t>(exp_in);
    while (e > 0) {
        if (e & 1u) {
            if (__builtin_mul_overflow(result, b, &result)) [[unlikely]] detail::fail("integer overflow in 'pow'", loc);
        }
        e >>= 1u;
        if (e > 0 && __builtin_mul_overflow(b, b, &b)) [[unlikely]] detail::fail("integer overflow in 'pow'", loc);
    }
    return result;
}
template <detail::Float T, detail::Num E>
[[nodiscard]] inline auto pow(T base, E e) noexcept -> T { return std::pow(base, static_cast<T>(e)); }

template <detail::Int T>
[[nodiscard]] constexpr auto count_ones(T x) noexcept -> std::uint32_t {
    return static_cast<std::uint32_t>(std::popcount(static_cast<std::make_unsigned_t<T>>(x)));
}
template <detail::Int T>
[[nodiscard]] constexpr auto leading_zeros(T x) noexcept -> std::uint32_t {
    return static_cast<std::uint32_t>(std::countl_zero(static_cast<std::make_unsigned_t<T>>(x)));
}
template <detail::Int T>
[[nodiscard]] constexpr auto trailing_zeros(T x) noexcept -> std::uint32_t {
    return static_cast<std::uint32_t>(std::countr_zero(static_cast<std::make_unsigned_t<T>>(x)));
}
template <detail::Int T>
[[nodiscard]] constexpr auto is_power_of_two(T x) noexcept -> bool {
    if constexpr (std::is_signed_v<T>) {
        if (x <= 0) return false;
    }
    return std::has_single_bit(static_cast<std::make_unsigned_t<T>>(x));
}

template <detail::Int T, detail::Int U>
[[nodiscard]] constexpr auto checked_add(T a, U b) noexcept -> std::optional<T> {
    T r;
    if (__builtin_add_overflow(a, b, &r)) return std::nullopt;
    return r;
}
template <detail::Int T, detail::Int U>
[[nodiscard]] constexpr auto checked_sub(T a, U b) noexcept -> std::optional<T> {
    T r;
    if (__builtin_sub_overflow(a, b, &r)) return std::nullopt;
    return r;
}
template <detail::Int T, detail::Int U>
[[nodiscard]] constexpr auto checked_mul(T a, U b) noexcept -> std::optional<T> {
    T r;
    if (__builtin_mul_overflow(a, b, &r)) return std::nullopt;
    return r;
}
template <detail::Int T, detail::Int U>
[[nodiscard]] constexpr auto checked_div(T a, U b_in) noexcept -> std::optional<T> {
    if (!std::in_range<T>(b_in)) return std::nullopt;
    const T b = static_cast<T>(b_in);
    if (b == 0) return std::nullopt;
    if constexpr (std::is_signed_v<T>) {
        if (a == std::numeric_limits<T>::min() && b == T(-1)) return std::nullopt;
    }
    return static_cast<T>(a / b);
}
template <detail::Int T>
[[nodiscard]] constexpr auto checked_neg(T a) noexcept -> std::optional<T> {
    T r;
    if (__builtin_sub_overflow(T(0), a, &r)) return std::nullopt;
    return r;
}
template <detail::Int T>
[[nodiscard]] constexpr auto checked_abs(T a) noexcept -> std::optional<T> {
    if constexpr (std::is_signed_v<T>) {
        if (a == std::numeric_limits<T>::min()) return std::nullopt;
        return a < 0 ? static_cast<T>(-a) : a;
    } else {
        return a;
    }
}

template <detail::Int T, detail::Int U>
[[nodiscard]] constexpr auto wrapping_add(T a, U b) noexcept -> T {
    using W = std::make_unsigned_t<T>;
    return static_cast<T>(static_cast<W>(static_cast<W>(a) + static_cast<W>(b)));
}
template <detail::Int T, detail::Int U>
[[nodiscard]] constexpr auto wrapping_sub(T a, U b) noexcept -> T {
    using W = std::make_unsigned_t<T>;
    return static_cast<T>(static_cast<W>(static_cast<W>(a) - static_cast<W>(b)));
}
template <detail::Int T, detail::Int U>
[[nodiscard]] constexpr auto wrapping_mul(T a, U b) noexcept -> T {
    using W = std::make_unsigned_t<T>;
    // Promote through uintmax so `u16 * u16` cannot overflow a signed `int` on the way.
    return static_cast<T>(static_cast<W>(static_cast<std::uintmax_t>(static_cast<W>(a)) * static_cast<std::uintmax_t>(static_cast<W>(b))));
}
template <detail::Int T, detail::Int U>
[[nodiscard]] constexpr auto wrapping_div(T a, U b_in, const std::source_location& loc = std::source_location::current()) -> T {
    const T b = static_cast<T>(b_in);
    if (b == 0) [[unlikely]] detail::fail("division by zero in 'wrapping_div'", loc);
    if constexpr (std::is_signed_v<T>) {
        if (a == std::numeric_limits<T>::min() && b == T(-1)) return a;
    }
    return static_cast<T>(a / b);
}
template <detail::Int T>
[[nodiscard]] constexpr auto wrapping_neg(T a) noexcept -> T { return wrapping_sub(T(0), a); }
template <detail::Int T>
[[nodiscard]] constexpr auto wrapping_abs(T a) noexcept -> T {
    if constexpr (std::is_signed_v<T>) return a < 0 ? wrapping_neg(a) : a;
    else return a;
}

template <detail::Int T, detail::Int U>
[[nodiscard]] constexpr auto saturating_add(T a, U b) noexcept -> T {
    T r;
    if (!__builtin_add_overflow(a, b, &r)) return r;
    if constexpr (std::is_signed_v<U>) return b < 0 ? std::numeric_limits<T>::min() : std::numeric_limits<T>::max();
    else return std::numeric_limits<T>::max();
}
template <detail::Int T, detail::Int U>
[[nodiscard]] constexpr auto saturating_sub(T a, U b) noexcept -> T {
    T r;
    if (!__builtin_sub_overflow(a, b, &r)) return r;
    if constexpr (std::is_signed_v<U>) return b < 0 ? std::numeric_limits<T>::max() : std::numeric_limits<T>::min();
    else return std::numeric_limits<T>::min();
}
template <detail::Int T, detail::Int U>
[[nodiscard]] constexpr auto saturating_mul(T a, U b) noexcept -> T {
    T r;
    if (!__builtin_mul_overflow(a, b, &r)) return r;
    const bool negative = (a < 0) != (b < 0);
    return negative ? std::numeric_limits<T>::min() : std::numeric_limits<T>::max();
}
template <detail::Int T, detail::Int U>
[[nodiscard]] constexpr auto saturating_div(T a, U b_in, const std::source_location& loc = std::source_location::current()) -> T {
    const T b = static_cast<T>(b_in);
    if (b == 0) [[unlikely]] detail::fail("division by zero in 'saturating_div'", loc);
    if constexpr (std::is_signed_v<T>) {
        if (a == std::numeric_limits<T>::min() && b == T(-1)) return std::numeric_limits<T>::max();
    }
    return static_cast<T>(a / b);
}
template <detail::Int T>
[[nodiscard]] constexpr auto saturating_neg(T a) noexcept -> T { return saturating_sub(T(0), a); }

template <detail::Int T, detail::Int U>
[[nodiscard]] constexpr auto div_euclid(T a, U b_in, const std::source_location& loc = std::source_location::current()) -> T {
    const T b = static_cast<T>(b_in);
    if (b == 0) [[unlikely]] detail::fail("division by zero in 'div_euclid'", loc);
    if constexpr (std::is_signed_v<T>) {
        if (a == std::numeric_limits<T>::min() && b == T(-1)) [[unlikely]] detail::fail("integer overflow in 'div_euclid'", loc);
        T q = static_cast<T>(a / b);
        if (a % b < 0) q = b > 0 ? static_cast<T>(q - 1) : static_cast<T>(q + 1);
        return q;
    } else {
        return static_cast<T>(a / b);
    }
}
template <detail::Int T, detail::Int U>
[[nodiscard]] constexpr auto rem_euclid(T a, U b_in, const std::source_location& loc = std::source_location::current()) -> T {
    const T b = static_cast<T>(b_in);
    if (b == 0) [[unlikely]] detail::fail("division by zero in 'rem_euclid'", loc);
    if constexpr (std::is_signed_v<T>) {
        if (b == T(-1)) return 0;  // also covers MIN % -1, which is UB in C++
        T r = static_cast<T>(a % b);
        if (r < 0) r = b < 0 ? static_cast<T>(r - b) : static_cast<T>(r + b);
        return r;
    } else {
        return static_cast<T>(a % b);
    }
}
template <detail::Float T, detail::Num U>
[[nodiscard]] inline auto rem_euclid(T a, U b_in) noexcept -> T {
    const T b = static_cast<T>(b_in);
    T r = std::fmod(a, b);
    if (r < 0) r += std::fabs(b);
    return r;
}

// Floats.
template <detail::Float T> [[nodiscard]] inline auto floor(T x) noexcept -> T { return std::floor(x); }
template <detail::Float T> [[nodiscard]] inline auto ceil(T x) noexcept -> T { return std::ceil(x); }
template <detail::Float T> [[nodiscard]] inline auto round(T x) noexcept -> T { return std::round(x); }
template <detail::Float T> [[nodiscard]] inline auto trunc(T x) noexcept -> T { return std::trunc(x); }
template <detail::Float T> [[nodiscard]] inline auto fract(T x) noexcept -> T { return x - std::trunc(x); }
template <detail::Float T> [[nodiscard]] inline auto sqrt(T x) noexcept -> T { return std::sqrt(x); }
template <detail::Float T> [[nodiscard]] inline auto cbrt(T x) noexcept -> T { return std::cbrt(x); }
template <detail::Float T, detail::Int E> [[nodiscard]] inline auto powi(T x, E e) noexcept -> T { return std::pow(x, static_cast<T>(e)); }
template <detail::Float T> [[nodiscard]] inline auto exp(T x) noexcept -> T { return std::exp(x); }
template <detail::Float T> [[nodiscard]] inline auto ln(T x) noexcept -> T { return std::log(x); }
template <detail::Float T> [[nodiscard]] inline auto log2(T x) noexcept -> T { return std::log2(x); }
template <detail::Float T> [[nodiscard]] inline auto log10(T x) noexcept -> T { return std::log10(x); }
template <detail::Float T> [[nodiscard]] inline auto sin(T x) noexcept -> T { return std::sin(x); }
template <detail::Float T> [[nodiscard]] inline auto cos(T x) noexcept -> T { return std::cos(x); }
template <detail::Float T> [[nodiscard]] inline auto tan(T x) noexcept -> T { return std::tan(x); }
template <detail::Float T> [[nodiscard]] inline auto asin(T x) noexcept -> T { return std::asin(x); }
template <detail::Float T> [[nodiscard]] inline auto acos(T x) noexcept -> T { return std::acos(x); }
template <detail::Float T> [[nodiscard]] inline auto atan(T x) noexcept -> T { return std::atan(x); }
template <detail::Float T> [[nodiscard]] inline auto sinh(T x) noexcept -> T { return std::sinh(x); }
template <detail::Float T> [[nodiscard]] inline auto cosh(T x) noexcept -> T { return std::cosh(x); }
template <detail::Float T> [[nodiscard]] inline auto tanh(T x) noexcept -> T { return std::tanh(x); }
template <detail::Float T> [[nodiscard]] inline auto to_degrees(T x) noexcept -> T { return x * (T(180) / std::numbers::pi_v<T>); }
template <detail::Float T> [[nodiscard]] inline auto to_radians(T x) noexcept -> T { return x * (std::numbers::pi_v<T> / T(180)); }
template <detail::Float T> [[nodiscard]] inline auto is_nan(T x) noexcept -> bool { return std::isnan(x); }
template <detail::Float T> [[nodiscard]] inline auto is_inf(T x) noexcept -> bool { return std::isinf(x); }
template <detail::Float T> [[nodiscard]] inline auto is_finite(T x) noexcept -> bool { return std::isfinite(x); }

// --- str ----------------------------------------------------------------------------------------------
[[nodiscard]] inline auto contains(std::string_view s, std::string_view needle) noexcept -> bool {
    return s.find(needle) != std::string_view::npos;
}
[[nodiscard]] inline auto starts_with(std::string_view s, std::string_view p) noexcept -> bool { return s.starts_with(p); }
[[nodiscard]] inline auto ends_with(std::string_view s, std::string_view p) noexcept -> bool { return s.ends_with(p); }
[[nodiscard]] inline auto index_of(std::string_view s, std::string_view needle, std::size_t start = 0) noexcept -> std::optional<std::size_t> {
    if (start > s.size()) return std::nullopt;
    auto p = s.find(needle, start);
    if (p == std::string_view::npos) return std::nullopt;
    return p;
}
[[nodiscard]] inline auto last_index_of(std::string_view s, std::string_view needle) noexcept -> std::optional<std::size_t> {
    auto p = s.rfind(needle);
    if (p == std::string_view::npos) return std::nullopt;
    return p;
}
[[nodiscard]] inline auto split_n(std::string_view s, std::string_view sep, std::size_t n) -> std::vector<std::string> {
    std::vector<std::string> out;
    if (n == 0) return out;
    if (sep.empty()) {
        // An empty separator splits into bytes, like Go's strings.SplitN.
        for (std::size_t i = 0; i < s.size(); ++i) {
            if (out.size() + 1 == n) {
                out.emplace_back(s.substr(i));
                return out;
            }
            out.emplace_back(s.substr(i, 1));
        }
        return out;
    }
    std::size_t start = 0;
    while (out.size() + 1 < n) {
        auto p = s.find(sep, start);
        if (p == std::string_view::npos) break;
        out.emplace_back(s.substr(start, p - start));
        start = p + sep.size();
    }
    out.emplace_back(s.substr(start));
    return out;
}
[[nodiscard]] inline auto split(std::string_view s, std::string_view sep) -> std::vector<std::string> {
    return split_n(s, sep, std::numeric_limits<std::size_t>::max());
}
[[nodiscard]] inline auto lines(std::string_view s) -> std::vector<std::string> {
    std::vector<std::string> out;
    std::size_t start = 0;
    while (start < s.size()) {
        auto p = s.find('\n', start);
        std::string_view line = s.substr(start, p == std::string_view::npos ? std::string_view::npos : p - start);
        if (line.ends_with('\r')) line.remove_suffix(1);
        out.emplace_back(line);
        if (p == std::string_view::npos) break;
        start = p + 1;
    }
    return out;
}

namespace detail {
[[nodiscard]] constexpr auto is_ascii_space(char c) noexcept -> bool {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v';
}
[[nodiscard]] constexpr auto trim_start_view(std::string_view s) noexcept -> std::string_view {
    std::size_t i = 0;
    while (i < s.size() && is_ascii_space(s[i])) ++i;
    return s.substr(i);
}
[[nodiscard]] constexpr auto trim_end_view(std::string_view s) noexcept -> std::string_view {
    std::size_t n = s.size();
    while (n > 0 && is_ascii_space(s[n - 1])) --n;
    return s.substr(0, n);
}
} // namespace detail

// View-returning functions borrow from an lvalue receiver; on a temporary `str` they return an owned copy
// so `f().trim()` can never dangle.
[[nodiscard]] constexpr auto trim(std::string_view s) noexcept -> str_ref { return detail::trim_end_view(detail::trim_start_view(s)); }
template <typename S> requires std::same_as<S, std::string>
[[nodiscard]] inline auto trim(S&& s) -> std::string { return std::string(trim(std::string_view(s))); }
[[nodiscard]] constexpr auto trim_start(std::string_view s) noexcept -> str_ref { return detail::trim_start_view(s); }
template <typename S> requires std::same_as<S, std::string>
[[nodiscard]] inline auto trim_start(S&& s) -> std::string { return std::string(detail::trim_start_view(s)); }
[[nodiscard]] constexpr auto trim_end(std::string_view s) noexcept -> str_ref { return detail::trim_end_view(s); }
template <typename S> requires std::same_as<S, std::string>
[[nodiscard]] inline auto trim_end(S&& s) -> std::string { return std::string(detail::trim_end_view(s)); }
[[nodiscard]] constexpr auto strip_prefix(std::string_view s, std::string_view p) noexcept -> std::optional<str_ref> {
    if (!s.starts_with(p)) return std::nullopt;
    return s.substr(p.size());
}
template <typename S> requires std::same_as<S, std::string>
[[nodiscard]] inline auto strip_prefix(S&& s, std::string_view p) -> std::optional<std::string> {
    if (!std::string_view(s).starts_with(p)) return std::nullopt;
    return s.substr(p.size());
}
[[nodiscard]] constexpr auto strip_suffix(std::string_view s, std::string_view p) noexcept -> std::optional<str_ref> {
    if (!s.ends_with(p)) return std::nullopt;
    return s.substr(0, s.size() - p.size());
}
template <typename S> requires std::same_as<S, std::string>
[[nodiscard]] inline auto strip_suffix(S&& s, std::string_view p) -> std::optional<std::string> {
    if (!std::string_view(s).ends_with(p)) return std::nullopt;
    return s.substr(0, s.size() - p.size());
}
[[nodiscard]] inline auto replace(std::string_view s, std::string_view from, std::string_view to) -> std::string {
    std::string out(s);
    if (from.empty()) return out;
    if (auto p = out.find(from); p != std::string::npos) out.replace(p, from.size(), to);
    return out;
}
[[nodiscard]] inline auto replace_all(std::string_view s, std::string_view from, std::string_view to) -> std::string {
    if (from.empty()) return std::string(s);
    std::string out;
    out.reserve(s.size());
    std::size_t start = 0;
    while (true) {
        auto p = s.find(from, start);
        if (p == std::string_view::npos) break;
        out.append(s.substr(start, p - start));
        out.append(to);
        start = p + from.size();
    }
    out.append(s.substr(start));
    return out;
}
template <detail::Int N>
[[nodiscard]] inline auto repeat(std::string_view s, N n_in, const std::source_location& loc = std::source_location::current()) -> std::string {
    if (n_in < 0) [[unlikely]] detail::fail("'repeat' with a negative count", loc);
    auto n = static_cast<std::size_t>(n_in);
    if (n != 0 && s.size() > std::numeric_limits<std::size_t>::max() / n) [[unlikely]] detail::fail("'repeat' result size overflows", loc);
    std::string out;
    out.reserve(s.size() * n);
    for (std::size_t i = 0; i < n; ++i) out.append(s);
    return out;
}
[[nodiscard]] inline auto to_upper(std::string_view s) -> std::string {
    std::string out(s);
    for (char& c : out) if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
    return out;
}
[[nodiscard]] inline auto to_lower(std::string_view s) -> std::string {
    std::string out(s);
    for (char& c : out) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return out;
}

namespace detail {
// Decodes one code point at `i`, advancing it; malformed or truncated sequences yield U+FFFD and one byte.
[[nodiscard]] inline auto decode_rune(std::string_view s, std::size_t& i) noexcept -> std::uint32_t {
    const auto b0 = static_cast<unsigned char>(s[i]);
    auto cont = [&](std::size_t k) -> int {
        if (i + k >= s.size()) return -1;
        auto b = static_cast<unsigned char>(s[i + k]);
        return (b & 0xC0u) == 0x80u ? (b & 0x3F) : -1;
    };
    if (b0 < 0x80u) { ++i; return b0; }
    std::uint32_t cp = 0;
    std::size_t n = 0;
    std::uint32_t min = 0;
    if ((b0 & 0xE0u) == 0xC0u) { cp = b0 & 0x1Fu; n = 1; min = 0x80; }
    else if ((b0 & 0xF0u) == 0xE0u) { cp = b0 & 0x0Fu; n = 2; min = 0x800; }
    else if ((b0 & 0xF8u) == 0xF0u) { cp = b0 & 0x07u; n = 3; min = 0x10000; }
    else { ++i; return 0xFFFD; }
    for (std::size_t k = 1; k <= n; ++k) {
        int c = cont(k);
        if (c < 0) { ++i; return 0xFFFD; }
        cp = (cp << 6) | static_cast<std::uint32_t>(c);
    }
    if (cp < min || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) { ++i; return 0xFFFD; }
    i += n + 1;
    return cp;
}
} // namespace detail

[[nodiscard]] inline auto runes(std::string_view s) -> std::vector<std::uint32_t> {
    std::vector<std::uint32_t> out;
    out.reserve(s.size());
    for (std::size_t i = 0; i < s.size();) out.push_back(detail::decode_rune(s, i));
    return out;
}
[[nodiscard]] inline auto rune_count(std::string_view s) noexcept -> std::size_t {
    std::size_t n = 0;
    for (std::size_t i = 0; i < s.size(); ++n) (void)detail::decode_rune(s, i);
    return n;
}
inline auto pop(std::string& s) -> std::optional<std::uint8_t> {
    if (s.empty()) return std::nullopt;
    auto b = static_cast<std::uint8_t>(s.back());
    s.pop_back();
    return b;
}

// --- Type-associated parsing (Q10): `i32.parse(s)`, `f64.parse(s)`, `bool.parse(s)` ---------------------
// Surrounding ASCII whitespace is ignored; anything else that is not a complete, in-range literal is `none`.
template <typename T>
[[nodiscard]] inline auto parse(std::string_view s) noexcept -> std::optional<T> {
    std::string_view t = detail::trim_end_view(detail::trim_start_view(s));
    if constexpr (std::same_as<T, bool>) {
        if (t.size() == 4 || t.size() == 5 || t.size() == 1) {
            std::string low(t);
            for (char& c : low) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
            if (low == "true" || low == "1") return true;
            if (low == "false" || low == "0") return false;
        }
        return std::nullopt;
    } else if constexpr (std::floating_point<T>) {
        return cfire::detail::parse_float<T>(t);
    } else {
        return cfire::detail::parse_int<T>(t);
    }
}

// --- Slices and fixed arrays --------------------------------------------------------------------------
template <detail::Seq C, typename U>
[[nodiscard]] constexpr auto contains(const C& c, const U& v) -> bool {
    return std::find(c.begin(), c.end(), v) != c.end();
}
template <detail::Seq C, typename U>
[[nodiscard]] constexpr auto index_of(const C& c, const U& v) -> std::optional<std::size_t> {
    auto it = std::find(c.begin(), c.end(), v);
    if (it == c.end()) return std::nullopt;
    return static_cast<std::size_t>(it - c.begin());
}
template <detail::Seq C>
[[nodiscard]] constexpr auto first(const C& c) -> std::optional<typename C::value_type> {
    if (c.size() == 0) return std::nullopt;
    return *c.begin();
}
template <detail::Seq C>
[[nodiscard]] constexpr auto last(const C& c) -> std::optional<typename C::value_type> {
    if (c.size() == 0) return std::nullopt;
    return *(c.end() - 1);
}
template <detail::Seq C>
constexpr void reverse(C& c) { std::reverse(c.begin(), c.end()); }
template <detail::Seq C>
constexpr void sort(C& c) { std::sort(c.begin(), c.end()); }
template <detail::Seq C, typename Less>
constexpr void sort_by(C& c, Less&& less) { std::sort(c.begin(), c.end(), std::forward<Less>(less)); }
template <detail::Seq C>
[[nodiscard]] constexpr auto is_sorted(const C& c) -> bool { return std::is_sorted(c.begin(), c.end()); }
template <detail::Seq C, typename U>
[[nodiscard]] constexpr auto binary_search(const C& c, const U& v) -> std::optional<std::size_t> {
    auto it = std::lower_bound(c.begin(), c.end(), v);
    if (it == c.end() || !(*it == v)) return std::nullopt;
    return static_cast<std::size_t>(it - c.begin());
}
template <detail::Seq C>
    requires std::convertible_to<const typename C::value_type&, std::string_view>
[[nodiscard]] inline auto join(const C& parts, std::string_view sep) -> std::string {
    std::string out;
    bool first_part = true;
    for (const auto& p : parts) {
        if (!first_part) out.append(sep);
        out.append(std::string_view(p));
        first_part = false;
    }
    return out;
}
template <typename T, typename A>
inline auto pop(std::vector<T, A>& v) -> std::optional<T> {
    if (v.empty()) return std::nullopt;
    T x = std::move(v.back());
    v.pop_back();
    return x;
}
// A stdlib container with its own `pop() -> T?` (`collections.PriorityQueue`, `MinQueue`), reached when Sema
// could not type the receiver (CF-183).
template <typename C>
    requires requires(C& c) { c.pop(); }
inline auto pop(C& c) -> decltype(c.pop()) {
    return c.pop();
}
template <typename T, typename A, detail::Int I, typename U>
inline void insert(std::vector<T, A>& v, I i, U&& x, const std::source_location& loc = std::source_location::current()) {
    if (std::cmp_less(i, 0) || std::cmp_greater(i, v.size())) [[unlikely]] {
        detail::fail(std::format("insert index {} out of bounds for length {}", i, v.size()), loc);
    }
    v.insert(v.begin() + static_cast<std::ptrdiff_t>(i), static_cast<T>(std::forward<U>(x)));
}
template <typename T, typename A, detail::Int I>
inline auto remove(std::vector<T, A>& v, I i, const std::source_location& loc = std::source_location::current()) -> T {
    if (std::cmp_less(i, 0) || std::cmp_greater_equal(i, v.size())) [[unlikely]] {
        detail::fail(std::format("remove index {} out of bounds for length {}", i, v.size()), loc);
    }
    auto it = v.begin() + static_cast<std::ptrdiff_t>(i);
    T x = std::move(*it);
    v.erase(it);
    return x;
}
template <typename T, typename A, detail::Int I>
inline auto swap_remove(std::vector<T, A>& v, I i, const std::source_location& loc = std::source_location::current()) -> T {
    if (std::cmp_less(i, 0) || std::cmp_greater_equal(i, v.size())) [[unlikely]] {
        detail::fail(std::format("swap_remove index {} out of bounds for length {}", i, v.size()), loc);
    }
    auto idx = static_cast<std::size_t>(i);
    T x = std::move(v[idx]);
    if (idx + 1 != v.size()) v[idx] = std::move(v.back());
    v.pop_back();
    return x;
}
template <typename T, typename A, detail::Int N>
inline void truncate(std::vector<T, A>& v, N n) {
    if (std::cmp_less(n, 0)) n = 0;
    if (std::cmp_less(n, v.size())) v.resize(static_cast<std::size_t>(n));
}
template <typename T, typename A, typename R>
inline void extend(std::vector<T, A>& v, const R& other) {
    // Copy first: `xs.extend(xs)` would otherwise read from storage that the insert reallocates.
    std::vector<T, A> tmp(other.begin(), other.end());
    v.insert(v.end(), std::make_move_iterator(tmp.begin()), std::make_move_iterator(tmp.end()));
}
template <typename T, typename A>
inline void extend(std::vector<T, A>& v, std::initializer_list<T> items) {
    v.insert(v.end(), items.begin(), items.end());  // `xs.extend([4, 5])`
}
template <typename T, typename A>
inline void clear(std::vector<T, A>& v) noexcept { v.clear(); }

// --- Maps ---------------------------------------------------------------------------------------------
template <typename K, typename V, typename Q>
[[nodiscard]] inline auto contains(const cfire::Map<K, V>& m, const Q& k) -> bool { return m.contains(k); }
template <typename K, typename V, typename Q>
[[nodiscard]] inline auto get(const cfire::Map<K, V>& m, const Q& k) -> std::optional<V> { return m.get(k); }
template <typename K, typename V, typename Q, typename U>
inline auto insert(cfire::Map<K, V>& m, Q&& k, U&& v) -> std::optional<V> {
    auto prev = m.get(k);
    m[std::forward<Q>(k)] = static_cast<V>(std::forward<U>(v));
    return prev;
}
template <typename K, typename V, typename Q>
inline auto remove(cfire::Map<K, V>& m, const Q& k) -> std::optional<V> {
    auto prev = m.get(k);
    if (prev) m.erase(k);
    return prev;
}
template <typename K, typename V>
[[nodiscard]] inline auto keys(const cfire::Map<K, V>& m) -> std::vector<K> {
    std::vector<K> out;
    out.reserve(m.size());
    for (const auto& kv : m) out.push_back(kv.first);
    return out;
}
template <typename K, typename V>
[[nodiscard]] inline auto values(const cfire::Map<K, V>& m) -> std::vector<V> {
    std::vector<V> out;
    out.reserve(m.size());
    for (const auto& kv : m) out.push_back(kv.second);
    return out;
}
template <typename K, typename V>
inline void clear(cfire::Map<K, V>& m) noexcept { m.clear(); }

} // namespace cfire::bm

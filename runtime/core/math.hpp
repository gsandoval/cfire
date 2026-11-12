#pragma once
#include "../cfire_prelude.hpp"

#include <cmath>
#include <fstream>
#include <random>
#include <cstdint>
#include <algorithm>
#include <optional>
#include <limits>
#include <type_traits>
#include <numbers>
#include <numeric>
#include <source_location>

namespace cfire::math {

// Q10: math constants are values (`math.PI`), not functions.
inline constexpr double PI = std::numbers::pi;
inline constexpr double E = std::numbers::e;
inline constexpr double TAU = 2.0 * std::numbers::pi;
inline constexpr double SQRT_2 = std::numbers::sqrt2;
inline constexpr double LN_2 = std::numbers::ln2;
inline constexpr double LN_10 = std::numbers::ln10;

// Multi-input functions with no natural receiver (Q7).
[[nodiscard]] inline auto atan2(double y, double x) noexcept -> double { return std::atan2(y, x); }
[[nodiscard]] inline auto hypot(double x, double y) noexcept -> double { return std::hypot(x, y); }

template <std::integral A, std::integral B>
[[nodiscard]] constexpr auto gcd(A a, B b, const std::source_location& loc = std::source_location::current()) {
    using T = std::common_type_t<A, B>;
    if constexpr (std::is_signed_v<T>) {
        if (a == std::numeric_limits<A>::min() || b == std::numeric_limits<B>::min()) [[unlikely]] {
            cfire::assert_true_at(false, "'math.gcd' of the minimum value overflows", loc.file_name(), static_cast<int>(loc.line()), "gcd overflow");
        }
    }
    return std::gcd(static_cast<T>(a), static_cast<T>(b));
}

template <std::integral A, std::integral B>
[[nodiscard]] constexpr auto lcm(A a, B b, const std::source_location& loc = std::source_location::current()) {
    using T = std::common_type_t<A, B>;
    T g = gcd(a, b, loc);
    if (g == 0) return T{0};
    T x = static_cast<T>(a) / g;
    T y = static_cast<T>(b);
    if constexpr (std::is_signed_v<T>) {
        if (x < 0) x = -x;
        if (y < 0) y = -y;
    }
    T r;
    if (__builtin_mul_overflow(x, y, &r)) [[unlikely]] {
        cfire::assert_true_at(false, "'math.lcm' overflows", loc.file_name(), static_cast<int>(loc.line()), "lcm overflow");
    }
    return r;
}

[[nodiscard]] inline auto abs_f64(double x) noexcept -> double { return std::abs(x); }
[[nodiscard]] inline auto sqrt_f64(double x) noexcept -> double { return std::sqrt(x); }
[[nodiscard]] inline auto sqrt(double x) noexcept -> double { return std::sqrt(x); }
[[nodiscard]] inline auto clamp_i64(std::int64_t v, std::int64_t lo, std::int64_t hi) noexcept -> std::int64_t {
    return std::clamp(v, lo, hi);
}
[[nodiscard]] inline auto clamp_f64(double v, double lo, double hi) noexcept -> double {
    return std::clamp(v, lo, hi);
}

template <typename A, typename B>
[[nodiscard]] constexpr auto checked_add(A a, B b) noexcept -> std::optional<std::common_type_t<A, B>> {
    using T = std::common_type_t<A, B>;
    T res;
    if (__builtin_add_overflow(static_cast<T>(a), static_cast<T>(b), &res)) return std::nullopt;
    return res;
}

template <typename A, typename B>
[[nodiscard]] constexpr auto checked_sub(A a, B b) noexcept -> std::optional<std::common_type_t<A, B>> {
    using T = std::common_type_t<A, B>;
    T res;
    if (__builtin_sub_overflow(static_cast<T>(a), static_cast<T>(b), &res)) return std::nullopt;
    return res;
}

template <typename A, typename B>
[[nodiscard]] constexpr auto checked_mul(A a, B b) noexcept -> std::optional<std::common_type_t<A, B>> {
    using T = std::common_type_t<A, B>;
    T res;
    if (__builtin_mul_overflow(static_cast<T>(a), static_cast<T>(b), &res)) return std::nullopt;
    return res;
}

template <typename A, typename B>
[[nodiscard]] constexpr auto wrapping_add(A a, B b) noexcept -> std::common_type_t<A, B> {
    using T = std::common_type_t<A, B>;
    using U = std::make_unsigned_t<T>;
    return static_cast<T>(static_cast<U>(a) + static_cast<U>(b));
}

template <typename A, typename B>
[[nodiscard]] constexpr auto wrapping_sub(A a, B b) noexcept -> std::common_type_t<A, B> {
    using T = std::common_type_t<A, B>;
    using U = std::make_unsigned_t<T>;
    return static_cast<T>(static_cast<U>(a) - static_cast<U>(b));
}

template <typename A, typename B>
[[nodiscard]] constexpr auto wrapping_mul(A a, B b) noexcept -> std::common_type_t<A, B> {
    using T = std::common_type_t<A, B>;
    using U = std::make_unsigned_t<T>;
    return static_cast<T>(static_cast<U>(a) * static_cast<U>(b));
}

template <typename A, typename B>
[[nodiscard]] constexpr auto saturating_add(A a_in, B b_in) noexcept -> std::common_type_t<A, B> {
    using T = std::common_type_t<A, B>;
    T a = static_cast<T>(a_in);
    T b = static_cast<T>(b_in);
    T res;
    if (__builtin_add_overflow(a, b, &res)) {
        if constexpr (std::is_signed_v<T>) {
            return (b > 0) ? std::numeric_limits<T>::max() : std::numeric_limits<T>::min();
        } else {
            return std::numeric_limits<T>::max();
        }
    }
    return res;
}

template <typename A, typename B>
[[nodiscard]] constexpr auto saturating_sub(A a_in, B b_in) noexcept -> std::common_type_t<A, B> {
    using T = std::common_type_t<A, B>;
    T a = static_cast<T>(a_in);
    T b = static_cast<T>(b_in);
    T res;
    if (__builtin_sub_overflow(a, b, &res)) {
        if constexpr (std::is_signed_v<T>) {
            return (b < 0) ? std::numeric_limits<T>::max() : std::numeric_limits<T>::min();
        } else {
            return std::numeric_limits<T>::min();
        }
    }
    return res;
}

template <typename A, typename B>
[[nodiscard]] constexpr auto saturating_mul(A a_in, B b_in) noexcept -> std::common_type_t<A, B> {
    using T = std::common_type_t<A, B>;
    T a = static_cast<T>(a_in);
    T b = static_cast<T>(b_in);
    T res;
    if (__builtin_mul_overflow(a, b, &res)) {
        if constexpr (std::is_signed_v<T>) {
            return ((a > 0 && b > 0) || (a < 0 && b < 0))
                ? std::numeric_limits<T>::max()
                : std::numeric_limits<T>::min();
        } else {
            return std::numeric_limits<T>::max();
        }
    }
    return res;
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


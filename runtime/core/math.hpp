#pragma once
#include "../cfire_prelude.hpp"

#include <cmath>
#include <random>
#include <cstdint>
#include <algorithm>

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


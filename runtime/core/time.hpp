#pragma once
#include "../cfire_prelude.hpp"

#include "sync.hpp"
#include <charconv>
#include <chrono>
#include <cstdint>
#include <ctime>
#include <format>
#include <string>
#include <string_view>
#include <expected>
#include <thread>

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


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
    std::int32_t offset_sec_{0};
    bool is_local_{false};

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
        return Time{tp + dur, offset_sec_, is_local_};
    }
    [[nodiscard]] inline auto sub(const Time& other) const noexcept -> Duration {
        auto diff = std::chrono::duration_cast<std::chrono::nanoseconds>(tp - other.tp).count();
        return Duration{diff};
    }

    [[nodiscard]] inline auto utc() const noexcept -> Time {
        return Time{tp, 0, false};
    }

    [[nodiscard]] inline auto local() const noexcept -> Time {
        std::time_t t = std::chrono::system_clock::to_time_t(tp);
        std::tm lt{};
#if defined(_WIN32)
        if (::localtime_s(&lt, &t) != 0) {
            return Time{tp, 0, true};
        }
        std::tm as_utc = lt;
        as_utc.tm_isdst = 0;
        std::time_t local_epoch = ::_mkgmtime(&as_utc);
        std::int32_t off = (local_epoch != static_cast<std::time_t>(-1))
            ? static_cast<std::int32_t>(local_epoch - t)
            : 0;
        return Time{tp, off, true};
#else
        if (!::localtime_r(&t, &lt)) {
            return Time{tp, 0, true};
        }
        std::int32_t off = 0;
        if constexpr (requires(const std::tm& m) { m.tm_gmtoff; }) {
            off = static_cast<std::int32_t>(lt.tm_gmtoff);
        } else {
            std::tm as_utc = lt;
            as_utc.tm_isdst = 0;
            std::time_t local_epoch = ::timegm(&as_utc);
            if (local_epoch != static_cast<std::time_t>(-1)) {
                off = static_cast<std::int32_t>(local_epoch - t);
            }
        }
        return Time{tp, off, true};
#endif
    }

    // Calendar breakdowns (UTC when utc(), local offset when local())
    [[nodiscard]] inline auto to_utc_tm() const noexcept -> std::tm {
        std::time_t t = std::chrono::system_clock::to_time_t(tp);
        std::tm tm_buf{};
#if defined(_WIN32)
        gmtime_s(&tm_buf, &t);
#else
        gmtime_r(&t, &tm_buf);
#endif
        return tm_buf;
    }

    [[nodiscard]] inline auto to_display_tm() const noexcept -> std::tm {
        std::time_t base = std::chrono::system_clock::to_time_t(tp);
        std::time_t shifted = static_cast<std::time_t>(static_cast<std::int64_t>(base) + static_cast<std::int64_t>(offset_sec_));
        std::tm tm_buf{};
#if defined(_WIN32)
        gmtime_s(&tm_buf, &shifted);
#else
        gmtime_r(&shifted, &tm_buf);
#endif
        return tm_buf;
    }

    [[nodiscard]] inline auto year() const noexcept -> std::int32_t {
        return to_display_tm().tm_year + 1900;
    }
    [[nodiscard]] inline auto month() const noexcept -> std::int32_t {
        return to_display_tm().tm_mon + 1;
    }
    [[nodiscard]] inline auto day() const noexcept -> std::int32_t {
        return to_display_tm().tm_mday;
    }
    [[nodiscard]] inline auto hour() const noexcept -> std::int32_t {
        return to_display_tm().tm_hour;
    }
    [[nodiscard]] inline auto minute() const noexcept -> std::int32_t {
        return to_display_tm().tm_min;
    }
    [[nodiscard]] inline auto second() const noexcept -> std::int32_t {
        return to_display_tm().tm_sec;
    }

    [[nodiscard]] inline auto format_rfc3339() const -> std::string {
        auto tm_buf = to_display_tm();
        if (!is_local_ && offset_sec_ == 0) {
            return std::format("{:04d}-{:02d}-{:02d}T{:02d}:{:02d}:{:02d}Z",
                tm_buf.tm_year + 1900, tm_buf.tm_mon + 1, tm_buf.tm_mday,
                tm_buf.tm_hour, tm_buf.tm_min, tm_buf.tm_sec);
        }
        char sign = offset_sec_ < 0 ? '-' : '+';
        std::int32_t abs_off = offset_sec_ < 0 ? -offset_sec_ : offset_sec_;
        int oh = abs_off / 3600;
        int om = (abs_off % 3600) / 60;
        return std::format("{:04d}-{:02d}-{:02d}T{:02d}:{:02d}:{:02d}{}{:02d}:{:02d}",
            tm_buf.tm_year + 1900, tm_buf.tm_mon + 1, tm_buf.tm_mday,
            tm_buf.tm_hour, tm_buf.tm_min, tm_buf.tm_sec, sign, oh, om);
    }

    // CRT-safe C++23 tokenizer: avoids std::strftime so unrecognized specifiers never trap UCRT.
    [[nodiscard]] inline auto format(std::string_view pattern) const -> std::string {
        static constexpr const char* kWdayShort[7] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
        static constexpr const char* kWdayFull[7] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};
        static constexpr const char* kMonShort[12] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
        static constexpr const char* kMonFull[12] = {"January", "February", "March", "April", "May", "June", "July", "August", "September", "October", "November", "December"};

        const std::tm tm_buf = to_display_tm();
        const int yr = tm_buf.tm_year + 1900;
        const int mon = tm_buf.tm_mon + 1;
        const int mday = tm_buf.tm_mday;
        const int hr = tm_buf.tm_hour;
        const int min = tm_buf.tm_min;
        const int sec = tm_buf.tm_sec;
        const int wday = (tm_buf.tm_wday >= 0 && tm_buf.tm_wday < 7) ? tm_buf.tm_wday : 0;
        const int mon_idx = (tm_buf.tm_mon >= 0 && tm_buf.tm_mon < 12) ? tm_buf.tm_mon : 0;
        const std::int64_t sub_ns = ((unix_nanos() % 1'000'000'000LL) + 1'000'000'000LL) % 1'000'000'000LL;
        const char sign = offset_sec_ < 0 ? '-' : '+';
        const std::int32_t abs_off = offset_sec_ < 0 ? -offset_sec_ : offset_sec_;
        const int oh = abs_off / 3600;
        const int om = (abs_off % 3600) / 60;

        std::string out;
        out.reserve(pattern.size() + 16);
        for (std::size_t i = 0; i < pattern.size(); ++i) {
            if (pattern[i] != '%') {
                out.push_back(pattern[i]);
                continue;
            }
            if (i + 1 >= pattern.size()) {
                out.push_back('%');
                break;
            }
            char spec = pattern[++i];
            if (spec == ':' && i + 1 < pattern.size() && pattern[i + 1] == 'z') {
                ++i;
                out += std::format("{}{:02d}:{:02d}", sign, oh, om);
                continue;
            }
            switch (spec) {
                case '%': out.push_back('%'); break;
                case 'Y': out += std::format("{:04d}", yr); break;
                case 'y': out += std::format("{:02d}", ((yr % 100) + 100) % 100); break;
                case 'm': out += std::format("{:02d}", mon); break;
                case 'd': out += std::format("{:02d}", mday); break;
                case 'e': out += std::format("{:2d}", mday); break;
                case 'H': out += std::format("{:02d}", hr); break;
                case 'I': out += std::format("{:02d}", (hr % 12 == 0) ? 12 : (hr % 12)); break;
                case 'p': out += (hr >= 12 ? "PM" : "AM"); break;
                case 'M': out += std::format("{:02d}", min); break;
                case 'S': out += std::format("{:02d}", sec); break;
                case 'f': out += std::format("{:06d}", static_cast<int>(sub_ns / 1000)); break;
                case 'L': out += std::format("{:03d}", static_cast<int>(sub_ns / 1'000'000)); break;
                case 'z': out += std::format("{}{:02d}{:02d}", sign, oh, om); break;
                case 'Z':
                    if (!is_local_ && offset_sec_ == 0) out += "UTC";
                    else out += std::format("{}{:02d}:{:02d}", sign, oh, om);
                    break;
                case 'F': out += std::format("{:04d}-{:02d}-{:02d}", yr, mon, mday); break;
                case 'T': out += std::format("{:02d}:{:02d}:{:02d}", hr, min, sec); break;
                case 'R': out += std::format("{:02d}:{:02d}", hr, min); break;
                case 'a': out += kWdayShort[wday]; break;
                case 'A': out += kWdayFull[wday]; break;
                case 'b':
                case 'h': out += kMonShort[mon_idx]; break;
                case 'B': out += kMonFull[mon_idx]; break;
                default:
                    out.push_back('%');
                    out.push_back(spec);
                    break;
            }
        }
        return out;
    }

    // Instant-only ordering and equality (UTC vs local of the same instant are equal).
    friend constexpr auto operator==(const Time& a, const Time& b) noexcept -> bool {
        return a.tp == b.tp;
    }
    friend constexpr auto operator<=>(const Time& a, const Time& b) noexcept {
        return a.tp <=> b.tp;
    }
};

[[nodiscard]] inline auto now() noexcept -> Time {
    return Time{std::chrono::system_clock::now(), 0, false};
}

[[nodiscard]] inline auto unix(std::int64_t sec, std::int64_t nsec = 0) noexcept -> Time {
    auto dur = std::chrono::duration_cast<std::chrono::system_clock::duration>(
        std::chrono::seconds(sec) + std::chrono::nanoseconds(nsec));
    return Time{std::chrono::system_clock::time_point{dur}, 0, false};
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
        if (sv.empty()) return false;
        for (char c : sv) {
            if (c < '0' || c > '9') return false;
        }
        auto [ptr, ec] = std::from_chars(sv.data(), sv.data() + sv.size(), out);
        return ec == std::errc{} && ptr == sv.data() + sv.size();
    };
    if (!parse_int(s.substr(0, 4), year) ||
        !parse_int(s.substr(5, 2), mon) ||
        !parse_int(s.substr(8, 2), mday) ||
        !parse_int(s.substr(11, 2), hour) ||
        !parse_int(s.substr(14, 2), min) ||
        !parse_int(s.substr(17, 2), sec)) {
        return cfire::make_error("failed to parse RFC 3339 numeric components");
    }
    if (mon < 1 || mon > 12 || mday < 1 || mday > 31 || hour < 0 || hour > 23 || min < 0 || min > 59 || sec < 0 || sec > 60) {
        return cfire::make_error("RFC 3339 calendar component out of range");
    }
    std::size_t pos = 19;
    std::int64_t nsec = 0;
    if (pos < s.size() && s[pos] == '.') {
        ++pos;
        std::size_t frac_digits = 0;
        std::int64_t scale = 100'000'000LL;
        while (pos < s.size() && s[pos] >= '0' && s[pos] <= '9') {
            if (scale >= 1) {
                nsec += static_cast<std::int64_t>(s[pos] - '0') * scale;
                scale /= 10;
            }
            ++frac_digits;
            ++pos;
        }
        if (frac_digits == 0) {
            return cfire::make_error("invalid fractional seconds in RFC 3339 timestamp");
        }
    }
    if (pos >= s.size()) {
        return cfire::make_error("missing timezone offset in RFC 3339 timestamp");
    }
    std::int32_t offset_sec = 0;
    bool is_local = false;
    if (s[pos] == 'Z' || s[pos] == 'z') {
        if (pos + 1 != s.size()) {
            return cfire::make_error("unexpected trailing characters in RFC 3339 timestamp");
        }
    } else if (s[pos] == '+' || s[pos] == '-') {
        int sign = (s[pos] == '-') ? -1 : 1;
        ++pos;
        std::string_view off_sv = s.substr(pos);
        int oh = 0;
        int om = 0;
        if (off_sv.size() == 5 && off_sv[2] == ':') {
            if (!parse_int(off_sv.substr(0, 2), oh) || !parse_int(off_sv.substr(3, 2), om)) {
                return cfire::make_error("invalid RFC 3339 timezone offset");
            }
        } else if (off_sv.size() == 4) {
            if (!parse_int(off_sv.substr(0, 2), oh) || !parse_int(off_sv.substr(2, 2), om)) {
                return cfire::make_error("invalid RFC 3339 timezone offset");
            }
        } else {
            return cfire::make_error("invalid RFC 3339 timezone offset");
        }
        if (oh < 0 || oh > 23 || om < 0 || om > 59) {
            return cfire::make_error("RFC 3339 timezone offset out of range");
        }
        offset_sec = sign * (oh * 3600 + om * 60);
        is_local = true;
    } else {
        return cfire::make_error("invalid timezone designator in RFC 3339 timestamp");
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
    auto adjusted_sec = static_cast<std::time_t>(static_cast<std::int64_t>(tt) - static_cast<std::int64_t>(offset_sec));
    auto tp = std::chrono::system_clock::from_time_t(adjusted_sec) +
              std::chrono::duration_cast<std::chrono::system_clock::duration>(std::chrono::nanoseconds(nsec));
    return Time{tp, offset_sec, is_local};
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


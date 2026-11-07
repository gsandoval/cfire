#pragma once
// cfire_test_runtime.hpp — Test Harness, Fuzzing, Shrinking, Benchmarking & Assertion Runtime for cfire (.cf)

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <limits>
#if __has_include(<print>)
#include <print>
#endif
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace cfire {

// --- Built-in `assert(cond)` for `test "..."` Blocks & Contracts ---
namespace testing_detail {
inline thread_local bool in_test_harness = false;
inline thread_local bool current_test_failed = false;
inline thread_local std::string failure_file;
inline thread_local int failure_line = 0;
inline thread_local std::string failure_expr;
inline thread_local std::string failure_msg;

struct TestAssertionFailure {};

template <typename Fn>
inline auto run_test_guarded(Fn&& fn) -> bool {
    try {
        fn();
        return !current_test_failed;
    } catch (const TestAssertionFailure&) {
        return false;
    }
}

template <typename T>
concept FormattableVal = std::is_default_constructible_v<std::formatter<std::decay_t<T>, char>>;

template <typename T>
[[nodiscard]] inline auto format_debug_val(const T& v) -> std::string {
    using Decayed = std::decay_t<T>;
    if constexpr (std::is_same_v<Decayed, std::string> ||
                  std::is_same_v<Decayed, std::string_view> ||
                  std::is_same_v<Decayed, const char*> ||
                  std::is_same_v<Decayed, char*>) {
        return std::format("\"{}\"", v);
    } else if constexpr (std::is_same_v<Decayed, bool>) {
        return v ? "true" : "false";
    } else if constexpr (FormattableVal<Decayed>) {
        return std::format("{}", v);
    } else {
        return "<unformattable>";
    }
}

// --- Deterministic Seeded PRNG (SplitMix64) & Arbitrary<T> (TEST-01, TEST-02) ---
struct FuzzRng {
    std::uint64_t state;
    explicit constexpr FuzzRng(std::uint64_t seed = 0x9E3779B97F4A7C15ULL) noexcept
        : state(seed ? seed : 0x9E3779B97F4A7C15ULL) {}

    constexpr auto next_u64() noexcept -> std::uint64_t {
        std::uint64_t z = (state += 0x9E3779B97F4A7C15ULL);
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
        return z ^ (z >> 31);
    }

    constexpr auto next_range(std::size_t max_excl) noexcept -> std::size_t {
        return max_excl == 0 ? 0 : static_cast<std::size_t>(next_u64() % max_excl);
    }
};

template <typename T, typename Enable = void>
struct Arbitrary {
    static auto generate(FuzzRng&, std::size_t) -> T {
        return T{};
    }
};

template <>
struct Arbitrary<bool> {
    static auto generate(FuzzRng& rng, std::size_t iter) -> bool {
        if (iter == 0) return false;
        if (iter == 1) return true;
        return (rng.next_u64() & 1ULL) != 0;
    }
};

template <typename T>
struct Arbitrary<T, std::enable_if_t<std::is_integral_v<T> && !std::is_same_v<T, bool>>> {
    static auto generate(FuzzRng& rng, std::size_t iter) -> T {
        if constexpr (std::is_signed_v<T>) {
            static constexpr std::array<T, 12> kEdges = {
                static_cast<T>(0),
                static_cast<T>(1),
                static_cast<T>(-1),
                std::numeric_limits<T>::min(),
                std::numeric_limits<T>::max(),
                static_cast<T>(std::numeric_limits<T>::min() + 1),
                static_cast<T>(std::numeric_limits<T>::max() - 1),
                static_cast<T>(2),
                static_cast<T>(-2),
                static_cast<T>(16),
                static_cast<T>(-16),
                static_cast<T>(64)
            };
            if (iter < kEdges.size()) return kEdges[iter];
        } else {
            static constexpr std::array<T, 8> kEdges = {
                static_cast<T>(0),
                static_cast<T>(1),
                std::numeric_limits<T>::max(),
                static_cast<T>(std::numeric_limits<T>::max() - 1),
                static_cast<T>(2),
                static_cast<T>(16),
                static_cast<T>(64),
                static_cast<T>(255)
            };
            if (iter < kEdges.size()) return kEdges[iter];
        }
        return static_cast<T>(rng.next_u64());
    }
};

template <>
struct Arbitrary<std::byte> {
    static auto generate(FuzzRng& rng, std::size_t iter) -> std::byte {
        return static_cast<std::byte>(Arbitrary<std::uint8_t>::generate(rng, iter));
    }
};

template <typename T>
struct Arbitrary<T, std::enable_if_t<std::is_floating_point_v<T>>> {
    static auto generate(FuzzRng& rng, std::size_t iter) -> T {
        static constexpr std::array<T, 8> kEdges = {
            static_cast<T>(0.0),
            static_cast<T>(-0.0),
            static_cast<T>(1.0),
            static_cast<T>(-1.0),
            static_cast<T>(0.5),
            static_cast<T>(-0.5),
            static_cast<T>(1e6),
            static_cast<T>(-1e6)
        };
        if (iter < kEdges.size()) return kEdges[iter];
        const auto raw = static_cast<std::int64_t>(rng.next_u64() % 2000001ULL) - 1000000LL;
        return static_cast<T>(raw) / static_cast<T>(1000.0);
    }
};

template <>
struct Arbitrary<std::string> {
    static auto generate(FuzzRng& rng, std::size_t iter) -> std::string {
        static const std::array<std::string_view, 16> kEdges = {
            std::string_view{"", 0},
            " ",
            "\t\r\n",
            "0",
            "-1",
            "true",
            "false",
            "null",
            std::string_view{"a\0b", 3},
            "🔥🚀✨",
            "\\\"\\\\/\b\f\n\r\t",
            "' OR '1'='1",
            "../../etc/passwd",
            "{}",
            "[]",
            "{\"id\":1,\"ok\":true}"
        };
        if (iter < kEdges.size()) return std::string(kEdges[iter]);
        const std::size_t len = rng.next_range(33);
        std::string s;
        s.reserve(len);
        for (std::size_t i = 0; i < len; ++i) {
            const auto ch = static_cast<char>(32 + rng.next_range(95));
            s.push_back(ch);
        }
        return s;
    }
};

template <typename E>
struct Arbitrary<std::vector<E>> {
    static auto generate(FuzzRng& rng, std::size_t iter) -> std::vector<E> {
        if (iter == 0) return {};
        if (iter == 1) return {Arbitrary<E>::generate(rng, 0)};
        const std::size_t len = rng.next_range(17);
        std::vector<E> out;
        out.reserve(len);
        for (std::size_t i = 0; i < len; ++i) {
            out.push_back(Arbitrary<E>::generate(rng, iter + i + 2));
        }
        return out;
    }
};

template <typename E, std::size_t N>
struct Arbitrary<std::array<E, N>> {
    static auto generate(FuzzRng& rng, std::size_t iter) -> std::array<E, N> {
        std::array<E, N> out{};
        for (std::size_t i = 0; i < N; ++i) {
            out[i] = Arbitrary<E>::generate(rng, iter + i);
        }
        return out;
    }
};

// --- Type-Directed Counterexample Shrinking (TEST-02) ---
template <typename T>
[[nodiscard]] inline auto shrink_candidates(const T& val) -> std::vector<T> {
    using Decayed = std::decay_t<T>;
    std::vector<T> out;
    if constexpr (std::is_same_v<Decayed, bool>) {
        if (val) out.push_back(false);
    } else if constexpr (std::is_integral_v<Decayed>) {
        if (val == 0) return out;
        out.push_back(static_cast<T>(0));
        if constexpr (std::is_signed_v<Decayed>) {
            if (val < 0 && val != std::numeric_limits<T>::min()) {
                out.push_back(static_cast<T>(-val));
            }
        }
        for (int shift = 1; shift <= 8; ++shift) {
            T delta = static_cast<T>(val / (static_cast<T>(1) << shift));
            if (delta == 0) break;
            T cand = static_cast<T>(val - delta);
            if (cand != 0 && cand != val) out.push_back(cand);
        }
        T toward_zero = (val > 0) ? static_cast<T>(val - 1) : static_cast<T>(val + 1);
        if (toward_zero != 0 && toward_zero != val) {
            out.push_back(toward_zero);
        }
    } else if constexpr (std::is_floating_point_v<Decayed>) {
        if (val != static_cast<T>(0.0)) {
            out.push_back(static_cast<T>(0.0));
            T half = val / static_cast<T>(2.0);
            if (half != val && half != static_cast<T>(0.0)) out.push_back(half);
        }
    } else if constexpr (std::is_same_v<Decayed, std::string>) {
        if (val.empty()) return out;
        out.emplace_back("");
        if (val.size() > 1) {
            out.push_back(val.substr(0, val.size() / 2));
            out.push_back(val.substr(val.size() / 2));
        }
        out.push_back(val.substr(0, val.size() - 1));
        if (val.size() > 1) {
            out.push_back(val.substr(1));
        }
    }
    return out;
}

template <typename E>
[[nodiscard]] inline auto shrink_candidates(const std::vector<E>& val) -> std::vector<std::vector<E>> {
    std::vector<std::vector<E>> out;
    if (val.empty()) return out;
    out.emplace_back();
    if (val.size() > 1) {
        out.emplace_back(val.begin(), val.begin() + static_cast<std::ptrdiff_t>(val.size() / 2));
        out.emplace_back(val.begin() + static_cast<std::ptrdiff_t>(val.size() / 2), val.end());
    }
    out.emplace_back(val.begin(), val.end() - 1);
    return out;
}

template <typename T>
[[nodiscard]] inline auto format_fuzz_arg(const T& v) -> std::string {
    return format_debug_val(v);
}

template <typename E>
[[nodiscard]] inline auto format_fuzz_arg(const std::vector<E>& v) -> std::string {
    std::string out = "[";
    for (std::size_t i = 0; i < v.size(); ++i) {
        if (i > 0) out += ", ";
        out += format_fuzz_arg(v[i]);
    }
    out += "]";
    return out;
}

// --- Automatic Resource-Leak Canary (TEST-04) ---
[[nodiscard]] inline auto count_open_fds() noexcept -> std::size_t {
    std::error_code ec;
    std::size_t count = 0;
#if defined(__APPLE__)
    static constexpr const char* kFdDir = "/dev/fd";
#else
    static constexpr const char* kFdDir = "/proc/self/fd";
#endif
    for (auto it = std::filesystem::directory_iterator(kFdDir, ec);
         !ec && it != std::filesystem::directory_iterator();
         it.increment(ec)) {
        ++count;
    }
    return count;
}

// --- Statistical Micro-Benchmarking Harness (TEST-05b) ---
struct BenchStats {
    std::string name{};
    std::uint64_t total_iters = 0;
    double min_ns = 0.0;
    double p50_ns = 0.0;
    double mean_ns = 0.0;
    double p99_ns = 0.0;
    double stddev_pct = 0.0;
    double ops_per_sec = 0.0;
};

template <typename Fn>
[[nodiscard]] inline auto run_benchmark(
    std::string_view name,
    Fn&& fn,
    double budget_ms = 200.0,
    bool smoke_only = false
) -> BenchStats {
    using Clock = std::chrono::steady_clock;
    BenchStats st;
    st.name = std::string(name);
    if (smoke_only) {
        auto t0 = Clock::now();
        fn();
        auto t1 = Clock::now();
        double ns = std::max(1.0, std::chrono::duration<double, std::nano>(t1 - t0).count());
        st.total_iters = 1;
        st.min_ns = ns;
        st.p50_ns = ns;
        st.mean_ns = ns;
        st.p99_ns = ns;
        st.stddev_pct = 0.0;
        st.ops_per_sec = 1e9 / ns;
        return st;
    }

    // 1. Warmup phase (~15ms or at least 4 calls)
    {
        auto w_start = Clock::now();
        std::size_t w_iters = 0;
        while (w_iters < 4 || std::chrono::duration<double, std::milli>(Clock::now() - w_start).count() < 15.0) {
            fn();
            ++w_iters;
            if (w_iters >= 100000) break;
        }
    }

    // 2. Calibrate batch size B so one batch takes at least ~0.2ms
    std::uint64_t batch_size = 1;
    while (batch_size < 10000000ULL) {
        auto t0 = Clock::now();
        for (std::uint64_t i = 0; i < batch_size; ++i) {
            fn();
        }
        auto t1 = Clock::now();
        double elapsed_ns = std::chrono::duration<double, std::nano>(t1 - t0).count();
        if (elapsed_ns >= 200000.0) { // >= 0.2 ms
            break;
        }
        if (elapsed_ns < 10000.0) {
            batch_size *= 10;
        } else {
            batch_size *= 2;
        }
    }

    // 3. Collect sample rounds across budget_ms (at least 10 samples, up to 25 samples)
    std::vector<double> samples_ns;
    samples_ns.reserve(25);
    auto b_start = Clock::now();
    while (samples_ns.size() < 10 ||
           (samples_ns.size() < 25 &&
            std::chrono::duration<double, std::milli>(Clock::now() - b_start).count() < budget_ms)) {
        auto t0 = Clock::now();
        for (std::uint64_t i = 0; i < batch_size; ++i) {
            fn();
        }
        auto t1 = Clock::now();
        double per_iter_ns = std::max(0.01, std::chrono::duration<double, std::nano>(t1 - t0).count() / static_cast<double>(batch_size));
        samples_ns.push_back(per_iter_ns);
        st.total_iters += batch_size;
    }

    std::sort(samples_ns.begin(), samples_ns.end());
    const std::size_t n = samples_ns.size();
    st.min_ns = samples_ns.front();
    st.p50_ns = samples_ns[n / 2];
    st.p99_ns = samples_ns[std::min(n - 1, (n * 99) / 100)];

    double sum = 0.0;
    for (double v : samples_ns) sum += v;
    st.mean_ns = sum / static_cast<double>(n);

    double sq_diff = 0.0;
    for (double v : samples_ns) {
        double d = v - st.mean_ns;
        sq_diff += d * d;
    }
    double stddev = std::sqrt(sq_diff / static_cast<double>(n));
    st.stddev_pct = (st.mean_ns > 0.0) ? (stddev / st.mean_ns) * 100.0 : 0.0;
    st.ops_per_sec = (st.mean_ns > 0.0) ? (1e9 / st.mean_ns) : 0.0;
    return st;
}

inline void print_bench_row(const BenchStats& st) {
    std::println("  [BENCH] {:<36} {:>9} iters | {:>8.1f} ns/op (p50: {:>7.1f} ns, p99: {:>7.1f} ns, ±{:>4.1f}%) | {:>10.0f} ops/s",
        st.name, st.total_iters, st.mean_ns, st.p50_ns, st.p99_ns, st.stddev_pct, st.ops_per_sec);
}
} // namespace testing_detail

// Compiler optimization barrier for benchmarks (`black_box(val)`) (TEST-05b)
template <typename T>
inline auto black_box(T&& val) noexcept -> std::decay_t<T> {
    std::decay_t<T> copy = std::forward<T>(val);
#if defined(__GNUC__) || defined(__clang__)
    asm volatile("" : "+r,m"(copy) : : "memory");
#endif
    return copy;
}

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
            throw testing_detail::TestAssertionFailure{};
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

template <typename L, typename R, typename Pred>
inline void assert_binop_at(
    const L& lhs,
    const R& rhs,
    Pred&& pred,
    std::string_view op_str,
    std::string_view msg,
    std::string_view file,
    int line,
    std::string_view expr_text
) {
    if (!pred(lhs, rhs)) {
        std::string detail = std::format(
            "{} [left: {}, right: {}]",
            msg,
            testing_detail::format_fuzz_arg(lhs),
            testing_detail::format_fuzz_arg(rhs));
        std::string full_expr = expr_text.empty()
            ? std::format("left {} right", op_str)
            : std::string(expr_text);
        assert_true_at(false, detail, file, line, full_expr);
    }
}

template <typename L, typename Elem, typename Pred>
inline void assert_binop_at(
    const std::vector<L>& lhs,
    std::initializer_list<Elem> rhs_init,
    Pred&& pred,
    std::string_view op_str,
    std::string_view msg,
    std::string_view file,
    int line,
    std::string_view expr_text
) {
    std::vector<L> rhs;
    rhs.reserve(rhs_init.size());
    for (const auto& item : rhs_init) {
        rhs.push_back(static_cast<L>(item));
    }
    assert_binop_at(lhs, rhs, std::forward<Pred>(pred), op_str, msg, file, line, expr_text);
}

template <typename L, typename Elem, typename Pred>
inline void assert_binop_at(
    std::initializer_list<L> lhs_init,
    std::initializer_list<Elem> rhs_init,
    Pred&& pred,
    std::string_view op_str,
    std::string_view msg,
    std::string_view file,
    int line,
    std::string_view expr_text
) {
    using ValT = std::conditional_t<std::is_convertible_v<L, std::string_view>, std::string, L>;
    std::vector<ValT> lhs;
    lhs.reserve(lhs_init.size());
    for (const auto& item : lhs_init) lhs.push_back(static_cast<ValT>(item));
    assert_binop_at(lhs, rhs_init, std::forward<Pred>(pred), op_str, msg, file, line, expr_text);
}

[[noreturn]] inline void panic(
    std::string_view msg,
    std::string_view file = "",
    int line = 0
) {
    assert_true_at(false, msg, file, line, "panic");
    std::abort();
}

inline void assert_true(bool cond, std::string_view msg = "assertion failed") {
    assert_true_at(cond, msg, "", 0, "");
}

} // namespace cfire

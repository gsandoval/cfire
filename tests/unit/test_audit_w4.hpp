#pragma once
// tests/unit/test_audit_w4.hpp — Wave W4 (uniform builtin API, breaking 0.16) regression tests.

#include "../test_harness.hpp"
#include "../../src/sema/builtin_methods.hpp"
#include "core/builtin_methods.hpp"
#include "core/math.hpp"

// Q16: `cfire fix --only cfire/RETIRED-API` rewrites every retired spelling into the new API, and nothing else.
void test_w4_migration_rewrites([[maybe_unused]] const std::filesystem::path& rt_dir) {
    std::println("=== RUN   test_w4_migration_rewrites (Q16)");
    ScopedTempDir dir("cfire_w4_migrate");
    auto file = dir.file("main.cf", R"(package main
import "core:str"
import "core:math"
import "core:sort"
import "core:sync"
import "core:net"
import "core:net/http"

type Box struct {
    v: i32
}

fn main() {
    raw := "  a,b,c  "
    t := str.trim(raw)
    parts := str.split(t, ",")
    up := str.to_upper(str.join(parts, "-"))
    n := str.parse_i32("12") or 0
    m := "7".parse_i64() or 0
    c := math.clamp_i64(i64(n) + m, 0, 10)
    r := math.sqrt(2.0 * 8.0)
    s := math.checked_add(c, 5) or 0
    mut xs: []i32 = [3, 1, 2]
    sort.slice(mut xs)
    p := math.pi()
    k := xs.len
    e := xs.empty()
    ch := sync.make_channel[i32](4)
    mu := sync.make_mutex(0)
    lk := sync.new_mutex()
    w := make[own[Box]](Box{v: 1})
    mut rt := http.make_router()
    mut ln := net.listen_tcp_addr("127.0.0.1", 0) or { return }
    q := http.get_insecure("https://127.0.0.1:1/") or { return }
}
)");
    auto res = run_cfire(std::format("fix --only cfire/RETIRED-API \"{}\"", file.string()));
    require(res.exit_code == 0, std::format("W4: cfire fix --only cfire/RETIRED-API exits 0, got: {}", res.output));
    auto fixed = cfire::build::read_file_text(file);
    require(fixed.has_value(), "W4: migrated file readable");
    if (!fixed) return;
    const std::string want = R"(package main
import "core:math"
import "core:sync"
import "core:net"
import "core:net/http"

type Box struct {
    v: i32
}

fn main() {
    raw := "  a,b,c  "
    t := raw.trim()
    parts := t.split(",")
    up := parts.join("-").to_upper()
    n := i32.parse("12") or 0
    m := i64.parse("7") or 0
    c := (i64(n) + m).clamp(0, 10)
    r := (2.0 * 8.0).sqrt()
    s := c.checked_add(5) or 0
    mut xs: []i32 = [3, 1, 2]
    xs.sort()
    p := math.PI
    k := xs.len()
    e := xs.is_empty()
    ch := make(sync.Channel[i32], 4)
    mu := make(sync.Mutex[i64], 0)
    lk := make(sync.Mutex[void])
    w := make(own[Box], Box{v: 1})
    mut rt := make(http.Router)
    mut ln := net.listen(0, "127.0.0.1") or { return }
    q := http.get("https://127.0.0.1:1/", verify_peer: false) or { return }
}
)";
    require(*fixed == want, std::format("W4: migrated source matches the new API exactly, got:\n{}", *fixed));

    // A second run finds nothing left to migrate, and the result type-checks with no RETIRED-API errors.
    auto again = run_cfire(std::format("fix --only cfire/RETIRED-API \"{}\"", file.string()));
    require(again.output.contains("No fixes needed"), std::format("W4: migration is idempotent, got: {}", again.output));
    auto check = run_cfire(std::format("check \"{}\"", file.string()));
    require(!check.output.contains("cfire/RETIRED-API"), std::format("W4: migrated file has no retired spellings, got: {}", check.output));

    // `--only` leaves other fixes alone: a missing `try` stays an error the user must look at.
    auto neg = dir.file("neg.cf", "package main\nimport \"core:fs\"\nfn main() -> void! {\n    s := fs.read_text(\"a.txt\")\n    t := str.trim(s)\n}\n");
    (void)run_cfire(std::format("fix --only cfire/RETIRED-API \"{}\"", neg.string()));
    auto neg_txt = cfire::build::read_file_text(neg);
    require(neg_txt && neg_txt->contains("    s := fs.read_text(\"a.txt\")\n") && neg_txt->contains("t := s.trim()"),
        "W4: --only applies just the requested codes (no 'try' inserted)");
    std::println("--- PASS: test_w4_migration_rewrites (Q16)");
}

// The table is the single source: runtime overloads behave as documented, including the UB-free edges.
void test_w4_builtin_runtime_semantics([[maybe_unused]] const std::filesystem::path& rt_dir) {
    std::println("=== RUN   test_w4_builtin_runtime_semantics (CF-162/CF-163/CF-170/CF-171)");
    namespace bm = cfire::bm;
    // Integers: checked/wrapping/saturating at the edges, euclidean division.
    constexpr auto i32min = std::numeric_limits<std::int32_t>::min();
    require(!bm::checked_abs(i32min).has_value() && bm::wrapping_abs(i32min) == i32min, "W4: abs at MIN");
    require(!bm::checked_div(i32min, -1).has_value() && bm::saturating_div(i32min, -1) == std::numeric_limits<std::int32_t>::max(), "W4: MIN / -1");
    require(bm::rem_euclid(-7, 3) == 2 && bm::div_euclid(-7, 3) == -3 && bm::rem_euclid(7, -3) == 1, "W4: euclidean division");
    require(bm::rem_euclid(std::numeric_limits<std::int64_t>::min(), std::int64_t{-1}) == 0, "W4: rem_euclid(MIN, -1) is 0, not UB");
    require(bm::saturating_add(std::uint8_t{250}, 10) == 255 && bm::saturating_sub(std::uint8_t{3}, 10) == 0, "W4: saturating unsigned");
    require(bm::wrapping_mul(std::uint16_t{65535}, std::uint16_t{65535}) == 1, "W4: wrapping_mul does not overflow through int promotion");
    require(bm::count_ones(std::uint32_t{0xF0}) == 4 && bm::leading_zeros(std::uint8_t{1}) == 7 && !bm::is_power_of_two(0) && bm::is_power_of_two(64), "W4: bit queries");
    require(bm::pow(std::int64_t{2}, 10) == 1024, "W4: integer pow");
    // Floats: NaN-aware min/max, signum of -0.0, fract.
    const double nan = std::numeric_limits<double>::quiet_NaN();
    require(bm::min(nan, 3.0) == 3.0 && bm::max(2.0, nan) == 2.0 && std::signbit(bm::signum(-0.0)) && bm::fract(-2.75) == -0.75, "W4: float edges");
    require(cfire::mod(5.5, 2.0) == 1.5 && std::isnan(cfire::mod(1.0, 0.0)), "W4: float % is fmod");
    // str: views, optional positions, UTF-8.
    std::string s = "  héllo  ";
    require(bm::trim(s) == "héllo" && bm::index_of(s, "l") == std::optional<std::size_t>{5} && !bm::index_of(s, "z"), "W4: trim / index_of");
    require(bm::index_of(std::string_view("abcabc"), "b", 2) == std::optional<std::size_t>{4} && !bm::index_of(std::string_view("ab"), "a", 9), "W4: index_of start");
    require(bm::rune_count("héllo") == 5 && bm::runes("\xff" "a") == std::vector<std::uint32_t>{0xFFFD, 'a'}, "W4: runes decode with U+FFFD");
    require(bm::split_n("a=b=c", "=", 2) == std::vector<std::string>{"a", "b=c"} && bm::split("", ",") == std::vector<std::string>{""}, "W4: split edges");
    require(bm::lines("a\r\nb\n") == std::vector<std::string>{"a", "b"}, "W4: lines drops \\r and the final empty line");
    require(bm::strip_prefix(std::string_view("--x"), "--") == std::optional<std::string_view>{"x"} && !bm::strip_suffix(std::string_view("a.c"), ".cf"), "W4: strip_*");
    require(bm::replace("a.b.c", ".", "-") == "a-b.c" && bm::replace_all("a.b.c", ".", "-") == "a-b-c" && bm::replace_all("x", "", "y") == "x", "W4: replace");
    require(cfire::bm::parse<std::uint8_t>("300") == std::nullopt && cfire::bm::parse<std::int32_t>(" -4 ") == std::optional<std::int32_t>{-4} &&
            cfire::bm::parse<bool>("TRUE") == std::optional<bool>{true}, "W4: T.parse");
    // Slices and maps.
    std::vector<int> xs{3, 1, 2};
    bm::sort(xs);
    bm::extend(xs, xs);
    require(xs == std::vector<int>{1, 2, 3, 1, 2, 3}, "W4: extend with itself copies first");
    require(bm::swap_remove(xs, 0) == 1 && xs.front() == 3 && bm::pop(xs) == std::optional<int>{2}, "W4: swap_remove / pop");
    cfire::Map<std::string, int> m;
    require(!bm::insert(m, std::string("k"), 1) && bm::insert(m, std::string("k"), 2) == std::optional<int>{1} &&
            bm::remove(m, std::string("k")) == std::optional<int>{2} && !bm::remove(m, std::string("k")), "W4: map insert/remove return previous");
    // math module.
    require(cfire::math::gcd(12, 18) == 6 && cfire::math::lcm(4, 6) == 12 && cfire::math::gcd(0, 0) == 0, "W4: gcd / lcm");
    // Every table row that names a receiver kind has a summary and an example (the reference is generated from it).
    for (const auto& m2 : cfire::frontend::kBuiltinMethods) {
        require(!m2.summary.empty() && !m2.example.empty() && m2.recv != 0, std::format("W4: table row '{}' is documented", m2.name));
    }
    std::println("--- PASS: test_w4_builtin_runtime_semantics (CF-162/CF-163/CF-170/CF-171)");
}

// CF-180: dual-stack listener, IPv6 peers, and invalid listen addresses are errors (not a bind to every interface).
void test_cf180_dual_stack_listener([[maybe_unused]] const std::filesystem::path& rt_dir) {
    std::println("=== RUN   test_cf180_dual_stack_listener (CF-180)");
    auto bad = cfire::net::listen(0, "not-an-ip");
    require(!bad.has_value(), "CF-180: an invalid listen address is an error");
    auto ln = cfire::net::listen(0);
    require(ln.has_value() && ln->port() != 0, "CF-180: wildcard listener binds");
    if (ln && ln->port() != 0) {
        auto c4 = cfire::net::connect("127.0.0.1", ln->port());
        auto s4 = ln->accept();
        require(c4.has_value() && s4.has_value() && s4->remote_addr() == "127.0.0.1", "CF-180: IPv4 client is reported as plain IPv4");
        auto c6 = cfire::net::connect("::1", ln->port());
        if (c6) {  // hosts without an IPv6 loopback skip this half
            auto s6 = ln->accept();
            require(s6.has_value() && s6->remote_addr() == "::1", "CF-180: IPv6 client address is reported");
        }
    }
    std::println("--- PASS: test_cf180_dual_stack_listener (CF-180)");
}

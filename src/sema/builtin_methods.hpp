#pragma once
// src/sema/builtin_methods.hpp — The single table of methods on builtin types (Q7, Q8, Q14; W4).
//
// Sema reads it for the closed method surface, arity, argument and return types; codegen lowers every entry
// with a runtime name to `cfire::bm::<name>(receiver, args...)` (runtime/core/builtin_methods.hpp); docgen
// renders it as the builtin-method reference. Adding a method means one row here plus its runtime overload.

#include <array>
#include <cstdint>
#include <string_view>
#include <vector>

namespace cfire::frontend {

// Receiver kinds, as a bitmask so one row can serve several kinds (`len` on str, slices, arrays and maps).
enum BmRecv : std::uint8_t {
    BmInt = 1u << 0,
    BmFloat = 1u << 1,
    BmStr = 1u << 2,
    BmSlice = 1u << 3,
    BmArray = 1u << 4,
    BmMap = 1u << 5,
    BmBool = 1u << 6,
};
inline constexpr std::uint8_t BmNum = BmInt | BmFloat;
inline constexpr std::uint8_t BmSeq = BmSlice | BmArray;
inline constexpr std::uint8_t BmSized = BmStr | BmSlice | BmArray | BmMap;

struct BuiltinMethod {
    std::string_view name;
    std::uint8_t recv;
    // Parameter types, comma separated, using the placeholders below. `fn` is a closure argument.
    //   Self = the receiver type, T = slice/array element, K/V = map key/value.
    std::string_view params;
    // Return type with the same placeholders; `?` suffix = optional, `[]` prefix = slice, `&str` = view.
    std::string_view ret;
    bool mutates;      // changes the receiver: needs a `mut` binding and invalidates loans (CF-143)
    bool borrows;      // the result is a view into the receiver and is loan-tracked
    bool lowered;      // lowered through `cfire::bm::<name>`; false keeps a dedicated codegen path
    std::string_view summary;
    std::string_view example;
};

// clang-format off
inline constexpr std::array kBuiltinMethods = {
    // --- Every sized container ---
    // `len` / `is_empty` keep their readable lowering (`.size()` / `.empty()`).
    BuiltinMethod{"len", BmSized, "", "usize", false, false, false, "Number of elements (bytes for `str`).", "n := xs.len()"},
    BuiltinMethod{"is_empty", BmSized, "", "bool", false, false, false, "`true` when `len() == 0`.", "if s.is_empty() { return }"},

    // --- Integers (every width) ---
    BuiltinMethod{"abs", BmNum, "", "Self", false, false, true, "Absolute value. Integer `MIN.abs()` panics; use `checked_abs` or `wrapping_abs`.", "d := (a - b).abs()"},
    BuiltinMethod{"min", BmNum, "Self", "Self", false, false, true, "Smaller of the two values (for floats, a NaN operand yields the other operand).", "lo := a.min(b)"},
    BuiltinMethod{"max", BmNum, "Self", "Self", false, false, true, "Larger of the two values (for floats, a NaN operand yields the other operand).", "hi := a.max(b)"},
    BuiltinMethod{"clamp", BmNum, "Self, Self", "Self", false, false, true, "Restricts the value to `lo..=hi`; panics when `lo > hi` (or either bound is NaN).", "v := x.clamp(0, 255)"},
    BuiltinMethod{"signum", BmNum, "", "Self", false, false, true, "`-1`, `0` or `1` (floats: `-1.0`, `±0.0`, `1.0`, NaN for NaN).", "s := delta.signum()"},
    BuiltinMethod{"pow", BmNum, "Self", "Self", false, false, true, "Integer power (a negative exponent or overflow panics) or float power.", "kb := base.pow(10)"},
    BuiltinMethod{"count_ones", BmInt, "", "u32", false, false, true, "Number of set bits.", "bits := mask.count_ones()"},
    BuiltinMethod{"leading_zeros", BmInt, "", "u32", false, false, true, "Number of leading zero bits.", "lz := x.leading_zeros()"},
    BuiltinMethod{"trailing_zeros", BmInt, "", "u32", false, false, true, "Number of trailing zero bits.", "tz := x.trailing_zeros()"},
    BuiltinMethod{"is_power_of_two", BmInt, "", "bool", false, false, true, "`true` for 1, 2, 4, …; `false` for zero and negatives.", "ok := n.is_power_of_two()"},
    BuiltinMethod{"div_euclid", BmInt, "Self", "Self", false, false, true, "Euclidean division (the remainder is never negative); panics on division by zero or overflow.", "q := a.div_euclid(b)"},
    BuiltinMethod{"rem_euclid", BmNum, "Self", "Self", false, false, true, "Euclidean remainder, always `>= 0`; integer division by zero panics.", "i := idx.rem_euclid(len)"},
    BuiltinMethod{"checked_add", BmInt, "Self", "Self?", false, false, true, "`none` on overflow.", "s := a.checked_add(b) or 0"},
    BuiltinMethod{"checked_sub", BmInt, "Self", "Self?", false, false, true, "`none` on overflow.", "s := a.checked_sub(b) or 0"},
    BuiltinMethod{"checked_mul", BmInt, "Self", "Self?", false, false, true, "`none` on overflow.", "p := a.checked_mul(b) or 0"},
    BuiltinMethod{"checked_div", BmInt, "Self", "Self?", false, false, true, "`none` on division by zero or overflow (`MIN / -1`).", "q := a.checked_div(b) or 0"},
    BuiltinMethod{"checked_neg", BmInt, "", "Self?", false, false, true, "`none` on overflow (`MIN`, or any non-zero unsigned value).", "n := a.checked_neg() or 0"},
    BuiltinMethod{"checked_abs", BmInt, "", "Self?", false, false, true, "`none` for `MIN`.", "d := a.checked_abs() or 0"},
    BuiltinMethod{"wrapping_add", BmInt, "Self", "Self", false, false, true, "Two's-complement wrapping addition.", "h := h.wrapping_add(c)"},
    BuiltinMethod{"wrapping_sub", BmInt, "Self", "Self", false, false, true, "Two's-complement wrapping subtraction.", "d := a.wrapping_sub(b)"},
    BuiltinMethod{"wrapping_mul", BmInt, "Self", "Self", false, false, true, "Two's-complement wrapping multiplication.", "h := h.wrapping_mul(16777619)"},
    BuiltinMethod{"wrapping_div", BmInt, "Self", "Self", false, false, true, "Wrapping division (`MIN / -1 == MIN`); division by zero panics.", "q := a.wrapping_div(b)"},
    BuiltinMethod{"wrapping_neg", BmInt, "", "Self", false, false, true, "Two's-complement wrapping negation.", "n := a.wrapping_neg()"},
    BuiltinMethod{"wrapping_abs", BmInt, "", "Self", false, false, true, "Absolute value, wrapping for `MIN`.", "d := a.wrapping_abs()"},
    BuiltinMethod{"saturating_add", BmInt, "Self", "Self", false, false, true, "Addition clamped to `MIN..=MAX`.", "s := a.saturating_add(b)"},
    BuiltinMethod{"saturating_sub", BmInt, "Self", "Self", false, false, true, "Subtraction clamped to `MIN..=MAX`.", "s := a.saturating_sub(b)"},
    BuiltinMethod{"saturating_mul", BmInt, "Self", "Self", false, false, true, "Multiplication clamped to `MIN..=MAX`.", "p := a.saturating_mul(b)"},
    BuiltinMethod{"saturating_div", BmInt, "Self", "Self", false, false, true, "Division clamped to `MIN..=MAX` (`MIN / -1 == MAX`); division by zero panics.", "q := a.saturating_div(b)"},
    BuiltinMethod{"saturating_neg", BmInt, "", "Self", false, false, true, "Negation clamped to `MIN..=MAX`.", "n := a.saturating_neg()"},
    // Shifts and explicit conversions keep their dedicated lowering (CF-165/CF-166).
    BuiltinMethod{"wrapping_shl", BmInt, "u32", "Self", false, false, false, "Left shift with the count taken modulo the bit width.", "x := x.wrapping_shl(n)"},
    BuiltinMethod{"wrapping_shr", BmInt, "u32", "Self", false, false, false, "Right shift with the count taken modulo the bit width.", "x := x.wrapping_shr(n)"},
    BuiltinMethod{"checked_shl", BmInt, "u32", "Self?", false, false, false, "`none` when the count is `>=` the bit width.", "x := v.checked_shl(n) or 0"},
    BuiltinMethod{"checked_shr", BmInt, "u32", "Self?", false, false, false, "`none` when the count is `>=` the bit width.", "x := v.checked_shr(n) or 0"},
    BuiltinMethod{"wrapping_as", BmNum, "", "", false, false, false, "Conversion to `T` that wraps on overflow: `x.wrapping_as[u8]()`.", "b := x.wrapping_as[u8]()"},
    BuiltinMethod{"saturating_as", BmNum, "", "", false, false, false, "Conversion to `T` that clamps on overflow: `x.saturating_as[u8]()`.", "b := x.saturating_as[u8]()"},
    BuiltinMethod{"try_as", BmNum, "", "", false, false, false, "Conversion to `T?`, `none` when the value does not fit: `x.try_as[u8]()`.", "b := x.try_as[u8]() or 0"},

    // --- Floats ---
    BuiltinMethod{"floor", BmFloat, "", "Self", false, false, true, "Largest integer value `<=` x.", "f := x.floor()"},
    BuiltinMethod{"ceil", BmFloat, "", "Self", false, false, true, "Smallest integer value `>=` x.", "c := x.ceil()"},
    BuiltinMethod{"round", BmFloat, "", "Self", false, false, true, "Nearest integer value, halfway cases away from zero.", "r := x.round()"},
    BuiltinMethod{"trunc", BmFloat, "", "Self", false, false, true, "Integer part, rounding toward zero.", "t := x.trunc()"},
    BuiltinMethod{"fract", BmFloat, "", "Self", false, false, true, "Fractional part, `x - x.trunc()`.", "f := x.fract()"},
    BuiltinMethod{"sqrt", BmFloat, "", "Self", false, false, true, "Square root (NaN for negative input).", "d := (dx * dx + dy * dy).sqrt()"},
    BuiltinMethod{"cbrt", BmFloat, "", "Self", false, false, true, "Cube root.", "c := v.cbrt()"},
    BuiltinMethod{"powi", BmFloat, "i32", "Self", false, false, true, "Raises to an integer power.", "a := r.powi(2)"},
    BuiltinMethod{"exp", BmFloat, "", "Self", false, false, true, "`e` raised to x.", "y := x.exp()"},
    BuiltinMethod{"ln", BmFloat, "", "Self", false, false, true, "Natural logarithm.", "y := x.ln()"},
    BuiltinMethod{"log2", BmFloat, "", "Self", false, false, true, "Base-2 logarithm.", "bits := n.log2()"},
    BuiltinMethod{"log10", BmFloat, "", "Self", false, false, true, "Base-10 logarithm.", "db := 20.0 * a.log10()"},
    BuiltinMethod{"sin", BmFloat, "", "Self", false, false, true, "Sine (radians).", "y := t.sin()"},
    BuiltinMethod{"cos", BmFloat, "", "Self", false, false, true, "Cosine (radians).", "x := t.cos()"},
    BuiltinMethod{"tan", BmFloat, "", "Self", false, false, true, "Tangent (radians).", "m := t.tan()"},
    BuiltinMethod{"asin", BmFloat, "", "Self", false, false, true, "Arcsine, in radians.", "a := y.asin()"},
    BuiltinMethod{"acos", BmFloat, "", "Self", false, false, true, "Arccosine, in radians.", "a := x.acos()"},
    BuiltinMethod{"atan", BmFloat, "", "Self", false, false, true, "Arctangent, in radians (see `math.atan2` for two inputs).", "a := m.atan()"},
    BuiltinMethod{"sinh", BmFloat, "", "Self", false, false, true, "Hyperbolic sine.", "y := x.sinh()"},
    BuiltinMethod{"cosh", BmFloat, "", "Self", false, false, true, "Hyperbolic cosine.", "y := x.cosh()"},
    BuiltinMethod{"tanh", BmFloat, "", "Self", false, false, true, "Hyperbolic tangent.", "y := x.tanh()"},
    BuiltinMethod{"to_degrees", BmFloat, "", "Self", false, false, true, "Converts radians to degrees.", "deg := rad.to_degrees()"},
    BuiltinMethod{"to_radians", BmFloat, "", "Self", false, false, true, "Converts degrees to radians.", "rad := deg.to_radians()"},
    BuiltinMethod{"is_nan", BmFloat, "", "bool", false, false, true, "`true` for NaN.", "if x.is_nan() { return }"},
    BuiltinMethod{"is_inf", BmFloat, "", "bool", false, false, true, "`true` for `+inf` or `-inf`.", "if x.is_inf() { return }"},
    BuiltinMethod{"is_finite", BmFloat, "", "bool", false, false, true, "`true` unless NaN or infinite.", "ok := x.is_finite()"},

    // --- str ---
    BuiltinMethod{"contains", BmStr, "&str", "bool", false, false, true, "`true` if the substring occurs.", "if path.contains(\"..\") { return }"},
    BuiltinMethod{"starts_with", BmStr, "&str", "bool", false, false, true, "`true` if `s` begins with `prefix`.", "if line.starts_with(\"#\") { continue }"},
    BuiltinMethod{"ends_with", BmStr, "&str", "bool", false, false, true, "`true` if `s` ends with `suffix`.", "if name.ends_with(\".cf\") { ... }"},
    BuiltinMethod{"index_of", BmStr, "&str, usize=", "usize?", false, false, true, "Byte position of the first occurrence at or after `start` (default 0), or `none`.", "dot := path.index_of(\".\") or { return }"},
    BuiltinMethod{"last_index_of", BmStr, "&str", "usize?", false, false, true, "Byte position of the last occurrence, or `none`.", "slash := path.last_index_of(\"/\") or 0"},
    BuiltinMethod{"split", BmStr, "&str", "[]str", false, false, true, "Splits on every occurrence of `sep` (an empty `s` gives one empty part).", "parts := line.split(\",\")"},
    BuiltinMethod{"split_n", BmStr, "&str, usize", "[]str", false, false, true, "Splits into at most `n` parts; the last part holds the rest.", "kv := line.split_n(\"=\", 2)"},
    BuiltinMethod{"lines", BmStr, "", "[]str", false, false, true, "Splits on `\\n`, dropping a trailing `\\r` from each line and a final empty line.", "for line in text.lines() { ... }"},
    BuiltinMethod{"trim", BmStr, "", "&str", false, true, true, "View without leading and trailing ASCII whitespace.", "name := raw.trim()"},
    BuiltinMethod{"trim_start", BmStr, "", "&str", false, true, true, "View without leading ASCII whitespace.", "body := text.trim_start()"},
    BuiltinMethod{"trim_end", BmStr, "", "&str", false, true, true, "View without trailing ASCII whitespace.", "line := raw.trim_end()"},
    BuiltinMethod{"strip_prefix", BmStr, "&str", "&str?", false, true, true, "View after `prefix`, or `none` if `s` does not start with it.", "rest := arg.strip_prefix(\"--\") or { continue }"},
    BuiltinMethod{"strip_suffix", BmStr, "&str", "&str?", false, true, true, "View before `suffix`, or `none` if `s` does not end with it.", "stem := file.strip_suffix(\".cf\") or file"},
    BuiltinMethod{"replace", BmStr, "&str, &str", "str", false, false, true, "Copy with the first occurrence of `from` replaced.", "s2 := s.replace(\"a\", \"b\")"},
    BuiltinMethod{"replace_all", BmStr, "&str, &str", "str", false, false, true, "Copy with every occurrence of `from` replaced.", "s2 := s.replace_all(\"\\t\", \"    \")"},
    BuiltinMethod{"repeat", BmStr, "usize", "str", false, false, true, "`s` concatenated `n` times.", "rule := \"-\".repeat(40)"},
    BuiltinMethod{"to_upper", BmStr, "", "str", false, false, true, "ASCII upper-case copy.", "tag := name.to_upper()"},
    BuiltinMethod{"to_lower", BmStr, "", "str", false, false, true, "ASCII lower-case copy.", "key := header.to_lower()"},
    BuiltinMethod{"runes", BmStr, "", "[]u32", false, false, true, "Unicode code points (invalid UTF-8 bytes decode as U+FFFD).", "for r in s.runes() { ... }"},
    BuiltinMethod{"rune_count", BmStr, "", "usize", false, false, true, "Number of Unicode code points.", "n := s.rune_count()"},
    BuiltinMethod{"bytes", BmStr, "", "[]byte", false, false, false, "The UTF-8 bytes of `s`.", "raw := s.bytes()"},
    BuiltinMethod{"pop", BmStr | BmSlice, "", "T?", true, false, true, "Removes and returns the last element (last byte for `str`), or `none` when empty.", "last := stack.pop() or { break }"},

    // --- Slices and fixed arrays ---
    BuiltinMethod{"contains", BmSeq, "T", "bool", false, false, true, "`true` if an element equals `v`.", "if xs.contains(3) { ... }"},
    BuiltinMethod{"index_of", BmSeq, "T", "usize?", false, false, true, "Position of the first element equal to `v`, or `none`.", "i := xs.index_of(v) or { return }"},
    BuiltinMethod{"first", BmSeq, "", "T?", false, false, true, "Copy of the first element, or `none` when empty.", "head := xs.first() or 0"},
    BuiltinMethod{"last", BmSeq, "", "T?", false, false, true, "Copy of the last element, or `none` when empty.", "tail := xs.last() or 0"},
    BuiltinMethod{"reverse", BmSeq, "", "void", true, false, true, "Reverses the elements in place.", "xs.reverse()"},
    BuiltinMethod{"sort", BmSeq, "", "void", true, false, true, "Sorts in place, ascending.", "xs.sort()"},
    BuiltinMethod{"sort_by", BmSeq, "fn", "void", true, false, true, "Sorts in place with a strict `less(a, b)` comparator.", "xs.sort_by(|a, b| a.score > b.score)"},
    BuiltinMethod{"is_sorted", BmSeq, "", "bool", false, false, true, "`true` if the elements are in ascending order.", "assert(xs.is_sorted())"},
    BuiltinMethod{"binary_search", BmSeq, "T", "usize?", false, false, true, "Position of `v` in an ascending sequence, or `none`.", "i := xs.binary_search(42) or { return }"},
    BuiltinMethod{"join", BmSlice | BmArray, "&str", "str", false, false, true, "Concatenates a `[]str` with `sep` between elements.", "csv := fields.join(\",\")"},
    BuiltinMethod{"push", BmSlice, "T", "void", true, false, false, "Appends `v`.", "xs.push(4)"},
    BuiltinMethod{"insert", BmSlice, "usize, T", "void", true, false, true, "Inserts `v` at `i`, shifting later elements; panics when `i > len()`.", "xs.insert(0, head)"},
    BuiltinMethod{"remove", BmSlice, "usize", "T", true, false, true, "Removes and returns the element at `i`, shifting later elements; panics when out of range.", "x := xs.remove(0)"},
    BuiltinMethod{"swap_remove", BmSlice, "usize", "T", true, false, true, "Removes the element at `i` by moving the last one into its place (O(1)); panics when out of range.", "x := xs.swap_remove(i)"},
    BuiltinMethod{"truncate", BmSlice, "usize", "void", true, false, true, "Keeps the first `n` elements (no effect when `n >= len()`).", "xs.truncate(10)"},
    BuiltinMethod{"extend", BmSlice, "[]T", "void", true, false, true, "Appends a copy of every element of `other`.", "xs.extend(more)"},
    BuiltinMethod{"clear", BmSlice | BmMap, "", "void", true, false, true, "Removes every element.", "xs.clear()"},

    // --- Maps ---
    BuiltinMethod{"contains", BmMap, "K", "bool", false, false, true, "`true` if the key is present.", "if m.contains(\"id\") { ... }"},
    BuiltinMethod{"get", BmMap, "K", "V?", false, false, true, "Copy of the value for `k`, or `none`.", "port := cfg.get(\"port\") or 8080"},
    BuiltinMethod{"insert", BmMap, "K, V", "V?", true, false, true, "Sets `k` to `v`; returns the previous value, or `none`.", "old := m.insert(k, v)"},
    BuiltinMethod{"remove", BmMap, "K", "V?", true, false, true, "Removes `k`; returns its value, or `none` if absent.", "gone := m.remove(k)"},
    BuiltinMethod{"keys", BmMap, "", "[]K", false, false, true, "Copy of the keys, in unspecified order (Q27).", "for k in m.keys() { ... }"},
    BuiltinMethod{"values", BmMap, "", "[]V", false, false, true, "Copy of the values, in unspecified order (Q27).", "total := m.values().len()"},

    // --- Raw access for C interop (dedicated lowering) ---
    BuiltinMethod{"bytes", BmSlice, "", "[]byte", false, false, false, "Byte view of the slice's storage.", "raw := xs.bytes()"},
    BuiltinMethod{"with_ext", BmStr, "&str", "str", false, false, false, "Path with its extension replaced.", "out := path.with_ext(\".o\")"},
};
// clang-format on

[[nodiscard]] constexpr auto find_builtin_method(std::uint8_t recv_kind, std::string_view name) -> const BuiltinMethod* {
    for (const auto& m : kBuiltinMethods) {
        if ((m.recv & recv_kind) != 0 && m.name == name) return &m;
    }
    return nullptr;
}

// Parameters ending in `=` are optional (`index_of(needle, start = 0)`).
[[nodiscard]] constexpr auto builtin_param_count(const BuiltinMethod& m) -> std::size_t {
    if (m.params.empty()) return 0;
    std::size_t n = 1;
    for (char c : m.params) n += (c == ',') ? 1 : 0;
    return n;
}
[[nodiscard]] constexpr auto builtin_required_param_count(const BuiltinMethod& m) -> std::size_t {
    std::size_t n = 0;
    for (std::string_view rest = m.params; !rest.empty();) {
        auto comma = rest.find(',');
        std::string_view p = rest.substr(0, comma);
        while (!p.empty() && p.back() == ' ') p.remove_suffix(1);
        if (!p.ends_with('=')) ++n;
        if (comma == std::string_view::npos) break;
        rest.remove_prefix(comma + 1);
    }
    return n;
}

// Builtin associated items on numeric types (Q10): `i32.MAX`, `f64.NAN`. `{}` is the lowered C++ type.
struct BuiltinAssocConst {
    std::string_view name;
    std::uint8_t recv;
    std::string_view cpp;
    std::string_view summary;
};
// clang-format off
inline constexpr std::array kBuiltinAssocConsts = {
    BuiltinAssocConst{"MAX", BmInt, "std::numeric_limits<{}>::max()", "Largest value of the type."},
    BuiltinAssocConst{"MIN", BmInt, "std::numeric_limits<{}>::min()", "Smallest value of the type."},
    BuiltinAssocConst{"BITS", BmInt, "static_cast<std::uint32_t>(std::numeric_limits<std::make_unsigned_t<{}>>::digits)", "Width in bits (`u32`)."},
    BuiltinAssocConst{"MAX", BmFloat, "std::numeric_limits<{}>::max()", "Largest finite value."},
    BuiltinAssocConst{"MIN", BmFloat, "std::numeric_limits<{}>::lowest()", "Most negative finite value."},
    BuiltinAssocConst{"MIN_POSITIVE", BmFloat, "std::numeric_limits<{}>::min()", "Smallest positive normal value."},
    BuiltinAssocConst{"EPSILON", BmFloat, "std::numeric_limits<{}>::epsilon()", "Difference between `1.0` and the next representable value."},
    BuiltinAssocConst{"INF", BmFloat, "std::numeric_limits<{}>::infinity()", "Positive infinity."},
    BuiltinAssocConst{"NEG_INF", BmFloat, "(-std::numeric_limits<{}>::infinity())", "Negative infinity."},
    BuiltinAssocConst{"NAN", BmFloat, "std::numeric_limits<{}>::quiet_NaN()", "Not a number."},
};
// clang-format on

[[nodiscard]] constexpr auto find_builtin_assoc_const(std::uint8_t recv_kind, std::string_view name) -> const BuiltinAssocConst* {
    for (const auto& c : kBuiltinAssocConsts) {
        if ((c.recv & recv_kind) != 0 && c.name == name) return &c;
    }
    return nullptr;
}

// Q16 migration: retired module functions and the replacement `cfire fix` applies.
enum class ApiMigrationKind : std::uint8_t {
    CallToMethod,  // `ns.f(recv, rest...)`  ->  `recv.to(rest...)`
    Rename,        // `ns.f(`                ->  `to(`
    CallToConst,   // `ns.f()`               ->  `to`
    RenameFn,      // `ns.f(args)`           ->  `ns.to(args)` (keeps the caller's module alias)
    SwapToFn,      // `ns.f(a, b)`           ->  `ns.to(b, a)` (`ns.f(a)` -> `ns.to(0, a)`)
    AppendVerifyOff,  // `ns.f(args)`        ->  `ns.to(args, verify_peer: false)` (Q30)
    TlsServerFiles,   // `ns.make_tls_server(p, c, k)` -> `make(ns.Server, p, tls: ns.TlsConfig{cert_file: c, key_file: k})` (Q31)
    TlsServerPem,     // the same with `cert_pem` / `key_pem`
};
struct ApiMigration {
    std::string_view ns;
    std::string_view fn;
    ApiMigrationKind kind;
    std::string_view to;
};
// clang-format off
inline constexpr std::array kApiMigrations = {
    ApiMigration{"math", "sqrt", ApiMigrationKind::CallToMethod, "sqrt"},
    ApiMigration{"math", "sqrt_f64", ApiMigrationKind::CallToMethod, "sqrt"},
    ApiMigration{"math", "abs_f64", ApiMigrationKind::CallToMethod, "abs"},
    ApiMigration{"math", "clamp_i64", ApiMigrationKind::CallToMethod, "clamp"},
    ApiMigration{"math", "clamp_f64", ApiMigrationKind::CallToMethod, "clamp"},
    ApiMigration{"math", "checked_add", ApiMigrationKind::CallToMethod, "checked_add"},
    ApiMigration{"math", "checked_sub", ApiMigrationKind::CallToMethod, "checked_sub"},
    ApiMigration{"math", "checked_mul", ApiMigrationKind::CallToMethod, "checked_mul"},
    ApiMigration{"math", "wrapping_add", ApiMigrationKind::CallToMethod, "wrapping_add"},
    ApiMigration{"math", "wrapping_sub", ApiMigrationKind::CallToMethod, "wrapping_sub"},
    ApiMigration{"math", "wrapping_mul", ApiMigrationKind::CallToMethod, "wrapping_mul"},
    ApiMigration{"math", "saturating_add", ApiMigrationKind::CallToMethod, "saturating_add"},
    ApiMigration{"math", "saturating_sub", ApiMigrationKind::CallToMethod, "saturating_sub"},
    ApiMigration{"math", "saturating_mul", ApiMigrationKind::CallToMethod, "saturating_mul"},
    ApiMigration{"math", "pi", ApiMigrationKind::CallToConst, "math.PI"},
    ApiMigration{"str", "trim", ApiMigrationKind::CallToMethod, "trim"},
    ApiMigration{"str", "split", ApiMigrationKind::CallToMethod, "split"},
    ApiMigration{"str", "join", ApiMigrationKind::CallToMethod, "join"},
    ApiMigration{"str", "to_upper", ApiMigrationKind::CallToMethod, "to_upper"},
    ApiMigration{"str", "to_lower", ApiMigrationKind::CallToMethod, "to_lower"},
    ApiMigration{"str", "replace", ApiMigrationKind::CallToMethod, "replace"},
    ApiMigration{"str", "replace_all", ApiMigrationKind::CallToMethod, "replace_all"},
    ApiMigration{"str", "starts_with", ApiMigrationKind::CallToMethod, "starts_with"},
    ApiMigration{"str", "ends_with", ApiMigrationKind::CallToMethod, "ends_with"},
    ApiMigration{"str", "contains", ApiMigrationKind::CallToMethod, "contains"},
    ApiMigration{"str", "index_of", ApiMigrationKind::CallToMethod, "index_of"},
    ApiMigration{"str", "last_index_of", ApiMigrationKind::CallToMethod, "last_index_of"},
    ApiMigration{"str", "bytes", ApiMigrationKind::CallToMethod, "bytes"},
    ApiMigration{"str", "pop", ApiMigrationKind::CallToMethod, "pop"},
    ApiMigration{"str", "parse_i64", ApiMigrationKind::Rename, "i64.parse"},
    ApiMigration{"str", "parse_i32", ApiMigrationKind::Rename, "i32.parse"},
    ApiMigration{"str", "parse_u64", ApiMigrationKind::Rename, "u64.parse"},
    ApiMigration{"str", "parse_f64", ApiMigrationKind::Rename, "f64.parse"},
    ApiMigration{"str", "parse_bool", ApiMigrationKind::Rename, "bool.parse"},
    ApiMigration{"net", "connect_tcp", ApiMigrationKind::RenameFn, "connect"},
    ApiMigration{"net", "listen_tcp", ApiMigrationKind::RenameFn, "listen"},
    ApiMigration{"net", "listen_tcp_addr", ApiMigrationKind::SwapToFn, "listen"},
    ApiMigration{"ssh", "dial", ApiMigrationKind::RenameFn, "connect"},
    ApiMigration{"websocket", "connect_insecure", ApiMigrationKind::AppendVerifyOff, "connect"},
    ApiMigration{"http", "get_insecure", ApiMigrationKind::AppendVerifyOff, "get"},
    ApiMigration{"http", "post_insecure", ApiMigrationKind::AppendVerifyOff, "post"},
    ApiMigration{"http", "make_tls_server", ApiMigrationKind::TlsServerFiles, "Server"},
    ApiMigration{"http", "make_tls_server_pem", ApiMigrationKind::TlsServerPem, "Server"},
    ApiMigration{"websocket", "make_tls_server", ApiMigrationKind::TlsServerFiles, "Server"},
    ApiMigration{"websocket", "make_tls_server_pem", ApiMigrationKind::TlsServerPem, "Server"},
    ApiMigration{"sort", "slice", ApiMigrationKind::CallToMethod, "sort"},
    ApiMigration{"sort", "slice_by", ApiMigrationKind::CallToMethod, "sort_by"},
    ApiMigration{"sort", "binary_search", ApiMigrationKind::CallToMethod, "binary_search"},
    ApiMigration{"sort", "reverse", ApiMigrationKind::CallToMethod, "reverse"},
    ApiMigration{"sort", "is_sorted", ApiMigrationKind::CallToMethod, "is_sorted"},
};
// clang-format on

[[nodiscard]] constexpr auto find_api_migration(std::string_view ns, std::string_view fn) -> const ApiMigration* {
    for (const auto& m : kApiMigrations) {
        if (m.ns == ns && m.fn == fn) return &m;
    }
    return nullptr;
}

// Q25: `make(ns.Type, args...)` for stdlib types lowers to the runtime constructor `cpp`. `old_fn` is the
// retired constructor function (`http.make_router()`), which `cfire fix` rewrites to `make(http.Router)`.
struct StdlibCtor {
    std::string_view ns;
    std::string_view type;
    std::string_view old_fn;
    std::string_view cpp;
    bool generic;  // `Channel[T]`: the type argument is passed as the C++ template argument
};
// clang-format off
inline constexpr std::array kStdlibCtors = {
    StdlibCtor{"http", "Router", "make_router", "cfire::http::make_router", false},
    StdlibCtor{"http", "Server", "make_server", "cfire::http::make_server", false},
    StdlibCtor{"websocket", "Server", "make_server", "cfire::websocket::make_server", false},
    StdlibCtor{"os", "RawTerminal", "make_raw", "cfire::os::make_raw", false},
    StdlibCtor{"sync", "Channel", "make_channel", "cfire::sync::make_channel", true},
    StdlibCtor{"sync", "Mutex", "make_mutex", "cfire::sync::make_mutex", true},
    StdlibCtor{"sync", "Mutex", "new_mutex", "cfire::sync::make_mutex", true},
    StdlibCtor{"sync", "Atomic", "make_atomic", "cfire::sync::make_atomic", true},
    StdlibCtor{"ai", "Agent", "make_agent", "cfire::ai::make_agent", false},
    StdlibCtor{"ai", "McpServer", "make_mcp_server", "cfire::ai::make_mcp_server", false},
    StdlibCtor{"webui", "App", "new_app", "cfire::webui::new_app", false},
};
// clang-format on

// `type_name` is the spelled type (`http.Router`, `sync.Channel[i32]`, or an import alias such as `h.Router`
// once the caller has canonicalized the namespace).
[[nodiscard]] constexpr auto find_stdlib_ctor_for(std::string_view ns, std::string_view type) -> const StdlibCtor* {
    if (auto br = type.find('['); br != std::string_view::npos) type = type.substr(0, br);
    for (const auto& c : kStdlibCtors) {
        if (c.ns == ns && c.type == type) return &c;
    }
    return nullptr;
}
[[nodiscard]] constexpr auto find_stdlib_ctor_by_old_fn(std::string_view ns, std::string_view fn) -> const StdlibCtor* {
    for (const auto& c : kStdlibCtors) {
        if (c.ns == ns && c.old_fn == fn) return &c;
    }
    return nullptr;
}

// Stdlib struct types usable from `.cf` code (CF-180, Q31). `fields` lists `name: type = default` entries in
// C++ declaration order, separated by `;` (codegen reorders designated initializers to match).
struct StdlibStruct {
    std::string_view ns;
    std::string_view name;
    std::string_view cpp;
    std::string_view fields;
    std::string_view summary;
};
// clang-format off
inline constexpr std::array kStdlibStructs = {
    StdlibStruct{"http", "ServerLimits", "cfire::http::ServerLimits",
        "header_timeout_ms: i32 = 10000; body_timeout_ms: i32 = 30000; max_body_bytes: usize = 10485760; workers: usize = 0; max_pending: usize = 128; "
        "keepalive_timeout_ms: i32 = 5000; max_requests_per_conn: usize = 1000; max_websockets: usize = 1024; max_connections: usize = 10000",
        "Per-connection limits for `http.Server` (Q22, CF-192, CF-193); pass to `server.set_limits(...)`. The server waits on idle and slow "
        "connections in an event loop, so `workers` is only how many handlers run at once; "
        "`workers = 0` keeps the default (twice the core count, at least 4). An idle keep-alive "
        "connection closes after `keepalive_timeout_ms` (0 turns keep-alive off) or after `max_requests_per_conn` requests. Each WebSocket "
        "session runs on its own thread, up to `max_websockets`. Over `max_connections` open connections or `max_pending` requests "
        "waiting for a worker, a client gets 503."},
    StdlibStruct{"http", "TlsConfig", "cfire::http::TlsConfig",
        "cert_file: str = \"\"; key_file: str = \"\"; cert_pem: str = \"\"; key_pem: str = \"\"",
        "Server certificate for `make(http.Server, port, tls: ...)` (Q31): set `cert_file`/`key_file`, or `cert_pem`/`key_pem` for in-memory PEM."},
    StdlibStruct{"websocket", "TlsConfig", "cfire::http::TlsConfig",
        "cert_file: str = \"\"; key_file: str = \"\"; cert_pem: str = \"\"; key_pem: str = \"\"",
        "Server certificate for `make(websocket.Server, port, tls: ...)` (Q31); the same type as `http.TlsConfig`."},
};
// clang-format on

[[nodiscard]] constexpr auto find_stdlib_struct(std::string_view ns, std::string_view name) -> const StdlibStruct* {
    for (const auto& st : kStdlibStructs) {
        if (st.ns == ns && st.name == name) return &st;
    }
    return nullptr;
}

struct StdlibStructField {
    std::string_view name;
    std::string_view type;
    std::string_view def;
};
// Splits `StdlibStruct::fields` into its entries, in declaration order.
[[nodiscard]] inline auto stdlib_struct_fields(const StdlibStruct& st) -> std::vector<StdlibStructField> {
    std::vector<StdlibStructField> out;
    auto trim = [](std::string_view v) {
        while (!v.empty() && v.front() == ' ') v.remove_prefix(1);
        while (!v.empty() && v.back() == ' ') v.remove_suffix(1);
        return v;
    };
    for (std::string_view rest = st.fields; !rest.empty();) {
        auto semi = rest.find(';');
        std::string_view item = trim(rest.substr(0, semi));
        auto colon = item.find(':');
        auto eq = item.find('=');
        StdlibStructField f;
        f.name = trim(item.substr(0, colon));
        f.type = trim(item.substr(colon + 1, eq == std::string_view::npos ? std::string_view::npos : eq - colon - 1));
        if (eq != std::string_view::npos) f.def = trim(item.substr(eq + 1));
        out.push_back(f);
        if (semi == std::string_view::npos) break;
        rest.remove_prefix(semi + 1);
    }
    return out;
}

// CF-188: type names the catalog used to list although no runtime type implements them. Using one is
// `cfire/UNKNOWN-STDLIB-TYPE`, naming the real replacement when there is one (`use` empty: none).
struct RemovedStdlibType {
    std::string_view ns;
    std::string_view name;
    std::string_view use;
};
// clang-format off
inline constexpr std::array kRemovedStdlibTypes = {
    RemovedStdlibType{"sqlite", "Db", "sqlite.Conn"},
    RemovedStdlibType{"sqlite", "Database", "sqlite.Conn"},
    RemovedStdlibType{"sqlite", "Stmt", "`db.query[T](sql, ...)` or `db.exec(sql, ...)` on a `sqlite.Conn`"},
    RemovedStdlibType{"sqlite", "Statement", "`db.query[T](sql, ...)` or `db.exec(sql, ...)` on a `sqlite.Conn`"},
    RemovedStdlibType{"sqlite", "Row", "a struct of your own: `db.query[T](...)` returns `[]T`"},
    RemovedStdlibType{"json", "Doc", "json.Value"},
    RemovedStdlibType{"json", "JsonDoc", "json.Value"},
    RemovedStdlibType{"json", "JsonValue", "json.Value"},
    RemovedStdlibType{"os", "STDIN", "`os.stdin()`, which returns an `fs.File`"},
    RemovedStdlibType{"os", "STDOUT", "`os.stdout()`, which returns an `fs.File`"},
    RemovedStdlibType{"os", "STDERR", "`os.stderr()`, which returns an `fs.File`"},
    RemovedStdlibType{"collections", "Vec", "a slice, `[]T`"},
    RemovedStdlibType{"collections", "Map", "a map, `map[K]V`"},
    RemovedStdlibType{"collections", "Heap", "collections.PriorityQueue[T] or collections.MinQueue[T]"},
    RemovedStdlibType{"sync", "AtomicInt", "sync.Atomic[i64]"},
    RemovedStdlibType{"sync", "WaitGroup", "`async.scope`, which joins every task it spawns"},
    RemovedStdlibType{"sync", "CondVar", ""},
    RemovedStdlibType{"time", "DateTime", "time.Time"},
    RemovedStdlibType{"unicode", "Rune", "`u32`: `unicode.runes(s)` returns `[]u32`"},
    RemovedStdlibType{"cli", "Parser", "`cli.parse[T](args)` with a struct `T` whose fields are the flags"},
    RemovedStdlibType{"cli", "Flag", "a field of the struct passed to `cli.parse[T]`, with a `cli:\"...\"` tag"},
    RemovedStdlibType{"cli", "Arg", "a field of the struct passed to `cli.parse[T]`, with a `cli:\"...\"` tag"},
    RemovedStdlibType{"tako", "Tako", ""},
    RemovedStdlibType{"ai", "Chunk", "ai.DocumentSection"},
    RemovedStdlibType{"ai", "ExtractedBug", ""},
    RemovedStdlibType{"ai", "ToolArgs", "a struct of your own, described with `ai.tool_def[T]`"},
    RemovedStdlibType{"websocket", "Message", "`str` or `[]byte`, from `conn.recv_text()` / `conn.recv_binary()`"},
};
// clang-format on

[[nodiscard]] constexpr auto find_removed_stdlib_type(std::string_view ns, std::string_view name) -> const RemovedStdlibType* {
    for (const auto& r : kRemovedStdlibTypes) {
        if (r.ns == ns && r.name == name) return &r;
    }
    return nullptr;
}

// Modules whose whole surface moved to methods (Q7); importing them is an error whose fix deletes the line.
inline constexpr std::array<std::string_view, 2> kRetiredModules = {"core:str", "core:sort"};

// Receiver kind of a spelled `.cf` type (`str`, `&str`, `[]i32`, `[4]u8`, `map[str]i32`, `i64`, `f32`), for the
// tools that only have type names (LSP completion, hover, docgen).
[[nodiscard]] constexpr auto builtin_recv_bits_for_type_name(std::string_view t) -> std::uint8_t {
    while (t.starts_with("&mut ") || t.starts_with("*mut ")) t.remove_prefix(5);
    while (t.starts_with("&") || t.starts_with("*")) t.remove_prefix(1);
    if (t == "str" || t == "string") return BmStr;
    if (t.starts_with("[]")) return BmSlice;
    if (t.starts_with("[") && t.size() > 1 && t[1] >= '0' && t[1] <= '9') return BmArray;
    if (t.starts_with("map[")) return BmMap;
    if (t == "bool") return BmBool;
    if (t == "f32" || t == "f64") return BmFloat;
    constexpr std::array<std::string_view, 10> ints = {"i8", "i16", "i32", "i64", "u8", "u16", "u32", "u64", "isize", "usize"};
    for (auto i : ints) if (t == i) return BmInt;
    return 0;
}

// `fn trim() -> &str`, `fn insert(i: usize, v: T)` — a display signature with the receiver placeholders kept.
[[nodiscard]] inline auto builtin_method_signature(const BuiltinMethod& m) -> std::string {
    std::string params;
    char pname = 'a';
    for (std::string_view rest = m.params; !rest.empty();) {
        auto comma = rest.find(',');
        std::string_view p = rest.substr(0, comma);
        while (!p.empty() && p.front() == ' ') p.remove_prefix(1);
        while (!p.empty() && p.back() == ' ') p.remove_suffix(1);
        const bool optional = p.ends_with('=');
        if (optional) p.remove_suffix(1);
        if (!params.empty()) params += ", ";
        params += std::string(1, pname++) + ": " + std::string(p == "fn" ? std::string_view("fn(T, T) -> bool") : p) + (optional ? " = default" : "");
        if (comma == std::string_view::npos) break;
        rest.remove_prefix(comma + 1);
    }
    std::string out = "fn " + std::string(m.name) + "(" + params + ")";
    if (!m.ret.empty() && m.ret != "void") out += " -> " + std::string(m.ret);
    // The table writes a view as `&str`; in `.cf` terms that is a `str` (CF-189: there is no `&T` type).
    for (std::size_t at; (at = out.find("&str")) != std::string::npos;) out.erase(at, 1);
    return out;
}

// Retired spellings (Q16/Q26): a hard error with a fix-it that renames the method.
struct RetiredBuiltinMethod {
    std::string_view name;
    std::uint8_t recv;
    std::string_view replacement;
};
inline constexpr std::array kRetiredBuiltinMethods = {
    RetiredBuiltinMethod{"empty", BmSized, "is_empty"},
};

} // namespace cfire::frontend

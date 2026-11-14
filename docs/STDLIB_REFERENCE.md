# `cfire` Standard Library (`core:*`) & Diagnostics Reference

> Auto-generated from the compiler's single-source-of-truth catalog via `./cfire doc`.

## 1. Builtin Types & Built-In `core:*` Standard Library Modules

### `builtin` (`builtin`) — Builtin Types: Methods, Associated Items & make

- **Auto-Link Flags:** `(always available)`
- **Overview:** Operations on values of builtin types are methods on the value (Q7): `s.trim()`, `xs.sort()`, `m.keys()`, `x.abs()`, `f.sqrt()`. Searches return `T?`, never `-1` (Q8). Limits and parsing belong to the type (Q10): `i32.MAX`, `f64.NAN`, `i32.parse(s) -> i32?`. `make` is the one construction builtin, and the type decides where memory lives (Q25): `make(T, args)` is a value, `make(own[T], args)` / `make(shared[T], args)` are heap-owned, and `make([]T, len[, cap])` is a slice. In the table, `Self` is the receiver type, `T` the element type and `K`/`V` the map key/value types.

| `.cf` Signature | Lowered C++23 | Description | Example |
|---|---|---|---|
| `fn (x: i64) abs() -> Self` | `cfire::bm::abs(x, ...)` | Absolute value. Integer `MIN.abs()` panics; use `checked_abs` or `wrapping_abs`. | `d := (a - b).abs()` |
| `fn (x: i64) min(a: Self) -> Self` | `cfire::bm::min(x, ...)` | Smaller of the two values (for floats, a NaN operand yields the other operand). | `lo := a.min(b)` |
| `fn (x: i64) max(a: Self) -> Self` | `cfire::bm::max(x, ...)` | Larger of the two values (for floats, a NaN operand yields the other operand). | `hi := a.max(b)` |
| `fn (x: i64) clamp(a: Self, b: Self) -> Self` | `cfire::bm::clamp(x, ...)` | Restricts the value to `lo..=hi`; panics when `lo > hi` (or either bound is NaN). | `v := x.clamp(0, 255)` |
| `fn (x: i64) signum() -> Self` | `cfire::bm::signum(x, ...)` | `-1`, `0` or `1` (floats: `-1.0`, `±0.0`, `1.0`, NaN for NaN). | `s := delta.signum()` |
| `fn (x: i64) pow(a: Self) -> Self` | `cfire::bm::pow(x, ...)` | Integer power (a negative exponent or overflow panics) or float power. | `kb := base.pow(10)` |
| `fn (x: i64) count_ones() -> u32` | `cfire::bm::count_ones(x, ...)` | Number of set bits. | `bits := mask.count_ones()` |
| `fn (x: i64) leading_zeros() -> u32` | `cfire::bm::leading_zeros(x, ...)` | Number of leading zero bits. | `lz := x.leading_zeros()` |
| `fn (x: i64) trailing_zeros() -> u32` | `cfire::bm::trailing_zeros(x, ...)` | Number of trailing zero bits. | `tz := x.trailing_zeros()` |
| `fn (x: i64) is_power_of_two() -> bool` | `cfire::bm::is_power_of_two(x, ...)` | `true` for 1, 2, 4, …; `false` for zero and negatives. | `ok := n.is_power_of_two()` |
| `fn (x: i64) div_euclid(a: Self) -> Self` | `cfire::bm::div_euclid(x, ...)` | Euclidean division (the remainder is never negative); panics on division by zero or overflow. | `q := a.div_euclid(b)` |
| `fn (x: i64) rem_euclid(a: Self) -> Self` | `cfire::bm::rem_euclid(x, ...)` | Euclidean remainder, always `>= 0`; integer division by zero panics. | `i := idx.rem_euclid(len)` |
| `fn (x: i64) checked_add(a: Self) -> Self?` | `cfire::bm::checked_add(x, ...)` | `none` on overflow. | `s := a.checked_add(b) or 0` |
| `fn (x: i64) checked_sub(a: Self) -> Self?` | `cfire::bm::checked_sub(x, ...)` | `none` on overflow. | `s := a.checked_sub(b) or 0` |
| `fn (x: i64) checked_mul(a: Self) -> Self?` | `cfire::bm::checked_mul(x, ...)` | `none` on overflow. | `p := a.checked_mul(b) or 0` |
| `fn (x: i64) checked_div(a: Self) -> Self?` | `cfire::bm::checked_div(x, ...)` | `none` on division by zero or overflow (`MIN / -1`). | `q := a.checked_div(b) or 0` |
| `fn (x: i64) checked_neg() -> Self?` | `cfire::bm::checked_neg(x, ...)` | `none` on overflow (`MIN`, or any non-zero unsigned value). | `n := a.checked_neg() or 0` |
| `fn (x: i64) checked_abs() -> Self?` | `cfire::bm::checked_abs(x, ...)` | `none` for `MIN`. | `d := a.checked_abs() or 0` |
| `fn (x: i64) wrapping_add(a: Self) -> Self` | `cfire::bm::wrapping_add(x, ...)` | Two's-complement wrapping addition. | `h := h.wrapping_add(c)` |
| `fn (x: i64) wrapping_sub(a: Self) -> Self` | `cfire::bm::wrapping_sub(x, ...)` | Two's-complement wrapping subtraction. | `d := a.wrapping_sub(b)` |
| `fn (x: i64) wrapping_mul(a: Self) -> Self` | `cfire::bm::wrapping_mul(x, ...)` | Two's-complement wrapping multiplication. | `h := h.wrapping_mul(16777619)` |
| `fn (x: i64) wrapping_div(a: Self) -> Self` | `cfire::bm::wrapping_div(x, ...)` | Wrapping division (`MIN / -1 == MIN`); division by zero panics. | `q := a.wrapping_div(b)` |
| `fn (x: i64) wrapping_neg() -> Self` | `cfire::bm::wrapping_neg(x, ...)` | Two's-complement wrapping negation. | `n := a.wrapping_neg()` |
| `fn (x: i64) wrapping_abs() -> Self` | `cfire::bm::wrapping_abs(x, ...)` | Absolute value, wrapping for `MIN`. | `d := a.wrapping_abs()` |
| `fn (x: i64) saturating_add(a: Self) -> Self` | `cfire::bm::saturating_add(x, ...)` | Addition clamped to `MIN..=MAX`. | `s := a.saturating_add(b)` |
| `fn (x: i64) saturating_sub(a: Self) -> Self` | `cfire::bm::saturating_sub(x, ...)` | Subtraction clamped to `MIN..=MAX`. | `s := a.saturating_sub(b)` |
| `fn (x: i64) saturating_mul(a: Self) -> Self` | `cfire::bm::saturating_mul(x, ...)` | Multiplication clamped to `MIN..=MAX`. | `p := a.saturating_mul(b)` |
| `fn (x: i64) saturating_div(a: Self) -> Self` | `cfire::bm::saturating_div(x, ...)` | Division clamped to `MIN..=MAX` (`MIN / -1 == MAX`); division by zero panics. | `q := a.saturating_div(b)` |
| `fn (x: i64) saturating_neg() -> Self` | `cfire::bm::saturating_neg(x, ...)` | Negation clamped to `MIN..=MAX`. | `n := a.saturating_neg()` |
| `fn (x: i64) wrapping_shl(a: u32) -> Self` | `(dedicated lowering)` | Left shift with the count taken modulo the bit width. | `x := x.wrapping_shl(n)` |
| `fn (x: i64) wrapping_shr(a: u32) -> Self` | `(dedicated lowering)` | Right shift with the count taken modulo the bit width. | `x := x.wrapping_shr(n)` |
| `fn (x: i64) checked_shl(a: u32) -> Self?` | `(dedicated lowering)` | `none` when the count is `>=` the bit width. | `x := v.checked_shl(n) or 0` |
| `fn (x: i64) checked_shr(a: u32) -> Self?` | `(dedicated lowering)` | `none` when the count is `>=` the bit width. | `x := v.checked_shr(n) or 0` |
| `fn (x: i64) wrapping_as()` | `(dedicated lowering)` | Conversion to `T` that wraps on overflow: `x.wrapping_as[u8]()`. | `b := x.wrapping_as[u8]()` |
| `fn (x: i64) saturating_as()` | `(dedicated lowering)` | Conversion to `T` that clamps on overflow: `x.saturating_as[u8]()`. | `b := x.saturating_as[u8]()` |
| `fn (x: i64) try_as()` | `(dedicated lowering)` | Conversion to `T?`, `none` when the value does not fit: `x.try_as[u8]()`. | `b := x.try_as[u8]() or 0` |
| `fn (f: f64) abs() -> Self` | `cfire::bm::abs(f, ...)` | Absolute value. Integer `MIN.abs()` panics; use `checked_abs` or `wrapping_abs`. | `d := (a - b).abs()` |
| `fn (f: f64) min(a: Self) -> Self` | `cfire::bm::min(f, ...)` | Smaller of the two values (for floats, a NaN operand yields the other operand). | `lo := a.min(b)` |
| `fn (f: f64) max(a: Self) -> Self` | `cfire::bm::max(f, ...)` | Larger of the two values (for floats, a NaN operand yields the other operand). | `hi := a.max(b)` |
| `fn (f: f64) clamp(a: Self, b: Self) -> Self` | `cfire::bm::clamp(f, ...)` | Restricts the value to `lo..=hi`; panics when `lo > hi` (or either bound is NaN). | `v := x.clamp(0, 255)` |
| `fn (f: f64) signum() -> Self` | `cfire::bm::signum(f, ...)` | `-1`, `0` or `1` (floats: `-1.0`, `±0.0`, `1.0`, NaN for NaN). | `s := delta.signum()` |
| `fn (f: f64) pow(a: Self) -> Self` | `cfire::bm::pow(f, ...)` | Integer power (a negative exponent or overflow panics) or float power. | `kb := base.pow(10)` |
| `fn (f: f64) rem_euclid(a: Self) -> Self` | `cfire::bm::rem_euclid(f, ...)` | Euclidean remainder, always `>= 0`; integer division by zero panics. | `i := idx.rem_euclid(len)` |
| `fn (f: f64) wrapping_as()` | `(dedicated lowering)` | Conversion to `T` that wraps on overflow: `x.wrapping_as[u8]()`. | `b := x.wrapping_as[u8]()` |
| `fn (f: f64) saturating_as()` | `(dedicated lowering)` | Conversion to `T` that clamps on overflow: `x.saturating_as[u8]()`. | `b := x.saturating_as[u8]()` |
| `fn (f: f64) try_as()` | `(dedicated lowering)` | Conversion to `T?`, `none` when the value does not fit: `x.try_as[u8]()`. | `b := x.try_as[u8]() or 0` |
| `fn (f: f64) floor() -> Self` | `cfire::bm::floor(f, ...)` | Largest integer value `<=` x. | `f := x.floor()` |
| `fn (f: f64) ceil() -> Self` | `cfire::bm::ceil(f, ...)` | Smallest integer value `>=` x. | `c := x.ceil()` |
| `fn (f: f64) round() -> Self` | `cfire::bm::round(f, ...)` | Nearest integer value, halfway cases away from zero. | `r := x.round()` |
| `fn (f: f64) trunc() -> Self` | `cfire::bm::trunc(f, ...)` | Integer part, rounding toward zero. | `t := x.trunc()` |
| `fn (f: f64) fract() -> Self` | `cfire::bm::fract(f, ...)` | Fractional part, `x - x.trunc()`. | `f := x.fract()` |
| `fn (f: f64) sqrt() -> Self` | `cfire::bm::sqrt(f, ...)` | Square root (NaN for negative input). | `d := (dx * dx + dy * dy).sqrt()` |
| `fn (f: f64) cbrt() -> Self` | `cfire::bm::cbrt(f, ...)` | Cube root. | `c := v.cbrt()` |
| `fn (f: f64) powi(a: i32) -> Self` | `cfire::bm::powi(f, ...)` | Raises to an integer power. | `a := r.powi(2)` |
| `fn (f: f64) exp() -> Self` | `cfire::bm::exp(f, ...)` | `e` raised to x. | `y := x.exp()` |
| `fn (f: f64) ln() -> Self` | `cfire::bm::ln(f, ...)` | Natural logarithm. | `y := x.ln()` |
| `fn (f: f64) log2() -> Self` | `cfire::bm::log2(f, ...)` | Base-2 logarithm. | `bits := n.log2()` |
| `fn (f: f64) log10() -> Self` | `cfire::bm::log10(f, ...)` | Base-10 logarithm. | `db := 20.0 * a.log10()` |
| `fn (f: f64) sin() -> Self` | `cfire::bm::sin(f, ...)` | Sine (radians). | `y := t.sin()` |
| `fn (f: f64) cos() -> Self` | `cfire::bm::cos(f, ...)` | Cosine (radians). | `x := t.cos()` |
| `fn (f: f64) tan() -> Self` | `cfire::bm::tan(f, ...)` | Tangent (radians). | `m := t.tan()` |
| `fn (f: f64) asin() -> Self` | `cfire::bm::asin(f, ...)` | Arcsine, in radians. | `a := y.asin()` |
| `fn (f: f64) acos() -> Self` | `cfire::bm::acos(f, ...)` | Arccosine, in radians. | `a := x.acos()` |
| `fn (f: f64) atan() -> Self` | `cfire::bm::atan(f, ...)` | Arctangent, in radians (see `math.atan2` for two inputs). | `a := m.atan()` |
| `fn (f: f64) sinh() -> Self` | `cfire::bm::sinh(f, ...)` | Hyperbolic sine. | `y := x.sinh()` |
| `fn (f: f64) cosh() -> Self` | `cfire::bm::cosh(f, ...)` | Hyperbolic cosine. | `y := x.cosh()` |
| `fn (f: f64) tanh() -> Self` | `cfire::bm::tanh(f, ...)` | Hyperbolic tangent. | `y := x.tanh()` |
| `fn (f: f64) to_degrees() -> Self` | `cfire::bm::to_degrees(f, ...)` | Converts radians to degrees. | `deg := rad.to_degrees()` |
| `fn (f: f64) to_radians() -> Self` | `cfire::bm::to_radians(f, ...)` | Converts degrees to radians. | `rad := deg.to_radians()` |
| `fn (f: f64) is_nan() -> bool` | `cfire::bm::is_nan(f, ...)` | `true` for NaN. | `if x.is_nan() { return }` |
| `fn (f: f64) is_inf() -> bool` | `cfire::bm::is_inf(f, ...)` | `true` for `+inf` or `-inf`. | `if x.is_inf() { return }` |
| `fn (f: f64) is_finite() -> bool` | `cfire::bm::is_finite(f, ...)` | `true` unless NaN or infinite. | `ok := x.is_finite()` |
| `fn (s: str) len() -> usize` | `(dedicated lowering)` | Number of elements (bytes for `str`). | `n := xs.len()` |
| `fn (s: str) is_empty() -> bool` | `(dedicated lowering)` | `true` when `len() == 0`. | `if s.is_empty() { return }` |
| `fn (s: str) contains(a: str) -> bool` | `cfire::bm::contains(s, ...)` | `true` if the substring occurs. | `if path.contains("..") { return }` |
| `fn (s: str) starts_with(a: str) -> bool` | `cfire::bm::starts_with(s, ...)` | `true` if `s` begins with `prefix`. | `if line.starts_with("#") { continue }` |
| `fn (s: str) ends_with(a: str) -> bool` | `cfire::bm::ends_with(s, ...)` | `true` if `s` ends with `suffix`. | `if name.ends_with(".cf") { ... }` |
| `fn (s: str) index_of(a: str, b: usize = default) -> usize?` | `cfire::bm::index_of(s, ...)` | Byte position of the first occurrence at or after `start` (default 0), or `none`. | `dot := path.index_of(".") or { return }` |
| `fn (s: str) last_index_of(a: str) -> usize?` | `cfire::bm::last_index_of(s, ...)` | Byte position of the last occurrence, or `none`. | `slash := path.last_index_of("/") or 0` |
| `fn (s: str) split(a: str) -> []str` | `cfire::bm::split(s, ...)` | Splits on every occurrence of `sep` (an empty `s` gives one empty part). | `parts := line.split(",")` |
| `fn (s: str) split_n(a: str, b: usize) -> []str` | `cfire::bm::split_n(s, ...)` | Splits into at most `n` parts; the last part holds the rest. | `kv := line.split_n("=", 2)` |
| `fn (s: str) lines() -> []str` | `cfire::bm::lines(s, ...)` | Splits on `\n`, dropping a trailing `\r` from each line and a final empty line. | `for line in text.lines() { ... }` |
| `fn (s: str) trim() -> str` | `cfire::bm::trim(s, ...)` | View without leading and trailing ASCII whitespace. | `name := raw.trim()` |
| `fn (s: str) trim_start() -> str` | `cfire::bm::trim_start(s, ...)` | View without leading ASCII whitespace. | `body := text.trim_start()` |
| `fn (s: str) trim_end() -> str` | `cfire::bm::trim_end(s, ...)` | View without trailing ASCII whitespace. | `line := raw.trim_end()` |
| `fn (s: str) strip_prefix(a: str) -> str?` | `cfire::bm::strip_prefix(s, ...)` | View after `prefix`, or `none` if `s` does not start with it. | `rest := arg.strip_prefix("--") or { continue }` |
| `fn (s: str) strip_suffix(a: str) -> str?` | `cfire::bm::strip_suffix(s, ...)` | View before `suffix`, or `none` if `s` does not end with it. | `stem := file.strip_suffix(".cf") or file` |
| `fn (s: str) replace(a: str, b: str) -> str` | `cfire::bm::replace(s, ...)` | Copy with the first occurrence of `from` replaced. | `s2 := s.replace("a", "b")` |
| `fn (s: str) replace_all(a: str, b: str) -> str` | `cfire::bm::replace_all(s, ...)` | Copy with every occurrence of `from` replaced. | `s2 := s.replace_all("\t", "    ")` |
| `fn (s: str) repeat(a: usize) -> str` | `cfire::bm::repeat(s, ...)` | `s` concatenated `n` times. | `rule := "-".repeat(40)` |
| `fn (s: str) to_upper() -> str` | `cfire::bm::to_upper(s, ...)` | ASCII upper-case copy. | `tag := name.to_upper()` |
| `fn (s: str) to_lower() -> str` | `cfire::bm::to_lower(s, ...)` | ASCII lower-case copy. | `key := header.to_lower()` |
| `fn (s: str) runes() -> []u32` | `cfire::bm::runes(s, ...)` | Unicode code points (invalid UTF-8 bytes decode as U+FFFD). | `for r in s.runes() { ... }` |
| `fn (s: str) rune_count() -> usize` | `cfire::bm::rune_count(s, ...)` | Number of Unicode code points. | `n := s.rune_count()` |
| `fn (s: str) bytes() -> []byte` | `(dedicated lowering)` | The UTF-8 bytes of `s`. | `raw := s.bytes()` |
| `fn (s: str) pop() -> T?` | `cfire::bm::pop(s, ...)` | Removes and returns the last element (last byte for `str`), or `none` when empty. | `last := stack.pop() or { break }` |
| `fn (s: str) with_ext(a: str) -> str` | `(dedicated lowering)` | Path with its extension replaced. | `out := path.with_ext(".o")` |
| `fn (xs: []T) len() -> usize` | `(dedicated lowering)` | Number of elements (bytes for `str`). | `n := xs.len()` |
| `fn (xs: []T) is_empty() -> bool` | `(dedicated lowering)` | `true` when `len() == 0`. | `if s.is_empty() { return }` |
| `fn (xs: []T) pop() -> T?` | `cfire::bm::pop(xs, ...)` | Removes and returns the last element (last byte for `str`), or `none` when empty. | `last := stack.pop() or { break }` |
| `fn (xs: []T) contains(a: T) -> bool` | `cfire::bm::contains(xs, ...)` | `true` if an element equals `v`. | `if xs.contains(3) { ... }` |
| `fn (xs: []T) index_of(a: T) -> usize?` | `cfire::bm::index_of(xs, ...)` | Position of the first element equal to `v`, or `none`. | `i := xs.index_of(v) or { return }` |
| `fn (xs: []T) first() -> T?` | `cfire::bm::first(xs, ...)` | Copy of the first element, or `none` when empty. | `head := xs.first() or 0` |
| `fn (xs: []T) last() -> T?` | `cfire::bm::last(xs, ...)` | Copy of the last element, or `none` when empty. | `tail := xs.last() or 0` |
| `fn (xs: []T) reverse()` | `cfire::bm::reverse(xs, ...)` | Reverses the elements in place. | `xs.reverse()` |
| `fn (xs: []T) sort()` | `cfire::bm::sort(xs, ...)` | Sorts in place, ascending. | `xs.sort()` |
| `fn (xs: []T) sort_by(a: fn(T, T) -> bool)` | `cfire::bm::sort_by(xs, ...)` | Sorts in place with a strict `less(a, b)` comparator. | `xs.sort_by(|a, b| a.score > b.score)` |
| `fn (xs: []T) is_sorted() -> bool` | `cfire::bm::is_sorted(xs, ...)` | `true` if the elements are in ascending order. | `assert(xs.is_sorted())` |
| `fn (xs: []T) binary_search(a: T) -> usize?` | `cfire::bm::binary_search(xs, ...)` | Position of `v` in an ascending sequence, or `none`. | `i := xs.binary_search(42) or { return }` |
| `fn (xs: []T) join(a: str) -> str` | `cfire::bm::join(xs, ...)` | Concatenates a `[]str` with `sep` between elements. | `csv := fields.join(",")` |
| `fn (xs: []T) push(a: T)` | `(dedicated lowering)` | Appends `v`. | `xs.push(4)` |
| `fn (xs: []T) insert(a: usize, b: T)` | `cfire::bm::insert(xs, ...)` | Inserts `v` at `i`, shifting later elements; panics when `i > len()`. | `xs.insert(0, head)` |
| `fn (xs: []T) remove(a: usize) -> T` | `cfire::bm::remove(xs, ...)` | Removes and returns the element at `i`, shifting later elements; panics when out of range. | `x := xs.remove(0)` |
| `fn (xs: []T) swap_remove(a: usize) -> T` | `cfire::bm::swap_remove(xs, ...)` | Removes the element at `i` by moving the last one into its place (O(1)); panics when out of range. | `x := xs.swap_remove(i)` |
| `fn (xs: []T) truncate(a: usize)` | `cfire::bm::truncate(xs, ...)` | Keeps the first `n` elements (no effect when `n >= len()`). | `xs.truncate(10)` |
| `fn (xs: []T) extend(a: []T)` | `cfire::bm::extend(xs, ...)` | Appends a copy of every element of `other`. | `xs.extend(more)` |
| `fn (xs: []T) clear()` | `cfire::bm::clear(xs, ...)` | Removes every element. | `xs.clear()` |
| `fn (xs: []T) bytes() -> []byte` | `(dedicated lowering)` | Byte view of the slice's storage. | `raw := xs.bytes()` |
| `fn (arr: [N]T) len() -> usize` | `(dedicated lowering)` | Number of elements (bytes for `str`). | `n := xs.len()` |
| `fn (arr: [N]T) is_empty() -> bool` | `(dedicated lowering)` | `true` when `len() == 0`. | `if s.is_empty() { return }` |
| `fn (arr: [N]T) contains(a: T) -> bool` | `cfire::bm::contains(arr, ...)` | `true` if an element equals `v`. | `if xs.contains(3) { ... }` |
| `fn (arr: [N]T) index_of(a: T) -> usize?` | `cfire::bm::index_of(arr, ...)` | Position of the first element equal to `v`, or `none`. | `i := xs.index_of(v) or { return }` |
| `fn (arr: [N]T) first() -> T?` | `cfire::bm::first(arr, ...)` | Copy of the first element, or `none` when empty. | `head := xs.first() or 0` |
| `fn (arr: [N]T) last() -> T?` | `cfire::bm::last(arr, ...)` | Copy of the last element, or `none` when empty. | `tail := xs.last() or 0` |
| `fn (arr: [N]T) reverse()` | `cfire::bm::reverse(arr, ...)` | Reverses the elements in place. | `xs.reverse()` |
| `fn (arr: [N]T) sort()` | `cfire::bm::sort(arr, ...)` | Sorts in place, ascending. | `xs.sort()` |
| `fn (arr: [N]T) sort_by(a: fn(T, T) -> bool)` | `cfire::bm::sort_by(arr, ...)` | Sorts in place with a strict `less(a, b)` comparator. | `xs.sort_by(|a, b| a.score > b.score)` |
| `fn (arr: [N]T) is_sorted() -> bool` | `cfire::bm::is_sorted(arr, ...)` | `true` if the elements are in ascending order. | `assert(xs.is_sorted())` |
| `fn (arr: [N]T) binary_search(a: T) -> usize?` | `cfire::bm::binary_search(arr, ...)` | Position of `v` in an ascending sequence, or `none`. | `i := xs.binary_search(42) or { return }` |
| `fn (arr: [N]T) join(a: str) -> str` | `cfire::bm::join(arr, ...)` | Concatenates a `[]str` with `sep` between elements. | `csv := fields.join(",")` |
| `fn (m: map[K]V) len() -> usize` | `(dedicated lowering)` | Number of elements (bytes for `str`). | `n := xs.len()` |
| `fn (m: map[K]V) is_empty() -> bool` | `(dedicated lowering)` | `true` when `len() == 0`. | `if s.is_empty() { return }` |
| `fn (m: map[K]V) clear()` | `cfire::bm::clear(m, ...)` | Removes every element. | `xs.clear()` |
| `fn (m: map[K]V) contains(a: K) -> bool` | `cfire::bm::contains(m, ...)` | `true` if the key is present. | `if m.contains("id") { ... }` |
| `fn (m: map[K]V) get(a: K) -> V?` | `cfire::bm::get(m, ...)` | Copy of the value for `k`, or `none`. | `port := cfg.get("port") or 8080` |
| `fn (m: map[K]V) insert(a: K, b: V) -> V?` | `cfire::bm::insert(m, ...)` | Sets `k` to `v`; returns the previous value, or `none`. | `old := m.insert(k, v)` |
| `fn (m: map[K]V) remove(a: K) -> V?` | `cfire::bm::remove(m, ...)` | Removes `k`; returns its value, or `none` if absent. | `gone := m.remove(k)` |
| `fn (m: map[K]V) keys() -> []K` | `cfire::bm::keys(m, ...)` | Copy of the keys, in unspecified order (Q27). | `for k in m.keys() { ... }` |
| `fn (m: map[K]V) values() -> []V` | `cfire::bm::values(m, ...)` | Copy of the values, in unspecified order (Q27). | `total := m.values().len()` |
| `const <int>.MAX: Self` | `std::numeric_limits<std::int64_t>::max()` | Largest value of the type. | `v := i32.MAX` |
| `const <int>.MIN: Self` | `std::numeric_limits<std::int64_t>::min()` | Smallest value of the type. | `v := i32.MIN` |
| `const <int>.BITS: u32` | `static_cast<std::uint32_t>(std::numeric_limits<std::make_unsigned_t<std::int64_t>>::digits)` | Width in bits (`u32`). | `v := i32.BITS` |
| `const <float>.MAX: Self` | `std::numeric_limits<double>::max()` | Largest finite value. | `v := f64.MAX` |
| `const <float>.MIN: Self` | `std::numeric_limits<double>::lowest()` | Most negative finite value. | `v := f64.MIN` |
| `const <float>.MIN_POSITIVE: Self` | `std::numeric_limits<double>::min()` | Smallest positive normal value. | `v := f64.MIN_POSITIVE` |
| `const <float>.EPSILON: Self` | `std::numeric_limits<double>::epsilon()` | Difference between `1.0` and the next representable value. | `v := f64.EPSILON` |
| `const <float>.INF: Self` | `std::numeric_limits<double>::infinity()` | Positive infinity. | `v := f64.INF` |
| `const <float>.NEG_INF: Self` | `(-std::numeric_limits<double>::infinity())` | Negative infinity. | `v := f64.NEG_INF` |
| `const <float>.NAN: Self` | `std::numeric_limits<double>::quiet_NaN()` | Not a number. | `v := f64.NAN` |
| `fn T.parse(s: str) -> T?` | `cfire::bm::parse<T>(s)` | Parses a complete literal of any numeric type or `bool` (surrounding whitespace ignored; `bool` accepts `true`/`false`/`1`/`0`); `none` on invalid or out-of-range input. | `port := u16.parse(arg) or 8080` |
| `make(T) / make(T, args...)` | `T{} / cfire_ctor_T(args...)` | A value of `T`: default field values, or the type's constructor `fn (T) make(...) -> T` (one per type). | `p := make(Point, 3, 4)` |
| `make(own[T], args...) / make(shared[T], args...)` | `cfire::make_own<T>(...) / cfire::make_shared_ptr<T>(...)` | A heap-owned `T` (`unique_ptr` / `shared_ptr`), built from one `T` value or the constructor's arguments. | `node := make(own[Node], Node{v: 1})` |
| `make([]T, len) / make([]T, len, cap)` | `cfire::make_slice<T>(len[, cap])` | `len` zero-filled elements, optionally with `cap` reserved; a negative size or `cap < len` panics. | `buf := make([]byte, 0, 4096)` |
| `make(map[K]V)` | `cfire::Map<K, V>{}` | An empty map (iteration order is unspecified, Q27). | `counts := make(map[str]i32)` |
| `fn (o: own[T]) release() -> *mut T` | `o.release()` | Gives up ownership for C interop (CF-181): returns the raw pointer and moves `o`, so any later use is `cfire/USE-AFTER-MOVE`. Not available on `shared[T]`. | `c_api_take(buf.release())` |
| `fn own[T].from_raw(p: *mut T) -> own[T]` | `std::unique_ptr<T>(p)` | Takes ownership of a pointer produced by `release` (CF-181). Adopting any other pointer is undefined behavior. | `buf := own[Buffer].from_raw(c_api_give())` |

### `core:audio` (`audio`) — Audio Engine, Polyphonic Mixer, Synth & WAV

- **Auto-Link Flags:** `(AudioToolbox / WebAudio / ALSA)`
- **Overview:** Zero-dependency multi-target audio synthesis, RIFF/WAV encoding and decoding, polyphonic `Sound` playback with pitch/pan/looping, and real-time float32 `Stream` DSP across macOS, iOS, WebAssembly, and Linux.

**Practical Example:**
```cf
package main

import "core:audio"

fn main() {
    try audio.init(48000, 2)
    defer audio.close()
    audio.set_master_volume(0.8)

    // 1. Zero-asset procedural sound effect playback
    coin_wave := audio.gen_sfx(audio.SfxPreset::Coin)
    coin_sfx := audio.load_sound_from_wave(coin_wave)
    coin_sfx.play()

    // 2. Synthesize tone, encode/save as 16-bit RIFF WAV, and stream live DSP
    tone := audio.gen_tone(audio.Waveform::Sine, 440.0, 0.25, 0.5)
    try audio.save_wav(tone, "a440.wav")

    stream := try audio.open_stream(48000, 2)
    if stream.needs_data(2048) {
        stream.push_samples(tone.samples)
    }
    println("Audio RMS: {}, Peak: {}", audio.rms_level(), audio.peak_level())
}
```

**Types:**

| Type | Kind | Description |
|---|---|---|
| `audio.Waveform` | enum | Oscillator shape for `audio.gen_tone` and `audio.gen_chirp`. Variants: `Sine`, `Square`, `Saw`, `Triangle`, `Noise`. |
| `audio.SfxPreset` | enum | A procedural sound effect for `audio.gen_sfx`. Variants: `Tap`, `Coin`, `Laser`, `Explosion`, `PowerUp`, `Jump`, `Alert`. |
| `audio.Wave` | struct | An in-memory waveform: interleaved `f32` samples in `-1.0`..`1.0`. Fields: `samples: []f32`, `sample_rate: i32`, `channels: i32`. |
| `audio.Sound` | handle | A wave registered with the mixer, playable many times at once (polyphonic). |
| `audio.Stream` | handle | A real-time output stream you push samples into, for custom synthesis. |

| `.cf` Signature | Lowered C++23 | Description | Example |
|---|---|---|---|
| `fn init(sample_rate: i32 = 48000, channels: i32 = 2) -> void!` | `cfire::audio::init(sample_rate, channels)` | Initializes the hardware audio output queue (or headless software mixer in CI). | `try audio.init(48000, 2)` |
| `fn close()` | `cfire::audio::close()` | Stops all active sounds and streams and releases audio hardware resources. | `defer audio.close()` |
| `fn is_ready() -> bool` | `cfire::audio::is_ready()` | Returns `true` if the audio subsystem is initialized. | `if audio.is_ready() { ... }` |
| `fn set_master_volume(vol: f32)` | `cfire::audio::set_master_volume(vol)` | Sets the master output volume multiplier (`0.0` to `1.0`). | `audio.set_master_volume(0.8)` |
| `fn master_volume() -> f32` | `cfire::audio::master_volume()` | Returns the current master output volume multiplier. | `v := audio.master_volume()` |
| `fn rms_level() -> f32` | `cfire::audio::rms_level()` | Returns the live master RMS amplitude (`0.0` to `1.0`) for UI audio meters. | `rms := audio.rms_level()` |
| `fn peak_level() -> f32` | `cfire::audio::peak_level()` | Returns the live master peak amplitude (`0.0` to `1.0`) for UI visualizers. | `pk := audio.peak_level()` |
| `fn gen_tone(kind: Waveform, freq_hz: f32, duration_sec: f32, volume: f32 = 0.5) -> Wave` | `cfire::audio::gen_tone(kind, freq_hz, duration_sec, volume)` | Synthesizes a stereo waveform (`Sine`, `Square`, `Saw`, `Triangle`, `Noise`) with smooth ADSR envelope. | `w := audio.gen_tone(audio.Waveform::Sine, 440.0, 0.25, 0.5)` |
| `fn gen_chirp(start_hz: f32, end_hz: f32, duration_sec: f32, kind: Waveform = Waveform::Sine, volume: f32 = 0.5) -> Wave` | `cfire::audio::gen_chirp(start_hz, end_hz, duration_sec, kind, volume)` | Synthesizes a frequency-swept tone from `start_hz` to `end_hz`. | `sweep := audio.gen_chirp(220.0, 880.0, 0.2, audio.Waveform::Saw, 0.4)` |
| `fn gen_sfx(preset: SfxPreset) -> Wave` | `cfire::audio::gen_sfx(preset)` | Generates a zero-asset procedural sound effect (`Tap`, `Coin`, `Laser`, `Explosion`, `PowerUp`, `Jump`, `Alert`). | `coin := audio.gen_sfx(audio.SfxPreset::Coin)` |
| `fn encode_wav(wave: Wave) -> []byte` | `cfire::audio::encode_wav(wave)` | Encodes an in-memory `Wave` into a 16-bit PCM RIFF/WAV byte slice. | `wav_bytes := audio.encode_wav(w)` |
| `fn decode_wav(data: []byte) -> Wave!` | `cfire::audio::decode_wav(data)` | Decodes a RIFF/WAV byte slice (8/16/24-bit PCM or 32-bit float) into a `Wave`. | `w := try audio.decode_wav(wav_bytes)` |
| `fn save_wav(wave: Wave, path: str) -> void!` | `cfire::audio::save_wav(wave, path)` | Writes a `Wave` to disk as a standard `.wav` file. | `try audio.save_wav(w, "tone.wav")` |
| `fn load_wav(path: str) -> Wave!` | `cfire::audio::load_wav(path)` | Loads and decodes a `.wav` file from disk or app bundle. | `w := try audio.load_wav("tone.wav")` |
| `fn load_sound_from_wave(wave: Wave) -> Sound` | `cfire::audio::load_sound_from_wave(wave)` | Registers an in-memory `Wave` with the polyphonic mixer and returns a playable `Sound` handle. | `sfx := audio.load_sound_from_wave(coin)` |
| `fn load_sound(path: str) -> Sound!` | `cfire::audio::load_sound(path)` | Loads a `.wav` file from disk into a playable polyphonic `Sound` handle. | `sfx := try audio.load_sound("click.wav")` |
| `fn (s: Sound) play()` | `s.play()` | Triggers polyphonic playback of the sound. | `sfx.play()` |
| `fn (s: Sound) stop()` | `s.stop()` | Stops active voices playing this sound. | `sfx.stop()` |
| `fn (s: Sound) set_volume(vol: f32)` | `s.set_volume(vol)` | Sets playback gain (`0.0` to `2.0`) for this sound. | `sfx.set_volume(0.75)` |
| `fn (s: Sound) set_pitch(pitch: f32)` | `s.set_pitch(pitch)` | Sets playback pitch/rate multiplier (`1.0` = original pitch). | `sfx.set_pitch(1.25)` |
| `fn (s: Sound) set_pan(pan: f32)` | `s.set_pan(pan)` | Sets constant-power stereo pan (`-1.0` left, `0.0` center, `+1.0` right). | `sfx.set_pan(-0.5)` |
| `fn (s: Sound) set_looping(loop: bool)` | `s.set_looping(loop)` | Enables or disables seamless loop playback. | `sfx.set_looping(true)` |
| `fn open_stream(sample_rate: i32 = 48000, channels: i32 = 2) -> Stream!` | `cfire::audio::open_stream(sample_rate, channels)` | Opens a real-time push `Stream` for custom DSP synthesis. | `stream := try audio.open_stream(48000, 2)` |
| `fn (st: Stream) push_samples(samples: []f32)` | `st.push_samples(samples)` | Pushes interleaved `f32` PCM samples into the stream's lock-free ring buffer. | `stream.push_samples(pcm_chunk)` |
| `fn (st: Stream) needs_data(target_frames: i32 = 2048) -> bool` | `st.needs_data(target_frames)` | Returns `true` when buffered frames fall below `target_frames`. | `if stream.needs_data(1024) { ... }` |

### `core:build` (`build`) — Declarative Project & Cross-Platform Build System (`build.cf`)

- **Auto-Link Flags:** `(compiler-integrated)`
- **Overview:** Declarative build manifest module (`build.cf`) for configuring native, GUI, WebAssembly, and iOS executables (`build.Executable`), static/shared libraries (`build.Library`), pre-build codegen steps (`build.Step`), external CMake/Git dependencies (`build.CMake`, `build.GitDependency`), pkg-config/framework links, and compile-time host/target queries (`build.os()`, `build.arch()`).

**Practical Example:**
```cf
package main

import "core:build"

// Declarative desktop GUI & CLI build manifest (`build.cf`)
app := build.Executable("Flint", {
    Entry: "src/main.cf",
    Subsystem: "gui",
    OutDir: "build",
    BundleId: "dev.flint.app",
    Icon: "assets/icon.png",
    Targets: ["native"]
})

fn main() {
    println("Target OS: {}, Arch: {}", build.os(), build.arch())
}
```

**Types:**

| Type | Kind | Description |
|---|---|---|
| `build.CC` | manifest | In `build.cf`: compiles C or C++ sources into the project (`build.CC({ Sources: [...] })`). |
| `build.CMake` | manifest | In `build.cf`: an external CMake project built as a dependency. |
| `build.Make` | manifest | In `build.cf`: an external Make project built as a dependency. |
| `build.Git` | manifest | In `build.cf`: a dependency fetched from a Git repository at a pinned revision. |
| `build.Archive` | manifest | In `build.cf`: a dependency fetched as a checksummed archive. |
| `build.Command` | manifest | In `build.cf`: a command run as part of the build. |
| `build.Executable` | manifest | In `build.cf`: declares an executable target (`build.Executable(name, { Entry: ... })`). |
| `build.Library` | manifest | In `build.cf`: declares a static or shared library target. |
| `build.Step` | manifest | In `build.cf`: a code-generation step run when its inputs are newer than its outputs. |
| `build.PkgConfig` | manifest | In `build.cf`: links a system library found through pkg-config. |
| `build.Link` | manifest | In `build.cf`: links a library by name. |
| `build.Include` | manifest | In `build.cf`: adds a header search directory. |
| `build.Tako` | manifest | In `build.cf`: enables the Tako deployment integration. |
| `build.Target` | manifest | In `build.cf`: what `build.Executable` and `build.Library` return. |
| `build.StepTarget` | manifest | In `build.cf`: what `build.Step` returns. |

| `.cf` Signature | Lowered C++23 | Description | Example |
|---|---|---|---|
| `fn Executable(name: str, cfg: ExecutableConfig) -> Target` | `(buildsys manifest)` | Declares an executable build target with `Entry`, `Subsystem` (`"cli"` or `"gui"`), `OutDir`, `Targets` (`["native", "wasm", "ios-sim"]`), `WebProfile`, `BundleId`, `AppName`, `Icon` (`.png`/`.svg`/`.icns`/`.ico`), `Sources`, `Includes`, `Defines`, and `Flags`. | `app := build.Executable("Flint", { Entry: "src/main.cf", Subsystem: "gui", BundleId: "dev.flint.app", Icon: "assets/icon.png" })` |
| `fn Library(name: str, cfg: LibraryConfig) -> Target` | `(buildsys manifest)` | Declares a static or shared C/C++/cfire library target with `Sources`, `Includes`, `Defines`, and `Flags`. | `lib := build.Library("corelib", { Sources: ["native/bridge.cpp"], Includes: ["native"] })` |
| `fn Step(name: str, cfg: StepConfig) -> StepTarget` | `(buildsys manifest)` | Declares a custom build step executing `Command` and `Args` in `WorkDir` whenever `Inputs` are newer than `Outputs`. | `gen := build.Step("assets", { Command: "python3", Args: ["scripts/bundle.py"], Inputs: ["ui/index.html"], Outputs: ["ui/bundle.js"] })` |
| `fn CMake(name: str, cfg: CMakeConfig) -> DepTarget` | `(buildsys manifest)` | Declares an external CMake or Git dependency (`Url`, `Commit`/`Branch`, `Sha256`, `Includes`, `Flags`, `Driver`). | `dep := build.CMake("zstd", { Url: "https://github.com/facebook/zstd.git", Branch: "v1.5.6" })` |
| `fn Link(lib: str)` | `(buildsys manifest)` | Adds a system library (`"sqlite3"`) or Apple framework (`"-framework Security"`) to the linker flags. | `build.Link("sqlite3")` |
| `fn Include(dir: str)` | `(buildsys manifest)` | Adds a relative or absolute header search directory (`-I<dir>`) for `import cpp` headers. | `build.Include("src/native/include")` |
| `fn PkgConfig(pkg: str)` | `(buildsys manifest)` | Queries `pkg-config --cflags --libs <pkg>` and links the resolved flags into the build. | `build.PkgConfig("libssh")` |
| `fn Tako()` | `(buildsys manifest)` | Auto-discovers and links the local Tako GUI runtime and headers (`import "pkg:tako"`). | `build.Tako()` |
| `fn os() -> str` | `(compile-time constant)` | Returns the target operating system identifier (`"macos"`, `"linux"`, `"windows"`, `"ios"`, `"wasm"`). | `if build.os() == "macos" { ... }` |
| `fn arch() -> str` | `(compile-time constant)` | Returns the target CPU architecture identifier (`"arm64"`, `"x86_64"`, `"wasm32"`). | `if build.arch() == "arm64" { ... }` |

### `core:cli` (`cli`) — Struct-Tag Command-Line Parser & Interactive Prompts

- **Auto-Link Flags:** `(header-only)`
- **Overview:** Generates zero-boilerplate command-line flag parsers from struct field tags (`cli:"--flag,help"`), subcommand/positional argument helpers, and interactive terminal prompts.

**Practical Example:**
```cf
package main

import "core:cli"

type AppFlags struct {
    host:    str  `cli:"--host"    default:"127.0.0.1"`
    port:    u16  `cli:"--port,-p" default:"8080"`
    workers: i32  `cli:"--workers" default:"4"`
    verbose: bool `cli:"--verbose,-v"`
}

fn main() {
    // Automatically binds command-line arguments to typed struct
    flags := try cli.parse[AppFlags]()
    println("Server listening on {}:{} with {} workers (verbose={})",
        flags.host, flags.port, flags.workers, flags.verbose)
}
```

| `.cf` Signature | Lowered C++23 | Description | Example |
|---|---|---|---|
| `fn parse[T](args: []str = []) -> T!` | `cfire::cli::parse<T>(argc, argv)` | Parses CLI flags (`str`, `i64`, `i32`, `f64`, `bool`) into a tagged struct `T`. | `args := try cli.parse[Args]()` |
| `fn try_parse[T](args: []str = []) -> T?` | `cfire::cli::try_parse<T>(argc, argv)` | Parses and validates CLI flags into a tagged struct `T`, returning `none` if any numeric flag argument is invalid. | `args := cli.try_parse[Args]() or { return }` |
| `fn args() -> []str` | `cfire::cli::args()` | Returns the raw command-line arguments passed to the process (excluding `argv[0]`). | `raw := cli.args()` |
| `fn subcommand() -> str` | `cfire::cli::subcommand()` | Returns the first non-flag positional argument (subcommand name) or `""` if none. | `cmd := cli.subcommand()` |
| `fn positional() -> []str` | `cfire::cli::positional()` | Returns all non-flag positional arguments passed on the command line. | `files := cli.positional()` |
| `fn prompt(message: str = "") -> str?` | `cfire::cli::prompt(message)` | Prints `message` to stderr and reads a line from stdin without trailing newline, or `none` on EOF. | `user := cli.prompt("Username: ") or { return }` |
| `fn prompt_password(message: str = "") -> str?` | `cfire::cli::prompt_password(message)` | Prints `message` to stderr with terminal echo disabled (`termios` `~ECHO`) and reads a password line from stdin, or `none` on EOF. | `pw := cli.prompt_password("Password: ") or { return }` |

### `core:collections` (`collections`) — Generic Containers (Set, PriorityQueue, MinQueue, Deque)

- **Auto-Link Flags:** `(header-only)`
- **Overview:** Standard generic containers backed by STL unordered_set, priority_queue, and deque.

**Practical Example:**
```cf
package main

import "core:collections"

fn main() {
    // 1. Generic Hash Set
    mut set := collections.Set[str]{}
    set.insert("alpha")
    set.insert("beta")
    println("Set contains 'alpha': {}", set.contains("alpha"))

    // 2. Generic Max-Heap Priority Queue
    mut pq := collections.PriorityQueue[i64]{}
    pq.push(10)
    pq.push(95)
    pq.push(42)
    println("Highest priority element: {}", pq.top()) // 95

    // 3. Double-Ended Queue (Deque)
    mut dq := collections.Deque[str]{}
    dq.push_back("item_2")
    dq.push_front("item_1")
    println("Front: {}, Back: {}", dq.pop_front(), dq.pop_back())
}
```

**Types:**

| Type | Kind | Description |
|---|---|---|
| `collections.Set[T]` | handle | An unordered set of unique values. |
| `collections.Deque[T]` | handle | A double-ended queue: push and pop at both ends in constant time. |
| `collections.PriorityQueue[T]` | handle | A max-heap: `pop()` returns the largest value first, or `none` when empty. |
| `collections.MinQueue[T]` | handle | A min-heap: `pop()` returns the smallest value first, or `none` when empty. |

| `.cf` Signature | Lowered C++23 | Description | Example |
|---|---|---|---|
| `type Set[T] struct` | `cfire::collections::Set<T>` | Generic hash set container. | `mut s := collections.Set[str]{}` |
| `type PriorityQueue[T] struct` | `cfire::collections::PriorityQueue<T>` | Max-first priority queue: `push(v)`, `pop() -> T?`, `peek() -> T?`, `len()`, `empty()`. | `mut pq := collections.PriorityQueue[i32]{}` |
| `type MinQueue[T] struct` | `cfire::collections::MinQueue<T>` | Min-first priority queue (smallest element pops first), with the same methods as `PriorityQueue[T]`. Tuples order element by element, so `MinQueue[(i64, i64)]` pops the lowest cost first. | `mut open := collections.MinQueue[i64]{}` |
| `type Deque[T] struct` | `cfire::collections::Deque<T>` | Generic double-ended queue. | `mut dq := collections.Deque[i32]{}` |

### `core:crypto/aes` (`aes`) — NIST FIPS 197 AES-256 Symmetric Encryption & GCM AEAD

- **Auto-Link Flags:** `(Header-only pure C++23)`
- **Overview:** Constant-time AES-256 block cipher implementation with Counter Mode (CTR) and Galois/Counter Mode (GCM) Authenticated Encryption with Associated Data (AEAD).

**Practical Example:**
```cf
package main

import "core:crypto/aes"
import "core:rand"

fn main() {
    key := rand.bytes(32)   // 256-bit AES key
    nonce := rand.bytes(12) // 96-bit standard GCM nonce
    secret := "Confidential credentials and token payload"
    aad := "tenant_id:prod_42"

    // Authenticated Encryption with Associated Data (GCM AEAD)
    res := try aes.encrypt_gcm(key, nonce, secret, aad)
    println("Ciphertext length: {} bytes, Tag: {:02x}", res.ciphertext.len(), res.tag[0])

    // Decrypt and verify cryptographic authenticity tag
    plain := try aes.decrypt_gcm(key, nonce, res.ciphertext, res.tag, aad)
    println("Authenticated decrypted plaintext: {}", plain)
}
```

**Types:**

| Type | Kind | Description |
|---|---|---|
| `aes.GcmResult` | struct | The output of AES-256-GCM encryption. Fields: `ciphertext: []byte`, `tag: []byte`. |

| `.cf` Signature | Lowered C++23 | Description | Example |
|---|---|---|---|
| `fn generate_key() -> []byte` | `cfire::aes::generate_key()` | Generates a 32-byte (256-bit) cryptographically secure AES-256 key from OS entropy. | `key := aes.generate_key()` |
| `fn encrypt(key: []byte, plaintext: []byte, aad: []byte = []) -> []byte!` | `cfire::aes::encrypt(key, pt, aad)` | Encrypts `plaintext` with AES-256-GCM using a fresh 12-byte random nonce and returns `[12-byte nonce || ciphertext || 16-byte tag]`. | `blob := try aes.encrypt(key, secret, aad)` |
| `fn decrypt(key: []byte, combined: []byte, aad: []byte = []) -> []byte!` | `cfire::aes::decrypt(key, combined, aad)` | Authenticates and decrypts a combined `[12-byte nonce || ciphertext || 16-byte tag]` AES-256-GCM payload. | `pt := try aes.decrypt(key, blob, aad)` |
| `fn encrypt_gcm(key: []byte, nonce: []byte, plaintext: []byte, aad: []byte = []) -> GcmResult!` | `cfire::aes::encrypt_gcm(key, nonce, pt, aad)` | Encrypts plaintext with AES-256-GCM AEAD, producing ciphertext and a 16-byte authentication tag. | `res := try aes.encrypt_gcm(key, nonce, secret, aad)` |
| `fn decrypt_gcm(key: []byte, nonce: []byte, ciphertext: []byte, tag: []byte, aad: []byte = []) -> []byte!` | `cfire::aes::decrypt_gcm(key, nonce, ct, tag, aad)` | Authenticates and decrypts AES-256-GCM ciphertext, failing if tag verification fails or data was tampered. | `pt := try aes.decrypt_gcm(key, nonce, res.ciphertext, res.tag, aad)` |
| `fn encrypt_ctr(key: []byte, iv: []byte, plaintext: []byte) -> []byte!` | `cfire::aes::encrypt_ctr(key, iv, pt)` | Encrypts plaintext using AES-256-CTR mode. | `ct := try aes.encrypt_ctr(key, iv, pt)` |
| `fn decrypt_ctr(key: []byte, iv: []byte, ciphertext: []byte) -> []byte!` | `cfire::aes::decrypt_ctr(key, iv, ct)` | Decrypts ciphertext using AES-256-CTR mode. | `pt := try aes.decrypt_ctr(key, iv, ct)` |
| `fn decrypt_gcm_str(key: []byte, nonce: []byte, ct: []byte, tag: []byte, aad: []byte = []) -> str!` | `cfire::aes::decrypt_gcm_str(key, nonce, ct, tag, aad)` | Decrypts an authenticated AES-GCM ciphertext and validates the tag, returning a UTF-8 string. | `pt := try aes.decrypt_gcm_str(key, nonce, ct, tag)` |

### `core:crypto/blake3` (`blake3`) — Cryptographic 256-Bit Digest (Alias)

- **Auto-Link Flags:** `(CommonCrypto / built-in)`
- **Overview:** Computes fast 256-bit cryptographic hex digests over byte slices (convenience alias for `core:crypto/sha256`).

**Practical Example:**
```cf
package main

import "core:crypto/blake3"

fn main() {
    blob := []byte("immutable chunk storage block 0x8842")
    hash := blake3.hash_hex(blob)
    println("Blake3 256-bit digest: {}", hash)
}
```

| `.cf` Signature | Lowered C++23 | Description | Example |
|---|---|---|---|
| `fn hash_hex(data: []byte) -> str` | `cfire::blake3::hash_hex(data)` | Returns a 64-character lowercase hex digest of `data`. | `digest := blake3.hash_hex(raw)` |

### `core:crypto/hmac` (`hmac`) — RFC 2104 HMAC-SHA256, RFC 5869 HKDF & PBKDF2-SHA256

- **Auto-Link Flags:** `(CommonCrypto / built-in)`
- **Overview:** Computes RFC 2104 keyed HMAC-SHA256 authentication digests, RFC 5869 HKDF-SHA256 derived keys, and RFC 8018 PBKDF2-HMAC-SHA256 password-derived keys.

**Practical Example:**
```cf
package main

import "core:crypto/hmac"

fn main() {
    secret_key := "super-secure-signing-secret"
    data := "tenant_id:42;session:8849"

    signature := hmac.sha256_hex(secret_key, data)
    println("HMAC-SHA256 signature: {}", signature)

    // Constant-time validation check prevents timing side-channel attacks
    is_valid := hmac.equal(signature, signature)
    println("Signature verified: {}", is_valid)
}
```

| `.cf` Signature | Lowered C++23 | Description | Example |
|---|---|---|---|
| `fn sha256_hex(key: str, data: str) -> str` | `cfire::hmac::sha256_hex(key, data)` | Returns a 64-character lowercase hex HMAC-SHA256 digest. | `sig := hmac.sha256_hex(secret, payload)` |
| `fn sha256_bytes(key: []byte, data: []byte) -> []byte` | `cfire::crypto::hmac_sha256(key, data)` | Computes HMAC-SHA256 over raw byte slices. | `mac := hmac.sha256_bytes(key, data)` |
| `fn verify(key: str, data: str, mac: str) -> bool` | `cfire::hmac::verify(key, data, mac)` | Verifies in constant time whether `mac` matches the computed HMAC-SHA256 hex digest of `data` under `key`. | `valid := hmac.verify(secret, payload, mac)` |
| `fn equal(a: str, b: str) -> bool` | `cfire::hmac::equal(a, b)` | Performs a constant-time equality check on two MAC digests to prevent timing attacks. | `ok := hmac.equal(sig1, sig2)` |
| `fn hkdf_sha256(ikm: []byte, salt: []byte = [], info: []byte = [], length: usize = 32) -> []byte` | `cfire::hmac::hkdf_sha256(ikm, salt, info, length)` | Derives cryptographic key material of `length` bytes using RFC 5869 HKDF-SHA256 Extract-and-Expand. | `key := hmac.hkdf_sha256(ikm, salt, info, 32)` |
| `fn hkdf_sha256_hex(ikm: str, salt: str = "", info: str = "", length: usize = 32) -> str` | `cfire::hmac::hkdf_sha256_hex(ikm, salt, info, length)` | Derives a hex-encoded key string using RFC 5869 HKDF-SHA256. | `khex := hmac.hkdf_sha256_hex(secret, "salt", "ctx", 32)` |
| `fn pbkdf2_sha256(password: str, salt: []byte, iterations: u32 = 600000, length: usize = 32) -> []byte` | `cfire::hmac::pbkdf2_sha256(password, salt, iterations, length)` | Derives a cryptographic key of `length` bytes from `password` and `salt` using RFC 8018 PBKDF2-HMAC-SHA256. | `key := hmac.pbkdf2_sha256(password, salt, 600000, 32)` |

### `core:crypto/sha256` (`sha256`) — Standard FIPS 180-4 SHA-256 Digest & CSPRNG

- **Auto-Link Flags:** `(CommonCrypto / built-in)`
- **Overview:** Computes NIST FIPS 180-4 SHA-256 digests and generates OS-backed cryptographically secure random bytes (`arc4random_buf` / `/dev/urandom`).

**Practical Example:**
```cf
package main

import "core:crypto/sha256"

fn main() {
    payload := "system_audit_transaction_event_2026"
    digest := sha256.hash_hex(payload)
    println("FIPS 180-4 SHA-256 Digest: {}", digest)
}
```

| `.cf` Signature | Lowered C++23 | Description | Example |
|---|---|---|---|
| `fn hash_hex(data: []byte) -> str` | `cfire::sha256::hash_hex(data)` | Returns a 64-character lowercase hex digest of `data`. | `digest := sha256.hash_hex(raw)` |
| `fn hash_bytes(data: []byte) -> []byte` | `cfire::sha256::hash_bytes(data)` | Returns 32-byte binary SHA-256 digest of `data`. | `b := sha256.hash_bytes(raw)` |
| `fn random_bytes(n: usize) -> []byte` | `cfire::sha256::random_bytes(n)` | Generates `n` cryptographically secure random bytes from the OS CSPRNG. | `nonce := sha256.random_bytes(12)` |
| `fn random_hex(n: usize = 32) -> str` | `cfire::sha256::random_hex(n)` | Generates `n` cryptographically secure random bytes encoded as a `2*n`-character lowercase hex string. | `token := sha256.random_hex(16)` |
| `fn hash_file_hex(path: str) -> str!` | `cfire::sha256::hash_file_hex(path)` | Computes the hex-encoded SHA-256 digest of a file on disk. | `h := try sha256.hash_file_hex("file.bin")` |

### `core:encoding/base64` (`base64`) — RFC 4648 Base64 & URL-Safe Encoding/Decoding

- **Auto-Link Flags:** `(header-only)`
- **Overview:** High-performance, zero-allocation Base64 and URL-safe Base64 encoder and decoder.

**Practical Example:**
```cf
package main

import "core:encoding/base64"

fn main() {
    credentials := "service_account:token_xyz_9988"
    encoded := base64.encode_str(credentials)
    println("Base64 encoded: {}", encoded)

    decoded := try base64.decode_str(encoded)
    println("Decoded text: {}", decoded)
}
```

| `.cf` Signature | Lowered C++23 | Description | Example |
|---|---|---|---|
| `fn encode(data: []byte, url_safe: bool = false) -> str` | `cfire::base64::encode(data, url_safe)` | Encodes binary byte slice into standard or URL-safe Base64 string. | `s := base64.encode(raw)` |
| `fn encode_str(s: str, url_safe: bool = false) -> str` | `cfire::base64::encode_str(s, url_safe)` | Encodes a UTF-8 text string into standard or URL-safe Base64. | `token := base64.encode_str("user:pass")` |
| `fn decode(s: str) -> []byte!` | `cfire::base64::decode(s)` | Decodes a Base64 string into binary bytes. | `bytes := try base64.decode(token)` |
| `fn decode_str(s: str) -> str!` | `cfire::base64::decode_str(s)` | Decodes a Base64 string into a UTF-8 text string. | `plain := try base64.decode_str(token)` |

### `core:encoding/csv` (`csv`) — RFC 4180 CSV Parsing & Stringification

- **Auto-Link Flags:** `(header-only)`
- **Overview:** RFC 4180 compliant CSV parser and serializer supporting multi-line quoted fields and comma escaping.

**Practical Example:**
```cf
package main

import "core:encoding/csv"

fn main() {
    csv_data := "service,region,qps\nauth,us-west-2,1450\napi,eu-central-1,4200\n"
    table := csv.parse(csv_data)
    for row in table {
        println("Service: {}, Region: {}, QPS: {}", row[0], row[1], row[2])
    }

    // Serialize 2D slice back into RFC 4180 CSV
    rendered := csv.stringify(table)
    println("Output:\n{}", rendered)
}
```

| `.cf` Signature | Lowered C++23 | Description | Example |
|---|---|---|---|
| `fn parse(s: str) -> [][]str` | `cfire::csv::parse(s)` | Parses a CSV string into a 2D table of rows and columns. | `rows := csv.parse(content)` |
| `fn stringify(rows: [][]str) -> str` | `cfire::csv::stringify(rows)` | Serializes a 2D table of strings into an RFC 4180 formatted CSV string. | `out := csv.stringify(rows)` |
| `fn read_all(path: str) -> [][]str!` | `cfire::csv::read_all(path)` | Reads an entire CSV file from disk and parses it into rows. | `table := try csv.read_all("data.csv")` |
| `fn write_all(path: str, rows: [][]str) -> void!` | `cfire::csv::write_all(path, rows)` | Serializes rows to CSV and writes to disk. | `try csv.write_all("report.csv", table)` |

### `core:encoding/hex` (`hex`) — Hexadecimal Encoding & Decoding

- **Auto-Link Flags:** `(header-only)`
- **Overview:** Encodes binary bytes to lowercase hexadecimal strings and decodes hex strings to byte buffers.

**Practical Example:**
```cf
package main

import "core:encoding/hex"

fn main() {
    binary_bytes := []byte([0xCA, 0xFE, 0xBA, 0xBE])
    hex_str := hex.encode(binary_bytes)
    println("Hexadecimal string: 0x{}", hex_str)

    restored := try hex.decode(hex_str)
    println("Restored byte length: {}", restored.len())
}
```

| `.cf` Signature | Lowered C++23 | Description | Example |
|---|---|---|---|
| `fn encode(data: []byte) -> str` | `cfire::hex::encode(data)` | Encodes binary bytes to lowercase hexadecimal string. | `h := hex.encode(digest)` |
| `fn encode_str(s: str) -> str` | `cfire::hex::encode_str(s)` | Encodes text characters to hexadecimal string. | `h := hex.encode_str("abc")` |
| `fn decode(s: str) -> []byte!` | `cfire::hex::decode(s)` | Decodes a hexadecimal string to binary bytes. | `buf := try hex.decode(h)` |
| `fn decode_str(s: str) -> str!` | `cfire::hex::decode_str(s)` | Decodes a hexadecimal string to a UTF-8 string. | `txt := try hex.decode_str(h)` |

### `core:fs` (`fs`) — Filesystem & Zero-Copy Byte I/O

- **Auto-Link Flags:** `(header-only)`
- **Overview:** Provides RAII file and directory utilities backed by `std::filesystem` and `std::vector<std::uint8_t>` with strict `_bytes` / `_text` symmetry.

**Practical Example:**
```cf
package main

import "core:fs"

fn main() {
    // 1. High-level one-shot file read and write
    try fs.write_text("config.ini", "workers=8\nport=9000\n")
    content := try fs.read_text("config.ini")
    println("Read configuration: {}", content)

    // 2. Streaming file operations with deterministic RAII defer
    mut file := try fs.open("service.log", fs.OpenMode::Append)
    defer file.close() // Closed deterministically on return

    _ = try file.write_text("[INFO] Worker subsystem initialized\n")
    try file.flush()
    offset := try file.tell()
    println("Log stream offset: {} bytes", offset)
}
```

**Types:**

| Type | Kind | Description |
|---|---|---|
| `fs.OpenMode` | enum | How `fs.open` opens a file. Variants: `Read`, `Write`, `Append`, `ReadWrite`. |
| `fs.SeekWhence` | enum | The origin for `file.seek`. Variants: `Start`, `Current`, `End`. |
| `fs.File` | handle | An open file, closed automatically when it goes out of scope. |

| `.cf` Signature | Lowered C++23 | Description | Example |
|---|---|---|---|
| `fn read_bytes(path: str) -> []byte!` | `cfire::fs::read_bytes(path)` | Reads an entire binary file into a byte buffer. | `raw := try fs.read_bytes("input.bin")` |
| `fn write_bytes(path: str, data: []byte) -> void!` | `cfire::fs::write_bytes(path, data)` | Writes a byte slice to disk (truncating any existing file). | `try fs.write_bytes("out.bin", raw)` |
| `fn append_bytes(path: str, data: []byte) -> void!` | `cfire::fs::append_bytes(path, data)` | Appends a byte slice to a file on disk (creating the file if it does not exist). | `try fs.append_bytes("out.bin", raw)` |
| `fn read_text(path: str) -> str!` | `cfire::fs::read_text(path)` | Reads an entire UTF-8 text file into a `str`. | `txt := try fs.read_text("config.json")` |
| `fn write_text(path: str, text: str) -> void!` | `cfire::fs::write_text(path, text)` | Writes a UTF-8 string to disk. | `try fs.write_text("hello.txt", "sapphire")` |
| `fn append_text(path: str, text: str) -> void!` | `cfire::fs::append_text(path, text)` | Appends a UTF-8 string to a file on disk. | `try fs.append_text("app.log", "started\n")` |
| `fn rename(from: str, to: str) -> void!` | `cfire::fs::rename(from, to)` | Renames or moves a file or directory. | `try fs.rename("tmp.bin", "final.bin")` |
| `fn copy_file(from: str, to: str) -> void!` | `cfire::fs::copy_file(from, to)` | Copies a file from `from` to `to` (overwriting `to` if it exists). | `try fs.copy_file("a.bin", "b.bin")` |
| `fn file_size(path: str) -> u64!` | `cfire::fs::file_size(path)` | Returns the size of a file in bytes. | `sz := try fs.file_size("input.bin")` |
| `fn open(path: str, mode: OpenMode = OpenMode::Read) -> File!` | `cfire::fs::open(path, mode)` | Opens a file with specified mode (Read, Write, Append, ReadWrite) returning a streaming File handle. | `mut f := try fs.open("log.txt", OpenMode::Append)` |
| `fn (f: File) fd() -> i32` | `f.fd()` | Returns the underlying OS file descriptor (`-1` if closed). | `fd := f.fd()` |
| `fn (f: File) is_open() -> bool` | `f.is_open()` | Returns `true` if the file handle holds a valid open file descriptor. | `if f.is_open() { ... }` |
| `fn (f: File) is_tty() -> bool` | `f.is_tty()` | Returns `true` if the file stream refers to an interactive terminal. | `if os.stdout().is_tty() { ... }` |
| `fn (f: File) poll(timeout_ms: i64 = 0) -> bool` | `f.poll(timeout_ms)` | Polls the file stream for readability up to `timeout_ms` milliseconds (`EINTR`-safe). | `if r.poll(50) { ... }` |
| `fn (mut f: File) read_into(mut buf: []byte) -> usize!` | `f.read_into(buf)` | Reads up to `buf.len()` bytes from the file stream into `buf`. | `n := try f.read_into(mut buf)` |
| `fn (mut f: File) read_exact(exact_bytes: usize) -> []byte!` | `f.read_exact(exact_bytes)` | Reads exactly `exact_bytes` from the file stream or returns an error on premature EOF. | `hdr := try f.read_exact(8)` |
| `fn (mut f: File) read_bytes(max_bytes: usize = 67108864) -> []byte!` | `f.read_bytes(max_bytes)` | Reads up to `max_bytes` from the file stream into a new byte slice. | `chunk := try f.read_bytes(4096)` |
| `fn (mut f: File) read_text(max_bytes: usize = 67108864) -> str!` | `f.read_text(max_bytes)` | Reads up to `max_bytes` from the file stream as a UTF-8 string. | `body := try f.read_text()` |
| `fn (mut f: File) read_bytes_until(delim: byte, max_bytes: usize = 65536) -> []byte!` | `f.read_bytes_until(delim, max_bytes)` | Reads bytes from the file stream up to and including `delim` (or `max_bytes`). | `frame := try f.read_bytes_until(byte(0))` |
| `fn (mut f: File) read_text_until(delim: str, max_bytes: usize = 65536) -> str!` | `f.read_text_until(delim, max_bytes)` | Reads text from the file stream up to and including the first byte of `delim` (or `max_bytes`). | `line := try f.read_text_until("\n")` |
| `fn (mut f: File) read_line(max_bytes: usize = 65536) -> str?` | `f.read_line(max_bytes)` | Reads the next line from the file stream (stripping `\r\n` / `\n`), or `none` on EOF. | `line := f.read_line() or { return }` |
| `fn (mut f: File) read_lines() -> []str!` | `f.read_lines()` | Reads all remaining lines from the file stream into a string slice. | `lines := try f.read_lines()` |
| `fn (mut f: File) write_bytes(data: []byte) -> usize!` | `f.write_bytes(data)` | Writes all bytes in `data` to the file stream, looping across partial writes and `EINTR`. | `n := try f.write_bytes(raw)` |
| `fn (mut f: File) write_text(text: str) -> usize!` | `f.write_text(text)` | Writes a UTF-8 string to the file stream, looping across partial writes and `EINTR`. | `n := try f.write_text("header\n")` |
| `fn (mut f: File) seek(offset: i64, whence: SeekWhence = SeekWhence::Start) -> i64!` | `f.seek(offset, whence)` | Seeks within file to offset from Start, Current, or End. | `pos := try f.seek(0, SeekWhence::Start)` |
| `fn (mut f: File) tell() -> i64!` | `f.tell()` | Returns current byte offset within file stream. | `pos := try f.tell()` |
| `fn (mut f: File) flush() -> void!` | `f.flush()` | Flushes buffered stream data to disk. | `try f.flush()` |
| `fn (mut f: File) close() -> void` | `f.close()` | Closes open file handle. | `f.close()` |
| `fn dirname(path: str) -> str` | `cfire::fs::dirname(path)` | Returns directory portion of a filesystem path. | `dir := fs.dirname("/a/b/c.txt")` |
| `fn basename(path: str) -> str` | `cfire::fs::basename(path)` | Returns file name portion of a filesystem path. | `name := fs.basename("/a/b/c.txt")` |
| `fn extension(path: str) -> str` | `cfire::fs::extension(path)` | Returns file extension including leading dot. | `ext := fs.extension("main.cf")` |
| `fn exists(path: str) -> bool` | `cfire::fs::exists(path)` | Returns `true` if the path exists on disk. | `if fs.exists("build.cf") { ... }` |
| `fn is_dir(path: str) -> bool` | `cfire::fs::is_dir(path)` | Returns `true` if `path` exists and is a directory. | `if fs.is_dir("src") { ... }` |
| `fn is_file(path: str) -> bool` | `cfire::fs::is_file(path)` | Returns `true` if `path` exists and is a regular file. | `if fs.is_file("build.cf") { ... }` |
| `fn mkdir_all(path: str) -> void!` | `cfire::fs::mkdir_all(path)` | Recursively creates a directory and all parent directories. | `try fs.mkdir_all(".cfire/cache")` |
| `fn remove(path: str) -> void!` | `cfire::fs::remove(path)` | Removes a file or directory tree recursively. | `try fs.remove("tmp_dir")` |
| `fn list_dir(path: str) -> []str!` | `cfire::fs::list_dir(path)` | Lists sorted filenames inside a directory. | `entries := try fs.list_dir("examples")` |

### `core:json` (`json`) — Compile-Time SIMD JSON Reflection & Dynamic DOM

- **Auto-Link Flags:** `-lsimdjson (or header-only)`
- **Overview:** Uses `json:"..."` struct tags (with automatic field-name fallback and `T?` optional field support) to emit specialized `simdjson::ondemand` parsers and fast `std::format` serializers with zero runtime reflection overhead, along with a zero-dependency dynamic DOM `json.Value`.

**Practical Example:**
```cf
package main

import "core:json"

type ServiceMetric struct {
    service: str  `json:"service"`
    latency: f64  `json:"latency_ms"`
    healthy: bool `json:"healthy"`
}

fn main() {
    // 1. Zero-overhead SIMD deserialization using compile-time struct tags
    payload := `{"service":"auth","latency_ms":1.42,"healthy":true}`
    m := try json.parse[ServiceMetric](payload)
    println("Service={}, Latency={}ms, Healthy={}", m.service, m.latency, m.healthy)

    // 2. High-speed JSON serialization
    serialized := json.stringify(m)
    println("Serialized output: {}", serialized)

    // 3. Dynamic DOM for arbitrary or schemaless JSON
    mut doc := try json.parse_dynamic(`{"tags":["systems","c++23"],"cluster":{"nodes":3}}`)
    println("Cluster nodes: {}", doc.get("cluster").get("nodes").as_int())
}
```

**Types:**

| Type | Kind | Description |
|---|---|---|
| `json.Value` | handle | A parsed JSON value of any shape, from `json.parse_dynamic`; inspect it with its methods. |

| `.cf` Signature | Lowered C++23 | Description | Example |
|---|---|---|---|
| `fn parse[T](payload: []byte | str) -> T!` | `cfire::json::parse<T>(payload)` | Parses UTF-8 JSON into a struct `T` using `simdjson::ondemand`. | `cfg := try json.parse[Config](raw)` |
| `fn stringify[T](val: T) -> str` | `cfire::json::stringify<T>(val)` | Serializes a struct `T` (`str`, `u64`, `i64`, `i32`, `f64`, `bool`, `T?`, slices, nested structs) into a JSON string. | `payload := json.stringify(metric)` |
| `fn schema[T]() -> str` | `cfire::json::schema<T>()` | Generates a Draft 2020-12 / OpenAI Strict Mode / MCP JSON Schema string for struct `T` using `json:"..."` and `desc:"..."` tags. | `s := json.schema[SearchTool]()` |
| `fn escape_string(s: str) -> str` | `cfire::json::escape_string(s)` | Escapes quotes, backslashes, and control characters for JSON strings. | `esc := json.escape_string(raw_txt)` |
| `fn parse_dynamic(payload: str) -> Value!` | `cfire::json::parse_dynamic(payload)` | Parses arbitrary JSON into a recursive dynamic DOM `Value`. | `v := try json.parse_dynamic(raw)` |
| `fn stringify_dynamic(val: Value, indent: i32 = 0) -> str` | `cfire::json::stringify_dynamic(val, indent)` | Serializes a dynamic DOM `Value` into compact or indented JSON. | `s := json.stringify_dynamic(v, 2)` |

### `core:keychain` (`keychain`) — Native OS Credential Keychain & Secret Service

- **Auto-Link Flags:** `-framework Security (macOS/iOS) / libsecret (Linux) / Advapi32 (Windows)`
- **Overview:** Cross-platform OS credential storage backed by `Security.framework` (`SecItemAdd` / `SecItemCopyMatching` / `SecItemDelete`) on macOS & iOS, `libsecret` (`secret-tool`) on Linux, and Windows Credential Manager (`CredWriteA` / `CredReadA`) on Windows.

**Practical Example:**
```cf
package main

import "core:keychain"

fn main() {
    try keychain.set("dev.flint.ssh", "vault-key", "super-secret-token")
    secret := try keychain.get("dev.flint.ssh", "vault-key")
    println("Retrieved secret length: {}", secret.len())
    try keychain.delete_password("dev.flint.ssh", "vault-key")
}
```

| `.cf` Signature | Lowered C++23 | Description | Example |
|---|---|---|---|
| `fn set(service: str, account: str, secret: str) -> void!` | `cfire::keychain::set(service, account, secret)` | Stores or updates a generic password for `(service, account)` in the OS keychain. | `try keychain.set("dev.flint.vault", "master", token)` |
| `fn get(service: str, account: str) -> str!` | `cfire::keychain::get(service, account)` | Retrieves the secret stored for `(service, account)` from the OS keychain. | `sec := try keychain.get("dev.flint.vault", "master")` |
| `fn delete_password(service: str, account: str) -> void!` | `cfire::keychain::delete_password(service, account)` | Removes the credential entry for `(service, account)` from the OS keychain. | `try keychain.delete_password("dev.flint.vault", "master")` |

### `core:log` (`log`) — Structured Leveled Logging

- **Auto-Link Flags:** `(header-only)`
- **Overview:** Leveled logging (`debug`, `info`, `warn`, `error`) with `Level` (`Debug`, `Info`, `Warn`, `Error`), `Format` (`Text`, `Json`), and structured key-value attributes.

**Practical Example:**
```cf
package main

import "core:log"

fn main() {
    log.set_level(log.Level::Debug)
    log.set_format(log.Format::Json) // Output format: Text or Json

    // Leveled structured logging with key-value attributes
    log.info("service worker started", "port", "8080", "cluster", "prod-us-east")
    log.warn("memory threshold reached", "usage_pct", "84", "threshold", "80")
    log.error("database pool exhausted", "active_conns", "100", "pool_size", "100")
}
```

**Types:**

| Type | Kind | Description |
|---|---|---|
| `log.Level` | enum | Minimum severity for `log.set_level`. Variants: `Debug`, `Info`, `Warn`, `Error`. |
| `log.Format` | enum | Output format for `log.set_format`. Variants: `Text`, `Json`. |

| `.cf` Signature | Lowered C++23 | Description | Example |
|---|---|---|---|
| `fn set_level(l: Level)` | `cfire::log::set_level(l)` | Sets the minimum logging severity threshold (`log.Level::Debug`, `Info`, `Warn`, `Error`). | `log.set_level(log.Level::Debug)` |
| `fn get_level() -> Level` | `cfire::log::get_level()` | Returns the current minimum logging severity threshold. | `lvl := log.get_level()` |
| `fn set_format(f: Format)` | `cfire::log::set_format(f)` | Sets the log output format (`log.Format::Text` or `log.Format::Json`). | `log.set_format(log.Format::Json)` |
| `fn get_format() -> Format` | `cfire::log::get_format()` | Returns the current log output format. | `fmt := log.get_format()` |
| `fn debug(msg: str, ...attrs: any)` | `cfire::log::debug(msg, ...)` | Logs a debug message with optional key-value attribute pairs. | `log.debug("cache miss", "key", "user:1")` |
| `fn info(msg: str, ...attrs: any)` | `cfire::log::info(msg, ...)` | Logs an informational message with optional key-value attribute pairs. | `log.info("server listening", "port", 8080)` |
| `fn warn(msg: str, ...attrs: any)` | `cfire::log::warn(msg, ...)` | Logs a warning message with optional key-value attribute pairs. | `log.warn("rate limit reached", "client", ip)` |
| `fn error(msg: str, ...attrs: any)` | `cfire::log::error(msg, ...)` | Logs an error message with optional key-value attribute pairs. | `log.error("database connection failed", "db", name)` |

### `core:math` (`math`) — Math Constants & Multi-Input Functions

- **Auto-Link Flags:** `(header-only)`
- **Overview:** Mathematical constants (`math.PI`, `math.E`, `math.TAU`, `math.SQRT_2`, `math.LN_2`, `math.LN_10`) and functions with no natural receiver (`atan2`, `hypot`, `gcd`, `lcm`). Single-value operations are methods on the number itself (`x.abs()`, `f.sqrt()`, `x.clamp(lo, hi)`, `a.checked_add(b)`); type limits and parsing are associated with the type (`i32.MAX`, `f64.NAN`, `i32.parse(s)`). See the builtin method reference.

**Practical Example:**
```cf
package main

import "core:math"

fn main() {
    bounded := (3.14159).clamp(0.0, 1.0)
    hypot := (3.0 * 3.0 + 4.0 * 4.0).sqrt()
    println("Clamped: {}, Hypot: {}", bounded, hypot)
}
```

| `.cf` Signature | Lowered C++23 | Description | Example |
|---|---|---|---|
| `const PI: f64` | `cfire::math::PI` | Ratio of a circle's circumference to its diameter (π). | `area := math.PI * r * r` |
| `const E: f64` | `cfire::math::E` | Euler's number (e). | `y := math.E.powi(2)` |
| `const TAU: f64` | `cfire::math::TAU` | Full turn in radians (2π). | `step := math.TAU / f64(n)` |
| `const SQRT_2: f64` | `cfire::math::SQRT_2` | Square root of 2. | `d := side * math.SQRT_2` |
| `const LN_2: f64` | `cfire::math::LN_2` | Natural logarithm of 2. | `bits := x.ln() / math.LN_2` |
| `const LN_10: f64` | `cfire::math::LN_10` | Natural logarithm of 10. | `d := x.ln() / math.LN_10` |
| `fn atan2(y: f64, x: f64) -> f64` | `cfire::math::atan2(y, x)` | Angle of the point `(x, y)` from the positive x-axis, in radians (`-π..=π`). | `heading := math.atan2(dy, dx)` |
| `fn hypot(x: f64, y: f64) -> f64` | `cfire::math::hypot(x, y)` | `sqrt(x*x + y*y)` without intermediate overflow or underflow. | `dist := math.hypot(dx, dy)` |
| `fn gcd(a: i64, b: i64) -> i64` | `cfire::math::gcd(a, b)` | Greatest common divisor (non-negative); `gcd(0, 0) == 0`. Panics for the minimum `i64`. | `g := math.gcd(12, 18)` |
| `fn lcm(a: i64, b: i64) -> i64` | `cfire::math::lcm(a, b)` | Least common multiple (non-negative); panics on overflow. | `l := math.lcm(4, 6)` |

### `core:net` (`net`) — POSIX BSD TCP Sockets & Loopback Networking

- **Auto-Link Flags:** `(POSIX sockets)`
- **Overview:** RAII TCP client, listener, and connected socketpair handles backed by BSD `AF_INET` / `AF_UNIX` sockets with non-blocking `poll` timeouts and strict `_bytes` / `_text` I/O symmetry.

**Practical Example:**
```cf
package main

import "core:net"

fn main() {
    // 1. Bind loopback TCP listener on an ephemeral port (0)
    mut ln := try net.listen(port: 0)
    port := ln.port()
    println("Listening on 127.0.0.1:{}", port)

    // 2. Connect client TCP socket
    mut client := try net.connect("127.0.0.1", port)
    _ = try client.write_text("PING\n")

    // 3. Accept incoming connection and read data
    mut conn := try ln.accept()
    msg := try conn.read_text(max_bytes: 64)
    println("Server received payload: {}", msg)
}
```

**Types:**

| Type | Kind | Description |
|---|---|---|
| `net.TcpListener` | handle | A listening TCP socket; `accept()` returns a `TcpConn` per client. |
| `net.TcpConn` | handle | A connected TCP stream, closed when it goes out of scope. |

| `.cf` Signature | Lowered C++23 | Description | Example |
|---|---|---|---|
| `fn listen(port: u16 = 0, host: str = "0.0.0.0") -> TcpListener!` | `cfire::net::listen(port, host)` | Binds a TCP listener on `host:port` (`0` picks an ephemeral port; the default host is every interface). | `mut ln := try net.listen(0)` |
| `fn (ln: TcpListener) port() -> u16` | `ln.port()` | Returns the bound TCP port. | `p := ln.port()` |
| `fn (mut ln: TcpListener) accept() -> TcpConn!` | `ln.accept()` | Accepts an incoming TCP connection with a safe poll timeout. | `mut conn := try ln.accept()` |
| `fn connect(host: str, port: u16) -> TcpConn!` | `cfire::net::connect(host, port)` | Connects a `TcpConn` to `host:port`. | `mut c := try net.connect("127.0.0.1", p)` |
| `fn socketpair() -> (TcpConn, TcpConn)!` | `cfire::net::socketpair()` | Creates a connected pair of full-duplex `TcpConn` stream sockets. | `(mut a, mut b) := try net.socketpair()` |
| `fn (c: TcpConn) remote_addr() -> str` | `c.remote_addr()` | The peer address of an accepted connection (`203.0.113.7`, `2001:db8::1`; IPv4-mapped IPv6 peers are reported as IPv4), or the host a client connection dialed. | `println("client {}", conn.remote_addr())` |
| `fn (c: TcpConn) fd() -> i32` | `c.fd()` | Returns the underlying socket file descriptor (`-1` if closed). | `fd := conn.fd()` |
| `fn (c: TcpConn) is_open() -> bool` | `c.is_open()` | Returns `true` if the socket connection is open. | `if conn.is_open() { ... }` |
| `fn (c: TcpConn) poll(timeout_ms: i64 = 0) -> bool` | `c.poll(timeout_ms)` | Polls the socket for readability up to `timeout_ms` milliseconds (`EINTR`-safe). | `if conn.poll(50) { ... }` |
| `fn (mut c: TcpConn) write_bytes(payload: []byte) -> usize!` | `c.write_bytes(payload)` | Writes all bytes in `payload` to the TCP/TLS connection, looping across partial sends and `EINTR`. | `_ = try c.write_bytes(raw)` |
| `fn (mut c: TcpConn) write_text(text: str) -> usize!` | `c.write_text(text)` | Writes a UTF-8 string to the TCP/TLS connection, looping across partial sends and `EINTR`. | `_ = try c.write_text("PING\n")` |
| `fn (mut c: TcpConn) read_into(mut buf: []byte) -> usize!` | `c.read_into(buf)` | Reads up to `buf.len()` bytes from the socket into `buf` (`EINTR`-safe). | `n := try conn.read_into(mut buf)` |
| `fn (mut c: TcpConn) read_bytes(max_bytes: usize = 4096) -> []byte!` | `c.read_bytes(max_bytes)` | Reads up to `max_bytes` from the socket into a new byte slice. | `pkt := try conn.read_bytes(256)` |
| `fn (mut c: TcpConn) read_exact(exact_bytes: usize) -> []byte!` | `c.read_exact(exact_bytes)` | Reads exactly `exact_bytes` from the socket or returns an error on premature EOF. | `hdr := try conn.read_exact(8)` |
| `fn (mut c: TcpConn) read_text(max_bytes: usize = 4096) -> str!` | `c.read_text(max_bytes)` | Reads up to `max_bytes` from the socket as a UTF-8 string. | `msg := try conn.read_text(64)` |
| `fn (mut c: TcpConn) read_bytes_until(delim: byte, max_bytes: usize = 65536) -> []byte!` | `c.read_bytes_until(delim, max_bytes)` | Reads bytes from the socket up to and including `delim` (or `max_bytes`). | `frame := try conn.read_bytes_until(byte(0))` |
| `fn (mut c: TcpConn) read_text_until(delim: str, max_bytes: usize = 65536) -> str!` | `c.read_text_until(delim, max_bytes)` | Reads text from the socket up to and including the first byte of `delim` (or `max_bytes`). | `line := try conn.read_text_until("\n")` |
| `fn (mut c: TcpConn) read_line(max_bytes: usize = 65536) -> str?` | `c.read_line(max_bytes)` | Reads a newline-delimited line up to `max_bytes` from the socket, or `none` on EOF. | `line := conn.read_line() or { return }` |
| `fn (mut c: TcpConn) set_timeout_ms(ms: i32) -> void` | `c.set_timeout_ms(ms)` | Sets socket read/write timeout in milliseconds (`0` for blocking). | `conn.set_timeout_ms(5000)` |
| `fn (mut c: TcpConn) close() -> void` | `c.close()` | Closes the TCP socket connection. | `conn.close()` |

### `core:net/http` (`http`) — HTTP/1.1 & HTTPS Client, Server & Router

- **Auto-Link Flags:** `(POSIX sockets + TLS)`
- **Overview:** RFC 9112 HTTP/1.1 & HTTPS client (`get`, `post`, `request`), wire parser (`parse_request`), response serializer (`Response`), HTTP/HTTPS server (`make(http.Server, port)`, `make(http.Server, port, tls: http.TlsConfig{...})`, per-connection `http.ServerLimits`), and request router (`make_router`) with integrated WebSocket route upgrade (`Router.ws`).

**Practical Example:**
```cf
package main

import "core:net/http"
import "core:json"

type HealthStatus struct {
    status: str `json:"status"`
    uptime: i64 `json:"uptime_sec"`
}

fn main() {
    // 1. HTTP GET client request with custom headers
    headers := { "User-Agent": "cfire-service/1.0", "Accept": "application/json" }
    resp := try http.get("http://127.0.0.1:8080/health", headers: headers)
    if resp.status_code == 200 {
        h := try json.parse[HealthStatus](resp.body)
        println("Service status: {}, uptime: {}s", h.status, h.uptime)
    }

    // 2. Build independent HTTP router with path parameters
    mut router := make(http.Router)
    router.get("/health", |req| {
        return http.Response.json(200, "{\"status\":\"healthy\",\"uptime_sec\":120}")
    })
    router.get("/users/:id", |req| {
        return http.Response.text(200, "User: " + req.param("id"))
    })

    // 3. Serve requests using the router
    mut server := try make(http.Server, port: 8080)
    server.serve_in_background(router)
}
```

**Types:**

| Type | Kind | Description |
|---|---|---|
| `http.Request` | struct | An HTTP request, as a handler receives it or as you build one to send. Fields: `remote_addr: str`, `method: str`, `path: str`, `raw_query: str`, `query_params: map[str]str`, `headers: map[str]str`, `params: map[str]str`, `body: str`. |
| `http.Response` | struct | An HTTP response; build one with `http.Response.text`, `.json`, `.html` or `.redirect`. Fields: `status: i32`, `status_code: i32`, `status_text: str`, `headers: map[str]str`, `body: str`. |
| `http.Router` | handle | Maps method and path patterns to handlers; registering a route returns the router, so calls chain. |
| `http.HttpServer` | handle | An HTTP server bound to a port (`make(http.Server, port)`); `serve_in_background` runs it and `stop` ends it. |
| `http.Server` | handle | The name `make(http.Server, port)` constructs; the same type as `http.HttpServer`. |
| `http.SseEvent` | struct | One server-sent event. Fields: `event: str`, `data: str`, `id: str`. |
| `http.ServerLimits` | struct | Per-connection limits for `http.Server` (Q22, CF-192); pass to `server.set_limits(...)`. `workers` is how many handlers run at once; `workers = 0` keeps the default (twice the core count, at least 4). An idle keep-alive connection closes after `keepalive_timeout_ms` (0 turns keep-alive off) or after `max_requests_per_conn` requests. Each WebSocket session runs on its own thread, up to `max_websockets`. Over a cap a client gets 503. Fields: `header_timeout_ms: i32`, `body_timeout_ms: i32`, `max_body_bytes: usize`, `workers: usize`, `max_pending: usize`, `keepalive_timeout_ms: i32`, `max_requests_per_conn: usize`, `max_websockets: usize`, `max_connections: usize`. |
| `http.TlsConfig` | struct | Server certificate for `make(http.Server, port, tls: ...)` (Q31): set `cert_file`/`key_file`, or `cert_pem`/`key_pem` for in-memory PEM. Fields: `cert_file: str`, `key_file: str`, `cert_pem: str`, `key_pem: str`. |

| `.cf` Signature | Lowered C++23 | Description | Example |
|---|---|---|---|
| `fn get(url: str, headers: map[str]str = {}, verify_peer: bool = true) -> Response!` | `cfire::http::get(url, headers, verify_peer)` | Executes an HTTP or HTTPS GET request against a target URL. | `resp := try http.get("https://127.0.0.1:8443/health", {}, false)` |
| `fn post(url: str, body: str, content_type: str = "application/json", headers: map[str]str = {}, verify_peer: bool = true) -> Response!` | `cfire::http::post(url, body, content_type, headers, verify_peer)` | Executes an HTTP or HTTPS POST request with payload body. | `resp := try http.post("http://127.0.0.1:8080/items", "{\"id\": 1}")` |
| `fn request(method: str, url: str, body: str = "", content_type: str = "", headers: map[str]str = {}, verify_peer: bool = true, ca_file: str = "") -> Response!` | `cfire::http::request(method, url, body, content_type, headers, verify_peer, ca_file)` | Executes an arbitrary HTTP or HTTPS request (PUT, DELETE, PATCH, etc.). | `resp := try http.request("DELETE", "http://127.0.0.1:8080/items/1")` |
| `fn post_sse(url: str, body: str, headers: map[str]str = {}, on_event: fn(SseEvent) -> bool, verify_peer: bool = true) -> void!` | `cfire::http::post_sse(url, body, headers, on_event, verify_peer)` | Streams an HTTP/HTTPS POST response using Server-Sent Events (`text/event-stream`) and chunked transfer encoding, invoking `on_event` for each event. | `try http.post_sse(url, payload, headers, |ev| { print("{}", ev.data); return true })` |
| `fn make(port: u16 = 0, tls: TlsConfig = {}) -> HttpServer!` | `cfire::http::make_server(port, tls)` | `make(http.Server, port)` creates an HTTP server listening on `port` (`0` picks an ephemeral port); `make(http.Server, port, tls: http.TlsConfig{cert_file: c, key_file: k})` (or `cert_pem` / `key_pem`) serves HTTPS. | `mut srv := try make(http.Server, 8443, tls: http.TlsConfig{cert_file: "cert.pem", key_file: "key.pem"})` |
| `fn make() -> Router` | `cfire::http::make_router()` | `make(http.Router)` creates an independent HTTP & WebSocket request router supporting path parameters (`:id`) and wildcards (`*path`). | `mut router := make(http.Router)` |
| `fn parse_request(raw: str) -> Request!` | `cfire::http::parse_request(raw)` | Parses raw HTTP/1.1 wire text into a structured Request model. | `req := try http.parse_request(raw_text)` |
| `fn Response.text(code: i32, payload: str) -> Response` | `cfire::http::Response::text(code, payload)` | Creates an HTTP Response with text/plain content-type and standard status reason. | `return http.Response.text(200, "OK")` |
| `fn Response.json(code: i32, json_str: str) -> Response` | `cfire::http::Response::json(code, json_str)` | Creates an HTTP Response with application/json content-type. | `return http.Response.json(200, "{\"status\":\"ok\"}")` |
| `fn Response.html(code: i32, html: str) -> Response` | `cfire::http::Response::html(code, html)` | Creates an HTTP Response with text/html content-type. | `return http.Response.html(200, "<h1>Hello</h1>")` |
| `fn Response.redirect(location: str, code: i32 = 302) -> Response` | `cfire::http::Response::redirect(location, code)` | Creates an HTTP redirect Response setting Location header. | `return http.Response.redirect("/login", 302)` |
| `fn (mut r: Router) get(pattern: str, handler: fn(Request) -> Response) -> Router` | `r.get(pattern, handler)` | Registers an HTTP GET route with optional path parameters (`:id`) or wildcards (`*path`). | `r.get("/users/:id", |req| http.Response.text(200, req.param("id")))` |
| `fn (mut r: Router) post(pattern: str, handler: fn(Request) -> Response) -> Router` | `r.post(pattern, handler)` | Registers an HTTP POST route handler. | `r.post("/items", |req| http.Response.json(201, "{}"))` |
| `fn (mut r: Router) ws(pattern: str, handler: fn(Request, mut websocket.Conn) -> void) -> Router` | `r.ws(pattern, handler)` | Registers a WebSocket route that automatically upgrades matching HTTP requests and invokes `handler(req, mut ws)`. | `r.ws("/chat/:room", |req, mut ws| { ... })` |
| `fn (r: Router) dispatch(req: Request) -> Response` | `r.dispatch(req)` | Dispatches an HTTP request through the router in-memory without open network sockets. | `res := router.dispatch(test_req)` |
| `fn (mut r: Router) use(mw: fn(mut Request) -> Response?) -> Router` | `r.use(mw)` | Registers pre-routing middleware that can short-circuit or validate HTTP and WebSocket upgrade requests. | `r.use(|req| { return None })` |
| `type ServerLimits struct { header_timeout_ms: i32 = 10000, body_timeout_ms: i32 = 30000, max_body_bytes: usize = 10485760, workers: usize = 0, max_pending: usize = 128, keepalive_timeout_ms: i32 = 5000, max_requests_per_conn: usize = 1000, max_websockets: usize = 1024, max_connections: usize = 10000 }` | `cfire::http::ServerLimits` | Per-connection server limits (Q22, CF-180, CF-192). Omitted fields keep their defaults; `workers = 0` keeps the default (twice the core count, at least 4). `keepalive_timeout_ms = 0` turns keep-alive off. | `srv.set_limits(http.ServerLimits{max_body_bytes: 1048576, header_timeout_ms: 5000, keepalive_timeout_ms: 2000})` |
| `type TlsConfig struct { cert_file: str, key_file: str, cert_pem: str, key_pem: str }` | `cfire::http::TlsConfig` | Server certificate for `make(http.Server, port, tls: ...)` (Q31): set `cert_file` / `key_file`, or `cert_pem` / `key_pem` for in-memory PEM. | `tls: http.TlsConfig{cert_pem: pair.cert_pem, key_pem: pair.key_pem}` |
| `fn (mut s: HttpServer) set_limits(limits: ServerLimits)` | `s.set_limits(limits)` | Replaces the per-connection limits (Q22, CF-192): `http.ServerLimits{header_timeout_ms, body_timeout_ms, max_body_bytes, workers, max_pending, keepalive_timeout_ms, max_requests_per_conn, max_websockets, max_connections}`; omitted fields keep their defaults. | `srv.set_limits(http.ServerLimits{max_body_bytes: 1048576})` |
| `fn (mut s: HttpServer) enable_tls_files(cert_file: str, key_file: str) -> void!` | `s.enable_tls_files(cert_file, key_file)` | Enables TLS on the server using certificate and private key PEM files. | `try srv.enable_tls_files("cert.pem", "key.pem")` |
| `fn (mut s: HttpServer) enable_tls_pem(cert_pem: str, key_pem: str) -> void!` | `s.enable_tls_pem(cert_pem, key_pem)` | Enables TLS on the server using in-memory certificate and private key PEM strings. | `try srv.enable_tls_pem(pair.cert_pem, pair.key_pem)` |
| `fn (mut s: HttpServer) serve_in_background(handler: fn(Request) -> Response | Router) -> void` | `s.serve_in_background(handler)` | Starts a non-blocking background accept loop serving HTTP and WebSocket upgrade requests. | `server.serve_in_background(router)` |
| `fn (s: HttpServer) port() -> u16` | `s.port()` | Returns the port the server is listening on (useful after binding port 0). | `port := srv.port()` |
| `fn (mut s: HttpServer) stop() -> void` | `s.stop()` | Gracefully closes the listening socket and stops worker threads. | `server.stop()` |
| `fn (req: Request) param(key: str, fallback: str = "") -> str` | `req.param(key, fallback)` | Retrieves a named route path parameter extracted by the router. | `id := req.param("id")` |
| `fn (req: Request) query(key: str, fallback: str = "") -> str` | `req.query(key, fallback)` | Retrieves a decoded URL query parameter. | `page := req.query("page", "1")` |
| `fn (req: Request) header(key: str, fallback: str = "") -> str` | `req.header(key, fallback)` | Performs a case-insensitive header lookup. | `auth := req.header("Authorization")` |

### `core:net/tls` (`tls`) — TLS 1.2 / 1.3 & X.509 Certificate Engine

- **Auto-Link Flags:** `(OpenSSL dynamic bridge)`
- **Overview:** Zero-build-flag TLS 1.2/1.3 client/server encryption, SNI hostname negotiation, X.509 peer certificate verification, PEM certificate loading, and in-memory self-signed certificate generation.

**Practical Example:**
```cf
package main

import "core:net/tls"
import "core:net/http"

fn main() {
    // 1. Generate an in-memory RSA-2048 X.509 self-signed certificate pair
    pair := try tls.generate_self_signed_pem("localhost", 365)

    // 2. Start an HTTPS server backed by the in-memory certificate
    mut router := make(http.Router)
    router.get("/secure", |req| {
        return http.Response.text(200, "TLS 1.3 Secure OK")
    })
    mut srv := try make(http.Server, 0, tls: http.TlsConfig{cert_pem: pair.cert_pem, key_pem: pair.key_pem})
    srv.serve_in_background(router)
    defer srv.stop()
}
```

**Types:**

| Type | Kind | Description |
|---|---|---|
| `tls.ServerContext` | handle | A TLS server configuration (certificate and key) that wraps accepted connections. |
| `tls.CertKeyPair` | struct | A certificate and its private key, in PEM form. Fields: `cert_pem: str`, `key_pem: str`. |

| `.cf` Signature | Lowered C++23 | Description | Example |
|---|---|---|---|
| `fn available() -> bool` | `cfire::tls::available()` | Returns `true` if the OpenSSL (`libssl` / `libcrypto`) runtime bridge is available. | `if tls.available() { ... }` |
| `fn generate_self_signed_pem(common_name: str = "localhost", days: i32 = 365) -> CertKeyPair!` | `cfire::tls::generate_self_signed_pem(common_name, days)` | Generates an in-memory RSA-2048 X.509v3 self-signed certificate and private key in PEM format. | `pair := try tls.generate_self_signed_pem("localhost")` |
| `fn wrap_client(mut conn: TcpConn, server_name: str = "", verify_peer: bool = true, ca_file: str = "") -> void!` | `cfire::tls::wrap_client(conn, server_name, verify_peer, ca_file)` | Upgrades an open `TcpConn` to a client TLS session with SNI and optional X.509 certificate verification. | `try tls.wrap_client(mut conn, "example.com", true)` |
| `fn connect(host: str, port: u16 = 443, verify_peer: bool = true, ca_file: str = "") -> TcpConn!` | `cfire::tls::connect(host, port, verify_peer, ca_file)` | Connects a TCP socket to `host:port` and completes a TLS client handshake. | `mut c := try tls.connect("example.com", 443)` |
| `fn ServerContext.from_files(cert_file: str, key_file: str) -> ServerContext!` | `cfire::tls::ServerContext::from_files(cert_file, key_file)` | Creates a TLS server context from X.509 certificate chain and private key PEM files on disk. | `ctx := try tls.ServerContext.from_files("cert.pem", "key.pem")` |
| `fn ServerContext.from_pem(cert_pem: str, key_pem: str) -> ServerContext!` | `cfire::tls::ServerContext::from_pem(cert_pem, key_pem)` | Creates a TLS server context directly from in-memory certificate and private key PEM strings. | `ctx := try tls.ServerContext.from_pem(pair.cert_pem, pair.key_pem)` |
| `fn (ctx: ServerContext) wrap_server(mut conn: TcpConn) -> void!` | `ctx.wrap_server(conn)` | Performs a server-side TLS handshake (`SSL_accept`) on an accepted `TcpConn`. | `try ctx.wrap_server(mut conn)` |

### `core:net/url` (`url`) — RFC 3986 URL Parsing & URL Encoding

- **Auto-Link Flags:** `(header-only)`
- **Overview:** RFC 3986 URL parsing with component extraction (scheme, host, port, path, query, fragment), form-urlencoded query parameter maps, and percent encoding/decoding.

**Practical Example:**
```cf
package main

import "core:net/url"

fn main() {
    raw := "https://api.domain.com:8443/v1/search?q=systems+c%2B%2B&limit=25#results"
    u := try url.parse(raw)
    println("Scheme: {}, Host: {}, Port: {}, Path: {}", u.scheme, u.host, u.port, u.path)
    println("Query parameter 'q': {}", u.query["q"])

    encoded := url.encode("raw query with spaces & symbols!")
    println("Percent-encoded: {}", encoded)
}
```

**Types:**

| Type | Kind | Description |
|---|---|---|
| `url.Url` | struct | A parsed URL, from `url.parse`. Fields: `scheme: str`, `host: str`, `port: u16`, `path: str`, `query: str`, `fragment: str`, `query_params: map[str]str`. |

| `.cf` Signature | Lowered C++23 | Description | Example |
|---|---|---|---|
| `fn parse(raw: str) -> Url!` | `cfire::url::parse(raw)` | Parses an RFC 3986 URL string into component fields and query parameters (`https` and `wss` default to port 443). | `u := try url.parse("wss://stream.example.com/ws?q=1")` |
| `fn encode(s: str) -> str` | `cfire::url::encode(s)` | Percent-encodes a string for safe use in URL path or query strings. | `enc := url.encode("hello world!")` |
| `fn decode(s: str) -> str` | `cfire::url::decode(s)` | Decodes a percent-encoded URL string. | `dec := url.decode(enc)` |

### `core:net/websocket` (`websocket`) — RFC 6455 High-Level WebSockets & WSS Client & Server

- **Auto-Link Flags:** `(POSIX sockets + TLS)`
- **Overview:** RFC 6455 full-duplex `ws://` and `wss://` WebSocket client (`connect`, with `verify_peer: false` for self-signed certificates), standalone server (`make(websocket.Server, port)`, `tls: websocket.TlsConfig{...}` for `wss://`), and connection upgrade handler (`upgrade`) with automated framing, client masking, and Ping/Pong management.

**Practical Example:**
```cf
package main

import "core:net/websocket"

fn main() {
    // 1. Start a standalone WebSocket server on an ephemeral port
    mut srv := try make(websocket.Server, 0)
    srv.serve_in_background(|mut ws: websocket.Conn| {
        msg := ws.recv_text() or ""
        _ = ws.send_text("ECHO: " + msg)
    })
    defer srv.stop()

    // 2. Connect full-duplex RFC 6455 WebSocket client
    mut ws := try websocket.connect("ws://127.0.0.1:" + str(srv.port()) + "/stream")
    defer ws.close()

    try ws.send_text("SUBSCRIBE BTC-USD")
    frame := try ws.recv_text()
    println("Received WebSocket stream frame: {}", frame)
}
```

**Types:**

| Type | Kind | Description |
|---|---|---|
| `websocket.Conn` | handle | An open WebSocket connection; messages are `str` (`recv_text`) or `[]byte` (`recv_binary`). |
| `websocket.Server` | handle | A WebSocket server bound to a port. |
| `websocket.TlsConfig` | struct | Server certificate for `make(websocket.Server, port, tls: ...)` (Q31); the same type as `http.TlsConfig`. Fields: `cert_file: str`, `key_file: str`, `cert_pem: str`, `key_pem: str`. |

| `.cf` Signature | Lowered C++23 | Description | Example |
|---|---|---|---|
| `fn connect(url: str, verify_peer: bool = true) -> Conn!` | `cfire::websocket::connect(url, verify_peer)` | Establishes a `ws://` or `wss://` WebSocket connection and completes the RFC 6455 handshake. | `mut ws := try websocket.connect("ws://127.0.0.1:8080/ws")` |
| `type TlsConfig struct { cert_file: str, key_file: str, cert_pem: str, key_pem: str }` | `cfire::http::TlsConfig` | Server certificate for `make(websocket.Server, port, tls: ...)` (Q31); the same type as `http.TlsConfig`. | `tls: websocket.TlsConfig{cert_file: "cert.pem", key_file: "key.pem"}` |
| `fn make(port: u16 = 0, tls: TlsConfig = {}) -> Server!` | `cfire::websocket::make_server(port, tls)` | `make(websocket.Server, port)` creates a standalone WebSocket server listening on `port` (`0` picks an ephemeral port); add `tls: websocket.TlsConfig{...}` for `wss://`. | `mut srv := try make(websocket.Server, 9001)` |
| `fn upgrade(req: Request, mut conn: TcpConn) -> Conn!` | `cfire::websocket::upgrade(req, conn)` | Upgrades an incoming HTTP or HTTPS connection to a WebSocket connection. | `mut ws := try websocket.upgrade(req, mut conn)` |
| `fn pair() -> (Conn, Conn)!` | `cfire::websocket::pair()` | Creates an in-process full-duplex WebSocket client/server loopback pair. | `mut (client, server) := try websocket.pair()` |
| `fn (mut ws: Conn) send_text(msg: str) -> void!` | `ws.send_text(msg)` | Sends a UTF-8 text frame to the peer (masked if client). | `try ws.send_text("hello world")` |
| `fn (mut ws: Conn) send_binary(data: []byte) -> void!` | `ws.send_binary(data)` | Sends a binary data frame to the peer (masked if client). | `try ws.send_binary(payload)` |
| `fn (mut ws: Conn) recv_text() -> str!` | `ws.recv_text()` | Receives the next text frame from the peer, automatically responding to Ping frames with Pong. | `msg := try ws.recv_text()` |
| `fn (mut ws: Conn) recv_binary() -> []byte!` | `ws.recv_binary()` | Receives the next binary frame from the peer. | `bytes := try ws.recv_binary()` |
| `fn (mut ws: Conn) ping(payload: str = "") -> void!` | `ws.ping(payload)` | Sends a Ping control frame to check peer liveness. | `try ws.ping("heartbeat")` |
| `fn (mut ws: Conn) set_timeout_ms(ms: i32) -> void` | `ws.set_timeout_ms(ms)` | Sets read/write socket timeout in milliseconds (`0` for blocking). | `ws.set_timeout_ms(10000)` |
| `fn (mut ws: Conn) close(code: u16 = 1000, reason: str = "")` | `ws.close(code, reason)` | Initiates a clean RFC 6455 closure handshake and closes the socket. | `ws.close()` |
| `fn (ws: Conn) is_open() -> bool` | `ws.is_open()` | Returns true if the WebSocket connection is open. | `if ws.is_open() { ... }` |
| `fn (mut s: Server) accept() -> Conn!` | `s.accept()` | Accepts one incoming connection, completes the TLS and RFC 6455 upgrade handshake, and returns a `Conn`. | `mut ws := try srv.accept()` |
| `fn (mut s: Server) serve_once(handler: fn(mut Conn) -> void) -> void` | `s.serve_once(handler)` | Accepts and serves a single WebSocket connection with `handler`. | `srv.serve_once(|mut ws| { ... })` |
| `fn (mut s: Server) listen_and_serve(handler: fn(mut Conn) -> void) -> void` | `s.listen_and_serve(handler)` | Runs a blocking accept loop upgrading and serving WebSocket connections. | `srv.listen_and_serve(|mut ws| { ... })` |
| `fn (mut s: Server) serve_in_background(handler: fn(mut Conn) -> void) -> void` | `s.serve_in_background(handler)` | Starts a background thread accepting and serving WebSocket connections. | `srv.serve_in_background(|mut ws| { ... })` |
| `fn (mut s: Server) stop() -> void` | `s.stop()` | Stops the background WebSocket server thread. | `srv.stop()` |

### `core:os` (`os`) — Environment, Standard Streams, Terminal, Pipes & Subprocesses

- **Auto-Link Flags:** `(header-only)`
- **Overview:** Process environment inspection, working directory queries, standard stream handles (`os.stdin()`, `os.stdout()`, `os.stderr()`), RAII pipes (`os.pipe()`), raw terminal control (`is_tty`, `term_size`, `make(os.RawTerminal)`, `on_signal`), and subprocess execution (`command`, `exec_output`).

**Practical Example:**
```cf
package main

import "core:os"

fn main() {
    home := os.getenv_opt("HOME") or "/tmp"
    println("Environment $HOME: {}", home)

    // Execute external process without shell interpretation and capture stdout/exit code
    output := try os.command("git", ["rev-parse", "--short", "HEAD"])
    println("Current Git commit: {} (exit code {})", output.stdout, output.exit_code)
}
```

**Types:**

| Type | Kind | Description |
|---|---|---|
| `os.ExecResult` | struct | The outcome of a finished subprocess (`os.command`). Fields: `exit_code: i32`, `stdout: str`, `stderr: str`. |
| `os.TermSize` | struct | A terminal's size in character cells. Fields: `cols: i32`, `rows: i32`. |
| `os.RawTerminal` | handle | The terminal in raw mode (no echo, no line buffering); restored when it goes out of scope. |

| `.cf` Signature | Lowered C++23 | Description | Example |
|---|---|---|---|
| `fn args() -> []str` | `cfire::os::args()` | Returns the command-line arguments passed to the current process. | `argv := os.args()` |
| `fn exit(code: i32)` | `cfire::os::exit(code)` | Terminates the current process immediately with status `code`. | `os.exit(1)` |
| `fn getenv(key: str, fallback: str = "") -> str` | `cfire::os::getenv(key, fallback)` | Reads environment variable `key`, returning `fallback` if unset. | `mode := os.getenv("ENV", "dev")` |
| `fn getenv_opt(key: str) -> str?` | `cfire::os::getenv_opt(key)` | Reads environment variable `key`, returning `none` if unset. | `val := os.getenv_opt("API_KEY") or { return }` |
| `fn setenv(key: str, value: str)` | `cfire::os::setenv(key, value)` | Sets environment variable `key` to `value` in the current process. | `os.setenv("MODE", "prod")` |
| `fn cwd() -> str` | `cfire::os::cwd()` | Returns the current working directory path. | `dir := os.cwd()` |
| `fn getpid() -> i64` | `cfire::os::getpid()` | Returns the process ID (PID) of the current process. | `pid := os.getpid()` |
| `fn stdin() -> File` | `cfire::os::stdin_file()` | Returns a non-owning `fs.File` stream handle wrapping standard input (FD 0). | `line := os.stdin().read_line() or { return }` |
| `fn stdout() -> File` | `cfire::os::stdout_file()` | Returns a non-owning `fs.File` stream handle wrapping standard output (FD 1). | `_ = try os.stdout().write_text("ok\n")` |
| `fn stderr() -> File` | `cfire::os::stderr_file()` | Returns a non-owning `fs.File` stream handle wrapping standard error (FD 2). | `_ = try os.stderr().write_text("warn\n")` |
| `fn pipe() -> (File, File)!` | `cfire::os::pipe()` | Creates an OS pipe returning an owning `(reader, writer)` tuple of `fs.File` handles with `FD_CLOEXEC` enabled. | `(mut r, mut w) := try os.pipe()` |
| `fn is_tty() -> bool` | `cfire::os::is_tty()` | Returns `true` if standard input refers to an interactive terminal (or call `.is_tty()` on any `fs.File`). | `if os.is_tty() { ... }` |
| `fn term_size() -> TermSize` | `cfire::os::term_size()` | Queries terminal dimensions (`cols`, `rows`) via `TIOCGWINSZ`, defaulting to 80x24 if not a TTY. | `sz := os.term_size()` |
| `fn make() -> RawTerminal!` | `cfire::os::make_raw()` | `make(os.RawTerminal)` puts standard input into raw terminal mode (`cfmakeraw`) and returns an RAII guard that restores `termios` when it goes out of scope. | `raw := try make(os.RawTerminal)` |
| `fn on_signal(sig: i32, handler: fn(i32))` | `cfire::os::on_signal(sig, handler)` | Registers a callback handler for POSIX signal `sig` (e.g., `SIGINT`, `SIGTERM`, `SIGWINCH`). | `os.on_signal(28, |sig| { println("resized: {}", sig) })` |
| `fn exec_output(cmd: str) -> str!` | `cfire::os::exec_output(cmd)` | Executes a shell command via `popen` and returns its captured stdout. | `out := try os.exec_output("uname -s")` |
| `fn exec(cmd: str) -> str!` | `cfire::os::exec_output(cmd)` | Executes a shell command via `popen` and returns its captured stdout. | `out := try os.exec("uname -s")` |
| `fn command(binary: str, args: []str) -> ExecResult!` | `cfire::os::command(binary, args)` | Spawns a subprocess with explicit argument list via `posix_spawnp` (zero shell injection risk) and captures stdout/stderr. | `res := try os.command("git", ["status"])` |

### `core:rand` (`rand`) — Thread-Local MT19937_64 & CSPRNG Generator

- **Auto-Link Flags:** `(header-only)`
- **Overview:** Fast, thread-safe pseudo-random and OS-entropy CSPRNG random generation.

**Practical Example:**
```cf
package main

import "core:rand"

fn main() {
    uid := rand.u64()
    dice_roll := rand.range_i64(1, 6)
    token_bytes := rand.bytes(16)
    println("ID: {}, Roll: {}, Entropy bytes: {}", uid, dice_roll, token_bytes.len())
}
```

| `.cf` Signature | Lowered C++23 | Description | Example |
|---|---|---|---|
| `fn u64() -> u64` | `cfire::rand::u64()` | Generates a random 64-bit unsigned integer. | `id := rand.u64()` |
| `fn range_i64(min_incl: i64, max_incl: i64) -> i64` | `cfire::rand::range_i64(min_incl, max_incl)` | Generates a uniform integer in `[min_incl, max_incl]`. | `n := rand.range_i64(1, 10)` |
| `fn f64_unit() -> f64` | `cfire::rand::f64_unit()` | Generates a uniform `f64` in `[0.0, 1.0)`. | `p := rand.f64_unit()` |
| `fn bytes(n: usize) -> []byte` | `cfire::rand::bytes(n)` | Generates `n` cryptographically secure random bytes from OS entropy. | `buf := rand.bytes(32)` |

### `core:regex` (`regex`) — ECMAScript Regular Expressions

- **Auto-Link Flags:** `(header-only)`
- **Overview:** Pattern matching, group extraction, and text substitution backed by C++23 `<regex>`.

**Practical Example:**
```cf
package main

import "core:regex"

fn main() {
    email_pattern := `^[a-zA-Z0-9_\.-]+@[a-zA-Z0-9\.-]+\.[a-z]{2,6}$`
    re := try regex.compile(email_pattern)

    email := "architect@cfire.memo.engineer"
    if try re.is_match(email) {
        println("Validated email: {}", email)
    }

    // Global text replacement
    cleaned := try regex.replace_all(`\s+`, "multiple    whitespace     characters", " ")
    println("Cleaned text: {}", cleaned)
}
```

**Types:**

| Type | Kind | Description |
|---|---|---|
| `regex.Regex` | handle | A compiled regular expression, from `regex.compile`. |

| `.cf` Signature | Lowered C++23 | Description | Example |
|---|---|---|---|
| `fn compile(pattern: str) -> Regex!` | `cfire::regex::compile(pattern)` | Compiles an ECMAScript regex pattern into a reusable Regex object. | `re := try regex.compile("\\d+")` |
| `fn is_match(pattern: str, text: str) -> bool!` | `cfire::regex::is_match(pattern, text)` | Tests whether a regex pattern matches text. | `ok := try regex.is_match("^[a-z]+$", s)` |
| `fn find(pattern: str, text: str) -> str?!` | `cfire::regex::find(pattern, text)` | Returns the first match of `pattern` in `text`, or `none`; an invalid pattern is an error. | `word := try regex.find("[a-z]+", s) or { return }` |
| `fn find_all(pattern: str, text: str) -> []str!` | `cfire::regex::find_all(pattern, text)` | Returns every non-overlapping match of `pattern` in `text`. | `nums := try regex.find_all("[0-9]+", line)` |
| `fn replace_all(pattern: str, text: str, repl: str) -> str!` | `cfire::regex::replace_all(pattern, text, repl)` | Replaces all occurrences matching pattern with repl. | `res := try regex.replace_all("\s+", s, " ")` |
| `fn (r: Regex) is_match(text: str) -> bool` | `r.is_match(text)` | Tests whether the compiled pattern matches anywhere in `text`. | `if re.is_match(line) { ... }` |
| `fn (r: Regex) find(text: str) -> str?` | `r.find(text)` | Returns the first match in `text`, or `none`. | `m := re.find(line) or { continue }` |
| `fn (r: Regex) find_all(text: str) -> []str` | `r.find_all(text)` | Returns every non-overlapping match in `text`. | `nums := re.find_all(line)` |
| `fn (r: Regex) replace_all(text: str, repl: str) -> str` | `r.replace_all(text, repl)` | Replaces every match in `text` with `repl` (`$1` refers to a group). | `clean := re.replace_all(s, " ")` |
| `fn (r: Regex) pattern() -> str` | `r.pattern()` | Returns the source pattern. | `println(re.pattern())` |

### `core:sync` (`sync`) — Thread-Safe Typed Channels & Mutexes

- **Auto-Link Flags:** `(header-only)`
- **Overview:** Go-style typed MPSC/MPMC channels (`Channel[T]`) with non-blocking and timeout receives, plus movable mutual exclusion locks (`Mutex`).

**Practical Example:**
```cf
package main

import "core:sync"

fn main() {
    // Typed buffered message channel
    ch := make(sync.Channel[i32], capacity: 10)

    async.scope(workers: 2) |scope| {
        // Producer task
        scope.spawn || {
            for i in [10, 20, 30] {
                ch.send(i)
            }
            ch.close()
        }

        // Consumer task
        scope.spawn || {
            for {
                item := ch.recv() or break
                println("Channel consumer received: {}", item)
            }
        }
    }
}
```

**Types:**

| Type | Kind | Description |
|---|---|---|
| `sync.Mutex[T]` | handle | A value of type `T` behind a lock; `lock()` returns a guard, or use `with_lock`. |
| `sync.MutexGuard[T]` | handle | Holds a `Mutex[T]` locked and gives access to its value; the lock is released when the guard goes out of scope. |
| `sync.Channel[T]` | handle | A thread-safe queue between tasks: `send`, `recv`, `try_recv`; `close()` wakes every waiting receiver. |
| `sync.Atomic[T]` | handle | A value shared between threads and read or changed atomically: `load`, `store`, `add`, `sub`. |
| `sync.Context` | handle | Carries cancellation and a deadline across tasks; see `core:sync/context`. |

| `.cf` Signature | Lowered C++23 | Description | Example |
|---|---|---|---|
| `fn make[T](capacity: usize = 0) -> Channel[T]` | `cfire::sync::make_channel<T>(capacity)` | `make(sync.Channel[T], capacity)` creates a shared, thread-safe channel of element type `T` (`0` = unbuffered). | `ch := make(sync.Channel[i32], 8)` |
| `fn make[T](initial: T) -> Mutex[T]` | `cfire::sync::make_mutex<T>(initial)` | `make(sync.Mutex[T], initial)` creates a mutex protecting a value of type `T`; `make(sync.Mutex[void])` is a pure lock. | `mu := make(sync.Mutex[i64], 0)` |
| `fn make[T](initial: T) -> Atomic[T]` | `cfire::sync::make_atomic<T>(initial)` | `make(sync.Atomic[T], initial)` creates an atomic variable holding `initial`. | `hits := make(sync.Atomic[i64], 0)` |
| `fn (ch: Channel[T]) send(val: T)` | `ch.send(val)` | Sends a value into the channel and wakes one waiting receiver. | `ch.send(42)` |
| `fn (ch: Channel[T]) recv() -> T!` | `ch.recv()` | Blocks until a value is available or returns an error if the channel is closed and empty. | `v := ch.recv() or 0` |
| `fn (ch: Channel[T]) try_recv() -> T?` | `ch.try_recv()` | Attempts a non-blocking receive from the channel, returning `none` immediately if the queue is empty. | `v := ch.try_recv() or { return }` |
| `fn (ch: Channel[T]) recv_timeout(timeout_ms: i64) -> T?` | `ch.recv_timeout(timeout_ms)` | Waits up to `timeout_ms` milliseconds for a value from the channel, returning `none` on timeout or closed empty channel. | `v := ch.recv_timeout(50) or { return }` |
| `fn (ch: Channel[T]) recv_ctx(ctx: Context) -> T!` | `ch.recv_ctx(ctx)` | Receives from the channel or aborts immediately with an error if `ctx` is cancelled. | `v := try ch.recv_ctx(ctx)` |
| `fn (ch: Channel[T]) len() -> usize` | `ch.len()` | Returns the number of buffered items currently queued in the channel. | `n := ch.len()` |
| `fn (mut ch: Channel[T]) close()` | `ch.close()` | Closes the channel and wakes all waiting receivers. | `ch.close()` |
| `type Mutex[T] struct` | `cfire::sync::MutexT<T>` | Mutual exclusion (CF-154): `Mutex[T]` protects a value of type `T`; `Mutex[void]` is a pure lock. Copies share the same lock. | `mu := make(sync.Mutex[i64], 0)` |
| `fn (m: Mutex[T]) lock() -> MutexGuard[T]` | `m.lock()` | `Mutex[T]`: blocks, then returns a guard that gives access to the value and unlocks when it goes out of scope. `Mutex[void]`: acquires the lock; pair it with `unlock()`. | `mut g := mu.lock()` |
| `fn (m: Mutex[void]) unlock()` | `m.unlock()` | Releases a `Mutex[void]` acquired with `lock()`. | `lk.unlock()` |
| `fn (m: Mutex[T]) try_lock() -> MutexGuard[T]?` | `m.try_lock()` | Non-blocking `lock()`: `none` (`false` for `Mutex[void]`) when another task holds the lock. | `if g := mu.try_lock() { ... }` |
| `fn (m: Mutex[T]) with_lock(f: fn(mut T) -> R) -> R` | `m.with_lock(f)` | Runs `f` with the protected value while holding the lock (`fn() -> R` for `Mutex[void]`) and unlocks when it returns. | `n := mu.with_lock(|mut v| { v += 1; return v })` |
| `fn (m: Mutex[T]) get() -> T` | `m.get()` | Copy of the protected value, read under the lock. | `total := mu.get()` |
| `fn (m: Mutex[T]) set(v: T)` | `m.set(v)` | Replaces the protected value under the lock. | `mu.set(0)` |
| `type Atomic[T] struct` | `cfire::sync::Atomic<T>` | Atomic variable. | `hits := make(sync.Atomic[i64], 0)` |
| `fn (a: Atomic[T]) load() -> T` | `a.load()` | Loads the atomic value. | `v := a.load()` |
| `fn (a: Atomic[T]) store(val: T)` | `a.store(val)` | Stores a value into the atomic. | `a.store(10)` |
| `fn (a: Atomic[T]) add(delta: T) -> T` | `a.add(delta)` | Adds delta to atomic value. | `a.add(1)` |
| `fn (a: Atomic[T]) sub(delta: T) -> T` | `a.sub(delta)` | Subtracts delta from atomic value. | `a.sub(1)` |

### `core:sync/context` (`context`) — Cancellation Tokens & Deadline Contexts

- **Auto-Link Flags:** `(header-only)`
- **Overview:** Structured concurrency cancellation token and deadline propagation hierarchy (`core:sync/context`).

**Practical Example:**
```cf
package main

import "core:sync/context"
import "core:time"

fn main() {
    bg := context.background()
    (ctx, cancel) := context.with_timeout(bg, 500) // 500ms deadline
    defer cancel("worker task complete")

    time.sleep_ms(40)
    if !ctx.is_done() {
        println("Operation succeeded within timeout deadline")
    }
}
```

**Types:**

| Type | Kind | Description |
|---|---|---|
| `context.Context` | handle | Carries cancellation and a deadline across tasks; cancelling a parent cancels its children. |

| `.cf` Signature | Lowered C++23 | Description | Example |
|---|---|---|---|
| `fn background() -> Context` | `cfire::context::background()` | Returns an empty, root non-cancellable Context. | `ctx := context.background()` |
| `fn with_cancel(parent: Context) -> (Context, fn(reason: str = ""))` | `cfire::context::with_cancel(parent)` | Derives a child Context and a cancellation handle function from a parent Context. | `(ctx, cancel) := context.with_cancel(parent)` |
| `fn with_timeout(parent: Context, ms: i64) -> (Context, fn(reason: str = ""))` | `cfire::context::with_timeout(parent, ms)` | Derives a child Context with an automatic millisecond deadline. | `(ctx, cancel) := context.with_timeout(parent, 250)` |
| `fn (ctx: Context) is_cancelled() -> bool` | `ctx.is_cancelled()` | Returns true if the Context has been cancelled or timed out. | `if ctx.is_cancelled() { return }` |
| `fn (ctx: Context) done() -> bool` | `ctx.done()` | Synonym for `is_cancelled()`. | `if ctx.done() { return }` |
| `fn (ctx: Context) reason() -> str` | `ctx.reason()` | Returns the cancellation reason string. | `msg := ctx.reason()` |
| `fn (ctx: Context) wait()` | `ctx.wait()` | Blocks until the Context is cancelled or its deadline expires. | `ctx.wait()` |
| `fn (ctx: Context) wait_for(ms: i64) -> bool` | `ctx.wait_for(ms)` | Blocks up to `ms` milliseconds or until the Context is cancelled; returns true if cancelled. | `cancelled := ctx.wait_for(100)` |

### `core:tako` (`tako`) — Tako Cloud SDK Primitives (S3, SQS)

- **Auto-Link Flags:** `(header-only)`
- **Overview:** Cloud primitives and storage compatibility services (`core:tako`).

**Practical Example:**
```cf
package main

import "core:tako" as tk

fn main() -> void! {
    mut s3 := tk.NewS3Service()
    try s3.CreateBucket("data")
    _ = try s3.PutObject("data", "item.json", "{\"status\":\"ok\"}", "application/json")
    obj := try s3.GetObject("data", "item.json")
    println("Stored payload: {}", obj.Body)
}
```

| `.cf` Signature | Lowered C++23 | Description | Example |
|---|---|---|---|
| `fn NewS3Service() -> S3Service` | `cfire::tako::NewS3Service()` | Creates an S3 compatible storage service client. | `s3 := tako.NewS3Service()` |
| `fn NewSQSService() -> SQSService` | `cfire::tako::NewSQSService()` | Creates an SQS compatible message queue service client. | `sqs := tako.NewSQSService()` |
| `fn (mut s: S3Service) CreateBucket(name: str) -> void!` | `s.CreateBucket(name)` | Creates a storage bucket. | `try s3.CreateBucket("b")` |
| `fn (mut s: S3Service) PutObject(bucket: str, key: str, data: str, content_type: str) -> str!` | `s.PutObject(bucket, key, data, content_type)` | Uploads an object and returns its ETag. | `etag := try s3.PutObject("b", "k", "d", "text/plain")` |
| `fn (mut s: S3Service) GetObject(bucket: str, key: str) -> S3Object!` | `s.GetObject(bucket, key)` | Downloads an object from storage. | `obj := try s3.GetObject("b", "k")` |
| `fn (mut s: SQSService) CreateQueue(name: str, fifo: bool = false) -> str!` | `s.CreateQueue(name, fifo)` | Creates a message queue and returns its URL. | `q := try sqs.CreateQueue("q", true)` |
| `fn (mut s: SQSService) SendMessage(queue_url: str, body: str, group: str = "", dedup: str = "") -> str!` | `s.SendMessage(queue_url, body, group, dedup)` | Publishes a message to the queue. | `id := try sqs.SendMessage(q, "body")` |
| `fn (mut s: SQSService) ReceiveMessages(queue_url: str, max_messages: i32 = 1) -> []SQSMessage!` | `s.ReceiveMessages(queue_url, max_messages)` | Receives available messages from the queue. | `msgs := try sqs.ReceiveMessages(q, 1)` |

### `core:time` (`time`) — Time, Calendar Dates, Durations & RFC 3339 Formatting

- **Auto-Link Flags:** `(header-only)`
- **Overview:** High-resolution monotonic timing, duration arithmetic, and RFC 3339 / ISO 8601 formatting backed by `std::chrono`.

**Practical Example:**
```cf
package main

import "core:time"

fn main() {
    now := time.now()
    println("Current RFC 3339 timestamp: {}", now.format_rfc3339())

    // High-precision duration measurement
    time.sleep_ms(30)
    elapsed := time.since(now)
    println("Elapsed duration: {} ms ({} us)", elapsed.to_millis(), elapsed.to_micros())
}
```

**Types:**

| Type | Kind | Description |
|---|---|---|
| `time.Duration` | handle | A span of time with nanosecond precision, made with `time.duration_millis(ms)` and friends and passed to `time.sleep`. |
| `time.Time` | handle | A point in wall-clock time, from `time.now()`, `time.unix(sec)` or `time.from_unix_seconds(sec)`. |

| `.cf` Signature | Lowered C++23 | Description | Example |
|---|---|---|---|
| `fn now_ms() -> i64` | `cfire::time::now_ms()` | Returns monotonic milliseconds from `std::chrono::steady_clock`. | `t0 := time.now_ms()` |
| `fn unix_seconds() -> i64` | `cfire::time::unix_seconds()` | Returns current UTC Unix timestamp in seconds. | `ts := time.unix_seconds()` |
| `fn sleep_ms(ms: i64)` | `cfire::time::sleep_ms(ms)` | Sleeps the current thread for `ms` milliseconds. | `time.sleep_ms(10)` |
| `fn sleep_ctx(ctx: Context, ms: i64) -> bool` | `cfire::time::sleep_ctx(ctx, ms)` | Sleeps the current thread for `ms` or until `ctx` is cancelled; returns true if completed without cancellation. | `ok := time.sleep_ctx(ctx, 50)` |
| `fn now() -> Time` | `cfire::time::now()` | Returns current wall clock `Time` instant. | `t := time.now()` |
| `fn unix(sec: i64, nsec: i64 = 0) -> Time` | `cfire::time::unix(sec, nsec)` | Constructs a `Time` instant from Unix seconds and nanoseconds. | `t := time.unix(1700000000)` |
| `fn parse_rfc3339(s: str) -> Time!` | `cfire::time::parse_rfc3339(s)` | Parses an RFC 3339 / ISO 8601 timestamp string into a `Time` instant. | `t := try time.parse_rfc3339("2026-10-18T12:00:00Z")` |
| `fn duration_seconds(s: i64) -> Duration` | `cfire::time::duration_seconds(s)` | Constructs a typed `Duration` representing `s` seconds. | `d := time.duration_seconds(5)` |
| `fn duration_millis(ms: i64) -> Duration` | `cfire::time::duration_millis(ms)` | Constructs a typed `Duration` representing `ms` milliseconds. | `d := time.duration_millis(250)` |
| `fn duration_nanos(n: i64) -> Duration` | `cfire::time::duration_nanos(n)` | Constructs a Duration from nanoseconds. | `d := time.duration_nanos(1000)` |
| `fn duration_micros(us: i64) -> Duration` | `cfire::time::duration_micros(us)` | Constructs a Duration from microseconds. | `d := time.duration_micros(500)` |
| `fn duration_minutes(m: i64) -> Duration` | `cfire::time::duration_minutes(m)` | Constructs a Duration from minutes. | `d := time.duration_minutes(5)` |
| `fn duration_hours(h: i64) -> Duration` | `cfire::time::duration_hours(h)` | Constructs a Duration from hours. | `d := time.duration_hours(2)` |
| `fn from_unix_seconds(sec: i64) -> Time` | `cfire::time::from_unix_seconds(sec)` | Constructs a Time instant from Unix epoch seconds. | `t := time.from_unix_seconds(1700000000)` |
| `fn sleep(d: Duration)` | `cfire::time::sleep(d)` | Sleeps the current thread for duration `d`. | `time.sleep(time.duration_millis(50))` |

### `core:unicode` (`unicode`) — UTF-8 Decoding, Rune Counting & Unicode Manipulation

- **Auto-Link Flags:** `(header-only)`
- **Overview:** RFC 3629 compliant branchless UTF-8 decoding, codepoint extraction, rune validation, and Unicode case transformation.

**Practical Example:**
```cf
package main

import "core:unicode"

fn main() {
    text := "cfire 🔥 世界"
    // Validate RFC 3629 UTF-8 byte sequences
    if unicode.is_valid_utf8(text) {
        rune_count := unicode.rune_count(text)
        println("Unicode scalar value count: {}", rune_count) // 10 runes
    }

    // Decode into individual 32-bit scalar codepoints
    runes := unicode.runes(text)
    for r in runes {
        println("Rune: U+{:04X}", r)
    }
}
```

| `.cf` Signature | Lowered C++23 | Description | Example |
|---|---|---|---|
| `fn rune_count(s: str) -> usize` | `cfire::unicode::rune_count(s)` | Counts the number of Unicode scalar values (codepoints) in a UTF-8 string. | `count := unicode.rune_count("Hello, 世界! 🚀")` |
| `fn runes(s: str) -> []u32` | `cfire::unicode::runes(s)` | Decodes all Unicode codepoints in `s` into a slice of 32-bit scalar values. | `cps := unicode.runes("cfire 🔥")` |
| `fn is_valid_utf8(s: str) -> bool` | `cfire::unicode::is_valid_utf8(s)` | Validates whether a string consists of strictly valid RFC 3629 UTF-8 byte sequences. | `ok := unicode.is_valid_utf8(payload)` |
| `fn encode_rune(r: u32) -> str` | `cfire::unicode::encode_rune(r)` | Encodes a 32-bit Unicode codepoint into its UTF-8 string representation. | `char := unicode.encode_rune(0x1F525)` |
| `fn to_lower(s: str) -> str` | `cfire::unicode::to_lower(s)` | Converts Latin, Greek, and Cyrillic Unicode characters to lowercase. | `lower := unicode.to_lower("HELLO, СВЕТ!")` |
| `fn to_upper(s: str) -> str` | `cfire::unicode::to_upper(s)` | Converts Latin, Greek, and Cyrillic Unicode characters to uppercase. | `upper := unicode.to_upper("hello, свет!")` |

### `ext:ai` (`ai`) — On-Device & Cloud AI, Agent Orchestration, Document Ingestion & MCP

- **Auto-Link Flags:** `(POSIX sockets + TLS + AVX2/NEON SIMD + NaturalLanguage / PDFKit / Vision / Tesseract)`
- **Overview:** Unified AI SDK built around stateful `Client` and `Agent` objects supporting Cross-Platform On-Device Intelligence (`ai.local()` / `ai.apple()`: `NaturalLanguage`/`PDFKit`/`Vision` on Apple; dynamic `libtesseract`/`pdftotext` + pure C++23 decoders and AVX2/NEON SIMD on Linux/Windows/WASM), OpenAI (`ai.openai()`), Anthropic Claude (`ai.anthropic()`), Google Gemini (`ai.gemini()`), Ollama (`ai.ollama()`), vLLM/llama.cpp (`ai.openai_compatible()`), and deterministic offline mocks (`ai.mock()`, `ai.mock_sequence()`), with multi-turn tool-calling agents (`make(ai.Agent, client, model)`), self-correcting structured outputs (`Client.generate_struct[T]`, `Agent.run_struct[T]`, `Agent.run_struct_validated[T]`) backed by compile-time `validate:"..."` struct tags, stateful client embeddings/OCR/language recognition (`Client.embed`, `Client.embed_image`, `Client.ocr`, `Client.detect_language`), pure stateless document parsers (`load_document`, `chunk_document`), SIMD vector math (`cosine_similarity`, `l2_distance`, `dot_product`, `normalize`), and a Model Context Protocol server (`McpServer`) that shares tool definitions with `Agent` (`use_mcp`).

**Practical Example:**
```cf
package main

import "ext:ai"
import "ext:db/sqlite"
import "core:json"

type SearchArgs struct {
    query: str `json:"query" desc:"Semantic search query" validate:"non_empty"`
    limit: i32 `json:"limit" desc:"Maximum results to return" validate:"min=1,max=10"`
}

type DocHit struct {
    title: str `sql:"title" json:"title"`
    score: f64 `sql:"score" json:"score"`
}

fn main() {
    // 1. Compile-time JSON Schema reflection & validation constraints for LLM tools
    println("Tool schema: {}", json.schema[SearchArgs]())

    // 2. Parse & chunk structured documents (.pdf, .pptx, .docx, .odt, .epub, .md)
    doc := try ai.parse_document("# Systems Guide\nZero-overhead C++23 memory safety.\n# Vector SQLite\nEmbedded SIMD vector similarity search and RAG.", "guide.md")
    chunks := ai.chunk_document(doc, 256, 32)

    // 3. Stateful on-device AI client + SQLite SIMD vector similarity search
    client := ai.local()
    mut db := try sqlite.open(":memory:")
    defer db.close()
    try db.exec("CREATE TABLE docs (title TEXT, vec BLOB)")

    for ch in chunks {
        vec := try client.embed(ch.text)
        try db.exec("INSERT INTO docs VALUES (?1, ?2)", ch.title, vec)
    }

    qvec := try client.embed("vector search in sqlite")
    hits := try db.query[DocHit](
        "SELECT title, vec_cosine_similarity(vec, ?1) AS score FROM docs ORDER BY score DESC LIMIT 1",
        qvec
    )
    println("Top match: {} (score={})", hits[0].title, hits[0].score)
}
```

**Types:**

| Type | Kind | Description |
|---|---|---|
| `ai.Provider` | enum | Which model API a `Client` talks to. Variants: `OpenAI`, `Anthropic`, `Gemini`, `Ollama`, `OpenAICompatible`, `Local`, `Apple`, `Mock`. |
| `ai.Client` | handle | A connection to a model provider; send it messages, optionally with tools. |
| `ai.Agent` | handle | A client plus a tool registry that runs the tool-calling loop for you. |
| `ai.McpServer` | handle | A Model Context Protocol server exposing your tools over stdio. |
| `ai.Message` | struct | One chat message. Fields: `role: str`, `content: str`, `attachments: []Attachment`, `tool_calls: []ToolCall`, `tool_call_id: str`, `tool_name: str`, `is_error: bool`. |
| `ai.Tool` | struct | A tool the model may call; usually built with `ai.tool_def[T]`. Fields: `name: str`, `description: str`, `schema_json: str`, `parameters_schema: str`. |
| `ai.ToolCall` | struct | A model's request to run one tool. Fields: `id: str`, `name: str`, `arguments_json: str`. |
| `ai.ChatResponse` | struct | A model's reply. Fields: `text: str`, `model: str`, `tool_calls: []ToolCall`. |
| `ai.Attachment` | struct | A file attached to a message. Fields: `filename: str`, `mime_type: str`, `extracted_text: str`. |
| `ai.Document` | struct | A parsed document (`ai.parse_document`). Fields: `path: str`, `mime_type: str`, `format: str`, `text: str`, `sections: []DocumentSection`. |
| `ai.DocumentSection` | struct | One section of a document, as `ai.chunk_document` returns them. Fields: `index: i32`, `title: str`, `text: str`. |
| `ai.ZipEntry` | struct | One file from a zip archive. Fields: `name: str`, `content: str`. |

| `.cf` Signature | Lowered C++23 | Description | Example |
|---|---|---|---|
| `fn local() -> Client` | `cfire::ai::local()` | Creates a cross-platform on-device `Client` (`default_model: "local"`) backed by Apple `NaturalLanguage`/`Vision`/`PDFKit` on macOS/iOS and `libtesseract`/`pdftotext` + pure C++23 SIMD embeddings on Linux/Windows/WASM. | `client := ai.local()` |
| `fn apple() -> Client` | `cfire::ai::apple()` | Alias for `ai.local()` creating an on-device `Client` (`default_model: "local"`). | `client := ai.apple()` |
| `fn openai(api_key: str = "", model: str = "gpt-4o-mini") -> Client` | `cfire::ai::openai(api_key, model)` | Creates an OpenAI client (reads `OPENAI_API_KEY` if `api_key` is empty). | `client := ai.openai()` |
| `fn anthropic(api_key: str = "", model: str = "claude-3-5-sonnet-latest") -> Client` | `cfire::ai::anthropic(api_key, model)` | Creates an Anthropic Claude client (reads `ANTHROPIC_API_KEY` if `api_key` is empty). | `client := ai.anthropic()` |
| `fn gemini(api_key: str = "", model: str = "gemini-2.5-flash") -> Client` | `cfire::ai::gemini(api_key, model)` | Creates a Google Gemini client (reads `GEMINI_API_KEY` or `GOOGLE_API_KEY` if `api_key` is empty). | `client := ai.gemini()` |
| `fn ollama(host: str = "http://127.0.0.1:11434", model: str = "llama3.2") -> Client` | `cfire::ai::ollama(host, model)` | Creates a local Ollama client (uses `OLLAMA_HOST` if set). | `client := ai.ollama()` |
| `fn openai_compatible(base_url: str, api_key: str = "", model: str = "default") -> Client` | `cfire::ai::openai_compatible(base_url, api_key, model)` | Creates a client for any OpenAI-compatible server (`vLLM`, `llama-server`, `LM Studio`, `Groq`, `Together`, `DeepSeek`). | `client := ai.openai_compatible("http://127.0.0.1:8080/v1")` |
| `fn mock(handler: fn([]Message, []Tool) -> ChatResponse!) -> Client` | `cfire::ai::mock(handler)` | Creates a deterministic offline mock `Client` for unit-testing multi-turn `Agent` tool loops and structured output validation retries without network access. | `client := ai.mock(|msgs, tools| -> ai.ChatResponse! { ... })` |
| `fn mock_sequence(responses: []ChatResponse) -> Client` | `cfire::ai::mock_sequence(responses)` | Creates a deterministic offline `Client` that replays a scripted sequence of `ChatResponse` turns in order. | `client := ai.mock_sequence(turns)` |
| `fn user(content: str) -> Message` | `cfire::ai::user(content)` | Constructs a user role `Message`. | `m := ai.user("Summarize this file.")` |
| `fn user_with_file(content: str, file_path: str) -> Message!` | `cfire::ai::user_with_file(content, file_path)` | Constructs a multimodal user `Message` with an attached file (`.png`, `.jpg`, `.webp`, `.bmp`, `.pdf`, `.docx`, `.pptx`, `.xlsx`, `.odt`, `.odp`, `.ods`, `.epub`, `.html`, `.csv`, `.wav`). | `m := try ai.user_with_file("Describe this diagram", "arch.png")` |
| `fn load_attachment(path: str) -> Attachment!` | `cfire::ai::load_attachment(path)` | Loads a file from disk into a MIME-typed multimodal `Attachment` (populating extracted Markdown automatically for Office/OpenDocument/EPUB/text formats). | `att := try ai.load_attachment("slides.pptx")` |
| `fn detect_mime_type(path_or_name: str, data: []byte = []) -> str` | `cfire::ai::detect_mime_type(path_or_name, data)` | Detects the MIME type of a file from magic bytes (`%PDF-`, `PNG`, `BMP`, `JPEG`, `WEBP`, `ZIP`/OOXML/ODF/EPUB) and filename extension. | `mime := ai.detect_mime_type("report.pdf")` |
| `fn load_document(path: str) -> Document!` | `cfire::ai::load_document(path)` | Extracts structured Markdown and per-page/per-slide `sections` (`[]DocumentSection`) from `.pdf` (Apple `PDFKit`, Linux `pdftotext`, or portable C++23 `/FlateDecode` + `/ToUnicode` CMap), `.docx`, `.pptx`, `.xlsx`, `.odt`, `.odp`, `.ods`, `.epub`, `.html`, `.md`, `.csv`, or image files. | `doc := try ai.load_document("deck.pptx")` |
| `fn parse_document(data: []byte, filename: str = "document.txt") -> Document!` | `cfire::ai::parse_document(data, filename)` | Parses an in-memory byte slice or string into a structured `Document` with page/slide `sections`. | `doc := try ai.parse_document(raw_bytes, "spec.docx")` |
| `fn chunk_document(doc: Document, max_chars: i32 = 512, overlap: i32 = 64) -> []DocumentSection` | `cfire::ai::chunk_document(doc, max_chars, overlap)` | Splits a `Document` into RAG-ready chunks while preserving each chunk's 1-based page/slide `index` and section `title`. | `chunks := ai.chunk_document(doc, 512, 64)` |
| `fn system(content: str) -> Message` | `cfire::ai::system(content)` | Constructs a system instruction `Message`. | `m := ai.system("You are a concise coding assistant.")` |
| `fn assistant(content: str) -> Message` | `cfire::ai::assistant(content)` | Constructs an assistant role `Message`. | `m := ai.assistant("Here is the summary:")` |
| `fn tool_result(tool_call_id: str, tool_name: str, content: str, is_error: bool = false) -> Message` | `cfire::ai::tool_result(tool_call_id, tool_name, content, is_error)` | Constructs a tool-result `Message` correlated with a model `ToolCall` (`id` and `name`). | `m := ai.tool_result(tc.id, tc.name, output)` |
| `fn tool_def[T](name: str, description: str) -> Tool` | `cfire::ai::tool_def<T>(name, description)` | Constructs a typed LLM `Tool` definition whose JSON Schema is derived at compile time from struct `T`. | `t := ai.tool_def[WeatherArgs]("get_weather", "Look up current weather")` |
| `fn (mut c: Client) set_model(model: str)` | `c.set_model(model)` | Sets the client's stateful `default_model` used by 1-argument `ask`, `embed`, `embed_image`, and `generate_struct[T]` calls. | `client.set_model("gpt-4o")` |
| `fn (c: Client) model() -> str` | `c.model()` | Returns the client's active `default_model`. | `m := client.model()` |
| `fn (c: Client) ask(prompt: str) -> str!` | `c.ask(prompt)` | Executes a single-turn prompt using `c.model()` (or pass `(model, prompt, system_prompt)` to override) and returns the assistant text response. | `reply := try client.ask("Hello!")` |
| `fn (c: Client) ask_with_file(prompt: str, file_path: str) -> str!` | `c.ask_with_file(prompt, file_path)` | Executes a multimodal prompt with an attached image, PDF, slide deck, or document. | `ans := try client.ask_with_file("Summarize this PDF", "paper.pdf")` |
| `fn (c: Client) chat(messages: []Message) -> ChatResponse!` | `c.chat(messages)` | Executes a multi-turn chat completion returning text, tool calls, and token usage. | `res := try client.chat(msgs)` |
| `fn (c: Client) chat_with_tools(messages: []Message, tools: []Tool) -> ChatResponse!` | `c.chat_with_tools(messages, tools)` | Executes a chat completion with compile-time schema-typed `tools` and returns any requested `ToolCall` invocations. | `res := try client.chat_with_tools(msgs, [weather_tool])` |
| `fn (c: Client) stream(messages: []Message, on_delta: fn(str)) -> str!` | `c.stream(messages, on_delta)` | Streams tokens in real time via SSE across OpenAI, Anthropic, Gemini, or Ollama, invoking `on_delta` for each token chunk. | `full := try client.stream(msgs, |tok| { print("{}", tok) })` |
| `fn (c: Client) generate_struct[T](prompt: str) -> T!` | `c.generate_struct<T>(prompt)` | Enforces `json.schema[T]()` on the model response, parses and validates `T` (including `validate:"..."` constraints), and automatically feeds validation errors back to the model to self-correct. | `inv := try client.generate_struct[Invoice](raw_text)` |
| `fn (c: Client) generate_struct_from_file[T](prompt: str, file_path: str) -> T!` | `c.generate_struct_from_file<T>(prompt, file_path)` | Extracts a strongly-typed `cfire` struct `T` directly from an image, PDF, slide deck, or document using compile-time `json.schema[T]()` with self-correcting validation retries. | `inv := try client.generate_struct_from_file[Invoice]("Extract fields", "receipt.png")` |
| `fn (c: Client) embed(text: str) -> []f32!` | `c.embed(text)` | Generates a normalized `[]f32` embedding vector using `c`'s provider and `default_model` (or pass `(model, text)` to override). | `vec := try client.embed("compile-time memory safety")` |
| `fn (c: Client) embed_image(path: str) -> []f32!` | `c.embed_image(path)` | Computes a normalized `[]f32` visual/multimodal embedding vector for an image file using `c`'s provider (`ai.local()` decodes PNG/BMP/PPM pixels in pure C++23 with 4x4 RGB/Luma moments, Sobel HOG edge histograms, 8x8 dHash, and fused OCR semantics). | `ivec := try client.embed_image("chart.png")` |
| `fn (c: Client) ocr(path: str) -> str!` | `c.ocr(path)` | Extracts text from an image file using `c`'s provider (`Vision.framework` on macOS/iOS, `libtesseract.so.5` / `tesseract` CLI on Linux, PNG/JPEG metadata, or cloud multimodal vision). | `text := try client.ocr("receipt.png")` |
| `fn (c: Client) detect_language(text: str) -> str` | `c.detect_language(text)` | Detects the dominant BCP-47 language code (e.g. `"en"`, `"es"`, `"ja"`) using Apple `NLLanguageRecognizer` on macOS/iOS or portable UTF-8 script/stopword classification. | `lang := client.detect_language("Bonjour tout le monde")` |
| `fn cosine_similarity(a: []f32, b: []f32) -> f32` | `cfire::ai::cosine_similarity(a, b)` | Computes the cosine similarity in `[-1.0, 1.0]` between two `[]f32` embedding vectors. | `sim := ai.cosine_similarity(v1, v2)` |
| `fn l2_distance(a: []f32, b: []f32) -> f32` | `cfire::ai::l2_distance(a, b)` | Computes the Euclidean (L2) distance between two `[]f32` vectors. | `dist := ai.l2_distance(v1, v2)` |
| `fn dot_product(a: []f32, b: []f32) -> f32` | `cfire::ai::dot_product(a, b)` | Computes the inner product of two `[]f32` vectors. | `dot := ai.dot_product(v1, v2)` |
| `fn normalize(v: []f32) -> []f32` | `cfire::ai::normalize(v)` | Returns an L2-normalized unit vector copy of `v`. | `unit := ai.normalize(raw_vec)` |
| `fn chunk_text(text: str, max_chars: i32 = 512, overlap: i32 = 64) -> []str` | `cfire::ai::chunk_text(text, max_chars, overlap)` | Splits a document into overlapping paragraph/sentence-aware chunks for RAG indexing. | `chunks := ai.chunk_text(markdown, 512, 64)` |
| `fn make(client: Client, model: str = "", system_prompt: str = "") -> Agent` | `cfire::ai::make_agent(client, model, system_prompt)` | `make(ai.Agent, client, model, system_prompt)` creates a stateful multi-turn `Agent` that runs tool calls through `client`. | `mut agent := make(ai.Agent, client, "gpt-4o")` |
| `fn (mut a: Agent) register_tool[Args](name: str, description: str, handler: fn(Args) -> R!)` | `a.register_tool<Args>(name, description, handler)` | Registers a schema-typed tool on the `Agent` (`handler` may return `str`, `str!`, or any JSON-serializable struct `R` / `R!`; errors are returned to the LLM to self-correct). | `agent.register_tool[LookupArgs]("lookup", "Lookup item", |args| { return item })` |
| `fn (mut a: Agent) register_tool_action(name: str, description: str, handler: fn() -> R!)` | `a.register_tool_action(name, description, handler)` | Registers a parameterless tool on the `Agent`. | `agent.register_tool_action("ping", "Health check", || { return "pong" })` |
| `fn (mut a: Agent) use_mcp(mcp: McpServer)` | `a.use_mcp(mcp)` | Mounts all tools registered on `mcp` directly into the `Agent`. | `agent.use_mcp(mcp)` |
| `fn (mut a: Agent) set_max_steps(steps: i32)` | `a.set_max_steps(steps)` | Configures the maximum number of tool-calling turns per `run` (default `8`). | `agent.set_max_steps(10)` |
| `fn (mut a: Agent) set_max_retries(retries: i32)` | `a.set_max_retries(retries)` | Configures the maximum number of self-correction retries for tool or output validation failures (default `2`). | `agent.set_max_retries(3)` |
| `fn (mut a: Agent) run(prompt: str) -> str!` | `a.run(prompt)` | Runs the multi-turn agent loop until the model produces a final text response. | `reply := try agent.run("Check the deployment status")` |
| `fn (mut a: Agent) run_messages(messages: []Message) -> ChatResponse!` | `a.run_messages(messages)` | Appends `messages` to the agent history and runs the multi-turn tool loop, returning the final `ChatResponse`. | `res := try agent.run_messages([ai.user("Hello")])` |
| `fn (mut a: Agent) run_struct[Out](prompt: str) -> Out!` | `a.run_struct<Out>(prompt)` | Runs the multi-turn agent loop and extracts a validated struct `Out` (automatically retrying on schema or `validate:"..."` failures). | `report := try agent.run_struct[Report]("Generate the status report")` |
| `fn (mut a: Agent) run_struct_validated[Out](prompt: str, validator: fn(Out) -> void!) -> Out!` | `a.run_struct_validated<Out>(prompt, validator)` | Runs the multi-turn agent loop, parses `Out`, and executes custom cross-field `validator` logic (feeding any `error(...)` back to the model to self-correct). | `report := try agent.run_struct_validated[Report]("Summarize", |r: Report| -> void! { ... })` |
| `fn (a: Agent) history() -> []Message` | `a.history()` | Returns the accumulated multi-turn conversation and tool-call `[]Message` history. | `msgs := agent.history()` |
| `fn (mut a: Agent) clear_history()` | `a.clear_history()` | Clears the agent's conversation history. | `agent.clear_history()` |
| `fn make(name: str, version: str = "1.0.0") -> McpServer` | `cfire::ai::make_mcp_server(name, version)` | `make(ai.McpServer, name, version)` creates a Model Context Protocol (MCP) server that serves registered tools over stdio. | `mut mcp := make(ai.McpServer, "docs", "1.0.0")` |
| `fn (mut s: McpServer) register_tool[T](name: str, description: str, handler: fn(T) -> R!)` | `s.register_tool<T>(name, description, handler)` | Registers an MCP tool whose input schema is derived from `T` (`json.schema[T]()`) and whose handler may return `str`, `str!`, or any JSON-serializable struct `R` / `R!`. | `mcp.register_tool[Query]("search", "Search docs", |q| { return "ok" })` |
| `fn (mut s: McpServer) register_tool_action(name: str, description: str, handler: fn() -> R!)` | `s.register_tool_action(name, description, handler)` | Registers a parameterless MCP tool. | `mcp.register_tool_action("status", "Server status", || { return "ready" })` |
| `fn (mut s: McpServer) handle_jsonrpc(req_json: str) -> str` | `s.handle_jsonrpc(req_json)` | Processes a single MCP JSON-RPC 2.0 request (`initialize`, `tools/list`, `tools/call`, `ping`) and returns the JSON-RPC response string. | `resp := mcp.handle_jsonrpc(line)` |
| `fn (mut s: McpServer) serve_stdio() -> i32` | `s.serve_stdio()` | Runs the MCP JSON-RPC 2.0 server loop over `stdin`/`stdout`. | `mcp.serve_stdio()` |
| `fn create_zip(entries: map[str]str) -> []byte` | `cfire::ai::create_zip(entries)` | Constructs an in-memory zip archive from filename-content pairs. | `z := ai.create_zip(entries)` |

### `ext:compress/zstd` (`zstd`) — Native Zstandard Frame Compression

- **Auto-Link Flags:** `-lzstd`
- **Overview:** Hardware-accelerated Zstandard frame compression and decompression backed by native `libzstd`.

**Practical Example:**
```cf
package main

import "ext:compress/zstd"

fn main() {
    raw := []byte("high-throughput packet trace payload ... high-throughput packet trace payload ...")

    // Hardware-accelerated Zstandard frame compression
    compressed := try zstd.compress(raw, level: 3)
    println("Original size: {} bytes -> Compressed: {} bytes", raw.len(), compressed.len())

    // Decompression using the embedded frame content size
    decompressed := try zstd.decompress(compressed)
    println("Decompressed size: {} bytes", decompressed.len())
}
```

| `.cf` Signature | Lowered C++23 | Description | Example |
|---|---|---|---|
| `fn compress(input: []byte, level: i32 = 3) -> []byte!` | `cfire::zstd::compress(input, level)` | Compresses a byte slice into a Zstandard frame. | `blob := try zstd.compress(raw, level: 3)` |
| `fn compress_str(input: str, level: i32 = 3) -> []byte!` | `cfire::zstd::compress_str(input, level)` | Compresses a string into a Zstandard frame. | `blob := try zstd.compress_str(s, 3)` |
| `fn decompress(frame: []byte) -> []byte!` | `cfire::zstd::decompress(frame)` | Decompresses a Zstandard frame using the embedded frame content size. | `orig := try zstd.decompress(blob)` |
| `fn decompress_str(frame: []byte) -> str!` | `cfire::zstd::decompress_str(frame)` | Decompresses a Zstandard frame into a UTF-8 string. | `orig := try zstd.decompress_str(blob)` |

### `ext:crypto/sodium` (`sodium`) — Memory-Hard Argon2id KDF, BLAKE2b, Subkey Derivation & XChaCha20-Poly1305 AEAD

- **Auto-Link Flags:** `-lsodium`
- **Overview:** Modern cryptographic primitives backed by `libsodium` (`crypto_pwhash` Argon2id password hashing, `crypto_generichash` keyed/unkeyed BLAKE2b, `crypto_kdf_derive_from_key` subkey derivation, and `crypto_aead_xchacha20poly1305_ietf` 192-bit nonce AEAD).

**Practical Example:**
```cf
package main

import "ext:crypto/sodium"

fn main() {
    salt := sodium.random_bytes(16)
    mut key := try sodium.argon2id("correct horse battery staple", salt, 32)
    defer sodium.memzero(mut key)

    sealed := try sodium.xchacha20poly1305_encrypt(key, []byte("top-secret-vault-payload"))
    opened := try sodium.xchacha20poly1305_decrypt(key, sealed)
    println("Decrypted bytes: {}", opened.len())
}
```

| `.cf` Signature | Lowered C++23 | Description | Example |
|---|---|---|---|
| `fn argon2id(password: str, salt: []byte, out_len: usize = 32, opslimit: u64 = 2, memlimit: usize = 67108864) -> []byte!` | `cfire::sodium::argon2id(password, salt, out_len, opslimit, memlimit)` | Derives a cryptographic key of `out_len` bytes from `password` and a 16-byte `salt` using Argon2id. | `key := try sodium.argon2id(master_pw, salt, 32)` |
| `fn argon2id_str(password: str, opslimit: u64 = 2, memlimit: usize = 67108864) -> str!` | `cfire::sodium::argon2id_str(password, opslimit, memlimit)` | Hashes a password into a PHC-formatted `$argon2id$...` verification string. | `encoded := try sodium.argon2id_str(password)` |
| `fn argon2id_verify(encoded: str, password: str) -> bool` | `cfire::sodium::argon2id_verify(encoded, password)` | Verifies a password against an encoded Argon2id hash string in constant time. | `ok := sodium.argon2id_verify(encoded, password)` |
| `fn blake2b(input: []byte | str, out_len: usize = 32, key: []byte = []) -> []byte!` | `cfire::sodium::blake2b(input, out_len, key)` | Computes a keyed or unkeyed BLAKE2b hash (`crypto_generichash`) of `out_len` bytes (16..64). | `mac := try sodium.blake2b(payload, 32, key)` |
| `fn derive_subkey(master_key: []byte, subkey_id: u64, context: str, out_len: usize = 32) -> []byte!` | `cfire::sodium::derive_subkey(master_key, subkey_id, context, out_len)` | Derives a domain-separated subkey of `out_len` bytes (16..64) from a 32-byte `master_key`, `subkey_id`, and 8-byte `context` (`crypto_kdf_derive_from_key`). | `sub := try sodium.derive_subkey(master_key, 1, "flintkdf", 32)` |
| `fn xchacha20poly1305_encrypt(key: []byte, plaintext: []byte, aad: []byte = []) -> []byte!` | `cfire::sodium::xchacha20poly1305_encrypt(key, pt, aad)` | Encrypts `plaintext` with a 32-byte key using XChaCha20-Poly1305, returning `[24-byte random nonce || ciphertext || 16-byte Poly1305 tag]`. | `sealed := try sodium.xchacha20poly1305_encrypt(key, secret)` |
| `fn xchacha20poly1305_decrypt(key: []byte, combined: []byte, aad: []byte = []) -> []byte!` | `cfire::sodium::xchacha20poly1305_decrypt(key, combined, aad)` | Authenticates and decrypts a `[24-byte nonce || ciphertext || 16-byte tag]` XChaCha20-Poly1305 payload. | `pt := try sodium.xchacha20poly1305_decrypt(key, sealed)` |
| `fn random_bytes(n: usize) -> []byte` | `cfire::sodium::random_bytes(n)` | Generates `n` cryptographically secure random bytes (`randombytes_buf`). | `salt := sodium.random_bytes(16)` |
| `fn memzero(mut buf: []byte)` | `cfire::sodium::memzero(buf)` | Securely zeroes sensitive key material in memory (`sodium_memzero`). | `sodium.memzero(mut key)` |

### `ext:db/sqlite` (`sqlite`) — Embedded SQLite3 & SQLCipher with WAL, Vector Search & Row Mapping

- **Auto-Link Flags:** `-lsqlite3 / -lsqlcipher`
- **Overview:** RAII SQLite3 & SQLCipher wrapper enabling `PRAGMA journal_mode=WAL`, `open_encrypted` full-database encryption, parameterized queries, `[]f32` embedding BLOB binding, built-in SQL vector similarity functions (`vec_cosine_similarity`, `vec_l2_distance`, `vec_dot`), dynamic extension loading (`sqlite-vec`), atomic transactions, and compile-time `sql:"..."` struct row mapping.

**Practical Example:**
```cf
package main

import "ext:db/sqlite"

type UserRecord struct {
    id:    i64 `sql:"id"`
    name:  str `sql:"name"`
    email: str `sql:"email"`
}

fn main() {
    // Opens SQLite connection and enables WAL mode automatically
    mut db := try sqlite.open("app.db")
    defer db.close()

    try db.exec("CREATE TABLE IF NOT EXISTS users (id INTEGER PRIMARY KEY, name TEXT, email TEXT)")

    // Atomic transaction with auto-rollback on error
    try db.tx |mut tx| {
        try tx.exec("INSERT INTO users (name, email) VALUES (?1, ?2)", "alice", "alice@systems.dev")
        try tx.exec("INSERT INTO users (name, email) VALUES (?1, ?2)", "bob", "bob@systems.dev")
    }

    // Direct struct row mapping via sql:"..." tags
    users := try db.query[UserRecord]("SELECT id, name, email FROM users WHERE id >= ?1", 1)
    for u in users {
        println("User [{}]: {} <{}>", u.id, u.name, u.email)
    }
}
```

**Types:**

| Type | Kind | Description |
|---|---|---|
| `sqlite.Conn` | handle | An open SQLite database; `exec` runs statements and `query[T]` maps rows to structs. |

| `.cf` Signature | Lowered C++23 | Description | Example |
|---|---|---|---|
| `fn open(path: str) -> Conn!` | `cfire::sqlite::open(path)` | Opens an SQLite3 database connection, enables WAL mode, and registers vector SQL functions (`vec_cosine_similarity`, `vec_cosine_distance`, `vec_l2_distance`, `vec_dot`, `vec_dims`). | `mut db := try sqlite.open("app.db")` |
| `fn open_encrypted(path: str, key: str) -> Conn!` | `cfire::sqlite::open_encrypted(path, key)` | Opens an SQLite3 / SQLCipher database and applies `PRAGMA key` encryption immediately before schema access. | `mut db := try sqlite.open_encrypted("vault.db", master_key)` |
| `fn (mut db: Conn) apply_key(key: str) -> void!` | `db.apply_key(key)` | Applies an encryption key (`sqlite3_key` / `PRAGMA key`) to an open database connection. | `try db.apply_key(master_key)` |
| `fn (mut db: Conn) rekey(new_key: str) -> void!` | `db.rekey(new_key)` | Rotates the database encryption key (`sqlite3_rekey` / `PRAGMA rekey`). | `try db.rekey(next_key)` |
| `fn (mut db: Conn) exec(sql: str, ...args) -> void!` | `db.exec(sql, args...)` | Executes a parameterized SQL statement (`?1`, `?2`, ...), including `[]f32` vector and `[]byte` BLOB parameters. | `try db.exec("INSERT INTO docs VALUES (?1, ?2)", id, vec)` |
| `fn (mut db: Conn) query[T](sql: str, ...args) -> []T!` | `db.query<T>(sql, args...)` | Executes a `SELECT` query and maps rows directly into struct `T` via `sql:"..."` tags (supporting `str`, `i64`, `f64`, `[]f32`, `[]byte`). | `rows := try db.query[Doc]("SELECT id, text FROM docs ORDER BY vec_cosine_similarity(vec, ?1) DESC", qvec)` |
| `fn (mut db: Conn) load_extension(path: str, entry_point: str = "") -> void!` | `db.load_extension(path, entry_point)` | Dynamically loads a native SQLite extension shared library (e.g. `sqlite-vec` `vec0` or `sqlite-vss`). | `try db.load_extension("./vec0")` |
| `fn (mut db: Conn) tx(block: fn(mut tx: Conn) -> void!) -> void!` | `db.tx([&](Conn& tx) { ... })` | Runs a closure inside an atomic `BEGIN IMMEDIATE ... COMMIT` transaction with automatic rollback on error. | `try db.tx |mut tx| { ... }` |

### `ext:net/ssh` (`ssh`) — First-Class SSH Client, PTY, SFTP & Port Forwarding

- **Auto-Link Flags:** `-lssh (libssh)`
- **Overview:** SSHv2 client, interactive PTY terminal sessions, one-shot command execution, SFTP file transfers, and TCP port forwarding backed by `libssh` (`CFIRE_HAS_LIBSSH=1`) with deterministic loopback/mock support (`ssh.enable_mock_server()`) for headless testing.

**Practical Example:**
```cf
package main

import "ext:net/ssh"
import "core:keychain"

fn main() {
    mut sess := try ssh.connect("10.0.0.12", 22, "deploy", 5000)
    defer sess.close()

    fp := sess.host_key_fingerprint()
    println("Host key: {fp}")

    priv_key := try keychain.get("com.example.ssh", "id_ed25519")
    try sess.auth_publickey_mem(priv_key, "")

    res := try sess.exec("uname -a")
    print(res.stdout)

    mut ch := try sess.open_pty("xterm-256color", 120, 36)
    defer ch.close()
    _ = try ch.write_text("ls -la\n")
    chunk := try ch.read_text(4096)
    print(chunk)
}
```

**Types:**

| Type | Kind | Description |
|---|---|---|
| `ssh.Session` | handle | An authenticated SSH connection; run commands or open channels on it. |
| `ssh.Channel` | handle | An SSH channel (a shell or a command) with its own input and output. |
| `ssh.ExecResult` | struct | The outcome of a remote command. Fields: `exit_code: i32`, `stdout: str`, `stderr: str`. |
| `ssh.SftpEntry` | struct | One entry of a remote directory listing. Fields: `name: str`, `is_dir: bool`. |

| `.cf` Signature | Lowered C++23 | Description | Example |
|---|---|---|---|
| `fn connect(host: str, port: u16 = 22, user: str = "", timeout_ms: i64 = 10000) -> Session!` | `cfire::ssh::connect(host, port, user, timeout_ms)` | Connects an SSH session to `host:port` as `user` (default: the local login name, like ssh(1)). | `mut sess := try ssh.connect("build-box", 22, "deploy")` |
| `fn enable_mock_server(enabled: bool = true)` | `cfire::ssh::enable_mock_server(enabled)` | Enables or disables the deterministic in-memory loopback SSH/PTY/SFTP mock server for unit tests. | `ssh.enable_mock_server(true)` |
| `fn (sess: Session) host_key_fingerprint() -> str` | `sess.host_key_fingerprint()` | Returns the connected server's public key fingerprint in `SHA256:<base64>` format. | `fp := sess.host_key_fingerprint()` |
| `fn (sess: Session) is_known_host() -> bool` | `sess.is_known_host()` | Checks whether the server's public key matches `~/.ssh/known_hosts`. | `if !sess.is_known_host() { ... }` |
| `fn (mut sess: Session) write_known_host() -> void!` | `sess.write_known_host()` | Appends the server's public key to `~/.ssh/known_hosts` (TOFU). | `try sess.write_known_host()` |
| `fn (mut sess: Session) auth_publickey_mem(private_key_pem: str, passphrase: str = "") -> void!` | `sess.auth_publickey_mem(private_key_pem, passphrase)` | Authenticates using an in-memory private key (PEM/OpenSSH) without writing temporary files to disk. | `try sess.auth_publickey_mem(pem_key, "")` |
| `fn (mut sess: Session) auth_publickey_file(private_key_path: str, passphrase: str = "") -> void!` | `sess.auth_publickey_file(private_key_path, passphrase)` | Authenticates using a private key file on disk. | `try sess.auth_publickey_file("/Users/me/.ssh/id_ed25519", "")` |
| `fn (mut sess: Session) auth_password(password: str) -> void!` | `sess.auth_password(password)` | Authenticates using password authentication. | `try sess.auth_password(secret_pw)` |
| `fn (mut sess: Session) auth_agent() -> void!` | `sess.auth_agent()` | Authenticates via the local `ssh-agent` (`SSH_AUTH_SOCK`). | `try sess.auth_agent()` |
| `fn (mut sess: Session) exec(command: str) -> ExecResult!` | `sess.exec(command)` | Executes a one-shot remote command and returns `ExecResult{exit_code, stdout, stderr}`. | `res := try sess.exec("uname -a")` |
| `fn (mut sess: Session) open_pty(term: str = "xterm-256color", cols: i32 = 80, rows: i32 = 24) -> Channel!` | `sess.open_pty(term, cols, rows)` | Opens an interactive PTY shell channel with initial terminal type and dimensions. | `mut ch := try sess.open_pty("xterm-256color", 120, 36)` |
| `fn (mut sess: Session) open_forward(remote_host: str, remote_port: u16, source_host: str = "127.0.0.1", local_port: u16 = 0) -> Channel!` | `sess.open_forward(remote_host, remote_port, source_host, local_port)` | Opens a direct TCP/IP port-forwarding channel (`ssh_channel_open_forward`). | `mut fwd := try sess.open_forward("127.0.0.1", 5432)` |
| `fn (mut sess: Session) sftp_write_text(remote_path: str, text: str, mode: u32 = 0644) -> void!` | `sess.sftp_write_text(remote_path, text, mode)` | Uploads UTF-8 `text` to `remote_path` over SFTP. | `try sess.sftp_write_text("/remote/app.conf", cfg_text)` |
| `fn (mut sess: Session) sftp_write_bytes(remote_path: str, data: []byte, mode: u32 = 0644) -> void!` | `sess.sftp_write_bytes(remote_path, data, mode)` | Uploads binary `data` to `remote_path` over SFTP. | `try sess.sftp_write_bytes("/remote/app.bin", raw_bytes)` |
| `fn (mut sess: Session) sftp_read_text(remote_path: str) -> str!` | `sess.sftp_read_text(remote_path)` | Downloads the UTF-8 text contents of `remote_path` over SFTP. | `body := try sess.sftp_read_text("/remote/app.conf")` |
| `fn (mut sess: Session) sftp_read_bytes(remote_path: str) -> []byte!` | `sess.sftp_read_bytes(remote_path)` | Downloads the raw binary bytes of `remote_path` over SFTP. | `raw := try sess.sftp_read_bytes("/remote/app.bin")` |
| `fn (mut sess: Session) sftp_list_dir(remote_path: str) -> []SftpEntry!` | `sess.sftp_list_dir(remote_path)` | Lists directory entries in `remote_path` over SFTP. | `entries := try sess.sftp_list_dir("/remote")` |
| `fn (mut sess: Session) sftp_mkdir(remote_path: str, mode: u32 = 0755) -> void!` | `sess.sftp_mkdir(remote_path, mode)` | Creates a remote directory over SFTP. | `try sess.sftp_mkdir("/remote/releases")` |
| `fn (mut sess: Session) sftp_remove(remote_path: str) -> void!` | `sess.sftp_remove(remote_path)` | Removes a remote file over SFTP. | `try sess.sftp_remove("/remote/old.tmp")` |
| `fn (mut sess: Session) close()` | `sess.close()` | Disconnects and frees the SSH session. | `defer sess.close()` |
| `fn (mut ch: Channel) read_text(max_bytes: usize = 4096) -> str!` | `ch.read_text(max_bytes)` | Non-blockingly reads up to `max_bytes` of UTF-8 text from the SSH channel (`""` when no data is currently ready). | `chunk := try ch.read_text(4096)` |
| `fn (mut ch: Channel) read_bytes(max_bytes: usize = 4096) -> []byte!` | `ch.read_bytes(max_bytes)` | Non-blockingly reads up to `max_bytes` of raw bytes from the SSH channel. | `raw := try ch.read_bytes(4096)` |
| `fn (mut ch: Channel) write_text(text: str) -> usize!` | `ch.write_text(text)` | Writes UTF-8 `text` into the SSH channel (e.g. PTY stdin). | `_ = try ch.write_text("ls -la\n")` |
| `fn (mut ch: Channel) write_bytes(data: []byte) -> usize!` | `ch.write_bytes(data)` | Writes binary `data` into the SSH channel. | `_ = try ch.write_bytes(raw)` |
| `fn (mut ch: Channel) resize_pty(cols: i32, rows: i32) -> void!` | `ch.resize_pty(cols, rows)` | Dynamically resizes an open interactive PTY (`ssh_channel_change_pty_size`). | `try ch.resize_pty(140, 42)` |
| `fn (ch: Channel) is_eof() -> bool` | `ch.is_eof()` | Returns `true` if the remote channel has reached EOF. | `if ch.is_eof() { break }` |
| `fn (mut ch: Channel) close()` | `ch.close()` | Sends EOF and closes the SSH channel. | `defer ch.close()` |

### `ext:webui` (`webui`) — Hybrid Native WebView & Two-Way Typed RPC Application Framework

- **Auto-Link Flags:** `(WKWebView / WebKitGTK / WebView2 + JSON-RPC 2.0)`
- **Overview:** Wails/Tauri-style hybrid desktop and mobile application runtime hosting an OS-native WebView (`WKWebView` on macOS & iOS, `WebKitGTK` on Linux, `WebView2` on Windows) over a custom `app://localhost/` URI scheme with two-way typed struct RPC (`app.bind[T]`), live event streaming (`app.on`, `app.emit`), reactive key-value state (`app.set_state`), and an injected `window.backend.*` ES6 Proxy + `window.invoke` + `window.runtime.*` JavaScript bridge.

**Practical Example:**
```cf
package main

import "ext:webui"

type TrackMixReq struct {
    channel: i32  `json:"channel"`
    gain_db: f64  `json:"gain_db"`
    muted:   bool `json:"muted"`
}

type TrackStatusRes struct {
    channel:      i32 `json:"channel"`
    effective_db: f64 `json:"effective_db"`
    status:       str `json:"status"`
}

fn main() {
    mut app := make(webui.App, "Pocket Mastering Studio", 960, 640)
    defer app.close()

    app.serve_dir("./ui")
    app.set_dark_mode(true)

    // Bind a typed cfire struct handler callable from JS via `await window.backend.set_track({...})`
    app.bind[TrackMixReq]("set_track") |req| {
        mut eff := req.gain_db
        mut label := "active"
        if req.muted {
            eff = -96.0
            label = "muted"
        }
        res := TrackStatusRes{
            channel:      req.channel,
            effective_db: eff,
            status:       label,
        }
        app.emit("meter:peak", res)
        return res
    }

    // Listen for live frontend events (`window.runtime.EventsEmit("fader:input", ...)`)
    app.on("fader:input") |payload| {
        app.set_state("last_fader", payload)
    }

    app.run()
}
```

**Types:**

| Type | Kind | Description |
|---|---|---|
| `webui.App` | handle | A native desktop window running a web UI, with RPC between the page and cfire. |

| `.cf` Signature | Lowered C++23 | Description | Example |
|---|---|---|---|
| `fn make(title: str = "cfire WebUI", width: i32 = 960, height: i32 = 640) -> App` | `cfire::webui::new_app(title, width, height)` | `make(webui.App, title, width, height)` creates a native WebView application window. | `mut app := make(webui.App, "Studio", 1280, 800)` |
| `fn (mut app: App) bind[T](name: str, handler: fn(T) -> any!)` | `app.bind<T>(name, handler)` | Binds a typed `cfire` closure to `window.backend.<name>(params)` and `window.invoke(name, params)`, automatically deserializing incoming JSON into struct `T` and serializing the return value. | `app.bind[TrackMix]("set_track") |req| { return compute_mix(req) }` |
| `fn (mut app: App) bind_async[T](name: str, handler: fn(T) -> any!)` | `app.bind_async<T>(name, handler)` | Binds a typed RPC handler that executes asynchronously on a background worker thread without blocking the main UI/WebView thread. | `app.bind_async[ConnectReq]("connect_ssh") |req| { return dial(req) }` |
| `fn (mut app: App) bind_action(name: str, handler: fn() -> any!)` | `app.bind_action(name, handler)` | Binds a zero-argument action closure to `window.backend.<name>()`. | `app.bind_action("reset_desk") || { return "ok" }` |
| `fn (mut app: App) bind_action_async(name: str, handler: fn() -> any!)` | `app.bind_action_async(name, handler)` | Binds a zero-argument RPC action that executes asynchronously on a background worker thread. | `app.bind_action_async("sync_now") || { return run_sync() }` |
| `fn (mut app: App) bind_raw(name: str, handler: fn(str) -> str!)` | `app.bind_raw(name, handler)` | Binds a raw JSON string RPC handler to `window.backend.<name>(params)`. | `app.bind_raw("raw_op") |raw_json| { return raw_json }` |
| `fn (mut app: App) on(event: str, handler: fn(str))` | `app.on(event, handler)` | Subscribes a backend callback to frontend events emitted via `window.runtime.EventsEmit(event, data)`. | `app.on("fader:input") |payload| { println("{}", payload) }` |
| `fn (mut app: App) emit(event: str, payload: any)` | `app.emit(event, payload)` | Serializes `payload` as JSON and dispatches it to all frontend `window.runtime.EventsOn(event, fn)` listeners. | `app.emit("meter:peak", status)` |
| `fn (mut app: App) emit_bytes(event: str, data: []byte)` | `app.emit_bytes(event, data)` | Emits a binary `[]byte` payload to frontend `window.runtime.EventsOn(event, fn)` listeners as a Base64-decoded `Uint8Array`. | `app.emit_bytes("pty:data", chunk)` |
| `fn (mut app: App) clear_history()` | `app.clear_history()` | Clears the bounded in-memory ring buffer of recorded emitted events. | `app.clear_history()` |
| `fn (mut app: App) set_max_history(max_events: usize)` | `app.set_max_history(max_events)` | Sets the maximum number of emitted events retained per event name for testing inspection (`0` disables history retention). | `app.set_max_history(128)` |
| `fn (app: App) clipboard_set(text: str, concealed: bool = false, clear_after_ms: i64 = 0)` | `app.clipboard_set(text, concealed, clear_after_ms)` | Copies `text` to the native OS system clipboard (`NSPasteboard` on macOS, `GtkClipboard` on Linux, `Win32` on Windows), optionally marking it concealed (`org.nspasteboard.ConcealedType` / `ExcludeClipboardContentFromMonitorProcessing`) and auto-clearing after `clear_after_ms` if unchanged. | `app.clipboard_set(token, true, 15000)` |
| `fn (app: App) clipboard_set_secret(text: str, clear_after_ms: i64 = 30000)` | `app.clipboard_set_secret(text, clear_after_ms)` | Copies a sensitive secret to the OS clipboard marked as concealed/transient and automatically clears it after `clear_after_ms` if the clipboard has not been overwritten. | `app.clipboard_set_secret(password, 30000)` |
| `fn (app: App) clipboard_get() -> str` | `app.clipboard_get()` | Reads current plain text from the native OS system clipboard. | `clip := app.clipboard_get()` |
| `fn (app: App) monospace_fonts() -> []str` | `app.monospace_fonts()` | Enumerates installed system monospace font families (`NSFontManager` on macOS, `fc-list :spacing=mono` on Linux, `EnumFontFamiliesExW` on Windows). | `fonts := app.monospace_fonts()` |
| `fn (mut app: App) set_icon(icon_path: str)` | `app.set_icon(icon_path)` | Sets the application window and dock/taskbar icon from a `.png`, `.icns`, or `.ico` file. | `app.set_icon("assets/icon.png")` |
| `fn (mut app: App) set_tray(icon_path: str, tooltip: str = "")` | `app.set_tray(icon_path, tooltip)` | Configures a system tray / menu-bar status item (`NSStatusItem` on macOS, `GtkStatusIcon` on Linux, `Shell_NotifyIconW` on Windows). | `app.set_tray("assets/tray.png", "Flint")` |
| `fn (mut app: App) add_tray_item(title: str, event: str)` | `app.add_tray_item(title, event)` | Appends a menu item to the system tray context menu that emits `event` to `app.on(event)` when clicked (or `"-"` for a separator). | `app.add_tray_item("Quick Connect", "tray:connect")` |
| `fn (mut app: App) hide_window()` | `app.hide_window()` | Hides the native application window without terminating the process or system tray. | `app.hide_window()` |
| `fn (mut app: App) show_window()` | `app.show_window()` | Restores and focuses a hidden or minimized native application window. | `app.show_window()` |
| `fn (mut app: App) set_hide_on_close(enabled: bool)` | `app.set_hide_on_close(enabled)` | Intercepts the window close button (`windowShouldClose:` / `delete-event` / `WM_CLOSE`) to hide the window to the system tray instead of exiting. | `app.set_hide_on_close(true)` |
| `fn (mut app: App) add_menu_item(menu: str, title: str, shortcut: str, event: str)` | `app.add_menu_item(menu, title, shortcut, event)` | Adds a custom menu item with keyboard shortcut to the application menu bar that emits `event` to `app.on(event)` when triggered. | `app.add_menu_item("Shell", "New Tab", "t", "menu:new_tab")` |
| `fn (mut app: App) set_state(key: str, val: str)` | `app.set_state(key, val)` | Updates a reactive key-value state entry and broadcasts a `state:change` event (`{key, value}`) to the frontend. | `app.set_state("preset", "Warm Tape")` |
| `fn (app: App) get_state(key: str) -> str` | `app.get_state(key)` | Reads the current string value for `key` from the application's state store. | `p := app.get_state("preset")` |
| `fn (mut app: App) set_html(html: str)` | `app.set_html(html)` | Registers inline HTML content as `/index.html` on the `app://localhost/` virtual filesystem. | `app.set_html("<!doctype html><html><body><h1>Hello</h1></body></html>")` |
| `fn (mut app: App) embed_asset(path: str, content: str, mime: str = "")` | `app.embed_asset(path, content, mime)` | Embeds an in-memory asset (`.html`, `.css`, `.js`, `.svg`, etc.) into the `app://localhost/` virtual filesystem. | `app.embed_asset("/app.css", "body { background: #111; }", "text/css")` |
| `fn (mut app: App) serve_dir(dir_path: str)` | `app.serve_dir(dir_path)` | Mounts a local or bundled asset directory (e.g. `"./ui"`) under `app://localhost/`. | `app.serve_dir("./ui")` |
| `fn (mut app: App) eval(js: str)` | `app.eval(js)` | Asynchronously evaluates arbitrary JavaScript inside the active WebView on the main UI thread. | `app.eval("document.body.classList.add('ready')")` |
| `fn (mut app: App) set_title(title: str)` | `app.set_title(title)` | Updates the native window title. | `app.set_title("Mastering Studio — Active")` |
| `fn (mut app: App) set_size(width: i32, height: i32)` | `app.set_size(width, height)` | Resizes the native application window. | `app.set_size(1280, 800)` |
| `fn (mut app: App) set_min_size(min_w: i32, min_h: i32)` | `app.set_min_size(min_w, min_h)` | Sets the minimum resizable dimensions for the native window. | `app.set_min_size(640, 420)` |
| `fn (mut app: App) set_max_size(max_w: i32, max_h: i32)` | `app.set_max_size(max_w, max_h)` | Sets the maximum resizable dimensions for the native window. | `app.set_max_size(1920, 1080)` |
| `fn (mut app: App) set_frameless(frameless: bool)` | `app.set_frameless(frameless)` | Configures a frameless full-size content view window with CSS `--wails-draggable: drag` support. | `app.set_frameless(true)` |
| `fn (mut app: App) set_transparent(transparent: bool)` | `app.set_transparent(transparent)` | Enables or disables transparent window and WebView background rendering. | `app.set_transparent(true)` |
| `fn (mut app: App) set_dark_mode(dark: bool)` | `app.set_dark_mode(dark)` | Configures dark or light native window appearance. | `app.set_dark_mode(true)` |
| `fn (mut app: App) set_always_on_top(on_top: bool)` | `app.set_always_on_top(on_top)` | Floats the application window above standard desktop windows. | `app.set_always_on_top(true)` |
| `fn (mut app: App) fullscreen()` | `app.fullscreen()` | Toggles native fullscreen window mode. | `app.fullscreen()` |
| `fn (mut app: App) minimize()` | `app.minimize()` | Minimizes the native window to the dock or taskbar. | `app.minimize()` |
| `fn (mut app: App) maximize()` | `app.maximize()` | Zooms or maximizes the native window. | `app.maximize()` |
| `fn (mut app: App) center()` | `app.center()` | Centers the native window on the active screen. | `app.center()` |
| `fn (app: App) open_file_dialog(title: str = "Open File") -> str` | `app.open_file_dialog(title)` | Opens a native OS file picker dialog and returns the selected path (or `""` if cancelled). | `path := app.open_file_dialog("Select Audio Track")` |
| `fn (app: App) save_file_dialog(title: str = "Save File", default_name: str = "") -> str` | `app.save_file_dialog(title, default_name)` | Opens a native OS save-file dialog and returns the chosen destination path. | `dest := app.save_file_dialog("Export Preset", "studio.json")` |
| `fn (app: App) message_dialog(title: str, message: str)` | `app.message_dialog(title, message)` | Displays a modal native OS alert dialog. | `app.message_dialog("Export Complete", "Saved master preset.")` |
| `fn (mut app: App) call_rpc(method: str, params_json: str = "{}") -> str!` | `app.call_rpc(method, params_json)` | Directly invokes a bound RPC handler in-memory without opening a window (ideal for `cfire test` and `fuzz` blocks). | `res := try app.call_rpc("set_track", "{\"channel\":1}")` |
| `fn (mut app: App) dispatch_event(event: str, data_json: str = "{}")` | `app.dispatch_event(event, data_json)` | Dispatches a synthetic frontend event to `app.on(event)` listeners during headless testing. | `app.dispatch_event("fader:input", "{\"channel\":1}")` |
| `fn (app: App) resolve_asset_mime(path: str) -> str` | `app.resolve_asset_mime(path)` | Returns the MIME type the embedded asset server reports for `path` (useful in headless contract tests). | `mime := app.resolve_asset_mime("/index.html")` |
| `fn (app: App) emitted_events_count(event: str) -> i64` | `app.emitted_events_count(event)` | Returns the number of times `app.emit(event, ...)` has been triggered. | `n := app.emitted_events_count("meter:peak")` |
| `fn (app: App) last_emitted_payload(event: str) -> str` | `app.last_emitted_payload(event)` | Returns the most recent JSON payload emitted for `event`. | `last := app.last_emitted_payload("meter:peak")` |
| `fn (mut app: App) run_webview_test(timeout_ms: i32 = 3000) -> bool!` | `app.run_webview_test(timeout_ms)` | Boots an offscreen native `WKWebView`, executes the loaded page's JavaScript and RPC calls until `window.runtime.Quit()` or `timeout_ms`, and returns `true` if `Quit()` was reached. | `ok := try app.run_webview_test(3000)` |
| `fn (mut app: App) run()` | `app.run()` | Launches the native OS window and WebView event loop (`NSWindow`+`WKWebView` on macOS, `UIWindow`+`WKWebView` on iOS, `GtkWindow`+`WebKitGTK` on Linux). | `app.run()` |
| `fn (mut app: App) quit()` | `app.quit()` | Requests termination of the native WebView event loop. | `app.quit()` |
| `fn (mut app: App) close()` | `app.close()` | Deterministic RAII cleanup handle for `defer app.close()`. | `defer app.close()` |
| `fn clipboard_get() -> str` | `cfire::webui::clipboard_get()` | Reads plaintext from the system clipboard. | `t := webui.clipboard_get()` |
| `fn clipboard_set(text: str, concealed: bool = false, clear_after_ms: i64 = 0)` | `cfire::webui::clipboard_set(text, concealed, clear_after_ms)` | Writes plaintext to the system clipboard with optional auto-clear timeout. | `webui.clipboard_set("secret", true, 10000)` |
| `fn clipboard_set_secret(text: str, clear_after_ms: i64 = 30000)` | `cfire::webui::clipboard_set_secret(text, clear_after_ms)` | Writes sensitive secret to the clipboard and schedules automatic clearing. | `webui.clipboard_set_secret("pass")` |
| `fn monospace_fonts() -> []str` | `cfire::webui::monospace_fonts()` | Returns a list of platform-installed monospaced font family names. | `fonts := webui.monospace_fonts()` |

## 2. Semantic Diagnostic Codes

### `[cfire/RETIRED-API]` — API Retired in the 0.16 Uniform-API Migration

The 0.16 release (Q16) replaced free functions, sentinels and duplicate spellings with one form each: methods on values (`s.trim()`, `xs.sort()`, `x.abs()`), type-associated items (`i32.parse(s)`, `i32.MAX`, `math.PI`), `.len()`, `.is_empty()`, `make(T, ...)` construction, `connect` / `listen` verbs and `verify_peer: false`. Every retired form is a hard error with a fix-it; `cfire fix --only cfire/RETIRED-API` applies exactly these rewrites.

**Rejected (`.cf`):**
```cf
import "core:str" // ERROR [cfire/RETIRED-API]
fn main() {
    t := str.trim(" x ") // ERROR [cfire/RETIRED-API]
    n := t.len // ERROR [cfire/RETIRED-API]
}
```

**Accepted (`.cf`):**
```cf
fn main() {
    t := " x ".trim() // OK
    n := t.len() // OK
}
```

### `[cfire/INVALID-MAKE]` — Invalid `make(T, ...)` Arguments

`make` takes the type first (Q25). Slices take `len` and optional `cap` integers, maps and arrays take nothing, and a struct takes either nothing (default fields) or the arguments of its declared constructor `fn (T) make(...) -> T`.

**Rejected (`.cf`):**
```cf
type Q struct { a: i32 }
fn main() {
    v := make([]i32, 1, 2, 3) // ERROR [cfire/INVALID-MAKE]
    q := make(Q, 1) // ERROR [cfire/INVALID-MAKE]
}
```

**Accepted (`.cf`):**
```cf
type Q struct { a: i32 }
fn (Q) make(a: i32) -> Q { return Q{a: a} }
fn main() {
    v := make([]i32, 1, 2) // OK
    q := make(Q, 1) // OK
}
```

### `[cfire/INVALID-CONSTRUCTOR]` — Associated Function Other Than `make`

A receiver type with no binding (`fn (T) ...`) declares the type's constructor, and the only name it may have is `make` (Q25). Methods need a receiver binding.

**Rejected (`.cf`):**
```cf
type P struct { x: i32 }
fn (P) origin() -> P { return P{x: 0} } // ERROR [cfire/INVALID-CONSTRUCTOR]
```

**Accepted (`.cf`):**
```cf
type P struct { x: i32 }
fn (P) make(x: i32) -> P { return P{x: x} } // OK
fn (p: P) shifted() -> P { return P{x: p.x + 1} } // OK
```

### `[cfire/INVALID-RELEASE]` — `release` on a Shared Pointer

`o.release() -> *mut T` hands ownership to C (CF-181) and exists only on `own[T]`: a `shared[T]` may have other owners.

**Rejected (`.cf`):**
```cf
type B struct { v: i32 }
fn main() {
    s := make(shared[B], B{v: 1})
    p := s.release() // ERROR [cfire/INVALID-RELEASE]
}
```

**Accepted (`.cf`):**
```cf
type B struct { v: i32 }
fn main() {
    o := make(own[B], B{v: 1})
    p := o.release() // OK; adopt it back with own[B].from_raw(p)
}
```

### `[cfire/UNKNOWN-ARG-LABEL]` — Unknown Labeled Argument

A labeled argument (`verify_peer: false`) must name a parameter of the called function; labels place arguments by name and skipped parameters keep their defaults.

**Rejected (`.cf`):**
```cf
import "core:net/http"
fn main() -> void! {
    r := try http.get("https://x", verify: false) // ERROR [cfire/UNKNOWN-ARG-LABEL]
}
```

**Accepted (`.cf`):**
```cf
import "core:net/http"
fn main() -> void! {
    r := try http.get("https://x", verify_peer: false) // OK
}
```

### `[cfire/FIELD-NAME-CASE]` — Struct Field Is Not snake_case

Struct fields are snake_case like functions and locals (Q28); capitalization carries no meaning. This is a warning; `cfire fix` renames the field and every use across the workspace. Untagged JSON/CLI keys follow the field name, so add a `json:"..."` tag to keep an existing wire name.

**Rejected (`.cf`):**
```cf
type Doc struct {
    TopK: i32 // WARNING [cfire/FIELD-NAME-CASE]
}
```

**Accepted (`.cf`):**
```cf
type Doc struct {
    top_k: i32 `json:"TopK"` // OK
}
```

### `[cfire/MUT-CALLSITE]` — Missing Call-Site `mut` Marker

Functions declaring `mut param: T` mutate the caller's variable in-place (`T&`). Callers must write `mut arg` at the call site so mutation is visible when reading code.

**Rejected (`.cf`):**
```cf
fn bump(mut x: i32) { x += 1 }
fn main() {
    mut a := 10
    bump(a) // ERROR [cfire/MUT-CALLSITE]
}
```

**Accepted (`.cf`):**
```cf
fn bump(mut x: i32) { x += 1 }
fn main() {
    mut a := 10
    bump(mut a) // OK
}
```

### `[cfire/IMMUTABLE-MUT-ARG]` — Passing Immutable Binding as `mut` Argument

Bindings declared with `:=` or `x: T = ...` without `mut` are `const` by default and cannot be passed to `mut` parameters.

**Rejected (`.cf`):**
```cf
fn reset(mut v: i32) { v = 0 }
fn main() {
    val := 5
    reset(mut val) // ERROR [cfire/IMMUTABLE-MUT-ARG]
}
```

**Accepted (`.cf`):**
```cf
fn reset(mut v: i32) { v = 0 }
fn main() {
    mut val := 5
    reset(mut val) // OK
}
```

### `[cfire/MUT-RECEIVER]` — Calling `mut` Method on Immutable Receiver

Methods with a mutable receiver `fn (mut self: Counter) inc()` cannot be invoked on an immutable binding.

**Rejected (`.cf`):**
```cf
c := Counter{Value: 0}
c.inc() // ERROR [cfire/MUT-RECEIVER]
```

**Accepted (`.cf`):**
```cf
mut c := Counter{Value: 0}
c.inc() // OK
```

### `[cfire/MUT-ALIAS]` — Mutable Aliasing Across Call-Site Arguments (Law of Exclusivity)

A variable passed as a `mut` argument (or used as a `mut` method receiver) cannot also be passed as another argument in the same call.

**Rejected (`.cf`):**
```cf
fn transfer(mut from: i32, mut to: i32) { ... }
fn main() {
    mut acc := 100
    transfer(mut acc, mut acc) // ERROR [cfire/MUT-ALIAS]
}
```

**Accepted (`.cf`):**
```cf
fn transfer(mut from: i32, mut to: i32) { ... }
fn main() {
    mut a := 100
    mut b := 50
    transfer(mut a, mut b) // OK: distinct bindings
}
```

### `[cfire/USE-AFTER-MOVE]` — Use of Value After Explicit `move`

Once a variable is passed with `move x` (which lowers to `std::move(x)`), reading or passing `x` again in the same scope is rejected at compile time.

**Rejected (`.cf`):**
```cf
consume(move buf)
println("{}", buf.len()) // ERROR [cfire/USE-AFTER-MOVE]
```

**Accepted (`.cf`):**
```cf
println("{}", buf.len())
consume(move buf) // OK: moved on last use
```

### `[cfire/NON-EXHAUSTIVE-MATCH]` — Non-Exhaustive `match` Statement Over Algebraic `enum`

Every `match` over a `type ... enum` must either cover all declared variants or include a wildcard `_ => { ... }` arm.

**Rejected (`.cf`):**
```cf
match shape {
    Circle(r) => { ... }
    // ERROR [cfire/NON-EXHAUSTIVE-MATCH]: missing Rect
}
```

**Accepted (`.cf`):**
```cf
match shape {
    Circle(r) => { ... }
    Rect(w, h) => { ... }
}
```

### `[cfire/NON-DIVERGING-OR-BLOCK]` — Non-Diverging Error Recovery Block on Value Binding

When initializing or assigning a variable with `val := fallible() or err { ... }`, the error recovery block must definitely diverge with `return`, `panic(...)`, or `os.exit(...)` to avoid unwrapping an uninitialized value.

**Rejected (`.cf`):**
```cf
fn compute() -> i32 {
    // ERROR [cfire/NON-DIVERGING-OR-BLOCK]: handler does not diverge
    x := parse_num() or err {
        println("error: {}", err.message())
    }
    return x
}
```

**Accepted (`.cf`):**
```cf
fn compute() -> i32 {
    x := parse_num() or err {
        println("error: {}", err.message())
        return 0
    }
    return x
}
```

### `[cfire/UNREACHABLE-MATCH-ARM]` — Unreachable `match` Arm Shadowed by Earlier Pattern

Every arm in a `match` statement or expression must be reachable. Arms appearing after a wildcard `_`, duplicate enum/literal patterns, or slice patterns already subsumed by a broader prefix/suffix pattern (e.g., `[first, ..mid, last]` after `[head, ..tail]`) are rejected at compile time.

**Rejected (`.cf`):**
```cf
match items {
    [head, ..tail] => { return head }
    [first, ..mid, last] => { return last } // ERROR [cfire/UNREACHABLE-MATCH-ARM]
    _ => { return "" }
}
```

**Accepted (`.cf`):**
```cf
match items {
    [] => { return "empty" }
    [only] => { return only }
    [first, ..mid, last] => { return first + last } // OK: size >= 2 is reachable
}
```

### `[cfire/MOVE-CALLSITE]` — Missing Call-Site `move` Marker

Functions declaring `move param: T` take ownership of the caller's argument (`std::move(arg)`). Callers must write `move arg` at the call site so ownership transfer is explicit.

**Rejected (`.cf`):**
```cf
fn consume(move buf: []byte) { ... }
fn main() {
    data := []byte("payload")
    consume(data) // ERROR [cfire/MOVE-CALLSITE]
}
```

**Accepted (`.cf`):**
```cf
fn consume(move buf: []byte) { ... }
fn main() {
    data := []byte("payload")
    consume(move data) // OK
}
```

### `[cfire/OPTIONAL-OR-ERR]` — Binding Error Variable `or err` on Optional `T?`

Optional values (`T?`, lowered to `std::optional<T>`) represent absence (`none`), not an error (`cfire::Error`). Use `or fallback` or a parameterless `or { ... }` block on `T?`, and reserve `or err { ... }` for fallible `T!` (`std::expected<T, cfire::Error>`) results.

**Rejected (`.cf`):**
```cf
opt := "42".parse_i64() or err {
    return 0 // ERROR [cfire/OPTIONAL-OR-ERR]
}
```

**Accepted (`.cf`):**
```cf
opt := "42".parse_i64() or {
    return 0 // OK
}
```

### `[cfire/USE-AFTER-FREE]` — Use of a Slice View After Owner Invalidation

Slice views (`s[a..b]`, `xs[a..b]`) track their root owner variable. If the owner is moved (`move x`), reassigned (`x = ...`), or structurally mutated by a reallocating container method (`.push()`, `.pop()`, `.insert()`, `.remove()`, `.clear()`), every view taken before is invalidated, and reading it is rejected.

**Rejected (`.cf`):**
```cf
mut nums: []i32 = [10, 20, 30]
view := nums[0..2]
nums.push(40) // may reallocate 'nums'
println("{}", view[0]) // ERROR [cfire/USE-AFTER-FREE]
```

**Accepted (`.cf`):**
```cf
mut nums: []i32 = [10, 20, 30]
nums.push(40)
view := nums[0..2] // view taken after the mutation
println("{}", view[0]) // OK
```

### `[cfire/DANGLING-POINTER]` — Inner-Scope Address Escaping to an Outer Pointer (retired with raw pointers, CF-189)

Raw pointers are not cfire syntax any more (CF-189), so no pointer can outlive its target; the check remains for code that still reaches it. Keep an index, the value itself, or `own[T]` instead.

**Rejected (`.cf`):**
```cf
mut p := &x // ERROR [cfire/RAW-POINTER]: raw pointers exist only in C++ interop
```

**Accepted (`.cf`):**
```cf
mut values: []i32 = [1, 2]
mut at := 0 // an index instead of a pointer
values[at] = 42 // OK
```

### `[cfire/DANGLING-RETURN]` — Returning a Pointer to Function-Local Storage (retired with raw pointers, CF-189)

Functions return values (`T`, `str`, `[]T`) or `own[T]`; a returned `str` or `[]T` is a copy of any view, so nothing returned can borrow a local (Q36, CF-189).

**Rejected (`.cf`):**
```cf
fn bad() -> *i32 { // ERROR [cfire/RAW-POINTER]
    local := 99
    return &local
}
```

**Accepted (`.cf`):**
```cf
fn good() -> i32 {
    local := 99
    return local // OK: returned by value
}
```

### `[cfire/ITERATOR-INVALIDATION]` — Structural Mutation or Move of Collection During `for` Iteration

While iterating over a collection in `for x in col` or `for i, x in col`, structurally mutating (`col.push(...)`, `col.clear()`), reassigning (`col = ...`), or moving (`move col`) the collection is rejected at compile time.

**Rejected (`.cf`):**
```cf
mut items := [1, 2, 3]
for x in items {
    items.push(x * 2) // ERROR [cfire/ITERATOR-INVALIDATION]
}
```

**Accepted (`.cf`):**
```cf
items := [1, 2, 3]
mut doubled: []i32 = []
for x in items {
    doubled.push(x * 2) // OK: mutating separate collection
}
```

### `[cfire/NULL-DEREF]` — Member Access on an Optional Variable That May Be `none`

A variable of type `T?` (`own[T]?`, `Session?`) is tracked across branches. Reading a field or calling a method on it is rejected until the code proves it holds a value: `if s != none { ... }`, an early `return` when it is `none`, `assert(s != none)`, or `if v := s { ... }`. (cfire values are otherwise never null: there are no raw pointers, CF-189.)

**Rejected (`.cf`):**
```cf
type Session struct { id: i32 }
fn find(id: i32) -> Session? {
    return none
}
s := find(1)
println("{}", s.id) // ERROR [cfire/NULL-DEREF]
```

**Accepted (`.cf`):**
```cf
type Session struct { id: i32 }
fn find(id: i32) -> Session? {
    return none
}
s := find(1)
if s != none {
    println("{}", s.id) // OK: narrowed to present
}
```

### `[cfire/INDEX-OUT-OF-BOUNDS]` — Compile-Time Out-of-Bounds or Negative Constant Sequence Index

Negative integer literal indices (`arr[-1]`) on sequences and non-negative constant indices `>= N` on fixed-size arrays (`[N]T`) or array literals are rejected at compile time. Dynamic indices are verified at runtime via `cfire::index_at`.

**Rejected (`.cf`):**
```cf
arr: [3]i32 = [10, 20, 30]
println("{}", arr[3]) // ERROR [cfire/INDEX-OUT-OF-BOUNDS]
```

**Accepted (`.cf`):**
```cf
arr: [3]i32 = [10, 20, 30]
println("{}", arr[2]) // OK
```

### `[cfire/DIVIDE-BY-ZERO]` — Compile-Time Division or Modulo by Constant Zero

Dividing or taking modulo by a constant zero literal (`x / 0`, `x % 0`, `x /= 0`, `x %= 0`) is rejected at compile time.

**Rejected (`.cf`):**
```cf
x := 100 / 0 // ERROR [cfire/DIVIDE-BY-ZERO]
```

**Accepted (`.cf`):**
```cf
denom := 4
x := 100 / denom // OK
```

### `[cfire/SQL-INJECTION-RISK]` — Dynamic String Concatenation or Interpolation in SQL Query

Passing a dynamically concatenated or interpolated string (`"SELECT ... " + input`) to `db.exec` or `db.query` risks SQL injection. Pass a static SQL string with `?1`, `?2` parameter placeholders instead.

**Rejected (`.cf`):**
```cf
try db.exec("DELETE FROM users WHERE name = '" + user_name + "'") // WARNING [cfire/SQL-INJECTION-RISK]
```

**Accepted (`.cf`):**
```cf
try db.exec("DELETE FROM users WHERE name = ?1", user_name) // OK: parameterized binding
```

### `[cfire/SHELL-INJECTION-RISK]` — Dynamic String Concatenation or Interpolation in Shell Command

Passing a dynamically concatenated or interpolated string (`"ls " + user_arg`) to `os.exec` or `os.exec_output` invokes `/bin/sh -c` and risks command injection. Use `os.command(binary, args)` to spawn subprocesses without shell interpretation.

**Rejected (`.cf`):**
```cf
out := try os.exec("cat " + untrusted_path) // WARNING [cfire/SHELL-INJECTION-RISK]
```

**Accepted (`.cf`):**
```cf
res := try os.command("cat", [untrusted_path]) // OK: direct posix_spawnp argv
```

### `[cfire/RETURN-IN-DEFER]` — `return` Statement Inside `defer { ... }` Block

`defer { ... }` blocks lower to a C++ RAII scope-exit destructor (`struct _CfireDefer { ~_CfireDefer() { ... } }`) and cannot return a value to the enclosing function.

**Rejected (`.cf`):**
```cf
fn process() -> i32 {
    defer {
        return 0 // ERROR [cfire/RETURN-IN-DEFER]
    }
    return 1
}
```

**Accepted (`.cf`):**
```cf
fn process() -> i32 {
    defer {
        cleanup()
    }
    return 1 // OK: return from enclosing function
}
```

### `[cfire/INVALID-STRUCT-TAG]` — Duplicate or Malformed Struct Reflection Tag

Struct reflection tags (`json:"..."`, `sql:"..."`, `db:"..."`, `cli:"..."`, `default:"..."`) are validated at compile time. Duplicate `json`, `sql`/`db`, or `cli` flag names across fields of the same struct, or `default:"..."` literals that cannot parse into the field's numeric or boolean type, are rejected at compile time.

**Rejected (`.cf`):**
```cf
type Config struct {
    Port: i64 `json:"port" default:"not_a_number"`
    Alt:  i64 `json:"port"` // ERROR [cfire/INVALID-STRUCT-TAG]
}
```

**Accepted (`.cf`):**
```cf
type Config struct {
    Port: i64 `json:"port" default:"8080"`
    Alt:  i64 `json:"alt_port"` // OK
}
```

### `[cfire/REDUNDANT-TRY]` — Redundant `try` on Non-Fallible Expression

`try` propagates errors from fallible `T!` (`std::expected<T, cfire::Error>`) expressions. Applying `try` to a plain value, literal, or non-fallible function call has no effect and is rejected at compile time.

**Rejected (`.cf`):**
```cf
fn add(a: i32, b: i32) -> i32 { return a + b }
fn main() {
    x := try add(1, 2) // ERROR [cfire/REDUNDANT-TRY]
}
```

**Accepted (`.cf`):**
```cf
fn add(a: i32, b: i32) -> i32 { return a + b }
fn main() {
    x := add(1, 2) // OK
}
```

### `[cfire/TRY-ON-OPTIONAL]` — Using `try` on Optional `T?` Instead of `or`

`try` only propagates fallible `T!` errors. Optional `T?` values represent absence (`none`) without an error payload and must be unwrapped using `or fallback` or `or { ... }`.

**Rejected (`.cf`):**
```cf
fn main() {
    n := try "42".parse_i64() // ERROR [cfire/TRY-ON-OPTIONAL]
}
```

**Accepted (`.cf`):**
```cf
fn main() {
    n := "42".parse_i64() or 0 // OK
}
```

### `[cfire/TRY-IN-NON-FALLIBLE-FN]` — `try` Inside Non-Fallible Function

Functions that use `try` (other than `main` and `test`/`fuzz`/`bench` blocks) must declare a fallible `T!` or `void!` return type so propagated errors can be returned to the caller.

**Rejected (`.cf`):**
```cf
fn load() -> str {
    return try fs.read_text("config.json") // ERROR [cfire/TRY-IN-NON-FALLIBLE-FN]
}
```

**Accepted (`.cf`):**
```cf
fn load() -> str! {
    return try fs.read_text("config.json") // OK
}
```

### `[cfire/TRY-IN-DEFER]` — `try` Inside `defer` Block

`defer` blocks run inside an RAII scope-exit destructor and cannot propagate errors out of the enclosing function with `try`. Handle errors explicitly with `or` inside `defer`.

**Rejected (`.cf`):**
```cf
fn write_log(mut f: fs.File) -> void! {
    defer {
        try f.flush() // ERROR [cfire/TRY-IN-DEFER]
    }
}
```

**Accepted (`.cf`):**
```cf
fn write_log(mut f: fs.File) -> void! {
    defer {
        f.flush() or err { eprintln("flush failed: {}", err.message()) } // OK
    }
}
```

### `[cfire/INVALID-TRY-CONTEXT]` — `try` Inside Short-Circuit Operand or `for` Condition

Hoisting `try` before a statement would eagerly evaluate the right-hand side of `&&` / `||` or evaluate a `for <cond> { ... }` loop condition only once before the loop. Hoist the `try` into a separate statement or use `or fallback`.

**Rejected (`.cf`):**
```cf
if ready && (try check_remote()) { // ERROR [cfire/INVALID-TRY-CONTEXT]
}
```

**Accepted (`.cf`):**
```cf
if ready {
    if try check_remote() { ... } // OK
}
```

### `[cfire/REDUNDANT-OR]` — Redundant `or` on Non-Fallible, Non-Optional Expression

`or` provides fallback values or diverging recovery blocks for fallible `T!` and optional `T?` expressions. Using `or` on a plain value or non-fallible call has no effect and is rejected at compile time.

**Rejected (`.cf`):**
```cf
x := (1 + 2) or 0 // ERROR [cfire/REDUNDANT-OR]
```

**Accepted (`.cf`):**
```cf
x := 1 + 2 // OK
```

### `[cfire/INVALID-OR-BLOCK-CONTEXT]` — Block `or { ... }` Inside Short-Circuit Operand or `for` Condition

Block-form `or { ... }` cannot be hoisted out of the right-hand side of `&&` / `||` or a `for <cond> { ... }` condition without breaking short-circuit or per-iteration evaluation. Use value fallback `or fallback` or hoist into a statement.

**Rejected (`.cf`):**
```cf
if ok && (parse_flag() or { return }) { // ERROR [cfire/INVALID-OR-BLOCK-CONTEXT]
}
```

**Accepted (`.cf`):**
```cf
if ok && (parse_flag() or false) { // OK
}
```

### `[cfire/REDUNDANT-CALLSITE-INTENT]` — Redundant `mut` Annotation on a Read-Only Parameter

Call-site `mut` annotations are only valid when the target parameter is declared `mut`. Writing `mut` on a read-only parameter misleads readers into thinking the call mutates the argument.

**Rejected (`.cf`):**
```cf
fn show(x: i32) { println("{}", x) }
fn main() {
    mut a := 10
    show(mut a) // ERROR [cfire/REDUNDANT-CALLSITE-INTENT]
}
```

**Accepted (`.cf`):**
```cf
fn show(x: i32) { println("{}", x) }
fn main() {
    mut a := 10
    show(a) // OK
}
```

### `[cfire/INVALID-MOVE]` — No-Op `move` on Rvalue or Scalar `Copy` Variable

`move` transfers ownership of an owned variable (`str`, `[]T`, `map[K]V`, `own[T]`, structs/enums). Applying `move` to a temporary rvalue/literal (`move 42`, `move foo()`) or to a scalar `Copy` primitive (`i32`, `f64`, `bool`) when not required by a `move` parameter has no effect and is rejected.

**Rejected (`.cf`):**
```cf
mut count := 10
y := move count // ERROR [cfire/INVALID-MOVE]
```

**Accepted (`.cf`):**
```cf
mut buf := "hello"
y := move buf // OK: transfers ownership of non-Copy 'str'
```

### `[cfire/BREAK-OUTSIDE-LOOP]` — `break` or `continue` Outside of a `for` Loop

`break` and `continue` are only valid inside the body of a `for` loop (and cannot cross closure or `defer` boundaries).

**Rejected (`.cf`):**
```cf
fn helper() {
    break // ERROR [cfire/BREAK-OUTSIDE-LOOP]
}
```

**Accepted (`.cf`):**
```cf
fn helper() {
    for i in 0..10 {
        if i == 5 { break } // OK
    }
}
```

### `[cfire/NO-EFFECT-STMT]` — Pure Expression Statement With No Side Effects

Standalone expression statements must perform an effect (such as a function/method call, `try`, `or { ... }`, `match`, `break`, or `continue`). Pure expressions like `x == 1` or `a + b` as statements discard their result and are rejected.

**Rejected (`.cf`):**
```cf
mut x := 0
x == 1 // ERROR [cfire/NO-EFFECT-STMT]: did you mean '=' assignment?
```

**Accepted (`.cf`):**
```cf
mut x := 0
x = 1 // OK
```


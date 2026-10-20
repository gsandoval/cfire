#pragma once
// cfire_prelude.hpp — Zero-overhead C++23 Runtime Prelude for cfire (.cf)

#include <algorithm>
#include <array>
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
#include <print>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

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

namespace cfire {

// --- Error Type for T! (std::expected<T, cfire::Error>) ---
class Error {
public:
    Error(std::string msg) : msg_(std::move(msg)) {}
    Error(const char* msg) : msg_(msg) {}
    [[nodiscard]] auto message() const noexcept -> std::string_view { return msg_; }
private:
    std::string msg_;
};

[[nodiscard]] inline auto make_error(std::string msg) -> std::unexpected<Error> {
    return std::unexpected<Error>(Error(std::move(msg)));
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
[[nodiscard]] inline auto slice(std::string_view sv, std::size_t start, std::size_t end) noexcept -> std::string_view {
    if (start > sv.size()) start = sv.size();
    if (end > sv.size()) end = sv.size();
    if (end < start) end = start;
    return sv.substr(start, end - start);
}

template <typename T>
[[nodiscard]] inline auto slice(const std::vector<T>& vec, std::size_t start, std::size_t end) noexcept -> std::span<const T> {
    if (start > vec.size()) start = vec.size();
    if (end > vec.size()) end = vec.size();
    if (end < start) end = start;
    return std::span<const T>(vec.data() + start, end - start);
}

template <typename T>
[[nodiscard]] inline auto slice(std::span<const T> sp, std::size_t start, std::size_t end) noexcept -> std::span<const T> {
    if (start > sp.size()) start = sp.size();
    if (end > sp.size()) end = sp.size();
    if (end < start) end = start;
    return sp.subspan(start, end - start);
}

template <typename T, std::size_t N>
[[nodiscard]] inline auto slice(const std::array<T, N>& arr, std::size_t start, std::size_t end) noexcept -> std::span<const T> {
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
inline void assert_true(bool cond, std::string_view msg = "assertion failed") {
    if (!cond) {
        std::println(stderr, "panic: {}", msg);
        std::abort();
    }
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
    [[nodiscard]] operator const char*() const noexcept { return ptr_; }
    [[nodiscard]] auto get() const noexcept -> const char* { return ptr_; }

private:
    std::array<char, 256> small_buf_{};
    std::string heap_str_{};
    const char* ptr_ = nullptr;
};

[[nodiscard]] inline auto c_str(const std::string& s) noexcept -> const char* {
    return s.c_str();
}
[[nodiscard]] inline auto c_str(const std::filesystem::path& p) noexcept -> const char* {
    return p.c_str();
}
[[nodiscard]] inline auto c_str(const char* s) noexcept -> const char* {
    return s;
}
[[nodiscard]] inline auto c_str(std::string_view sv) noexcept -> CStrViewHolder {
    return CStrViewHolder(sv);
}
[[nodiscard]] inline auto path_with_ext(std::filesystem::path p, std::string_view ext) -> std::filesystem::path {
    p.replace_extension(ext);
    return p;
}
[[nodiscard]] inline auto to_bytes(std::string_view sv) noexcept -> std::span<const std::byte> {
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

} // namespace cfire

using cfire::str;

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

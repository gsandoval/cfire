#pragma once
#include "../cfire_prelude.hpp"

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <expected>
#include <span>
#include <string>
#include <string_view>
#include <vector>
#if !defined(_WIN32)
#include <dlfcn.h>
#endif
#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif

#ifdef CFIRE_USE_SQLITE
#include <sqlite3.h>
namespace cfire::sqlite {

namespace detail {

[[nodiscard]] inline auto extract_f32_span(sqlite3_value* val, std::vector<float>& scratch) noexcept -> std::span<const float> {
    int type = sqlite3_value_type(val);
    if (type == SQLITE_BLOB) {
        const void* ptr = sqlite3_value_blob(val);
        int bytes = sqlite3_value_bytes(val);
        if (!ptr || bytes <= 0 || (bytes % static_cast<int>(sizeof(float))) != 0) return {};
        return std::span<const float>( static_cast<const float*>(ptr), static_cast<std::size_t>(bytes) / sizeof(float) );
    }
    if (type == SQLITE_TEXT) {
        const char* txt = reinterpret_cast<const char*>(sqlite3_value_text(val));
        int len = sqlite3_value_bytes(val);
        if (!txt || len <= 0) return {};
        scratch.clear();
        std::string_view sv(txt, static_cast<std::size_t>(len));
        std::size_t i = 0;
        while (i < sv.size()) {
            while (i < sv.size() && (sv[i] == '[' || sv[i] == ']' || sv[i] == ',' || sv[i] == ' ' || sv[i] == '\t' || sv[i] == '\n' || sv[i] == '\r')) ++i;
            if (i >= sv.size()) break;
            std::size_t start = i;
            while (i < sv.size() && sv[i] != ',' && sv[i] != ']' && sv[i] != ' ' && sv[i] != '\t' && sv[i] != '\n' && sv[i] != '\r') ++i;
            std::string tok(sv.substr(start, i - start));
            char* endptr = nullptr;
            float f = std::strtof(tok.c_str(), &endptr);
            if (endptr && endptr != tok.c_str()) {
                scratch.push_back(f);
            }
        }
        return std::span<const float>(scratch.data(), scratch.size());
    }
    return {};
}

inline void sql_vec_cosine_similarity(sqlite3_context* ctx, int argc, sqlite3_value** argv) noexcept {
    if (argc < 2) { sqlite3_result_double(ctx, 0.0); return; }
    std::vector<float> s0, s1;
    auto a = extract_f32_span(argv[0], s0);
    auto b = extract_f32_span(argv[1], s1);
    std::size_t n = std::min(a.size(), b.size());
    if (n == 0) { sqlite3_result_double(ctx, 0.0); return; }

    double dot = 0.0;
    double na = 0.0;
    double nb = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
        double va = static_cast<double>(a[i]);
        double vb = static_cast<double>(b[i]);
        dot += va * vb;
        na += va * va;
        nb += vb * vb;
    }
    double denom = std::sqrt(na) * std::sqrt(nb);
    sqlite3_result_double(ctx, denom > 1e-12 ? (dot / denom) : 0.0);
}

inline void sql_vec_cosine_distance(sqlite3_context* ctx, int argc, sqlite3_value** argv) noexcept {
    if (argc < 2) { sqlite3_result_double(ctx, 1.0); return; }
    std::vector<float> s0, s1;
    auto a = extract_f32_span(argv[0], s0);
    auto b = extract_f32_span(argv[1], s1);
    std::size_t n = std::min(a.size(), b.size());
    if (n == 0) { sqlite3_result_double(ctx, 1.0); return; }

    double dot = 0.0;
    double na = 0.0;
    double nb = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
        double va = static_cast<double>(a[i]);
        double vb = static_cast<double>(b[i]);
        dot += va * vb;
        na += va * va;
        nb += vb * vb;
    }
    double denom = std::sqrt(na) * std::sqrt(nb);
    double sim = denom > 1e-12 ? (dot / denom) : 0.0;
    sqlite3_result_double(ctx, 1.0 - sim);
}

inline void sql_vec_l2_distance(sqlite3_context* ctx, int argc, sqlite3_value** argv) noexcept {
    if (argc < 2) { sqlite3_result_double(ctx, 0.0); return; }
    std::vector<float> s0, s1;
    auto a = extract_f32_span(argv[0], s0);
    auto b = extract_f32_span(argv[1], s1);
    std::size_t n = std::min(a.size(), b.size());
    if (n == 0) { sqlite3_result_double(ctx, 0.0); return; }

    double sum_sq = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
        double d = static_cast<double>(a[i]) - static_cast<double>(b[i]);
        sum_sq += d * d;
    }
    sqlite3_result_double(ctx, std::sqrt(sum_sq));
}

inline void sql_vec_dot(sqlite3_context* ctx, int argc, sqlite3_value** argv) noexcept {
    if (argc < 2) { sqlite3_result_double(ctx, 0.0); return; }
    std::vector<float> s0, s1;
    auto a = extract_f32_span(argv[0], s0);
    auto b = extract_f32_span(argv[1], s1);
    std::size_t n = std::min(a.size(), b.size());
    double dot = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
        dot += static_cast<double>(a[i]) * static_cast<double>(b[i]);
    }
    sqlite3_result_double(ctx, dot);
}

inline void sql_vec_dims(sqlite3_context* ctx, int argc, sqlite3_value** argv) noexcept {
    if (argc < 1) { sqlite3_result_int(ctx, 0); return; }
    std::vector<float> s0;
    auto a = extract_f32_span(argv[0], s0);
    sqlite3_result_int(ctx, static_cast<int>(a.size()));
}

inline void register_vector_functions(sqlite3* db) noexcept {
    if (!db) return;
    int flags = SQLITE_UTF8 | SQLITE_DETERMINISTIC;
    sqlite3_create_function_v2(db, "vec_cosine_similarity", 2, flags, nullptr, sql_vec_cosine_similarity, nullptr, nullptr, nullptr);
    sqlite3_create_function_v2(db, "vec_cosine_distance",   2, flags, nullptr, sql_vec_cosine_distance,   nullptr, nullptr, nullptr);
    sqlite3_create_function_v2(db, "vec_l2_distance",       2, flags, nullptr, sql_vec_l2_distance,       nullptr, nullptr, nullptr);
    sqlite3_create_function_v2(db, "vec_dot",               2, flags, nullptr, sql_vec_dot,               nullptr, nullptr, nullptr);
    sqlite3_create_function_v2(db, "vec_dims",              1, flags, nullptr, sql_vec_dims,              nullptr, nullptr, nullptr);
}

} // namespace detail

template <typename T>
auto map_row(sqlite3_stmt* stmt) -> T;

inline void bind_arg(sqlite3_stmt* stmt, int idx, std::string_view v) {
    sqlite3_bind_text(stmt, idx, v.data(), static_cast<int>(v.size()), SQLITE_TRANSIENT);
}
inline void bind_arg(sqlite3_stmt* stmt, int idx, const char* v) {
    bind_arg(stmt, idx, std::string_view(v ? v : ""));
}
inline void bind_arg(sqlite3_stmt* stmt, int idx, const std::string& v) {
    bind_arg(stmt, idx, std::string_view(v));
}
template <std::integral T>
    requires (!std::is_same_v<std::decay_t<T>, bool>)
inline void bind_arg(sqlite3_stmt* stmt, int idx, T v) {
    if constexpr (sizeof(T) <= 4) {
        sqlite3_bind_int(stmt, idx, static_cast<int>(v));
    } else {
        sqlite3_bind_int64(stmt, idx, static_cast<sqlite3_int64>(v));
    }
}
inline void bind_arg(sqlite3_stmt* stmt, int idx, double v) {
    sqlite3_bind_double(stmt, idx, v);
}
inline void bind_arg(sqlite3_stmt* stmt, int idx, float v) {
    sqlite3_bind_double(stmt, idx, static_cast<double>(v));
}
template <typename B>
    requires std::is_same_v<std::decay_t<B>, bool>
inline void bind_arg(sqlite3_stmt* stmt, int idx, B v) {
    sqlite3_bind_int(stmt, idx, v ? 1 : 0);
}
inline void bind_arg(sqlite3_stmt* stmt, int idx, const std::vector<std::uint8_t>& v) {
    sqlite3_bind_blob(stmt, idx, v.empty() ? "" : reinterpret_cast<const void*>(v.data()),
        static_cast<int>(v.size()), SQLITE_TRANSIENT);
}
inline void bind_arg(sqlite3_stmt* stmt, int idx, const std::vector<float>& v) {
    sqlite3_bind_blob(stmt, idx, v.empty() ? "" : reinterpret_cast<const void*>(v.data()),
        static_cast<int>(v.size() * sizeof(float)), SQLITE_TRANSIENT);
}

class Conn {
public:
    explicit Conn(sqlite3* db) : db_(db) {}
    ~Conn() {
        close();
    }
    Conn(Conn&& other) noexcept : db_(std::exchange(other.db_, nullptr)) {}
    auto operator=(Conn&& other) noexcept -> Conn& {
        if (this != &other) {
            close();
            db_ = std::exchange(other.db_, nullptr);
        }
        return *this;
    }

    void close() noexcept {
        if (db_) {
            sqlite3_close(db_);
            db_ = nullptr;
        }
    }

    template <typename... Args>
    [[nodiscard]] auto exec(std::string_view sql, const Args&... args) -> std::expected<void, cfire::Error> {
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(db_, sql.data(), static_cast<int>(sql.size()), &stmt, nullptr) != SQLITE_OK) {
            return cfire::make_error(std::format("sqlite prepare error: {}", sqlite3_errmsg(db_)));
        }
        auto guard = cfire::scope_exit([stmt] { sqlite3_finalize(stmt); });
        int idx = 1;
        (bind_arg(stmt, idx++, args), ...);
        int rc = sqlite3_step(stmt);
        if (rc != SQLITE_DONE && rc != SQLITE_ROW) {
            return cfire::make_error(std::format("sqlite step error: {}", sqlite3_errmsg(db_)));
        }
        return {};
    }

    template <typename T, typename... Args>
    [[nodiscard]] auto query(std::string_view sql, const Args&... args) -> std::expected<std::vector<T>, cfire::Error> {
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(db_, sql.data(), static_cast<int>(sql.size()), &stmt, nullptr) != SQLITE_OK) {
            return cfire::make_error(std::format("sqlite prepare error: {}", sqlite3_errmsg(db_)));
        }
        auto guard = cfire::scope_exit([stmt] { sqlite3_finalize(stmt); });
        int idx = 1;
        (bind_arg(stmt, idx++, args), ...);
        std::vector<T> rows;
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            rows.push_back(map_row<T>(stmt));
        }
        return rows;
    }

    template <typename F>
    [[nodiscard]] auto tx(F&& fn) -> std::expected<void, cfire::Error> {
        CFIRE_TRY(exec("BEGIN IMMEDIATE"));
        auto res = fn(*this);
        if (!res.has_value()) {
            (void)exec("ROLLBACK");
            return res;
        }
        CFIRE_TRY(exec("COMMIT"));
        return {};
    }

    [[nodiscard]] auto load_extension(std::string_view path, std::string_view entry_point = "") -> std::expected<void, cfire::Error> {
#if !defined(PLATFORM_WEB) && !defined(PLATFORM_IOS) && !(defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE)
        if (!db_) return cfire::make_error("sqlite connection is closed");
        using EnableExtFn = int(*)(sqlite3*, int);
        using LoadExtFn = int(*)(sqlite3*, const char*, const char*, char**);
        auto fn_enable = reinterpret_cast<EnableExtFn>(::dlsym(RTLD_DEFAULT, "sqlite3_enable_load_extension"));
        auto fn_load = reinterpret_cast<LoadExtFn>(::dlsym(RTLD_DEFAULT, "sqlite3_load_extension"));
        if (!fn_enable || !fn_load) {
            return cfire::make_error("system libsqlite3 does not expose sqlite3_load_extension; use built-in vec_cosine_similarity / vec_l2_distance functions");
        }
        fn_enable(db_, 1);
        std::string p_str(path);
        std::string ep_str(entry_point);
        char* err_msg = nullptr;
        int rc = fn_load(db_, p_str.c_str(), ep_str.empty() ? nullptr : ep_str.c_str(), &err_msg);
        if (rc != SQLITE_OK) {
            std::string err = err_msg ? err_msg : "failed to load sqlite extension";
            if (err_msg) sqlite3_free(err_msg);
            return cfire::make_error(std::format("sqlite load_extension('{}') failed: {}", path, err));
        }
        return {};
#else
        (void)path;
        (void)entry_point;
        return cfire::make_error("dynamic SQLite extensions are disabled on this target platform; use built-in vec_cosine_similarity / vec_l2_distance functions");
#endif
    }

private:
    sqlite3* db_ = nullptr;
};

[[nodiscard]] inline auto open(const std::filesystem::path& p) -> std::expected<Conn, cfire::Error> {
    std::filesystem::path resolved = p;
#if defined(PLATFORM_IOS) || (defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE)
    if (resolved.is_relative() && !resolved.empty() && resolved.string() != ":memory:") {
        if (const char* home = std::getenv("HOME")) {
            std::error_code ec;
            auto docs = std::filesystem::path(home) / "Documents";
            std::filesystem::create_directories(docs, ec);
            resolved = docs / resolved;
            if (resolved.has_parent_path()) {
                std::filesystem::create_directories(resolved.parent_path(), ec);
            }
        }
    }
#endif
    sqlite3* raw = nullptr;
    if (sqlite3_open(resolved.c_str(), &raw) != SQLITE_OK) {
        std::string err = raw ? sqlite3_errmsg(raw) : "unknown sqlite open error";
        if (raw) sqlite3_close(raw);
        return cfire::make_error(err);
    }
    detail::register_vector_functions(raw);
    Conn c(raw);
    (void)c.exec("PRAGMA journal_mode=WAL;");
    return c;
}

[[nodiscard]] inline auto col_str(sqlite3_stmt* stmt, int col) -> std::string {
    const unsigned char* txt = sqlite3_column_text(stmt, col);
    return txt ? reinterpret_cast<const char*>(txt) : "";
}
[[nodiscard]] inline auto col_u64(sqlite3_stmt* stmt, int col) -> std::uint64_t {
    return static_cast<std::uint64_t>(sqlite3_column_int64(stmt, col));
}
[[nodiscard]] inline auto col_i64(sqlite3_stmt* stmt, int col) -> std::int64_t {
    return static_cast<std::int64_t>(sqlite3_column_int64(stmt, col));
}
[[nodiscard]] inline auto col_i32(sqlite3_stmt* stmt, int col) -> std::int32_t {
    return static_cast<std::int32_t>(sqlite3_column_int(stmt, col));
}
[[nodiscard]] inline auto col_f64(sqlite3_stmt* stmt, int col) -> double {
    return sqlite3_column_double(stmt, col);
}
[[nodiscard]] inline auto col_f32(sqlite3_stmt* stmt, int col) -> float {
    return static_cast<float>(sqlite3_column_double(stmt, col));
}
[[nodiscard]] inline auto col_bool(sqlite3_stmt* stmt, int col) -> bool {
    return sqlite3_column_int(stmt, col) != 0;
}
[[nodiscard]] inline auto col_bytes(sqlite3_stmt* stmt, int col) -> std::vector<std::uint8_t> {
    const void* blob = sqlite3_column_blob(stmt, col);
    int n = sqlite3_column_bytes(stmt, col);
    if (!blob || n <= 0) return {};
    const auto* p = static_cast<const std::uint8_t*>(blob);
    return std::vector<std::uint8_t>(p, p + n);
}
[[nodiscard]] inline auto col_f32_vec(sqlite3_stmt* stmt, int col) -> std::vector<float> {
    const void* blob = sqlite3_column_blob(stmt, col);
    int n = sqlite3_column_bytes(stmt, col);
    if (!blob || n <= 0) return {};
    std::size_t count = static_cast<std::size_t>(n) / sizeof(float);
    std::vector<float> out(count);
    std::memcpy(out.data(), blob, count * sizeof(float));
    return out;
}

} // namespace cfire::sqlite
#endif

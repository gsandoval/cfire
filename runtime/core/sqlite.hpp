#pragma once
#include "../cfire_prelude.hpp"

#include <string>
#include <string_view>
#include <vector>
#include <expected>
#include <cstdint>

#ifdef CFIRE_USE_SQLITE
#include <sqlite3.h>
namespace cfire::sqlite {

template <typename T>
auto map_row(sqlite3_stmt* stmt) -> T;

inline void bind_arg(sqlite3_stmt* stmt, int idx, std::string_view v) {
    sqlite3_bind_text(stmt, idx, v.data(), static_cast<int>(v.size()), SQLITE_TRANSIENT);
}
inline void bind_arg(sqlite3_stmt* stmt, int idx, std::uint64_t v) {
    sqlite3_bind_int64(stmt, idx, static_cast<sqlite3_int64>(v));
}
inline void bind_arg(sqlite3_stmt* stmt, int idx, std::int64_t v) {
    sqlite3_bind_int64(stmt, idx, static_cast<sqlite3_int64>(v));
}
inline void bind_arg(sqlite3_stmt* stmt, int idx, std::int32_t v) {
    sqlite3_bind_int(stmt, idx, v);
}
inline void bind_arg(sqlite3_stmt* stmt, int idx, double v) {
    sqlite3_bind_double(stmt, idx, v);
}
inline void bind_arg(sqlite3_stmt* stmt, int idx, bool v) {
    sqlite3_bind_int(stmt, idx, v ? 1 : 0);
}

class Conn {
public:
    explicit Conn(sqlite3* db) : db_(db) {}
    ~Conn() {
        if (db_) sqlite3_close(db_);
    }
    Conn(Conn&& other) noexcept : db_(std::exchange(other.db_, nullptr)) {}
    auto operator=(Conn&& other) noexcept -> Conn& {
        if (this != &other) {
            if (db_) sqlite3_close(db_);
            db_ = std::exchange(other.db_, nullptr);
        }
        return *this;
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

private:
    sqlite3* db_ = nullptr;
};

[[nodiscard]] inline auto open(const std::filesystem::path& p) -> std::expected<Conn, cfire::Error> {
    sqlite3* raw = nullptr;
    if (sqlite3_open(p.c_str(), &raw) != SQLITE_OK) {
        std::string err = raw ? sqlite3_errmsg(raw) : "unknown sqlite open error";
        if (raw) sqlite3_close(raw);
        return cfire::make_error(err);
    }
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
[[nodiscard]] inline auto col_bool(sqlite3_stmt* stmt, int col) -> bool {
    return sqlite3_column_int(stmt, col) != 0;
}

} // namespace cfire::sqlite
#endif

// --- `core:crypto/sha256` & `core:crypto/hmac` (Standard FIPS 180-4 SHA-256 & RFC 2104 HMAC) ---


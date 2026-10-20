#pragma once
#include "../cfire_prelude.hpp"

#include "time.hpp"
#include <iostream>
#include <mutex>
#include <string>
#include <string_view>
#include <format>
#include <vector>

namespace cfire::log {

enum class Level : std::int32_t {
    Debug = 0,
    Info = 1,
    Warn = 2,
    Error = 3
};

enum class Format : std::int32_t {
    Text = 0,
    Json = 1
};

namespace detail {
    inline std::atomic<Level> g_level{Level::Info};
    inline std::atomic<Format> g_format{Format::Text};
    inline std::mutex g_log_mutex{};

    inline auto level_str(Level l) noexcept -> const char* {
        switch (l) {
            case Level::Debug: return "DEBUG";
            case Level::Info:  return "INFO";
            case Level::Warn:  return "WARN";
            case Level::Error: return "ERROR";
        }
        return "INFO";
    }

    inline auto level_color(Level l) noexcept -> const char* {
        switch (l) {
            case Level::Debug: return "\033[36m"; // Cyan
            case Level::Info:  return "\033[32m"; // Green
            case Level::Warn:  return "\033[33m"; // Yellow
            case Level::Error: return "\033[31m"; // Red
        }
        return "\033[0m";
    }

    inline void write_log(Level l, std::string_view msg, const std::vector<std::pair<std::string, std::string>>& attrs) {
        if (l < g_level.load(std::memory_order_relaxed)) {
            return;
        }

        std::string ts = cfire::time::now().format_rfc3339();
        std::lock_guard<std::mutex> lock(g_log_mutex);

        if (g_format.load(std::memory_order_relaxed) == Format::Json) {
            std::string out = "{\"time\":\"" + ts + "\",\"level\":\"" + level_str(l) + "\",\"msg\":\"";
            for (char c : msg) {
                if (c == '"') out += "\\\"";
                else if (c == '\\') out += "\\\\";
                else if (c == '\n') out += "\\n";
                else out += c;
            }
            out += "\"";
            for (const auto& [k, v] : attrs) {
                out += ",\"" + k + "\":\"";
                for (char c : v) {
                    if (c == '"') out += "\\\"";
                    else if (c == '\\') out += "\\\\";
                    else if (c == '\n') out += "\\n";
                    else out += c;
                }
                out += "\"";
            }
            out += "}\n";
            std::cerr << out;
        } else {
            std::string out = std::format("{}[{}]\033[0m {} - {}", level_color(l), level_str(l), ts, msg);
            for (const auto& [k, v] : attrs) {
                out += std::format(" {}={}", k, v);
            }
            out += "\n";
            std::cerr << out;
        }
    }
} // namespace detail

inline void set_level(Level l) noexcept {
    detail::g_level.store(l, std::memory_order_relaxed);
}

inline void set_format(Format f) noexcept {
    detail::g_format.store(f, std::memory_order_relaxed);
}

inline auto get_level() noexcept -> Level {
    return detail::g_level.load(std::memory_order_relaxed);
}

inline auto get_format() noexcept -> Format {
    return detail::g_format.load(std::memory_order_relaxed);
}

template <typename... Args>
inline void debug(std::string_view msg, Args&&... args) {
    std::vector<std::pair<std::string, std::string>> attrs;
    auto add_pairs = [&]<typename Self, typename K, typename V, typename... Rest>(Self& self, K&& k, V&& v, Rest&&... rest) {
        attrs.emplace_back(std::string(std::forward<K>(k)), std::string(std::forward<V>(v)));
        if constexpr (sizeof...(rest) >= 2) {
            self(self, std::forward<Rest>(rest)...);
        }
    };
    if constexpr (sizeof...(args) >= 2) {
        add_pairs(add_pairs, std::forward<Args>(args)...);
    }
    detail::write_log(Level::Debug, msg, attrs);
}

template <typename... Args>
inline void info(std::string_view msg, Args&&... args) {
    std::vector<std::pair<std::string, std::string>> attrs;
    auto add_pairs = [&]<typename Self, typename K, typename V, typename... Rest>(Self& self, K&& k, V&& v, Rest&&... rest) {
        attrs.emplace_back(std::string(std::forward<K>(k)), std::string(std::forward<V>(v)));
        if constexpr (sizeof...(rest) >= 2) {
            self(self, std::forward<Rest>(rest)...);
        }
    };
    if constexpr (sizeof...(args) >= 2) {
        add_pairs(add_pairs, std::forward<Args>(args)...);
    }
    detail::write_log(Level::Info, msg, attrs);
}

template <typename... Args>
inline void warn(std::string_view msg, Args&&... args) {
    std::vector<std::pair<std::string, std::string>> attrs;
    auto add_pairs = [&]<typename Self, typename K, typename V, typename... Rest>(Self& self, K&& k, V&& v, Rest&&... rest) {
        attrs.emplace_back(std::string(std::forward<K>(k)), std::string(std::forward<V>(v)));
        if constexpr (sizeof...(rest) >= 2) {
            self(self, std::forward<Rest>(rest)...);
        }
    };
    if constexpr (sizeof...(args) >= 2) {
        add_pairs(add_pairs, std::forward<Args>(args)...);
    }
    detail::write_log(Level::Warn, msg, attrs);
}

template <typename... Args>
inline void error(std::string_view msg, Args&&... args) {
    std::vector<std::pair<std::string, std::string>> attrs;
    auto add_pairs = [&]<typename Self, typename K, typename V, typename... Rest>(Self& self, K&& k, V&& v, Rest&&... rest) {
        attrs.emplace_back(std::string(std::forward<K>(k)), std::string(std::forward<V>(v)));
        if constexpr (sizeof...(rest) >= 2) {
            self(self, std::forward<Rest>(rest)...);
        }
    };
    if constexpr (sizeof...(args) >= 2) {
        add_pairs(add_pairs, std::forward<Args>(args)...);
    }
    detail::write_log(Level::Error, msg, attrs);
}

} // namespace cfire::log


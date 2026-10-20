#include "cfire_prelude.hpp"

[[nodiscard]] inline auto trim(const std::string& s) -> std::string;
[[nodiscard]] inline auto split(const std::string& s, const std::string& sep) -> std::vector<std::string>;
[[nodiscard]] inline auto join(const std::vector<std::string>& parts, const std::string& sep) -> std::string;
[[nodiscard]] inline auto contains(const std::string& s, const std::string& needle) -> bool;
[[nodiscard]] inline auto starts_with(const std::string& s, const std::string& prefix) -> bool;
[[nodiscard]] inline auto ends_with(const std::string& s, const std::string& suffix) -> bool;
[[nodiscard]] inline auto to_upper(const std::string& s) -> std::string;
[[nodiscard]] inline auto to_lower(const std::string& s) -> std::string;
[[nodiscard]] inline auto replace_all(const std::string& s, const std::string& from, const std::string& to) -> std::string;
[[nodiscard]] inline auto replace(const std::string& s, const std::string& from, const std::string& to) -> std::string;

[[nodiscard]] inline auto trim(const std::string& s) -> std::string {
    std::size_t start = 0;
    std::size_t end = s.size();
    while ((start < end) && ((((s[start] == 32) || (s[start] == 9)) || (s[start] == 10)) || (s[start] == 13))) {
        start = (start + 1);
    }
    while ((end > start) && ((((s[(end - 1)] == 32) || (s[(end - 1)] == 9)) || (s[(end - 1)] == 10)) || (s[(end - 1)] == 13))) {
        end = (end - 1);
    }
    if (start >= end) {
        return "";
    }
    std::string result = "";
    result += cfire::slice(s, start, end);
    return result;
}

[[nodiscard]] inline auto split(const std::string& s, const std::string& sep) -> std::vector<std::string> {
    std::vector<std::string> res = {};
    if (sep.size() == 0) {
        (void)(res.push_back(s));
        return res;
    }
    std::size_t start = 0;
    std::size_t i = 0;
    auto n = s.size();
    auto sn = sep.size();
    while ((i + sn) <= n) {
        if (cfire::slice(s, i, (i + sn)) == sep) {
            std::string item = "";
            item += cfire::slice(s, start, i);
            (void)(res.push_back(item));
            start = (i + sn);
            i = start;
        } else {
            i = (i + 1);
        }
    }
    if (start <= n) {
        std::string last_item = "";
        last_item += cfire::slice(s, start, n);
        (void)(res.push_back(last_item));
    }
    return res;
}

[[nodiscard]] inline auto join(const std::vector<std::string>& parts, const std::string& sep) -> std::string {
    std::string res = "";
    std::size_t i = 0;
    auto n = parts.size();
    while (i < n) {
        if (i > 0) {
            res += sep;
        }
        res += parts[i];
        i = (i + 1);
    }
    return res;
}

[[nodiscard]] inline auto contains(const std::string& s, const std::string& needle) -> bool {
    auto n = s.size();
    auto nn = needle.size();
    if (nn == 0) {
        return true;
    }
    if (nn > n) {
        return false;
    }
    std::size_t i = 0;
    while ((i + nn) <= n) {
        if (cfire::slice(s, i, (i + nn)) == needle) {
            return true;
        }
        i = (i + 1);
    }
    return false;
}

[[nodiscard]] inline auto starts_with(const std::string& s, const std::string& prefix) -> bool {
    auto n = s.size();
    auto pn = prefix.size();
    if (pn > n) {
        return false;
    }
    return (cfire::slice(s, 0, pn) == prefix);
}

[[nodiscard]] inline auto ends_with(const std::string& s, const std::string& suffix) -> bool {
    auto n = s.size();
    auto sn = suffix.size();
    if (sn > n) {
        return false;
    }
    return (cfire::slice(s, (n - sn), n) == suffix);
}

[[nodiscard]] inline auto to_upper(const std::string& s) -> std::string {
    auto res = s;
    std::size_t i = 0;
    auto n = res.size();
    while (i < n) {
        auto c = res[i];
        if ((c >= 97) && (c <= 122)) {
            res[i] = (c - 32);
        }
        i = (i + 1);
    }
    return res;
}

[[nodiscard]] inline auto to_lower(const std::string& s) -> std::string {
    auto res = s;
    std::size_t i = 0;
    auto n = res.size();
    while (i < n) {
        auto c = res[i];
        if ((c >= 65) && (c <= 90)) {
            res[i] = (c + 32);
        }
        i = (i + 1);
    }
    return res;
}

[[nodiscard]] inline auto replace_all(const std::string& s, const std::string& from, const std::string& to) -> std::string {
    if (from.size() == 0) {
        return s;
    }
    std::string res = "";
    std::size_t start = 0;
    std::size_t i = 0;
    auto n = s.size();
    auto fn_len = from.size();
    while ((i + fn_len) <= n) {
        if (cfire::slice(s, i, (i + fn_len)) == from) {
            res += cfire::slice(s, start, i);
            res += to;
            start = (i + fn_len);
            i = start;
        } else {
            i = (i + 1);
        }
    }
    if (start < n) {
        res += cfire::slice(s, start, n);
    }
    return res;
}

[[nodiscard]] inline auto replace(const std::string& s, const std::string& from, const std::string& to) -> std::string {
    return replace_all(s, from, to);
}

#include "cfire_prelude.hpp"

struct CppParsedFnSig;
struct CppCacheEntry;

struct CppParsedFnSig {
    std::string return_type{};
    std::vector<std::string> param_types{};
};

template <>
struct cfire::reflect_struct<CppParsedFnSig> {
    static constexpr std::string_view name = "CppParsedFnSig";
    static constexpr std::size_t field_count = 2;
    static constexpr std::array<std::string_view, 2> field_names = {
        "return_type",
        "param_types",
    };
    static constexpr std::array<std::string_view, 2> field_tags = {
        R"tag()tag",
        R"tag()tag",
    };
};

struct CppCacheEntry {
    std::string key{};
    std::string name{};
    std::string kind{};
    std::string return_type{};
    std::vector<std::string> param_types{};
};

template <>
struct cfire::reflect_struct<CppCacheEntry> {
    static constexpr std::string_view name = "CppCacheEntry";
    static constexpr std::size_t field_count = 5;
    static constexpr std::array<std::string_view, 5> field_names = {
        "key",
        "name",
        "kind",
        "return_type",
        "param_types",
    };
    static constexpr std::array<std::string_view, 5> field_tags = {
        R"tag()tag",
        R"tag()tag",
        R"tag()tag",
        R"tag()tag",
        R"tag()tag",
    };
};

[[nodiscard]] auto bitwise_xor_byte(std::uint64_t a, std::uint8_t b) -> std::uint64_t;
[[nodiscard]] auto get_fnv1a_initial_offset() -> std::uint64_t;
[[nodiscard]] auto selfhost_clang_hash_key(std::string_view s) -> std::uint64_t;
[[nodiscard]] auto selfhost_clang_trim_qual(std::string_view s) -> std::string;
[[nodiscard]] auto selfhost_clang_parse_fn_sig(std::string_view full_sig) -> CppParsedFnSig;
[[nodiscard]] auto selfhost_clang_format_cache_entry(const CppCacheEntry& entry) -> std::string;
[[nodiscard]] auto selfhost_clang_parse_cache_line(std::string_view line) -> CppCacheEntry;

[[nodiscard]] auto bitwise_xor_byte(std::uint64_t a, std::uint8_t b) -> std::uint64_t {
    {
        return a ^ static_cast<uint64_t>(b);
    }
}

[[nodiscard]] auto get_fnv1a_initial_offset() -> std::uint64_t {
    {
        return 0xcbf29ce484222325ULL;
    }
}

[[nodiscard]] auto selfhost_clang_hash_key(std::string_view s) -> std::uint64_t {
    std::uint64_t h = get_fnv1a_initial_offset();
    std::uint64_t prime = 1099511628211;
    std::size_t i = 0;
    auto n = s.size();
    while (i < n) {
        auto c = s[i];
        h = bitwise_xor_byte(h, c);
        h = (h * prime);
        i = (i + 1);
    }
    return h;
}

[[nodiscard]] auto selfhost_clang_trim_qual(std::string_view s) -> std::string {
    std::size_t start = 0;
    auto n = s.size();
    while ((start < n) && ((s[start] == 32) || (s[start] == 9))) {
        start = (start + 1);
    }
    std::size_t end = n;
    while ((end > start) && ((s[(end - 1)] == 32) || (s[(end - 1)] == 9))) {
        end = (end - 1);
    }
    std::string res = "";
    res += cfire::slice(s, start, end);
    return res;
}

[[nodiscard]] auto selfhost_clang_parse_fn_sig(std::string_view full_sig) -> CppParsedFnSig {
    auto res = CppParsedFnSig{.return_type = "", .param_types = {}};
    auto n = full_sig.size();
    std::size_t lp = n;
    std::size_t rp = n;
    std::size_t i = 0;
    while (i < n) {
        if ((full_sig[i] == 40) && (lp == n)) {
            lp = i;
        }
        if (full_sig[i] == 41) {
            rp = i;
        }
        i = (i + 1);
    }
    if (((lp == n) || (rp == n)) || (rp <= lp)) {
        res.return_type = selfhost_clang_trim_qual(full_sig);
        return res;
    }
    res.return_type = selfhost_clang_trim_qual(cfire::slice(full_sig, 0, lp));
    auto params_sv = selfhost_clang_trim_qual(cfire::slice(full_sig, (lp + 1), rp));
    if ((params_sv.size() == 0) || (params_sv == "void")) {
        return res;
    }
    std::int32_t depth = 0;
    std::size_t start = 0;
    auto pn = params_sv.size();
    std::size_t j = 0;
    while (j < pn) {
        auto c = params_sv[j];
        if ((c == 60) || (c == 40)) {
            depth = (depth + 1);
        } else {
            if ((c == 62) || (c == 41)) {
                depth = (depth - 1);
            } else {
                if ((c == 44) && (depth == 0)) {
                    auto pt = selfhost_clang_trim_qual(cfire::slice(params_sv, start, j));
                    if (pt.size() > 0) {
                        (void)(res.param_types.push_back(pt));
                    }
                    start = (j + 1);
                }
            }
        }
        j = (j + 1);
    }
    auto last_pt = selfhost_clang_trim_qual(cfire::slice(params_sv, start, pn));
    if (last_pt.size() > 0) {
        (void)(res.param_types.push_back(last_pt));
    }
    return res;
}

[[nodiscard]] auto selfhost_clang_format_cache_entry(const CppCacheEntry& entry) -> std::string {
    std::string line = ((((((entry.key + "\t") + entry.name) + "\t") + entry.kind) + "\t") + entry.return_type);
    std::size_t i = 0;
    auto n = entry.param_types.size();
    while (i < n) {
        line += ("\t" + entry.param_types[i]);
        i = (i + 1);
    }
    line += "\n";
    return line;
}

[[nodiscard]] auto selfhost_clang_parse_cache_line(std::string_view line) -> CppCacheEntry {
    auto res = CppCacheEntry{.key = "", .name = "", .kind = "", .return_type = "", .param_types = {}};
    std::vector<std::string> cols = {};
    std::size_t start = 0;
    auto n = line.size();
    std::size_t i = 0;
    while (i < n) {
        if (line[i] == 9) {
            std::string col = "";
            col += cfire::slice(line, start, i);
            (void)(cols.push_back(col));
            start = (i + 1);
        }
        i = (i + 1);
    }
    if (start <= n) {
        std::string last_col = "";
        last_col += cfire::slice(line, start, n);
        (void)(cols.push_back(last_col));
    }
    if (cols.size() >= 4) {
        res.key = cols[0];
        res.name = cols[1];
        res.kind = cols[2];
        res.return_type = cols[3];
        std::size_t ci = 4;
        auto cn = cols.size();
        while (ci < cn) {
            (void)(res.param_types.push_back(cols[ci]));
            ci = (ci + 1);
        }
    }
    return res;
}

#include "cfire_prelude.hpp"

struct StructFieldLine {
    bool is_valid{};
    bool is_comment_only{};
    std::string name{};
    std::string type_and_default{};
    std::string tag{};
    std::string comment{};
};

template <>
struct cfire::reflect_struct<StructFieldLine> {
    static constexpr std::string_view name = "StructFieldLine";
    static constexpr std::size_t field_count = 6;
    static constexpr std::array<std::string_view, 6> field_names = {
        "is_valid",
        "is_comment_only",
        "name",
        "type_and_default",
        "tag",
        "comment",
    };
    static constexpr std::array<std::string_view, 6> field_tags = {
        R"tag()tag",
        R"tag()tag",
        R"tag()tag",
        R"tag()tag",
        R"tag()tag",
        R"tag()tag",
    };
};

[[nodiscard]] auto trim_ws(std::string_view sv) -> std::string {
    std::size_t b = 0;
    auto n = sv.size();
    while ((b < n) && (((sv[b] == 32) || (sv[b] == 9)) || (sv[b] == 13))) {
        b = (b + 1);
    }
    std::size_t e = n;
    while ((e > b) && (((sv[(e - 1)] == 32) || (sv[(e - 1)] == 9)) || (sv[(e - 1)] == 13))) {
        e = (e - 1);
    }
    return ("" + cfire::slice(sv, b, e));
}

[[nodiscard]] auto make_spaces(std::int64_t count) -> std::string {
    if (count <= 0) {
        return "";
    }
    std::string s = "";
    auto rem = count;
    while (rem >= 8) {
        s = (s + "        ");
        rem = (rem - 8);
    }
    while (rem >= 4) {
        s = (s + "    ");
        rem = (rem - 4);
    }
    while (rem > 0) {
        s = (s + " ");
        rem = (rem - 1);
    }
    return s;
}

[[nodiscard]] auto starts_with_str(std::string_view s, std::string_view prefix) -> bool {
    auto n = s.size();
    auto pn = prefix.size();
    if (pn > n) {
        return false;
    }
    return (cfire::slice(s, 0, pn) == prefix);
}

[[nodiscard]] auto ends_with_str(std::string_view s, std::string_view suffix) -> bool {
    auto n = s.size();
    auto sn = suffix.size();
    if (sn > n) {
        return false;
    }
    return (cfire::slice(s, (n - sn), n) == suffix);
}

[[nodiscard]] auto find_char(std::string_view s, std::uint8_t target, std::size_t start) -> std::int64_t {
    auto i = start;
    auto n = s.size();
    while (i < n) {
        if (s[i] == target) {
            return i;
        }
        i = (i + 1);
    }
    return -1;
}

[[nodiscard]] auto find_str(std::string_view s, std::string_view target, std::size_t start) -> std::int64_t {
    auto i = start;
    auto n = s.size();
    auto tn = target.size();
    if (tn == 0) {
        return start;
    }
    if (tn > n) {
        return -1;
    }
    auto max_i = (n - tn);
    while (i <= max_i) {
        if (cfire::slice(s, i, (i + tn)) == target) {
            return i;
        }
        i = (i + 1);
    }
    return -1;
}

[[nodiscard]] auto is_ident_char(std::uint8_t c) -> bool {
    if (((((c >= 65) && (c <= 90)) || ((c >= 97) && (c <= 122))) || ((c >= 48) && (c <= 57))) || (c == 95)) {
        return true;
    }
    return false;
}

[[nodiscard]] auto parse_struct_field_line(std::string_view trimmed) -> StructFieldLine {
    StructFieldLine invalid = StructFieldLine{.is_valid = false, .is_comment_only = false, .name = "", .type_and_default = "", .tag = "", .comment = ""};
    if ((trimmed.size() == 0) || (trimmed == "}")) {
        return invalid;
    }
    if (starts_with_str(trimmed, "//")) {
        return StructFieldLine{.is_valid = true, .is_comment_only = true, .name = "", .type_and_default = "", .tag = "", .comment = ("" + trimmed)};
    }
    auto colon = find_char(trimmed, 58, 0);
    if (colon <= 0) {
        return invalid;
    }
    auto fname = trim_ws(cfire::slice(trimmed, 0, colon));
    if (fname.size() == 0) {
        return invalid;
    }
    std::size_t fi = 0;
    auto fn_len = fname.size();
    while (fi < fn_len) {
        if (!is_ident_char(fname[fi])) {
            return invalid;
        }
        fi = (fi + 1);
    }
    auto rest = trim_ws(cfire::slice(trimmed, (colon + 1), trimmed.size()));
    std::string tag = "";
    std::string comment = "";
    auto cpos = find_str(rest, "//", 0);
    auto b1 = find_char(rest, 96, 0);
    if ((b1 >= 0) && ((cpos < 0) || (b1 < cpos))) {
        auto b2 = find_char(rest, 96, (b1 + 1));
        if (b2 >= 0) {
            tag = cfire::slice(rest, b1, (b2 + 1));
            auto after_tag = trim_ws(cfire::slice(rest, (b2 + 1), rest.size()));
            if (starts_with_str(after_tag, "//")) {
                comment = after_tag;
            }
            rest = trim_ws(cfire::slice(rest, 0, b1));
        }
    } else {
        if (cpos >= 0) {
            comment = trim_ws(cfire::slice(rest, cpos, rest.size()));
            rest = trim_ws(cfire::slice(rest, 0, cpos));
        }
    }
    if (rest.size() == 0) {
        return invalid;
    }
    return StructFieldLine{.is_valid = true, .is_comment_only = false, .name = fname, .type_and_default = rest, .tag = tag, .comment = comment};
}

[[nodiscard]] auto selfhost_format_lines(const std::vector<std::string>& raw_lines) -> std::vector<std::string> {
    std::vector<std::string> out_lines = {};
    auto n = raw_lines.size();
    std::int32_t indent = 0;
    bool in_raw_string = false;
    std::size_t i = 0;
    while (i < n) {
        auto raw = raw_lines[i];
        if (in_raw_string) {
            out_lines.push_back(raw);
            if (find_char(raw, 96, 0) >= 0) {
                in_raw_string = false;
            }
            i = (i + 1);
            continue;
        }
        auto trimmed = trim_ws(raw);
        if (trimmed.size() == 0) {
            out_lines.push_back("");
            i = (i + 1);
            continue;
        }
        if (starts_with_str(trimmed, "type ") && ends_with_str(trimmed, "struct {")) {
            auto j = (i + 1);
            std::vector<StructFieldLine> fields = {};
            auto pure_fields = true;
            auto real_field_count = 0;
            while (j < n) {
                auto ft = trim_ws(raw_lines[j]);
                if (ft == "}") {
                    break;
                }
                if (ft.size() == 0) {
                    j = (j + 1);
                    continue;
                }
                auto sfl = parse_struct_field_line(ft);
                if (sfl.is_valid) {
                    if (!sfl.is_comment_only) {
                        real_field_count = (real_field_count + 1);
                    }
                    fields.push_back(sfl);
                } else {
                    pure_fields = false;
                    break;
                }
                j = (j + 1);
            }
            if ((pure_fields && (j < n)) && (real_field_count > 0)) {
                out_lines.push_back((make_spaces((indent * 4)) + trimmed));
                std::int64_t max_name = 0;
                std::int64_t max_type = 0;
                auto any_tags_or_comments = false;
                std::size_t f_idx = 0;
                auto num_fields = fields.size();
                while (f_idx < num_fields) {
                    auto f = fields[f_idx];
                    if (!f.is_comment_only) {
                        std::int64_t fn_len = f.name.size();
                        if (fn_len > max_name) {
                            max_name = fn_len;
                        }
                        std::int64_t ft_len = f.type_and_default.size();
                        if (ft_len > max_type) {
                            max_type = ft_len;
                        }
                        if ((f.tag.size() > 0) || (f.comment.size() > 0)) {
                            any_tags_or_comments = true;
                        }
                    }
                    f_idx = (f_idx + 1);
                }
                std::size_t field_idx = 0;
                auto row = (i + 1);
                while (row < j) {
                    if (trim_ws(raw_lines[row]).size() == 0) {
                        out_lines.push_back("");
                        row = (row + 1);
                        continue;
                    }
                    auto f = fields[field_idx];
                    field_idx = (field_idx + 1);
                    auto line_str = make_spaces(((indent + 1) * 4));
                    if (f.is_comment_only) {
                        line_str = (line_str + f.comment);
                    } else {
                        line_str = ((line_str + f.name) + ": ");
                        if ((real_field_count > 1) && any_tags_or_comments) {
                            auto diff_name = (max_name - f.name.size());
                            line_str = (line_str + make_spaces(diff_name));
                        }
                        line_str = (line_str + f.type_and_default);
                        if (f.tag.size() > 0) {
                            auto diff_type = ((max_type - f.type_and_default.size()) + 1);
                            line_str = ((line_str + make_spaces(diff_type)) + f.tag);
                        }
                        if (f.comment.size() > 0) {
                            line_str = ((line_str + " ") + f.comment);
                        }
                    }
                    out_lines.push_back(line_str);
                    row = (row + 1);
                }
                out_lines.push_back((make_spaces((indent * 4)) + "}"));
                i = (j + 1);
                continue;
            }
        }
        std::int32_t leading_close = 0;
        std::size_t ci = 0;
        auto t_len = trimmed.size();
        while (ci < t_len) {
            auto c = trimmed[ci];
            if (((c == 125) || (c == 41)) || (c == 93)) {
                leading_close = (leading_close + 1);
            } else {
                break;
            }
            ci = (ci + 1);
        }
        auto line_indent = (indent - leading_close);
        if (line_indent < 0) {
            line_indent = 0;
        }
        out_lines.push_back((make_spaces((line_indent * 4)) + trimmed));
        auto in_str = false;
        std::int32_t net_delta = 0;
        std::size_t k = 0;
        while (k < t_len) {
            auto c = trimmed[k];
            if (((!in_str && (c == 47)) && ((k + 1) < t_len)) && (trimmed[(k + 1)] == 47)) {
                break;
            }
            if ((c == 34) && ((k == 0) || (trimmed[(k - 1)] != 92))) {
                in_str = !in_str;
                k = (k + 1);
                continue;
            }
            if (!in_str && (c == 96)) {
                auto close_bt = find_char(trimmed, 96, (k + 1));
                if (close_bt >= 0) {
                    k = (close_bt + 1);
                    continue;
                } else {
                    in_raw_string = true;
                    break;
                }
            }
            if (!in_str) {
                if (((c == 123) || (c == 40)) || (c == 91)) {
                    net_delta = (net_delta + 1);
                } else {
                    if (((c == 125) || (c == 41)) || (c == 93)) {
                        net_delta = (net_delta - 1);
                    }
                }
            }
            k = (k + 1);
        }
        indent = (indent + net_delta);
        if (indent < 0) {
            indent = 0;
        }
        i = (i + 1);
    }
    return out_lines;
}

[[nodiscard]] auto selfhost_format_code(std::string_view src) -> std::string {
    std::vector<std::string> raw_lines = {};
    auto n = src.size();
    std::size_t start = 0;
    std::size_t i = 0;
    while (i < n) {
        if (src[i] == 10) {
            auto line_end = i;
            if ((line_end > start) && (src[(line_end - 1)] == 13)) {
                line_end = (line_end - 1);
            }
            raw_lines.push_back(("" + cfire::slice(src, start, line_end)));
            start = (i + 1);
        }
        i = (i + 1);
    }
    if (start < n) {
        auto end_idx = n;
        if ((end_idx > start) && (src[(end_idx - 1)] == 13)) {
            end_idx = (end_idx - 1);
        }
        raw_lines.push_back(("" + cfire::slice(src, start, end_idx)));
    }
    auto formatted_lines = selfhost_format_lines(raw_lines);
    std::string result_str = "";
    auto prev_blank = false;
    auto num_fl = formatted_lines.size();
    std::size_t fl_idx = 0;
    while (fl_idx < num_fl) {
        auto line = formatted_lines[fl_idx];
        if (line.size() == 0) {
            if (!prev_blank && (result_str.size() > 0)) {
                result_str = (result_str + "\n");
                prev_blank = true;
            }
            fl_idx = (fl_idx + 1);
            continue;
        }
        prev_blank = false;
        result_str = ((result_str + line) + "\n");
        fl_idx = (fl_idx + 1);
    }
    while ((result_str.size() >= 2) && ends_with_str(result_str, "\n\n")) {
        result_str = ("" + cfire::slice(result_str, 0, (result_str.size() - 1)));
    }
    if ((result_str.size() > 0) && !ends_with_str(result_str, "\n")) {
        result_str = (result_str + "\n");
    }
    return result_str;
}

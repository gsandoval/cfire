#include "cfire_prelude.hpp"

struct Occurrence;
struct FileModification;
struct RenameResult;

struct Occurrence {
    std::string file_path{};
    std::size_t line{};
    std::size_t col{};
    std::size_t end_col{};
};

template <>
struct cfire::reflect_struct<Occurrence> {
    static constexpr std::string_view name = "Occurrence";
    static constexpr std::size_t field_count = 4;
    static constexpr std::array<std::string_view, 4> field_names = {
        "file_path",
        "line",
        "col",
        "end_col",
    };
    static constexpr std::array<std::string_view, 4> field_tags = {
        R"tag()tag",
        R"tag()tag",
        R"tag()tag",
        R"tag()tag",
    };
};

struct FileModification {
    std::string file_path{};
    std::string original_content{};
    std::string modified_content{};
    std::vector<Occurrence> occurrences{};
};

template <>
struct cfire::reflect_struct<FileModification> {
    static constexpr std::string_view name = "FileModification";
    static constexpr std::size_t field_count = 4;
    static constexpr std::array<std::string_view, 4> field_names = {
        "file_path",
        "original_content",
        "modified_content",
        "occurrences",
    };
    static constexpr std::array<std::string_view, 4> field_tags = {
        R"tag()tag",
        R"tag()tag",
        R"tag()tag",
        R"tag()tag",
    };
};

struct RenameResult {
    bool success{};
    std::string old_name{};
    std::string new_name{};
    std::string error{};
    std::vector<FileModification> modifications{};
    std::size_t total_occurrences{};
};

template <>
struct cfire::reflect_struct<RenameResult> {
    static constexpr std::string_view name = "RenameResult";
    static constexpr std::size_t field_count = 6;
    static constexpr std::array<std::string_view, 6> field_names = {
        "success",
        "old_name",
        "new_name",
        "error",
        "modifications",
        "total_occurrences",
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

[[nodiscard]] inline auto is_keyword(std::string_view name) -> bool;
[[nodiscard]] inline auto is_valid_identifier(std::string_view name) -> bool;
[[nodiscard]] inline auto split_lines(std::string_view text) -> std::vector<std::string>;
[[nodiscard]] inline auto json_escape(std::string_view sv) -> std::string;
[[nodiscard]] inline auto usize_to_str(std::size_t val) -> std::string;
[[nodiscard]] inline auto apply_line_replacements(std::string_view original_content, const std::vector<Occurrence>& occurrences, std::string_view new_name) -> std::string;
[[nodiscard]] inline auto generate_unified_diff(const RenameResult& res) -> std::string;
[[nodiscard]] inline auto to_json(const RenameResult& res) -> std::string;

[[nodiscard]] inline auto is_keyword(std::string_view name) -> bool {
    if (((((name == "package") || (name == "import")) || (name == "cpp")) || (name == "cplusplus")) || (name == "as")) {
        return true;
    }
    if (((((name == "type") || (name == "struct")) || (name == "enum")) || (name == "interface")) || (name == "map")) {
        return true;
    }
    if (((((((name == "fn") || (name == "async")) || (name == "in")) || (name == "mut")) || (name == "out")) || (name == "move")) || (name == "var")) {
        return true;
    }
    if (((((((((name == "if") || (name == "else")) || (name == "for")) || (name == "match")) || (name == "defer")) || (name == "return")) || (name == "try")) || (name == "or")) || (name == "test")) {
        return true;
    }
    if (((((name == "true") || (name == "false")) || (name == "none")) || (name == "nil")) || (name == "null")) {
        return true;
    }
    return false;
}

[[nodiscard]] inline auto is_valid_identifier(std::string_view name) -> bool {
    auto n = name.size();
    if (n == 0) {
        return false;
    }
    auto c0 = name[0];
    if (!((((c0 >= 65) && (c0 <= 90)) || ((c0 >= 97) && (c0 <= 122))) || (c0 == 95))) {
        return false;
    }
    std::size_t idx = 1;
    while (idx < n) {
        auto c = name[idx];
        if (!(((((c >= 65) && (c <= 90)) || ((c >= 97) && (c <= 122))) || ((c >= 48) && (c <= 57))) || (c == 95))) {
            return false;
        }
        idx = (idx + 1);
    }
    if (is_keyword(name)) {
        return false;
    }
    return true;
}

[[nodiscard]] inline auto split_lines(std::string_view text) -> std::vector<std::string> {
    std::vector<std::string> lines = {};
    auto n = text.size();
    if (n == 0) {
        return lines;
    }
    std::size_t start = 0;
    std::size_t i = 0;
    while (i < n) {
        if (text[i] == 10) {
            auto end = i;
            if ((end > start) && (text[(end - 1)] == 13)) {
                end = (end - 1);
            }
            (void)(lines.push_back(("" + cfire::slice(text, start, end))));
            start = (i + 1);
        }
        i = (i + 1);
    }
    if (start < n) {
        auto end = n;
        if ((end > start) && (text[(end - 1)] == 13)) {
            end = (end - 1);
        }
        (void)(lines.push_back(("" + cfire::slice(text, start, end))));
    }
    return lines;
}

[[nodiscard]] inline auto json_escape(std::string_view sv) -> std::string {
    std::string escaped = "";
    std::size_t start = 0;
    std::size_t i = 0;
    auto n = sv.size();
    while (i < n) {
        auto c = sv[i];
        if (((((c == 34) || (c == 92)) || (c == 10)) || (c == 13)) || (c == 9)) {
            if (i > start) {
                escaped += cfire::slice(sv, start, i);
            }
            if (c == 34) {
                escaped += "\\\"";
            } else {
                if (c == 92) {
                    escaped += "\\\\";
                } else {
                    if (c == 10) {
                        escaped += "\\n";
                    } else {
                        if (c == 13) {
                            escaped += "\\r";
                        } else {
                            if (c == 9) {
                                escaped += "\\t";
                            }
                        }
                    }
                }
            }
            start = (i + 1);
        }
        i = (i + 1);
    }
    if (start < n) {
        escaped += cfire::slice(sv, start, n);
    }
    return escaped;
}

[[nodiscard]] inline auto usize_to_str(std::size_t val) -> std::string {
    if (val == 0) {
        return "0";
    }
    auto v = val;
    std::string digits = "";
    while (v > 0) {
        auto d = (v % 10);
        if (d == 0) {
            digits = ("0" + digits);
        } else {
            if (d == 1) {
                digits = ("1" + digits);
            } else {
                if (d == 2) {
                    digits = ("2" + digits);
                } else {
                    if (d == 3) {
                        digits = ("3" + digits);
                    } else {
                        if (d == 4) {
                            digits = ("4" + digits);
                        } else {
                            if (d == 5) {
                                digits = ("5" + digits);
                            } else {
                                if (d == 6) {
                                    digits = ("6" + digits);
                                } else {
                                    if (d == 7) {
                                        digits = ("7" + digits);
                                    } else {
                                        if (d == 8) {
                                            digits = ("8" + digits);
                                        } else {
                                            if (d == 9) {
                                                digits = ("9" + digits);
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
        v = (v / 10);
    }
    return digits;
}

[[nodiscard]] inline auto apply_line_replacements(std::string_view original_content, const std::vector<Occurrence>& occurrences, std::string_view new_name) -> std::string {
    if (occurrences.size() == 0) {
        return ("" + original_content);
    }
    auto file_lines = split_lines(original_content);
    std::vector<Occurrence> occs = {};
    std::size_t oi = 0;
    auto num_occs = occurrences.size();
    while (oi < num_occs) {
        (void)(occs.push_back(occurrences[oi]));
        oi = (oi + 1);
    }
    std::size_t i = 1;
    while (i < occs.size()) {
        auto key = occs[i];
        auto j = i;
        while (j > 0) {
            auto cur = occs[(j - 1)];
            auto should_swap = false;
            if (cur.line < key.line) {
                should_swap = true;
            } else {
                if ((cur.line == key.line) && (cur.col < key.col)) {
                    should_swap = true;
                }
            }
            if (should_swap) {
                occs[j] = cur;
                j = (j - 1);
            } else {
                break;
            }
        }
        occs[j] = key;
        i = (i + 1);
    }
    std::size_t k = 0;
    while (k < occs.size()) {
        auto occ = occs[k];
        if (occ.line > 0) {
            auto ln_idx = (occ.line - 1);
            if (ln_idx < file_lines.size()) {
                auto cur_line = file_lines[ln_idx];
                if ((occ.col > 0) && (occ.end_col >= occ.col)) {
                    auto sc = (occ.col - 1);
                    auto ec = (occ.end_col - 1);
                    if ((sc <= cur_line.size()) && (ec <= cur_line.size())) {
                        auto prefix = cfire::slice(cur_line, 0, sc);
                        auto suffix = cfire::slice(cur_line, ec, cur_line.size());
                        std::string replaced_line = "";
                        replaced_line += prefix;
                        replaced_line += new_name;
                        replaced_line += suffix;
                        file_lines[ln_idx] = replaced_line;
                    }
                }
            }
        }
        k = (k + 1);
    }
    std::string modded = "";
    std::size_t li = 0;
    auto n_lines = file_lines.size();
    auto has_trailing_nl = ((original_content.size() > 0) && (original_content[(original_content.size() - 1)] == 10));
    while (li < n_lines) {
        modded += file_lines[li];
        if (((li + 1) < n_lines) || has_trailing_nl) {
            modded += "\n";
        }
        li = (li + 1);
    }
    return modded;
}

[[nodiscard]] inline auto generate_unified_diff(const RenameResult& res) -> std::string {
    std::string diff = "";
    std::size_t m_idx = 0;
    auto n_mods = res.modifications.size();
    while (m_idx < n_mods) {
        auto mod = res.modifications[m_idx];
        auto orig_lines = split_lines(mod.original_content);
        auto mod_lines = split_lines(mod.modified_content);
        diff += "--- a/";
        diff += mod.file_path;
        diff += "\n+++ b/";
        diff += mod.file_path;
        diff += "\n";
        std::size_t i = 0;
        auto max_lines = orig_lines.size();
        if (mod_lines.size() > max_lines) {
            max_lines = mod_lines.size();
        }
        while (i < max_lines) {
            if (((i < orig_lines.size()) && (i < mod_lines.size())) && (orig_lines[i] == mod_lines[i])) {
                i = (i + 1);
                continue;
            }
            std::size_t hunk_start = 0;
            if (i >= 2) {
                hunk_start = (i - 2);
            }
            auto hunk_end = (i + 3);
            if (hunk_end > max_lines) {
                hunk_end = max_lines;
            }
            auto o_end = hunk_end;
            if (o_end > orig_lines.size()) {
                o_end = orig_lines.size();
            }
            auto o_start = hunk_start;
            if (o_start > orig_lines.size()) {
                o_start = orig_lines.size();
            }
            auto orig_len = (o_end - o_start);
            auto m_end = hunk_end;
            if (m_end > mod_lines.size()) {
                m_end = mod_lines.size();
            }
            auto m_start = hunk_start;
            if (m_start > mod_lines.size()) {
                m_start = mod_lines.size();
            }
            auto mod_len = (m_end - m_start);
            diff += "@@ -";
            diff += usize_to_str((hunk_start + 1));
            diff += ",";
            diff += usize_to_str(orig_len);
            diff += " +";
            diff += usize_to_str((hunk_start + 1));
            diff += ",";
            diff += usize_to_str(mod_len);
            diff += " @@\n";
            auto k = hunk_start;
            while (k < hunk_end) {
                if (((k < orig_lines.size()) && (k < mod_lines.size())) && (orig_lines[k] == mod_lines[k])) {
                    diff += " ";
                    diff += orig_lines[k];
                    diff += "\n";
                } else {
                    if (k < orig_lines.size()) {
                        diff += "-";
                        diff += orig_lines[k];
                        diff += "\n";
                    }
                    if (k < mod_lines.size()) {
                        diff += "+";
                        diff += mod_lines[k];
                        diff += "\n";
                    }
                }
                k = (k + 1);
            }
            i = hunk_end;
        }
        m_idx = (m_idx + 1);
    }
    return diff;
}

[[nodiscard]] inline auto to_json(const RenameResult& res) -> std::string {
    if (!res.success) {
        std::string err_json = "{\"success\":false,\"error\":\"";
        err_json += json_escape(res.error);
        err_json += "\"}\n";
        return err_json;
    }
    std::string json_str = "{\n";
    json_str += "  \"success\": true,\n";
    json_str += "  \"old_name\": \"";
    json_str += json_escape(res.old_name);
    json_str += "\",\n";
    json_str += "  \"new_name\": \"";
    json_str += json_escape(res.new_name);
    json_str += "\",\n";
    json_str += "  \"files_modified\": ";
    json_str += usize_to_str(res.modifications.size());
    json_str += ",\n";
    json_str += "  \"total_occurrences\": ";
    json_str += usize_to_str(res.total_occurrences);
    json_str += ",\n";
    json_str += "  \"files\": [\n";
    std::size_t i = 0;
    auto num_mods = res.modifications.size();
    while (i < num_mods) {
        if (i > 0) {
            json_str += ",\n";
        }
        auto mod = res.modifications[i];
        json_str += "    {\n";
        json_str += "      \"file\": \"";
        json_str += json_escape(mod.file_path);
        json_str += "\",\n";
        json_str += "      \"occurrences_count\": ";
        json_str += usize_to_str(mod.occurrences.size());
        json_str += ",\n";
        json_str += "      \"occurrences\": [";
        std::size_t j = 0;
        auto num_occs = mod.occurrences.size();
        while (j < num_occs) {
            if (j > 0) {
                json_str += ", ";
            }
            auto occ = mod.occurrences[j];
            json_str += "{\"line\":";
            json_str += usize_to_str(occ.line);
            json_str += ",\"col\":";
            json_str += usize_to_str(occ.col);
            json_str += ",\"end_col\":";
            json_str += usize_to_str(occ.end_col);
            json_str += "}";
            j = (j + 1);
        }
        json_str += "]\n    }";
        i = (i + 1);
    }
    json_str += "\n  ]\n}\n";
    return json_str;
}

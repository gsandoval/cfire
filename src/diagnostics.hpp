#pragma once
// src/diagnostics.hpp — Structured Diagnostics, 1-Based Source Locations & Auto-Fix Synthesizer

#include <string>
#include <string_view>
#include <vector>
#include <optional>
#include <format>
#include <cctype>
#include <algorithm>
#include <filesystem>
#include <unordered_set>
#include "cfire_prelude.hpp"
#include "ast.hpp"
#include "docgen/catalog.hpp"

namespace cfire::diag {

struct FixItEdit {
    std::string title{};
    int start_line = 1; // 1-based
    int start_col = 1;  // 1-based
    int end_line = 1;   // 1-based
    int end_col = 1;    // 1-based
    std::string replacement{};
};

struct StructuredDiagnostic {
    std::string file{};
    int line = 1;       // 1-based
    int col = 1;        // 1-based
    std::string severity{}; // "error" | "warning"
    std::string code{};     // e.g. "cfire/MUT-CALLSITE"
    std::string message{};  // clean message without redundant "<file>:<line>:<col>: " prefix
    std::vector<FixItEdit> fixits{};
};

[[nodiscard]] inline auto json_escape(std::string_view s) -> std::string {
    std::string out;
    out.reserve(s.size() + 16);
    for (char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    out += std::format("\\u{:04x}", static_cast<unsigned int>(c));
                } else {
                    out += c;
                }
                break;
        }
    }
    return out;
}

[[nodiscard]] inline auto parse_diagnostic_string(
    std::string_view err_str,
    std::string_view default_file = "",
    std::string_view default_severity = "error",
    std::string_view default_code = "cfire/ERROR"
) -> StructuredDiagnostic {
    StructuredDiagnostic d;
    d.file = std::string(default_file);
    d.severity = std::string(default_severity);
    d.code = std::string(default_code);
    d.line = 1;
    d.col = 1;

    std::size_t c1 = std::string_view::npos;
    if (!default_file.empty() && err_str.starts_with(default_file)) {
        c1 = err_str.find(':', default_file.size());
    } else {
        c1 = err_str.find(':');
    }

    if (c1 != std::string_view::npos && c1 > 0) {
        auto c2 = err_str.find(':', c1 + 1);
        if (c2 != std::string_view::npos) {
            auto c3 = err_str.find(':', c2 + 1);
            if (c3 != std::string_view::npos) {
                auto parsed_line = cfire::detail::parse_int<int>(err_str.substr(c1 + 1, c2 - c1 - 1));
                auto parsed_col = cfire::detail::parse_int<int>(err_str.substr(c2 + 1, c3 - c2 - 1));
                if (parsed_line && *parsed_line > 0 && parsed_col && *parsed_col > 0) {
                    d.file = std::string(err_str.substr(0, c1));
                    d.line = *parsed_line;
                    d.col = *parsed_col;

                    std::string_view rest = err_str.substr(c3 + 1);
                    while (!rest.empty() && std::isspace(static_cast<unsigned char>(rest.front()))) {
                        rest.remove_prefix(1);
                    }
                    if (rest.starts_with("warning: ")) {
                        d.severity = "warning";
                        rest.remove_prefix(9);
                    } else if (rest.starts_with("error: ")) {
                        d.severity = "error";
                        rest.remove_prefix(7);
                    }
                    d.message = std::string(rest);
                } else {
                    d.message = std::string(err_str);
                }
            } else {
                d.message = std::string(err_str);
            }
        } else {
            d.message = std::string(err_str);
        }
    } else {
        d.message = std::string(err_str);
    }

    // Extract diagnostic code suffix (e.g. `(cfire/MUT-CALLSITE)`)
    if (auto p = d.message.find("cfire/"); p != std::string::npos) {
        std::size_t e = p + 6;
        while (e < d.message.size() && (std::isalnum(static_cast<unsigned char>(d.message[e])) || d.message[e] == '-')) {
            ++e;
        }
        d.code = d.message.substr(p, e - p);
    }

    return d;
}

[[nodiscard]] inline auto split_lines(std::string_view text) -> std::vector<std::string> {
    std::vector<std::string> lines;
    std::string cur;
    for (char c : text) {
        if (c == '\n') {
            lines.push_back(std::move(cur));
            cur.clear();
        } else if (c != '\r') {
            cur += c;
        }
    }
    lines.push_back(std::move(cur));
    return lines;
}

inline void synthesize_fixits_for_diagnostic(
    StructuredDiagnostic& d,
    const cfire::frontend::FileAST* ast,
    const std::vector<std::string>& lines
) {
    if (d.code == "cfire/MUT-CALLSITE") {
        FixItEdit edit;
        edit.title = "Insert 'mut ' at call site (cfire/MUT-CALLSITE)";
        edit.start_line = d.line;
        edit.start_col = d.col;
        edit.end_line = d.line;
        edit.end_col = d.col;
        edit.replacement = "mut ";
        d.fixits.push_back(std::move(edit));
        return;
    }

    if (d.code == "cfire/OUT-CALLSITE") {
        FixItEdit edit;
        edit.title = "Insert 'out ' at call site (cfire/OUT-CALLSITE)";
        edit.start_line = d.line;
        edit.start_col = d.col;
        edit.end_line = d.line;
        edit.end_col = d.col;
        edit.replacement = "out ";
        d.fixits.push_back(std::move(edit));
        return;
    }

    if (d.code == "cfire/IMMUTABLE-MUT-ARG" || d.code == "cfire/MUT-RECEIVER") {
        std::string var_name;
        if (auto p = d.message.find("immutable variable '"); p != std::string::npos) {
            p += 20;
            if (auto e = d.message.find('\'', p); e != std::string::npos) {
                var_name = d.message.substr(p, e - p);
            }
        }
        if (!var_name.empty()) {
            // Find declaration of var_name up to (d.line - 1) in lines
            int target_line = -1;
            int target_col = -1;
            for (int ln = std::min(d.line - 2, static_cast<int>(lines.size()) - 1); ln >= 0; --ln) {
                const auto& cur_ln = lines[static_cast<std::size_t>(ln)];
                auto pos = cur_ln.find(var_name);
                if (pos != std::string::npos) {
                    bool left_ok = (pos == 0) || (!std::isalnum(static_cast<unsigned char>(cur_ln[pos - 1])) && cur_ln[pos - 1] != '_');
                    std::size_t after = pos + var_name.size();
                    bool right_ok = (after >= cur_ln.size()) || (!std::isalnum(static_cast<unsigned char>(cur_ln[after])) && cur_ln[after] != '_');
                    if (left_ok && right_ok) {
                        // Ensure it's a declaration: followed by `:=` or `:` or `=`:
                        std::string_view tail = std::string_view(cur_ln).substr(after);
                        while (!tail.empty() && std::isspace(static_cast<unsigned char>(tail.front()))) tail.remove_prefix(1);
                        if (tail.starts_with(":=") || tail.starts_with(":") || tail.starts_with("=")) {
                            std::string_view head = std::string_view(cur_ln).substr(0, pos);
                            while (!head.empty() && std::isspace(static_cast<unsigned char>(head.back()))) head.remove_suffix(1);
                            if (!head.ends_with("mut")) {
                                target_line = ln + 1; // 1-based
                                target_col = static_cast<int>(pos) + 1; // 1-based
                                break;
                            }
                        }
                    }
                }
            }
            if (target_line > 0 && target_col > 0) {
                FixItEdit edit;
                edit.title = std::format("Declare '{}' as mutable ('mut {}')", var_name, var_name);
                edit.start_line = target_line;
                edit.start_col = target_col;
                edit.end_line = target_line;
                edit.end_col = target_col;
                edit.replacement = "mut ";
                d.fixits.push_back(std::move(edit));
            }
        }
        return;
    }

    if (d.code == "cfire/NON-EXHAUSTIVE-MATCH" && ast != nullptr) {
        std::string enum_name;
        if (auto p = d.message.find("non-exhaustive match on enum '"); p != std::string::npos) {
            p += 30;
            if (auto e = d.message.find('\'', p); e != std::string::npos) {
                enum_name = d.message.substr(p, e - p);
            }
        }
        if (!enum_name.empty()) {
            const cfire::frontend::EnumDecl* target_enum = nullptr;
            for (const auto& en : ast->enums) {
                if (en.name == enum_name) {
                    target_enum = &en;
                    break;
                }
            }
            if (target_enum) {
                // Find closing brace of match statement around d.line
                int match_close_line = -1;
                int brace_depth = 0;
                bool found_open_brace = false;

                int start_0 = std::max(0, d.line - 1);
                for (int ln = start_0; ln < static_cast<int>(lines.size()); ++ln) {
                    const std::string& cur_ln = lines[static_cast<std::size_t>(ln)];
                    for (int c_idx = 0; c_idx < static_cast<int>(cur_ln.size()); ++c_idx) {
                        char c = cur_ln[static_cast<std::size_t>(c_idx)];
                        if (c == '{') {
                            found_open_brace = true;
                            ++brace_depth;
                        } else if (c == '}') {
                            if (found_open_brace) {
                                --brace_depth;
                                if (brace_depth == 0) {
                                    match_close_line = ln + 1; // 1-based
                                    break;
                                }
                            }
                        }
                    }
                    if (match_close_line != -1) break;
                }

                if (match_close_line != -1) {
                    std::string base_indent;
                    if (start_0 < static_cast<int>(lines.size())) {
                        const std::string& match_ln = lines[static_cast<std::size_t>(start_0)];
                        for (char c : match_ln) {
                            if (c == ' ' || c == '\t') base_indent += c;
                            else break;
                        }
                    }
                    std::string arm_indent = base_indent + "    ";

                    // Collect missing variants
                    std::vector<std::string> missing;
                    for (const auto& v : target_enum->variants) {
                        if (d.message.find(std::format("missing variant '{}'", v.name)) != std::string::npos) {
                            missing.push_back(v.name);
                        }
                    }
                    if (missing.empty()) {
                        for (const auto& v : target_enum->variants) {
                            missing.push_back(v.name);
                        }
                    }

                    std::string arms_text;
                    for (const auto& m : missing) {
                        arms_text += arm_indent + enum_name + "::" + m + " => {}\n";
                    }

                    if (!arms_text.empty()) {
                        FixItEdit edit;
                        edit.title = std::format("Add missing match arm(s) for enum '{}'", enum_name);
                        edit.start_line = match_close_line;
                        edit.start_col = 1;
                        edit.end_line = match_close_line;
                        edit.end_col = 1;
                        edit.replacement = arms_text;
                        d.fixits.push_back(std::move(edit));
                    }
                }
            }
        }
    }
}

[[nodiscard]] inline auto serialize_diagnostics_json(
    std::string_view file,
    bool success,
    const std::vector<StructuredDiagnostic>& diags
) -> std::string {
    std::string json;
    json.reserve(256 + diags.size() * 128);
    json += std::format("{{\"success\": {}, \"file\": \"{}\", \"diagnostics\": [",
        success ? "true" : "false", json_escape(file));

    for (std::size_t i = 0; i < diags.size(); ++i) {
        if (i > 0) json += ", ";
        const auto& d = diags[i];
        json += std::format(
            "{{\"file\": \"{}\", \"line\": {}, \"col\": {}, \"severity\": \"{}\", \"code\": \"{}\", \"message\": \"{}\", \"fixits\": [",
            json_escape(d.file), d.line, d.col, json_escape(d.severity), json_escape(d.code), json_escape(d.message));

        for (std::size_t j = 0; j < d.fixits.size(); ++j) {
            if (j > 0) json += ", ";
            const auto& fix = d.fixits[j];
            json += std::format(
                "{{\"title\": \"{}\", \"start_line\": {}, \"start_col\": {}, \"end_line\": {}, \"end_col\": {}, \"replacement\": \"{}\"}}",
                json_escape(fix.title), fix.start_line, fix.start_col, fix.end_line, fix.end_col, json_escape(fix.replacement));
        }
        json += "]}";
    }
    json += "]}";
    return json;
}

inline auto detect_missing_stdlib_imports(
    const cfire::frontend::FileAST& ast,
    const std::vector<std::string>& lines
) -> std::vector<FixItEdit> {
    std::vector<FixItEdit> edits;
    auto catalog = cfire::doc::stdlib_catalog();

    std::unordered_set<std::string> existing_imports;
    for (const auto& imp : ast.imports) {
        existing_imports.insert(imp.path);
    }

    int insert_line = 1;
    for (std::size_t i = 0; i < lines.size(); ++i) {
        std::string_view l = lines[i];
        while (!l.empty() && std::isspace(static_cast<unsigned char>(l.front()))) l.remove_prefix(1);
        if (l.starts_with("package ")) {
            insert_line = static_cast<int>(i) + 2; // line after package declaration
            break;
        }
    }

    std::unordered_set<std::string> needed_modules;
    for (const auto& mod : catalog) {
        if (existing_imports.contains(mod.import_path)) continue;
        std::string pattern = mod.ns_alias + ".";
        for (const auto& line : lines) {
            auto pos = line.find(pattern);
            if (pos != std::string::npos) {
                bool left_ok = (pos == 0) || (!std::isalnum(static_cast<unsigned char>(line[pos - 1])) && line[pos - 1] != '_');
                if (left_ok) {
                    needed_modules.insert(mod.import_path);
                    break;
                }
            }
        }
    }

    std::vector<std::string> sorted_needed(needed_modules.begin(), needed_modules.end());
    std::sort(sorted_needed.begin(), sorted_needed.end());

    std::string imports_text;
    for (const auto& imp : sorted_needed) {
        imports_text += std::format("import \"{}\"\n", imp);
    }

    if (!imports_text.empty()) {
        FixItEdit edit;
        edit.title = "Import missing standard library module(s)";
        edit.start_line = insert_line;
        edit.start_col = 1;
        edit.end_line = insert_line;
        edit.end_col = 1;
        edit.replacement = imports_text;
        edits.push_back(std::move(edit));
    }

    return edits;
}

inline auto apply_fixits(
    std::string_view original_content,
    std::vector<FixItEdit> edits
) -> std::pair<std::string, std::vector<FixItEdit>> {
    if (edits.empty()) {
        return {std::string(original_content), {}};
    }

    std::vector<std::size_t> line_offsets;
    line_offsets.push_back(0);
    for (std::size_t i = 0; i < original_content.size(); ++i) {
        if (original_content[i] == '\n') {
            line_offsets.push_back(i + 1);
        }
    }

    struct OffsetEdit {
        FixItEdit edit;
        std::size_t start_byte = 0;
        std::size_t end_byte = 0;
    };

    std::vector<OffsetEdit> offset_edits;
    for (auto& e : edits) {
        if (e.start_line <= 0 || static_cast<std::size_t>(e.start_line - 1) >= line_offsets.size()) continue;
        if (e.end_line <= 0 || static_cast<std::size_t>(e.end_line - 1) >= line_offsets.size()) continue;

        std::size_t s_line_start = line_offsets[static_cast<std::size_t>(e.start_line - 1)];
        std::size_t e_line_start = line_offsets[static_cast<std::size_t>(e.end_line - 1)];

        std::size_t s_byte = s_line_start + static_cast<std::size_t>(std::max(0, e.start_col - 1));
        std::size_t e_byte = e_line_start + static_cast<std::size_t>(std::max(0, e.end_col - 1));

        s_byte = std::min(s_byte, original_content.size());
        e_byte = std::min(e_byte, original_content.size());

        if (s_byte <= e_byte) {
            offset_edits.push_back(OffsetEdit{std::move(e), s_byte, e_byte});
        }
    }

    std::sort(offset_edits.begin(), offset_edits.end(), [](const OffsetEdit& a, const OffsetEdit& b) {
        if (a.start_byte != b.start_byte) return a.start_byte > b.start_byte;
        return a.end_byte > b.end_byte;
    });

    std::string result(original_content);
    std::vector<FixItEdit> applied_edits;
    std::size_t last_start = result.size() + 1;

    for (const auto& oe : offset_edits) {
        if (oe.end_byte <= last_start && oe.start_byte <= oe.end_byte && oe.end_byte <= result.size()) {
            result.replace(oe.start_byte, oe.end_byte - oe.start_byte, oe.edit.replacement);
            last_start = oe.start_byte;
            applied_edits.push_back(oe.edit);
        }
    }

    return {std::move(result), std::move(applied_edits)};
}

[[nodiscard]] inline auto serialize_fix_result_json(
    std::string_view file,
    bool success,
    std::size_t applied_count,
    const std::vector<FixItEdit>& edits
) -> std::string {
    std::string json;
    json += std::format("{{\"success\": {}, \"file\": \"{}\", \"applied_fixes\": {}, \"edits\": [",
        success ? "true" : "false", json_escape(file), applied_count);
    for (std::size_t i = 0; i < edits.size(); ++i) {
        if (i > 0) json += ", ";
        const auto& fix = edits[i];
        json += std::format(
            "{{\"title\": \"{}\", \"start_line\": {}, \"start_col\": {}, \"end_line\": {}, \"end_col\": {}, \"replacement\": \"{}\"}}",
            json_escape(fix.title), fix.start_line, fix.start_col, fix.end_line, fix.end_col, json_escape(fix.replacement));
    }
    json += "]}";
    return json;
}

} // namespace cfire::diag

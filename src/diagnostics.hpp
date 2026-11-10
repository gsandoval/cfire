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
#include "docgen/catalog_types.hpp"

namespace cfire::diag {

struct FixItEdit {
    std::string title{};
    int start_line = 1; // 1-based
    int start_col = 1;  // 1-based
    int end_line = 1;   // 1-based
    int end_col = 1;    // 1-based
    std::string replacement{};
    bool is_preferred = true;
};

struct StructuredDiagnostic {
    std::string file{};
    int line = 1;       // 1-based
    int col = 1;        // 1-based
    int end_line = 1;   // 1-based
    int end_col = 1;    // 1-based
    std::string severity{}; // "error" | "warning" | "info"
    std::string code{};     // e.g. "cfire/MUT-CALLSITE"
    std::string message{};  // clean message without redundant "<file>:<line>:<col>: " prefix
    std::string source{"cfire"};
    std::vector<FixItEdit> fixits{};
};

using Diagnostic = StructuredDiagnostic;

[[nodiscard]] inline auto json_escape(std::string_view s) -> std::string {
    return cfire::detail::json_escape(s);
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
    d.end_line = 1;
    d.end_col = 1;
    d.source = "cfire";

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
                    d.end_line = *parsed_line;
                    d.end_col = *parsed_col + 1;

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
    const std::vector<std::string>& lines,
    const std::vector<const cfire::frontend::EnumDecl*>& extra_enums = {}
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

    if (d.code == "cfire/MOVE-CALLSITE") {
        FixItEdit edit;
        edit.title = "Insert 'move ' at call site (cfire/MOVE-CALLSITE)";
        edit.start_line = d.line;
        edit.start_col = d.col;
        edit.end_line = d.line;
        edit.end_col = d.col;
        edit.replacement = "move ";
        d.fixits.push_back(std::move(edit));
        return;
    }

    if (d.code == "cfire/REDUNDANT-TRY") {
        if (d.line >= 1 && static_cast<std::size_t>(d.line - 1) < lines.size()) {
            const std::string& cur_ln = lines[static_cast<std::size_t>(d.line - 1)];
            std::size_t search_from = (d.col > 1 && static_cast<std::size_t>(d.col - 1) < cur_ln.size())
                ? static_cast<std::size_t>(d.col - 1)
                : 0;
            if (auto pos = cur_ln.find("try ", search_from); pos != std::string::npos) {
                FixItEdit edit;
                edit.title = "Remove redundant 'try ' (cfire/REDUNDANT-TRY)";
                edit.start_line = d.line;
                edit.start_col = static_cast<int>(pos) + 1;
                edit.end_line = d.line;
                edit.end_col = static_cast<int>(pos) + 5;
                edit.replacement = "";
                edit.is_preferred = true;
                d.fixits.push_back(std::move(edit));
            }
        }
        return;
    }

    if (d.code == "cfire/REDUNDANT-CALLSITE-INTENT") {
        if (d.line >= 1 && static_cast<std::size_t>(d.line - 1) < lines.size()) {
            const std::string& cur_ln = lines[static_cast<std::size_t>(d.line - 1)];
            std::size_t search_from = (d.col > 1 && static_cast<std::size_t>(d.col - 1) < cur_ln.size())
                ? static_cast<std::size_t>(d.col - 1)
                : 0;
            std::string_view kw = d.message.contains("redundant 'out'") ? "out " : "mut ";
            if (auto pos = cur_ln.find(kw, search_from); pos != std::string::npos) {
                FixItEdit edit;
                edit.title = std::format("Remove redundant '{}' at call site (cfire/REDUNDANT-CALLSITE-INTENT)", kw);
                edit.start_line = d.line;
                edit.start_col = static_cast<int>(pos) + 1;
                edit.end_line = d.line;
                edit.end_col = static_cast<int>(pos + kw.size()) + 1;
                edit.replacement = "";
                edit.is_preferred = true;
                d.fixits.push_back(std::move(edit));
            }
        }
        return;
    }

    if (d.code == "cfire/UNHANDLED-RESULT") {
        if (d.line > 0 && d.col > 0) {
            FixItEdit edit;
            edit.title = "Add 'try' before fallible call (cfire/UNHANDLED-RESULT)";
            edit.start_line = d.line;
            edit.start_col = d.col;
            edit.end_line = d.line;
            edit.end_col = d.col;
            edit.replacement = "try ";
            edit.is_preferred = true;
            d.fixits.push_back(std::move(edit));
        }
        return;
    }

    if (d.code == "cfire/MISSING-IMPORT") {
        std::string mod_path;
        if (auto p = d.message.find("import \""); p != std::string::npos) {
            p += 8;
            if (auto e = d.message.find('"', p); e != std::string::npos) {
                mod_path = d.message.substr(p, e - p);
            }
        }
        if (!mod_path.empty()) {
            int insert_line = 1;
            for (std::size_t i = 0; i < lines.size(); ++i) {
                std::string_view l = lines[i];
                while (!l.empty() && std::isspace(static_cast<unsigned char>(l.front()))) l.remove_prefix(1);
                if (l.starts_with("package ")) {
                    insert_line = static_cast<int>(i) + 2;
                    break;
                }
            }
            FixItEdit edit;
            edit.title = std::format("Import missing module '{}' (cfire/MISSING-IMPORT)", mod_path);
            edit.start_line = insert_line;
            edit.start_col = 1;
            edit.end_line = insert_line;
            edit.end_col = 1;
            edit.replacement = std::format("import \"{}\"\n", mod_path);
            edit.is_preferred = true;
            d.fixits.push_back(std::move(edit));
        }
        return;
    }

    if (d.code == "cfire/INVALID-IMPORT") {
        std::string old_imp;
        std::string new_imp;
        if (auto p = d.message.find("module '"); p != std::string::npos) {
            p += 8;
            if (auto e = d.message.find('\'', p); e != std::string::npos) {
                old_imp = d.message.substr(p, e - p);
            }
        }
        if (auto p = d.message.find("imported as '"); p != std::string::npos) {
            p += 13;
            if (auto e = d.message.find('\'', p); e != std::string::npos) {
                new_imp = d.message.substr(p, e - p);
            }
        }
        if (!old_imp.empty() && !new_imp.empty() && d.line >= 1 && static_cast<std::size_t>(d.line - 1) < lines.size()) {
            const std::string& cur_ln = lines[static_cast<std::size_t>(d.line - 1)];
            std::string target_str = "\"" + old_imp + "\"";
            if (auto pos = cur_ln.find(target_str); pos != std::string::npos) {
                FixItEdit edit;
                edit.title = std::format("Replace '{}' with '{}' (cfire/INVALID-IMPORT)", old_imp, new_imp);
                edit.start_line = d.line;
                edit.start_col = static_cast<int>(pos) + 1;
                edit.end_line = d.line;
                edit.end_col = static_cast<int>(pos + target_str.size()) + 1;
                edit.replacement = "\"" + new_imp + "\"";
                edit.is_preferred = true;
                d.fixits.push_back(std::move(edit));
                return;
            }
        }
    }

    if (d.code == "cfire/DEPRECATED-API") {
        if (d.line >= 1 && static_cast<std::size_t>(d.line - 1) < lines.size()) {
            const std::string& cur_ln = lines[static_cast<std::size_t>(d.line - 1)];
            auto try_replace_token = [&](std::string_view from_tok, std::string_view to_tok) -> bool {
                if (auto pos = cur_ln.find(from_tok); pos != std::string::npos) {
                    FixItEdit edit;
                    edit.title = std::format("Replace '{}' with '{}' (cfire/DEPRECATED-API)", from_tok, to_tok);
                    edit.start_line = d.line;
                    edit.start_col = static_cast<int>(pos) + 1;
                    edit.end_line = d.line;
                    edit.end_col = static_cast<int>(pos + from_tok.size()) + 1;
                    edit.replacement = std::string(to_tok);
                    edit.is_preferred = true;
                    d.fixits.push_back(std::move(edit));
                    return true;
                }
                return false;
            };
            if (d.message.contains("'fs.read_all'")) {
                if (try_replace_token("fs.read_all", "fs.read_bytes")) return;
            } else if (d.message.contains("'fs.write_all'")) {
                if (try_replace_token("fs.write_all", "fs.write_bytes")) return;
            } else if (d.message.contains("'os.read_stdin_lines'")) {
                if (try_replace_token("os.read_stdin_lines", "try os.stdin().read_lines")) return;
            } else if (d.message.contains("'os.read_stdin_bytes'")) {
                if (try_replace_token("os.read_stdin_bytes", "try os.stdin().read_bytes")) return;
            } else if (d.message.contains("'os.read_stdin'")) {
                if (try_replace_token("os.read_stdin", "try os.stdin().read_text")) return;
            } else if (d.message.contains("'.read_str'")) {
                if (try_replace_token(".read_str", ".read_text")) return;
            } else if (d.message.contains("'.write_str'")) {
                if (try_replace_token(".write_str", ".write_text")) return;
            }
        }
        return;
    }

    if (d.code == "cfire/DEPRECATED-VAR") {
        if (d.line >= 1 && static_cast<std::size_t>(d.line - 1) < lines.size()) {
            const std::string& cur_ln = lines[static_cast<std::size_t>(d.line - 1)];
            if (auto vpos = cur_ln.find("var "); vpos != std::string::npos) {
                FixItEdit edit;
                edit.start_line = d.line;
                edit.end_line = d.line;
                edit.is_preferred = true;
                if (cur_ln.substr(vpos).starts_with("var mut ")) {
                    edit.title = "Replace 'var mut' with 'mut' (cfire/DEPRECATED-VAR)";
                    edit.start_col = static_cast<int>(vpos) + 1;
                    edit.end_col = static_cast<int>(vpos + 8) + 1;
                    edit.replacement = "mut ";
                    d.fixits.push_back(std::move(edit));
                    return;
                } else if (auto eq_pos = cur_ln.find(" = ", vpos); eq_pos != std::string::npos && cur_ln.find(':', vpos) > eq_pos) {
                    edit.title = "Replace 'var x = ...' with 'x := ...' (cfire/DEPRECATED-VAR)";
                    edit.start_col = static_cast<int>(vpos) + 1;
                    edit.end_col = static_cast<int>(cur_ln.size()) + 1;
                    std::string line_tail = cur_ln.substr(vpos + 4);
                    if (auto eq_in_tail = line_tail.find(" = "); eq_in_tail != std::string::npos) {
                        line_tail.replace(eq_in_tail, 3, " := ");
                    }
                    edit.replacement = line_tail;
                    d.fixits.push_back(std::move(edit));
                    return;
                } else {
                    edit.title = "Remove 'var' keyword (cfire/DEPRECATED-VAR)";
                    edit.start_col = static_cast<int>(vpos) + 1;
                    edit.end_col = static_cast<int>(vpos + 4) + 1;
                    edit.replacement = "";
                    d.fixits.push_back(std::move(edit));
                    return;
                }
            }
        }
        return;
    }

    if (d.code == "cfire/DEPRECATED-DISCARD") {
        if (d.line >= 1 && static_cast<std::size_t>(d.line - 1) < lines.size()) {
            const std::string& cur_ln = lines[static_cast<std::size_t>(d.line - 1)];
            if (auto pos = cur_ln.find(":="); pos != std::string::npos) {
                FixItEdit edit;
                edit.title = "Replace ':=' with '=' for discard (cfire/DEPRECATED-DISCARD)";
                edit.start_line = d.line;
                edit.start_col = static_cast<int>(pos) + 1;
                edit.end_line = d.line;
                edit.end_col = static_cast<int>(pos + 2) + 1;
                edit.replacement = "=";
                edit.is_preferred = true;
                d.fixits.push_back(std::move(edit));
                return;
            }
        }
        return;
    }

    if (d.code == "cfire/DEPRECATED-IN-PARAM") {
        if (d.line >= 1 && static_cast<std::size_t>(d.line - 1) < lines.size()) {
            const std::string& cur_ln = lines[static_cast<std::size_t>(d.line - 1)];
            if (auto pos = cur_ln.find("in "); pos != std::string::npos) {
                FixItEdit edit;
                edit.title = "Remove redundant 'in' parameter intent (cfire/DEPRECATED-IN-PARAM)";
                edit.start_line = d.line;
                edit.start_col = static_cast<int>(pos) + 1;
                edit.end_line = d.line;
                edit.end_col = static_cast<int>(pos + 3) + 1;
                edit.replacement = "";
                edit.is_preferred = true;
                d.fixits.push_back(std::move(edit));
                return;
            }
        }
        return;
    }

    if (d.code == "cfire/DEPRECATED-INTENT-POS") {
        if (d.line >= 1 && static_cast<std::size_t>(d.line - 1) < lines.size()) {
            const std::string& cur_ln = lines[static_cast<std::size_t>(d.line - 1)];
            if (auto mpos = cur_ln.find(": mut "); mpos != std::string::npos) {
                auto id_end = cur_ln.rfind(' ', mpos);
                auto id_start = (id_end == std::string::npos) ? 0 : id_end + 1;
                if (id_end == std::string::npos) {
                    auto p_start = cur_ln.rfind('(', mpos);
                    id_start = (p_start == std::string::npos) ? 0 : p_start + 1;
                }
                while (id_start < mpos && (cur_ln[id_start] == ' ' || cur_ln[id_start] == '(' || cur_ln[id_start] == ',')) ++id_start;
                std::string pname = cur_ln.substr(id_start, mpos - id_start);
                FixItEdit edit;
                edit.title = std::format("Move 'mut' before parameter name: 'mut {}:' (cfire/DEPRECATED-INTENT-POS)", pname);
                edit.start_line = d.line;
                edit.start_col = static_cast<int>(id_start) + 1;
                edit.end_line = d.line;
                edit.end_col = static_cast<int>(mpos + 6) + 1;
                edit.replacement = "mut " + pname + ": ";
                edit.is_preferred = true;
                d.fixits.push_back(std::move(edit));
                return;
            }
        }
        return;
    }

    if (d.code == "cfire/DEPRECATED-SEPARATOR") {
        if (d.line >= 1 && static_cast<std::size_t>(d.line - 1) < lines.size()) {
            const std::string& cur_ln = lines[static_cast<std::size_t>(d.line - 1)];
            if (d.col >= 1 && static_cast<std::size_t>(d.col - 1) < cur_ln.size()) {
                auto dot_pos = cur_ln.rfind('.', static_cast<std::size_t>(d.col - 1));
                if (dot_pos == std::string::npos) dot_pos = cur_ln.find('.', static_cast<std::size_t>(d.col - 1));
                if (dot_pos != std::string::npos) {
                    FixItEdit edit;
                    edit.title = "Replace '.' with '::' for enum variant (cfire/DEPRECATED-SEPARATOR)";
                    edit.start_line = d.line;
                    edit.start_col = static_cast<int>(dot_pos) + 1;
                    edit.end_line = d.line;
                    edit.end_col = static_cast<int>(dot_pos + 1) + 1;
                    edit.replacement = "::";
                    edit.is_preferred = true;
                    d.fixits.push_back(std::move(edit));
                    return;
                }
            }
        }
        return;
    }

    if (d.code == "cfire/REDUNDANT-DEREF") {
        if (d.line >= 1 && static_cast<std::size_t>(d.line - 1) < lines.size()) {
            const std::string& cur_ln = lines[static_cast<std::size_t>(d.line - 1)];
            if (auto pos = cur_ln.find("(*"); pos != std::string::npos) {
                if (auto end_p = cur_ln.find(").", pos); end_p != std::string::npos) {
                    std::string id = cur_ln.substr(pos + 2, end_p - (pos + 2));
                    FixItEdit edit;
                    edit.title = std::format("Replace '(*{}).' with '{}.' (cfire/REDUNDANT-DEREF)", id, id);
                    edit.start_line = d.line;
                    edit.start_col = static_cast<int>(pos) + 1;
                    edit.end_line = d.line;
                    edit.end_col = static_cast<int>(end_p + 2) + 1;
                    edit.replacement = id + ".";
                    edit.is_preferred = true;
                    d.fixits.push_back(std::move(edit));
                    return;
                }
            }
        }
        return;
    }

    if (d.code == "cfire/NULL-DEREF") {
        std::string ptr_name;
        if (auto p = d.message.find("pointer '"); p != std::string::npos) {
            p += 9;
            if (auto e = d.message.find('\'', p); e != std::string::npos) {
                ptr_name = d.message.substr(p, e - p);
            }
        }
        if (!ptr_name.empty() && d.line >= 1 && static_cast<std::size_t>(d.line - 1) < lines.size()) {
            const std::string& cur_ln = lines[static_cast<std::size_t>(d.line - 1)];
            std::string indent;
            for (char c : cur_ln) {
                if (c == ' ' || c == '\t') indent += c;
                else break;
            }
            FixItEdit edit;
            edit.title = std::format("Insert 'assert({} != none)' guard before dereference (cfire/NULL-DEREF)", ptr_name);
            edit.start_line = d.line;
            edit.start_col = 1;
            edit.end_line = d.line;
            edit.end_col = 1;
            edit.replacement = std::format("{}assert({} != none)\n", indent, ptr_name);
            d.fixits.push_back(std::move(edit));
            return;
        }
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

    if (d.code == "cfire/NON-EXHAUSTIVE-MATCH") {
        std::string enum_name;
        std::string fallback_variant;
        if (auto p = d.message.find("non-exhaustive match on enum '"); p != std::string::npos) {
            p += 30;
            if (auto e = d.message.find('\'', p); e != std::string::npos) {
                enum_name = d.message.substr(p, e - p);
            }
        }
        if (auto p = d.message.find("missing variant '"); p != std::string::npos) {
            p += 17;
            if (auto e = d.message.find('\'', p); e != std::string::npos) {
                fallback_variant = d.message.substr(p, e - p);
            }
        }

        const cfire::frontend::EnumDecl* target_enum = nullptr;
        if (!enum_name.empty()) {
            if (ast != nullptr) {
                for (const auto& en : ast->enums) {
                    if (en.name == enum_name) {
                        target_enum = &en;
                        break;
                    }
                }
            }
            if (!target_enum) {
                for (const auto* en : extra_enums) {
                    if (en && en->name == enum_name) {
                        target_enum = en;
                        break;
                    }
                }
            }
        }

        int start_0 = std::max(0, d.line - 1);
        std::string base_indent;
        if (static_cast<std::size_t>(start_0) < lines.size()) {
            const std::string& match_ln = lines[static_cast<std::size_t>(start_0)];
            for (char c : match_ln) {
                if (c == ' ' || c == '\t') base_indent += c;
                else break;
            }
        }
        std::string arm_indent = base_indent + "    ";
        std::string body_indent = arm_indent + "    ";

        int match_close_line = -1; // 0-based
        int match_close_col = 0;   // 0-based
        int brace_depth = 0;
        bool found_open_brace = false;

        for (int ln = start_0; ln < static_cast<int>(lines.size()); ++ln) {
            const std::string& cur_ln = lines[static_cast<std::size_t>(ln)];
            for (int col = 0; col < static_cast<int>(cur_ln.size()); ++col) {
                char c = cur_ln[static_cast<std::size_t>(col)];
                if (c == '{') {
                    found_open_brace = true;
                    ++brace_depth;
                } else if (c == '}') {
                    if (found_open_brace) {
                        --brace_depth;
                        if (brace_depth == 0) {
                            match_close_line = ln;
                            match_close_col = col;
                            break;
                        }
                    }
                }
            }
            if (match_close_line != -1) break;
        }

        if (match_close_line == -1) {
            match_close_line = start_0 + 1;
            match_close_col = 0;
        }

        std::unordered_set<std::string> covered;
        if (ast != nullptr) {
            const cfire::frontend::MatchStmt* target_match = nullptr;
            auto find_match = [&](auto self, const std::vector<cfire::frontend::StmtPtr>& stmts) -> void {
                if (target_match) return;
                for (const auto& s : stmts) {
                    if (!s) continue;
                    if (const auto* ms = std::get_if<cfire::frontend::MatchStmt>(&s->node)) {
                        if (ms->line - 1 == start_0 || (ms->line - 1 <= start_0 && start_0 <= match_close_line)) {
                            target_match = ms;
                            return;
                        }
                    }
                    if (const auto* ifs = std::get_if<cfire::frontend::IfStmt>(&s->node)) {
                        self(self, ifs->then_body);
                        self(self, ifs->else_body);
                    } else if (const auto* fs = std::get_if<cfire::frontend::ForStmt>(&s->node)) {
                        self(self, fs->body);
                    } else if (const auto* ms2 = std::get_if<cfire::frontend::MatchStmt>(&s->node)) {
                        for (const auto& arm : ms2->arms) self(self, arm.body);
                    }
                }
            };
            for (const auto& fn : ast->functions) find_match(find_match, fn.body);
            for (const auto& t : ast->tests) find_match(find_match, t.body);
            if (target_match) {
                for (const auto& arm : target_match->arms) {
                    covered.insert(arm.variant_name);
                }
            }
        }

        std::string arms_text;
        if (target_enum) {
            for (const auto& v : target_enum->variants) {
                if (!covered.contains(v.name)) {
                    arms_text += arm_indent + v.name;
                    if (!v.payload_types.empty()) {
                        arms_text += "(";
                        for (std::size_t i = 0; i < v.payload_types.size(); ++i) {
                            if (i > 0) arms_text += ", ";
                            arms_text += std::format("v{}", i);
                        }
                        arms_text += ")";
                    }
                    arms_text += " => {\n" + body_indent + "\n" + arm_indent + "}\n";
                }
            }
        } else if (!fallback_variant.empty()) {
            arms_text = arm_indent + fallback_variant + " => {\n" + body_indent + "\n" + arm_indent + "}\n";
        }

        int edit_ln_0 = match_close_line;
        int edit_col_0 = 0;
        if (match_close_col > 0 && static_cast<std::size_t>(match_close_line) < lines.size()) {
            const std::string& cl_ln = lines[static_cast<std::size_t>(match_close_line)];
            std::size_t first_non_ws = cl_ln.find_first_not_of(" \t");
            if (first_non_ws != std::string::npos && static_cast<int>(first_non_ws) == match_close_col) {
                edit_col_0 = 0;
            } else {
                edit_col_0 = match_close_col;
                arms_text = "\n" + arms_text + base_indent;
            }
        }

        if (!arms_text.empty()) {
            FixItEdit edit;
            edit.title = std::format("Fill missing match arms for '{}' (cfire/NON-EXHAUSTIVE-MATCH)",
                enum_name.empty() ? "enum" : enum_name);
            edit.start_line = edit_ln_0 + 1;
            edit.start_col = edit_col_0 + 1;
            edit.end_line = edit_ln_0 + 1;
            edit.end_col = edit_col_0 + 1;
            edit.replacement = arms_text;
            edit.is_preferred = true;
            d.fixits.push_back(std::move(edit));
        }

        std::string wildcard_text = arm_indent + "_ => {\n" + body_indent + "\n" + arm_indent + "}\n";
        if (edit_col_0 > 0) {
            wildcard_text = "\n" + wildcard_text + base_indent;
        }
        FixItEdit wc_edit;
        wc_edit.title = "Add wildcard pattern '_ => { ... }' (cfire/NON-EXHAUSTIVE-MATCH)";
        wc_edit.start_line = edit_ln_0 + 1;
        wc_edit.start_col = edit_col_0 + 1;
        wc_edit.end_line = edit_ln_0 + 1;
        wc_edit.end_col = edit_col_0 + 1;
        wc_edit.replacement = wildcard_text;
        wc_edit.is_preferred = false;
        d.fixits.push_back(std::move(wc_edit));
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
            "{{\"file\": \"{}\", \"line\": {}, \"col\": {}, \"end_line\": {}, \"end_col\": {}, \"severity\": \"{}\", \"code\": \"{}\", \"source\": \"{}\", \"message\": \"{}\", \"fixits\": [",
            json_escape(d.file), d.line, d.col, d.end_line, d.end_col, json_escape(d.severity), json_escape(d.code), json_escape(d.source), json_escape(d.message));

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

[[nodiscard]] inline auto serialize_diagnostics_json(
    const std::vector<StructuredDiagnostic>& diags
) -> std::string {
    bool ok = true;
    for (const auto& d : diags) {
        if (d.severity == "error") { ok = false; break; }
    }
    return serialize_diagnostics_json("", ok, diags);
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
        if (!applied_edits.empty()) {
            const auto& prev = applied_edits.back();
            if (prev.start_line == oe.edit.start_line &&
                prev.start_col == oe.edit.start_col &&
                prev.end_line == oe.edit.end_line &&
                prev.end_col == oe.edit.end_col &&
                prev.replacement == oe.edit.replacement) {
                continue;
            }
        }
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

[[nodiscard]] inline auto humanize_cpp_compiler_output(std::string_view raw_out) -> std::string {
    std::string s(raw_out);
    auto replace_all = [](std::string& str, std::string_view from, std::string_view to) {
        if (from.empty()) return;
        std::size_t pos = 0;
        while ((pos = str.find(from, pos)) != std::string::npos) {
            str.replace(pos, from.size(), to);
            pos += to.size();
        }
    };

    replace_all(s, "std::__1::", "std::");
    replace_all(s, "std::__cxx11::", "std::");
    replace_all(s, "std::basic_string<char, std::char_traits<char>, std::allocator<char>>", "std::string");
    replace_all(s, "std::basic_string<char>", "std::string");
    replace_all(s, "std::basic_string_view<char, std::char_traits<char>>", "std::string_view");
    replace_all(s, "std::basic_string_view<char>", "std::string_view");
    replace_all(s, "std::string_view", "&str");
    replace_all(s, "std::string", "str");
    replace_all(s, "std::int64_t", "i64");
    replace_all(s, "std::int32_t", "i32");
    replace_all(s, "std::int16_t", "i16");
    replace_all(s, "std::int8_t", "i8");
    replace_all(s, "std::uint64_t", "u64");
    replace_all(s, "std::uint32_t", "u32");
    replace_all(s, "std::uint16_t", "u16");
    replace_all(s, "std::uint8_t", "u8");
    replace_all(s, "std::size_t", "usize");
    replace_all(s, "std::byte", "byte");
    replace_all(s, "cfire::fs::File", "fs.File");
    replace_all(s, "cfire::io::File", "fs.File");
    replace_all(s, "cfire::net::TcpConn", "net.TcpConn");
    replace_all(s, "cfire::net::TcpStream", "net.TcpConn");
    replace_all(s, "cfire::net::TcpListener", "net.TcpListener");
    replace_all(s, "std::unexpected<cfire::Error>", "error(...)");
    replace_all(s, "std::unexpected<Error>", "error(...)");
    replace_all(s, "cfire::make_error", "error(...)");
    replace_all(s, "cfire::Error", "Error");
    replace_all(s, "cfire::unwrap_or", "'or' fallback");
    replace_all(s, "cfire::unwrap_val", "'try' unwrap");
    replace_all(s, "cfire::try_unwrap", "'try' unwrap");
    replace_all(s, "cfire::slice", "slice '[a..b]'");
    replace_all(s, "cfire::index_at", "index '[i]'");
    replace_all(s, "no member named 'size' in", "no member '.len' in");
    replace_all(s, "does not provide a subscript operator", "does not support indexing '[i]'");

    // Strip Clang ` (aka '...')` type alias annotations
    {
        std::size_t apos = 0;
        while ((apos = s.find(" (aka '", apos)) != std::string::npos) {
            auto end_pos = s.find("')", apos + 7);
            if (end_pos != std::string::npos && end_pos - apos < 160) {
                s.erase(apos, (end_pos + 2) - apos);
            } else {
                apos += 7;
            }
        }
    }

    // Rewrite `.cfire/gen/<stem>.cpp` paths back to `<stem>.cf`
    {
        std::size_t gpos = 0;
        while ((gpos = s.find(".cfire/gen/", gpos)) != std::string::npos) {
            auto end_pos = s.find(".cpp:", gpos);
            if (end_pos != std::string::npos && end_pos - gpos < 128) {
                std::string stem = s.substr(gpos + 11, end_pos - (gpos + 11));
                if (stem.ends_with("_test")) stem.erase(stem.size() - 5);
                s.replace(gpos, (end_pos + 4) - gpos, stem + ".cf");
            } else {
                gpos += 11;
            }
        }
    }

    // Iteratively rewrite C++ container templates from innermost to outermost
    for (int pass = 0; pass < 6; ++pass) {
        bool changed = false;
        auto rewrite_unary_template = [&](std::string_view prefix, std::string_view open_rep, std::string_view close_rep) {
            std::size_t pos = 0;
            while ((pos = s.find(prefix, pos)) != std::string::npos) {
                std::size_t inner_start = pos + prefix.size();
                int depth = 1;
                std::size_t i = inner_start;
                std::size_t comma_pos = std::string::npos;
                while (i < s.size() && depth > 0) {
                    if (s[i] == '<') ++depth;
                    else if (s[i] == '>') --depth;
                    else if (s[i] == ',' && depth == 1 && comma_pos == std::string::npos) {
                        comma_pos = i;
                    }
                    if (depth > 0) ++i;
                }
                if (i < s.size() && depth == 0) {
                    std::size_t inner_end = (comma_pos != std::string::npos) ? comma_pos : i;
                    std::string inner = s.substr(inner_start, inner_end - inner_start);
                    while (!inner.empty() && inner.back() == ' ') inner.pop_back();
                    std::string rep = std::string(open_rep) + inner + std::string(close_rep);
                    s.replace(pos, i - pos + 1, rep);
                    changed = true;
                } else {
                    pos += prefix.size();
                }
            }
        };
        rewrite_unary_template("std::vector<", "[]", "");
        rewrite_unary_template("std::span<const ", "&[]", "");
        rewrite_unary_template("std::span<", "&mut []", "");
        rewrite_unary_template("std::optional<", "", "?");
        rewrite_unary_template("std::expected<", "", "!");
        rewrite_unary_template("std::unique_ptr<", "own[", "]");
        rewrite_unary_template("std::shared_ptr<", "shared[", "]");
        if (!changed) break;
    }

    // Collapse C++ runtime header template instantiation errors onto the originating `.cf:line:col` call site
    auto is_runtime_hdr_line = [](std::string_view ln) -> bool {
        return ln.contains("cfire_prelude.hpp:") ||
               ln.contains("cfire_core_stdlib.hpp:") ||
               ln.contains("cfire_test_runtime.hpp:") ||
               ln.contains("/runtime/core/");
    };
    if (s.contains("cfire_prelude.hpp:") || s.contains("cfire_core_stdlib.hpp:") ||
        s.contains("cfire_test_runtime.hpp:") || s.contains("/runtime/core/")) {
        auto raw_lines = split_lines(s);
        std::vector<std::string> out_lines;
        std::vector<bool> skip_line(raw_lines.size(), false);
        for (std::size_t idx = 0; idx < raw_lines.size(); ++idx) {
            if (skip_line[idx]) continue;
            const std::string& ln = raw_lines[idx];
            if (ln.starts_with("In file included from ") &&
                (ln.contains("<built-in>") || ln.contains("cfire_prelude.hpp") || ln.contains("/runtime/"))) {
                continue;
            }
            if (is_runtime_hdr_line(ln) && ln.contains(": error: ")) {
                auto err_pos = ln.find(": error: ");
                std::string err_msg = ln.substr(err_pos + 9);
                std::string cf_loc_prefix;
                for (std::size_t j = idx + 1; j < raw_lines.size() && j <= idx + 12; ++j) {
                    const std::string& nln = raw_lines[j];
                    if (auto note_pos = nln.find(": note: in instantiation of "); note_pos != std::string::npos) {
                        std::string_view prefix_sv(nln.data(), note_pos);
                        if (prefix_sv.contains(".cf:")) {
                            cf_loc_prefix = std::string(prefix_sv);
                            skip_line[j] = true;
                            // Also skip snippet lines immediately following the note or runtime error
                            for (std::size_t k = idx + 1; k < raw_lines.size(); ++k) {
                                if (k == j) continue;
                                std::string_view kl = raw_lines[k];
                                while (!kl.empty() && kl.front() == ' ') kl.remove_prefix(1);
                                if (kl.starts_with("|") || (!kl.empty() && std::isdigit(static_cast<unsigned char>(kl.front())) && raw_lines[k].contains(" | "))) {
                                    skip_line[k] = true;
                                } else if (is_runtime_hdr_line(raw_lines[k]) && raw_lines[k].contains(": note:")) {
                                    skip_line[k] = true;
                                } else if (k > j) {
                                    break;
                                }
                            }
                            break;
                        }
                    }
                }
                if (!cf_loc_prefix.empty()) {
                    if (!err_msg.contains("cfire/")) {
                        err_msg += " (cfire/TYPE-ERROR)";
                    }
                    out_lines.push_back(std::format("{}: error: {}", cf_loc_prefix, err_msg));
                    continue;
                }
            }
            out_lines.push_back(ln);
        }
        std::string rebuilt;
        for (std::size_t i = 0; i < out_lines.size(); ++i) {
            if (i > 0) rebuilt += '\n';
            rebuilt += out_lines[i];
        }
        s = std::move(rebuilt);
    }

    return s;
}

} // namespace cfire::diag

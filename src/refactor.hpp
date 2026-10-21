#pragma once
// src/refactor.hpp — Semantic CLI Renaming & Refactoring Engine for cfire (`cfire refactor rename`)

#include "lsp.hpp"
#include <algorithm>
#include <cctype>
#include <expected>
#include <filesystem>
#include <format>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <unistd.h>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace cfire::refactor {

struct Occurrence {
    std::string file_path;
    int line{0};     // 1-indexed
    int col{0};      // 1-indexed
    int end_col{0};  // 1-indexed
};

struct FileModification {
    std::string file_path;
    std::string original_content;
    std::string modified_content;
    std::vector<Occurrence> occurrences;
};

struct RenameResult {
    bool success{false};
    std::string old_name;
    std::string new_name;
    std::string error;
    std::vector<FileModification> modifications;
    std::size_t total_occurrences{0};
};

[[nodiscard]] inline auto is_valid_identifier(std::string_view name) -> bool {
    if (name.empty()) return false;
    if (!std::isalpha(static_cast<unsigned char>(name[0])) && name[0] != '_') return false;
    for (char c : name) {
        if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_') return false;
    }
    static const std::unordered_set<std::string_view> keywords = {
        "package", "import", "cpp", "cplusplus", "as", "type", "struct", "enum", "interface",
        "map", "fn", "async", "in", "mut", "out", "move", "var",
        "if", "else", "for", "match", "defer", "return", "try", "or", "test",
        "true", "false", "none", "nil", "null"
    };
    return !keywords.contains(name);
}

[[nodiscard]] inline auto generate_unified_diff(const RenameResult& res) -> std::string {
    std::string diff;
    for (const auto& mod : res.modifications) {
        auto orig_lines = lsp::detail::split_lines(mod.original_content);
        auto mod_lines = lsp::detail::split_lines(mod.modified_content);

        diff += std::format("--- a/{}\n", mod.file_path);
        diff += std::format("+++ b/{}\n", mod.file_path);

        std::size_t i = 0;
        std::size_t max_lines = std::max(orig_lines.size(), mod_lines.size());
        while (i < max_lines) {
            if (i < orig_lines.size() && i < mod_lines.size() && orig_lines[i] == mod_lines[i]) {
                ++i;
                continue;
            }
            std::size_t hunk_start = (i >= 2) ? i - 2 : 0;
            std::size_t hunk_end = std::min(max_lines, i + 3);

            std::size_t orig_len = std::min(orig_lines.size(), hunk_end) - std::min(orig_lines.size(), hunk_start);
            std::size_t mod_len = std::min(mod_lines.size(), hunk_end) - std::min(mod_lines.size(), hunk_start);

            diff += std::format("@@ -{},{} +{},{} @@\n", hunk_start + 1, orig_len, hunk_start + 1, mod_len);
            for (std::size_t k = hunk_start; k < hunk_end; ++k) {
                if (k < orig_lines.size() && k < mod_lines.size() && orig_lines[k] == mod_lines[k]) {
                    diff += " " + orig_lines[k] + "\n";
                } else {
                    if (k < orig_lines.size()) {
                        diff += "-" + orig_lines[k] + "\n";
                    }
                    if (k < mod_lines.size()) {
                        diff += "+" + mod_lines[k] + "\n";
                    }
                }
            }
            i = hunk_end;
        }
    }
    return diff;
}

[[nodiscard]] inline auto to_json(const RenameResult& res) -> std::string {
    if (!res.success) {
        return std::format(
            R"({{"success":false,"error":"{}"}})" "\n",
            lsp::detail::json_escape(res.error));
    }
    std::string out = "{\n";
    out += "  \"success\": true,\n";
    out += std::format("  \"old_name\": \"{}\",\n", lsp::detail::json_escape(res.old_name));
    out += std::format("  \"new_name\": \"{}\",\n", lsp::detail::json_escape(res.new_name));
    out += std::format("  \"files_modified\": {},\n", res.modifications.size());
    out += std::format("  \"total_occurrences\": {},\n", res.total_occurrences);
    out += "  \"files\": [\n";
    for (std::size_t i = 0; i < res.modifications.size(); ++i) {
        if (i > 0) out += ",\n";
        const auto& mod = res.modifications[i];
        out += "    {\n";
        out += std::format("      \"file\": \"{}\",\n", lsp::detail::json_escape(mod.file_path));
        out += std::format("      \"occurrences_count\": {},\n", mod.occurrences.size());
        out += "      \"occurrences\": [";
        for (std::size_t j = 0; j < mod.occurrences.size(); ++j) {
            if (j > 0) out += ", ";
            const auto& occ = mod.occurrences[j];
            out += std::format(R"({{"line":{},"col":{},"end_col":{}}})", occ.line, occ.col, occ.end_col);
        }
        out += "]\n    }";
    }
    out += "\n  ]\n}\n";
    return out;
}

[[nodiscard]] inline auto plan_rename(
    const std::filesystem::path& target_file,
    int line_1,
    int col_1,
    std::string_view new_name
) -> RenameResult {
    RenameResult res;
    res.new_name = std::string(new_name);

    if (!is_valid_identifier(new_name)) {
        res.error = std::format("cfire/REFACTOR-INVALID-NAME: '{}' is not a valid non-keyword cfire identifier", new_name);
        return res;
    }

    auto raw_txt = build::read_file_text(target_file);
    if (!raw_txt) {
        res.error = std::format("cfire/FILE-NOT-FOUND: {}", raw_txt.error());
        return res;
    }

    auto lines = lsp::detail::split_lines(*raw_txt);
    int line_0 = line_1 - 1;
    int col_0 = col_1 - 1;

    auto sym = lsp::detail::symbol_at_position(lines, line_0, col_0);
    if (sym.name.empty()) {
        res.error = std::format("cfire/REFACTOR-NO-SYMBOL: no renameable identifier found at {}:{}:{}",
            target_file.string(), line_1, col_1);
        return res;
    }

    res.old_name = sym.name;
    if (res.old_name == res.new_name) {
        res.error = std::format("cfire/REFACTOR-NOOP: target symbol is already named '{}'", new_name);
        return res;
    }

    lsp::Server srv;
    std::filesystem::path abs_p = std::filesystem::absolute(target_file).parent_path();
    while (!abs_p.empty() && abs_p != abs_p.parent_path()) {
        std::error_code ec;
        if (std::filesystem::exists(abs_p / "build.cf", ec) ||
            std::filesystem::exists(abs_p / ".git", ec) ||
            std::filesystem::exists(abs_p / ".cfire", ec) ||
            std::filesystem::exists(abs_p / ".vscode", ec) ||
            std::filesystem::exists(abs_p / ".zed", ec)) {
            srv.set_workspace_root(abs_p.string());
            break;
        }
        abs_p = abs_p.parent_path();
    }
    std::string target_uri = lsp::detail::path_to_uri(target_file.string());

    auto occurrences_lsp = srv.find_symbol_occurrences(target_uri, line_0, col_0, sym.name, sym.qualifier);
    if (occurrences_lsp.empty()) {
        res.error = std::format("cfire/REFACTOR-NO-REFS: no references found for '{}'", sym.name);
        return res;
    }

    // Collision detection:
    // Check if new_name already exists in target AST
    auto ast_res = frontend::parse_source(target_file.string(), *raw_txt);
    if (ast_res) {
        auto ws = srv.resolve_workspace(target_uri, *ast_res);
        auto env = lsp::detail::build_scope_bindings(*ast_res, ws, lines, line_0);
        if (env.contains(res.new_name)) {
            const auto& existing = env.at(res.new_name);
            res.error = std::format("cfire/REFACTOR-COLLISION: symbol '{}' already exists in scope (declared at line {})",
                res.new_name, existing.decl_line);
            return res;
        }
        for (const auto& st : ast_res->structs) {
            if (st.name == res.new_name) {
                res.error = std::format("cfire/REFACTOR-COLLISION: struct '{}' already exists in package at line {}",
                    res.new_name, st.line);
                return res;
            }
        }
        for (const auto& fn : ast_res->functions) {
            if (fn.name == res.new_name) {
                res.error = std::format("cfire/REFACTOR-COLLISION: function '{}' already exists in package at line {}",
                    res.new_name, fn.line);
                return res;
            }
        }
        for (const auto& en : ast_res->enums) {
            if (en.name == res.new_name) {
                res.error = std::format("cfire/REFACTOR-COLLISION: enum '{}' already exists in package at line {}",
                    res.new_name, en.line);
                return res;
            }
        }
    }

    std::unordered_map<std::string, std::vector<Occurrence>> by_file;
    for (const auto& occ : occurrences_lsp) {
        std::string path = lsp::detail::uri_to_path(occ.uri);
        Occurrence o;
        o.file_path = path;
        o.line = occ.line + 1;
        o.col = occ.start_col + 1;
        o.end_col = occ.end_col + 1;
        by_file[path].push_back(o);
        res.total_occurrences++;
    }

    for (auto& [path, occs] : by_file) {
        auto file_raw = build::read_file_text(path);
        if (!file_raw) continue;

        FileModification mod;
        mod.file_path = path;
        mod.original_content = *file_raw;
        mod.occurrences = occs;

        auto file_lines = lsp::detail::split_lines(*file_raw);
        // Sort occurrences descending by line and column so string replacements don't offset subsequent indices
        std::sort(occs.begin(), occs.end(), [](const Occurrence& a, const Occurrence& b) {
            if (a.line != b.line) return a.line > b.line;
            return a.col > b.col;
        });

        for (const auto& occ : occs) {
            int ln_idx = occ.line - 1;
            if (ln_idx >= 0 && static_cast<std::size_t>(ln_idx) < file_lines.size()) {
                std::string& cur_line = file_lines[static_cast<std::size_t>(ln_idx)];
                std::size_t sc = static_cast<std::size_t>(occ.col - 1);
                std::size_t ec = static_cast<std::size_t>(occ.end_col - 1);
                if (sc <= cur_line.size() && ec <= cur_line.size() && ec >= sc) {
                    cur_line.replace(sc, ec - sc, res.new_name);
                }
            }
        }

        std::string modded;
        for (std::size_t li = 0; li < file_lines.size(); ++li) {
            modded += file_lines[li];
            if (li + 1 < file_lines.size() || file_raw->ends_with("\n")) {
                modded += "\n";
            }
        }
        mod.modified_content = std::move(modded);
        res.modifications.push_back(std::move(mod));
    }

    std::sort(res.modifications.begin(), res.modifications.end(), [](const FileModification& a, const FileModification& b) {
        return a.file_path < b.file_path;
    });

    res.success = true;
    return res;
}

[[nodiscard]] inline auto apply_rename(const RenameResult& res) -> std::expected<void, std::string> {
    if (!res.success) return std::unexpected(res.error);
    for (const auto& mod : res.modifications) {
        auto tmp_file = std::format("{}.tmp_{}", mod.file_path, ::getpid());
        {
            std::ofstream out(tmp_file, std::ios::binary | std::ios::trunc);
            if (!out) {
                return std::unexpected(std::format("failed to write temporary file '{}'", tmp_file));
            }
            out << mod.modified_content;
        }
        std::error_code ec;
        std::filesystem::rename(tmp_file, mod.file_path, ec);
        if (ec) {
            return std::unexpected(std::format("failed to atomically replace '{}': {}", mod.file_path, ec.message()));
        }
    }
    return {};
}

} // namespace cfire::refactor

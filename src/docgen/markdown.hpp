#pragma once
// src/docgen/markdown.hpp — Markdown Rendering, Package Doc Extraction & LLM Reference Generator

#include "catalog.hpp"
#include "styles.hpp"
#include "../buildsys.hpp"
#include "../formatter.hpp"
#include <algorithm>
#include <cctype>
#include <expected>
#include <filesystem>
#include <format>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace cfire::doc {
namespace detail {

[[nodiscard]] inline auto trim_md(std::string_view sv) -> std::string {
    while (!sv.empty() && (sv.front() == ' ' || sv.front() == '\t' || sv.front() == '\r')) sv.remove_prefix(1);
    while (!sv.empty() && (sv.back() == ' ' || sv.back() == '\t' || sv.back() == '\r')) sv.remove_suffix(1);
    return std::string(sv);
}

[[nodiscard]] inline auto render_inline_md(std::string_view text) -> std::string {
    std::string out;
    out.reserve(text.size() + 16);
    size_t i = 0;
    while (i < text.size()) {
        if (text[i] == '`') {
            size_t end = text.find('`', i + 1);
            if (end != std::string_view::npos) {
                out += "<code>";
                out += html_escape(text.substr(i + 1, end - i - 1));
                out += "</code>";
                i = end + 1;
                continue;
            }
        }
        if (i + 1 < text.size() && text[i] == '*' && text[i + 1] == '*') {
            size_t end = text.find("**", i + 2);
            if (end != std::string_view::npos) {
                out += "<strong>";
                out += render_inline_md(text.substr(i + 2, end - i - 2));
                out += "</strong>";
                i = end + 2;
                continue;
            }
        }
        if (text[i] == '*') {
            size_t end = text.find('*', i + 1);
            if (end != std::string_view::npos) {
                out += "<em>";
                out += render_inline_md(text.substr(i + 1, end - i - 1));
                out += "</em>";
                i = end + 1;
                continue;
            }
        }
        if (text[i] == '[') {
            size_t cb = text.find(']', i + 1);
            if (cb != std::string_view::npos && cb + 1 < text.size() && text[cb + 1] == '(') {
                size_t cp = text.find(')', cb + 2);
                if (cp != std::string_view::npos) {
                    auto lt = text.substr(i + 1, cb - i - 1);
                    auto lu = text.substr(cb + 2, cp - cb - 2);
                    out += "<a href=\"" + html_escape(lu) + "\">" + render_inline_md(lt) + "</a>";
                    i = cp + 1;
                    continue;
                }
            }
        }
        switch (text[i]) {
            case '&': out += "&amp;"; break;
            case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break;
            case '"': out += "&quot;"; break;
            default: out.push_back(text[i]); break;
        }
        ++i;
    }
    return out;
}

[[nodiscard]] inline auto slugify_heading(std::string_view text) -> std::string {
    std::string slug;
    for (char c : text) {
        if (std::isalnum(static_cast<unsigned char>(c))) {
            slug.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
        } else if (c == ' ' || c == '-' || c == '_') {
            if (!slug.empty() && slug.back() != '-') {
                slug.push_back('-');
            }
        }
    }
    if (!slug.empty() && slug.back() == '-') slug.pop_back();
    return slug;
}

[[nodiscard]] inline auto is_md_table_separator(std::string_view line) -> bool {
    if (line.empty() || line.front() != '|') return false;
    for (char c : line) {
        if (c != '|' && c != '-' && c != ':' && c != ' ' && c != '\t') return false;
    }
    return true;
}

[[nodiscard]] inline auto split_md_table_row(std::string_view line) -> std::vector<std::string> {
    std::vector<std::string> cells;
    if (line.starts_with('|')) line.remove_prefix(1);
    if (line.ends_with('|')) line.remove_suffix(1);
    size_t start = 0;
    while (start < line.size()) {
        size_t next = line.find('|', start);
        if (next == std::string_view::npos) {
            cells.push_back(trim_md(line.substr(start)));
            break;
        }
        cells.push_back(trim_md(line.substr(start, next - start)));
        start = next + 1;
    }
    return cells;
}

[[nodiscard]] inline auto is_md_ordered_list(std::string_view line, size_t& content_pos) -> bool {
    size_t i = 0;
    while (i < line.size() && std::isdigit(static_cast<unsigned char>(line[i]))) ++i;
    if (i > 0 && i < line.size() && line[i] == '.') {
        ++i;
        if (i < line.size() && (line[i] == ' ' || line[i] == '\t')) {
            while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) ++i;
            content_pos = i;
            return true;
        }
    }
    return false;
}

} // namespace detail

[[nodiscard]] inline auto render_guide_markdown(std::string_view md_text) -> std::string {
    std::vector<std::string> lines;
    {
        std::stringstream ss{std::string(md_text)};
        std::string line;
        while (std::getline(ss, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            lines.push_back(line);
        }
    }

    // Dynamic sidebar navigation extracted from headings
    std::vector<std::pair<std::string, std::string>> chapters;
    for (const auto& l : lines) {
        if (l.starts_with("## Chapter ")) {
            auto rest = l.substr(11);
            auto colon = rest.find(':');
            if (colon != std::string::npos) {
                auto num = detail::trim_md(rest.substr(0, colon));
                auto title = detail::trim_md(rest.substr(colon + 1));
                chapters.push_back({"#chapter-" + num, num + ". " + title});
            } else {
                chapters.push_back({"#" + detail::slugify_heading(rest), std::string(rest)});
            }
        } else if (l.starts_with("## ") && !l.starts_with("## Chapter")) {
            auto title = detail::trim_md(l.substr(3));
            chapters.push_back({"#" + detail::slugify_heading(title), title});
        }
    }

    std::string main_body;
    size_t idx = 0;
    bool had_subtitle = false;

    while (idx < lines.size()) {
        std::string_view line = lines[idx];
        std::string trimmed = detail::trim_md(line);

        if (trimmed.empty()) {
            ++idx;
            continue;
        }

        // Fenced code blocks
        if (trimmed.starts_with("```")) {
            std::string lang = detail::trim_md(trimmed.substr(3));
            ++idx;
            std::string code;
            while (idx < lines.size() && !detail::trim_md(lines[idx]).starts_with("```")) {
                code += lines[idx];
                code += '\n';
                ++idx;
            }
            if (idx < lines.size()) ++idx;

            std::string tag_cls = "tag-cf";
            std::string tag_name = lang;
            if (lang == "cf") {
                tag_cls = "tag-cf";
                tag_name = "cfire";
            } else if (lang == "cpp" || lang == "c++") {
                tag_cls = "tag-cpp";
                tag_name = "C++23";
            } else if (lang == "bash" || lang == "sh") {
                tag_cls = "tag-cf";
                tag_name = "terminal";
            } else if (lang.empty()) {
                tag_name = "text";
            }

            main_body += "<div class=\"code-header\"><span class=\"" + tag_cls + "\">" + detail::html_escape(tag_name) + "</span></div>\n";
            main_body += "<pre><code>" + detail::html_escape(code) + "</code></pre>\n";
            continue;
        }

        // Headings
        if (trimmed.starts_with("# ")) {
            main_body += "<h1>" + detail::render_inline_md(detail::trim_md(trimmed.substr(2))) + "</h1>\n";
            ++idx;
            continue;
        }
        if (trimmed.starts_with("## ")) {
            auto rest = detail::trim_md(trimmed.substr(3));
            if (rest.starts_with("Chapter ")) {
                auto chap_rest = rest.substr(8);
                auto colon = chap_rest.find(':');
                if (colon != std::string_view::npos) {
                    auto num = detail::trim_md(chap_rest.substr(0, colon));
                    auto title = detail::trim_md(chap_rest.substr(colon + 1));
                    main_body += "<h2 id=\"chapter-" + num + "\"><a id=\"ch" + num + "\"></a>" + num + ". " + detail::render_inline_md(title) + "</h2>\n";
                    ++idx;
                    continue;
                }
            }
            std::string sid = detail::slugify_heading(rest);
            main_body += "<h2 id=\"" + sid + "\">" + detail::render_inline_md(rest) + "</h2>\n";
            ++idx;
            continue;
        }
        if (trimmed.starts_with("### ")) {
            auto rest = detail::trim_md(trimmed.substr(4));
            std::string sid = detail::slugify_heading(rest);
            main_body += "<h3 id=\"" + sid + "\">" + detail::render_inline_md(rest) + "</h3>\n";
            ++idx;
            continue;
        }
        if (trimmed.starts_with("#### ")) {
            auto rest = detail::trim_md(trimmed.substr(5));
            std::string sid = detail::slugify_heading(rest);
            main_body += "<h4 id=\"" + sid + "\">" + detail::render_inline_md(rest) + "</h4>\n";
            ++idx;
            continue;
        }

        // Horizontal rules
        if (trimmed == "---" || trimmed == "***" || trimmed == "___") {
            main_body += "<hr style=\"margin: 2.5rem 0; border: none; border-top: 1px solid var(--border);\" />\n";
            ++idx;
            continue;
        }

        // Blockquotes
        if (trimmed.starts_with('>')) {
            std::string bq_text;
            while (idx < lines.size() && detail::trim_md(lines[idx]).starts_with('>')) {
                std::string bl = detail::trim_md(lines[idx]);
                bl = detail::trim_md(bl.substr(1));
                if (!bq_text.empty()) bq_text += " ";
                bq_text += bl;
                ++idx;
            }
            if (!had_subtitle) {
                main_body += "<p class=\"subtitle\" style=\"margin: 0 0 2rem;\">" + detail::render_inline_md(bq_text) + "</p>\n";
                had_subtitle = true;
            } else {
                main_body += "<blockquote><p>" + detail::render_inline_md(bq_text) + "</p></blockquote>\n";
            }
            continue;
        }

        // Tables
        if (trimmed.starts_with('|') && trimmed.ends_with('|')) {
            std::vector<std::string> headers = detail::split_md_table_row(trimmed);
            ++idx;
            if (idx < lines.size() && detail::is_md_table_separator(detail::trim_md(lines[idx]))) {
                ++idx;
            }
            std::vector<std::vector<std::string>> body_rows;
            while (idx < lines.size() && detail::trim_md(lines[idx]).starts_with('|') && detail::trim_md(lines[idx]).ends_with('|')) {
                body_rows.push_back(detail::split_md_table_row(detail::trim_md(lines[idx])));
                ++idx;
            }

            main_body += "<div class=\"table-wrap\">\n  <div class=\"table-scroll-cue\">Scroll table &rarr;</div>\n  <table>\n";
            if (!headers.empty()) {
                main_body += "    <thead>\n      <tr>";
                for (const auto& h : headers) {
                    main_body += "<th>" + detail::render_inline_md(h) + "</th>";
                }
                main_body += "</tr>\n    </thead>\n";
            }
            if (!body_rows.empty()) {
                main_body += "    <tbody>\n";
                for (const auto& row : body_rows) {
                    main_body += "      <tr>";
                    for (const auto& cell : row) {
                        main_body += "<td>" + detail::render_inline_md(cell) + "</td>";
                    }
                    main_body += "</tr>\n";
                }
                main_body += "    </tbody>\n";
            }
            main_body += "  </table>\n</div>\n";
            continue;
        }

        // Unordered lists
        if (trimmed.starts_with("- ") || trimmed.starts_with("* ")) {
            main_body += "<ul>\n";
            while (idx < lines.size()) {
                std::string tl = detail::trim_md(lines[idx]);
                if (tl.starts_with("- ") || tl.starts_with("* ")) {
                    main_body += "  <li>" + detail::render_inline_md(detail::trim_md(tl.substr(2))) + "</li>\n";
                    ++idx;
                } else if (!tl.empty() && (lines[idx].starts_with("  ") || lines[idx].starts_with('\t'))) {
                    main_body += " " + detail::render_inline_md(tl);
                    ++idx;
                } else {
                    break;
                }
            }
            main_body += "</ul>\n";
            continue;
        }

        // Ordered lists
        size_t cpos = 0;
        if (detail::is_md_ordered_list(trimmed, cpos)) {
            main_body += "<ol>\n";
            while (idx < lines.size()) {
                std::string tl = detail::trim_md(lines[idx]);
                size_t p = 0;
                if (detail::is_md_ordered_list(tl, p)) {
                    main_body += "  <li>" + detail::render_inline_md(detail::trim_md(tl.substr(p))) + "</li>\n";
                    ++idx;
                } else if (!tl.empty() && (lines[idx].starts_with("  ") || lines[idx].starts_with('\t'))) {
                    main_body += " " + detail::render_inline_md(tl);
                    ++idx;
                } else {
                    break;
                }
            }
            main_body += "</ol>\n";
            continue;
        }

        // Paragraphs
        std::string p_text;
        while (idx < lines.size()) {
            std::string tl = detail::trim_md(lines[idx]);
            if (tl.empty() || tl.starts_with('#') || tl.starts_with("```") || tl == "---" ||
                tl.starts_with('>') || (tl.starts_with('|') && tl.ends_with('|')) ||
                tl.starts_with("- ") || tl.starts_with("* ") || detail::is_md_ordered_list(tl, cpos)) {
                break;
            }
            if (!p_text.empty()) p_text += " ";
            p_text += tl;
            ++idx;
        }
        if (!p_text.empty()) {
            main_body += "<p>" + detail::render_inline_md(p_text) + "</p>\n";
        }
    }

    std::string out;
    out += "<!DOCTYPE html>\n<html lang=\"en\">\n<head>\n";
    out += "  <meta charset=\"UTF-8\" />\n";
    out += "  <meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\" />\n";
    out += "  <title>Language Guide — cfire (pronounced sapphire)</title>\n";
    out += "  <link rel=\"icon\" type=\"image/png\" href=\"assets/cfire_logo_emblem.png\" />\n";
    out += "  <style>" + detail::shared_css() + "</style>\n";
    out += "</head>\n<body>\n";
    out += detail::navbar_html("guide");
    out += "<div class=\"container\">\n  <div class=\"doc-layout\">\n";
    out += "    <aside class=\"sidebar\">\n";
    out += "      <details class=\"sidebar-details\" open>\n";
    out += "        <summary class=\"sidebar-summary\">\n";
    out += "          <span>Chapters &amp; Navigation</span>\n";
    out += "          <span class=\"sidebar-chevron\">&#9662;</span>\n";
    out += "        </summary>\n";
    out += "        <div class=\"sidebar-content\">\n";
    out += "          <h4>Guide Chapters</h4>\n";
    out += "          <ul>\n";
    for (const auto& ch : chapters) {
        out += "            <li><a href=\"" + ch.first + "\">" + detail::html_escape(ch.second) + "</a></li>\n";
    }
    out += "          </ul>\n";
    out += "          <h4 style=\"margin-top: 1.25rem;\">Quick Links</h4>\n";
    out += "          <ul>\n";
    out += "            <li><a href=\"reference.html\">Stdlib Catalog (28 Modules) &rarr;</a></li>\n";
    out += "            <li><a href=\"index.html#landscape\">Systems Landscape &rarr;</a></li>\n";
    out += "            <li><a href=\"index.html#benchmarks\">Compiler Benchmarks &rarr;</a></li>\n";
    out += "          </ul>\n";
    out += "        </div>\n";
    out += "      </details>\n";
    out += "    </aside>\n";
    out += "    <main>\n";
    out += main_body;
    out += "    </main>\n  </div>\n</div>\n";
    out += "<footer class=\"footer\">\n";
    out += "  Generated by <code>cfire doc</code> &bull; <a href=\"reference.html\">Explore the `core:*` Stdlib &amp; Diagnostics Reference &rarr;</a>\n";
    out += "</footer>\n</body>\n</html>\n";
    return out;
}

[[nodiscard]] inline auto generate_guide_markdown(const std::filesystem::path& docs_dir = "docs") -> std::string {
    std::filesystem::path guide_path = docs_dir / "GUIDE.md";
    if (std::filesystem::exists(guide_path)) {
        std::ifstream ifs(guide_path);
        std::stringstream ss;
        ss << ifs.rdbuf();
        return ss.str();
    }
    return "# The `cfire` Language & Systems Programming Guide\n";
}

[[nodiscard]] inline auto generate_reference_markdown() -> std::string {
    std::string out = "# `cfire` Standard Library (`core:*`) & Diagnostics Reference\n\n";
    out += "> Auto-generated from the compiler's single-source-of-truth catalog via `./cfire doc`.\n\n";
    out += "## 1. Built-In `core:*` Standard Library Modules\n\n";
    for (const auto& mod : stdlib_catalog()) {
        out += std::format("### `{}` (`{}`) — {}\n\n", mod.import_path, mod.ns_alias, mod.title);
        out += std::format("- **Auto-Link Flags:** `{}`\n", mod.link_flags);
        out += std::format("- **Overview:** {}\n\n", mod.overview);
        auto ex = detail::module_practical_example(mod.import_path);
        if (!ex.empty()) {
            out += "**Practical Example:**\n```cf\n" + std::string(ex) + "\n```\n\n";
        }
        out += "| `.cf` Signature | Lowered C++23 | Description | Example |\n";
        out += "|---|---|---|---|\n";
        for (const auto& fn : mod.functions) {
            out += std::format("| `{}` | `{}` | {} | `{}` |\n",
                fn.cf_sig, fn.cpp_lowering, fn.summary, fn.example);
        }
        out += "\n";
    }

    out += "## 2. Semantic Diagnostic Codes\n\n";
    for (const auto& d : diagnostics_catalog()) {
        out += std::format("### `[{}]` — {}\n\n", d.code, d.title);
        out += d.rule + "\n\n";
        out += "**Rejected (`.cf`):**\n```cf\n" + d.bad_example + "\n```\n\n";
        out += "**Accepted (`.cf`):**\n```cf\n" + d.good_example + "\n```\n\n";
    }
    return out;
}

// Extracts Markdown API documentation for a specific `.cf` file (including doc comments and lowered C++23 signatures)
[[nodiscard]] inline auto extract_file_markdown_doc(const std::filesystem::path& cf_file)
    -> std::expected<std::string, std::string>
{
    if (!std::filesystem::exists(cf_file)) {
        return std::unexpected(std::format("file not found: {}", cf_file.string()));
    }
    std::ifstream ifs(cf_file);
    std::stringstream ss;
    ss << ifs.rdbuf();
    std::string src = ss.str();

    std::vector<std::string> lines;
    {
        std::istringstream ls(src);
        std::string l;
        while (std::getline(ls, l)) {
            if (!l.empty() && l.back() == '\r') l.pop_back();
            lines.push_back(l);
        }
    }

    auto comment_above = [&](int one_based_line) -> std::string {
        int idx = one_based_line - 2;
        std::vector<std::string> collected;
        while (idx >= 0) {
            std::string t = cfire::fmt::detail::trim_ws(lines[static_cast<std::size_t>(idx)]);
            if (t.starts_with("//")) {
                std::string clean = cfire::fmt::detail::trim_ws(std::string_view(t).substr(t.starts_with("///") ? 3 : 2));
                collected.push_back(clean);
                --idx;
            } else {
                break;
            }
        }
        std::reverse(collected.begin(), collected.end());
        std::string out;
        for (const auto& c : collected) {
            if (!out.empty()) out += " ";
            out += c;
        }
        return out;
    };

    auto ast_res = cfire::frontend::parse_source(cf_file.string(), src);
    if (!ast_res) {
        return std::unexpected(ast_res.error());
    }
    const auto& ast = *ast_res;

    std::string md = std::format("# Package `{}` (`{}`)\n\n", ast.package_name, cf_file.filename().string());
    if (!ast.imports.empty()) {
        md += "## Imports\n";
        for (const auto& imp : ast.imports) {
            md += std::format("- `\"{}\"`\n", imp.path);
        }
        md += "\n";
    }
    if (!ast.structs.empty()) {
        md += "## Structs\n\n";
        for (const auto& st : ast.structs) {
            md += std::format("### `type {} struct`\n", st.name);
            if (auto doc = comment_above(st.line); !doc.empty()) md += doc + "\n\n";
            md += "```cf\n" + cfire::fmt::format_struct_signature(st) + "\n```\n\n";
        }
    }
    if (!ast.enums.empty()) {
        md += "## Algebraic Enums\n\n";
        for (const auto& en : ast.enums) {
            md += std::format("### `type {} enum`\n", en.name);
            if (auto doc = comment_above(en.line); !doc.empty()) md += doc + "\n\n";
            md += "```cf\n" + cfire::fmt::format_enum_signature(en) + "\n```\n\n";
        }
    }
    if (!ast.functions.empty()) {
        md += "## Functions\n\n";
        for (const auto& fn : ast.functions) {
            md += std::format("### `{}`\n", cfire::fmt::format_fn_signature(fn));
            if (auto doc = comment_above(fn.line); !doc.empty()) md += doc + "\n\n";
        }
    }
    if (!ast.tests.empty()) {
        md += "## Built-in Tests\n\n";
        for (const auto& t : ast.tests) {
            md += std::format("- `test \"{}\"` (line {})\n", t.name, t.line);
        }
        md += "\n";
    }
    return md;
}

// Generates the compressed LLM & AI Agent language reference sheet (`cfire doc --llm`).
[[nodiscard]] inline auto generate_llm_reference() -> std::string_view {
    return R"(# cfire Language Reference Sheet for LLMs & AI Agents
Grammar:
- Package: `package <name>` (canonical: main)
- Imports: `import "core:<mod>"`, `import "./path"`, `import cpp "header.h" @define("MACRO")`
- Declarations:
  - Const: `x := 42`, `name: str = "cfire"`
  - Mutable: `mut y := 10`, `mut count: i64 = 0`
- Signatures: `fn name(in a: i32, mut b: str, move c: []byte) -> Ret! { ... }`
  - Fallible return: `-> T!` lowers to `std::expected<T, cfire::Error>`
  - Optional return: `-> T?` lowers to `std::optional<T>`
  - Tuples: `fn coords() -> (f64, f64) { return (1.0, 2.0) }`, deconstruct: `(x, y) := coords()`
- Types:
  - Struct: `type User struct { id: i64, name: str `json:"name"` }`
  - Enum: `type Shape enum { Circle(f64), Rect(f64, f64) }`
  - Interface: `type Writer interface { write(b: []byte) -> usize! }`
- Control Flow:
  - `if cond { ... } else { ... }`
  - `for item in slice { ... }` / `for i, item in slice { ... }`
  - `match val { Shape::Circle(r) => 3.14 * r * r, Shape::Rect(w, h) => w * h }`
  - `defer resource.close()`
- Error Handling:
  - `val := try fallible()` (early return on error)
  - `val := fallible() or fallback_expr`
  - `val := fallible() or err { return -1 }` (must diverge or panic)
- Foreign C/C++: `cpp { /* verbatim C++23 */ }`
- Standard Library Modules:
  - `core:fs`: open, File (read, read_all, write, seek, tell, flush, close), dirname, basename, extension
  - `core:json`: parse_dynamic, stringify_dynamic, Value, decode[T], stringify[T]
  - `core:net/http`: get, post, make_server; `core:net/url`: parse, encode, decode
  - `core:unicode`: rune_count, runes, is_valid_utf8, to_lower, to_upper, encode_rune
  - `core:os`: command, exec, getenv; `core:os/exec`: exec.command
  - `core:time`: now, Duration, format_rfc3339, parse_rfc3339
  - `core:regex`: compile, is_match, replace_all
  - `core:collections`: Set[T], PriorityQueue[T], Deque[T]
  - `core:sort`: slice, slice_by, binary_search
  - `core:crypto/sha256`: hash_hex, hash_bytes; `core:crypto/rand`: bytes
  - `core:log`: set_level, debug, info, warn, error)";
}

} // namespace cfire::doc

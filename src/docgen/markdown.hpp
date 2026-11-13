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

[[nodiscard]] inline auto clean_nav_title(std::string_view text) -> std::string {
    std::string cleaned;
    cleaned.reserve(text.size());
    size_t i = 0;
    while (i < text.size()) {
        if (text[i] == '`') {
            ++i;
            continue;
        }
        if (text.substr(i).starts_with("$\\leftrightarrow$")) {
            cleaned += "\xE2\x86\x94";
            i += 17;
            continue;
        }
        cleaned.push_back(text[i]);
        ++i;
    }
    return html_escape(cleaned);
}

[[nodiscard]] inline auto short_part_title(std::string_view raw) -> std::string {
    if (raw.find("Part I:") != std::string_view::npos) return "Part I &mdash; Tooling &amp; Setup";
    if (raw.find("Part II:") != std::string_view::npos) return "Part II &mdash; Core Language";
    if (raw.find("Part III:") != std::string_view::npos) return "Part III &mdash; Core Stdlib";
    if (raw.find("Part IV:") != std::string_view::npos) return "Part IV &mdash; Net, UI &amp; AI";
    return clean_nav_title(raw);
}

[[nodiscard]] inline auto short_chapter_title(int num, std::string_view raw) -> std::string {
    switch (num) {
        case 1: return "Install &amp; Setup";
        case 2: return "Quickstart &amp; Tour";
        case 3: return "Workspaces &amp; CLI";
        case 4: return "Types &amp; Control";
        case 5: return "Functions &amp; Loans";
        case 6: return "Structs &amp; Enums";
        case 7: return "Errors &amp; Defer";
        case 8: return "Strings &amp; Containers";
        case 9: return "FS, OS &amp; CLI";
        case 10: return "Concurrency &amp; Channels";
        case 11: return "C/C++ Interop";
        case 12: return "Testing &amp; Rules";
        case 13: return "HTTP, WS &amp; TLS";
        case 14: return "JSON, SQLite &amp; Crypto";
        case 15: return "Graphics &amp; iOS UI";
        case 16: return "Audio &amp; DSP";
        case 17: return "AI, RAG &amp; MCP";
        case 18: return "Hybrid WebUI Apps";
        case 19: return "Production &amp; Examples";
        default: return clean_nav_title(raw);
    }
}

[[nodiscard]] inline auto short_topic_title(std::string_view raw) -> std::string {
    static const std::pair<std::string_view, std::string_view> kMap[] = {
        {"How the `cfire` Toolchain Works", "Toolchain"},
        {"Quick Installation Script", "Quick Install"},
        {"Checking Toolchain Health", "Doctor & Setup"},
        {"Building from Source", "Source Build"},
        {"Understanding the Three Stdlib Namespaces", "Stdlib Tiers"},
        {"Setting Up WebAssembly & iOS", "WASM & iOS"},
        {"Connecting Your Editor (LSP)", "LSP & MCP"},
        {"Trying Ideas Instantly", "Live Eval"},
        {"Writing Your First `.cf` Program", "Hello World"},
        {"Checking, Building, Inspecting C++23", "Build & Eject"},
        {"Scaffolding New Projects", "Project Init"},
        {"Multi-Package Workspace Layout", "Workspaces"},
        {"Declarative Build Files", "build.cf Spec"},
        {"Complete CLI Command Reference", "CLI Commands"},
        {"Bindings, Scalar Types", "Scalars & Types"},
        {"Working with Slices", "Slices & Maps"},
        {"Unified `for` Loops", "Loops & Ranges"},
        {"Pointers (`*T`, `*mut T`)", "Pointers & RAII"},
        {"Seeing Every Mutation & Move", "Call-Site Intent"},
        {"Closures, Trailing Blocks", "Closures & Generics"},
        {"Loan Tracking:", "Loan Tracking"},
        {"Structs, Default Field Values", "Structs & Methods"},
        {"Compile-Time Struct Tags", "Struct Tags"},
        {"Algebraic Enums, Slice Patterns", "Enums & Match"},
        {"Structural Interfaces", "Interfaces"},
        {"Separating Errors from Absence", "T! vs T? Types"},
        {"Propagating Errors with `try`", "try & or Recovery"},
        {"Flow-Sensitive Null Pointer Safety", "Null Safety"},
        {"Deterministic Scope Cleanup", "defer Cleanup"},
        {"UTF-8 Strings, Unicode Scalars", "UTF-8 & Regex"},
        {"Zero-Copy Buffer Pointers", "Buffer Pointers"},
        {"Generic Sets, Priority Queues", "Sets, Heaps & Sort"},
        {"Streaming File I/O, Clocks", "FS, Time & Rand"},
        {"Safe Subprocess Execution", "Safe Subprocesses"},
        {"Compile-Time CLI Flag Parsing", "CLI & Logging"},
        {"Why Unstructured Threads Fail", "async.scope Tasks"},
        {"Communicating Across Workers", "MPMC Channels"},
        {"Hierarchical Cancellation", "Context & Deadlines"},
        {"Zero-Wrapper Interop:", "Inline C++ Blocks"},
        {"Importing C Headers", "C Headers & Flags"},
        {"Orchestrating C, CMake", "Native Libraries"},
        {"Unit Tests, Doc-Tests", "Unit & Doc Tests"},
        {"Property-Based Fuzzing", "Fuzzing & Bench"},
        {"Source-Mapped Sanitizers", "Sanitizers & Fix"},
        {"Complete 18-Rule", "18 Safety Rules"},
        {"One Networking Stack", "Cross-Platform Net"},
        {"Building an HTTP Router", "HTTP & SSE"},
        {"Upgrading to WebSockets", "WebSockets & TLS"},
        {"Compile-Time JSON Serialization", "JSON & Schemas"},
        {"Embedded SQLite WAL", "SQLite & Vectors"},
        {"Zstandard Compression", "Zstd, AES & Hash"},
        {"Choosing a UI Stack", "UI Options"},
        {"Lock-Free 64-Voice Mixer", "64-Voice Mixer"},
        {"Streaming Real-Time DSP", "Real-Time DSP"},
        {"Unified Cloud & On-Device Inference", "Cloud & Local AI"},
        {"Schema-Typed Tool Calling", "Structured Tools"},
        {"Offline Document Ingestion", "Document RAG"},
        {"Building Model Context Protocol", "MCP Servers"},
        {"Why `ext:webui`?", "WebUI Architecture"},
        {"Typed RPC Bindings", "Typed RPC Bindings"},
        {"Bidirectional Event Bus", "Events & State"},
        {"Frameless Window Chrome", "Window Chrome"},
        {"Headless & Live Offscreen", "Offscreen Testing"},
        {"Blueprint: RAG + HTTP/JSON", "RAG Microservice"},
        {"Cloud & Local Service Primitives", "Cloud Primitives"},
        {"Canonical Projects Index", "Examples Index"},
        {"Further Reference & Module Index", "Module Index"},
    };
    for (const auto& [prefix, short_lbl] : kMap) {
        if (raw.starts_with(prefix)) {
            return html_escape(short_lbl);
        }
    }
    std::string cleaned = clean_nav_title(raw);
    auto paren = cleaned.find(" (");
    if (paren != std::string::npos) cleaned = cleaned.substr(0, paren);
    if (cleaned.size() > 24) cleaned = cleaned.substr(0, 22) + "..";
    return cleaned;
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
    std::string current;
    bool in_code = false;
    for (size_t i = 0; i < line.size(); ++i) {
        if (line[i] == '\\' && i + 1 < line.size() && line[i + 1] == '|') {
            current.push_back('|');
            ++i;
            continue;
        }
        if (line[i] == '`') {
            in_code = !in_code;
            current.push_back('`');
            continue;
        }
        if (line[i] == '|' && !in_code) {
            cells.push_back(trim_md(current));
            current.clear();
            continue;
        }
        current.push_back(line[i]);
    }
    cells.push_back(trim_md(current));
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

    struct SubHead {
        std::string id;
        std::string title;
        std::string html;
    };
    struct ChapterSection {
        int num = 0;
        std::string num_str;
        std::string id;
        std::string title;
        std::string part;
        std::string html;
        std::vector<SubHead> subheads;
    };

    std::vector<ChapterSection> chapters;
    std::string intro_html;
    std::string pending_part_banner;
    std::string current_part = "Part I: Getting Started & Tooling";
    bool next_cpp_is_lowering = false;
    std::string pending_lowering_title;
    bool next_text_is_output = false;

    auto append_content = [&](std::string_view chunk) {
        if (chapters.empty()) {
            intro_html += chunk;
        } else if (!chapters.back().subheads.empty()) {
            chapters.back().subheads.back().html += chunk;
        } else {
            chapters.back().html += chunk;
        }
    };

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
            if (lang == "cf" || lang == "cfire") {
                tag_cls = "tag-cf";
                tag_name = "cfire (.cf)";
            } else if (lang == "cpp" || lang == "c++") {
                tag_cls = "tag-cpp";
                tag_name = "ISO C++23";
            } else if (lang == "bash" || lang == "sh") {
                tag_cls = "tag-cf";
                tag_name = "terminal";
            } else if (lang.empty() || lang == "text") {
                tag_name = "text";
            }

            if ((lang == "text" || lang.empty()) && (next_text_is_output || code.starts_with("# Output:"))) {
                next_text_is_output = false;
                std::string clean_out = code;
                if (clean_out.starts_with("# Output:\n")) {
                    clean_out = clean_out.substr(10);
                }
                std::string tray = "<div class=\"terminal-tray\">\n"
                                   "  <div class=\"terminal-tray-header\" style=\"display:inline-flex;align-items:center;gap:0.35rem;\"><span class=\"material-symbols-outlined\" style=\"font-size:0.95rem;\" aria-hidden=\"true\">check_circle</span> Expected Terminal Output</div>\n"
                                   "  <pre><code>" + detail::html_escape(clean_out) + "</code></pre>\n"
                                   "</div>\n";
                append_content(tray);
                continue;
            }

            if ((lang == "cpp" || lang == "c++") && next_cpp_is_lowering) {
                next_cpp_is_lowering = false;
                std::string drawer = "<details class=\"lowering-drawer\" open>\n"
                                     "  <summary><span style=\"display:inline-flex;align-items:center;gap:0.35rem;\"><span class=\"material-symbols-outlined\" style=\"font-size:1rem;\" aria-hidden=\"true\">code_blocks</span> " + detail::render_inline_md(pending_lowering_title) + "</span><span>ISO C++23</span></summary>\n"
                                     "  <div class=\"lowering-body\">\n"
                                     "    <div class=\"code-header\"><span class=\"tag-cpp\">Emitted ISO C++23</span><button class=\"code-copy-btn\" onclick=\"copyCode(this)\">Copy</button></div>\n"
                                     "    <pre><code>" + detail::html_escape(code) + "</code></pre>\n"
                                     "  </div>\n"
                                     "</details>\n";
                append_content(drawer);
                continue;
            }

            std::string block = "<div class=\"code-header\"><span class=\"" + tag_cls + "\">" + detail::html_escape(tag_name) + "</span><button class=\"code-copy-btn\" onclick=\"copyCode(this)\">Copy</button></div>\n";
            block += "<pre><code>" + detail::html_escape(code) + "</code></pre>\n";
            append_content(block);
            continue;
        }

        // Headings
        if (trimmed.starts_with("# Part ")) {
            current_part = detail::trim_md(trimmed.substr(2));
            pending_part_banner = "<div class=\"part-banner\"><span class=\"part-pill\">SECTION</span><h2>" + detail::render_inline_md(current_part) + "</h2></div>\n";
            ++idx;
            continue;
        }
        if (trimmed.starts_with("# ")) {
            auto h1_tag = "<h1>" + detail::render_inline_md(detail::trim_md(trimmed.substr(2))) + "</h1>\n";
            append_content(h1_tag);
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
                    int num_val = 0;
                    try { num_val = std::stoi(num); } catch (...) {}

                    ChapterSection sec;
                    sec.num = num_val;
                    sec.num_str = num;
                    sec.id = "chapter-" + num;
                    sec.title = std::string(title);
                    sec.part = current_part;

                    if (!pending_part_banner.empty()) {
                        sec.html += pending_part_banner;
                        pending_part_banner.clear();
                    }
                    sec.html += "<h2 id=\"chapter-" + num + "\"><a id=\"ch" + num + "\"></a>" + num + ". " + detail::render_inline_md(title) + "</h2>\n";
                    chapters.push_back(std::move(sec));
                    ++idx;
                    continue;
                }
            }
            std::string sid = detail::slugify_heading(rest);
            auto h2_tag = "<h2 id=\"" + sid + "\">" + detail::render_inline_md(rest) + "</h2>\n";
            append_content(h2_tag);
            ++idx;
            continue;
        }
        if (trimmed.starts_with("### ")) {
            auto rest = detail::trim_md(trimmed.substr(4));
            std::string badge;
            size_t bracket = rest.rfind('[');
            if (bracket != std::string_view::npos && rest.ends_with(']')) {
                badge = detail::trim_md(rest.substr(bracket + 1, rest.size() - bracket - 2));
                rest = detail::trim_md(rest.substr(0, bracket));
            } else {
                if (rest.find("Quick Installation Script") != std::string_view::npos) {
                    badge = "Recommended for POSIX";
                } else if (rest.find("Overview & System Requirements") != std::string_view::npos) {
                    badge = "1-to-1 C++23 LOWERING";
                } else if (rest.find("WebAssembly") != std::string_view::npos) {
                    badge = "wasm32-emscripten";
                } else if (rest.find("iOS Native") != std::string_view::npos) {
                    badge = "arm64-apple-ios";
                }
            }
            std::string base_sid = detail::slugify_heading(rest);
            std::string sid = chapters.empty() ? base_sid : ("ch" + chapters.back().num_str + "-" + base_sid);
            std::string badge_html = badge.empty() ? "" : "<span class=\"heading-badge\">" + detail::html_escape(badge) + "</span>";
            std::string heading_html = "<h3 class=\"guide-heading\" id=\"" + sid + "\"><span class=\"heading-hash\">#</span><span class=\"heading-text\">" + detail::render_inline_md(rest) + "</span>" + badge_html + "</h3>\n";
            if (!chapters.empty()) {
                chapters.back().subheads.push_back({.id = "#" + sid, .title = std::string(rest), .html = heading_html});
            } else {
                intro_html += heading_html;
            }
            ++idx;
            continue;
        }
        if (trimmed.starts_with("#### ")) {
            auto rest = detail::trim_md(trimmed.substr(5));
            if (rest.starts_with("Under the Hood")) {
                next_cpp_is_lowering = true;
                pending_lowering_title = rest;
                ++idx;
                continue;
            }
            if (rest == "Expected Output") {
                next_text_is_output = true;
                ++idx;
                continue;
            }
            std::string base_sid = detail::slugify_heading(rest);
            std::string sid = chapters.empty() ? base_sid : ("ch" + chapters.back().num_str + "-" + base_sid);
            auto h4_tag = "<h4 id=\"" + sid + "\">" + detail::render_inline_md(rest) + "</h4>\n";
            append_content(h4_tag);
            ++idx;
            continue;
        }
        if (trimmed.starts_with('#')) {
            size_t hashes = 0;
            while (hashes < trimmed.size() && trimmed[hashes] == '#') ++hashes;
            auto rest = detail::trim_md(trimmed.substr(hashes));
            if (rest == "Expected Output") {
                next_text_is_output = true;
                ++idx;
                continue;
            }
            std::string base_sid = detail::slugify_heading(rest);
            std::string sid = chapters.empty() ? base_sid : ("ch" + chapters.back().num_str + "-" + base_sid);
            auto h_tag = "<h4 id=\"" + sid + "\" class=\"guide-subhead5\">" + detail::render_inline_md(rest) + "</h4>\n";
            append_content(h_tag);
            ++idx;
            continue;
        }

        // Horizontal rules
        if (trimmed == "---" || trimmed == "***" || trimmed == "___") {
            if (chapters.empty() || chapters.back().subheads.empty()) {
                append_content("<hr style=\"margin: 2.5rem 0; border: none; border-top: 1px solid var(--border);\" />\n");
            }
            ++idx;
            continue;
        }

        // Blockquotes & Callouts
        if (trimmed.starts_with('>')) {
            std::string bq_text;
            while (idx < lines.size() && detail::trim_md(lines[idx]).starts_with('>')) {
                std::string bl = detail::trim_md(lines[idx]);
                bl = detail::trim_md(bl.substr(1));
                if (!bq_text.empty()) bq_text += " ";
                bq_text += bl;
                ++idx;
            }
            if (bq_text.starts_with("[!NOTE]") || bq_text.starts_with("[!TIP]") ||
                bq_text.starts_with("[!IMPORTANT]") || bq_text.starts_with("[!WARNING]") ||
                bq_text.starts_with("[!CAUTION]")) {
                std::string kind = "note";
                std::string title = "Note";
                size_t prefix_len = 7;
                if (bq_text.starts_with("[!TIP]")) { kind = "tip"; title = "Tip"; prefix_len = 6; }
                else if (bq_text.starts_with("[!IMPORTANT]")) { kind = "important"; title = "Important"; prefix_len = 12; }
                else if (bq_text.starts_with("[!WARNING]")) { kind = "warning"; title = "Warning"; prefix_len = 10; }
                else if (bq_text.starts_with("[!CAUTION]")) { kind = "caution"; title = "Caution"; prefix_len = 10; }
                auto body = detail::trim_md(bq_text.substr(prefix_len));
                std::string callout = "<div class=\"callout callout-" + kind + "\">\n"
                                     "  <div class=\"callout-title\">" + title + "</div>\n"
                                     "  <p>" + detail::render_inline_md(body) + "</p>\n"
                                     "</div>\n";
                append_content(callout);
            } else if (!had_subtitle) {
                append_content("<p class=\"subtitle\" style=\"margin: 0 0 2rem;\">" + detail::render_inline_md(bq_text) + "</p>\n");
                had_subtitle = true;
            } else {
                append_content("<blockquote><p>" + detail::render_inline_md(bq_text) + "</p></blockquote>\n");
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

            std::string tbl = "<div class=\"table-wrap\">\n  <div class=\"table-scroll-cue\">Scroll table &rarr;</div>\n  <table>\n";
            if (!headers.empty()) {
                tbl += "    <thead>\n      <tr>";
                for (const auto& h : headers) {
                    tbl += "<th>" + detail::render_inline_md(h) + "</th>";
                }
                tbl += "</tr>\n    </thead>\n";
            }
            if (!body_rows.empty()) {
                tbl += "    <tbody>\n";
                for (const auto& row : body_rows) {
                    tbl += "      <tr>";
                    for (const auto& cell : row) {
                        tbl += "<td>" + detail::render_inline_md(cell) + "</td>";
                    }
                    tbl += "</tr>\n";
                }
                tbl += "    </tbody>\n";
            }
            tbl += "  </table>\n</div>\n";
            append_content(tbl);
            continue;
        }

        auto count_indent = [](std::string_view s) -> size_t {
            size_t n = 0;
            for (char c : s) {
                if (c == ' ') ++n;
                else if (c == '\t') n += 4;
                else break;
            }
            return n;
        };

        auto parse_indented_code_block = [&](size_t fence_indent) -> std::string {
            std::string fence_tl = detail::trim_md(lines[idx]);
            std::string lang = detail::trim_md(fence_tl.substr(3));
            ++idx;
            std::string code;
            while (idx < lines.size() && !detail::trim_md(lines[idx]).starts_with("```")) {
                std::string_view raw_cl = lines[idx];
                size_t stripped = 0;
                while (stripped < fence_indent && !raw_cl.empty() && raw_cl.front() == ' ') {
                    raw_cl.remove_prefix(1);
                    ++stripped;
                }
                code += raw_cl;
                code += '\n';
                ++idx;
            }
            if (idx < lines.size()) ++idx;
            std::string tag_cls = "tag-cf";
            std::string tag_name = lang;
            if (lang == "cf" || lang == "cfire") {
                tag_cls = "tag-cf";
                tag_name = "cfire (.cf)";
            } else if (lang == "cpp" || lang == "c++") {
                tag_cls = "tag-cpp";
                tag_name = "ISO C++23";
            } else if (lang == "bash" || lang == "sh") {
                tag_cls = "tag-cf";
                tag_name = "terminal";
            } else if (lang.empty() || lang == "text") {
                tag_name = "text";
            }
            std::string block = "\n    <div class=\"code-header\"><span class=\"" + tag_cls + "\">" + detail::html_escape(tag_name) + "</span><button class=\"code-copy-btn\" onclick=\"copyCode(this)\">Copy</button></div>\n";
            block += "    <pre><code>" + detail::html_escape(code) + "</code></pre>\n  ";
            return block;
        };

        auto parse_list_item_children = [&](size_t base_indent, std::string& li_body) {
            std::string sub_list;
            std::string sub_tag;
            auto flush_sub_list = [&]() {
                if (!sub_list.empty()) {
                    li_body += sub_list + "    </" + sub_tag + ">\n  ";
                    sub_list.clear();
                    sub_tag.clear();
                }
            };
            while (idx < lines.size()) {
                std::string next_raw = lines[idx];
                std::string next_tl = detail::trim_md(next_raw);
                if (next_tl.empty()) {
                    size_t look = idx + 1;
                    while (look < lines.size() && detail::trim_md(lines[look]).empty()) ++look;
                    if (look < lines.size() && count_indent(lines[look]) > base_indent) {
                        idx = look;
                        continue;
                    }
                    break;
                }
                size_t next_indent = count_indent(next_raw);
                if (next_indent > base_indent) {
                    size_t sub_p = 0;
                    if (next_tl.starts_with("```")) {
                        flush_sub_list();
                        li_body += parse_indented_code_block(next_indent);
                    } else if (next_tl.starts_with("- ") || next_tl.starts_with("* ")) {
                        if (sub_tag != "ul") {
                            flush_sub_list();
                            sub_list = "\n    <ul>\n";
                            sub_tag = "ul";
                        }
                        sub_list += "      <li>" + detail::render_inline_md(detail::trim_md(next_tl.substr(2))) + "</li>\n";
                        ++idx;
                    } else if (detail::is_md_ordered_list(next_tl, sub_p)) {
                        if (sub_tag != "ol") {
                            flush_sub_list();
                            sub_list = "\n    <ol>\n";
                            sub_tag = "ol";
                        }
                        sub_list += "      <li>" + detail::render_inline_md(detail::trim_md(next_tl.substr(sub_p))) + "</li>\n";
                        ++idx;
                    } else {
                        flush_sub_list();
                        li_body += " " + detail::render_inline_md(next_tl);
                        ++idx;
                    }
                } else {
                    break;
                }
            }
            flush_sub_list();
        };

        // Unordered lists
        if (trimmed.starts_with("- ") || trimmed.starts_with("* ")) {
            size_t base_indent = count_indent(line);
            std::string ul = "<ul>\n";
            while (idx < lines.size()) {
                std::string raw_line = lines[idx];
                std::string tl = detail::trim_md(raw_line);
                if (tl.empty()) {
                    size_t look = idx + 1;
                    while (look < lines.size() && detail::trim_md(lines[look]).empty()) ++look;
                    if (look < lines.size()) {
                        std::string ltl = detail::trim_md(lines[look]);
                        if (count_indent(lines[look]) <= base_indent && (ltl.starts_with("- ") || ltl.starts_with("* "))) {
                            idx = look;
                            continue;
                        }
                    }
                    break;
                }
                size_t cur_indent = count_indent(raw_line);
                if (cur_indent <= base_indent && (tl.starts_with("- ") || tl.starts_with("* "))) {
                    std::string li_body = detail::render_inline_md(detail::trim_md(tl.substr(2)));
                    ++idx;
                    parse_list_item_children(base_indent, li_body);
                    ul += "  <li>" + li_body + "</li>\n";
                } else {
                    break;
                }
            }
            ul += "</ul>\n";
            append_content(ul);
            continue;
        }

        // Ordered lists
        size_t cpos = 0;
        if (detail::is_md_ordered_list(trimmed, cpos)) {
            size_t base_indent = count_indent(line);
            std::string ol = "<ol>\n";
            while (idx < lines.size()) {
                std::string raw_line = lines[idx];
                std::string tl = detail::trim_md(raw_line);
                if (tl.empty()) {
                    size_t look = idx + 1;
                    while (look < lines.size() && detail::trim_md(lines[look]).empty()) ++look;
                    size_t lp = 0;
                    if (look < lines.size()) {
                        std::string ltl = detail::trim_md(lines[look]);
                        if (count_indent(lines[look]) <= base_indent && detail::is_md_ordered_list(ltl, lp)) {
                            idx = look;
                            continue;
                        }
                    }
                    break;
                }
                size_t cur_indent = count_indent(raw_line);
                size_t p = 0;
                if (cur_indent <= base_indent && detail::is_md_ordered_list(tl, p)) {
                    std::string li_body = detail::render_inline_md(detail::trim_md(tl.substr(p)));
                    ++idx;
                    parse_list_item_children(base_indent, li_body);
                    ol += "  <li>" + li_body + "</li>\n";
                } else {
                    break;
                }
            }
            ol += "</ol>\n";
            append_content(ol);
            continue;
        }

        // Single-line HTML comments (the GUIDE's generated-section markers) are not rendered.
        if (std::string tl0 = detail::trim_md(lines[idx]); tl0.starts_with("<!--") && tl0.ends_with("-->")) {
            ++idx;
            continue;
        }

        // Paragraphs
        size_t p_start_idx = idx;
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
            append_content("<p>" + detail::render_inline_md(p_text) + "</p>\n");
        } else if (idx == p_start_idx) {
            ++idx;
        }
    }

    // Attach intro content to chapter 1
    if (!chapters.empty() && !intro_html.empty()) {
        chapters[0].html = intro_html + "<hr style=\"margin: 2.5rem 0; border: none; border-top: 1px solid var(--border);\" />\n" + chapters[0].html;
        intro_html.clear();
    }

    // Build flat index of all bite-sized topics across chapters for Prev/Next Bite pagination
    struct FlatTopicRef {
        std::string ch_num;
        std::string ch_title;
        std::string sub_id; // without '#'
        std::string sub_title;
        size_t sub_idx;
    };
    std::vector<FlatTopicRef> flat_topics;
    for (const auto& ch : chapters) {
        for (size_t s = 0; s < ch.subheads.size(); ++s) {
            flat_topics.push_back({
                .ch_num = ch.num_str,
                .ch_title = ch.title,
                .sub_id = ch.subheads[s].id.substr(1),
                .sub_title = ch.subheads[s].title,
                .sub_idx = s
            });
        }
    }

    // Build Chapter Panes with Topic Pill Bar, Bite-Sized Topic Sections & Pager Cards
    std::string panes_html;
    size_t flat_cursor = 0;
    for (size_t i = 0; i < chapters.size(); ++i) {
        auto& ch = chapters[i];
        std::string active_cls = (i == 0) ? " active" : "";
        panes_html += std::format("<article class=\"chapter-pane{}\" id=\"chapter-pane-{}\" data-chapter=\"{}\" data-part=\"{}\" data-title=\"{}\" data-short-title=\"{}\">\n",
            active_cls, ch.num_str, ch.num_str, detail::clean_nav_title(ch.part), detail::clean_nav_title(ch.title), detail::short_chapter_title(ch.num, ch.title));
        panes_html += "  <div class=\"chapter-intro\">\n" + ch.html + "  </div>\n";

        // Render Bite-Sized Topic Pill Bar if chapter has subheads (compact 2-word labels on desktop)
        if (!ch.subheads.empty()) {
            panes_html += "  <div class=\"topic-pill-bar\">\n";
            panes_html += std::format("    <span class=\"topic-pill-label\">Ch {} ({})</span>\n", ch.num_str, ch.subheads.size());
            for (size_t s = 0; s < ch.subheads.size(); ++s) {
                std::string sid = ch.subheads[s].id.substr(1);
                std::string pill_active = (s == 0) ? " active" : "";
                panes_html += std::format(
                    "    <a href=\"#{}\" class=\"topic-pill{}\" data-topic-pill=\"{}\" title=\"{}\" onclick=\"navigateToSubhead('{}', '{}'); return false;\">{}.{} {}</a>\n",
                    sid, pill_active, sid, detail::clean_nav_title(ch.subheads[s].title), ch.num_str, sid, ch.num_str, s + 1, detail::short_topic_title(ch.subheads[s].title));
            }
            panes_html += "  </div>\n";

            // Render each ### subhead as a discrete bite-sized <section class="guide-topic">
            for (size_t s = 0; s < ch.subheads.size(); ++s) {
                std::string sid = ch.subheads[s].id.substr(1);
                std::string topic_active = (s == 0) ? " active-topic" : "";
                panes_html += std::format("  <section class=\"guide-topic{}\" id=\"topic-{}\" data-topic-id=\"{}\" data-topic-title=\"{}\" data-short-title=\"{}.{} {}\">\n",
                    topic_active, sid, sid, detail::clean_nav_title(ch.subheads[s].title), ch.num_str, s + 1, detail::short_topic_title(ch.subheads[s].title));
                panes_html += ch.subheads[s].html;

                // Bite-Sized Topic Pager (Prev Bite / Next Bite across chapters)
                panes_html += "    <div class=\"chapter-pager topic-pager\">\n";
                if (flat_cursor == 0) {
                    panes_html += "      <a href=\"index.html\" class=\"pager-card pager-prev\">\n"
                                  "        <span class=\"pager-label\">&larr; OVERVIEW</span>\n"
                                  "        <strong class=\"pager-title\">Landing &amp; Rosetta Showcase</strong>\n"
                                  "      </a>\n";
                } else {
                    const auto& prev_t = flat_topics[flat_cursor - 1];
                    panes_html += std::format(
                        "      <a href=\"#{}\" class=\"pager-card pager-prev\" onclick=\"navigateToSubhead('{}', '{}'); return false;\">\n"
                        "        <span class=\"pager-label\">&larr; PREVIOUS TOPIC (CH {}.{})</span>\n"
                        "        <strong class=\"pager-title\">{}</strong>\n"
                        "      </a>\n",
                        prev_t.sub_id, prev_t.ch_num, prev_t.sub_id, prev_t.ch_num, prev_t.sub_idx + 1, detail::clean_nav_title(prev_t.sub_title));
                }

                if (flat_cursor + 1 < flat_topics.size()) {
                    const auto& next_t = flat_topics[flat_cursor + 1];
                    panes_html += std::format(
                        "      <a href=\"#{}\" class=\"pager-card pager-next\" onclick=\"navigateToSubhead('{}', '{}'); return false;\">\n"
                        "        <span class=\"pager-label\">NEXT TOPIC (CH {}.{}) &rarr;</span>\n"
                        "        <strong class=\"pager-title\">{}</strong>\n"
                        "      </a>\n",
                        next_t.sub_id, next_t.ch_num, next_t.sub_id, next_t.ch_num, next_t.sub_idx + 1, detail::clean_nav_title(next_t.sub_title));
                } else {
                    panes_html += std::format(
                        "      <a href=\"reference.html\" class=\"pager-card pager-next\">\n"
                        "        <span class=\"pager-label\">NEXT &rarr;</span>\n"
                        "        <strong class=\"pager-title\">Stdlib Reference ({} Modules)</strong>\n"
                        "      </a>\n",
                        stdlib_catalog().size());
                }
                panes_html += "    </div>\n";
                panes_html += "  </section>\n";
                ++flat_cursor;
            }
        }

        // Chapter-Level Pager Controls (shown in Chapter View mode)
        panes_html += "  <div class=\"chapter-pager\">\n";
        if (i == 0) {
            panes_html += "    <a href=\"index.html\" class=\"pager-card pager-prev\">\n";
            panes_html += "      <span class=\"pager-label\">&larr; PREVIOUS</span>\n";
            panes_html += "      <strong class=\"pager-title\">Overview: Language Architecture</strong>\n";
            panes_html += "    </a>\n";
        } else {
            const auto& prev = chapters[i - 1];
            panes_html += std::format("    <a href=\"#{}\" class=\"pager-card pager-prev\" onclick=\"navigateToChapter('{}'); return false;\">\n",
                prev.id, prev.num_str);
            panes_html += "      <span class=\"pager-label\">&larr; PREVIOUS CHAPTER</span>\n";
            panes_html += std::format("      <strong class=\"pager-title\">Ch {}: {}</strong>\n", prev.num_str, detail::clean_nav_title(prev.title));
            panes_html += "    </a>\n";
        }

        if (i + 1 < chapters.size()) {
            const auto& next = chapters[i + 1];
            panes_html += std::format("    <a href=\"#{}\" class=\"pager-card pager-next\" onclick=\"navigateToChapter('{}'); return false;\">\n",
                next.id, next.num_str);
            panes_html += "      <span class=\"pager-label\">NEXT CHAPTER &rarr;</span>\n";
            panes_html += std::format("      <strong class=\"pager-title\">Ch {}: {}</strong>\n", next.num_str, detail::clean_nav_title(next.title));
            panes_html += "    </a>\n";
        } else {
            panes_html += "    <a href=\"reference.html\" class=\"pager-card pager-next\">\n";
            panes_html += "      <span class=\"pager-label\">NEXT &rarr;</span>\n";
            panes_html += std::format("      <strong class=\"pager-title\">Stdlib Reference ({} Modules)</strong>\n", stdlib_catalog().size());
            panes_html += "    </a>\n";
        }
        panes_html += "  </div>\n";
        panes_html += "</article>\n\n";
    }

    // Build Right-Rail TOC HTML
    std::string toc_rail_html;
    toc_rail_html += "<aside class=\"guide-toc-rail\" id=\"guide-toc-rail\">\n";
    toc_rail_html += "  <div class=\"toc-header\">TOPICS IN THIS CHAPTER</div>\n";
    toc_rail_html += "  <div id=\"toc-container\">\n";
    for (size_t i = 0; i < chapters.size(); ++i) {
        const auto& ch = chapters[i];
        std::string display_style = (i == 0) ? "block" : "none";
        toc_rail_html += std::format("    <div class=\"toc-chapter-group\" id=\"toc-group-{}\" style=\"display: {};\">\n", ch.num_str, display_style);
        if (!ch.subheads.empty()) {
            toc_rail_html += "      <ul class=\"toc-list\">\n";
            for (size_t s = 0; s < ch.subheads.size(); ++s) {
                const auto& sh = ch.subheads[s];
                std::string active_toc = (i == 0 && s == 0) ? " class=\"active-toc-item\"" : "";
                toc_rail_html += std::format("        <li><a href=\"{}\"{} title=\"{}\" onclick=\"navigateToSubhead('{}', '{}'); return false;\">{}.{} {}</a></li>\n",
                    sh.id, active_toc, detail::clean_nav_title(sh.title), ch.num_str, sh.id.substr(1), ch.num_str, s + 1, detail::short_topic_title(sh.title));
            }
            toc_rail_html += "      </ul>\n";
        } else {
            toc_rail_html += "      <p style=\"font-size:0.8rem;color:var(--muted);\">Chapter overview</p>\n";
        }
        toc_rail_html += "    </div>\n";
    }
    toc_rail_html += "  </div>\n";
    toc_rail_html += R"HTML(  <ul class="rail-action-list">
    <li>
      <a href="https://github.com/gsandoval/cfire/edit/main/docs/GUIDE.md" target="_blank" rel="noopener" class="rail-action-btn">
        <svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M11 4H4a2 2 0 0 0-2 2v14a2 2 0 0 0 2 2h14a2 2 0 0 0 2-2v-7"></path><path d="M18.5 2.5a2.121 2.121 0 0 1 3 3L12 15l-4 1 1-4 9.5-9.5z"></path></svg>
        <span>Edit on GitHub</span>
      </a>
    </li>
    <li>
      <button class="rail-action-btn" onclick="askAiAgent()">
        <svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><polygon points="13 2 3 14 12 14 11 22 21 10 12 10 13 2"></polygon></svg>
        <span id="ai-agent-label">Ask AI Agent</span>
      </button>
    </li>
    <li>
      <button class="rail-action-btn" onclick="sharePageUrl()">
        <svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><circle cx="18" cy="5" r="3"></circle><circle cx="6" cy="12" r="3"></circle><circle cx="18" cy="19" r="3"></circle><line x1="8.59" y1="13.51" x2="15.42" y2="17.49"></line><line x1="15.41" y1="6.51" x2="8.59" y2="10.49"></line></svg>
        <span id="share-label">Share page</span>
      </button>
    </li>
  </ul>
  <div class="rail-telemetry-box">
    <div class="rail-telemetry-row">
      <span class="rail-telemetry-label">Toolchain</span>
      <span class="rail-telemetry-val">Clang 18.1.8</span>
    </div>
    <div class="rail-telemetry-row">
      <span class="rail-telemetry-label">Target Emit</span>
      <span class="rail-telemetry-val val-amber">ISO C++23</span>
    </div>
    <div class="rail-telemetry-row">
      <span class="rail-telemetry-label">Zero GC</span>
      <span class="rail-telemetry-val">Deterministic</span>
    </div>
  </div>
)HTML";
    toc_rail_html += std::format(R"HTML(  <div class="toc-quick-links">
    <div class="toc-header">QUICK LINKS</div>
    <ul class="toc-list">
      <li><a href="reference.html">Stdlib Catalog ({} Modules) &rarr;</a></li>
      <li><a href="index.html#landscape">Systems Landscape &rarr;</a></li>
      <li><a href="index.html#benchmarks">Compiler Benchmarks &rarr;</a></li>
      <li><a href="https://github.com/gsandoval/cfire" target="_blank" rel="noopener">GitHub Repository &rarr;</a></li>
    </ul>
  </div>
</aside>
)HTML", stdlib_catalog().size());

    std::string out;
    out += "<!DOCTYPE html>\n<html lang=\"en\">\n<head>\n";
    out += "  <meta charset=\"UTF-8\" />\n";
    out += "  <meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\" />\n";
    out += "  <title>Language Guide — cfire (pronounced sapphire)</title>\n";
    out += "  <link rel=\"icon\" type=\"image/png\" href=\"assets/cfire_logo_emblem.png\" />\n";
    out += detail::icon_font_head_links();
    out += "  <style>" + detail::shared_css() + "</style>\n";
    out += "</head>\n<body>\n";
    out += detail::navbar_html("guide");
    out += "<div class=\"guide-shell\">\n";
    out += "  <aside class=\"guide-sidebar\">\n";
    out += "    <div class=\"sidebar-details\" id=\"guide-sidebar-details\">\n";
    out += "      <div class=\"mobile-unified-bar\">\n";
    out += "        <button type=\"button\" class=\"sidebar-summary\" onclick=\"this.closest('.sidebar-details').classList.toggle('open')\">\n";
    out += "          <span id=\"mobile-sidebar-label\"><span class=\"sidebar-summary-badge\">Ch 1</span> Install &amp; Setup</span>\n";
    out += "          <span class=\"material-symbols-outlined sidebar-chevron\" aria-hidden=\"true\">expand_more</span>\n";
    out += "        </button>\n";
    out += "        <select id=\"mobile-topic-select\" class=\"mobile-topic-select\" aria-label=\"Jump to subsection\" onchange=\"if(this.value){const p=this.value.split('|');navigateToSubhead(p[0],p[1]);}\"></select>\n";
    out += "        <button type=\"button\" class=\"mobile-mode-btn\" id=\"mobile-mode-cycle\" onclick=\"cycleMobileReaderMode()\" title=\"Toggle Paged vs Full Chapter view\">Paged</button>\n";
    out += "      </div>\n";
    out += "      <div class=\"sidebar-content\" id=\"sidebar-nav\">\n";
    out += "        <div class=\"nav-search-wrapper\">\n";
    out += "          <input type=\"text\" id=\"guide-filter\" class=\"search-input\" placeholder=\"Search topics, APIs &amp; code...\" oninput=\"filterGuideNav(this.value)\" />\n";
    out += "          <kbd class=\"nav-search-kbd\">⌘K</kbd>\n";
    out += "        </div>\n";
    out += "        <div id=\"guide-search-results\" class=\"search-results-dropdown\"></div>\n";

    std::string current_rendered_part;
    for (const auto& entry : chapters) {
        if (entry.part != current_rendered_part) {
            if (!current_rendered_part.empty()) {
                out += "      </ul>\n";
            }
            current_rendered_part = entry.part;
            out += "      <h4 class=\"sidebar-part-title\">" + detail::short_part_title(current_rendered_part) + "</h4>\n";
            out += "      <ul class=\"sidebar-chapter-list\">\n";
        }
        std::string is_active_cls = (entry.num == 1) ? " is-active" : "";
        std::string active_cls = (entry.num == 1) ? " active" : "";
        std::string badge_html = (entry.num == 1) ? "<span class=\"active-badge\">ACTIVE</span>" : "";
        out += std::format("        <li class=\"nav-item{}\" id=\"nav-item-{}\">\n"
                           "          <a href=\"#{}\" class=\"sidebar-ch-link{}\" data-chapter=\"{}\" title=\"{}\" onclick=\"navigateToChapter('{}'); return false;\">\n"
                           "            <span class=\"ch-text\">Ch {}: {}</span>\n"
                           "            {}\n"
                           "          </a>\n"
                           "        </li>\n",
            is_active_cls, entry.num_str,
            entry.id, active_cls, entry.num_str, detail::clean_nav_title(entry.title), entry.num_str,
            entry.num_str, detail::short_chapter_title(entry.num, entry.title),
            badge_html);
    }
    if (!current_rendered_part.empty()) {
        out += "      </ul>\n";
    }

    out += std::format(R"HTML(        <a href="reference.html" class="sidebar-footer-card">
          <span class="sfc-title">Stdlib Reference</span>
          <span class="sfc-badge">{} MODULES</span>
        </a>
      </div>
    </div>
  </aside>
)HTML", stdlib_catalog().size());

    out += R"HTML(
  <div class="guide-viewport topic-View-mode" id="guide-viewport">
    <main class="guide-content-inner">
      <div class="reader-toolbar">
        <div class="reader-breadcrumbs" id="reader-breadcrumbs">
          <a href="index.html">cfire Docs</a>
          <span class="sep">/</span>
          <span id="breadcrumb-part">Part I: Getting Started &amp; Tooling</span>
          <span class="sep">/</span>
          <span class="current" id="breadcrumb-chapter">Chapter 1: Installation &amp; Platform Setup</span>
        </div>
        <div class="toolbar-actions">
          <div class="reader-mode-toggle">
            <button class="mode-btn active" id="btn-topic-mode" onclick="setReaderMode('topic')">Topic View</button>
            <button class="mode-btn" id="btn-chapter-mode" onclick="setReaderMode('chapter')">Full Chapter</button>
            <button class="mode-btn" id="btn-all-mode" onclick="setReaderMode('all')">View All</button>
          </div>
          <button type="button" class="btn-toolbar-ai" onclick="openAiModal()" title="Generate context-rich AI prompt for this topic">
            <span class="material-symbols-outlined" style="font-size:0.95rem;" aria-hidden="true">auto_awesome</span>
            <span>Ask AI</span>
          </button>
        </div>
      </div>

)HTML";

    out += panes_html;
    out += R"HTML(      <footer class="footer" style="margin-top: 3.5rem; padding: 2rem 0; border-top: 1px solid var(--border);">
        Generated by <code>cfire doc</code> &bull; <a href="reference.html">Explore the <code>core:*</code> Stdlib &amp; Diagnostics Reference &rarr;</a>
      </footer>
    </main>
)HTML";

    out += toc_rail_html;
    out += "  </div>\n</div>\n";
    out += detail::ai_context_modal_html();

    out += std::format(R"JS(
<script>
const TOTAL_GUIDE_CHAPTERS = {};
)JS", chapters.size());

    out += R"JS(
function copyCode(btn) {
  const pre = btn.closest('.code-header').nextElementSibling;
  const code = pre ? pre.querySelector('code') : null;
  if (!code) return;
  navigator.clipboard.writeText(code.innerText).then(() => {
    const orig = btn.innerText;
    btn.innerText = 'Copied!';
    setTimeout(() => { btn.innerText = orig; }, 2000);
  });
}

function sharePageUrl() {
  navigator.clipboard.writeText(window.location.href).then(() => {
    const label = document.getElementById('share-label');
    if (label) {
      const orig = label.innerText;
      label.innerText = 'Link Copied!';
      setTimeout(() => { label.innerText = orig; }, 2000);
    }
  });
}

function filterGuideNav(query) {
  const q = query.toLowerCase().trim();
  const items = document.querySelectorAll('#sidebar-nav .nav-item');
  const resultsBox = document.getElementById('guide-search-results');
  if (!q) {
    items.forEach(li => {
      li.style.display = '';
    });
    if (resultsBox) {
      resultsBox.classList.remove('active');
      resultsBox.innerHTML = '';
    }
    return;
  }
  items.forEach(li => {
    const a = li.querySelector('.sidebar-ch-link');
    const text = (li.innerText + ' ' + (a ? (a.getAttribute('title') || '') : '')).toLowerCase();
    li.style.display = text.includes(q) ? '' : 'none';
  });

  // Deep search across all bite-sized topic sections (headings, tables, and code blocks)
  if (resultsBox) {
    const hits = [];
    document.querySelectorAll('.guide-topic').forEach(sec => {
      if (hits.length >= 12) return;
      const bodyText = sec.innerText.toLowerCase();
      if (bodyText.includes(q)) {
        const pane = sec.closest('.chapter-pane');
        const chNum = pane ? pane.getAttribute('data-chapter') : '1';
        const chTitle = pane ? (pane.getAttribute('data-short-title') || pane.getAttribute('data-title') || '') : '';
        const sid = sec.getAttribute('data-topic-id');
        const tTitle = sec.getAttribute('data-topic-title') || sid;
        hits.push({ chNum, chTitle, sid, tTitle });
      }
    });
    if (hits.length > 0) {
      resultsBox.innerHTML = hits.map(h =>
        '<a href="#' + h.sid + '" class="search-hit-item" onclick="navigateToSubhead(\'' + h.chNum + '\', \'' + h.sid + '\'); return false;">' +
        '<span class="search-hit-title">' + h.tTitle + '</span>' +
        '<span class="search-hit-meta">Ch ' + h.chNum + ': ' + h.chTitle + '</span></a>'
      ).join('');
      resultsBox.classList.add('active');
    } else {
      resultsBox.innerHTML = '<div style="padding:0.4rem 0.55rem;font-size:0.76rem;color:var(--muted);">No matching topics found</div>';
      resultsBox.classList.add('active');
    }
  }
}

function syncMobileTopicSelect(targetPane, activeSid) {
  const sel = document.getElementById('mobile-topic-select');
  if (!sel || !targetPane) return;
  const chNum = targetPane.getAttribute('data-chapter') || '1';
  const topics = targetPane.querySelectorAll('.guide-topic');
  if (topics.length === 0) {
    sel.style.display = 'none';
    return;
  }
  sel.style.display = '';
  const total = topics.length;
  let opts = '';
  topics.forEach((t, idx) => {
    const sid = t.getAttribute('data-topic-id') || '';
    const shortLbl = t.getAttribute('data-short-title') || ((idx + 1) + '. Topic');
    const selected = (sid === activeSid) ? ' selected' : '';
    opts += '<option value="' + chNum + '|' + sid + '"' + selected + '>' + shortLbl + ' (' + (idx + 1) + '/' + total + ')</option>';
  });
  sel.innerHTML = opts;
}

function activateTopicWithinChapter(targetPane, sid) {
  if (!targetPane) return;
  const topics = targetPane.querySelectorAll('.guide-topic');
  if (topics.length === 0) return;
  let matched = null;
  topics.forEach(t => {
    if (t.getAttribute('data-topic-id') === sid) {
      t.classList.add('active-topic');
      matched = t;
    } else {
      t.classList.remove('active-topic');
    }
  });
  if (!matched) {
    topics[0].classList.add('active-topic');
    matched = topics[0];
    sid = matched.getAttribute('data-topic-id');
  }
  // Show chapter intro only on the first topic in Topic View mode
  const intro = targetPane.querySelector('.chapter-intro');
  const vp = document.getElementById('guide-viewport');
  if (intro && vp && vp.classList.contains('topic-View-mode')) {
    intro.style.display = (matched === topics[0]) ? 'block' : 'none';
  } else if (intro) {
    intro.style.display = 'block';
  }
  // Sync desktop topic pills
  targetPane.querySelectorAll('.topic-pill').forEach(p => {
    if (p.getAttribute('data-topic-pill') === sid) {
      p.classList.add('active');
    } else {
      p.classList.remove('active');
    }
  });
  // Sync mobile unified topic selector
  syncMobileTopicSelect(targetPane, sid);
  // Sync right-rail TOC links
  document.querySelectorAll('.toc-list a').forEach(a => {
    a.classList.remove('active-toc-item');
  });
  const tocLink = document.querySelector('.toc-chapter-group a[href="#' + sid + '"]');
  if (tocLink) tocLink.classList.add('active-toc-item');
}

function navigateToChapter(num, updateHash) {
  if (updateHash === undefined) updateHash = true;
  if (updateHash) {
    history.pushState(null, '', '#chapter-' + num);
  }
  document.querySelectorAll('.chapter-pane').forEach(p => p.classList.remove('active'));
  const targetPane = document.getElementById('chapter-pane-' + num);
  if (targetPane) {
    targetPane.classList.add('active');
    const part = targetPane.getAttribute('data-part') || '';
    const title = targetPane.getAttribute('data-title') || '';
    const shortTitle = targetPane.getAttribute('data-short-title') || title;
    const bPart = document.getElementById('breadcrumb-part');
    const bChap = document.getElementById('breadcrumb-chapter');
    if (bPart) bPart.innerText = part;
    if (bChap) bChap.innerText = 'Ch ' + num + ': ' + title;
    const mobLabel = document.getElementById('mobile-sidebar-label');
    if (mobLabel) {
      mobLabel.innerHTML = '<span class="sidebar-summary-badge">Ch ' + num + '</span> ' + shortTitle;
    }
    const firstTopic = targetPane.querySelector('.guide-topic');
    if (firstTopic) {
      activateTopicWithinChapter(targetPane, firstTopic.getAttribute('data-topic-id'));
    } else {
      syncMobileTopicSelect(targetPane, '');
    }
  }
  const mobDetails = document.getElementById('guide-sidebar-details');
  if (mobDetails && window.innerWidth <= 840) {
    mobDetails.classList.remove('open');
  }

  // Update dynamic sidebar active chapter state
  document.querySelectorAll('#sidebar-nav .nav-item').forEach(li => li.classList.remove('is-active'));
  const activeLi = document.getElementById('nav-item-' + num);
  if (activeLi) activeLi.classList.add('is-active');

  document.querySelectorAll('.sidebar-ch-link').forEach(a => {
    const existingBadge = a.querySelector('.active-badge');
    if (existingBadge) existingBadge.remove();
    if (a.getAttribute('data-chapter') == num) {
      a.classList.add('active');
      const badge = document.createElement('span');
      badge.className = 'active-badge';
      badge.innerText = 'ACTIVE';
      a.appendChild(badge);
    } else {
      a.classList.remove('active');
    }
  });

  document.querySelectorAll('.toc-chapter-group').forEach(g => g.style.display = 'none');
  const targetToc = document.getElementById('toc-group-' + num);
  if (targetToc) {
    targetToc.style.display = 'block';
  }
  const vp = document.getElementById('guide-viewport');
  if (vp) vp.scrollTop = 0;
  if (window.innerWidth <= 840) window.scrollTo(0, 0);
}

function navigateToSubhead(num, sid) {
  navigateToChapter(num, false);
  history.pushState(null, '', '#' + sid);
  const targetPane = document.getElementById('chapter-pane-' + num);
  activateTopicWithinChapter(targetPane, sid);
  const mobDetails = document.getElementById('guide-sidebar-details');
  if (mobDetails && window.innerWidth <= 840) {
    mobDetails.classList.remove('open');
  }
  const vp = document.getElementById('guide-viewport');
  if (vp && vp.classList.contains('topic-View-mode')) {
    vp.scrollTop = 0;
    if (window.innerWidth <= 840) window.scrollTo(0, 0);
  } else {
    setTimeout(() => {
      const el = document.getElementById(sid);
      if (el) el.scrollIntoView({ behavior: 'smooth' });
    }, 50);
  }
}

function setReaderMode(mode) {
  const vp = document.getElementById('guide-viewport');
  const btnTopic = document.getElementById('btn-topic-mode');
  const btnChap = document.getElementById('btn-chapter-mode');
  const btnAll = document.getElementById('btn-all-mode');
  const mobCycle = document.getElementById('mobile-mode-cycle');
  const bChap = document.getElementById('breadcrumb-chapter');
  if (!vp) return;
  vp.classList.remove('topic-View-mode', 'chapter-View-mode', 'view-all-mode');
  if (btnTopic) btnTopic.classList.remove('active');
  if (btnChap) btnChap.classList.remove('active');
  if (btnAll) btnAll.classList.remove('active');

  if (mode === 'all') {
    vp.classList.add('view-all-mode');
    if (btnAll) btnAll.classList.add('active');
    if (mobCycle) mobCycle.textContent = 'All';
    if (bChap) bChap.innerText = 'All ' + TOTAL_GUIDE_CHAPTERS + ' Chapters';
    document.querySelectorAll('.chapter-intro').forEach(el => el.style.display = 'block');
    document.querySelectorAll('.toc-chapter-group').forEach(g => g.style.display = 'block');
  } else if (mode === 'chapter') {
    vp.classList.add('chapter-View-mode');
    if (btnChap) btnChap.classList.add('active');
    if (mobCycle) mobCycle.textContent = 'Full';
    document.querySelectorAll('.chapter-intro').forEach(el => el.style.display = 'block');
    updateChapterFromHash();
  } else {
    vp.classList.add('topic-View-mode');
    if (btnTopic) btnTopic.classList.add('active');
    if (mobCycle) mobCycle.textContent = 'Paged';
    updateChapterFromHash();
  }
}

function cycleMobileReaderMode() {
  const vp = document.getElementById('guide-viewport');
  if (!vp) return;
  if (vp.classList.contains('topic-View-mode')) {
    setReaderMode('chapter');
  } else {
    setReaderMode('topic');
  }
}

function updateChapterFromHash() {
  const hash = window.location.hash;
  if (!hash || hash === '#') {
    navigateToChapter(1, false);
    return;
  }
  if (hash.startsWith('#chapter-')) {
    const num = hash.replace('#chapter-', '');
    navigateToChapter(num, false);
    return;
  }
  const targetId = hash.substring(1);
  const targetEl = document.getElementById(targetId);
  if (targetEl) {
    const pane = targetEl.closest('.chapter-pane');
    if (pane) {
      const num = pane.getAttribute('data-chapter');
      const topicSec = targetEl.closest('.guide-topic');
      if (topicSec) {
        navigateToSubhead(num, topicSec.getAttribute('data-topic-id'));
      } else {
        navigateToChapter(num, false);
      }
      return;
    }
  }
  navigateToChapter(1, false);
}

window.addEventListener('keydown', (e) => {
  if ((e.metaKey || e.ctrlKey) && e.key.toLowerCase() === 'k') {
    e.preventDefault();
    const filter = document.getElementById('guide-filter');
    if (filter) { filter.focus(); filter.select(); }
  } else if (e.key === 'Escape') {
    closeAiModal();
  }
});

window.addEventListener('DOMContentLoaded', updateChapterFromHash);
window.addEventListener('hashchange', updateChapterFromHash);
</script>
)JS";
    out += "</body>\n</html>\n";
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
    out += "## 1. Builtin Types & Built-In `core:*` Standard Library Modules\n\n";
    for (const auto& mod : reference_modules()) {
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
            const char* kind = t.is_bench ? "bench" : (t.is_fuzz ? "fuzz" : "test");
            md += std::format("- `{} \"{}\"` (line {})\n", kind, t.name, t.line);
        }
        md += "\n";
    }
    return md;
}

// Generates the compressed LLM & AI Agent language reference sheet (`cfire doc --llm`).
[[nodiscard]] inline auto generate_llm_reference() -> std::string {
    static std::string cached_ref;
    if (!cached_ref.empty()) return cached_ref;

    std::string md = R"(# cfire Language Reference Sheet for LLMs & AI Agents

## Grammar & Syntax Rules
- Package: `package <name>` (canonical entry: `package main`)
  - Same-Directory Package Merging: All `.cf` files in the same directory declaring the same `package <name>` automatically share types, functions, methods, and constants without writing `import "./sibling.cf"` (test-only `*_test.cf` files are excluded from production builds).
  - OS-Conditional Platform Files: Files ending in `*_darwin.cf` / `*_macos.cf`, `*_linux.cf`, `*_windows.cf`, `*_ios.cf`, `*_wasm.cf`, or `*_unix.cf` are compiled only when targeting the matching platform.
- Imports:
  - Foundational Core: `import "core:<module>"` (e.g. `import "core:fs"`, `import "core:str"`, `import "core:json"`, `import "core:net/http"`, `import "core:audio"`, `import "core:keychain"`)
  - Domain Extensions: `import "ext:<module>"` (e.g. `import "ext:ai"`, `import "ext:compress/zstd"`, `import "ext:crypto/sodium"`, `import "ext:db/sqlite"`, `import "ext:net/ssh"`, `import "ext:webui"`)
  - Toolchain Packages: `import "pkg:<name>"` (e.g. `import "pkg:tako"`, resolved automatically from workspace/toolchain roots alongside `build.Tako()` in `build.cf`)
  - Local package: `import "./path/to/pkg"`
  - Foreign C/C++: `import cpp "header.h" @pkg("libname")` or `@define("MACRO")` (for linking system frameworks/libraries declaratively, set `Frameworks: [...]`, `Libraries: [...]`, `UrlSchemes: [...]`, and `SignIdentity: "..."` on `build.Executable` in `build.cf`).
- Variable Declarations:
  - Immutable binding (including top-level constants): `x := 42`, `name: str = "cfire"` (no `const` keyword)
  - Mutable binding: `mut y := 10`, `mut count: i64 = 0`
- Built-in Prelude Functions & Type Constructors:
  - Standard Output & Error: `println(fmt, ...)` / `print(fmt, ...)` (`stdout`), `eprintln(fmt, ...)` / `eprint(fmt, ...)` (`stderr`)
  - Interactive Terminal Input: `cli.prompt(message: str = "") -> str?` and `cli.prompt_password(message: str = "") -> str?` (`core:cli`); or stream directly from `os.stdin().read_line() -> str?` (`core:os`)
  - Assertions: `assert(cond)`, `assert_eq(a, b)`, `assert_ne(a, b)`, `panic(msg)`
  - Canonical Type Constructors (`T(x)`, no `as` keyword):
    - Scalar conversions: `i8(x)`, `i16(x)`, `i32(x)`, `i64(x)`, `isize(x)`, `u8(x)`, `byte(x)`, `u16(x)`, `u32(x)`, `u64(x)`, `usize(x)`, `f32(x)`, `f64(x)`, `bool(x)`
    - Byte/u8 slice & string conversions: `[]byte(s_or_slice)`, `[]u8(s_or_slice)`, `str(bytes_or_scalar)`
- Strings & Arbitrary Expression Interpolation:
  - Double-quoted strings interpolate arbitrary expressions and optional format specifiers: `"sum={a + b} first={items[0]} hex={mask:04x}"` (lowers to `std::format("sum={} first={} hex={:04x}", a + b, items[0], mask)`)
  - Raw multi-line strings: `` `SELECT * FROM users` ``
- Function Signatures:
  - `fn name(a: i32, mut b: str, move c: []byte) -> Ret! { ... }`
  - Method with receiver: `fn (mut self: Server) start() -> void! { ... }`
  - Fallible return: `-> T!` lowers to `std::expected<T, cfire::Error>`
  - Optional return: `-> T?` lowers to `std::optional<T>`
  - Multi-return tuples: `fn coords() -> (f64, f64) { return (1.0, 2.0) }`, destructuring: `(x, y) := coords()`
  - Generic parameters: `fn process[T: Display](item: T) { ... }`
- Parameter Intent Rules:
  - Default: immutable view / read-only
  - `mut`: must pass mutable variable with `mut` at call site: `update(mut val)`
  - `move`: transfers ownership with `move` at call site: `consume(move item)`
  - `out`: writes output in-place with `out` at call site: `populate(out dst)`
- Types:
  - Primitives: `i8`, `i16`, `i32`, `i64`, `isize`, `u8`, `byte`, `u16`, `u32`, `u64`, `usize`, `f32`, `f64`, `bool`, `char`, `void`
  - Strings & Byte Slices: `str`, `&str`, `[]byte`, `[]u8`
  - Slices & Arrays: `[]T`, `&[]T`, `&mut []T`, `[N]T`
  - Maps: `map[Key]Val`
  - Smart Pointers: `own[T]` (unique), `shared[T]` (reference-counted)
  - Raw Pointers: `*T`, `*mut T`
  - Struct: `type User struct { id: i64, name: str `json:"name"` }`
  - Enum: `type Shape enum { Circle(f64), Rect(f64, f64), None }`
  - Interface: `type Writer interface { write(b: []byte) -> usize! }`
- Control Flow & RAII Defer:
  - `if cond { ... } else { ... }`
  - Exclusive numeric range loop: `for i in 0..n { ... }` (iterates `0` to `n - 1`)
  - Inclusive numeric range loop: `for i in 1..=n { ... }` (iterates `1` to `n`)
  - Collection loop: `for item in slice { ... }` / `for i, item in slice { ... }`
  - Conditional / unbounded loop: `for cond { ... }` / `for { ... }`
  - Exhaustive pattern match: `match val { Shape::Circle(r) => 3.14 * r * r, Shape::Rect(w, h) => w * h }`
  - Single-statement or multi-statement RAII scope cleanup: `defer resource.close()` or `defer { f.flush(); f.close() }` (`return` inside `defer { ... }` is rejected via `cfire/RETURN-IN-DEFER`)
- Testing, Fuzzing, Benchmarks & Zero-Code Invariants:
  - Unit test blocks: `test "description" { ... }`
  - Property-based fuzz blocks: `fuzz "description" |x: i64, s: str| { ... }` (or parameterized `test "name" |x: i64| { ... }`): automatically runs 16 adversarial edge-case dictionary seeds + deterministic PRNG runs (`--fuzz-runs <N>`, `--seed <S>`) for primitives, `str`, `[]T`, and reflected user structs, with automatic counterexample shrinking on failure.
  - Multi-iteration statistical benchmarks: `bench "description" { black_box(work()) }` and parameterized variations `bench "name" |size in [64, 1024, 16384]| { ... }` or `|w in [16, 64], h in [16, 64]|` or `|k in 1..=4|`: dynamically calibrates batch size `B`, collects 10-25 timing samples over `--bench-time <ms>`, and reports `iters`, `min`, `p50`, `mean +- stddev%`, `p99`, and `ops/s`. During normal `cfire test`, each `bench` block runs 1 smoke iteration.
  - Executable doc-tests: fenced ` ```cf ` blocks inside `///` doc-comments containing `assert(...)` or `assert_eq(...)` are automatically extracted and executed by `cfire test`.
  - Zero-code automatic checks: every `test`/`fuzz` block enforces an open file-descriptor leak canary (`count_open_fds()`), and importing `core:json` with JSON-tagged structs automatically synthesizes `[auto] json round-trip: <StructName>` tests during `cfire test`.
  - Power-assertions (`assert(a == b)`, `assert_eq(a, b)`, `assert_ne(a, b)`): evaluate each operand once and automatically print `[left: <val>, right: <val>]` value diffs on failure.
- Error (`T!`) & Optional (`T?`) Handling:
  - `val := try fallible()` (early return error on failure)
  - `val := fallible() or fallback_expr`
  - `val := fallible() or err { return -1 }` or parameterless `val := fallible() or { return -1 }` (block must definitely diverge or panic)
  - `val := optional() or fallback_expr`
  - `val := optional() or { return -1 }` (`or err { ... }` on `T?` is rejected via `cfire/OPTIONAL-OR-ERR`)
  - Flow-sensitive narrowing: after `if opt != none { ... }` or `if opt == none { return }`, `opt` is narrowed to `T` and its fields/methods can be accessed directly (`opt.field`).
- Memory Safety, Loan Tracking & Bounds Rules:
  - Move Discipline (`cfire/USE-AFTER-FREE`, `cfire/MOVE-CALLSITE`): Passing a non-Copy variable (`own[T]`, `str`, `[]T`, `map[K]V`, or structs containing them) with `move` invalidates the variable unless reassigned on all control-flow paths; `move` parameters require `move` at the call site.
  - Region & Loan Tracking (`cfire/DANGLING-POINTER`, `cfire/DANGLING-RETURN`, `cfire/ITERATOR-INVALIDATION`): Taking `&x` or `&mut x` records an active loan on `x`; mutating, moving, or outliving `x` while borrowed, returning `&local`, or calling `.push()`/`.clear()`/`.erase()` on a slice/map while iterating over it is rejected at compile time.
  - Null-Safety Flow Analysis (`cfire/NULL-DEREF`): Raw pointers (`*T`, `*mut T`) and smart pointers (`own[T]`, `shared[T]`) must be initialized non-null or guarded by `if p != none` / `assert(p != none)` before `*p` or `p.field` dereference.
  - Bounds & Arithmetic Safety (`cfire/INDEX-OUT-OF-BOUNDS`, `cfire/DIVIDE-BY-ZERO`): Constant out-of-bounds indexing and division/modulo by constant zero are rejected at compile time; dynamic slice indexing (`arr[i]`) and `slice_from(s, offset, count)` enforce bounds checks at runtime.
  - Struct Tag Validation (`cfire/INVALID-STRUCT-TAG`): Duplicate `json`, `sql`/`db`, or `cli` flag tags across fields of the same struct, or invalid `default:"..."` literals that cannot parse into numeric/boolean field types, are rejected at compile time.
  - Injection Warnings (`cfire/SQL-INJECTION-RISK`, `cfire/SHELL-INJECTION-RISK`): Dynamic string concatenation in `db.exec`/`db.query` or `os.exec`/`os.exec_output` emits a warning; use parameterized `?1` queries.
- Foreign C/C++: `cpp { /* verbatim C++23 */ }`, `cpp::some_c_func()`
- CLI Tooling:
  - `cfire init [--template cli|tako|gui] [--name <pkg>]` / `cfire new <dir> [--template cli|tako|gui]`: Scaffold a new `cfire` project (`build.cf`, `src/main.cf`, `tests/main_test.cf`, `.gitignore`)
  - `cfire run [--web] [--ios] [target]`: Run native app, execute binary, launch iOS Simulator (`--ios`), or serve web app (`--web`)
  - `cfire build [--target wasm|ios-sim|ios] [--web-profile canvas|cli] [--subsystem gui|console] [-o <out>] [target]`: Compile native binary, signed macOS/iOS `.app` bundle, or WebAssembly
  - `cfire check [--json] [--fix] [file|dir]`: Zero-arg workspace-aware type & borrow checker with 1-based coordinates and fixits
  - `cfire test [--sanitize] [--json] [--filter <pat>] [--bench] [--fuzz-runs <N>] [--seed <S>] [--bench-time <ms>] [file|dir]`: Zero-arg workspace test & fuzz runner (discovers `test`, `fuzz`, `///` doc-tests, and `[auto] json round-trip` invariants)
  - `cfire bench [--json] [--filter <pat>] [--bench-time <ms>] [file|dir]`: Multi-iteration parameterized statistical benchmark runner
  - `cfire fix [--dry-run] [--json] [file|dir]`: Zero-arg workspace auto-fixer for missing `core:*` imports, `mut` call-site markers, and `match` arms
  - `cfire serve [--port <n>] [--no-open] [target|path]`: Built-in zero-dependency static server with WASM MIME and COOP/COEP isolation headers
  - `cfire outline [--json] [file|dir]`: AST symbol outline with exact types, intents, receivers, interfaces, and test/fuzz/bench blocks
  - `cfire inspect <target|cfire/CODE>`: Inspect symbol, header, module, or compiler diagnostic rule (`cfire inspect cfire/USE-AFTER-FREE`)
  - `cfire install [--user|--prefix <dir>|--portable <dir>]`: Install self-relocating `cfire` binary and `runtime/` headers
  - `cfire ide [target|--all|--status] [--global]`: Auto-detect & configure LSP, MCP, and agent rules for VS Code, Antigravity (`agy`), Claude Code (`claude`), Cursor, Windsurf, Zed, Neovim, and CLion
  - `cfire mcp [--setup|--print-config]`: Start built-in Model Context Protocol server over stdio (`cfire_stdlib_list`, `cfire_stdlib_lookup`, `cfire_check`, `cfire_test`, `cfire_eval`, `cfire_outline`, `cfire_inspect`, `cfire_doc_llm`)
  - `cfire doc --llm`: Dump this LLM context reference sheet
  - `cfire eval 'expr'`: Immediate evaluation of expressions

## Standard Library Modules (`core:*`, `ext:*`, `vendor:*`)
)";

    for (const auto& mod : stdlib_catalog()) {
        md += std::format("\n### `import \"{}\"` (alias: `{}`)\n", mod.import_path, mod.ns_alias);
        md += std::format("{}\n", mod.overview);
        if (!mod.functions.empty()) {
            md += "Functions & Methods:\n";
            for (const auto& fn : mod.functions) {
                if (fn.name.contains('.')) {
                    md += std::format("- `{}`: {}\n", fn.cf_sig, fn.summary);
                } else {
                    md += std::format("- `{}` (call: `{}.{}`): {}\n", fn.cf_sig, mod.ns_alias, fn.name, fn.summary);
                }
            }
        }
    }

    cached_ref = std::move(md);
    return cached_ref;
}

} // namespace cfire::doc

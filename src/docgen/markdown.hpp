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

    struct SubHead {
        std::string id;
        std::string title;
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

    auto append_content = [&](std::string_view chunk) {
        if (chapters.empty()) {
            intro_html += chunk;
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
            std::string sid = detail::slugify_heading(rest);
            if (!chapters.empty()) {
                chapters.back().subheads.push_back({.id = "#" + sid, .title = std::string(rest)});
                chapters.back().html += "<h3 id=\"" + sid + "\">" + detail::render_inline_md(rest) + "</h3>\n";
            } else {
                intro_html += "<h3 id=\"" + sid + "\">" + detail::render_inline_md(rest) + "</h3>\n";
            }
            ++idx;
            continue;
        }
        if (trimmed.starts_with("#### ")) {
            auto rest = detail::trim_md(trimmed.substr(5));
            std::string sid = detail::slugify_heading(rest);
            auto h4_tag = "<h4 id=\"" + sid + "\">" + detail::render_inline_md(rest) + "</h4>\n";
            append_content(h4_tag);
            ++idx;
            continue;
        }

        // Horizontal rules
        if (trimmed == "---" || trimmed == "***" || trimmed == "___") {
            append_content("<hr style=\"margin: 2.5rem 0; border: none; border-top: 1px solid var(--border);\" />\n");
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
                bq_text.starts_with("[!IMPORTANT]") || bq_text.starts_with("[!WARNING]")) {
                std::string kind = "note";
                std::string title = "Note";
                size_t prefix_len = 7;
                if (bq_text.starts_with("[!TIP]")) { kind = "tip"; title = "Tip"; prefix_len = 6; }
                else if (bq_text.starts_with("[!IMPORTANT]")) { kind = "important"; title = "Important"; prefix_len = 12; }
                else if (bq_text.starts_with("[!WARNING]")) { kind = "warning"; title = "Warning"; prefix_len = 10; }
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

        // Unordered lists
        if (trimmed.starts_with("- ") || trimmed.starts_with("* ")) {
            std::string ul = "<ul>\n";
            while (idx < lines.size()) {
                std::string tl = detail::trim_md(lines[idx]);
                if (tl.starts_with("- ") || tl.starts_with("* ")) {
                    ul += "  <li>" + detail::render_inline_md(detail::trim_md(tl.substr(2))) + "</li>\n";
                    ++idx;
                } else if (!tl.empty() && (lines[idx].starts_with("  ") || lines[idx].starts_with('\t'))) {
                    ul += " " + detail::render_inline_md(tl);
                    ++idx;
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
            std::string ol = "<ol>\n";
            while (idx < lines.size()) {
                std::string tl = detail::trim_md(lines[idx]);
                size_t p = 0;
                if (detail::is_md_ordered_list(tl, p)) {
                    ol += "  <li>" + detail::render_inline_md(detail::trim_md(tl.substr(p))) + "</li>\n";
                    ++idx;
                } else if (!tl.empty() && (lines[idx].starts_with("  ") || lines[idx].starts_with('\t'))) {
                    ol += " " + detail::render_inline_md(tl);
                    ++idx;
                } else {
                    break;
                }
            }
            ol += "</ol>\n";
            append_content(ol);
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
            append_content("<p>" + detail::render_inline_md(p_text) + "</p>\n");
        }
    }

    // Attach intro content to chapter 1
    if (!chapters.empty() && !intro_html.empty()) {
        chapters[0].html = intro_html + "<hr style=\"margin: 2.5rem 0; border: none; border-top: 1px solid var(--border);\" />\n" + chapters[0].html;
        intro_html.clear();
    }

    // Build Chapter Panes with Pager Cards
    std::string panes_html;
    for (size_t i = 0; i < chapters.size(); ++i) {
        auto& ch = chapters[i];
        std::string active_cls = (i == 0) ? " active" : "";
        panes_html += std::format("<article class=\"chapter-pane{}\" id=\"chapter-pane-{}\" data-chapter=\"{}\" data-part=\"{}\" data-title=\"{}\">\n",
            active_cls, ch.num_str, ch.num_str, detail::html_escape(ch.part), detail::html_escape(ch.title));
        panes_html += ch.html;

        // Pager Controls
        panes_html += "  <div class=\"chapter-pager\">\n";
        if (i == 0) {
            panes_html += "    <a href=\"index.html\" class=\"pager-btn pager-prev\">\n";
            panes_html += "      <span class=\"pager-dir\">&larr; Overview</span>\n";
            panes_html += "      <span class=\"pager-title\">Language Architecture</span>\n";
            panes_html += "    </a>\n";
        } else {
            const auto& prev = chapters[i - 1];
            panes_html += std::format("    <a href=\"#{}\" class=\"pager-btn pager-prev\" onclick=\"navigateToChapter('{}')\">\n",
                prev.id, prev.num_str);
            panes_html += "      <span class=\"pager-dir\">&larr; Previous Chapter</span>\n";
            panes_html += std::format("      <span class=\"pager-title\">{}. {}</span>\n", prev.num_str, detail::html_escape(prev.title));
            panes_html += "    </a>\n";
        }

        if (i + 1 < chapters.size()) {
            const auto& next = chapters[i + 1];
            panes_html += std::format("    <a href=\"#{}\" class=\"pager-btn pager-next\" onclick=\"navigateToChapter('{}')\">\n",
                next.id, next.num_str);
            panes_html += "      <span class=\"pager-dir\">Next Chapter &rarr;</span>\n";
            panes_html += std::format("      <span class=\"pager-title\">{}. {}</span>\n", next.num_str, detail::html_escape(next.title));
            panes_html += "    </a>\n";
        } else {
            panes_html += "    <a href=\"reference.html\" class=\"pager-btn pager-next\">\n";
            panes_html += "      <span class=\"pager-dir\">Stdlib Reference &rarr;</span>\n";
            panes_html += std::format("      <span class=\"pager-title\">Explore {} Built-in Modules</span>\n", stdlib_catalog().size());
            panes_html += "    </a>\n";
        }
        panes_html += "  </div>\n";
        panes_html += "</article>\n\n";
    }

    // Build Right-Rail TOC HTML
    std::string toc_rail_html;
    toc_rail_html += "<aside class=\"guide-toc-rail\" id=\"guide-toc-rail\">\n";
    toc_rail_html += "  <div class=\"toc-header\">ON THIS PAGE</div>\n";
    toc_rail_html += "  <div id=\"toc-container\">\n";
    for (size_t i = 0; i < chapters.size(); ++i) {
        const auto& ch = chapters[i];
        std::string display_style = (i == 0) ? "block" : "none";
        toc_rail_html += std::format("    <div class=\"toc-chapter-group\" id=\"toc-group-{}\" style=\"display: {};\">\n", ch.num_str, display_style);
        if (!ch.subheads.empty()) {
            toc_rail_html += "      <ul class=\"toc-list\">\n";
            for (const auto& sh : ch.subheads) {
                toc_rail_html += std::format("        <li><a href=\"{}\" onclick=\"navigateToSubhead('{}', '{}')\">{}</a></li>\n",
                    sh.id, ch.num_str, sh.id.substr(1), detail::html_escape(sh.title));
            }
            toc_rail_html += "      </ul>\n";
        } else {
            toc_rail_html += "      <p style=\"font-size:0.8rem;color:var(--muted);\">Chapter overview</p>\n";
        }
        toc_rail_html += "    </div>\n";
    }
    toc_rail_html += "  </div>\n";
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
    out += "  <style>" + detail::shared_css() + "</style>\n";
    out += "</head>\n<body>\n";
    out += detail::navbar_html("guide");
    out += "<div class=\"guide-shell\">\n";
    out += "  <aside class=\"guide-sidebar\">\n";
    out += "    <details class=\"sidebar-details\" open>\n";
    out += "      <summary class=\"sidebar-summary\">\n";
    out += "        <span>Chapters &amp; Navigation</span>\n";
    out += "        <span class=\"sidebar-chevron\">&#9662;</span>\n";
    out += "      </summary>\n";
    out += "      <div class=\"sidebar-content\" id=\"sidebar-nav\">\n";
    out += "        <input type=\"text\" id=\"guide-filter\" class=\"search-input\" placeholder=\"Filter chapters &amp; topics...\" oninput=\"filterGuideNav(this.value)\" style=\"margin-bottom: 0.75rem;\" />\n";

    std::string current_rendered_part;
    for (const auto& entry : chapters) {
        if (entry.part != current_rendered_part) {
            if (!current_rendered_part.empty()) {
                out += "        </ul>\n";
            }
            current_rendered_part = entry.part;
            out += "        <h4 class=\"sidebar-part-title\">" + detail::html_escape(current_rendered_part) + "</h4>\n";
            out += "        <ul class=\"sidebar-chapter-list\">\n";
        }
        std::string active_cls = (entry.num == 1) ? " active" : "";
        out += std::format("          <li class=\"nav-item\"><a href=\"#{}\" class=\"sidebar-ch-link{}\" data-chapter=\"{}\" onclick=\"navigateToChapter('{}')\">{}. {}</a>",
            entry.id, active_cls, entry.num_str, entry.num_str, entry.num_str, detail::html_escape(entry.title));
        if (!entry.subheads.empty()) {
            out += "\n            <ul class=\"subhead-list\">\n";
            for (const auto& sub : entry.subheads) {
                out += std::format("              <li><a href=\"{}\" onclick=\"navigateToSubhead('{}', '{}')\">{}</a></li>\n",
                    sub.id, entry.num_str, sub.id.substr(1), detail::html_escape(sub.title));
            }
            out += "            </ul>\n          ";
        }
        out += "</li>\n";
    }
    if (!current_rendered_part.empty()) {
        out += "        </ul>\n";
    }

    out += std::format(R"HTML(        <div class="toc-quick-links" style="margin-top: 1.25rem;">
          <h4 style="margin-bottom: 0.5rem;">Quick Links</h4>
          <ul style="list-style: none; padding: 0; margin: 0;">
            <li style="margin-bottom: 0.35rem;"><a href="reference.html">Stdlib Catalog ({} Modules) &rarr;</a></li>
            <li style="margin-bottom: 0.35rem;"><a href="index.html#landscape">Systems Landscape &rarr;</a></li>
            <li style="margin-bottom: 0.35rem;"><a href="index.html#benchmarks">Compiler Benchmarks &rarr;</a></li>
          </ul>
        </div>
      </div>
    </details>
  </aside>

  <div class="guide-viewport" id="guide-viewport">
    <div class="guide-content-inner">
      <div class="reader-toolbar">
        <div class="reader-breadcrumbs" id="reader-breadcrumbs">
          <a href="index.html">cfire Docs</a>
          <span class="sep">/</span>
          <span id="breadcrumb-part">Part I: Getting Started &amp; Tooling</span>
          <span class="sep">/</span>
          <span class="current" id="breadcrumb-chapter">Chapter 1: Installation &amp; Platform Setup</span>
        </div>
        <div class="reader-mode-toggle">
          <button class="mode-btn active" id="btn-chapter-mode" onclick="setReaderMode('chapter')">Chapter View</button>
          <button class="mode-btn" id="btn-all-mode" onclick="setReaderMode('all')">View All</button>
        </div>
      </div>

)HTML", stdlib_catalog().size());

    out += panes_html;
    out += R"HTML(      <footer class="footer" style="margin-top: 3.5rem; padding: 2rem 0; border-top: 1px solid var(--border);">
        Generated by <code>cfire doc</code> &bull; <a href="reference.html">Explore the `core:*` Stdlib &amp; Diagnostics Reference &rarr;</a>
      </footer>
    </div>
)HTML";

    out += toc_rail_html;
    out += "  </div>\n</div>\n";

    out += R"JS(
<script>
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

function filterGuideNav(query) {
  const q = query.toLowerCase().trim();
  const items = document.querySelectorAll('#sidebar-nav .nav-item');
  items.forEach(li => {
    const text = li.innerText.toLowerCase();
    if (!q || text.includes(q)) {
      li.style.display = '';
    } else {
      li.style.display = 'none';
    }
  });
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
    const bPart = document.getElementById('breadcrumb-part');
    const bChap = document.getElementById('breadcrumb-chapter');
    if (bPart) bPart.innerText = part;
    if (bChap) bChap.innerText = title;
  }
  document.querySelectorAll('.sidebar-ch-link').forEach(a => {
    if (a.getAttribute('data-chapter') == num) {
      a.classList.add('active');
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
}

function navigateToSubhead(num, sid) {
  navigateToChapter(num, false);
  history.pushState(null, '', '#' + sid);
  setTimeout(() => {
    const el = document.getElementById(sid);
    if (el) el.scrollIntoView({ behavior: 'smooth' });
  }, 50);
}

function setReaderMode(mode) {
  const vp = document.getElementById('guide-viewport');
  const btnChap = document.getElementById('btn-chapter-mode');
  const btnAll = document.getElementById('btn-all-mode');
  const bChap = document.getElementById('breadcrumb-chapter');
  if (!vp) return;
  if (mode === 'all') {
    vp.classList.add('view-all-mode');
    if (btnAll) btnAll.classList.add('active');
    if (btnChap) btnChap.classList.remove('active');
    if (bChap) bChap.innerText = 'All 16 Chapters';
    document.querySelectorAll('.toc-chapter-group').forEach(g => g.style.display = 'block');
  } else {
    vp.classList.remove('view-all-mode');
    if (btnChap) btnChap.classList.add('active');
    if (btnAll) btnAll.classList.remove('active');
    updateChapterFromHash();
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
      navigateToChapter(num, false);
      setTimeout(() => {
        targetEl.scrollIntoView({ behavior: 'smooth' });
      }, 50);
      return;
    }
  }
  navigateToChapter(1, false);
}

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
[[nodiscard]] inline auto generate_llm_reference() -> std::string {
    static std::string cached_ref;
    if (!cached_ref.empty()) return cached_ref;

    std::string md = R"(# cfire Language Reference Sheet for LLMs & AI Agents

## Grammar & Syntax Rules
- Package: `package <name>` (canonical entry: `package main`)
- Imports:
  - Stdlib: `import "core:<module>"` (e.g. `import "core:fs"`)
  - Local package: `import "./path/to/pkg"`
  - Foreign C/C++: `import cpp "header.h" @pkg("libname")` or `@define("MACRO")`
- Variable Declarations:
  - Immutable binding: `x := 42`, `name: str = "cfire"`
  - Mutable binding: `mut y := 10`, `mut count: i64 = 0`
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
- Types:
  - Primitives: `i8`, `i16`, `i32`, `i64`, `isize`, `u8`, `u16`, `u32`, `u64`, `usize`, `f32`, `f64`, `bool`, `char`, `void`
  - Strings & Bytes: `str`, `&str`, `bytes`, `[]byte`
  - Slices & Arrays: `[]T`, `&[]T`, `&mut []T`, `[N]T`
  - Maps: `map[Key]Val`
  - Smart Pointers: `own[T]` (unique), `shared[T]` (reference-counted)
  - Raw Pointers: `*T`, `*mut T`
  - Struct: `type User struct { id: i64, name: str `json:"name"` }`
  - Enum: `type Shape enum { Circle(f64), Rect(f64, f64), None }`
  - Interface: `type Writer interface { write(b: []byte) -> usize! }`
- Control Flow:
  - `if cond { ... } else { ... }`
  - `for item in slice { ... }` / `for i, item in slice { ... }`
  - `match val { Shape::Circle(r) => 3.14 * r * r, Shape::Rect(w, h) => w * h }`
  - `defer resource.close()`
- Error Handling:
  - `val := try fallible()` (early return error on failure)
  - `val := fallible() or fallback_expr`
  - `val := fallible() or err { return -1 }` (block must definitely diverge or panic)
- Foreign C/C++: `cpp { /* verbatim C++23 */ }`, `cpp::some_c_func()`
- CLI Tooling:
  - `cfire run [--web] [target]`: Run native app, execute binary, or serve web app (--web)
  - `cfire build [--target wasm] [--web-profile canvas|cli] [--subsystem gui|console] [-o <out>] [target]`: Compile native binary, signed macOS `.app` bundle, or WebAssembly
  - `cfire serve [--port <n>] [--no-open] [target|path]`: Built-in zero-dependency static server with WASM MIME and COOP/COEP isolation headers
  - `cfire check --json <file>`: Multi-error structured diagnostics with 1-based coordinates and fixits
  - `cfire outline --json <file|dir>`: AST symbol outline with exact types, intents, receivers
  - `cfire doc --llm`: Dump this LLM context reference sheet
  - `cfire test`: Run tests with assertion location reporting
  - `cfire eval 'expr'`: Immediate evaluation of expressions

## Standard Library Modules (`core:*`)
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

#pragma once
// runtime/core/ai/doc_office.hpp — OOXML (DOCX/PPTX/XLSX), OpenDocument (ODT/ODP/ODS), HTML & EPUB Parsers

#include "doc_zip_deflate.hpp"

namespace cfire::ai::detail {

inline void parse_docx_xml(std::string_view xml, Document& doc) {
    doc.format = "docx";
    std::string full_md;
    std::string current_sec_title = "Document";
    std::string current_sec_body;
    std::int32_t sec_idx = 1;

    std::size_t pos = 0;
    while (pos < xml.size()) {
        auto next_p = xml.find("<w:p", pos);
        auto next_tbl = xml.find("<w:tbl", pos);
        if (next_p == std::string_view::npos && next_tbl == std::string_view::npos) break;

        if (next_tbl != std::string_view::npos && (next_p == std::string_view::npos || next_tbl < next_p)) {
            auto end_tbl = xml.find("</w:tbl>", next_tbl);
            if (end_tbl == std::string_view::npos) break;
            std::string_view tbl_xml = xml.substr(next_tbl, end_tbl - next_tbl);
            std::size_t tr_pos = 0;
            bool first_row = true;
            while ((tr_pos = tbl_xml.find("<w:tr", tr_pos)) != std::string_view::npos) {
                auto end_tr = tbl_xml.find("</w:tr>", tr_pos);
                if (end_tr == std::string_view::npos) break;
                std::string_view tr_xml = tbl_xml.substr(tr_pos, end_tr - tr_pos);
                std::vector<std::string> cells;
                std::size_t tc_pos = 0;
                while ((tc_pos = tr_xml.find("<w:tc", tc_pos)) != std::string_view::npos) {
                    auto end_tc = tr_xml.find("</w:tc>", tc_pos);
                    if (end_tc == std::string_view::npos) break;
                    cells.push_back(trim_ws(extract_xml_text_runs(tr_xml.substr(tc_pos, end_tc - tc_pos), "w:t")));
                    tc_pos = end_tc + 7;
                }
                if (!cells.empty()) {
                    std::string row_line = "| ";
                    for (const auto& c : cells) row_line += c + " | ";
                    row_line.pop_back();
                    full_md += row_line + "\n";
                    current_sec_body += row_line + "\n";
                    if (first_row) {
                        std::string sep_line = "|";
                        for (std::size_t k = 0; k < cells.size(); ++k) sep_line += " --- |";
                        full_md += sep_line + "\n";
                        current_sec_body += sep_line + "\n";
                        first_row = false;
                    }
                }
                tr_pos = end_tr + 7;
            }
            full_md += "\n";
            current_sec_body += "\n";
            pos = end_tbl + 8;
            continue;
        }

        // Check <w:p> or <w:p ...>
        std::size_t after_p = next_p + 4;
        if (after_p < xml.size() && xml[after_p] != '>' && !std::isspace(static_cast<unsigned char>(xml[after_p])) && xml[after_p] != '/') {
            pos = after_p;
            continue;
        }
        auto end_p = xml.find("</w:p>", next_p);
        if (end_p == std::string_view::npos) break;
        std::string_view p_xml = xml.substr(next_p, end_p - next_p);
        std::string text = trim_ws(extract_xml_text_runs(p_xml, "w:t"));
        if (!text.empty()) {
            int heading_level = 0;
            auto st_pos = p_xml.find("w:pStyle");
            if (st_pos != std::string_view::npos) {
                auto val_pos = p_xml.find("w:val=\"", st_pos);
                if (val_pos != std::string_view::npos) {
                    val_pos += 7;
                    auto q_end = p_xml.find('"', val_pos);
                    if (q_end != std::string_view::npos) {
                        std::string_view style_name = p_xml.substr(val_pos, q_end - val_pos);
                        if (style_name.starts_with("Heading") || style_name.starts_with("heading")) {
                            char last = style_name.back();
                            if (last >= '1' && last <= '6') heading_level = last - '0';
                            else heading_level = 1;
                        } else if (style_name == "Title") {
                            heading_level = 1;
                        } else if (style_name == "Subtitle") {
                            heading_level = 2;
                        }
                    }
                }
            }

            if (heading_level > 0) {
                if (!trim_ws(current_sec_body).empty()) {
                    doc.sections.push_back(DocumentSection{
                        .index = sec_idx++,
                        .title = current_sec_title,
                        .text = trim_ws(current_sec_body)
                    });
                    current_sec_body.clear();
                }
                current_sec_title = text;
                std::string hashes(static_cast<std::size_t>(heading_level), '#');
                std::string line = hashes + " " + text + "\n\n";
                full_md += line;
                current_sec_body += line;
            } else {
                full_md += text + "\n\n";
                current_sec_body += text + "\n\n";
            }
        }
        pos = end_p + 6;
    }

    if (!trim_ws(current_sec_body).empty()) {
        doc.sections.push_back(DocumentSection{
            .index = sec_idx,
            .title = current_sec_title,
            .text = trim_ws(current_sec_body)
        });
    }
    doc.text = trim_ws(full_md);
}

inline void parse_pptx_zip(const std::unordered_map<std::string, std::string>& entries, Document& doc) {
    doc.format = "pptx";
    struct SlideItem {
        int num = 0;
        std::string xml;
        std::string notes_xml;
    };
    std::vector<SlideItem> slides;
    for (const auto& [name, content] : entries) {
        if (name.starts_with("ppt/slides/slide") && name.ends_with(".xml")) {
            std::string_view num_sv(name);
            num_sv.remove_prefix(16); // "ppt/slides/slide"
            num_sv.remove_suffix(4);  // ".xml"
            int slide_num = 0;
            for (char c : num_sv) {
                if (std::isdigit(static_cast<unsigned char>(c))) slide_num = slide_num * 10 + (c - '0');
            }
            if (slide_num > 0) {
                std::string notes_key = std::format("ppt/notesSlides/notesSlide{}.xml", slide_num);
                std::string notes = entries.contains(notes_key) ? entries.at(notes_key) : "";
                slides.push_back(SlideItem{slide_num, content, std::move(notes)});
            }
        }
    }
    std::sort(slides.begin(), slides.end(), [](const SlideItem& a, const SlideItem& b) { return a.num < b.num; });

    std::string full_md;
    for (const auto& sl : slides) {
        std::string slide_title;
        std::vector<std::string> body_lines;

        // Parse <p:sp> shapes to distinguish title placeholder from body text
        std::size_t sp_pos = 0;
        while ((sp_pos = sl.xml.find("<p:sp", sp_pos)) != std::string::npos) {
            auto end_sp = sl.xml.find("</p:sp>", sp_pos);
            if (end_sp == std::string::npos) break;
            std::string_view sp_xml(sl.xml.data() + sp_pos, end_sp - sp_pos);
            bool is_title = (sp_xml.find("type=\"title\"") != std::string_view::npos) ||
                            (sp_xml.find("type=\"ctrTitle\"") != std::string_view::npos);

            std::size_t p_pos = 0;
            while ((p_pos = sp_xml.find("<a:p", p_pos)) != std::string_view::npos) {
                auto end_p = sp_xml.find("</a:p>", p_pos);
                if (end_p == std::string_view::npos) break;
                std::string para = trim_ws(extract_xml_text_runs(sp_xml.substr(p_pos, end_p - p_pos), "a:t"));
                if (!para.empty()) {
                    if (is_title && slide_title.empty()) slide_title = para;
                    else body_lines.push_back(para);
                }
                p_pos = end_p + 6;
            }
            sp_pos = end_sp + 7;
        }

        // Fallback if <a:t> wasn't inside <p:sp>
        if (slide_title.empty() && body_lines.empty()) {
            std::string raw = trim_ws(extract_xml_text_runs(sl.xml, "a:t"));
            if (!raw.empty()) body_lines.push_back(raw);
        }
        if (slide_title.empty()) {
            if (!body_lines.empty()) {
                slide_title = body_lines.front();
                body_lines.erase(body_lines.begin());
            } else {
                slide_title = std::format("Slide {}", sl.num);
            }
        }

        std::string sec_md = std::format("## Slide {}: {}\n", sl.num, slide_title);
        for (const auto& b : body_lines) {
            sec_md += "- " + b + "\n";
        }
        if (!sl.notes_xml.empty()) {
            std::string notes_txt = trim_ws(extract_xml_text_runs(sl.notes_xml, "a:t"));
            if (!notes_txt.empty()) {
                sec_md += "Notes: " + notes_txt + "\n";
            }
        }

        doc.sections.push_back(DocumentSection{
            .index = sl.num,
            .title = slide_title,
            .text = trim_ws(sec_md)
        });
        full_md += sec_md + "\n";
    }
    doc.text = trim_ws(full_md);
}

inline void parse_xlsx_zip(const std::unordered_map<std::string, std::string>& entries, Document& doc) {
    doc.format = "xlsx";
    std::vector<std::string> shared_strings;
    if (auto it = entries.find("xl/sharedStrings.xml"); it != entries.end()) {
        std::string_view ss_xml = it->second;
        std::size_t si_pos = 0;
        while ((si_pos = ss_xml.find("<si", si_pos)) != std::string_view::npos) {
            auto end_si = ss_xml.find("</si>", si_pos);
            if (end_si == std::string_view::npos) break;
            shared_strings.push_back(extract_xml_text_runs(ss_xml.substr(si_pos, end_si - si_pos), "t"));
            si_pos = end_si + 5;
        }
    }

    struct SheetItem {
        int num = 0;
        std::string xml;
    };
    std::vector<SheetItem> sheets;
    for (const auto& [name, content] : entries) {
        if (name.starts_with("xl/worksheets/sheet") && name.ends_with(".xml")) {
            std::string_view num_sv(name);
            num_sv.remove_prefix(19);
            num_sv.remove_suffix(4);
            int num = 0;
            for (char c : num_sv) {
                if (std::isdigit(static_cast<unsigned char>(c))) num = num * 10 + (c - '0');
            }
            if (num > 0) sheets.push_back(SheetItem{num, content});
        }
    }
    std::sort(sheets.begin(), sheets.end(), [](const SheetItem& a, const SheetItem& b) { return a.num < b.num; });

    std::string full_md;
    for (const auto& sh : sheets) {
        std::string sheet_title = std::format("Sheet {}", sh.num);
        std::string sheet_md = std::format("## {}\n", sheet_title);
        std::string_view xml = sh.xml;
        std::size_t row_pos = 0;
        bool first_row = true;
        while ((row_pos = xml.find("<row", row_pos)) != std::string_view::npos) {
            auto end_row = xml.find("</row>", row_pos);
            if (end_row == std::string_view::npos) break;
            std::string_view row_xml = xml.substr(row_pos, end_row - row_pos);
            std::vector<std::string> cells;
            std::size_t c_pos = 0;
            while ((c_pos = row_xml.find("<c", c_pos)) != std::string_view::npos) {
                if (c_pos + 2 < row_xml.size() && row_xml[c_pos + 2] != ' ' && row_xml[c_pos + 2] != '>') {
                    c_pos += 2;
                    continue;
                }
                auto gt = row_xml.find('>', c_pos);
                if (gt == std::string_view::npos) break;
                if (row_xml[gt - 1] == '/') {
                    cells.emplace_back("");
                    c_pos = gt + 1;
                    continue;
                }
                auto end_c = row_xml.find("</c>", gt);
                if (end_c == std::string_view::npos) break;
                std::string_view c_tag = row_xml.substr(c_pos, gt - c_pos);
                std::string_view c_body = row_xml.substr(gt + 1, end_c - (gt + 1));
                bool is_shared = c_tag.find("t=\"s\"") != std::string_view::npos;
                bool is_inline = c_tag.find("t=\"inlineStr\"") != std::string_view::npos;
                std::string val;
                if (is_inline) {
                    val = extract_xml_text_runs(c_body, "t");
                } else {
                    val = extract_xml_text_runs(c_body, "v");
                    if (is_shared && !val.empty()) {
                        std::size_t idx = static_cast<std::size_t>(std::strtoul(val.c_str(), nullptr, 10));
                        if (idx < shared_strings.size()) val = shared_strings[idx];
                    }
                }
                cells.push_back(trim_ws(val));
                c_pos = end_c + 4;
            }
            if (!cells.empty()) {
                std::string line = "| ";
                for (const auto& c : cells) line += c + " | ";
                line.pop_back();
                sheet_md += line + "\n";
                if (first_row) {
                    std::string sep = "|";
                    for (std::size_t k = 0; k < cells.size(); ++k) sep += " --- |";
                    sheet_md += sep + "\n";
                    first_row = false;
                }
            }
            row_pos = end_row + 6;
        }
        doc.sections.push_back(DocumentSection{
            .index = sh.num,
            .title = sheet_title,
            .text = trim_ws(sheet_md)
        });
        full_md += sheet_md + "\n";
    }
    doc.text = trim_ws(full_md);
}

inline void parse_html_document(std::string_view html, Document& doc) {
    doc.format = "html";
    std::string cleaned;
    cleaned.reserve(html.size());

    // Strip <script>, <style>, and <!-- comments -->
    std::size_t i = 0;
    while (i < html.size()) {
        if (html.substr(i).starts_with("<!--")) {
            auto end_c = html.find("-->", i + 4);
            i = (end_c == std::string_view::npos) ? html.size() : (end_c + 3);
            continue;
        }
        if (html[i] == '<') {
            auto gt = html.find('>', i + 1);
            if (gt == std::string_view::npos) break;
            std::string tag_raw(html.substr(i + 1, gt - i - 1));
            std::string tag_lower;
            for (unsigned char c : tag_raw) {
                if (std::isspace(c) || c == '/') {
                    if (!tag_lower.empty()) break;
                    if (c == '/' && tag_lower.empty()) tag_lower.push_back('/');
                } else {
                    tag_lower.push_back(static_cast<char>(std::tolower(c)));
                }
            }
            if (tag_lower == "script" || tag_lower == "style") {
                std::string close_needle = "</" + tag_lower;
                std::size_t scan = gt + 1;
                while (scan < html.size()) {
                    if (html[scan] == '<' && scan + close_needle.size() <= html.size()) {
                        bool match = true;
                        for (std::size_t k = 0; k < close_needle.size(); ++k) {
                            if (std::tolower(static_cast<unsigned char>(html[scan + k])) != close_needle[k]) {
                                match = false;
                                break;
                            }
                        }
                        if (match) {
                            auto close_gt = html.find('>', scan);
                            scan = (close_gt == std::string_view::npos) ? html.size() : (close_gt + 1);
                            break;
                        }
                    }
                    ++scan;
                }
                i = scan;
                continue;
            }
            if (tag_lower.size() == 2 && tag_lower[0] == 'h' && tag_lower[1] >= '1' && tag_lower[1] <= '6') {
                cleaned += "\n\n" + std::string(static_cast<std::size_t>(tag_lower[1] - '0'), '#') + " ";
            } else if (tag_lower.size() == 3 && tag_lower[0] == '/' && tag_lower[1] == 'h' && tag_lower[2] >= '1' && tag_lower[2] <= '6') {
                cleaned += "\n\n";
            } else if (tag_lower == "li") {
                cleaned += "\n- ";
            } else if (tag_lower == "p" || tag_lower == "/p" || tag_lower == "br" || tag_lower == "div" || tag_lower == "/div" || tag_lower == "/tr") {
                cleaned += "\n";
            } else if (tag_lower == "td" || tag_lower == "th") {
                cleaned += " | ";
            }
            i = gt + 1;
            continue;
        }
        cleaned.push_back(html[i++]);
    }

    std::string unescaped = xml_unescape(cleaned);
    // Split into sections by Markdown headings
    std::string full_md;
    std::string cur_title = "Document";
    std::string cur_body;
    std::int32_t sec_idx = 1;
    std::size_t line_start = 0;
    while (line_start < unescaped.size()) {
        auto nl = unescaped.find('\n', line_start);
        if (nl == std::string::npos) nl = unescaped.size();
        std::string line = trim_ws(std::string_view(unescaped).substr(line_start, nl - line_start));
        line_start = nl + 1;
        if (line.empty()) continue;
        if (line.starts_with("#")) {
            if (!trim_ws(cur_body).empty()) {
                doc.sections.push_back(DocumentSection{.index = sec_idx++, .title = cur_title, .text = trim_ws(cur_body)});
                cur_body.clear();
            }
            std::size_t h_end = 0;
            while (h_end < line.size() && line[h_end] == '#') ++h_end;
            cur_title = trim_ws(std::string_view(line).substr(h_end));
        }
        cur_body += line + "\n";
        full_md += line + "\n";
    }
    if (!trim_ws(cur_body).empty()) {
        doc.sections.push_back(DocumentSection{.index = sec_idx, .title = cur_title, .text = trim_ws(cur_body)});
    }
    doc.text = trim_ws(full_md);
}

inline void parse_odf_zip(
    const std::unordered_map<std::string, std::string>& entries,
    std::string_view mime_type,
    Document& doc
) {
    if (mime_type.find("presentation") != std::string::npos) doc.format = "odp";
    else if (mime_type.find("spreadsheet") != std::string::npos) doc.format = "ods";
    else doc.format = "odt";

    auto it = entries.find("content.xml");
    if (it == entries.end()) return;
    std::string_view xml = it->second;

    if (doc.format == "odp") {
        std::size_t pos = 0;
        std::int32_t slide_idx = 1;
        std::string full_md;
        while ((pos = xml.find("<draw:page", pos)) != std::string_view::npos) {
            auto gt = xml.find('>', pos);
            if (gt == std::string_view::npos) break;
            if (xml[gt - 1] == '/') { pos = gt + 1; continue; }
            auto end_page = xml.find("</draw:page>", gt + 1);
            if (end_page == std::string_view::npos) break;
            std::string_view page_xml = xml.substr(gt + 1, end_page - (gt + 1));

            std::string slide_txt;
            std::size_t p_pos = 0;
            while ((p_pos = page_xml.find("<text:p", p_pos)) != std::string_view::npos) {
                auto p_gt = page_xml.find('>', p_pos);
                if (p_gt == std::string_view::npos) break;
                if (page_xml[p_gt - 1] == '/') { p_pos = p_gt + 1; continue; }
                auto p_end = page_xml.find("</text:p>", p_gt + 1);
                if (p_end == std::string_view::npos) break;
                std::string_view inner = page_xml.substr(p_gt + 1, p_end - (p_gt + 1));
                std::string line;
                std::size_t k = 0;
                while (k < inner.size()) {
                    if (inner[k] == '<') {
                        auto t_end = inner.find('>', k);
                        if (t_end == std::string_view::npos) break;
                        std::string_view tag = inner.substr(k + 1, t_end - k - 1);
                        if (tag.starts_with("text:s")) line.push_back(' ');
                        else if (tag.starts_with("text:tab")) line.push_back('\t');
                        else if (tag.starts_with("text:line-break")) line.push_back('\n');
                        k = t_end + 1;
                    } else {
                        line.push_back(inner[k++]);
                    }
                }
                line = trim_ws(xml_unescape(line));
                if (!line.empty()) {
                    if (!slide_txt.empty()) slide_txt += "\n";
                    slide_txt += line;
                }
                p_pos = p_end + 9;
            }
            if (!slide_txt.empty()) {
                std::string title = std::format("Slide {}", slide_idx);
                doc.sections.push_back(DocumentSection{.index = slide_idx++, .title = title, .text = slide_txt});
                if (!full_md.empty()) full_md += "\n\n";
                full_md += std::format("## {}\n{}", title, slide_txt);
            }
            pos = end_page + 12;
        }
        doc.text = trim_ws(full_md);
        return;
    }

    if (doc.format == "ods") {
        std::size_t pos = 0;
        std::int32_t sheet_idx = 1;
        std::string full_md;
        while ((pos = xml.find("<table:table", pos)) != std::string_view::npos) {
            if (xml.substr(pos).starts_with("<table:table-")) { pos += 13; continue; }
            auto gt = xml.find('>', pos);
            if (gt == std::string_view::npos) break;
            auto end_tbl = xml.find("</table:table>", gt + 1);
            if (end_tbl == std::string_view::npos) break;
            std::string_view tbl_xml = xml.substr(gt + 1, end_tbl - (gt + 1));

            std::string sheet_md;
            std::size_t r_pos = 0;
            std::size_t row_idx = 0;
            while ((r_pos = tbl_xml.find("<table:table-row", r_pos)) != std::string_view::npos) {
                auto r_gt = tbl_xml.find('>', r_pos);
                if (r_gt == std::string_view::npos) break;
                if (tbl_xml[r_gt - 1] == '/') { r_pos = r_gt + 1; continue; }
                auto r_end = tbl_xml.find("</table:table-row>", r_gt + 1);
                if (r_end == std::string_view::npos) break;
                std::string_view row_xml = tbl_xml.substr(r_gt + 1, r_end - (r_gt + 1));

                std::vector<std::string> cells;
                std::size_t c_pos = 0;
                while ((c_pos = row_xml.find("<table:table-cell", c_pos)) != std::string_view::npos) {
                    auto c_gt = row_xml.find('>', c_pos);
                    if (c_gt == std::string_view::npos) break;
                    if (row_xml[c_gt - 1] == '/') { cells.emplace_back(""); c_pos = c_gt + 1; continue; }
                    auto c_end = row_xml.find("</table:table-cell>", c_gt + 1);
                    if (c_end == std::string_view::npos) break;
                    std::string_view cell_xml = row_xml.substr(c_gt + 1, c_end - (c_gt + 1));
                    std::string cell_txt;
                    std::size_t k = 0;
                    while (k < cell_xml.size()) {
                        if (cell_xml[k] == '<') {
                            auto t_end = cell_xml.find('>', k);
                            if (t_end == std::string_view::npos) break;
                            k = t_end + 1;
                        } else {
                            cell_txt.push_back(cell_xml[k++]);
                        }
                    }
                    cells.push_back(trim_ws(xml_unescape(cell_txt)));
                    c_pos = c_end + 19;
                }
                if (!cells.empty()) {
                    sheet_md += "| ";
                    for (const auto& c : cells) sheet_md += c + " | ";
                    sheet_md.pop_back();
                    sheet_md += "\n";
                    if (row_idx == 0) {
                        sheet_md += "|";
                        for (std::size_t k = 0; k < cells.size(); ++k) sheet_md += " --- |";
                        sheet_md += "\n";
                    }
                    ++row_idx;
                }
                r_pos = r_end + 18;
            }
            sheet_md = trim_ws(sheet_md);
            if (!sheet_md.empty()) {
                std::string title = std::format("Sheet {}", sheet_idx);
                doc.sections.push_back(DocumentSection{.index = sheet_idx++, .title = title, .text = sheet_md});
                if (!full_md.empty()) full_md += "\n\n";
                full_md += std::format("## {}\n{}", title, sheet_md);
            }
            pos = end_tbl + 14;
        }
        doc.text = trim_ws(full_md);
        return;
    }

    // Default: OpenDocument Text (.odt)
    std::string full_md;
    std::string current_title = "Document";
    std::string current_body;
    std::int32_t sec_idx = 1;
    std::size_t pos = 0;
    while (pos < xml.size()) {
        auto next_h = xml.find("<text:h", pos);
        auto next_p = xml.find("<text:p", pos);
        std::size_t tag_pos = std::min(next_h, next_p);
        if (tag_pos == std::string_view::npos) break;
        bool is_heading = (tag_pos == next_h);
        std::string close_tag = is_heading ? "</text:h>" : "</text:p>";

        auto gt = xml.find('>', tag_pos);
        if (gt == std::string_view::npos) break;
        if (xml[gt - 1] == '/') { pos = gt + 1; continue; }
        auto end_el = xml.find(close_tag, gt + 1);
        if (end_el == std::string_view::npos) break;
        std::string_view inner = xml.substr(gt + 1, end_el - (gt + 1));

        std::string para;
        std::size_t k = 0;
        while (k < inner.size()) {
            if (inner[k] == '<') {
                auto t_end = inner.find('>', k);
                if (t_end == std::string_view::npos) break;
                std::string_view tag = inner.substr(k + 1, t_end - k - 1);
                if (tag.starts_with("text:s")) para.push_back(' ');
                else if (tag.starts_with("text:tab")) para.push_back('\t');
                else if (tag.starts_with("text:line-break")) para.push_back('\n');
                k = t_end + 1;
            } else {
                para.push_back(inner[k++]);
            }
        }
        para = trim_ws(xml_unescape(para));
        if (!para.empty()) {
            if (is_heading) {
                if (!trim_ws(current_body).empty()) {
                    doc.sections.push_back(DocumentSection{.index = sec_idx++, .title = current_title, .text = trim_ws(current_body)});
                    current_body.clear();
                }
                current_title = para;
                std::string h_line = "## " + para;
                current_body += h_line + "\n\n";
                full_md += h_line + "\n\n";
            } else {
                current_body += para + "\n\n";
                full_md += para + "\n\n";
            }
        }
        pos = end_el + close_tag.size();
    }
    if (!trim_ws(current_body).empty()) {
        doc.sections.push_back(DocumentSection{.index = sec_idx, .title = current_title, .text = trim_ws(current_body)});
    }
    doc.text = trim_ws(full_md);
}

inline void parse_epub_zip(const std::unordered_map<std::string, std::string>& entries, Document& doc) {
    doc.format = "epub";
    std::vector<std::string> chapter_keys;
    for (const auto& [k, _] : entries) {
        if (k.ends_with(".xhtml") || k.ends_with(".html") || k.ends_with(".htm")) {
            if (k.find("nav") == std::string::npos && k.find("toc") == std::string::npos) {
                chapter_keys.push_back(k);
            }
        }
    }
    std::sort(chapter_keys.begin(), chapter_keys.end());
    std::string full_md;
    std::int32_t ch_idx = 1;
    for (const auto& k : chapter_keys) {
        Document ch_doc;
        parse_html_document(entries.at(k), ch_doc);
        if (!ch_doc.text.empty()) {
            std::string title = (!ch_doc.sections.empty() && ch_doc.sections.front().title != "Document")
                                ? ch_doc.sections.front().title
                                : std::format("Chapter {}", ch_idx);
            doc.sections.push_back(DocumentSection{.index = ch_idx++, .title = title, .text = ch_doc.text});
            if (!full_md.empty()) full_md += "\n\n";
            full_md += ch_doc.text;
        }
    }
    doc.text = trim_ws(full_md);
}

} // namespace cfire::ai::detail

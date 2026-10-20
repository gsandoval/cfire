#include "cfire_prelude.hpp"

struct StdFnDoc;
struct StdModuleDoc;
struct DiagnosticDoc;
struct DocStruct;
struct DocEnum;
struct DocFunction;
struct DocTest;

struct StdFnDoc {
    std::string name{};
    std::string cf_sig{};
    std::string cpp_lowering{};
    std::string summary{};
    std::string example{};
};

template <>
struct cfire::reflect_struct<StdFnDoc> {
    static constexpr std::string_view name = "StdFnDoc";
    static constexpr std::size_t field_count = 5;
    static constexpr std::array<std::string_view, 5> field_names = {
        "name",
        "cf_sig",
        "cpp_lowering",
        "summary",
        "example",
    };
    static constexpr std::array<std::string_view, 5> field_tags = {
        R"tag()tag",
        R"tag()tag",
        R"tag()tag",
        R"tag()tag",
        R"tag()tag",
    };
};

struct StdModuleDoc {
    std::string import_path{};
    std::string ns_alias{};
    std::string title{};
    std::string link_flags{};
    std::string overview{};
    std::vector<StdFnDoc> functions{};
};

template <>
struct cfire::reflect_struct<StdModuleDoc> {
    static constexpr std::string_view name = "StdModuleDoc";
    static constexpr std::size_t field_count = 6;
    static constexpr std::array<std::string_view, 6> field_names = {
        "import_path",
        "ns_alias",
        "title",
        "link_flags",
        "overview",
        "functions",
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

struct DiagnosticDoc {
    std::string code{};
    std::string title{};
    std::string rule{};
    std::string bad_example{};
    std::string good_example{};
};

template <>
struct cfire::reflect_struct<DiagnosticDoc> {
    static constexpr std::string_view name = "DiagnosticDoc";
    static constexpr std::size_t field_count = 5;
    static constexpr std::array<std::string_view, 5> field_names = {
        "code",
        "title",
        "rule",
        "bad_example",
        "good_example",
    };
    static constexpr std::array<std::string_view, 5> field_tags = {
        R"tag()tag",
        R"tag()tag",
        R"tag()tag",
        R"tag()tag",
        R"tag()tag",
    };
};

struct DocStruct {
    std::string name{};
    std::string doc_comment{};
    std::string formatted_sig{};
};

template <>
struct cfire::reflect_struct<DocStruct> {
    static constexpr std::string_view name = "DocStruct";
    static constexpr std::size_t field_count = 3;
    static constexpr std::array<std::string_view, 3> field_names = {
        "name",
        "doc_comment",
        "formatted_sig",
    };
    static constexpr std::array<std::string_view, 3> field_tags = {
        R"tag()tag",
        R"tag()tag",
        R"tag()tag",
    };
};

struct DocEnum {
    std::string name{};
    std::string doc_comment{};
    std::string formatted_sig{};
};

template <>
struct cfire::reflect_struct<DocEnum> {
    static constexpr std::string_view name = "DocEnum";
    static constexpr std::size_t field_count = 3;
    static constexpr std::array<std::string_view, 3> field_names = {
        "name",
        "doc_comment",
        "formatted_sig",
    };
    static constexpr std::array<std::string_view, 3> field_tags = {
        R"tag()tag",
        R"tag()tag",
        R"tag()tag",
    };
};

struct DocFunction {
    std::string name{};
    std::string doc_comment{};
    std::string formatted_sig{};
};

template <>
struct cfire::reflect_struct<DocFunction> {
    static constexpr std::string_view name = "DocFunction";
    static constexpr std::size_t field_count = 3;
    static constexpr std::array<std::string_view, 3> field_names = {
        "name",
        "doc_comment",
        "formatted_sig",
    };
    static constexpr std::array<std::string_view, 3> field_tags = {
        R"tag()tag",
        R"tag()tag",
        R"tag()tag",
    };
};

struct DocTest {
    std::string name{};
    std::size_t line{};
};

template <>
struct cfire::reflect_struct<DocTest> {
    static constexpr std::string_view name = "DocTest";
    static constexpr std::size_t field_count = 2;
    static constexpr std::array<std::string_view, 2> field_names = {
        "name",
        "line",
    };
    static constexpr std::array<std::string_view, 2> field_tags = {
        R"tag()tag",
        R"tag()tag",
    };
};

[[nodiscard]] auto doc_usize_to_str(std::size_t val) -> std::string;
[[nodiscard]] auto html_escape(std::string_view sv) -> std::string;
[[nodiscard]] auto navbar_html(std::string_view active_page) -> std::string;
[[nodiscard]] auto generate_reference_markdown(const std::vector<StdModuleDoc>& modules, const std::vector<DiagnosticDoc>& diags, const std::vector<std::string>& examples) -> std::string;
[[nodiscard]] auto generate_reference_html(const std::vector<StdModuleDoc>& modules, const std::vector<DiagnosticDoc>& diags, std::string_view shared_css, const std::vector<std::string>& examples) -> std::string;
[[nodiscard]] auto format_package_markdown(std::string_view pkg_name, std::string_view file_name, const std::vector<std::string>& imports, const std::vector<DocStruct>& structs, const std::vector<DocEnum>& enums, const std::vector<DocFunction>& functions, const std::vector<DocTest>& tests) -> std::string;

[[nodiscard]] auto doc_usize_to_str(std::size_t val) -> std::string {
    if (val == 0) {
        return "0";
    }
    std::string digits_table = "0123456789";
    auto v = val;
    std::string digits = "";
    while (v > 0) {
        auto d = (v % 10);
        digits = (cfire::slice(digits_table, d, (d + 1)) + digits);
        v = (v / 10);
    }
    return digits;
}

[[nodiscard]] auto html_escape(std::string_view sv) -> std::string {
    std::string escaped = "";
    std::size_t start = 0;
    std::size_t i = 0;
    auto n = sv.size();
    while (i < n) {
        auto c = sv[i];
        if ((((c == 38) || (c == 60)) || (c == 62)) || (c == 34)) {
            if (i > start) {
                escaped += cfire::slice(sv, start, i);
            }
            if (c == 38) {
                escaped += "&amp;";
            } else {
                if (c == 60) {
                    escaped += "&lt;";
                } else {
                    if (c == 62) {
                        escaped += "&gt;";
                    } else {
                        if (c == 34) {
                            escaped += "&quot;";
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

[[nodiscard]] auto navbar_html(std::string_view active_page) -> std::string {
    std::string idx_class = "";
    if (active_page == "index") {
        idx_class = "active";
    }
    std::string guide_class = "";
    if (active_page == "guide") {
        guide_class = "active";
    }
    std::string ref_class = "";
    if (active_page == "reference") {
        ref_class = "active";
    }
    std::string s = "\n<nav class=\"navbar\">\n";
    s += "  <div class=\"navbar-inner\">\n";
    s += "    <a class=\"brand\" href=\"index.html\">\n";
    s += "      <img src=\"assets/cfire_logo_emblem.png\" alt=\"cfire logo\" class=\"brand-icon\" />\n";
    s += "      <span class=\"brand-text\">c<span class=\"fire\">fire</span></span>\n";
    s += "      <span class=\"brand-badge\">ISO C++23</span>\n";
    s += "    </a>\n";
    s += "    <div class=\"nav-links\">\n";
    s += "      <a href=\"index.html\" class=\"";
    s += idx_class;
    s += "\">Overview</a>\n";
    s += "      <a href=\"guide.html\" class=\"";
    s += guide_class;
    s += "\">Guide</a>\n";
    s += "      <a href=\"reference.html\" class=\"";
    s += ref_class;
    s += "\">Stdlib</a>\n";
    s += "      <a href=\"https://github.com/gsandoval/cfire\" target=\"_blank\" rel=\"noopener\">GitHub &rarr;</a>\n";
    s += "      <a href=\"index.html#install\" class=\"btn-nav-primary\">Install</a>\n";
    s += "    </div>\n";
    s += "  </div>\n";
    s += "</nav>\n";
    return s;
}

[[nodiscard]] auto generate_reference_markdown(const std::vector<StdModuleDoc>& modules, const std::vector<DiagnosticDoc>& diags, const std::vector<std::string>& examples) -> std::string {
    std::string s = "# `cfire` Standard Library (`core:*`) & Diagnostics Reference\n\n";
    s += "> Auto-generated from the compiler's single-source-of-truth catalog via `./cfire doc`.\n\n";
    s += "## 1. Built-In `core:*` Standard Library Modules\n\n";
    std::size_t mi = 0;
    auto num_mods = modules.size();
    while (mi < num_mods) {
        auto mod = modules[mi];
        s += "### `";
        s += mod.import_path;
        s += "` (`";
        s += mod.ns_alias;
        s += "`) — ";
        s += mod.title;
        s += "\n\n- **Auto-Link Flags:** `";
        s += mod.link_flags;
        s += "`\n- **Overview:** ";
        s += mod.overview;
        s += "\n\n";
        if ((mi < examples.size()) && (examples[mi].size() > 0)) {
            s += "**Practical Example:**\n```cf\n";
            s += examples[mi];
            s += "\n```\n\n";
        }
        s += "| `.cf` Signature | Lowered C++23 | Description | Example |\n";
        s += "|---|---|---|---|\n";
        std::size_t fi = 0;
        auto num_fns = mod.functions.size();
        while (fi < num_fns) {
            auto fn_doc = mod.functions[fi];
            s += "| `";
            s += fn_doc.cf_sig;
            s += "` | `";
            s += fn_doc.cpp_lowering;
            s += "` | ";
            s += fn_doc.summary;
            s += " | `";
            s += fn_doc.example;
            s += "` |\n";
            fi = (fi + 1);
        }
        s += "\n";
        mi = (mi + 1);
    }
    s += "## 2. Semantic Diagnostic Codes\n\n";
    std::size_t di = 0;
    auto num_diags = diags.size();
    while (di < num_diags) {
        auto d = diags[di];
        s += "### `[";
        s += d.code;
        s += "]` — ";
        s += d.title;
        s += "\n\n";
        s += d.rule;
        s += "\n\n**Rejected (`.cf`):**\n```cf\n";
        s += d.bad_example;
        s += "\n```\n\n**Accepted (`.cf`):**\n```cf\n";
        s += d.good_example;
        s += "\n```\n\n";
        di = (di + 1);
    }
    return s;
}

[[nodiscard]] auto generate_reference_html(const std::vector<StdModuleDoc>& modules, const std::vector<DiagnosticDoc>& diags, std::string_view shared_css, const std::vector<std::string>& examples) -> std::string {
    std::string s = "<!DOCTYPE html>\n<html lang=\"en\">\n<head>\n";
    s += "  <meta charset=\"UTF-8\" />\n";
    s += "  <meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\" />\n";
    s += "  <title>Stdlib &amp; Diagnostics Reference — cfire</title>\n";
    s += "  <style>";
    s += shared_css;
    s += "</style>\n";
    s += "</head>\n<body>\n";
    s += navbar_html("reference");
    s += "<div class=\"container\">\n";
    s += "  <div class=\"doc-layout\">\n";
    s += "    <aside class=\"sidebar\">\n";
    s += "      <details class=\"sidebar-details\" open>\n";
    s += "        <summary class=\"sidebar-summary\">\n";
    s += "          <span>Modules &amp; Diagnostics</span>\n";
    s += "          <span class=\"sidebar-chevron\">&#9662;</span>\n";
    s += "        </summary>\n";
    s += "        <div class=\"sidebar-content\">\n";
    s += "          <h4>`core:*` Modules</h4>\n          <ul>\n";
    std::size_t mi = 0;
    auto num_mods = modules.size();
    while (mi < num_mods) {
        auto mod = modules[mi];
        s += "            <li><a href=\"#mod-";
        s += mod.ns_alias;
        s += "\"><code>";
        s += mod.import_path;
        s += "</code></a></li>\n";
        mi = (mi + 1);
    }
    s += "          </ul>\n          <h4 style=\"margin-top:1.25rem;\">Diagnostics</h4>\n          <ul>\n";
    std::size_t di = 0;
    auto num_diags = diags.size();
    while (di < num_diags) {
        auto d = diags[di];
        s += "            <li><a href=\"#diag-";
        s += d.code;
        s += "\"><code>";
        s += d.code;
        s += "</code></a></li>\n";
        di = (di + 1);
    }
    s += "          </ul>\n        </div>\n      </details>\n    </aside>\n    <main>\n";
    s += "      <h1>`core:*` Standard Library &amp; Diagnostics Reference</h1>\n";
    s += "      <p class=\"subtitle\" style=\"margin:0 0 2rem;\">Automatically generated from the live <code>cfire</code> compiler metadata catalog and verified by <code>cfire doc --check</code>.</p>\n";
    std::size_t m2 = 0;
    while (m2 < num_mods) {
        auto mod = modules[m2];
        s += "      <h2 id=\"mod-";
        s += mod.ns_alias;
        s += "\"><code>import \"";
        s += mod.import_path;
        s += "\"</code> &mdash; ";
        s += html_escape(mod.title);
        s += "</h2>\n";
        s += "      <p>";
        s += mod.overview;
        s += " <br/><small style=\"color:var(--muted)\">Auto-linked flags: <code>";
        s += html_escape(mod.link_flags);
        s += "</code></small></p>\n";
        if ((m2 < examples.size()) && (examples[m2].size() > 0)) {
            s += "      <div class=\"code-header\"><span class=\"tag-cf\">Practical Workflow Example</span></div>\n";
            s += "      <pre><code>";
            s += html_escape(examples[m2]);
            s += "</code></pre>\n";
        }
        s += "      <div class=\"table-wrap api-table-wrap\"><table>\n        <thead><tr><th>`.cf` Signature</th><th>C++23 Lowering</th><th>Description &amp; Example</th></tr></thead>\n        <tbody>\n";
        std::size_t fi = 0;
        auto num_fns = mod.functions.size();
        while (fi < num_fns) {
            auto fn_doc = mod.functions[fi];
            s += "          <tr><td><code>";
            s += html_escape(fn_doc.cf_sig);
            s += "</code></td><td><code>";
            s += html_escape(fn_doc.cpp_lowering);
            s += "</code></td><td>";
            s += html_escape(fn_doc.summary);
            s += "<br/><code>";
            s += html_escape(fn_doc.example);
            s += "</code></td></tr>\n";
            fi = (fi + 1);
        }
        s += "        </tbody>\n      </table></div>\n";
        m2 = (m2 + 1);
    }
    s += "      <h2 class=\"section-title\">Compiler Semantic Diagnostics</h2>\n";
    std::size_t d2 = 0;
    while (d2 < num_diags) {
        auto d = diags[d2];
        s += "      <div class=\"card\" id=\"diag-";
        s += d.code;
        s += "\" style=\"margin-bottom:1.25rem;\">\n";
        s += "        <h3><code>[";
        s += d.code;
        s += "]</code> &mdash; ";
        s += html_escape(d.title);
        s += "</h3>\n";
        s += "        <p style=\"margin-bottom:0.85rem;\">";
        s += html_escape(d.rule);
        s += "</p>\n";
        s += "        <div class=\"grid-2\">\n";
        s += "          <div><div class=\"code-header\"><span style=\"color:var(--err);font-weight:700;\">Rejected (.cf)</span></div><pre><code>";
        s += html_escape(d.bad_example);
        s += "</code></pre></div>\n";
        s += "          <div><div class=\"code-header\"><span style=\"color:var(--ok);font-weight:700;\">Accepted (.cf)</span></div><pre><code>";
        s += html_escape(d.good_example);
        s += "</code></pre></div>\n";
        s += "        </div>\n      </div>\n";
        d2 = (d2 + 1);
    }
    s += "    </main>\n  </div>\n</div>\n";
    s += "<footer class=\"footer\">Generated by <code>cfire doc</code> &bull; Always kept in sync with <code>src/docgen.hpp</code>.</footer>\n";
    s += "</body>\n</html>\n";
    return s;
}

[[nodiscard]] auto format_package_markdown(std::string_view pkg_name, std::string_view file_name, const std::vector<std::string>& imports, const std::vector<DocStruct>& structs, const std::vector<DocEnum>& enums, const std::vector<DocFunction>& functions, const std::vector<DocTest>& tests) -> std::string {
    std::string md = "# Package `";
    md += pkg_name;
    md += "` (`";
    md += file_name;
    md += "`)\n\n";
    if (imports.size() > 0) {
        md += "## Imports\n";
        std::size_t i = 0;
        while (i < imports.size()) {
            md += "- `\"";
            md += imports[i];
            md += "\"`\n";
            i = (i + 1);
        }
        md += "\n";
    }
    if (structs.size() > 0) {
        md += "## Structs\n\n";
        std::size_t i = 0;
        while (i < structs.size()) {
            auto st = structs[i];
            md += "### `type ";
            md += st.name;
            md += " struct`\n";
            if (st.doc_comment.size() > 0) {
                md += st.doc_comment;
                md += "\n\n";
            }
            md += "```cf\n";
            md += st.formatted_sig;
            md += "\n```\n\n";
            i = (i + 1);
        }
    }
    if (enums.size() > 0) {
        md += "## Algebraic Enums\n\n";
        std::size_t i = 0;
        while (i < enums.size()) {
            auto en = enums[i];
            md += "### `type ";
            md += en.name;
            md += " enum`\n";
            if (en.doc_comment.size() > 0) {
                md += en.doc_comment;
                md += "\n\n";
            }
            md += "```cf\n";
            md += en.formatted_sig;
            md += "\n```\n\n";
            i = (i + 1);
        }
    }
    if (functions.size() > 0) {
        md += "## Functions\n\n";
        std::size_t i = 0;
        while (i < functions.size()) {
            auto fn_doc = functions[i];
            md += "### `";
            md += fn_doc.formatted_sig;
            md += "`\n";
            if (fn_doc.doc_comment.size() > 0) {
                md += fn_doc.doc_comment;
                md += "\n\n";
            }
            i = (i + 1);
        }
    }
    if (tests.size() > 0) {
        md += "## Built-in Tests\n\n";
        std::size_t i = 0;
        while (i < tests.size()) {
            auto t = tests[i];
            md += "- `test \"";
            md += t.name;
            md += "\"` (line ";
            md += doc_usize_to_str(t.line);
            md += ")\n";
            i = (i + 1);
        }
        md += "\n";
    }
    return md;
}

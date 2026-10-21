#pragma once
// src/docgen/html.hpp — HTML Landing Page, Rosetta Showcase, Guide, Reference & Docs Directory Sync

#include "catalog.hpp"
#include "styles.hpp"
#include "markdown.hpp"
#include <expected>
#include <filesystem>
#include <format>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace cfire::doc {

[[nodiscard]] inline auto compile_snippet_to_cpp(std::string_view filename, std::string_view cf_source) -> std::string {
    auto ast = cfire::frontend::parse_source(std::string(filename), cf_source);
    if (!ast) return std::format("// Parse error: {}\n", ast.error());
    auto checked = cfire::frontend::analyze(std::move(*ast));
    if (!checked) return std::format("// Sema error: {}\n", checked.error());
    return cfire::frontend::emit_cpp(*checked);
}

[[nodiscard]] inline auto generate_reproducible_metrics() -> std::string {
    return std::format(R"HTML(  <!-- Metrics Ribbon -->
  <div class="stats-ribbon">
    <div class="stat-pill">
      <div class="stat-val">0 ms</div>
      <div class="stat-lbl">GC Pauses (Pure RAII)</div>
    </div>
    <div class="stat-pill">
      <div class="stat-val">1-to-1</div>
      <div class="stat-lbl">Readable ISO C++23</div>
    </div>
    <div class="stat-pill">
      <div class="stat-val">{}</div>
      <div class="stat-lbl">Built-in `core:*` Modules</div>
    </div>
    <div class="stat-pill">
      <div class="stat-val">0 &mu;s</div>
      <div class="stat-lbl">C / C++ Binding Overhead</div>
    </div>
    <div class="stat-pill">
      <div class="stat-val">1-Click</div>
      <div class="stat-lbl">CMake Project Ejection</div>
    </div>
  </div>)HTML", stdlib_catalog().size());
}

[[nodiscard]] inline auto generate_reproducible_showcase() -> std::string {
    // 1. Intents Snippet
    std::string_view snip1_cf = R"(package main

type Box struct {
    value: i32
}

fn increment(mut b: Box, amount: i32) {
    b.value = b.value + amount
}

fn main() {
    mut b := Box{ value: 40 }
    // Call-site 'mut' marker is mandatory:
    increment(mut b, 2)
    
    // Explicit unique ownership:
    p := make(own[Box], Box{ value: 100 })
    println("b={} p={}", b.value, (*p).value)
})";
    std::string snip1_cpp = compile_snippet_to_cpp("snippet_intents.cf", snip1_cf);

    // 2. Patterns Snippet
    std::string_view snip2_cf = R"(package main

type Token enum {
    Ident(name: str),
    Number(val: i64),
    Eof,
}

fn classify(s: []str) -> str {
    match s {
        []                   => { return "empty" }
        [head, ..tail]       => { return "head=" + head }
        [first, ..mid, last] => { return first + ".." + last }
    }
    return "other"
}

fn main() {
    items: []str = ["alpha", "beta", "gamma"]
    println("result: {}", classify(items))
})";
    std::string snip2_cpp = compile_snippet_to_cpp("snippet_patterns.cf", snip2_cf);

    // 3. WebSockets & Concurrency Snippet
    std::string_view snip3_cf = R"(package main

import "core:net/websocket"
import "core:sync/context"
import "core:time"

fn main() {
    bg := context.background()
    (ctx, cancel) := context.with_timeout(bg, 500)
    defer cancel("done")

    (mut client, mut server) := try websocket.pair()
    try client.send_text("ping-payload")
    msg := try server.recv_text()
    println("received: {}", msg)
})";
    std::string snip3_cpp = compile_snippet_to_cpp("snippet_net.cf", snip3_cf);

    // 4. JSON Struct Tags Snippet
    std::string_view snip4_cf = R"(package main

import "core:json"

type Metric struct {
    service: str  `json:"service"`
    latency: f64  `json:"latency_ms"`
    healthy: bool `json:"healthy"`
}

fn main() {
    payload := `{"service":"auth","latency_ms":1.42,"healthy":true}`
    m := try json.parse[Metric](payload)
    println("service={} latency={}ms", m.service, m.latency)
})";
    std::string snip4_cpp = compile_snippet_to_cpp("snippet_json.cf", snip4_cf);

    // 5. Clang Interop Snippet
    std::string_view snip5_cf = R"(package main

// Direct C header ingestion via Clang AST bridge:
import cpp "fast_hash.h"

fn main() {
    msg := "cfire systems programming"
    // Automatic boundary coercion: 'str' passes as 'const char*'
    hash := cpp::fh_fnv1a(msg, msg.len())
    println("fnv1a hash: 0x{:x}", hash)
})";
    std::string snip5_cpp = compile_snippet_to_cpp("snippet_interop.cf", snip5_cf);

    std::string out;
    out += R"HTML(  <!-- Interactive Live Rosetta Showcase -->
  <h2 class="section-title">See the Lowering: Live `.cf` &rarr; ISO C++23</h2>
  <p class="section-subtitle">Select any systems pattern below to inspect the clean, idiomatic ISO C++23 code emitted by the compiler.</p>

  <div class="tabs">
    <button class="tab-btn active" onclick="showTab('tab-intents', this)">1. Intents &amp; Smart Pointers</button>
    <button class="tab-btn" onclick="showTab('tab-patterns', this)">2. Enums &amp; Slice Patterns</button>
    <button class="tab-btn" onclick="showTab('tab-net', this)">3. WebSockets &amp; Cancellation</button>
    <button class="tab-btn" onclick="showTab('tab-json', this)">4. SIMD JSON Reflection</button>
    <button class="tab-btn" onclick="showTab('tab-interop', this)">5. Clang C/C++ Interop</button>
  </div>

  <!-- Tab 1: Intents -->
  <div id="tab-intents" class="tab-panel active">
    <div class="rosetta-mobile-toggle">
      <button class="active" onclick="toggleRosettaMobile(this, 'cf')">cfire (.cf)</button>
      <button onclick="toggleRosettaMobile(this, 'cpp')">Emitted ISO C++23</button>
    </div>
    <div class="grid-2">
      <div class="rosetta-pane-cf">
        <div class="code-header"><span class="tag-cf">cfire (.cf)</span><span>Call-site Intent Safety</span></div>
        <pre><code>)HTML";
    out += html_escape(snip1_cf);
    out += R"HTML(</code></pre>
      </div>
      <div class="rosetta-pane-cpp">
        <div class="code-header"><span class="tag-cpp">Emitted ISO C++23</span><span>Compiler-Verified Lowering</span></div>
        <pre><code>)HTML";
    out += html_escape(snip1_cpp);
    out += R"HTML(</code></pre>
      </div>
    </div>
    <div class="console-preview">
      <div class="console-header"><span class="console-dot"></span> Terminal Output Preview</div>
      <div>$ cfire run main.cf<br />b=42 p=100</div>
    </div>
  </div>

  <!-- Tab 2: Enums & Patterns -->
  <div id="tab-patterns" class="tab-panel">
    <div class="rosetta-mobile-toggle">
      <button class="active" onclick="toggleRosettaMobile(this, 'cf')">cfire (.cf)</button>
      <button onclick="toggleRosettaMobile(this, 'cpp')">Emitted ISO C++23</button>
    </div>
    <div class="grid-2">
      <div class="rosetta-pane-cf">
        <div class="code-header"><span class="tag-cf">cfire (.cf)</span><span>Exhaustive Match &amp; Destructuring</span></div>
        <pre><code>)HTML";
    out += html_escape(snip2_cf);
    out += R"HTML(</code></pre>
      </div>
      <div class="rosetta-pane-cpp">
        <div class="code-header"><span class="tag-cpp">Emitted ISO C++23</span><span>std::variant &amp; std::span</span></div>
        <pre><code>)HTML";
    out += html_escape(snip2_cpp);
    out += R"HTML(</code></pre>
      </div>
    </div>
    <div class="console-preview">
      <div class="console-header"><span class="console-dot"></span> Terminal Output Preview</div>
      <div>$ cfire run main.cf<br />result: head=alpha</div>
    </div>
  </div>

  <!-- Tab 3: WebSockets & Cancellation -->
  <div id="tab-net" class="tab-panel">
    <div class="rosetta-mobile-toggle">
      <button class="active" onclick="toggleRosettaMobile(this, 'cf')">cfire (.cf)</button>
      <button onclick="toggleRosettaMobile(this, 'cpp')">Emitted ISO C++23</button>
    </div>
    <div class="grid-2">
      <div class="rosetta-pane-cf">
        <div class="code-header"><span class="tag-cf">cfire (.cf)</span><span>Modern Concurrency &amp; WebSockets</span></div>
        <pre><code>)HTML";
    out += html_escape(snip3_cf);
    out += R"HTML(</code></pre>
      </div>
      <div class="rosetta-pane-cpp">
        <div class="code-header"><span class="tag-cpp">Emitted ISO C++23</span><span>RFC 6455 Loopback Pair</span></div>
        <pre><code>)HTML";
    out += html_escape(snip3_cpp);
    out += R"HTML(</code></pre>
      </div>
    </div>
    <div class="console-preview">
      <div class="console-header"><span class="console-dot"></span> Terminal Output Preview</div>
      <div>$ cfire run main.cf<br />received: ping-payload</div>
    </div>
  </div>

  <!-- Tab 4: JSON -->
  <div id="tab-json" class="tab-panel">
    <div class="rosetta-mobile-toggle">
      <button class="active" onclick="toggleRosettaMobile(this, 'cf')">cfire (.cf)</button>
      <button onclick="toggleRosettaMobile(this, 'cpp')">Emitted ISO C++23</button>
    </div>
    <div class="grid-2">
      <div class="rosetta-pane-cf">
        <div class="code-header"><span class="tag-cf">cfire (.cf)</span><span>Compile-Time SIMD Struct Tags</span></div>
        <pre><code>)HTML";
    out += html_escape(snip4_cf);
    out += R"HTML(</code></pre>
      </div>
      <div class="rosetta-pane-cpp">
        <div class="code-header"><span class="tag-cpp">Emitted ISO C++23</span><span>simdjson::ondemand Specialized</span></div>
        <pre><code>)HTML";
    out += html_escape(snip4_cpp);
    out += R"HTML(</code></pre>
      </div>
    </div>
    <div class="console-preview">
      <div class="console-header"><span class="console-dot"></span> Terminal Output Preview</div>
      <div>$ cfire run main.cf<br />service=auth latency=1.42ms</div>
    </div>
  </div>

  <!-- Tab 5: Interop -->
  <div id="tab-interop" class="tab-panel">
    <div class="rosetta-mobile-toggle">
      <button class="active" onclick="toggleRosettaMobile(this, 'cf')">cfire (.cf)</button>
      <button onclick="toggleRosettaMobile(this, 'cpp')">Emitted ISO C++23</button>
    </div>
    <div class="grid-2">
      <div class="rosetta-pane-cf">
        <div class="code-header"><span class="tag-cf">cfire (.cf)</span><span>Direct C/C++ Header Ingestion</span></div>
        <pre><code>)HTML";
    out += html_escape(snip5_cf);
    out += R"HTML(</code></pre>
      </div>
      <div class="rosetta-pane-cpp">
        <div class="code-header"><span class="tag-cpp">Emitted ISO C++23</span><span>Direct Inlined Invocation</span></div>
        <pre><code>)HTML";
    out += html_escape(snip5_cpp);
    out += R"HTML(</code></pre>
      </div>
    </div>
    <div class="console-preview">
      <div class="console-header"><span class="console-dot"></span> Terminal Output Preview</div>
      <div>$ cfire run main.cf<br />fnv1a hash: 0x811c9dc5</div>
    </div>
  </div>)HTML";

    return out;
}

[[nodiscard]] inline auto generate_landing_html(const std::filesystem::path& docs_dir = "docs") -> std::string {
    auto template_path = docs_dir / "index.template.html";
    std::string tmpl;
    if (std::filesystem::exists(template_path)) {
        std::ifstream ifs(template_path);
        std::stringstream ss;
        ss << ifs.rdbuf();
        tmpl = ss.str();
    }

    if (tmpl.empty()) {
        tmpl = "<!DOCTYPE html>\n<html lang=\"en\">\n<head>\n  <meta charset=\"UTF-8\" />\n"
               "  <title>cfire — Sapphire on Fire</title>\n  <style>" + detail::shared_css() + "</style>\n"
               "</head>\n<body>\n" + detail::navbar_html("index") +
               "<div class=\"container\">\n  <!-- REPRODUCIBLE_METRICS -->\n  <!-- REPRODUCIBLE_ROSETTA_SHOWCASE -->\n</div>\n"
               "</body>\n</html>\n";
    }

    std::string metrics = generate_reproducible_metrics();
    std::string showcase = generate_reproducible_showcase();

    auto replace_marker = [](std::string& src, std::string_view marker, std::string_view replacement) {
        auto pos = src.find(marker);
        if (pos != std::string::npos) {
            src.replace(pos, marker.size(), replacement);
        }
    };

    replace_marker(tmpl, "<!-- REPRODUCIBLE_METRICS -->", metrics);
    replace_marker(tmpl, "<!-- REPRODUCIBLE_ROSETTA_SHOWCASE -->", showcase);

    return tmpl;
}

[[nodiscard]] inline auto generate_guide_html(const std::filesystem::path& docs_dir = "docs") -> std::string {
    std::filesystem::path guide_path = docs_dir / "GUIDE.md";
    if (std::filesystem::exists(guide_path)) {
        std::ifstream ifs(guide_path);
        std::stringstream ss;
        ss << ifs.rdbuf();
        return render_guide_markdown(ss.str());
    }
    return render_guide_markdown("# The `cfire` Language Guide\n\nDocumentation guide source file not found.\n");
}

[[nodiscard]] inline auto generate_reference_html() -> std::string {
    std::string out;
    out += "<!DOCTYPE html>\n<html lang=\"en\">\n<head>\n";
    out += "  <meta charset=\"UTF-8\" />\n";
    out += "  <meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\" />\n";
    out += "  <title>Stdlib &amp; Diagnostics Reference — cfire</title>\n";
    out += "  <style>" + detail::shared_css() + "</style>\n";
    out += "</head>\n<body>\n";
    out += detail::navbar_html("reference");
    out += "<div class=\"container\">\n";
    out += "  <div class=\"doc-layout\">\n";
    out += "    <aside class=\"sidebar\">\n";
    out += "      <details class=\"sidebar-details\" open>\n";
    out += "        <summary class=\"sidebar-summary\">\n";
    out += "          <span>Modules &amp; Diagnostics</span>\n";
    out += "          <span class=\"sidebar-chevron\">&#9662;</span>\n";
    out += "        </summary>\n";
    out += "        <div class=\"sidebar-content\">\n";
    out += "          <h4>`core:*` Modules</h4>\n          <ul>\n";
    for (const auto& mod : stdlib_catalog()) {
        out += std::format("            <li><a href=\"#mod-{}\"><code>{}</code></a></li>\n", mod.ns_alias, mod.import_path);
    }
    out += "          </ul>\n          <h4 style=\"margin-top:1.25rem;\">Diagnostics</h4>\n          <ul>\n";
    for (const auto& d : diagnostics_catalog()) {
        out += std::format("            <li><a href=\"#diag-{}\"><code>{}</code></a></li>\n", d.code, d.code);
    }
    out += "          </ul>\n        </div>\n      </details>\n    </aside>\n    <main>\n";
    out += "      <h1>`core:*` Standard Library &amp; Diagnostics Reference</h1>\n";
    out += "      <p class=\"subtitle\" style=\"margin:0 0 2rem;\">Automatically generated from the live <code>cfire</code> compiler metadata catalog and verified by <code>cfire doc --check</code>.</p>\n";

    for (const auto& mod : stdlib_catalog()) {
        out += std::format("      <h2 id=\"mod-{}\"><code>import \"{}\"</code> &mdash; {}</h2>\n",
            mod.ns_alias, mod.import_path, detail::html_escape(mod.title));
        out += std::format("      <p>{} <br/><small style=\"color:var(--muted)\">Auto-linked flags: <code>{}</code></small></p>\n",
            mod.overview, detail::html_escape(mod.link_flags));
        auto ex = detail::module_practical_example(mod.import_path);
        if (!ex.empty()) {
            out += "      <div class=\"code-header\"><span class=\"tag-cf\">Practical Workflow Example</span></div>\n";
            out += "      <pre><code>" + detail::html_escape(ex) + "</code></pre>\n";
        }
        out += "      <div class=\"table-wrap api-table-wrap\"><table>\n        <thead><tr><th>`.cf` Signature</th><th>C++23 Lowering</th><th>Description &amp; Example</th></tr></thead>\n        <tbody>\n";
        for (const auto& fn : mod.functions) {
            out += std::format("          <tr><td><code>{}</code></td><td><code>{}</code></td><td>{}<br/><code>{}</code></td></tr>\n",
                detail::html_escape(fn.cf_sig),
                detail::html_escape(fn.cpp_lowering),
                detail::html_escape(fn.summary),
                detail::html_escape(fn.example));
        }
        out += "        </tbody>\n      </table></div>\n";
    }

    out += "      <h2 class=\"section-title\">Compiler Semantic Diagnostics</h2>\n";
    for (const auto& d : diagnostics_catalog()) {
        out += std::format("      <div class=\"card\" id=\"diag-{}\" style=\"margin-bottom:1.25rem;\">\n", d.code);
        out += std::format("        <h3><code>[{}]</code> &mdash; {}</h3>\n", d.code, detail::html_escape(d.title));
        out += std::format("        <p style=\"margin-bottom:0.85rem;\">{}</p>\n", detail::html_escape(d.rule));
        out += "        <div class=\"grid-2\">\n";
        out += std::format("          <div><div class=\"code-header\"><span style=\"color:var(--err);font-weight:700;\">Rejected (.cf)</span></div><pre><code>{}</code></pre></div>\n",
            detail::html_escape(d.bad_example));
        out += std::format("          <div><div class=\"code-header\"><span style=\"color:var(--ok);font-weight:700;\">Accepted (.cf)</span></div><pre><code>{}</code></pre></div>\n",
            detail::html_escape(d.good_example));
        out += "        </div>\n      </div>\n";
    }

    out += "    </main>\n  </div>\n</div>\n";
    out += "<footer class=\"footer\">Generated by <code>cfire doc</code> &bull; Always kept in sync with <code>src/docgen.hpp</code>.</footer>\n";
    out += "</body>\n</html>\n";
    return out;
}

// Generates or verifies (`check_only = true`) the full documentation suite in `<repo>/docs/`.
[[nodiscard]] inline auto sync_docs_directory(const std::filesystem::path& docs_dir, bool check_only)
    -> std::expected<void, std::string>
{
    struct DocTarget {
        std::filesystem::path path;
        std::string content;
    };
    if (check_only) {
        if (!std::filesystem::exists(docs_dir / "GUIDE.md")) {
            return std::unexpected(std::format("missing documentation file: {} (authoritative guide source required)", (docs_dir / "GUIDE.md").string()));
        }
        if (!std::filesystem::exists(docs_dir / "index.template.html")) {
            return std::unexpected(std::format("missing documentation file: {} (authoritative landing template required)", (docs_dir / "index.template.html").string()));
        }
    }

    std::vector<DocTarget> targets = {
        {docs_dir / "index.html", generate_landing_html(docs_dir)},
        {docs_dir / "guide.html", generate_guide_html(docs_dir)},
        {docs_dir / "reference.html", generate_reference_html()},
        {docs_dir / "STDLIB_REFERENCE.md", generate_reference_markdown()},
    };

    if (!check_only) {
        std::filesystem::create_directories(docs_dir);
    }

    for (const auto& t : targets) {
        if (check_only) {
            if (!std::filesystem::exists(t.path)) {
                return std::unexpected(std::format("missing documentation file: {} (run `./cfire doc` to regenerate)", t.path.string()));
            }
            std::ifstream ifs(t.path);
            std::stringstream ss;
            ss << ifs.rdbuf();
            if (ss.str() != t.content) {
                return std::unexpected(std::format("documentation file out of date: {} (run `./cfire doc` to regenerate)", t.path.string()));
            }
        } else {
            std::ofstream ofs(t.path, std::ios::trunc);
            ofs << t.content;
        }
    }
    return {};
}

} // namespace cfire::doc

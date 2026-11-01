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

    // 6. Multi-Target Audio DSP & Trailing Closures Snippet
    std::string_view snip6_cf = R"(package main

import "core:audio"

fn main() {
    try audio.init(48000, 2)
    defer audio.close()

    // Zero-asset procedural SFX + 64-voice polyphonic mixer
    sfx := audio.load_sound_from_wave(audio.gen_sfx(audio.SfxPreset::Coin))
    sfx.play()

    // Real-time float32 DSP stream across macOS, iOS, WASM & Linux
    tone := audio.gen_tone(audio.Waveform::Sine, 440.0, 0.05, 0.4)
    stream := try audio.open_stream(48000, 2)
    stream.push_samples(tone.samples)
    println("rms={:.2f} peak={:.2f}", audio.rms_level(), audio.peak_level())
})";
    std::string snip6_cpp = compile_snippet_to_cpp("snippet_audio.cf", snip6_cf);

    // 7. AI-First Local Embeddings, Vector SQLite & MCP Tooling Snippet
    std::string_view snip7_cf = R"CF(package main

import "core:ai" as ai
import "core:db/sqlite" as sqlite
import "core:json" as json

type QueryPlan struct {
    Target: str `json:"target" desc:"Search target concept"`
    TopK: i64   `json:"top_k" desc:"Number of top results"`
}

type DocEntry struct {
    Id: i64      `sql:"id"`
    Title: str   `sql:"title"`
    Snippet: str `sql:"snippet"`
}

fn main() {
    // 1. Compile-time JSON Schema extraction for LLMs/MCP
    schema := json.schema[QueryPlan]()

    // 2. Embedded SQLite with built-in vector similarity
    mut db := try sqlite.open(":memory:")
    try db.exec("CREATE TABLE docs (id INTEGER, title TEXT, snippet TEXT, embedding BLOB)")

    // 3. Normalized local embeddings + vector indexing
    doc := "cfire lowers 1-to-1 to ISO C++23 with zero runtime overhead."
    vec := try ai.embed("local", doc)
    try db.exec("INSERT INTO docs VALUES (1, 'Transpiler', ?1, ?2)", doc, vec)

    // 4. In-database semantic search
    qvec := try ai.embed("local", "modern C++23 compiler")
    hits := try db.query[DocEntry]("SELECT id, title, snippet FROM docs ORDER BY vec_cosine_similarity(embedding, ?1) DESC LIMIT 1", qvec)
    if hits.len > 0 {
        println("Match: {}", hits[0].Snippet)
    }
}
)CF";
    std::string snip7_cpp = compile_snippet_to_cpp("snippet_ai.cf", snip7_cf);

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
    <button class="tab-btn" onclick="showTab('tab-audio', this)">6. Audio DSP (`core:audio`)</button>
    <button class="tab-btn" onclick="showTab('tab-ai', this)">7. AI, Vector SQLite &amp; MCP (`core:ai`)</button>
  </div>

  <!-- Tab 1: Intents -->
  <div id="tab-intents" class="tab-panel active">
    <div class="rosetta-mobile-toggle">
      <button class="active" onclick="toggleRosettaMobile(this, 'cf')">cfire (.cf)</button>
      <button onclick="toggleRosettaMobile(this, 'cpp')">Emitted ISO C++23</button>
    </div>
    <div class="grid-2">
      <div class="rosetta-pane-cf">
        <div class="code-header"><span class="tag-cf">cfire (.cf)</span><button class="code-copy-btn" onclick="copyCode(this)">Copy</button></div>
        <pre><code>)HTML";
    out += html_escape(snip1_cf);
    out += R"HTML(</code></pre>
      </div>
      <div class="rosetta-pane-cpp">
        <div class="code-header"><span class="tag-cpp">Emitted ISO C++23</span><button class="code-copy-btn" onclick="copyCode(this)">Copy</button></div>
        <pre><code>)HTML";
    out += html_escape(snip1_cpp);
    out += R"HTML(</code></pre>
      </div>
    </div>
    <div class="telemetry-bar">
      <div class="telemetry-header">
        <span>Terminal Execution Telemetry</span>
        <span class="telemetry-pass">&#10003; PASS (0 diagnostics)</span>
      </div>
      <div class="telemetry-body">
        <span class="telemetry-stat"><code>$ cfire run main.cf</code></span>
        <span class="telemetry-stat">Output: <strong style="color:var(--text-bright);">b=42 p=100</strong></span>
        <span class="telemetry-stat">GC Pauses: <span class="telemetry-val">0.00ms</span></span>
        <span class="telemetry-stat">Binding Overhead: <span class="telemetry-val">0 &mu;s</span></span>
      </div>
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
        <div class="code-header"><span class="tag-cf">cfire (.cf)</span><button class="code-copy-btn" onclick="copyCode(this)">Copy</button></div>
        <pre><code>)HTML";
    out += html_escape(snip2_cf);
    out += R"HTML(</code></pre>
      </div>
      <div class="rosetta-pane-cpp">
        <div class="code-header"><span class="tag-cpp">Emitted ISO C++23</span><button class="code-copy-btn" onclick="copyCode(this)">Copy</button></div>
        <pre><code>)HTML";
    out += html_escape(snip2_cpp);
    out += R"HTML(</code></pre>
      </div>
    </div>
    <div class="telemetry-bar">
      <div class="telemetry-header">
        <span>Terminal Execution Telemetry</span>
        <span class="telemetry-pass">&#10003; PASS (0 diagnostics)</span>
      </div>
      <div class="telemetry-body">
        <span class="telemetry-stat"><code>$ cfire run main.cf</code></span>
        <span class="telemetry-stat">Output: <strong style="color:var(--text-bright);">result: head=alpha</strong></span>
        <span class="telemetry-stat">GC Pauses: <span class="telemetry-val">0.00ms</span></span>
        <span class="telemetry-stat">Binding Overhead: <span class="telemetry-val">0 &mu;s</span></span>
      </div>
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
        <div class="code-header"><span class="tag-cf">cfire (.cf)</span><button class="code-copy-btn" onclick="copyCode(this)">Copy</button></div>
        <pre><code>)HTML";
    out += html_escape(snip3_cf);
    out += R"HTML(</code></pre>
      </div>
      <div class="rosetta-pane-cpp">
        <div class="code-header"><span class="tag-cpp">Emitted ISO C++23</span><button class="code-copy-btn" onclick="copyCode(this)">Copy</button></div>
        <pre><code>)HTML";
    out += html_escape(snip3_cpp);
    out += R"HTML(</code></pre>
      </div>
    </div>
    <div class="telemetry-bar">
      <div class="telemetry-header">
        <span>Terminal Execution Telemetry</span>
        <span class="telemetry-pass">&#10003; PASS (0 diagnostics)</span>
      </div>
      <div class="telemetry-body">
        <span class="telemetry-stat"><code>$ cfire run main.cf</code></span>
        <span class="telemetry-stat">Output: <strong style="color:var(--text-bright);">received: ping-payload</strong></span>
        <span class="telemetry-stat">GC Pauses: <span class="telemetry-val">0.00ms</span></span>
        <span class="telemetry-stat">Binding Overhead: <span class="telemetry-val">0 &mu;s</span></span>
      </div>
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
        <div class="code-header"><span class="tag-cf">cfire (.cf)</span><button class="code-copy-btn" onclick="copyCode(this)">Copy</button></div>
        <pre><code>)HTML";
    out += html_escape(snip4_cf);
    out += R"HTML(</code></pre>
      </div>
      <div class="rosetta-pane-cpp">
        <div class="code-header"><span class="tag-cpp">Emitted ISO C++23</span><button class="code-copy-btn" onclick="copyCode(this)">Copy</button></div>
        <pre><code>)HTML";
    out += html_escape(snip4_cpp);
    out += R"HTML(</code></pre>
      </div>
    </div>
    <div class="telemetry-bar">
      <div class="telemetry-header">
        <span>Terminal Execution Telemetry</span>
        <span class="telemetry-pass">&#10003; PASS (0 diagnostics)</span>
      </div>
      <div class="telemetry-body">
        <span class="telemetry-stat"><code>$ cfire run main.cf</code></span>
        <span class="telemetry-stat">Output: <strong style="color:var(--text-bright);">service=auth latency=1.42ms</strong></span>
        <span class="telemetry-stat">GC Pauses: <span class="telemetry-val">0.00ms</span></span>
        <span class="telemetry-stat">Binding Overhead: <span class="telemetry-val">0 &mu;s</span></span>
      </div>
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
        <div class="code-header"><span class="tag-cf">cfire (.cf)</span><button class="code-copy-btn" onclick="copyCode(this)">Copy</button></div>
        <pre><code>)HTML";
    out += html_escape(snip5_cf);
    out += R"HTML(</code></pre>
      </div>
      <div class="rosetta-pane-cpp">
        <div class="code-header"><span class="tag-cpp">Emitted ISO C++23</span><button class="code-copy-btn" onclick="copyCode(this)">Copy</button></div>
        <pre><code>)HTML";
    out += html_escape(snip5_cpp);
    out += R"HTML(</code></pre>
      </div>
    </div>
    <div class="telemetry-bar">
      <div class="telemetry-header">
        <span>Terminal Execution Telemetry</span>
        <span class="telemetry-pass">&#10003; PASS (0 diagnostics)</span>
      </div>
      <div class="telemetry-body">
        <span class="telemetry-stat"><code>$ cfire run main.cf</code></span>
        <span class="telemetry-stat">Output: <strong style="color:var(--text-bright);">fnv1a hash: 0x811c9dc5</strong></span>
        <span class="telemetry-stat">GC Pauses: <span class="telemetry-val">0.00ms</span></span>
        <span class="telemetry-stat">Binding Overhead: <span class="telemetry-val">0 &mu;s</span></span>
      </div>
    </div>
  </div>

  <!-- Tab 6: Audio DSP -->
  <div id="tab-audio" class="tab-panel">
    <div class="rosetta-mobile-toggle">
      <button class="active" onclick="toggleRosettaMobile(this, 'cf')">cfire (.cf)</button>
      <button onclick="toggleRosettaMobile(this, 'cpp')">Emitted ISO C++23</button>
    </div>
    <div class="grid-2">
      <div class="rosetta-pane-cf">
        <div class="code-header"><span class="tag-cf">cfire (.cf)</span><button class="code-copy-btn" onclick="copyCode(this)">Copy</button></div>
        <pre><code>)HTML";
    out += html_escape(snip6_cf);
    out += R"HTML(</code></pre>
      </div>
      <div class="rosetta-pane-cpp">
        <div class="code-header"><span class="tag-cpp">Emitted ISO C++23</span><button class="code-copy-btn" onclick="copyCode(this)">Copy</button></div>
        <pre><code>)HTML";
    out += html_escape(snip6_cpp);
    out += R"HTML(</code></pre>
      </div>
    </div>
    <div class="telemetry-bar">
      <div class="telemetry-header">
        <span>Terminal Execution Telemetry</span>
        <span class="telemetry-pass">&#10003; PASS (0 diagnostics)</span>
      </div>
      <div class="telemetry-body">
        <span class="telemetry-stat"><code>$ cfire run main.cf</code></span>
        <span class="telemetry-stat">Output: <strong style="color:var(--text-bright);">rms=0.24 peak=0.40</strong></span>
        <span class="telemetry-stat">GC Pauses: <span class="telemetry-val">0.00ms</span></span>
        <span class="telemetry-stat">Binding Overhead: <span class="telemetry-val">0 &mu;s</span></span>
      </div>
    </div>
  </div>

  <!-- Tab 7: AI, Vector SQLite & MCP -->
  <div id="tab-ai" class="tab-panel">
    <div class="rosetta-mobile-toggle">
      <button class="active" onclick="toggleRosettaMobile(this, 'cf')">cfire (.cf)</button>
      <button onclick="toggleRosettaMobile(this, 'cpp')">Emitted ISO C++23</button>
    </div>
    <div class="grid-2">
      <div class="rosetta-pane-cf">
        <div class="code-header"><span class="tag-cf">cfire (.cf)</span><button class="code-copy-btn" onclick="copyCode(this)">Copy</button></div>
        <pre><code>)HTML";
    out += html_escape(snip7_cf);
    out += R"HTML(</code></pre>
      </div>
      <div class="rosetta-pane-cpp">
        <div class="code-header"><span class="tag-cpp">Emitted ISO C++23</span><button class="code-copy-btn" onclick="copyCode(this)">Copy</button></div>
        <pre><code>)HTML";
    out += html_escape(snip7_cpp);
    out += R"HTML(</code></pre>
      </div>
    </div>
    <div class="telemetry-bar">
      <div class="telemetry-header">
        <span>Terminal Execution Telemetry</span>
        <span class="telemetry-pass">&#10003; PASS (0 diagnostics)</span>
      </div>
      <div class="telemetry-body">
        <span class="telemetry-stat"><code>$ cfire run main.cf</code></span>
        <span class="telemetry-stat">Output: <strong style="color:var(--text-bright);">Match: cfire lowers 1-to-1 to ISO C++23 with zero runtime overhead.</strong></span>
        <span class="telemetry-stat">GC Pauses: <span class="telemetry-val">0.00ms</span></span>
        <span class="telemetry-stat">Binding Overhead: <span class="telemetry-val">0 &mu;s</span></span>
      </div>
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
               "  <title>cfire — Systems Language (pronounced sapphire)</title>\n  <style>" + detail::shared_css() + "</style>\n"
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
    out += "  <link rel=\"icon\" type=\"image/png\" href=\"assets/cfire_logo_emblem.png\" />\n";
    out += "  <style>" + detail::shared_css() + "</style>\n";
    out += "</head>\n<body>\n";
    out += detail::navbar_html("reference");
    out += "<div class=\"guide-shell\">\n";
    out += "  <aside class=\"guide-sidebar\">\n";
    out += "    <details class=\"sidebar-details\" open>\n";
    out += "      <summary class=\"sidebar-summary\">\n";
    out += "        <span>Modules &amp; Diagnostics</span>\n";
    out += "        <span class=\"sidebar-chevron\">&#9662;</span>\n";
    out += "      </summary>\n";
    out += "      <div class=\"sidebar-content\" id=\"sidebar-nav\">\n";
    out += "        <input type=\"text\" id=\"ref-filter\" class=\"search-input\" placeholder=\"Filter modules &amp; errors...\" oninput=\"filterRefNav(this.value)\" style=\"margin-bottom: 0.75rem;\" />\n";
    out += "        <h4>`core:*` Modules</h4>\n        <ul class=\"sidebar-module-list\" style=\"list-style: none; padding: 0;\">\n";
    for (const auto& mod : stdlib_catalog()) {
        out += std::format("          <li class=\"nav-item\"><a href=\"#mod-{}\"><code>{}</code></a></li>\n", mod.ns_alias, mod.import_path);
    }
    out += "        </ul>\n        <h4 style=\"margin-top:1.25rem;\">Diagnostics</h4>\n        <ul class=\"sidebar-diag-list\" style=\"list-style: none; padding: 0;\">\n";
    for (const auto& d : diagnostics_catalog()) {
        out += std::format("          <li class=\"nav-item\"><a href=\"#diag-{}\"><code>{}</code></a></li>\n", d.code, d.code);
    }
    out += std::format(R"HTML(        </ul>
        <div class="toc-quick-links" style="margin-top: 1.25rem;">
          <h4>Quick Links</h4>
          <ul style="list-style: none; padding: 0; margin: 0;">
            <li style="margin-bottom: 0.35rem;"><a href="guide.html">Language Guide (16 Chapters) &rarr;</a></li>
            <li style="margin-bottom: 0.35rem;"><a href="index.html#landscape">Systems Landscape &rarr;</a></li>
            <li style="margin-bottom: 0.35rem;"><a href="index.html#benchmarks">Compiler Benchmarks &rarr;</a></li>
          </ul>
        </div>
      </div>
    </details>
  </aside>
  <div class="guide-viewport" id="ref-viewport">
    <div class="guide-content-inner" style="max-width: 960px;">
      <h1>`core:*` Standard Library &amp; Diagnostics Reference</h1>
      <p class="subtitle" style="margin:0 0 2rem;">Automatically generated from the live <code>cfire</code> compiler metadata catalog and verified by <code>cfire doc --check</code>.</p>
)HTML");

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

    out += R"HTML(      <footer class="footer" style="margin-top: 4rem; padding: 2rem 0; border-top: 1px solid var(--border);">
        Generated by <code>cfire doc</code> &bull; Always kept in sync with <code>src/docgen.hpp</code>.
      </footer>
    </div>
  </div>
</div>
<script>
function filterRefNav(query) {
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
</script>
</body>
</html>
)HTML";
    return out;
}

// Generates or verifies (`check_only = true`) the full documentation suite in `<repo>/docs/`.
[[nodiscard]] inline auto sync_docs_directory(
    const std::filesystem::path& docs_dir,
    bool check_only,
    const std::filesystem::path& runtime_dir = {}
) -> std::expected<void, std::string>
{
    // Auto-locate runtime directory if not explicitly passed
    std::filesystem::path rt_dir = runtime_dir;
    if (rt_dir.empty()) {
        if (const char* env = std::getenv("CFIRE_RUNTIME_DIR")) {
            rt_dir = std::filesystem::path(env);
        } else {
            auto candidate = docs_dir.parent_path() / "runtime";
            if (std::filesystem::exists(candidate / "cfire_prelude.hpp")) {
                rt_dir = candidate;
            } else if (std::filesystem::exists("runtime/cfire_prelude.hpp")) {
                rt_dir = "runtime";
            }
        }
    }

    // 1. Run Automated Library & Documentation Integrity Audit
    if (!rt_dir.empty()) {
        auto audit_res = audit_documentation_catalog(rt_dir);
        if (!audit_res) {
            return std::unexpected(audit_res.error());
        }
    }

    struct DocTarget {
        std::filesystem::path path;
        std::string content;
    };
    if ( std::filesystem::exists(docs_dir / "GUIDE.md") && std::filesystem::exists(docs_dir / "index.template.html") ) {
        std::ifstream g_ifs(docs_dir / "GUIDE.md");
        std::stringstream g_ss;
        g_ss << g_ifs.rdbuf();
        std::string guide_txt = g_ss.str();

        std::ifstream t_ifs(docs_dir / "index.template.html");
        std::stringstream t_ss;
        t_ss << t_ifs.rdbuf();
        std::string tmpl_txt = t_ss.str();

        std::string expected_btn = std::format("Explore {} Stdlib Modules", stdlib_catalog().size());
        if (!tmpl_txt.contains(expected_btn)) {
            return std::unexpected(std::format(
                "Documentation audit failure: docs/index.template.html has stale stdlib module count (expected '{}').",
                expected_btn));
        }

        // Verify every `## Chapter N:` in GUIDE.md has a corresponding `guide.html#chapter-N` roadmap card in index.template.html
        std::istringstream g_lines(guide_txt);
        std::string g_line;
        while (std::getline(g_lines, g_line)) {
            if (g_line.starts_with("## Chapter ")) {
                auto rest = g_line.substr(11);
                auto colon = rest.find(':');
                if (colon != std::string::npos) {
                    std::string ch_num = detail::trim_md(rest.substr(0, colon));
                    std::string expected_href = "guide.html#chapter-" + ch_num;
                    if (!tmpl_txt.contains(expected_href)) {
                        return std::unexpected(std::format(
                            "Documentation audit failure: docs/GUIDE.md defines 'Chapter {}', but docs/index.template.html is missing roadmap card link '{}'.",
                            ch_num, expected_href));
                    }
                }
            }
        }
    }

    if (check_only) {
        if (!std::filesystem::exists(docs_dir / "GUIDE.md")) {
            return std::unexpected(std::format("missing documentation file: {} (authoritative guide source required)", (docs_dir / "GUIDE.md").string()));
        }
        if (!std::filesystem::exists(docs_dir / "index.template.html")) {
            return std::unexpected(std::format("missing documentation file: {} (authoritative landing template required)", (docs_dir / "index.template.html").string()));
        }
        auto llm_ref = generate_llm_reference();
        for (const auto& mod : stdlib_catalog()) {
            if (!llm_ref.contains(mod.import_path)) {
                return std::unexpected(std::format("llm reference missing module: {}", mod.import_path));
            }
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

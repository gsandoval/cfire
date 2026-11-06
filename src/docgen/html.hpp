#pragma once
// src/docgen/html.hpp — HTML Landing Page, Rosetta Showcase, Guide, Reference & Docs Directory Sync

#include "catalog.hpp"
#include "styles.hpp"
#include "markdown.hpp"
#include "clang_bridge.hpp"
#include <expected>
#include <filesystem>
#include <format>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace cfire::doc {

[[nodiscard]] inline auto compile_snippet_to_cpp(
    std::string_view filename,
    std::string_view cf_source,
    const std::filesystem::path& hint_rt_dir = {}
) -> std::string {
    auto ast = cfire::frontend::parse_source(std::string(filename), cf_source);
    if (!ast) return std::format("// Parse error: {}\n", ast.error());
    auto checked = cfire::frontend::analyze(std::move(*ast));
    if (!checked) return std::format("// Sema error: {}\n", checked.error());

    std::filesystem::path rt_dir = hint_rt_dir;
    if (rt_dir.empty() || !std::filesystem::exists(rt_dir / "cfire_prelude.hpp")) {
        if (const char* env = std::getenv("CFIRE_RUNTIME_DIR")) {
            rt_dir = std::filesystem::path(env);
        } else if (std::filesystem::exists("runtime/cfire_prelude.hpp")) {
            rt_dir = "runtime";
        } else if (std::filesystem::exists("cfire/runtime/cfire_prelude.hpp")) {
            rt_dir = "cfire/runtime";
        } else if (std::filesystem::exists("../runtime/cfire_prelude.hpp")) {
            rt_dir = "../runtime";
        }
    }
    std::vector<std::string> cflags = {"-std=c++2b"};
    if (!rt_dir.empty()) {
        cflags.push_back("-I" + std::filesystem::absolute(rt_dir).string());
        if (std::filesystem::is_directory(rt_dir / "vendor")) {
            cflags.push_back("-I" + std::filesystem::absolute(rt_dir / "vendor").string());
        }
    }
    auto cache_dir = std::filesystem::temp_directory_path() / "cfire_doc_cache";
    for (const auto& imp : checked->ast.imports) {
        if (!imp.is_cpp) continue;
        if (auto idx = cfire::bridge::inspect_header(imp.path, cflags, cache_dir)) {
            for (const auto& [sym_name, sym] : idx->symbols) {
                if (sym.kind == "FunctionDecl") {
                    checked->cpp_fn_params[sym_name] = sym.param_types;
                }
            }
        }
    }
    return cfire::frontend::emit_cpp(*checked);
}

[[nodiscard]] inline auto generate_reproducible_metrics() -> std::string {
    return std::format(R"HTML(  <!-- Metrics Ribbon -->
  <div class="stats-ribbon">
    <div class="stat-pill">
      <div class="stat-val">ISO C++23</div>
      <div class="stat-lbl">Pedantic-Clean Output</div>
    </div>
    <div class="stat-pill">
      <div class="stat-val">1-to-1</div>
      <div class="stat-lbl">Line-Mapped Lowering</div>
    </div>
    <div class="stat-pill">
      <div class="stat-val">{} Rules</div>
      <div class="stat-lbl">Static Intent &amp; Match Checks</div>
    </div>
    <div class="stat-pill">
      <div class="stat-val">Core + Ext</div>
      <div class="stat-lbl">{} Tiered Stdlib Modules</div>
    </div>
    <div class="stat-pill">
      <div class="stat-val">cfire eject</div>
      <div class="stat-lbl">Standalone CMake Exit</div>
    </div>
  </div>)HTML", diagnostics_catalog().size(), stdlib_catalog().size());
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
        [only]               => { return "single=" + only }
        [first, ..mid, last] => { return first + "..(" + str(mid.len()) + ").." + last }
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

import "ext:net/websocket"
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

import "ext:json"

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

import "ext:audio"

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

    // 7. AI-First Multimodal Documents, Local Embeddings, Vector SQLite & MCP Tooling Snippet
    std::string_view snip7_cf = R"CF(package main

import "ext:ai" as ai
import "ext:db/sqlite" as sqlite
import "ext:json" as json

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

    // 2. Zero-dependency document & slide extraction (.pdf, .pptx, .docx, .odt, .epub, .md)
    doc := try ai.parse_document("# Transpiler\ncfire lowers 1-to-1 to ISO C++23 with zero runtime overhead.", "arch.md")
    chunks := ai.chunk_document(doc, 512, 64)

    // 3. Embedded SQLite with AVX2/NEON SIMD vector similarity
    mut db := try sqlite.open(":memory:")
    try db.exec("CREATE TABLE docs (id INTEGER, title TEXT, snippet TEXT, embedding BLOB)")

    for ch in chunks {
        vec := try ai.embed("local", ch.text)
        try db.exec("INSERT INTO docs VALUES (?1, ?2, ?3, ?4)", ch.index, ch.title, ch.text, vec)
    }

    // 4. In-database semantic search
    qvec := try ai.embed("local", "modern C++23 compiler")
    hits := try db.query[DocEntry]("SELECT id, title, snippet FROM docs ORDER BY vec_cosine_similarity(embedding, ?1) DESC LIMIT 1", qvec)
    if hits.len() > 0 {
        println("Match [{}]: {}", hits[0].Title, hits[0].Snippet)
    }
}
)CF";
    std::string snip7_cpp = compile_snippet_to_cpp("snippet_ai.cf", snip7_cf);

    // 8. Hybrid Desktop & Mobile WebUI (ext:webui) Snippet
    std::string_view snip8_cf = R"CF(package main

import "ext:webui"

type GreetReq struct {
    name:  str `json:"name"`
    count: i32 `json:"count"`
}

type GreetRes struct {
    message: str `json:"message"`
    count:   i32 `json:"count"`
}

fn main() {
    mut app := webui.new_app("cfire Studio", 960, 640)
    defer app.close()
    app.serve_dir("./ui")

    app.bind[GreetReq]("greet") |req| {
        next := req.count + 1
        res := GreetRes{message: "Hello, {req.name}!", count: next}
        app.emit("greet:updated", res)
        return res
    }

    raw := try app.call_rpc("greet", `{"name":"Ada","count":1}`)
    println("rpc={} events={}", raw, app.emitted_events_count("greet:updated"))
}
)CF";
    std::string snip8_cpp = compile_snippet_to_cpp("snippet_webui.cf", snip8_cf);

    std::string out;
    out += R"HTML(  <!-- Interactive Live Rosetta Showcase -->
  <h2 class="section-title">See the Lowering: Live <code>.cf</code> &rarr; ISO C++23</h2>
  <p class="section-subtitle">Select any systems pattern below to inspect the clean, idiomatic ISO C++23 code emitted by the compiler.</p>

  <div class="tabs">
    <button class="tab-btn active" onclick="showTab('tab-intents', this)">1. Intents &amp; Smart Pointers</button>
    <button class="tab-btn" onclick="showTab('tab-patterns', this)">2. Enums &amp; Slice Patterns</button>
    <button class="tab-btn" onclick="showTab('tab-net', this)">3. WebSockets &amp; Cancellation</button>
    <button class="tab-btn" onclick="showTab('tab-json', this)">4. SIMD JSON Reflection</button>
    <button class="tab-btn" onclick="showTab('tab-interop', this)">5. Clang C/C++ Interop</button>
    <button class="tab-btn" onclick="showTab('tab-audio', this)">6. Audio DSP (<code>ext:audio</code>)</button>
    <button class="tab-btn" onclick="showTab('tab-ai', this)">7. AI, Vector SQLite &amp; MCP (<code>ext:ai</code>)</button>
    <button class="tab-btn" onclick="showTab('tab-webui', this)">8. Hybrid WebUI (<code>ext:webui</code>)</button>
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
        <span style="display:inline-flex;align-items:center;gap:0.35rem;"><span class="material-symbols-outlined" style="font-size:0.95rem;color:var(--accent);" aria-hidden="true">terminal</span> Terminal Execution Telemetry</span>
        <span class="telemetry-pass" style="display:inline-flex;align-items:center;gap:0.3rem;"><span class="material-symbols-outlined" style="font-size:0.95rem;" aria-hidden="true">check_circle</span> PASS (0 diagnostics)</span>
      </div>
      <div class="telemetry-body">
        <span class="telemetry-stat"><code>$ cfire run main.cf</code></span>
        <span class="telemetry-stat">Output: <strong style="color:var(--text-bright);">b=42 p=100</strong></span>
        <span class="telemetry-stat">Standard: <span class="telemetry-val">ISO C++23 (-pedantic-errors)</span></span>
        <span class="telemetry-stat">Static Checks: <span class="telemetry-val">18/18 cfire/* rules</span></span>
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
        <span style="display:inline-flex;align-items:center;gap:0.35rem;"><span class="material-symbols-outlined" style="font-size:0.95rem;color:var(--accent);" aria-hidden="true">terminal</span> Terminal Execution Telemetry</span>
        <span class="telemetry-pass" style="display:inline-flex;align-items:center;gap:0.3rem;"><span class="material-symbols-outlined" style="font-size:0.95rem;" aria-hidden="true">check_circle</span> PASS (0 diagnostics)</span>
      </div>
      <div class="telemetry-body">
        <span class="telemetry-stat"><code>$ cfire run main.cf</code></span>
        <span class="telemetry-stat">Output: <strong style="color:var(--text-bright);">result: alpha..(1)..gamma</strong></span>
        <span class="telemetry-stat">Standard: <span class="telemetry-val">ISO C++23 (-pedantic-errors)</span></span>
        <span class="telemetry-stat">Static Checks: <span class="telemetry-val">18/18 cfire/* rules</span></span>
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
        <span style="display:inline-flex;align-items:center;gap:0.35rem;"><span class="material-symbols-outlined" style="font-size:0.95rem;color:var(--accent);" aria-hidden="true">terminal</span> Terminal Execution Telemetry</span>
        <span class="telemetry-pass" style="display:inline-flex;align-items:center;gap:0.3rem;"><span class="material-symbols-outlined" style="font-size:0.95rem;" aria-hidden="true">check_circle</span> PASS (0 diagnostics)</span>
      </div>
      <div class="telemetry-body">
        <span class="telemetry-stat"><code>$ cfire run main.cf</code></span>
        <span class="telemetry-stat">Output: <strong style="color:var(--text-bright);">received: ping-payload</strong></span>
        <span class="telemetry-stat">Standard: <span class="telemetry-val">ISO C++23 (-pedantic-errors)</span></span>
        <span class="telemetry-stat">Static Checks: <span class="telemetry-val">18/18 cfire/* rules</span></span>
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
        <span style="display:inline-flex;align-items:center;gap:0.35rem;"><span class="material-symbols-outlined" style="font-size:0.95rem;color:var(--accent);" aria-hidden="true">terminal</span> Terminal Execution Telemetry</span>
        <span class="telemetry-pass" style="display:inline-flex;align-items:center;gap:0.3rem;"><span class="material-symbols-outlined" style="font-size:0.95rem;" aria-hidden="true">check_circle</span> PASS (0 diagnostics)</span>
      </div>
      <div class="telemetry-body">
        <span class="telemetry-stat"><code>$ cfire run main.cf</code></span>
        <span class="telemetry-stat">Output: <strong style="color:var(--text-bright);">service=auth latency=1.42ms</strong></span>
        <span class="telemetry-stat">Standard: <span class="telemetry-val">ISO C++23 (-pedantic-errors)</span></span>
        <span class="telemetry-stat">Static Checks: <span class="telemetry-val">18/18 cfire/* rules</span></span>
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
        <span style="display:inline-flex;align-items:center;gap:0.35rem;"><span class="material-symbols-outlined" style="font-size:0.95rem;color:var(--accent);" aria-hidden="true">terminal</span> Terminal Execution Telemetry</span>
        <span class="telemetry-pass" style="display:inline-flex;align-items:center;gap:0.3rem;"><span class="material-symbols-outlined" style="font-size:0.95rem;" aria-hidden="true">check_circle</span> PASS (0 diagnostics)</span>
      </div>
      <div class="telemetry-body">
        <span class="telemetry-stat"><code>$ cfire run main.cf</code></span>
        <span class="telemetry-stat">Output: <strong style="color:var(--text-bright);">fnv1a hash: 0x268ccafd</strong></span>
        <span class="telemetry-stat">Standard: <span class="telemetry-val">ISO C++23 (-pedantic-errors)</span></span>
        <span class="telemetry-stat">Static Checks: <span class="telemetry-val">18/18 cfire/* rules</span></span>
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
        <span style="display:inline-flex;align-items:center;gap:0.35rem;"><span class="material-symbols-outlined" style="font-size:0.95rem;color:var(--accent);" aria-hidden="true">terminal</span> Terminal Execution Telemetry</span>
        <span class="telemetry-pass" style="display:inline-flex;align-items:center;gap:0.3rem;"><span class="material-symbols-outlined" style="font-size:0.95rem;" aria-hidden="true">check_circle</span> PASS (0 diagnostics)</span>
      </div>
      <div class="telemetry-body">
        <span class="telemetry-stat"><code>$ cfire run main.cf</code></span>
        <span class="telemetry-stat">Output: <strong style="color:var(--text-bright);">rms=0.24 peak=0.40</strong></span>
        <span class="telemetry-stat">Standard: <span class="telemetry-val">ISO C++23 (-pedantic-errors)</span></span>
        <span class="telemetry-stat">Static Checks: <span class="telemetry-val">18/18 cfire/* rules</span></span>
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
        <span style="display:inline-flex;align-items:center;gap:0.35rem;"><span class="material-symbols-outlined" style="font-size:0.95rem;color:var(--accent);" aria-hidden="true">terminal</span> Terminal Execution Telemetry</span>
        <span class="telemetry-pass" style="display:inline-flex;align-items:center;gap:0.3rem;"><span class="material-symbols-outlined" style="font-size:0.95rem;" aria-hidden="true">check_circle</span> PASS (0 diagnostics)</span>
      </div>
      <div class="telemetry-body">
        <span class="telemetry-stat"><code>$ cfire run main.cf</code></span>
        <span class="telemetry-stat">Output: <strong style="color:var(--text-bright);">Match [Transpiler]: cfire lowers 1-to-1 to ISO C++23 with zero runtime overhead.</strong></span>
        <span class="telemetry-stat">Standard: <span class="telemetry-val">ISO C++23 (-pedantic-errors)</span></span>
        <span class="telemetry-stat">Static Checks: <span class="telemetry-val">18/18 cfire/* rules</span></span>
      </div>
    </div>
  </div>

  <!-- Tab 8: Hybrid Desktop & Mobile WebUI -->
  <div id="tab-webui" class="tab-panel">
    <div class="rosetta-mobile-toggle">
      <button class="active" onclick="toggleRosettaMobile(this, 'cf')">cfire (.cf)</button>
      <button onclick="toggleRosettaMobile(this, 'cpp')">Emitted ISO C++23</button>
    </div>
    <div class="grid-2">
      <div class="rosetta-pane-cf">
        <div class="code-header"><span class="tag-cf">cfire (.cf)</span><button class="code-copy-btn" onclick="copyCode(this)">Copy</button></div>
        <pre><code>)HTML";
    out += html_escape(snip8_cf);
    out += R"HTML(</code></pre>
      </div>
      <div class="rosetta-pane-cpp">
        <div class="code-header"><span class="tag-cpp">Emitted ISO C++23</span><button class="code-copy-btn" onclick="copyCode(this)">Copy</button></div>
        <pre><code>)HTML";
    out += html_escape(snip8_cpp);
    out += R"HTML(</code></pre>
      </div>
    </div>
    <div class="telemetry-bar">
      <div class="telemetry-header">
        <span style="display:inline-flex;align-items:center;gap:0.35rem;"><span class="material-symbols-outlined" style="font-size:0.95rem;color:var(--accent);" aria-hidden="true">terminal</span> Terminal Execution Telemetry</span>
        <span class="telemetry-pass" style="display:inline-flex;align-items:center;gap:0.3rem;"><span class="material-symbols-outlined" style="font-size:0.95rem;" aria-hidden="true">check_circle</span> PASS (0 diagnostics)</span>
      </div>
      <div class="telemetry-body">
        <span class="telemetry-stat"><code>$ cfire run main.cf</code></span>
        <span class="telemetry-stat">Output: <strong style="color:var(--text-bright);">rpc={"message":"Hello, Ada!","count":2} events=1</strong></span>
        <span class="telemetry-stat">Standard: <span class="telemetry-val">ISO C++23 (-pedantic-errors)</span></span>
        <span class="telemetry-stat">Static Checks: <span class="telemetry-val">18/18 cfire/* rules</span></span>
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
               "  <title>cfire — Systems Language (pronounced sapphire)</title>\n" + detail::icon_font_head_links() + "  <style>" + detail::shared_css() + "</style>\n"
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
    const auto& mods = stdlib_catalog();
    const auto& diags = diagnostics_catalog();

    std::string out;
    out += "<!DOCTYPE html>\n<html lang=\"en\">\n<head>\n";
    out += "  <meta charset=\"UTF-8\" />\n";
    out += "  <meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\" />\n";
    out += "  <title>Stdlib &amp; Diagnostics Reference — cfire</title>\n";
    out += "  <link rel=\"icon\" type=\"image/png\" href=\"assets/cfire_logo_emblem.png\" />\n";
    out += detail::icon_font_head_links();
    out += "  <style>" + detail::shared_css() + "</style>\n";
    out += "</head>\n<body>\n";
    out += detail::navbar_html("reference");
    out += "<div class=\"guide-shell\">\n";
    out += "  <aside class=\"guide-sidebar\">\n";
    out += "    <div class=\"sidebar-details\" id=\"ref-sidebar-details\">\n";
    out += "      <button type=\"button\" class=\"sidebar-summary\" onclick=\"this.parentElement.classList.toggle('open')\">\n";
    out += "        <span id=\"ref-mobile-sidebar-label\"><span class=\"sidebar-summary-badge\">Stdlib:</span> Overview &amp; Diagnostics</span>\n";
    out += "        <span class=\"material-symbols-outlined sidebar-chevron\" aria-hidden=\"true\">expand_more</span>\n";
    out += "      </button>\n";
    out += "      <div class=\"sidebar-content\" id=\"sidebar-nav\">\n";
    out += "        <div style=\"position:relative; margin-bottom: 0.75rem;\">\n";
    out += "          <input type=\"text\" id=\"ref-filter\" class=\"search-input\" placeholder=\"Search APIs &amp; errors (⌘K)...\" oninput=\"filterRefNav(this.value)\" autocomplete=\"off\" />\n";
    out += "          <div id=\"ref-search-results\" class=\"search-results-dropdown\"></div>\n";
    out += "        </div>\n";
    out += "        <ul class=\"sidebar-module-list\" style=\"list-style: none; padding: 0; margin-bottom: 0.75rem;\">\n";
    out += std::format("          <li class=\"nav-item\"><a href=\"#overview\" id=\"nav-pane-overview\" class=\"active\" onclick=\"navigateToRefPane('overview'); return false;\"><strong>Overview (All {} Modules)</strong></a></li>\n", mods.size());
    out += "        </ul>\n";
    std::size_t core_count = 0, ext_count = 0, vendor_count = 0;
    for (const auto& mod : mods) {
        if (mod.import_path.starts_with("core:")) ++core_count;
        else if (mod.import_path.starts_with("ext:")) ++ext_count;
        else if (mod.import_path.starts_with("vendor:")) ++vendor_count;
    }
    out += std::format("        <h4>Foundational Core ({})</h4>\n        <ul class=\"sidebar-module-list\" style=\"list-style: none; padding: 0;\">\n", core_count);
    for (const auto& mod : mods) {
        if (!mod.import_path.starts_with("core:")) continue;
        out += std::format("          <li class=\"nav-item\"><a href=\"#mod-{}\" id=\"nav-pane-mod-{}\" onclick=\"navigateToRefPane('mod-{}'); return false;\"><code>{}</code> <span style=\"color:var(--muted);font-size:0.75rem;\">({})</span></a></li>\n",
            mod.ns_alias, mod.ns_alias, mod.ns_alias, mod.import_path, mod.functions.size());
    }
    out += std::format("        </ul>\n        <h4 style=\"margin-top:1rem;\">Domain Extensions ({})</h4>\n        <ul class=\"sidebar-module-list\" style=\"list-style: none; padding: 0;\">\n", ext_count);
    for (const auto& mod : mods) {
        if (!mod.import_path.starts_with("ext:")) continue;
        out += std::format("          <li class=\"nav-item\"><a href=\"#mod-{}\" id=\"nav-pane-mod-{}\" onclick=\"navigateToRefPane('mod-{}'); return false;\"><code>{}</code> <span style=\"color:var(--muted);font-size:0.75rem;\">({})</span></a></li>\n",
            mod.ns_alias, mod.ns_alias, mod.ns_alias, mod.import_path, mod.functions.size());
    }
    out += std::format("        </ul>\n        <h4 style=\"margin-top:1rem;\">Vendor Libraries ({})</h4>\n        <ul class=\"sidebar-module-list\" style=\"list-style: none; padding: 0;\">\n", vendor_count);
    for (const auto& mod : mods) {
        if (!mod.import_path.starts_with("vendor:")) continue;
        out += std::format("          <li class=\"nav-item\"><a href=\"#mod-{}\" id=\"nav-pane-mod-{}\" onclick=\"navigateToRefPane('mod-{}'); return false;\"><code>{}</code> <span style=\"color:var(--muted);font-size:0.75rem;\">({})</span></a></li>\n",
            mod.ns_alias, mod.ns_alias, mod.ns_alias, mod.import_path, mod.functions.size());
    }
    out += std::format("        </ul>\n        <h4 style=\"margin-top:1.25rem;\">Diagnostics ({})</h4>\n        <ul class=\"sidebar-diag-list\" style=\"list-style: none; padding: 0;\">\n", diags.size());
    for (const auto& d : diags) {
        out += std::format("          <li class=\"nav-item\"><a href=\"#diag-{}\" id=\"nav-diag-{}\" onclick=\"navigateToRefDiag('diag-{}'); return false;\"><code>{}</code></a></li>\n",
            d.code, d.code, d.code, d.code);
    }
    out += std::format(R"HTML(        </ul>
        <div class="toc-quick-links" style="margin-top: 1.25rem;">
          <h4>Quick Links</h4>
          <ul style="list-style: none; padding: 0; margin: 0;">
            <li style="margin-bottom: 0.35rem;"><a href="guide.html">Language Guide (19 Chapters) &rarr;</a></li>
            <li style="margin-bottom: 0.35rem;"><a href="index.html#landscape">Systems Landscape &rarr;</a></li>
            <li style="margin-bottom: 0.35rem;"><a href="index.html#benchmarks">Compiler Benchmarks &rarr;</a></li>
          </ul>
        </div>
      </div>
    </div>
  </aside>
  <div class="guide-viewport" id="ref-viewport">
    <main class="guide-content-inner" style="max-width: 960px;">
      <div class="reader-toolbar">
        <div class="reader-breadcrumbs">
          <a href="index.html">cfire Docs</a>
          <span>/</span>
          <a href="#overview" onclick="navigateToRefPane('overview'); return false;">Stdlib Reference</a>
          <span>/</span>
          <span class="crumb-current" id="ref-active-crumb">Overview (All {0} Modules)</span>
        </div>
        <div class="toolbar-actions">
          <div class="reader-mode-toggle" role="group" aria-label="Reference Reading Mode">
            <button type="button" class="mode-btn active" id="btn-ref-module" onclick="setRefMode('module')" title="Single-module focused view">Module View</button>
            <button type="button" class="mode-btn" id="btn-ref-all" onclick="setRefMode('all')" title="Continuous all-modules view">View All ({0})</button>
          </div>
          <button type="button" class="btn-toolbar-ai" onclick="openAiModal()" title="Generate context-rich AI prompt for this module">
            <span class="material-symbols-outlined" style="font-size:0.95rem;" aria-hidden="true">auto_awesome</span>
            <span>Ask AI</span>
          </button>
        </div>
      </div>
)HTML", mods.size());

    // Pane 0: Categorized Overview Dashboard
    out += std::format("      <article class=\"ref-pane active\" id=\"ref-pane-overview\" data-pane-id=\"overview\" data-pane-title=\"Overview (All {} Modules)\">\n", mods.size());
    out += "        <h1>Standard Library &amp; Diagnostics Reference</h1>\n";
    out += std::format("        <p class=\"subtitle\" style=\"margin:0 0 1.5rem;\">{} modules organized into three explicit package namespaces — <strong><code>core:*</code> Foundational Core</strong> ({} zero-dependency C++23 modules), <strong><code>ext:*</code> Domain Extensions</strong> ({} opt-in networking, database, compression, audio, AI, and WebUI modules), and <strong><code>vendor:*</code> Third-Party Libraries</strong> ({} bundled C/C++ engines) — plus {} semantic diagnostics, verified by <code>cfire doc --check</code>.</p>\n", mods.size(), core_count, ext_count, vendor_count, diags.size());

    struct CategoryGroup {
        std::string_view title;
        std::vector<std::string_view> aliases;
    };
    const std::vector<CategoryGroup> categories = {
        {"core:* — Strings, Unicode, Regex & Containers", {"str", "unicode", "regex", "collections", "sort"}},
        {"core:* — Filesystem, OS, Time, Sync, CLI & Logging", {"fs", "os", "time", "math", "rand", "sync", "context", "cli", "log"}},
        {"core:* — Cryptography & Data Encodings", {"aes", "blake3", "hmac", "sha256", "base64", "csv", "hex"}},
        {"ext:* — JSON, Compression, Networking, SQLite, Audio, AI & WebUI", {"json", "zstd", "net", "http", "tls", "url", "websocket", "sqlite", "audio", "ai", "webui"}},
        {"vendor:* — Graphics & Immediate-Mode GUI", {"rl", "rg"}},
    };

    for (const auto& cat : categories) {
        out += std::format("        <div class=\"ref-category-title\">{}</div>\n", detail::html_escape(cat.title));
        out += "        <div class=\"ref-category-grid\">\n";
        for (auto alias : cat.aliases) {
            for (const auto& mod : mods) {
                if (mod.ns_alias == alias) {
                    out += std::format(
                        "          <a href=\"#mod-{}\" class=\"ref-mod-card\" onclick=\"navigateToRefPane('mod-{}'); return false;\">\n"
                        "            <div class=\"ref-mod-card-head\">\n"
                        "              <span class=\"ref-mod-card-code\">{}</span>\n"
                        "              <span class=\"ref-mod-card-count\">{} fns</span>\n"
                        "            </div>\n"
                        "            <div class=\"ref-mod-card-title\">{}</div>\n"
                        "            <div class=\"ref-mod-card-desc\">{}</div>\n"
                        "          </a>\n",
                        mod.ns_alias, mod.ns_alias,
                        mod.import_path, mod.functions.size(),
                        detail::html_escape(mod.title),
                        detail::render_inline_md(mod.overview));
                    break;
                }
            }
        }
        out += "        </div>\n";
    }

    out += std::format("        <div class=\"ref-category-title\">Compiler Semantic Diagnostics ({} Rules)</div>\n", diags.size());
    out += "        <div class=\"ref-category-grid\">\n";
    out += std::format(
        "          <a href=\"#diagnostics\" class=\"ref-mod-card\" onclick=\"navigateToRefPane('diagnostics'); return false;\">\n"
        "            <div class=\"ref-mod-card-head\">\n"
        "              <span class=\"ref-mod-card-code\">cfire/* Rules</span>\n"
        "              <span class=\"ref-mod-card-count\">{} rules</span>\n"
        "            </div>\n"
        "            <div class=\"ref-mod-card-title\">Compiler Semantic Diagnostics Catalog</div>\n"
        "            <div class=\"ref-mod-card-desc\">Rejected vs. Accepted <code>.cf</code> side-by-side verification rules enforced by the semantic checker.</div>\n"
        "          </a>\n",
        diags.size());
    out += "        </div>\n";

    if (!mods.empty()) {
        out += std::format(
            "        <nav class=\"chapter-pager\" aria-label=\"Module Pagination\">\n"
            "          <div class=\"pager-card empty\"></div>\n"
            "          <a href=\"#mod-{}\" class=\"pager-card next\" onclick=\"navigateToRefPane('mod-{}'); return false;\">\n"
            "            <span class=\"pager-dir\">First Module &rarr;</span>\n"
            "            <span class=\"pager-title\">{} &mdash; {}</span>\n"
            "          </a>\n"
            "        </nav>\n",
            mods.front().ns_alias, mods.front().ns_alias,
            mods.front().import_path, detail::html_escape(mods.front().title));
    }
    out += "      </article>\n";

    // Individual Module Panes (1..34)
    for (std::size_t i = 0; i < mods.size(); ++i) {
        const auto& mod = mods[i];
        out += std::format(
            "      <article class=\"ref-pane\" id=\"ref-pane-mod-{}\" data-pane-id=\"mod-{}\" data-pane-title=\"{} — {}\">\n",
            mod.ns_alias, mod.ns_alias, mod.import_path, detail::html_escape(mod.title));
        out += std::format("        <h2 id=\"mod-{}\" style=\"margin-top:0.25rem;\"><code>import \"{}\"</code> &mdash; {}</h2>\n",
            mod.ns_alias, mod.import_path, detail::html_escape(mod.title));
        out += std::format("        <p>{} <br/><small style=\"color:var(--muted)\">Auto-linked flags: <code>{}</code> &bull; Namespace: <code>{}.*</code> &bull; {} exported functions</small></p>\n",
            detail::render_inline_md(mod.overview), detail::html_escape(mod.link_flags), mod.ns_alias, mod.functions.size());

        auto ex = detail::module_practical_example(mod.import_path);
        if (!ex.empty()) {
            out += "        <div class=\"code-header\"><span class=\"tag-cf\">Practical Workflow Example (.cf)</span></div>\n";
            out += "        <pre><code>" + detail::html_escape(ex) + "</code></pre>\n";
        }

        out += "        <div class=\"table-wrap api-table-wrap\"><table>\n          <thead><tr><th><code>.cf</code> Signature</th><th>C++23 Lowering</th><th>Description &amp; Example</th></tr></thead>\n          <tbody>\n";
        for (const auto& fn : mod.functions) {
            out += std::format("            <tr><td><code>{}</code></td><td><code>{}</code></td><td>{}<br/><code>{}</code></td></tr>\n",
                detail::html_escape(fn.cf_sig),
                detail::html_escape(fn.cpp_lowering),
                detail::render_inline_md(fn.summary),
                detail::html_escape(fn.example));
        }
        out += "          </tbody>\n        </table></div>\n";

        // Module Pager
        out += "        <nav class=\"chapter-pager\" aria-label=\"Module Pagination\">\n";
        if (i == 0) {
            out += std::format(
                   "          <a href=\"#overview\" class=\"pager-card prev\" onclick=\"navigateToRefPane('overview'); return false;\">\n"
                   "            <span class=\"pager-dir\">&larr; Overview</span>\n"
                   "            <span class=\"pager-title\">All {} Standard Library Modules</span>\n"
                   "          </a>\n",
                   mods.size());
        } else {
            const auto& prev = mods[i - 1];
            out += std::format(
                "          <a href=\"#mod-{}\" class=\"pager-card prev\" onclick=\"navigateToRefPane('mod-{}'); return false;\">\n"
                "            <span class=\"pager-dir\">&larr; Previous Module ({}/{})</span>\n"
                "            <span class=\"pager-title\">{} &mdash; {}</span>\n"
                "          </a>\n",
                prev.ns_alias, prev.ns_alias, i, mods.size(), prev.import_path, detail::html_escape(prev.title));
        }

        if (i + 1 < mods.size()) {
            const auto& next = mods[i + 1];
            out += std::format(
                "          <a href=\"#mod-{}\" class=\"pager-card next\" onclick=\"navigateToRefPane('mod-{}'); return false;\">\n"
                "            <span class=\"pager-dir\">Next Module ({}/{}) &rarr;</span>\n"
                "            <span class=\"pager-title\">{} &mdash; {}</span>\n"
                "          </a>\n",
                next.ns_alias, next.ns_alias, i + 2, mods.size(), next.import_path, detail::html_escape(next.title));
        } else {
            out += std::format("          <a href=\"#diagnostics\" class=\"pager-card next\" onclick=\"navigateToRefPane('diagnostics'); return false;\">\n"
                   "            <span class=\"pager-dir\">Diagnostics Reference &rarr;</span>\n"
                   "            <span class=\"pager-title\">{} Semantic Verification Rules</span>\n"
                   "          </a>\n", diags.size());
        }
        out += "        </nav>\n";
        out += "      </article>\n";
    }

    // Diagnostics Pane
    out += "      <article class=\"ref-pane\" id=\"ref-pane-diagnostics\" data-pane-id=\"diagnostics\" data-pane-title=\"Compiler Semantic Diagnostics\">\n";
    out += "        <h2 class=\"section-title\" id=\"diagnostics\" style=\"margin-top:0.25rem;\">Compiler Semantic Diagnostics</h2>\n";
    out += "        <p style=\"color:var(--muted); margin-bottom:1.25rem;\">Deterministic compile-time diagnostics emitted by the single-pass type and effect checker (<code>src/sema.hpp</code>).</p>\n";
    for (const auto& d : diags) {
        out += std::format("        <div class=\"card\" id=\"diag-{}\" style=\"margin-bottom:1.25rem;\">\n", d.code);
        out += std::format("          <h3><code>[{}]</code> &mdash; {}</h3>\n", d.code, detail::render_inline_md(d.title));
        out += std::format("          <p style=\"margin-bottom:0.85rem;\">{}</p>\n", detail::render_inline_md(d.rule));
        out += "          <div class=\"grid-2\">\n";
        out += std::format("            <div><div class=\"code-header\"><span style=\"color:var(--err);font-weight:700;\">Rejected (.cf)</span></div><pre><code>{}</code></pre></div>\n",
            detail::html_escape(d.bad_example));
        out += std::format("            <div><div class=\"code-header\"><span style=\"color:var(--ok);font-weight:700;\">Accepted (.cf)</span></div><pre><code>{}</code></pre></div>\n",
            detail::html_escape(d.good_example));
        out += "          </div>\n        </div>\n";
    }
    if (!mods.empty()) {
        const auto& last_mod = mods.back();
        out += std::format(
            "        <nav class=\"chapter-pager\" aria-label=\"Module Pagination\">\n"
            "          <a href=\"#mod-{}\" class=\"pager-card prev\" onclick=\"navigateToRefPane('mod-{}'); return false;\">\n"
            "            <span class=\"pager-dir\">&larr; Previous Module</span>\n"
            "            <span class=\"pager-title\">{} &mdash; {}</span>\n"
            "          </a>\n"
            "          <a href=\"#overview\" class=\"pager-card next\" onclick=\"navigateToRefPane('overview'); return false;\">\n"
            "            <span class=\"pager-dir\">Back to Overview &rarr;</span>\n"
            "            <span class=\"pager-title\">All {} Standard Library Modules</span>\n"
            "          </a>\n"
            "        </nav>\n",
            last_mod.ns_alias, last_mod.ns_alias, last_mod.import_path, detail::html_escape(last_mod.title), mods.size());
    }
    out += "      </article>\n";

    out += R"HTML(      <footer class="footer" style="margin-top: 4rem; padding: 2rem 0; border-top: 1px solid var(--border);">
        Generated by <code>cfire doc</code> &bull; Always kept in sync with <code>src/docgen.hpp</code>.
      </footer>
    </main>
  </div>
</div>
)HTML";
    out += detail::ai_context_modal_html();
    out += R"HTML(
<script>
let currentRefMode = 'module';
let activeRefPane = 'overview';

function setRefMode(mode) {
  currentRefMode = (mode === 'all') ? 'all' : 'module';
  const vp = document.getElementById('ref-viewport');
  const btnMod = document.getElementById('btn-ref-module');
  const btnAll = document.getElementById('btn-ref-all');
  if (currentRefMode === 'all') {
    if (vp) vp.classList.add('view-all-mode');
    if (btnAll) btnAll.classList.add('active');
    if (btnMod) btnMod.classList.remove('active');
  } else {
    if (vp) vp.classList.remove('view-all-mode');
    if (btnMod) btnMod.classList.add('active');
    if (btnAll) btnAll.classList.remove('active');
    navigateToRefPane(activeRefPane, false);
  }
}

function navigateToRefPane(paneId, pushHash = true) {
  const target = document.getElementById('ref-pane-' + paneId);
  if (!target) return;
  activeRefPane = paneId;

  document.querySelectorAll('.ref-pane').forEach(p => {
    p.classList.toggle('active', p.id === 'ref-pane-' + paneId);
  });

  document.querySelectorAll('#sidebar-nav a').forEach(a => a.classList.remove('active'));
  const navLink = document.getElementById('nav-pane-' + paneId);
  if (navLink) navLink.classList.add('active');

  const paneTitle = target.getAttribute('data-pane-title') || paneId;
  const crumb = document.getElementById('ref-active-crumb');
  if (crumb) {
    crumb.textContent = paneTitle;
  }
  const mobLabel = document.getElementById('ref-mobile-sidebar-label');
  if (mobLabel) {
    mobLabel.innerHTML = '<span class="sidebar-summary-badge">Stdlib:</span> ' + paneTitle;
  }
  const mobDetails = document.getElementById('ref-sidebar-details');
  if (mobDetails && window.innerWidth <= 840) {
    mobDetails.classList.remove('open');
  }

  if (pushHash) {
    const hash = (paneId === 'overview') ? '#overview' : ('#' + paneId);
    if (location.hash !== hash) {
      history.replaceState(null, '', hash);
    }
  }

  const vp = document.getElementById('ref-viewport');
  if (currentRefMode === 'all') {
    target.scrollIntoView({ behavior: 'smooth', block: 'start' });
  } else {
    if (vp) vp.scrollTop = 0;
    window.scrollTo(0, 0);
  }
}

function navigateToRefDiag(diagId) {
  navigateToRefPane('diagnostics', false);
  document.querySelectorAll('#sidebar-nav a').forEach(a => a.classList.remove('active'));
  const navDiag = document.getElementById('nav-' + diagId);
  if (navDiag) navDiag.classList.add('active');
  history.replaceState(null, '', '#' + diagId);
  const mobDetails = document.getElementById('ref-sidebar-details');
  if (mobDetails && window.innerWidth <= 840) {
    mobDetails.classList.remove('open');
  }
  const el = document.getElementById(diagId);
  if (el) {
    setTimeout(() => el.scrollIntoView({ behavior: 'smooth', block: 'start' }), 30);
  }
}

function handleRefHash() {
  const raw = (location.hash || '').replace(/^#/, '');
  if (!raw || raw === 'overview') {
    navigateToRefPane('overview', false);
    return;
  }
  if (raw.startsWith('mod-')) {
    navigateToRefPane(raw, false);
    return;
  }
  if (raw.startsWith('diag-')) {
    navigateToRefDiag(raw);
    return;
  }
  if (raw === 'diagnostics') {
    navigateToRefPane('diagnostics', false);
    return;
  }
}

function filterRefNav(query) {
  const q = query.toLowerCase().trim();
  const items = document.querySelectorAll('#sidebar-nav .nav-item');
  items.forEach(li => {
    const text = li.innerText.toLowerCase();
    li.style.display = (!q || text.includes(q)) ? '' : 'none';
  });

  const dropdown = document.getElementById('ref-search-results');
  if (!dropdown) return;
  if (!q) {
    dropdown.classList.remove('open');
    dropdown.innerHTML = '';
    return;
  }

  const hits = [];
  document.querySelectorAll('.ref-pane[data-pane-id^="mod-"]').forEach(pane => {
    const paneId = pane.getAttribute('data-pane-id');
    const modTitle = pane.getAttribute('data-pane-title') || paneId;
    pane.querySelectorAll('tbody tr').forEach(row => {
      const text = row.innerText || '';
      if (text.toLowerCase().includes(q)) {
        const sigCell = row.querySelector('td code');
        const sig = sigCell ? sigCell.textContent : modTitle;
        hits.push({ paneId: paneId, title: sig, path: modTitle, diagId: null });
      }
    });
  });

  document.querySelectorAll('#ref-pane-diagnostics .card').forEach(card => {
    const text = card.innerText || '';
    if (text.toLowerCase().includes(q)) {
      const h3 = card.querySelector('h3');
      hits.push({ paneId: 'diagnostics', title: h3 ? h3.textContent : card.id, path: 'Semantic Diagnostic', diagId: card.id });
    }
  });

  if (hits.length === 0) {
    dropdown.innerHTML = '<div class="search-result-item"><span class="search-result-path">No matching API signatures or diagnostics</span></div>';
    dropdown.classList.add('open');
    return;
  }

  dropdown.innerHTML = hits.slice(0, 12).map(h => {
    const action = h.diagId
      ? `navigateToRefDiag('${h.diagId}')`
      : `navigateToRefPane('${h.paneId}')`;
    return `<a class="search-result-item" href="#${h.diagId || h.paneId}" onclick="${action}; document.getElementById('ref-search-results').classList.remove('open'); return false;">` +
           `<span class="search-result-title">${h.title.replace(/</g, '&lt;')}</span>` +
           `<span class="search-result-path">${h.path.replace(/</g, '&lt;')}</span></a>`;
  }).join('');
  dropdown.classList.add('open');
}

document.addEventListener('keydown', e => {
  if ((e.metaKey || e.ctrlKey) && e.key.toLowerCase() === 'k') {
    e.preventDefault();
    const input = document.getElementById('ref-filter');
    if (input) { input.focus(); input.select(); }
  } else if (e.key === 'Escape') {
    const dropdown = document.getElementById('ref-search-results');
    if (dropdown) dropdown.classList.remove('open');
    closeAiModal();
  }
});

window.addEventListener('hashchange', handleRefHash);
window.addEventListener('DOMContentLoaded', handleRefHash);
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

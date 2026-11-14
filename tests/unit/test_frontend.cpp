#include "../test_harness.hpp"
#include "../../src/mcp.hpp"
#include "test_lsp_docgen.hpp"
#include "test_ergonomics_upstream.hpp"
#include "test_golden_codegen.hpp"
#include "test_audit_w0.hpp"
#include "test_audit_w2.hpp"
#include "test_audit_w4.hpp"
#include "test_audit_w5.hpp"
#include "test_cf183.hpp"
#include "test_cf188.hpp"
#include "test_cf192.hpp"
#include "test_cf193.hpp"
#include "test_cf194.hpp"

void test_google_cpp_and_language_design(const std::filesystem::path& rt_dir) {
    std::println("=== RUN   test_google_cpp_and_language_design");

    // 1. ARCH-CPP-04: Test zero-allocation <charconv> integer parsing
    {
        auto v1 = cfire::detail::parse_int<int>("12345");
        require(v1.has_value() && *v1 == 12345, "parse_int<int>(12345)");

        auto v2 = cfire::detail::parse_int<std::uint16_t>("8080");
        require(v2.has_value() && *v2 == 8080, "parse_int<uint16>(8080)");

        auto v3 = cfire::detail::parse_int<int>("-42");
        require(v3.has_value() && *v3 == -42, "parse_int<int>(-42)");

        auto v4 = cfire::detail::parse_int<int>("invalid");
        require(!v4.has_value(), "parse_int<int>(invalid) returns nullopt");

        auto v5 = cfire::detail::parse_int<std::uint8_t>("99999");
        require(!v5.has_value(), "parse_int<uint8>(overflow) returns nullopt");

        auto v6 = cfire::detail::parse_int<std::size_t>("");
        require(!v6.has_value(), "parse_int(empty) returns nullopt");
    }

    // 2. ARCH-CPP-05: Test CFIRE_LIFETIMEBOUND and slice / view accessors
    {
        std::string text = "hello google cpp";
        std::string_view sub = cfire::slice(text, 6, 12);
        require(sub == "google", "slice string_view");

        std::vector<int> nums = {10, 20, 30, 40};
        std::span<const int> sp = cfire::slice(nums, 1, 3);
        require(sp.size() == 2 && sp[0] == 20 && sp[1] == 30, "slice vector span");

        cfire::Error err("operation timed out");
        require(err.message() == "operation timed out", "Error::message() lifetimebound view");
    }

    // 3. ARCH-CPP-01: Multi-struct receiver method name collision resolution (LD-NEXT-03)
    {
        ScopedTempDir tmp("cfire_shapes");
        auto cf_path = tmp.file("shapes.cf", R"(package main

type Circle struct {
    r: i32
}

type Rect struct {
    w: i32
    h: i32
}

fn (self: Circle) area() -> i32 {
    return 3 * self.r * self.r
}

fn (self: Rect) area() -> i32 {
    return self.w * self.h
}

fn main() {
    c := Circle{r: 2}
    r := Rect{w: 3, h: 4}
    println("circle_area={}, rect_area={}", c.area(), r.area())
}
)");

        auto out_bin = tmp.path / "shapes_bin";
        auto plan = cfire::build::build_native(cf_path, rt_dir, out_bin);
        require(plan.has_value(), std::format("build_native(shapes.cf) with multi-struct area() methods: {}", plan ? "" : plan.error()));
        if (plan) {
            int rc = 0;
            std::string out = cfire::bridge::run_command_capture(out_bin.string(), rc);
            require(rc == 0, std::format("shapes exited with code {}", rc));
            require(out.contains("circle_area=12, rect_area=12"),
                std::format("expected 'circle_area=12, rect_area=12', got:\n{}", out));
        }
    }

    std::println("--- PASS: test_google_cpp_and_language_design");
}

void test_section2_language_ergonomics(const std::filesystem::path& rt_dir) {
    std::println("=== RUN   test_section2_language_ergonomics");

    // 1. AGENT-P1-01: String interpolation & format lowering in println / print
    {
        ScopedTempDir tmp("cfire_p1_01");
        auto cf_path = tmp.file("print_test.cf", R"(package main

fn main() {
    name := "cfire"
    println("hello {name}")
    count := 42
    println("item count={}", count)
    msg := "runtime msg"
    println(msg)
    println(100)
    print("prefix: ")
    println("done")
}
)");

        auto out_bin = tmp.path / "print_bin";
        auto plan = cfire::build::build_native(cf_path, rt_dir, out_bin);
        require(plan.has_value(), std::format("build_native(print_test.cf): {}", plan ? "" : plan.error()));
        if (plan) {
            int rc = 0;
            std::string out = cfire::bridge::run_command_capture(out_bin.string(), rc);
            require(rc == 0, std::format("print_test exited with code {}", rc));
            require(out.contains("hello cfire"), "contains 'hello cfire'");
            require(out.contains("item count=42"), "contains 'item count=42'");
            require(out.contains("runtime msg"), "contains 'runtime msg'");
            require(out.contains("100"), "contains '100'");
            require(out.contains("prefix: done"), "contains 'prefix: done'");
        }
    }

    // 2. AGENT-P1-02: Struct literal disambiguation in control-flow headers
    {
        ScopedTempDir tmp("cfire_p1_02");
        auto cf_path = tmp.file("control_flow_disambig.cf", R"(package main

type Point struct {
    x: i32
    y: i32
}

fn is_origin(p: Point) -> bool {
    return p.x == 0 && p.y == 0
}

fn check_point(p: Point) -> bool {
    return p.x == 1 && p.y == 2
}

fn main() {
    MAX_LIMIT := 10
    count := 5
    if count < MAX_LIMIT {
        println("if_limit_ok")
    }

    mut i := 0
    LIMIT := 3
    for i < LIMIT {
        i += 1
    }
    println("for_limit_ok={}", i)

    STATE := 2
    match STATE {
        1 => println("one"),
        2 => println("match_state_ok"),
        _ => println("other"),
    }

    if is_origin(Point{ x: 0, y: 0 }) {
        println("origin_ok")
    }

    if check_point(Point{ x: 1, y: 2 }) {
        println("check_point_ok")
    }

    if (Point{ x: 1, y: 2 }.x == 1) {
        println("paren_struct_ok")
    }
}
)");

        auto out_bin = tmp.path / "control_flow_bin";
        auto plan = cfire::build::build_native(cf_path, rt_dir, out_bin);
        require(plan.has_value(), std::format("build_native(control_flow_disambig.cf): {}", plan ? "" : plan.error()));
        if (plan) {
            int rc = 0;
            std::string out = cfire::bridge::run_command_capture(out_bin.string(), rc);
            require(rc == 0, std::format("control_flow exited with code {}", rc));
            require(out.contains("if_limit_ok"), "contains 'if_limit_ok'");
            require(out.contains("for_limit_ok=3"), "contains 'for_limit_ok=3'");
            require(out.contains("match_state_ok"), "contains 'match_state_ok'");
            require(out.contains("origin_ok"), "contains 'origin_ok'");
            require(out.contains("check_point_ok"), "contains 'check_point_ok'");
            require(out.contains("paren_struct_ok"), "contains 'paren_struct_ok'");
        }
    }

    // 3. AGENT-P1-03: Expression-Oriented match (MatchExpr)
    {
        ScopedTempDir tmp("cfire_p1_03");
        auto cf_path = tmp.file("match_expr_test.cf", R"(package main

type Shape enum {
    Circle(i32)
    Rect(i32, i32)
}

fn describe(s: Shape) -> str {
    return match s {
        Shape::Circle(r) => "circle",
        Shape::Rect(w, h) => "rect",
    }
}

fn http_code(verb: str) -> i32 {
    code := match verb {
        "GET" => 200,
        "POST" => 201,
        _ => 400,
    }
    return code
}

fn main() {
    c := Shape::Circle(5)
    r := Shape::Rect(10, 20)
    println("c={}, r={}, code_get={}, code_post={}, code_unk={}",
        describe(c), describe(r), http_code("GET"), http_code("POST"), http_code("DELETE"))
}
)");

        auto out_bin = tmp.path / "match_expr_bin";
        auto plan = cfire::build::build_native(cf_path, rt_dir, out_bin);
        require(plan.has_value(), std::format("build_native(match_expr_test.cf): {}", plan ? "" : plan.error()));
        if (plan) {
            int rc = 0;
            std::string out = cfire::bridge::run_command_capture(out_bin.string(), rc);
            require(rc == 0, std::format("match_expr exited with code {}", rc));
            require(out.contains("c=circle, r=rect, code_get=200, code_post=201, code_unk=400"),
                std::format("expected match expressions to evaluate, got: {}", out));
        }

        // Test formatter roundtrip on MatchExpr
        auto fmt_res = cfire::fmt::format_source(R"(package main

fn test_fn(x: str) -> i32 {
    return match x {
        "A" => 1,
        _ => 0,
    }
}
)", "fmt_match.cf");
        require(fmt_res.has_value(), "cfire::fmt::format_source on match expression");

        // Negative sema tests for MatchExpr:
        // A. Enum missing variant
        require_sema_error(R"(package main
type Shape enum { Circle(i32), Rect(i32, i32) }
fn test_fn(s: Shape) -> str {
    return match s {
        Shape::Circle(r) => "circle",
    }
}
)", "cfire/NON-EXHAUSTIVE-MATCH", "missing_rect.cf");

        // B. Primitive match missing wildcard '_' fallback
        require_sema_error(R"(package main
fn test_fn(v: str) -> i32 {
    return match v {
        "GET" => 1,
    }
}
)", "cfire/NON-EXHAUSTIVE-MATCH", "missing_wildcard.cf");
    }

    // 4. AGENT-P1-04: Compound Assignment & Bitwise Operators
    {
        ScopedTempDir tmp("cfire_p1_04");
        auto cf_path = tmp.file("bitwise_and_compound.cf", R"(package main

fn main() {
    mut x := 10
    x *= 3      // 30
    x /= 2      // 15
    x %= 4      // 3
    x <<= 2     // 12
    x >>= 1     // 6
    x |= 1      // 7
    x &= 3      // 3
    x ^= 6      // 5
    println("compound_x={}", x)

    // Bitwise operators
    a := 0xF0
    b := 0x3F
    c := (a & b) | (1 << 4) ^ (~0)
    shift := 16 >> 2
    println("c={}, shift={}", c, shift)
}
)");

        auto out_bin = tmp.path / "bitwise_bin";
        auto plan = cfire::build::build_native(cf_path, rt_dir, out_bin);
        require(plan.has_value(), std::format("build_native(bitwise_and_compound.cf): {}", plan ? "" : plan.error()));
        if (plan) {
            int rc = 0;
            std::string out = cfire::bridge::run_command_capture(out_bin.string(), rc);
            require(rc == 0, std::format("bitwise exited with code {}", rc));
            require(out.contains("compound_x=5"), std::format("expected compound_x=5, got: {}", out));
            require(out.contains("shift=4"), std::format("expected shift=4, got: {}", out));
        }

        // Negative sema test: compound assignment on immutable variable
        require_sema_error(R"(package main
fn test_fn() {
    x := 5
    x *= 2
}
)", "cfire/ASSIGN-IMMUTABLE", "imm_compound.cf");
    }

    // 5. Trailing closures and fn(...) -> R parameter types on user-defined methods
    {
        ScopedTempDir tmp("cfire_trailing_closure");
        auto cf_path = tmp.file("trailing_closure.cf", R"(package main

type Layout struct {
    y: i32
}

fn (mut l: Layout) section(title: str, body: fn()) {
    println("begin:{}", title)
    body()
    l.y += 10
    println("end:{}:{}", title, l.y)
}

fn (mut l: Layout) with_card(h: i32, draw: fn(i32)) {
    draw(l.y)
    l.y += h
}

fn main() {
    mut lay := Layout{y: 5}
    lay.section("Header") || {
        lay.with_card(20) |top_y| {
            println("card_at={}", top_y)
        }
    }
    println("final_y={}", lay.y)
}
)");

        auto out_bin = tmp.path / "trailing_closure_bin";
        auto plan = cfire::build::build_native(cf_path, rt_dir, out_bin);
        require(plan.has_value(), std::format("build_native(trailing_closure.cf): {}", plan ? "" : plan.error()));
        if (plan) {
            int rc = 0;
            std::string out = cfire::bridge::run_command_capture(out_bin.string(), rc);
            require(rc == 0, std::format("trailing_closure exited with code {}", rc));
            require(out.contains("begin:Header"), "contains 'begin:Header'");
            require(out.contains("card_at=5"), "contains 'card_at=5'");
            require(out.contains("end:Header:35"), "contains 'card_at=35'");
            require(out.contains("final_y=35"), "contains 'final_y=35'");
        }
    }

    // 6. Multi-target core:audio synthesis, WAV codec, polyphonic Sound & Stream DSP
    {
        ScopedTempDir tmp("cfire_core_audio");
        // generic_string(): forward slashes, so a Windows path pasted into the .cf string has no `\t` escapes
        auto wav_path = (tmp.path / "test_tone.wav").generic_string();
        auto cf_path = tmp.file("audio_test.cf", std::format(R"CF(package main

import "core:audio"
import "core:os"

fn main() {{
    os.setenv("CFIRE_HEADLESS_TEST", "1")
    try audio.init(48000, 2)
    defer audio.close()

    assert(audio.is_ready(), "audio engine should be ready")
    audio.set_master_volume(0.8)

    tone := audio.gen_tone(audio.Waveform::Sine, 440.0, 0.1, 0.5)
    assert_eq(tone.frame_count(), 4800, "48kHz * 0.1s == 4800 frames")
    assert_eq(tone.channels, 2, "stereo channels")

    try audio.save_wav(tone, "{}")
    loaded := try audio.load_wav("{}")
    assert_eq(loaded.frame_count(), 4800, "decoded WAV frame count matches")
    assert_eq(loaded.sample_rate, 48000, "decoded WAV sample rate matches")

    sfx := audio.load_sound_from_wave(audio.gen_sfx(audio.SfxPreset::Coin))
    sfx.set_volume(0.9)
    sfx.set_pitch(1.2)
    sfx.set_pan(-0.25)
    sfx.play()
    assert(sfx.is_playing(), "sound voice should be active")

    stream := try audio.open_stream(48000, 2)
    assert(stream.needs_data(2048), "fresh stream needs data")
    stream.push_samples(tone.samples)
    assert(audio.peak_level() > 0.05, "mixer peak level should be > 0.05")
    assert(audio.rms_level() > 0.01, "mixer rms level should be > 0.01")

    println("audio_ok:frames={{}}:ready={{}}", loaded.frame_count(), audio.is_ready())
}}
)CF", wav_path, wav_path));

        auto out_bin = tmp.path / "audio_test_bin";
        auto plan = cfire::build::build_native(cf_path, rt_dir, out_bin);
        require(plan.has_value(), std::format("build_native(audio_test.cf): {}", plan ? "" : plan.error()));
        if (plan) {
            int rc = 0;
            std::string out = cfire::bridge::run_command_capture(out_bin.string(), rc);
            require(rc == 0, std::format("audio_test exited with code {}:\n{}", rc, out));
            require(out.contains("audio_ok:frames=4800:ready=true"), "audio_test output verified");
        }
    }

    // 7. AI-First Architecture: json.schema[T](), core:ai embeddings, Vector SQLite, SSE Streaming, Structured Outputs & MCP
    {
        ScopedTempDir tmp("cfire_core_ai");
        auto cf_path = tmp.file("ai_test.cf", R"CF(package main

import "ext:ai"
import "ext:db/sqlite"
import "core:fs"
import "core:json"
import "core:net/http"

type ToolArgs struct {
    query: str  `json:"query" desc:"Search query string"`
    limit: i32  `json:"limit" desc:"Maximum results"`
    exact: bool `json:"exact"`
}

type ExtractedBug struct {
    component: str `json:"component" desc:"Subsystem name"`
    severity:  i32 `json:"severity"  desc:"Severity 1..5"`
}

type DocRow struct {
    title: str   `sql:"title"`
    score: f64   `sql:"score"`
    vec:   []f32 `sql:"vec"`
}

fn main() {
    // 1. Compile-time JSON Schema reflection with desc:"..." tags
    schema := json.schema[ToolArgs]()
    assert(schema.contains(`"type":"object"`), "schema object type")
    assert(schema.contains(`"description":"Search query string"`), "schema field description")
    assert(schema.contains(`"required":["query","limit","exact"]`), "schema required fields")

    // 2. RAG text chunking & language detection via stateful on-device Client
    local_ai := ai.local()
    chunks := ai.chunk_text("First paragraph about systems programming.\n\nSecond paragraph about vector search and embeddings in SQLite.", 60, 12)
    assert(chunks.len() >= 2, "chunk_text splits into multiple chunks")
    lang := local_ai.detect_language("Hello from the cfire systems language")
    assert(!lang.is_empty(), "detect_language returns non-empty code")

    // 3. Unified local embeddings + SQLite vector similarity search + []f32 column mapping
    v_sys := try local_ai.embed("C++23 systems programming memory safety and RAII")
    v_sql := try local_ai.embed("SQLite embedded vector similarity search and cosine distance")
    assert(v_sys.len() >= 128, "embedding dimension >= 128")
    self_sim := ai.cosine_similarity(v_sql, v_sql)
    assert(self_sim > 0.99, "self cosine similarity ~ 1.0")

    mut db := try sqlite.open(":memory:")
    defer db.close()
    try db.exec("CREATE TABLE docs (title TEXT, vec BLOB)")
    try db.exec("INSERT INTO docs VALUES (?1, ?2)", "Systems", v_sys)
    try db.exec("INSERT INTO docs VALUES (?1, ?2)", "VectorDB", v_sql)

    q_vec := try local_ai.embed("vector search in sqlite cosine distance")
    rows := try db.query[DocRow](
        "SELECT title, vec_cosine_similarity(vec, ?1) AS score, vec FROM docs ORDER BY score DESC",
        q_vec
    )
    assert_eq(rows.len(), 2, "2 rows returned")
    assert_eq(rows[0].title, "VectorDB", "VectorDB ranks #1 for vector search query")
    assert_eq(rows[0].vec.len(), v_sql.len(), "round-tripped []f32 BLOB matches dimension")

    // 4. Model Context Protocol (MCP) server registration & JSON-RPC dispatch
    mut mcp := make(ai.McpServer, "demo-mcp", "1.0.0")
    mcp.register_tool[ToolArgs]("search_docs", "Search indexed docs", |args| {
        return "hit:" + args.query
    })
    init_resp := mcp.handle_jsonrpc(`{"jsonrpc":"2.0","id":1,"method":"initialize","params":{}}`)
    assert(init_resp.contains(`"name":"demo-mcp"`), "MCP initialize response")
    list_resp := mcp.handle_jsonrpc(`{"jsonrpc":"2.0","id":2,"method":"tools/list"}`)
    assert(list_resp.contains(`"name":"search_docs"`), "MCP tools/list includes search_docs")
    call_resp := mcp.handle_jsonrpc(`{"jsonrpc":"2.0","id":3,"method":"tools/call","params":{"name":"search_docs","arguments":{"query":"simd","limit":5,"exact":true}}}`)
    assert(call_resp.contains("hit:simd"), "MCP tools/call executes typed handler")

    // 5. Local mock OpenAI-compatible HTTP server for structured outputs & SSE token streaming
    mut srv := try make(http.Server, 0)
    defer srv.stop()
    mut router := make(http.Router)
    router.post("/v1/chat/completions", |req| {
        if req.body.contains(`"stream":true`) {
            sse_payload := "data: {\"choices\":[{\"delta\":{\"content\":\"Hello \"}}]}\n\ndata: {\"choices\":[{\"delta\":{\"content\":\"AI\"}}]}\n\ndata: [DONE]\n\n"
            mut r := http.Response.text(200, sse_payload)
            r.headers["Content-Type"] = "text/event-stream"
            return r
        }
        return http.Response.json(200, `{"model":"mock-gpt","choices":[{"message":{"content":"{\"component\":\"audio\",\"severity\":4}"}}],"usage":{"prompt_tokens":12,"completion_tokens":8}}`)
    })
    srv.serve_in_background(router)

    base := "http://127.0.0.1:{srv.port()}"
    client := ai.openai_compatible(base, "test-key")
    bug := try client.generate_struct[ExtractedBug]("mock-gpt", "audio glitch")
    assert_eq(bug.component, "audio", "generate_struct parsed component")
    assert_eq(bug.severity, 4, "generate_struct parsed severity")

    mut streamed_tokens := ""
    full := try client.stream("mock-gpt", [ai.user("Say hi")], |tok| {
        streamed_tokens = streamed_tokens + tok
    })
    assert_eq(full, "Hello AI", "SSE stream accumulated full text")
    assert_eq(streamed_tokens, "Hello AI", "SSE stream callback received tokens")

    // 6. Multimodal & Document Ingestion (.docx, .pptx, .xlsx, .pdf, .html, PNG OCR & Image Embeddings)
    docx_bytes := ai.create_zip([
        ai.ZipEntry{
            name: "word/document.xml",
            content: `<w:document><w:body><w:p><w:pPr><w:pStyle w:val="Heading1"/></w:pPr><w:r><w:t>Architecture Overview</w:t></w:r></w:p><w:p><w:r><w:t>Zero-overhead C++23 lowering and memory safety.</w:t></w:r></w:p></w:body></w:document>`
        }
    ])
    try fs.write_bytes("spec.docx", docx_bytes)
    docx_doc := try ai.load_document("spec.docx")
    try fs.remove("spec.docx")
    assert_eq(docx_doc.format, "docx", "detected docx format")
    assert(docx_doc.text.contains("# Architecture Overview"), "docx heading converted to markdown")
    assert(docx_doc.text.contains("Zero-overhead C++23 lowering"), "docx paragraph extracted")

    pptx_bytes := ai.create_zip([
        ai.ZipEntry{
            name: "ppt/slides/slide1.xml",
            content: `<p:sld><p:cSld><p:spTree><p:sp><p:nvSpPr><p:nvPr><p:ph type="title"/></p:nvPr></p:nvSpPr><p:txBody><a:p><a:r><a:t>Q3 Performance</a:t></a:r></a:p></p:txBody></p:sp><p:sp><p:txBody><a:p><a:r><a:t>Sub-millisecond SIMD vector queries</a:t></a:r></a:p></p:txBody></p:sp></p:spTree></p:cSld></p:sld>`
        },
        ai.ZipEntry{
            name: "ppt/slides/slide2.xml",
            content: `<p:sld><p:cSld><p:spTree><p:sp><p:nvSpPr><p:nvPr><p:ph type="title"/></p:nvPr></p:nvSpPr><p:txBody><a:p><a:r><a:t>Multimodal Roadmap</a:t></a:r></a:p></p:txBody></p:sp><p:sp><p:txBody><a:p><a:r><a:t>Native PDF and slide deck ingestion</a:t></a:r></a:p></p:txBody></p:sp></p:spTree></p:cSld></p:sld>`
        }
    ])
    try fs.write_bytes("deck.pptx", pptx_bytes)
    pptx_doc := try ai.load_document("deck.pptx")
    assert_eq(pptx_doc.format, "pptx", "detected pptx format")
    assert_eq(pptx_doc.sections.len(), 2, "pptx extracted 2 slide sections")
    assert_eq(pptx_doc.sections[0].index, 1, "slide 1 index")
    assert_eq(pptx_doc.sections[0].title, "Q3 Performance", "slide 1 title")
    assert_eq(pptx_doc.sections[1].index, 2, "slide 2 index")
    assert_eq(pptx_doc.sections[1].title, "Multimodal Roadmap", "slide 2 title")

    slide_chunks := ai.chunk_document(pptx_doc, 120, 16)
    assert(slide_chunks.len() >= 2, "chunk_document preserves slide sections")
    assert_eq(slide_chunks[1].index, 2, "chunk_document retains slide 2 provenance index")

    xlsx_bytes := ai.create_zip([
        ai.ZipEntry{
            name: "xl/sharedStrings.xml",
            content: `<sst><si><t>Metric</t></si><si><t>Latency</t></si></sst>`
        },
        ai.ZipEntry{
            name: "xl/worksheets/sheet1.xml",
            content: `<worksheet><sheetData><row><c t="s"><v>0</v></c><c t="s"><v>1</v></c></row><row><c t="inlineStr"><is><t>P99</t></is></c><c><v>1.2</v></c></row></sheetData></worksheet>`
        }
    ])
    try fs.write_bytes("metrics.xlsx", xlsx_bytes)
    xlsx_doc := try ai.load_document("metrics.xlsx")
    try fs.remove("metrics.xlsx")
    assert_eq(xlsx_doc.format, "xlsx", "detected xlsx format")
    assert(xlsx_doc.text.contains("Metric"), "xlsx shared string resolved")
    assert(xlsx_doc.text.contains("1.2"), "xlsx numeric value extracted")

    pdf_raw := "%PDF-1.4\n1 0 obj\n<< /Length 120 >>\nstream\n/CIDInit /ProcSet findresource begin\n2 beginbfchar\n<0001> <004C>\n<0002> <0069>\nendbfchar\n1 beginbfrange\n<0010> <0012> <006E>\nendbfrange\nendstream\nendobj\n2 0 obj\n<< /Length 88 >>\nstream\nBT /F1 12 Tf 72 712 Td [<000100020010> -250 (Quarterly Financial Summary)] TJ ET\nendstream\nendobj\n%%EOF"
    try fs.write_text("report.pdf", pdf_raw)
    pdf_doc := try ai.load_document("report.pdf")
    try fs.remove("report.pdf")
    assert_eq(pdf_doc.format, "pdf", "detected pdf format")
    assert(pdf_doc.text.contains("Lin Quarterly Financial Summary"), "pdf CMap + TJ kerning array extracted")

    odt_bytes := ai.create_zip([
        ai.ZipEntry{name: "mimetype", content: "application/vnd.oasis.opendocument.text"},
        ai.ZipEntry{
            name: "content.xml",
            content: `<office:document-content><office:body><office:text><text:h>Linux Deployment</text:h><text:p>Zero external dev packages required.</text:p></office:text></office:body></office:document-content>`
        }
    ])
    try fs.write_bytes("guide.odt", odt_bytes)
    odt_doc := try ai.load_document("guide.odt")
    try fs.remove("guide.odt")
    assert_eq(odt_doc.format, "odt", "detected LibreOffice odt format")
    assert(odt_doc.text.contains("Zero external dev packages required."), "odt paragraph extracted")

    epub_bytes := ai.create_zip([
        ai.ZipEntry{name: "META-INF/container.xml", content: "<container/>"},
        ai.ZipEntry{name: "OEBPS/ch1.xhtml", content: "<html><body><h1>Chapter One</h1><p>Systems programming on Linux and macOS.</p></body></html>"}
    ])
    try fs.write_bytes("book.epub", epub_bytes)
    epub_doc := try ai.load_document("book.epub")
    try fs.remove("book.epub")
    assert_eq(epub_doc.format, "epub", "detected epub format")
    assert(epub_doc.text.contains("Systems programming on Linux and macOS."), "epub XHTML chapter extracted")

    // Construct a minimal PNG with an embedded tEXt chunk for deterministic cross-platform OCR & image embedding
    mut png_data: []u8 = [
        0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A,
        0x00, 0x00, 0x00, 0x0D, 0x49, 0x48, 0x44, 0x52,
        0x00, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00, 0x10,
        0x08, 0x02, 0x00, 0x00, 0x00, 0x90, 0x91, 0x68, 0x36,
        0x00, 0x00, 0x00, 0x15, 0x74, 0x45, 0x58, 0x74,
        0x43, 0x6F, 0x6D, 0x6D, 0x65, 0x6E, 0x74, 0x00,
        0x49, 0x6E, 0x76, 0x6F, 0x69, 0x63, 0x65, 0x20, 0x23, 0x34, 0x32, 0x30, 0x30,
        0x00, 0x00, 0x00, 0x00
    ]
    try fs.write_bytes("invoice.png", png_data)
    ocr_txt := try local_ai.ocr("invoice.png")
    assert(ocr_txt.contains("Invoice #4200"), "OCR extracted PNG text metadata")
    img_vec := try local_ai.embed_image("invoice.png")
    try fs.remove("invoice.png")
    assert_eq(img_vec.len(), 384, "embed_image returns 384-dim normalized vector")
    assert(ai.cosine_similarity(img_vec, img_vec) > 0.99, "embed_image self similarity ~ 1.0")

    // Construct two 2x2 24-bit BMP images (red vs blue) to verify pure C++23 pixel decoding & visual embedding discrimination
    mut bmp_red: []u8 = [
        0x42, 0x4D, 0x46, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x36, 0x00, 0x00, 0x00,
        0x28, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x01, 0x00,
        0x18, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00, 0x13, 0x0B, 0x00, 0x00,
        0x13, 0x0B, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0xFF, 0x00, 0x00, 0xFF, 0x00, 0x00,
        0x00, 0x00, 0xFF, 0x00, 0x00, 0xFF, 0x00, 0x00
    ]
    mut bmp_blue: []u8 = [
        0x42, 0x4D, 0x46, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x36, 0x00, 0x00, 0x00,
        0x28, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x01, 0x00,
        0x18, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00, 0x13, 0x0B, 0x00, 0x00,
        0x13, 0x0B, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0xFF, 0x00, 0x00, 0xFF, 0x00, 0x00, 0x00, 0x00,
        0xFF, 0x00, 0x00, 0xFF, 0x00, 0x00, 0x00, 0x00
    ]
    try fs.write_bytes("red.bmp", bmp_red)
    try fs.write_bytes("blue.bmp", bmp_blue)
    red_vec := try local_ai.embed_image("red.bmp")
    blue_vec := try local_ai.embed_image("blue.bmp")
    try fs.remove("red.bmp")
    try fs.remove("blue.bmp")
    assert(ai.cosine_similarity(red_vec, blue_vec) < 0.85, "pixel-decoded RGB moments distinguish red vs blue images")

    // Verify P6 PPM pixel decoding & multi-language detection across all platforms
    mut ppm_green: []u8 = [
        0x50, 0x36, 0x0A, 0x32, 0x20, 0x32, 0x0A, 0x32, 0x35, 0x35, 0x0A,
        0x00, 0xFF, 0x00, 0x00, 0xFF, 0x00,
        0x00, 0xFF, 0x00, 0x00, 0xFF, 0x00
    ]
    try fs.write_bytes("green.ppm", ppm_green)
    green_vec := try local_ai.embed_image("green.ppm")
    try fs.remove("green.ppm")
    assert_eq(green_vec.len(), 384, "PPM image embedded into 384-dim vector")

    assert_eq(local_ai.detect_language("Hello world, the quick brown fox jumps with the dog"), "en", "detect_language English")
    assert_eq(local_ai.detect_language("Hola mundo, el perro corre con la niña por el parque"), "es", "detect_language Spanish")
    assert_eq(local_ai.detect_language("こんにちは世界"), "ja", "detect_language Japanese")

    // Verify multimodal client.stream with file attachment
    att_msg := try ai.user_with_file("Stream summary of slides", "deck.pptx")
    mut stream_att_out := ""
    stream_att_full := try client.stream("mock-gpt", [att_msg], |tok| {
        stream_att_out = stream_att_out + tok
    })
    assert_eq(stream_att_full, "Hello AI", "multimodal stream with attachment succeeded")
    assert_eq(stream_att_out, "Hello AI", "multimodal stream callback accumulated tokens")

    bug_from_file := try client.generate_struct_from_file[ExtractedBug]("mock-gpt", "Inspect this slide deck", "deck.pptx")
    try fs.remove("deck.pptx")
    assert_eq(bug_from_file.component, "audio", "generate_struct_from_file parsed struct")

    println("ai_ok:top={}:bug={}:{}:stream={}:slides={}", rows[0].title, bug.component, bug.severity, full, pptx_doc.sections.len())
}
)CF");

        auto out_bin = tmp.path / "ai_test_bin";
        auto plan = cfire::build::build_native(cf_path, rt_dir, out_bin);
        require(plan.has_value(), std::format("build_native(ai_test.cf): {}", plan ? "" : plan.error()));
        if (plan) {
            int rc = 0;
            std::string out = cfire::bridge::run_command_capture(out_bin.string(), rc);
            require(rc == 0, std::format("ai_test exited with code {}:\n{}", rc, out));
            require(out.contains("ai_ok:top=VectorDB:bug=audio:4:stream=Hello AI:slides=2"),
                    std::format("unexpected ai_test output: {}", out));
        }
    }

    test_ergonomics_and_upstream_suite(rt_dir);

    std::println("--- PASS: test_section2_language_ergonomics");
}




#pragma once
// tests/unit/test_ergonomics_upstream.hpp — Developer Ergonomics, Native Fuzz/Bench & Upstream Language Conformance Tests

#include "../test_harness.hpp"

inline void test_ergonomics_and_upstream_suite(const std::filesystem::path& rt_dir) {
    // =========================================================================
    // 2.21 Developer Ergonomics Milestones (ERG-01 through ERG-05)
    // =========================================================================
    {
        // ERG-03a: Numeric range loop immutability check (negative sema test)
        require_sema_error(R"(package main
fn main() {
    for i in 0..5 {
        i += 1
    }
}
)", "cfire/ASSIGN-IMMUTABLE", "imm_range.cf");

        // ERG-03b: Return inside defer block (negative sema test)
        require_sema_error(R"(package main
fn work() -> i64 {
    defer {
        return 1
    }
    return 0
}
)", "cfire/RETURN-IN-DEFER", "ret_defer.cf");

        // ERG-05a: Use-after-move inside arbitrary expression interpolation (negative sema test)
        require_sema_error(R"(package main
fn consume(move b: []u8) {}
fn main() {
    mut buf: []u8 = []
    consume(move buf)
    s := "len={buf.len() + 1}"
    println(s)
}
)", "cfire/USE-AFTER-MOVE", "bad_interp_move.cf");

        // ERG-02, ERG-03, ERG-04, ERG-05a: End-to-end multi-file package merging, range loops,
        // block defer LIFO order, expression interpolation, and single-eval power assertions
        ScopedTempDir tmp("cfire_erg_suite");
        tmp.file("helpers.cf", R"CF(package main

fn compute_range_sum(limit: i64) -> i64 {
    mut sum: i64 = 0
    for i in 0..limit {
        sum += i
    }
    for j in 1..=3 {
        sum += j * 10
    }
    return sum
}

fn format_summary(a: i64, b: i64) -> str {
    mut items: []str = ["alpha", "beta"]
    return "sum={a + b} first={items[0]} literal={{ok}}"
}
)CF");

        auto main_cf = tmp.file("main.cf", R"CF(package main

fn run_defer_demo() {
    println("step:start")
    defer {
        println("step:defer1a")
        println("step:defer1b")
    }
    defer {
        println("step:defer2")
    }
    println("step:body")
}

fn main() {
    total := compute_range_sum(5)
    assert(total == 70)
    assert_eq(total, 70)
    msg := format_summary(12, 30)
    println("{} hex={:02x}", msg, 255)
    run_defer_demo()
}
)CF");

        tmp.file("main_test.cf", R"CF(package main

mut eval_counter: i64 = 0

fn bumped() -> i64 {
    eval_counter += 1
    return eval_counter
}

test "ergonomics suite and single-eval assertions" {
    assert(compute_range_sum(5) == 70)
    assert_eq(bumped(), 1)
    assert(bumped() == 2)
    assert_eq(eval_counter, 2)
}
)CF");

        auto erg_bin = tmp.path / "erg_bin";
        auto plan = cfire::build::build_native(main_cf, rt_dir, erg_bin);
        require(plan.has_value(), std::format("ERG-02/03/04/05 build_native(main.cf): {}", plan ? "" : plan.error()));
        if (plan) {
            int rc = 0;
            std::string out = cfire::bridge::run_command_capture(erg_bin.string(), rc);
            require(rc == 0, std::format("erg_bin exited with code {}:\n{}", rc, out));
            require(out.contains("sum=42 first=alpha literal={ok} hex=ff"),
                    std::format("ERG-05a: unexpected interpolation output: {}", out));
            require(out.contains("step:start\nstep:body\nstep:defer2\nstep:defer1a\nstep:defer1b"),
                    std::format("ERG-03b: unexpected block defer output: {}", out));
        }

        // Verify ERG-01a/b & ERG-02c via CLI commands (`cfire check`, `cfire test`, `cfire new`)
        run_cfire(std::format("check \"{}\"", tmp.path.string()))
            .assert_ok("ERG-01b: cfire check <dir> ok");

        run_cfire(std::format("test \"{}\"", tmp.path.string()))
            .assert_ok("ERG-01a/02b: cfire test <dir> ok")
            .assert_contains("PASS (1 cfire tests succeeded)", "ERG-01a: test suite summary");

        // Verify power-assertion failure formatting (`[left: ..., right: ...]`)
        ScopedTempDir fail_tmp("cfire_erg_fail_assert");
        auto fail_test_cf = fail_tmp.file("fail_test.cf", R"CF(package main
test "power assert diff" {
    lhs := 41
    rhs := 42
    assert(lhs == rhs)
}
)CF");
        run_cfire(std::format("test \"{}\"", fail_test_cf.string()))
            .assert_fail("ERG-04b: failing power assert exits non-zero")
            .assert_contains("[left: 41, right: 42]", "ERG-04b: power assert shows evaluated lhs and rhs");

        // Verify ERG-05b (`cfire new <dir>`)
        ScopedTempDir new_tmp("cfire_erg_new_proj");
        auto proj_dir = new_tmp.path / "demo_app";
        run_cfire(std::format("new \"{}\"", proj_dir.string()))
            .assert_ok("ERG-05b: cfire new <dir> ok");
        require(std::filesystem::exists(proj_dir / "build.cf") &&
                std::filesystem::exists(proj_dir / "main.cf") &&
                std::filesystem::exists(proj_dir / "main_test.cf"),
                "ERG-05b: scaffolded files created");

        run_cfire(std::format("test \"{}\"", proj_dir.string()))
            .assert_ok("ERG-05b: cfire test on scaffolded project ok")
            .assert_contains("PASS (1 cfire tests succeeded)", "ERG-05b: scaffolded project test passed");

        // TEST-01 through TEST-06: Fuzzing, Shrinking, Auto-JSON Round-Trip, FD Leak Canary, Doc-Tests, Benchmarks & LSP
        ScopedTempDir test_suite_tmp("cfire_native_test_fuzz_bench");
        auto suite_cf = test_suite_tmp.file("suite.cf", R"CF(package main

import "ext:json"

type Packet struct {
    Id:   i64  `json:"id"`
    Tag:  str  `json:"tag"`
    Flag: bool `json:"flag"`
}

/// Doubles an integer and verifies via executable doc-test.
/// ```cf
/// assert_eq(double_val(21), 42)
/// assert(double_val(0) == 0)
/// ```
fn double_val(x: i64) -> i64 {
    return x * 2
}

fuzz "double_val algebraic property" |x: i64| {
    if x > -1000000 && x < 1000000 {
        assert_eq(double_val(x), x + x)
    }
}

fuzz "struct arbitrary generation" |p: Packet| {
    s := json.stringify(p)
    assert(s.len() > 0)
}

bench "double_val throughput" |size in [16, 64]| {
    mut acc: i64 = 0
    for i in 0..size {
        acc += double_val(i)
    }
    black_box(acc)
}

fn main() {}
)CF");

        // 1. Run `cfire test` (verifies fuzzing, doc-test extraction, [auto] json round-trip synthesis, and 1-iter bench smoke pass)
        run_cfire(std::format("test \"{}\" --fuzz-runs 32 --seed 42", suite_cf.string()))
            .assert_ok("TEST-01/03b/05a: cfire test with fuzz, doctest, auto json round-trip, and bench smoke")
            .assert_contains("[doctest] double_val", "TEST-05a: executable /// doctest discovered and passed")
            .assert_contains("[auto] json round-trip: Packet", "TEST-03b: automatic JSON round-trip test synthesized and passed")
            .assert_contains("--- PASS: double_val algebraic property", "TEST-01: fuzz block passed");

        // 2. Run `cfire bench` (verifies multi-iteration statistical harness and parameter variations)
        run_cfire(std::format("bench \"{}\" --bench-time 25", suite_cf.string()))
            .assert_ok("TEST-05b/c/d: cfire bench multi-iteration parameterized benchmark")
            .assert_contains("double_val throughput/size=16", "TEST-05c: benchmark variation size=16")
            .assert_contains("double_val throughput/size=64", "TEST-05c: benchmark variation size=64")
            .assert_contains("ops/s", "TEST-05b: benchmark statistical output columns");

        // 3. Verify TEST-02: Automatic Counterexample Shrinking on failing fuzz test
        ScopedTempDir shrink_tmp("cfire_native_fuzz_shrink");
        auto shrink_cf = shrink_tmp.file("shrink_fail.cf", R"CF(package main
fuzz "shrink integer above 100" |x: i64| {
    assert(x < 100)
}
)CF");
        run_cfire(std::format("test \"{}\" --fuzz-runs 64 --seed 12345", shrink_cf.string()))
            .assert_fail("TEST-02: failing fuzz block exits non-zero")
            .assert_contains("(x=100)", "TEST-02: shrinks failing i64 down to exact minimal boundary 100")
            .assert_contains("reproduce: --seed 12345", "TEST-02: prints deterministic reproduction command");

        // 4. Verify TEST-04: Automatic Resource-Leak Canary on unclosed file descriptor
        ScopedTempDir leak_tmp("cfire_native_fd_leak");
        auto leak_cf = leak_tmp.file("leak_fail.cf", R"CF(package main
test "deliberate fd leak" {
    cpp {
        std::FILE* leaked_fp = std::fopen("/dev/null", "r");
        (void)leaked_fp;
    }
}
)CF");
        run_cfire(std::format("test \"{}\"", leak_cf.string()))
            .assert_fail("TEST-04: leaked file descriptor fails test")
            .assert_contains("resource leak canary", "TEST-04: reports open file descriptor leak canary");

        // 5. Verify TEST-06: LSP hover/symbols and `cfire outline --json` for `fuzz`, `bench`, `black_box`
        run_cfire(std::format("outline --json \"{}\"", suite_cf.string()))
            .assert_ok("TEST-06: cfire outline --json ok")
            .assert_contains("\"kind\": \"fuzz\"", "TEST-06: outline --json includes fuzz kind")
            .assert_contains("\"kind\": \"bench\"", "TEST-06: outline --json includes bench kind");

        {
            cfire::lsp::Server lsp_srv;
            std::string uri = cfire::lsp::detail::path_to_uri(suite_cf);
            std::string syms = lsp_srv.compute_document_symbols(uri);
            require(syms.contains("fuzz: double_val algebraic property") &&
                    syms.contains("bench: double_val throughput"),
                    std::format("TEST-06a: LSP documentSymbol includes fuzz and bench blocks: {}", syms));
        }

        // 6. Verify Upstream Feedback Language & Codegen Fixes (CF-013..CF-049) & Language Design Invariants (UPSTREAM-02)
        {
            // CF-039: Multiline raw string closing line with braces does not drift formatter indentation
            std::string raw_src = "package main\n\nfn build_html() -> str {\n    if true {\n        s := `line1\nline2`}\n    return \"ok\"\n}\n";
            std::string formatted = cfire::fmt::format_source(raw_src).value_or("");
            require(formatted.contains("    return \"ok\""),
                std::format("CF-039: formatter keeps 4-space indent after multiline raw string close:\n{}", formatted));

            // UPSTREAM-02 Item 1: `as` cast syntax is rejected with actionable diagnostic pointing to `T(x)` / `[]byte(x)`
            {
                auto as_res = cfire::frontend::parse_source("bad_as.cf", "package main\nfn main() {\n    x := 42 as i64\n}\n");
                require(!as_res.has_value() && as_res.error().contains("'as' cast syntax is not supported in cfire") &&
                        as_res.error().contains("i64(...)"),
                        std::format("UPSTREAM-02: expected actionable 'as' diagnostic, got: {}", as_res ? "ok" : as_res.error()));

                auto as_bytes = cfire::frontend::parse_source("bad_as_bytes.cf", "package main\nfn main() {\n    b := \"hello\" as []byte\n}\n");
                require(!as_bytes.has_value() && as_bytes.error().contains("use constructor conversion '[]byte(...)' instead"),
                        std::format("UPSTREAM-02: expected actionable '[]byte(...)' diagnostic, got: {}", as_bytes ? "ok" : as_bytes.error()));
            }

            // UPSTREAM-02 Item 2: Top-level `cpp { ... }` is rejected with actionable diagnostic
            {
                auto top_cpp = cfire::frontend::parse_source("bad_top_cpp.cf", "package main\ncpp { int x = 1; }\nfn main() {}\n");
                require(!top_cpp.has_value() && top_cpp.error().contains("top-level 'cpp { ... }' blocks are not supported in cfire"),
                        std::format("UPSTREAM-02: expected top-level cpp rejection diagnostic, got: {}", top_cpp ? "ok" : top_cpp.error()));
            }

            // UPSTREAM-02 Item 3: `const` declaration is rejected with actionable diagnostic pointing to `:=`
            {
                auto c_res = cfire::frontend::parse_source("bad_const.cf", "package main\nconst LIMIT = 42\nfn main() {}\n");
                require(!c_res.has_value() && c_res.error().contains("bindings are immutable by default"),
                        std::format("UPSTREAM-02: expected actionable 'const' diagnostic, got: {}", c_res ? "ok" : c_res.error()));
            }

            // UPSTREAM-02 Item 4: `.{ ... }` anonymous struct literal is rejected
            {
                auto dot_res = cfire::frontend::parse_source("bad_dot.cf", "package main\nfn main() {\n    x := .{ a: 1 }\n}\n");
                require(!dot_res.has_value(), "UPSTREAM-02: anonymous '.{...}' struct literal must be rejected");
            }

            // UPSTREAM-02 Item 5: `or err { ... }` on optional `T?` is rejected by `cfire/OPTIONAL-OR-ERR`
            require_sema_error(R"(package main
fn maybe_num() -> i64? {
    return none
}
fn main() {
    x := maybe_num() or err {
        return
    }
    println("{}", x)
}
)", "cfire/OPTIONAL-OR-ERR", "bad_opt_or_err.cf");

            // UPSTREAM-02 Item 6: Unguarded `opt.field` on optional `T?` is rejected by `cfire/NULL-DEREF`
            require_sema_error(R"(package main
type Item struct {
    id: i64
}
fn get_item() -> Item? {
    return none
}
fn main() {
    it := get_item()
    println("{}", it.id)
}
)", "cfire/NULL-DEREF", "bad_opt_deref.cf");

            ScopedTempDir cf_tmp("cfire_upstream_lang_fixes");
            auto sub_dir = cf_tmp.subpath("subpkg");
            std::filesystem::create_directories(sub_dir);
            (void)cfire::fs::write_text(sub_dir / "helper.cf", R"CF(package subpkg

LIMIT := 100

fn helper() -> i64 {
    return LIMIT + 7
}
)CF");

            auto upstream_cf = cf_tmp.file("upstream_suite.cf", R"CF(package main

import "ext:json"
import "ext:db/sqlite"
import "core:str"
import "core:fs"
import "./subpkg" as sp

GLOBAL_LIMIT := 42
TYPED_LIMIT: i64 = 100
LIMIT := 5

// UPSTREAM-02 Item 7: Same-named helper() in main and imported package `sp`
fn helper() -> i64 {
    return LIMIT
}

// User struct with .open() and .read() methods to verify no UFCS collision with `fs.open` / `f.read`
type CustomHandle struct {
    tag: str
}

fn (h: CustomHandle) open() -> str {
    return "custom-open:" + h.tag
}

fn (h: CustomHandle) read() -> str {
    return "custom-read:" + h.tag
}

// CF-033: Out-of-order struct declarations (Parent references Child before Child is declared)
// CF-025: Comma-separated struct fields
// CF-049 & CF-037: Untagged struct fields + optional T? field in JSON
type ParentConfig struct {
    title: str,
    child: ChildItem,
    note: str?
}

type ChildItem struct {
    export: i32,
    stdout: str
}

type PtrNode struct {
    val: i32
    next: *PtrNode = none
}

fn find_node(flag: bool) -> *PtrNode {
    if !flag {
        return none
    }
    return none
}

fn make_rows(ok: bool) -> [][]str! {
    if !ok {
        return error("rows failed")
    }
    return [["a", "b"], ["c"]]
}

fn greet(name: str) -> str {
    return "hi-" + name
}

fn classify_code(code: i32) -> str {
    return match code {
        200 => "ok"
        404 => "missing"
        _ => "other"
    }
}

fn maybe_child(ok: bool) -> ChildItem? {
    if !ok {
        return none
    }
    return ChildItem{export: 55, stdout: "narrowed"}
}

test "upstream language and codegen fixes (CF-013..CF-049)" {
    // CF-013: Canonical constructor conversions `i64(x)` and `bool(x)`
    x: i32 = 15
    y := i64(x) + GLOBAL_LIMIT + TYPED_LIMIT
    assert(y == 157)
    assert(bool(1) == true)
    assert(bool(0) == false)

    // CF-014: local immutable binding (including `input` identifier without prelude collision)
    LOCAL_K := 9
    input := "user_input_var"
    assert(LOCAL_K == 9)
    assert(input == "user_input_var")

    // UPSTREAM-02 Item 2: Canonical `[]byte(...)` and `[]u8(...)` slice type constructors
    mut b1: []byte = []byte("abc")
    b1.push(byte(100))
    assert(b1.len() == 4)
    assert(str(b1) == "abcd")
    u1 := []u8("xyz")
    assert(u1.len() == 3)
    assert(str(u1) == "xyz")

    // UPSTREAM-02 Item 7: Multi-package symbol hygiene & UFCS non-collision
    assert(helper() == 5)
    assert(sp.helper() == 107)
    ch := CustomHandle{tag: "h1"}
    assert(ch.open() == "custom-open:h1")
    assert(ch.read() == "custom-read:h1")

    // CF-019: pointer default none and return none
    n := PtrNode{val: 5}
    assert(n.next == none)
    assert(find_node(false) == none)

    // CF-020 & CF-038: C++ keywords (`export`, `delete`, `class`) and `stdout` macro as identifiers
    delete := 12
    class := "ssh"
    assert(delete == 12)
    assert(class == "ssh")

    // CF-033, CF-037, CF-049: Out-of-order structs + untagged JSON + optional T? field
    p := ParentConfig{
        title: "flint",
        child: ChildItem{export: 7, stdout: "ready"},
        note: none
    }
    raw_json := json.stringify(p)
    parsed := try json.parse[ParentConfig](raw_json)
    assert(parsed.title == "flint")
    assert(parsed.child.export == 7)
    assert(parsed.child.stdout == "ready")
    assert(parsed.note == none)

    // CF-032 & CF-040/CF-047: `[][]str!` and `or` fallback values
    rows := make_rows(true) or []
    assert(rows.len() == 2)
    empty_rows := make_rows(false) or []
    assert(empty_rows.len() == 0)

    num := str.parse_i64("not_a_number") or -1
    assert(num == -1)

    // CF-034: Nested string literals inside string interpolation
    msg := "result={greet("world")}"
    assert(msg == "result=hi-world")

    // CF-035: `match` expression returning string literals
    label := classify_code(200)
    assert(label == "ok")

    // CF-036: Optional `T?` field access after `!= none` and early-return `== none` guard
    mut opt_child: ChildItem? = ChildItem{export: 99, stdout: "opt"}
    if opt_child != none {
        assert(opt_child.export == 99)
        assert(opt_child.stdout == "opt")
    }
    guarded := maybe_child(true)
    if guarded == none {
        return
    }
    assert(guarded.export == 55)

    // CF-044: `assert(slice == [a, b])`
    items := ["alpha", "beta"]
    assert(items == ["alpha", "beta"])

    // CF-048: `or` handler block inside reassignment (`=`) and parameterless `or { ... }`
    mut parsed_val: i64 = 0
    parsed_val = str.parse_i64("1234") or {
        return
    }
    assert(parsed_val == 1234)

    mut r2: [][]str = []
    r2 = make_rows(true) or err {
        return
    }
    assert(r2.len() == 2)

    unwrapped_opt := maybe_child(true) or {
        return
    }
    assert(unwrapped_opt.export == 55)

    // CF-007: sqlite.open_encrypted + []byte BLOB parameter binding
    mut db := try sqlite.open_encrypted(":memory:", "vault-secret-key")
    defer db.close()
    try db.exec("CREATE TABLE secrets (id TEXT, cipher BLOB)")
    blob := []byte("encrypted-payload")
    try db.exec("INSERT INTO secrets VALUES (?1, ?2)", "k1", blob)
}
)CF");
            run_cfire(std::format("test \"{}\"", upstream_cf.string()))
                .assert_ok("CF-013..CF-049: upstream language & codegen suite passes")
                .assert_contains("PASS: upstream language and codegen fixes", "CF-013..CF-049 test block passed");

            // UPSTREAM-03: Comprehensive Orthogonality, build.cf Frameworks/Libraries, Type Inference & Docs/LSP/MCP Parity
            {
                // 1. Reject `: bytes` type annotation with actionable diagnostic suggesting `[]byte`
                auto bad_bytes_type = cfire::frontend::parse_source("bad_bytes_type.cf", "package main\nfn main() {\n    b: bytes = []byte(\"hi\")\n}\n");
                require(!bad_bytes_type.has_value() && bad_bytes_type.error().contains("'bytes' is not a type in cfire; use '[]byte' instead"),
                        std::format("UPSTREAM-03: expected ': bytes' rejection diagnostic, got: {}", bad_bytes_type ? "ok" : bad_bytes_type.error()));

                // 2. Reject top-level `cpp { ... }`
                auto bad_top_cpp = cfire::frontend::parse_source("bad_top_cpp.cf", "package main\ncpp {\n    int x = 1;\n}\nfn main() {}\n");
                require(!bad_top_cpp.has_value(), "UPSTREAM-03: top-level cpp { ... } must be rejected");

                // 3. Declarative `Frameworks` and `Libraries` on `build.Executable` in `build.cf`
                auto bcf_dir = cf_tmp.subpath("bcf_frameworks");
                std::filesystem::create_directories(bcf_dir);
                (void)cfire::fs::write_text(bcf_dir / "build.cf", R"CF(package main
import "core:build"

app := build.Executable{
    Name: "vault_app",
    Entry: "main.cf",
    Frameworks: ["Security", "CoreFoundation"],
    Libraries: ["z", "sqlite3"],
    UrlSchemes: ["ssh", "sftp"],
    SignIdentity: "Developer ID Application: Flint",
}
)CF");
                (void)cfire::fs::write_text(bcf_dir / "main.cf", "package main\nfn main() {}\n");
                auto parsed_bcf = cfire::build::parse_build_cf(bcf_dir / "build.cf");
                require(parsed_bcf.has_value() && parsed_bcf->executables.size() == 1, "UPSTREAM-03: parse_build_cf with Frameworks & Libraries");
                require(parsed_bcf->executables[0].frameworks == std::vector<std::string>{"Security", "CoreFoundation"},
                        "UPSTREAM-03: Executable.Frameworks parsed");
                require(parsed_bcf->executables[0].libraries == std::vector<std::string>{"z", "sqlite3"},
                        "UPSTREAM-03: Executable.Libraries parsed");
                require(parsed_bcf->executables[0].url_schemes == std::vector<std::string>{"ssh", "sftp"},
                        "CF-052: Executable.UrlSchemes parsed");
                require(parsed_bcf->executables[0].sign_identity == "Developer ID Application: Flint",
                        "CF-052: Executable.SignIdentity parsed");

                // CF-005: End-to-end .cf compilation and execution with `import "ext:net/ssh"`
                auto ssh_cf = cf_tmp.file("ssh_e2e/ssh_suite.cf", R"CF(package main

import "ext:net/ssh"

test "ssh e2e session and pty" {
    ssh.enable_mock_server(true)
    mut sess := try ssh.dial("10.0.0.42", 22, "root", 2000)
    defer sess.close()

    fp := sess.host_key_fingerprint()
    assert(fp.starts_with("SHA256:"))
    try sess.auth_publickey_mem("-----BEGIN OPENSSH PRIVATE KEY-----\ntest\n-----END OPENSSH PRIVATE KEY-----", "")

    res := try sess.exec("whoami")
    assert_eq(res.exit_code, 0)

    mut ch := try sess.open_pty("xterm-256color", 120, 36)
    defer ch.close()
    _ = try ch.write("echo pty-ok\n")
    try ch.resize_pty(132, 42)
    chunk := try ch.read(1024)
    assert(chunk.contains("pty-ok"))

    try sess.sftp_write_all("/tmp/hello.txt", "sftp-data")
    got := try sess.sftp_read_all("/tmp/hello.txt")
    assert_eq(got, "sftp-data")
}
)CF");
                run_cfire(std::format("test \"{}\"", ssh_cf.string()))
                    .assert_ok("CF-005: ext:net/ssh .cf compilation and execution passes")
                    .assert_contains("PASS: ssh e2e session and pty", "CF-005 test block passed");

                // 4. LSP & Sema type inference for `[]byte(x)`, `[]u8(x)`, `str(x)`, `readln()`, and `OrExpr` on `T?`
                auto inf_ast = cfire::frontend::parse_source("infer_test.cf", R"CF(package main
fn main() {
    b := []byte("abc")
    u := []u8("abc")
    s := str(b)
    ln := readln("> ") or "fallback"
}
)CF");
                require(inf_ast.has_value(), "UPSTREAM-03: parse infer_test.cf");
                cfire::lsp::detail::WorkspaceContext ws_ctx;
                std::unordered_map<std::string, cfire::lsp::detail::LocalBindingInfo> env_map;
                const auto& body = inf_ast->functions[0].body;
                require(body.size() == 4, "UPSTREAM-03: 4 var decls in infer_test.cf");
                const auto* vd_b = std::get_if<cfire::frontend::VarDeclStmt>(&body[0]->node);
                const auto* vd_u = std::get_if<cfire::frontend::VarDeclStmt>(&body[1]->node);
                const auto* vd_s = std::get_if<cfire::frontend::VarDeclStmt>(&body[2]->node);
                const auto* vd_ln = std::get_if<cfire::frontend::VarDeclStmt>(&body[3]->node);
                require(vd_b && cfire::lsp::detail::infer_expr_cf_type(vd_b->init, *inf_ast, ws_ctx, env_map) == "[]byte",
                        "UPSTREAM-03: infer_expr_cf_type([]byte(\"abc\")) == []byte");
                require(vd_u && cfire::lsp::detail::infer_expr_cf_type(vd_u->init, *inf_ast, ws_ctx, env_map) == "[]u8",
                        "UPSTREAM-03: infer_expr_cf_type([]u8(\"abc\")) == []u8");
                require(vd_s && cfire::lsp::detail::infer_expr_cf_type(vd_s->init, *inf_ast, ws_ctx, env_map) == "str",
                        "UPSTREAM-03: infer_expr_cf_type(str(b)) == str");
                require(vd_ln && cfire::lsp::detail::infer_expr_cf_type(vd_ln->init, *inf_ast, ws_ctx, env_map) == "str",
                        "UPSTREAM-03: infer_expr_cf_type(readln() or \"fallback\") unwraps str? to str");

                // 5. Catalog, MCP, and LLM Reference Completeness Invariant Check
                auto has_cat_fn = [](std::string_view mod_path, std::string_view fn_name) -> bool {
                    for (const auto& mod : cfire::doc::stdlib_catalog()) {
                        if (mod.import_path == mod_path) {
                            for (const auto& fn : mod.functions) {
                                if (fn.name == fn_name) return true;
                            }
                        }
                    }
                    return false;
                };
                require(has_cat_fn("core:cli", "prompt_password") && has_cat_fn("core:cli", "subcommand") && has_cat_fn("core:cli", "positional"),
                        "UPSTREAM-03: core:cli functions in stdlib_catalog");
                require(has_cat_fn("core:crypto/aes", "generate_key") && has_cat_fn("core:crypto/aes", "encrypt") && has_cat_fn("core:crypto/aes", "decrypt"),
                        "UPSTREAM-03: core:crypto/aes functions in stdlib_catalog");
                require(has_cat_fn("core:crypto/hmac", "pbkdf2_sha256"),
                        "UPSTREAM-03: core:crypto/hmac pbkdf2_sha256 in stdlib_catalog");
                require(has_cat_fn("core:os", "args") && has_cat_fn("core:os", "exit") && has_cat_fn("core:os", "getenv_opt") &&
                        has_cat_fn("core:os", "term_size") && has_cat_fn("core:os", "make_raw") && has_cat_fn("core:os", "on_signal") &&
                        !has_cat_fn("core:os", "read_line"),
                        "UPSTREAM-03: core:os functions in stdlib_catalog (and removed read_line absent)");
                require(has_cat_fn("core:str", "parse_i64") && has_cat_fn("core:str", "parse_f64") && has_cat_fn("core:str", "parse_bool") && has_cat_fn("core:str", "find"),
                        "UPSTREAM-03: core:str parse_* and find in stdlib_catalog");
                require(has_cat_fn("core:sync", "Channel.try_recv") && has_cat_fn("core:sync", "Channel.recv_timeout") &&
                        has_cat_fn("core:sync", "Mutex") && has_cat_fn("core:sync", "Mutex.with_lock"),
                        "UPSTREAM-03: core:sync try_recv, recv_timeout, and Mutex in stdlib_catalog");
                require(has_cat_fn("ext:webui", "App.bind_async") && has_cat_fn("ext:webui", "App.emit_bytes") &&
                        has_cat_fn("ext:webui", "App.clipboard_set") && has_cat_fn("ext:webui", "App.clipboard_set_secret") &&
                        has_cat_fn("ext:webui", "App.set_icon") && has_cat_fn("ext:webui", "App.add_menu_item") &&
                        has_cat_fn("ext:webui", "App.monospace_fonts"),
                        "UPSTREAM-03: ext:webui async/binary/clipboard/secret/icon/menu/fonts in stdlib_catalog");
                require(has_cat_fn("ext:net/ssh", "dial") && has_cat_fn("ext:net/ssh", "Session.auth_publickey_mem") &&
                        has_cat_fn("ext:net/ssh", "Session.open_pty") && has_cat_fn("ext:net/ssh", "Channel.resize_pty") &&
                        has_cat_fn("ext:net/ssh", "Session.sftp_write_all") && has_cat_fn("ext:net/ssh", "Session.open_forward"),
                        "CF-005: ext:net/ssh functions in stdlib_catalog");

                std::string llm_ref = cfire::doc::generate_llm_reference();
                require(llm_ref.contains("[]byte(s_or_slice)") && llm_ref.contains("readln(prompt: str = \"\") -> str?") &&
                        llm_ref.contains("cfire/OPTIONAL-OR-ERR") && !llm_ref.contains("`bytes`, `[]byte`"),
                        "UPSTREAM-03: generate_llm_reference includes canonical []byte(x), readln, and OPTIONAL-OR-ERR");

                // 6. Follow-up Upstream Review Fixes (CF-027 & E1..E7)
                // E3: Reserved parameter-intent keywords `out`, `move`, and `mut` as variable names emit actionable diagnostic
                {
                    auto bad_out = cfire::frontend::parse_source("bad_out.cf", "package main\nfn main() {\n    out := 1\n}\n");
                    require(!bad_out.has_value() && bad_out.error().contains("'out' is a reserved parameter-intent keyword"),
                            std::format("E3: expected reserved keyword diagnostic for 'out := 1', got: {}", bad_out ? "ok" : bad_out.error()));

                    auto bad_move = cfire::frontend::parse_source("bad_move.cf", "package main\nfn main() {\n    move := 1\n}\n");
                    require(!bad_move.has_value() && bad_move.error().contains("'move' is a reserved parameter-intent keyword"),
                            std::format("E3: expected reserved keyword diagnostic for 'move := 1', got: {}", bad_move ? "ok" : bad_move.error()));
                }

                // E4 & E5: `core:build` module in stdlib_catalog and `Channel.close` mutable receiver signature
                require(has_cat_fn("core:build", "Executable") && has_cat_fn("core:build", "Library") &&
                        has_cat_fn("core:build", "Step") && has_cat_fn("core:build", "CMake"),
                        "E4: core:build declarative builders documented in stdlib_catalog");
                {
                    bool found_mut_close = false;
                    for (const auto& mod : cfire::doc::stdlib_catalog()) {
                        if (mod.import_path == "core:sync") {
                            for (const auto& fn : mod.functions) {
                                if (fn.name == "Channel.close" && fn.cf_sig.contains("mut ch: Channel[T]")) {
                                    found_mut_close = true;
                                }
                            }
                        }
                    }
                    require(found_mut_close, "E5: core:sync Channel.close documented with mut receiver");
                }

                // E6: `os.exit(code)` recognized as diverging inside `or err { ... }` handler block
                require_sema_ok(R"(package main
import "core:os"
fn fallible() -> i32! { return 42 }
fn main() {
    val := fallible() or err {
        os.exit(1)
    }
    assert(val == 42)
}
)", "test_os_exit_diverges.cf");

                // CF-027: Closure returning both `return error(...)` inside `or err` handler and `return v` value
                auto cf027_file = cf_tmp.file("cf027_closure.cf", R"CF(package main

fn check(s: str) -> str! {
    if s == "" {
        return error("empty")
    }
    return s
}

fn apply(cb: fn(str) -> str!) -> str! {
    return try cb("hello")
}

test "CF-027 closure mixed error and value return inside or err block" {
    res := try apply(|params: str| {
        v := check(params) or err {
            return error("wrapped: " + err.message())
        }
        return v + "!"
    })
    assert_eq(res, "hello!")
}
)CF");
                run_cfire(std::format("test \"{}\"", cf027_file.string()))
                    .assert_ok("CF-027: closure with or err { return error(...) } and value return compiles and passes");

                // E2: `cfire run` preserves non-zero process exit code (not truncated to 0 via & 0xFF on wait status)
                auto exit3_cf = cf_tmp.file("exit3.cf", R"CF(package main
import "core:os"
fn main() {
    os.exit(3)
}
)CF");
                auto exit3_res = run_cfire(std::format("run \"{}\"", exit3_cf.string()));
                require(exit3_res.exit_code == 3,
                        std::format("E2: expected cfire run os.exit(3) to return exit code 3, got {}", exit3_res.exit_code));
            }
        }
    }
}

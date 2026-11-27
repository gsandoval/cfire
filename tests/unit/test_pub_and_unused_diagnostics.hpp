#pragma once
// tests/unit/test_pub_and_unused_diagnostics.hpp — v0.13.0 package visibility (`pub`)
// and compile-time unused hygiene warnings (`UNUSED-IMPORT`, `UNUSED-VAR`, `UNUSED-PARAM`, `UNUSED-SYMBOL`).

#include "../test_harness.hpp"

void test_pub_and_unused_diagnostics(const std::filesystem::path& rt_dir) {
    std::println("=== RUN   test_pub_and_unused_diagnostics");

    const std::string cfire_bin = cfire::bridge::shell_quote(
        std::filesystem::absolute(cfire_exe()).string());

    auto has_diag = [](const std::vector<cfire::diag::StructuredDiagnostic>& diags,
                       std::string_view code,
                       std::string_view substr = "") -> bool {
        for (const auto& d : diags) {
            if (d.code == code && (substr.empty() || d.message.contains(substr))) {
                return true;
            }
        }
        return false;
    };

    auto count_diag = [](const std::vector<cfire::diag::StructuredDiagnostic>& diags,
                         std::string_view code) -> int {
        int n = 0;
        for (const auto& d : diags) {
            if (d.code == code) ++n;
        }
        return n;
    };

    // 1. `pub` positive cross-package access, inherited method/field/ctor visibility, and white-box sibling/`_test.cf` access
    {
        ScopedTempDir ws("cfire_pub_pos_whitebox");
        ws.file("dom/dom.cf", R"CF(package dom

pub TOKEN := "dom-ok"
local_secret := 77

pub type Kind enum {
    Alpha,
    Beta,
}

type SecretKind enum {
    Hidden,
}

pub type Printable interface {
    fn render() -> str
}

type PrivateRole interface {
    fn secret_tag() -> str
}

pub type Widget struct {
    name: str
    kind: Kind
}

type HiddenBox struct {
    val: i64
}

fn (Widget) make(name: str, kind: Kind) -> Widget {
    return Widget{name: name, kind: kind}
}

fn (w: Widget) render() -> str {
    return w.name + ":" + format_kind(w.kind)
}

fn (w: Widget) label() -> str {
    return TOKEN + ":" + w.name
}

fn format_kind(k: Kind) -> str {
    return match k {
        Alpha => "alpha",
        Beta => "beta",
    }
}

fn format_secret[P: Printable, R: PrivateRole](s: SecretKind, p: P, r: R, h: HiddenBox) -> str {
    return match s {
        Hidden => format("{}:{}:{}", p.render(), r.secret_tag(), h.val),
    }
}

fn (h: HiddenBox) secret_tag() -> str {
    return format("box-{}", h.val)
}

pub fn make_summary(w: Widget) -> str {
    hb := HiddenBox{val: local_secret}
    return format_secret(SecretKind::Hidden(), w, hb, hb)
}
)CF");
        ws.file("dom/helper.cf", R"CF(package dom

fn sibling_only_sum() -> i64 {
    h := HiddenBox{val: local_secret}
    return h.val + 3
}
)CF");
        ws.file("dom/dom_test.cf", R"CF(package dom

test "whitebox same package tests access private decls" {
    assert_eq(sibling_only_sum(), i64(80))
    assert_eq(HiddenBox{val: 5}.secret_tag(), "box-5")
    assert(SecretKind::Hidden() == SecretKind::Hidden())
}
)CF");
        auto main_cf = ws.file("app/main.cf", R"CF(package main

import "../dom"

fn print_it[T: dom.Printable](x: T) -> str {
    return x.render()
}

fn main() {
    w := make(dom.Widget, "gear", dom.Kind::Beta())
    println("{}|{}|{}|{}", w.name, w.label(), print_it(w), dom.make_summary(w))
}
)CF");

        auto chk = run_cfire(std::format("check --verify-cpp \"{}\"", main_cf.string()));
        chk.assert_ok("cross-pkg pub access compiles cleanly");
        require(!chk.output.contains("cfire/PRIVATE-SYMBOL"),
            std::format("expected no PRIVATE-SYMBOL errors, got:\n{}", chk.output));

        auto run_res = run_cfire(std::format("run --no-cache \"{}\"", main_cf.string()));
        run_res.assert_ok("cross-pkg pub run succeeds")
            .assert_contains("gear|dom-ok:gear|gear:beta|gear:beta:box-77:77", "cross-pkg pub output matches");

        auto test_res = run_cfire(std::format("test --no-cache \"{}\"", (ws.path / "dom").string()));
        test_res.assert_ok("same-pkg white-box dom_test.cf passes")
            .assert_contains("PASS: whitebox same package tests access private decls", "dom_test passed");

        // Formatter idempotence on `pub` + `cfire outline --json` `"pub"` field (including inherited method visibility)
        std::string dom_raw = cfire::fs::read_text(ws.path / "dom" / "dom.cf").value_or("");
        auto fmt1 = cfire::fmt::format_source(dom_raw, (ws.path / "dom" / "dom.cf").string());
        require(fmt1.has_value(), "format dom.cf with pub decls");
        if (fmt1) {
            auto fmt2 = cfire::fmt::format_source(*fmt1, (ws.path / "dom" / "dom.cf").string());
            require(fmt2.has_value() && *fmt2 == *fmt1, "cfire fmt is idempotent on pub type/fn/global");
            require(fmt1->contains("pub TOKEN :=") && fmt1->contains("pub type Widget struct") &&
                    fmt1->contains("pub fn make_summary"),
                std::format("cfire fmt preserves pub prefix, got:\n{}", *fmt1));
        }

        auto out_res = run_cfire(std::format("outline --json \"{}\"", (ws.path / "dom" / "dom.cf").string()));
        out_res.assert_ok("cfire outline --json succeeds on dom.cf")
            .assert_contains(R"("name": "Widget")", "outline includes Widget")
            .assert_contains(R"("name": "HiddenBox")", "outline includes HiddenBox")
            .assert_contains(R"("pub": true)", "outline reports pub: true")
            .assert_contains(R"("pub": false)", "outline reports pub: false");
    }

    // 2. `pub` negative enforcement: `PRIVATE-SYMBOL`, `pub` on method/ctor syntax error, `REDUNDANT-PUB`, and `--only cfire/PRIVATE-SYMBOL`
    {
        ScopedTempDir ws("cfire_pub_neg_and_fix");
        auto dep_cf = ws.file("dep/dep.cf", R"CF(package dep

SECRET := 11

type PrivStruct struct {
    x: i64
}

fn priv_fn(v: i64) -> i64 {
    return v + SECRET
}
)CF");
        auto main_cf = ws.file("app/main.cf", R"CF(package main

import "../dep"

fn main() {
    a := dep.PrivStruct{x: 4}
    b := dep.priv_fn(a.x)
    println("{} {}", b, dep.SECRET)
}
)CF");

        auto bad_chk = run_cfire(std::format("check \"{}\"", main_cf.string()));
        bad_chk.assert_fail("cross-pkg access to private symbols fails")
            .assert_contains("cfire/PRIVATE-SYMBOL", "emits cfire/PRIVATE-SYMBOL");

        // Cross-file migration via `cfire fix --only cfire/PRIVATE-SYMBOL`
        auto fix_pub = run_cfire(std::format("fix --only cfire/PRIVATE-SYMBOL \"{}\"", main_cf.string()));
        fix_pub.assert_ok("cfire fix --only cfire/PRIVATE-SYMBOL succeeds");

        std::string fixed_dep = cfire::fs::read_text(dep_cf).value_or("");
        require(fixed_dep.contains("pub SECRET := 11") &&
                fixed_dep.contains("pub type PrivStruct struct") &&
                fixed_dep.contains("pub fn priv_fn(v: i64) -> i64"),
            std::format("expected cross-file fix to prepend `pub `, got:\n{}", fixed_dep));

        run_cfire(std::format("check \"{}\"", main_cf.string()))
            .assert_ok("app/main.cf checks cleanly after --only cfire/PRIVATE-SYMBOL migration");

        // Reject `pub` on method or constructor as `cfire/SYNTAX`
        auto method_pub_cf = ws.file("bad_method_pub.cf", R"CF(package dep
pub type Foo struct { x: i64 }
pub fn (f: Foo) get() -> i64 { return f.x }
)CF");
        run_cfire(std::format("check \"{}\"", method_pub_cf.string()))
            .assert_fail("pub on method rejected")
            .assert_contains("cfire/SYNTAX", "pub on method reports cfire/SYNTAX");

        auto ctor_pub_cf = ws.file("bad_ctor_pub.cf", R"CF(package dep
pub type Foo struct { x: i64 }
pub fn (Foo) make(x: i64) -> Foo { return Foo{x: x} }
)CF");
        run_cfire(std::format("check \"{}\"", ctor_pub_cf.string()))
            .assert_fail("pub on constructor rejected")
            .assert_contains("cfire/SYNTAX", "pub on ctor reports cfire/SYNTAX");

        // `cfire/REDUNDANT-PUB` in `package main`, but NOT in `build.cf`
        cfire::analysis::CheckOptions copts;
        copts.verify_cpp_backend = false;
        copts.resolve_sibling_packages = false;
        auto red_diags = cfire::analysis::check_source("main_red.cf", R"CF(package main
pub VAL := 5
pub fn main() { println("{}", VAL) }
)CF", copts);
        require(count_diag(red_diags, "cfire/REDUNDANT-PUB") >= 2,
            "expected cfire/REDUNDANT-PUB warnings on pub decls in package main");

        auto build_cf_path = ws.file("build.cf", R"CF(package main
import "core:build"
pub app := build.Executable{Name: "app", Entry: "main.cf"}
)CF");
        auto build_diags = cfire::analysis::check_source(build_cf_path.string(),
            cfire::fs::read_text(build_cf_path).value_or(""), copts);
        require(!has_diag(build_diags, "cfire/REDUNDANT-PUB"),
            "build.cf must NOT emit cfire/REDUNDANT-PUB");
    }

    // 3. `cfire/UNUSED-IMPORT`: core:*, aliases, relative pkgs, duplicates, per-file precision, `import cpp`, and `cfire fix`
    {
        ScopedTempDir ws("cfire_unused_import");
        ws.file("dom/dom.cf", "package dom\npub fn one() -> i64 { return 1 }\npub fn two() -> i64 { return 2 }\n");
        ws.file("extra/extra.cf", "package extra\npub fn ten() -> i64 { return 10 }\n");

        // Multi-file package precision: a.cf uses `core:math` & `../extra`, b.cf does not -> warn only on b.cf
        auto a_cf = ws.file("pkg_ab/a.cf", R"CF(package pkg_ab
import "core:math"
import "../extra"

pub fn run_a() -> i64 {
    return i64(math.abs(-2.0)) + extra.ten()
}
)CF");
        auto b_cf = ws.file("pkg_ab/b.cf", R"CF(package pkg_ab
import "core:math"
import "core:fs" as files
import "../dom"
import "../dom"

pub fn run_b() -> i64 {
    return dom.two()
}
)CF");
        ws.file("pkg_cpp_used/inc.h", "#pragma once\nstatic inline int cpp_ok(void) { return 3; }\n");
        auto cpp_used_cf = ws.file("pkg_cpp_used/cpp_used.cf", R"CF(package pkg_cpp_used
import cpp "inc.h"
pub fn use_cpp() -> i64 {
    mut v: i64 = 0
    cpp { v = cpp_ok(); }
    return v
}
)CF");
        ws.file("pkg_cpp_unused/inc.h", "#pragma once\nstatic inline int cpp_ok(void) { return 3; }\n");
        auto cpp_unused_cf = ws.file("pkg_cpp_unused/cpp_unused.cf", R"CF(package pkg_cpp_unused
import cpp "inc.h"
pub fn no_cpp() -> i64 { return 1 }
)CF");

        cfire::analysis::CheckOptions popts;
        popts.verify_cpp_backend = false;
        popts.resolve_sibling_packages = true;

        auto b_diags = cfire::analysis::check_source(b_cf.string(), cfire::fs::read_text(b_cf).value_or(""), popts);
        require(has_diag(b_diags, "cfire/UNUSED-IMPORT", "core:math") &&
                has_diag(b_diags, "cfire/UNUSED-IMPORT", "core:fs") &&
                has_diag(b_diags, "cfire/UNUSED-IMPORT", "duplicate"),
            "b.cf warns on unused core:math, aliased core:fs, and duplicate ../dom");
        for (const auto& d : b_diags) {
            if (d.code == "cfire/UNUSED-IMPORT") {
                require(d.file.contains("b.cf") && !d.file.contains("a.cf"),
                    std::format("UNUSED-IMPORT diagnostic attributed to b.cf, not a.cf (got '{}')", d.file));
            }
        }
        auto a_diags = cfire::analysis::check_source(a_cf.string(), cfire::fs::read_text(a_cf).value_or(""), popts);
        for (const auto& d : a_diags) {
            if (d.code == "cfire/UNUSED-IMPORT") {
                require(!d.file.contains("a.cf"),
                    std::format("a.cf emits zero UNUSED-IMPORT warnings on itself (got '{}')", d.file));
            }
        }

        auto cpp_u_diags = cfire::analysis::check_source(cpp_used_cf.string(), cfire::fs::read_text(cpp_used_cf).value_or(""), popts);
        require(!has_diag(cpp_u_diags, "cfire/UNUSED-IMPORT"),
            "import cpp exempt when file has cpp { ... } block");

        auto cpp_un_diags = cfire::analysis::check_source(cpp_unused_cf.string(), cfire::fs::read_text(cpp_unused_cf).value_or(""), popts);
        require(has_diag(cpp_un_diags, "cfire/UNUSED-IMPORT"),
            "import cpp without cpp {} or cpp.* usage warns UNUSED-IMPORT");

        // Plain `cfire fix` (`MachineApplicable`) deletes unused imports, including EOF without trailing newline,
        // and skips deletion when an unresolved error exists in the same file.
        auto fix_eof = ws.file("fix_eof.cf", "package main\nimport \"core:math\"\nfn main() {}\n");
        run_cfire(std::format("fix \"{}\"", fix_eof.string())).assert_ok("cfire fix removes unused import");
        std::string fixed_eof_txt = cfire::fs::read_text(fix_eof).value_or("");
        require(!fixed_eof_txt.contains("core:math"),
            std::format("expected unused core:math removed by plain cfire fix, got:\n{}", fixed_eof_txt));

        auto fix_last_line = ws.file("fix_last_line.cf", "package main\nimport \"core:math\"");
        run_cfire(std::format("fix \"{}\"", fix_last_line.string())).assert_ok("cfire fix handles last-line EOF unused import");
        std::string fixed_last_txt = cfire::fs::read_text(fix_last_line).value_or("");
        require(!fixed_last_txt.contains("core:math"),
            std::format("expected last-line unused import removed, got:\n{}", fixed_last_txt));

        auto fix_guarded = ws.file("fix_guarded.cf", "package main\nimport \"core:math\"\nfn main() { _ = missing_ident }\n");
        (void)run_cfire(std::format("fix \"{}\"", fix_guarded.string()));
        std::string guarded_txt = cfire::fs::read_text(fix_guarded).value_or("");
        require(guarded_txt.contains("import \"core:math\""),
            "cfire fix skips import deletion when file has unresolved identifier diagnostic");
    }

    // 4. `cfire/UNUSED-VAR` & `cfire/UNUSED-PARAM`: locals, write-only mut, shadow, destructure, for/if/match/or, `_`, EditorOnly + LSP codeAction
    {
        ScopedTempDir ws("cfire_unused_var_param");
        const std::string vp_src = R"CF(package main

type Pair struct { x: i64 }
fn (self: Pair) peek(used_p: i64, dead_p: i64, _ok_p: i64) -> i64 {
    return self.x + used_p
}

fn mut_param_test(mut out_p: i64) {
    out_p = 42
}

fn get_pair() -> (i64, i64) {
    return (1, 2)
}

fn maybe_num(flag: bool) -> i64? {
    if flag { return 5 }
    return none
}

fn fallible() -> i64! {
    return error("boom")
}

type Msg enum { Val(i64), Empty }

fn main() {
    unread_loc := 10
    mut write_only := 1
    write_only = 2

    shad := 100
    if true {
        shad := 200
        println("{}", shad)
    }

    used_d, dead_d := get_pair()
    println("{}", used_d)

    for idx, val in [10, 20] {
        println("{}", idx)
    }

    if unw := maybe_num(true) {
        _ = 1
    }

    m := Msg::Val(9)
    match m {
        Val(bound_unused) => {}
        Empty => {}
    }

    _ = fallible() or err {
        println("or failed")
    }

    _suppressed := 123
    cb := |dead_cp: i64| {
        println("cb")
    }
    cb(1)
    mut _dummy := 0
    mut_param_test(mut _dummy)
    p := Pair{x: 3}
    println("{}", p.peek(1, 2, 3))
}
)CF";
        auto vp_cf = ws.file("vp.cf", vp_src);
        cfire::analysis::CheckOptions opts;
        opts.verify_cpp_backend = false;
        opts.resolve_sibling_packages = false;
        auto diags = cfire::analysis::check_source(vp_cf.string(), vp_src, opts);

        // Expected UNUSED-VAR
        require(has_diag(diags, "cfire/UNUSED-VAR", "'unread_loc'"), "warns on unread local");
        require(has_diag(diags, "cfire/UNUSED-VAR", "'write_only'"), "warns on write-only mut local");
        require(has_diag(diags, "cfire/UNUSED-VAR", "'shad'"), "warns on shadowed outer local unread before shadow");
        require(has_diag(diags, "cfire/UNUSED-VAR", "'dead_d'"), "warns on unused destructured variable");
        require(has_diag(diags, "cfire/UNUSED-VAR", "'val'"), "warns on unused for-loop value");
        require(has_diag(diags, "cfire/UNUSED-VAR", "'unw'"), "warns on unused if-unwrap variable");
        require(has_diag(diags, "cfire/UNUSED-VAR", "'bound_unused'"), "warns on unused match pattern binding");
        require(has_diag(diags, "cfire/UNUSED-VAR", "'err'"), "warns on unused `or err` binding");
        require(!has_diag(diags, "cfire/UNUSED-VAR", "'_suppressed'"), "`_name` suppresses UNUSED-VAR");
        require(!has_diag(diags, "cfire/UNUSED-VAR", "'used_d'"), "used destructured var does not warn");
        require(!has_diag(diags, "cfire/UNUSED-VAR", "'idx'"), "used loop idx does not warn");

        // Expected UNUSED-PARAM
        require(has_diag(diags, "cfire/UNUSED-PARAM", "'dead_p'"), "warns on unused method parameter");
        require(has_diag(diags, "cfire/UNUSED-PARAM", "'dead_cp'"), "warns on unused closure parameter");
        require(!has_diag(diags, "cfire/UNUSED-PARAM", "'self'"), "`self` receiver exempt from UNUSED-PARAM");
        require(!has_diag(diags, "cfire/UNUSED-PARAM", "'_ok_p'"), "`_name` suppresses UNUSED-PARAM");
        require(!has_diag(diags, "cfire/UNUSED-PARAM", "'out_p'"), "`mut` param written to exempt from UNUSED-PARAM");

        // Plain `cfire fix` does NOT apply EditorOnly `_` renames automatically
        (void)run_cfire(std::format("fix \"{}\"", vp_cf.string()));
        std::string after_fix = cfire::fs::read_text(vp_cf).value_or("");
        require(after_fix.contains("unread_loc := 10"), "plain cfire fix preserves EditorOnly unused local");

        // Verify LSP codeAction exposes `_` prefix quickfix for UNUSED-VAR
        cfire::lsp::Server srv;
        std::filesystem::create_directory(ws.path / ".git");
        auto uri = cfire::lsp::detail::path_to_uri(vp_cf);
        auto rpc_frame = [](std::string_view body) {
            return std::format("Content-Length: {}\r\n\r\n{}", body.size(), body);
        };
        // Find 0-based line of `unread_loc := 10`
        int unread_line = 0;
        {
            std::size_t pos = vp_src.find("unread_loc := 10");
            for (std::size_t i = 0; i < pos; ++i) if (vp_src[i] == '\n') ++unread_line;
        }
        std::stringstream lsp_in, lsp_out;
        lsp_in << rpc_frame(R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{}})");
        lsp_in << rpc_frame(std::format(
            R"({{"jsonrpc":"2.0","method":"textDocument/didOpen","params":{{"textDocument":{{"uri":"{}","text":"{}"}}}}}})",
            cfire::lsp::detail::json_escape(uri), cfire::lsp::detail::json_escape(vp_src)));
        lsp_in << rpc_frame(std::format(
            R"({{"jsonrpc":"2.0","id":2,"method":"textDocument/codeAction","params":{{"textDocument":{{"uri":"{}"}},"range":{{"start":{{"line":{},"character":4}},"end":{{"line":{},"character":14}}}}}}}})",
            cfire::lsp::detail::json_escape(uri), unread_line, unread_line));
        lsp_in << rpc_frame(R"({"jsonrpc":"2.0","id":9,"method":"shutdown","params":null})");
        lsp_in << rpc_frame(R"({"jsonrpc":"2.0","method":"exit","params":null})");
        srv.handle_stream(lsp_in, lsp_out);
        std::string ca_resp = lsp_out.str();
        require(ca_resp.contains("Prefix 'unread_loc' with '_'") &&
                ca_resp.contains(R"("newText":"_")") &&
                ca_resp.contains("cfire/UNUSED-VAR"),
            std::format("LSP codeAction offers `_` rename for UNUSED-VAR, got:\n{}", ca_resp));
    }

    // 5. `cfire/UNUSED-SYMBOL`, exemptions, stderr warning delivery on build/run/test, and TestResultCache warning replay
    {
        ScopedTempDir ws("cfire_unused_symbol_and_cache");
        // Library package: test-only helper is used in sibling *_test.cf -> NO UNUSED-SYMBOL on non-test build,
        // while truly dead private symbols (including self-recursive private fn) DO warn.
        ws.file("lib/lib.cf", R"CF(package lib

DEAD_GLOB := 1
_IGNORED_GLOB := 2
pub PUB_GLOB := 3

type DeadType struct { x: i64 }
fn (d: DeadType) dead_method() -> i64 { return d.x }

type DeadEnum enum { A, B }
type DeadIface interface { fn f() }

pub type LivePub struct { v: i64 }
fn (l: LivePub) unused_but_on_pub_type() -> i64 { return l.v }

type Iface interface { fn ping() -> i64 }
type Impl struct { k: i64 }
fn (i: Impl) ping() -> i64 { return i.k }

fn dead_self_rec(n: i64) -> i64 {
    if n <= 0 { return 0 }
    return dead_self_rec(n - 1)
}

fn _suppressed_fn() -> i64 { return 9 }

fn test_only_helper() -> i64 { return 42 }

pub fn public_entry() -> i64 {
    x := Impl{k: 5}
    return use_iface(x) + PUB_GLOB
}

fn use_iface[T: Iface](v: T) -> i64 {
    return v.ping()
}
)CF");
        ws.file("lib/lib_test.cf", R"CF(package lib

test "uses test_only_helper" {
    dead_test_local := 1
    assert_eq(test_only_helper(), i64(42))
}
)CF");
        auto main_cf = ws.file("app/main.cf", R"CF(package main

import "../lib"

fn main() {
    x := 1
    println("RUN_OUT={}", lib.public_entry())
}
)CF");
        // Separate package with `cpp { ... }` -> UNUSED-SYMBOL suppressed for package
        auto cpp_pkg_cf = ws.file("cpppkg/main.cf", R"CF(package main
fn maybe_called_from_cpp() -> i64 { return 7 }
fn main() {
    mut r: i64 = 0
    cpp { r = 1; }
    println("{}", r)
}
)CF");

        cfire::analysis::CheckOptions bopts;
        bopts.verify_cpp_backend = false;
        bopts.resolve_sibling_packages = true;
        auto diags = cfire::analysis::check_source(main_cf.string(), cfire::fs::read_text(main_cf).value_or(""), bopts);

        require(has_diag(diags, "cfire/UNUSED-SYMBOL", "'DEAD_GLOB'"), "warns on dead private global");
        require(has_diag(diags, "cfire/UNUSED-SYMBOL", "'DeadType'"), "warns on dead private struct");
        require(has_diag(diags, "cfire/UNUSED-SYMBOL", "'DeadType.dead_method'"), "warns on dead method of private struct");
        require(has_diag(diags, "cfire/UNUSED-SYMBOL", "'DeadEnum'"), "warns on dead private enum");
        require(has_diag(diags, "cfire/UNUSED-SYMBOL", "'DeadIface'"), "warns on dead private interface");
        require(has_diag(diags, "cfire/UNUSED-SYMBOL", "'dead_self_rec'"), "warns on self-recursive dead private fn");

        require(!has_diag(diags, "cfire/UNUSED-SYMBOL", "'_IGNORED_GLOB'"), "_IGNORED_GLOB exempt");
        require(!has_diag(diags, "cfire/UNUSED-SYMBOL", "'PUB_GLOB'"), "PUB_GLOB exempt");
        require(!has_diag(diags, "cfire/UNUSED-SYMBOL", "'unused_but_on_pub_type'"), "method on pub type exempt");
        require(!has_diag(diags, "cfire/UNUSED-SYMBOL", "'ping'"), "interface-satisfying method exempt");
        require(!has_diag(diags, "cfire/UNUSED-SYMBOL", "'_suppressed_fn'"), "_suppressed_fn exempt");
        require(!has_diag(diags, "cfire/UNUSED-SYMBOL", "'test_only_helper'"), "sibling *_test.cf symbol exempt on build");
        require(!has_diag(diags, "cfire/UNUSED-SYMBOL", "'main'"), "fn main exempt");

        auto cpp_diags = cfire::analysis::check_source(cpp_pkg_cf.string(), cfire::fs::read_text(cpp_pkg_cf).value_or(""), bopts);
        require(!has_diag(cpp_diags, "cfire/UNUSED-SYMBOL"), "package with cpp {} exempt from UNUSED-SYMBOL");

        // Regular CLI warning delivery on build, run, test (to stderr while exiting 0) + TestResultCache replay
        auto run_out_path = ws.path / "run.stdout";
        auto run_err_path = ws.path / "run.stderr";
        int rc = 0;
        (void)cfire::bridge::run_command_capture(
            std::format("{} run --no-cache {} > {} 2> {}",
                cfire_bin,
                cfire::bridge::shell_quote(main_cf.string()),
                cfire::bridge::shell_quote(run_out_path.string()),
                cfire::bridge::shell_quote(run_err_path.string())),
            rc, false);
        require(rc == 0, std::format("cfire run exits 0 despite warnings (rc={})", rc));
        std::string run_out = cfire::fs::read_text(run_out_path).value_or("");
        std::string run_err = cfire::fs::read_text(run_err_path).value_or("");
        require(run_out.contains("RUN_OUT=8"), std::format("cfire run stdout preserved, got:\n{}", run_out));
        require(run_err.contains("cfire/UNUSED-VAR") && run_err.contains("cfire/UNUSED-SYMBOL"),
            std::format("cfire run wrote unused warnings to stderr, got:\n{}", run_err));

        auto out_bin = ws.path / "app_bin";
        auto bld_res = run_cfire(std::format("build {} -o {}",
            cfire::bridge::shell_quote(main_cf.string()),
            cfire::bridge::shell_quote(out_bin.string())));
        bld_res.assert_ok("cfire build exits 0 on warnings")
            .assert_contains("cfire/UNUSED-VAR", "cfire build emits UNUSED-VAR");

        // Run `cfire test` twice on `lib`: warnings must NOT be suppressed on the second run (TestResultCache skip)
        auto test1 = run_cfire(std::format("test {}", cfire::bridge::shell_quote((ws.path / "lib").string())));
        test1.assert_ok("first cfire test on lib exits 0")
            .assert_contains("cfire/UNUSED-VAR", "first cfire test prints UNUSED-VAR");
        auto test2 = run_cfire(std::format("test {}", cfire::bridge::shell_quote((ws.path / "lib").string())));
        test2.assert_ok("second cfire test on lib exits 0")
            .assert_contains("cfire/UNUSED-VAR", "second cfire test still prints warnings (TestResultCache skipped)")
            .assert_contains("PASS: uses test_only_helper", "second cfire test passes");
    }

    (void)rt_dir;
    std::println("--- PASS: test_pub_and_unused_diagnostics");
}

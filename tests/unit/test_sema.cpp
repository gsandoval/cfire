#include "../test_harness.hpp"

void test_sema_intent_and_move_diagnostics() {
    std::println("=== RUN   test_sema_intent_and_move_diagnostics");

    require_sema_error(R"(package main
fn bump(mut x: i32) { x += 1 }
fn main() { mut val: i32 = 10; bump(val) }
)", "cfire/MUT-CALLSITE", "bad_mut.cf");

    require_sema_error(R"(package main
fn consume(move s: str) { println("{}", s) }
fn main() { msg := "secret"; consume(move msg); println("{}", msg) }
)", "cfire/USE-AFTER-MOVE", "use_after_move.cf");

    require_sema_error(R"(package main
type Shape enum { Circle(f64), Rect(f64, f64), Empty }
fn main() {
    s := Shape::Circle(1.0)
    match s {
        Circle(r) => { println("{}", r) }
        Rect(w, h) => { println("{}", w) }
    }
}
)", "cfire/NON-EXHAUSTIVE-MATCH", "non_exhaustive.cf");

    require_sema_error(R"(package main
type Counter struct { Val: i32 }
fn (mut c: Counter) inc() { c.Val += 1 }
fn main() { imm_c := Counter{Val: 0}; imm_c.inc() }
)", "cfire/MUT-RECEIVER", "mut_receiver.cf");

    require_sema_error(R"(package main
fn write_out(out target: i32) { target = 42 }
fn main() { mut v: i32 = 0; write_out(v) }
)", "cfire/DEPRECATED-OUT-PARAM", "out_callsite.cf");

    require_sema_error(R"(package main
fn mutate_val(mut target: i32) { target += 1 }
fn main() { imm_v: i32 = 10; mutate_val(mut imm_v) }
)", "cfire/IMMUTABLE-MUT-ARG", "imm_mut_arg.cf");

    require_sema_error(R"(package main
fn transfer(mut from: i32, mut to: i32) { from -= 10; to += 10 }
fn main() { mut acc: i32 = 100; transfer(mut acc, mut acc) }
)", "cfire/MUT-ALIAS", "mut_alias.cf");

    // Unreachable match arm diagnostics (cfire/UNREACHABLE-MATCH-ARM)
    require_sema_error(R"(package main
fn classify(s: []str) -> str {
    match s {
        []                   => { return "empty" }
        [head, ..tail]       => { return "head=" + head }
        [first, ..mid, last] => { return first + ".." + last }
    }
    return "other"
}
fn main() {}
)", "cfire/UNREACHABLE-MATCH-ARM", "unreachable_slice.cf");

    require_sema_error(R"(package main
type Token enum { Ident(str), Eof }
fn main() {
    t := Token::Eof
    match t {
        Ident(s) => { println("{}", s) }
        Ident(s2) => { println("{}", s2) }
        Eof => {}
    }
}
)", "cfire/UNREACHABLE-MATCH-ARM", "duplicate_enum_arm.cf");

    require_sema_error(R"(package main
fn main() {
    x := 10
    match x {
        1 => { println("one") }
        _ => { println("wild") }
        2 => { println("two") }
    }
}
)", "cfire/UNREACHABLE-MATCH-ARM", "after_wildcard_arm.cf");

    // SAFE-P0-01: Origin & Loan Invalidation (cfire/USE-AFTER-FREE)
    require_sema_error(R"(package main
fn consume(move s: str) { println("{}", s) }
fn main() {
    mut msg := "hello"
    p := &mut msg
    consume(move msg)
    println("{}", *p)
}
)", "cfire/USE-AFTER-FREE", "uaf_after_move.cf");

    require_sema_error(R"(package main
fn main() {
    mut nums: []i32 = [1, 2, 3]
    view := nums[0..2]
    nums.push(4)
    println("{}", view[0])
}
)", "cfire/USE-AFTER-FREE", "uaf_after_push_realloc.cf");

    require_sema_error(R"(package main
fn main() {
    mut nums: []i32 = [1, 2, 3]
    p := nums.data()
    nums = [10, 20]
    println("{}", *p)
}
)", "cfire/USE-AFTER-FREE", "uaf_after_reassign.cf");

    // SAFE-P0-01: Lexical Scope Escape (cfire/DANGLING-POINTER & cfire/DANGLING-RETURN)
    require_sema_error(R"(package main
fn main() {
    mut p: *mut i32 = none
    if true {
        mut inner := 42
        p = &mut inner
    }
}
)", "cfire/DANGLING-POINTER", "dangling_inner_scope.cf");

    require_sema_error(R"(package main
fn leak_ptr() -> *mut i32 {
    mut local := 99
    return &mut local
}
fn main() {}
)", "cfire/DANGLING-RETURN", "dangling_return_ptr.cf");

    require_sema_error(R"(package main
fn leak_slice() -> &[]i32 {
    nums: []i32 = [1, 2, 3]
    return nums[0..2]
}
fn main() {}
)", "cfire/DANGLING-RETURN", "dangling_return_slice.cf");

    // SAFE-P0-01: Iterator Invalidation (cfire/ITERATOR-INVALIDATION)
    require_sema_error(R"(package main
fn main() {
    mut items: []i32 = [1, 2, 3]
    for x in items {
        items.push(x * 2)
    }
}
)", "cfire/ITERATOR-INVALIDATION", "iter_invalidation.cf");

    // SAFE-P0-02: Flow-Sensitive Non-Null Pointer Verification (cfire/NULL-DEREF)
    require_sema_error(R"(package main
fn main() {
    mut p: *mut i32 = none
    *p = 10
}
)", "cfire/NULL-DEREF", "null_deref_none.cf");

    require_sema_error(R"(package main
fn main() {
    mut p: *mut i32
    println("{}", *p)
}
)", "cfire/NULL-DEREF", "null_deref_uninit.cf");

    require_sema_error(R"(package main
fn test_branch(flag: bool) {
    mut x := 10
    mut p: *mut i32 = none
    if flag {
        p = &mut x
    }
    *p = 20
}
fn main() {}
)", "cfire/NULL-DEREF", "null_deref_maybe_null.cf");

    require_sema_error(R"(package main
fn test_eq_none(p: *mut i32?) {
    if p == none {
        *p = 1
    }
}
fn main() {}
)", "cfire/NULL-DEREF", "null_deref_in_none_branch.cf");

    // Positive verification: flow-sensitive non-null narrowing via != none, early return, and assert
    {
        auto valid_ast = cfire::frontend::parse_source("null_narrowing_ok.cf", R"(package main
fn narrow_if(p: *mut i32?) {
    if p != none {
        *p = 42
    }
}
fn narrow_early_return(p: *mut i32?) {
    if p == none {
        return
    }
    *p = 77
}
fn narrow_assert(p: *mut i32?) {
    assert(p != none)
    *p = 99
}
fn main() {}
)");
        require(valid_ast.has_value(), "parse valid non-null narrowing (SAFE-P0-02)");
        if (valid_ast) {
            auto checked = cfire::frontend::analyze(std::move(*valid_ast));
            require(checked.has_value(), "sema accepts narrowed non-null dereferences (SAFE-P0-02)");
        }
    }

    // SAFE-P0-03: Compile-Time & Runtime Bounds Verification (cfire/INDEX-OUT-OF-BOUNDS & cfire::index_at)
    require_sema_error(R"(package main
fn main() {
    nums: []i32 = [10, 20, 30]
    println("{}", nums[-1])
}
)", "cfire/INDEX-OUT-OF-BOUNDS", "oob_negative_index.cf");

    require_sema_error(R"(package main
fn main() {
    arr: [4]i32 = [1, 2, 3, 4]
    println("{}", arr[4])
}
)", "cfire/INDEX-OUT-OF-BOUNDS", "oob_fixed_array_index.cf");

    {
        auto bounds_ast = cfire::frontend::parse_source("bounds_codegen.cf", R"(package main
fn check_codegen(idx: i32) -> i32 {
    arr: [4]i32 = [10, 20, 30, 40]
    nums: []i32 = [1, 2, 3]
    mut m: map[str]i32 = {}
    m["k"] = 5
    mut sum := arr[2] + nums[idx] + m["k"]
    for i, v in nums {
        sum += nums[i] + v
    }
    return sum
}
fn main() {}
)");
        require(bounds_ast.has_value(), "parse bounds_codegen.cf (SAFE-P0-03)");
        if (bounds_ast) {
            auto checked = cfire::frontend::analyze(std::move(*bounds_ast));
            require(checked.has_value(), "analyze bounds_codegen.cf (SAFE-P0-03)");
            if (checked) {
                std::string cpp = cfire::frontend::emit_cpp(*checked);
                require(cpp.contains("cfire::index_at(nums, idx,"),
                    "dynamic slice indexing lowers to cfire::index_at (SAFE-P0-03)");
                require(cpp.contains("arr[2]"),
                    "proven in-bounds fixed-array constant index emits zero-overhead operator[] (SAFE-P0-03)");
                require(cpp.contains("nums[i]"),
                    "proven in-bounds loop index variable emits zero-overhead operator[] (SAFE-P0-03)");
                require(cpp.contains("m[\"k\"]"),
                    "map indexing emits direct operator[] (SAFE-P0-03)");
            }
        }
    }

    run_cfire_source(R"(package main
fn get_elem(nums: &[]i32, idx: i32) -> i32 {
    return nums[idx]
}
fn main() {
    nums: []i32 = [10, 20, 30]
    println("{}", get_elem(nums, 99))
}
)", "cfire_oob_panic")
        .assert_fail("dynamic out-of-bounds slice index exits non-zero (SAFE-P0-03)")
        .assert_contains("index 99 out of bounds for length 3", "dynamic OOB panic message (SAFE-P0-03)");

    // SAFE-02a: Compile-Time Divide-By-Zero Verification (cfire/DIVIDE-BY-ZERO)
    require_sema_error(R"(package main
fn main() {
    x := 100 / 0
    println("{}", x)
}
)", "cfire/DIVIDE-BY-ZERO", "div_by_zero.cf");

    require_sema_error(R"(package main
fn main() {
    mut x := 50
    x %= 0
}
)", "cfire/DIVIDE-BY-ZERO", "mod_assign_by_zero.cf");

    // SAFE-02b: Smart Pointer & Pointer Member-Access Null Verification (cfire/NULL-DEREF)
    require_sema_error(R"(package main
type Node struct { Val: i32 }
fn main() {
    mut box: own[Node] = none
    println("{}", box.Val)
}
)", "cfire/NULL-DEREF", "null_smart_ptr_member.cf");

    {
        auto valid_smart_ast = cfire::frontend::parse_source("valid_smart_ptr.cf", R"(package main
type Node struct { Val: i32 }
fn main() {
    box := make[own[Node]](Node{Val: 7})
    println("{}", box.Val)
}
)");
        require(valid_smart_ast.has_value(), "parse valid make[own[Node]] (SAFE-02b)");
        if (valid_smart_ast) {
            auto checked = cfire::frontend::analyze(std::move(*valid_smart_ast));
            require(checked.has_value(), "sema accepts make[own[Node]] non-null member access (SAFE-02b)");
        }
    }

    // SAFE-02c: Missing 'move' Call-Site Intent (cfire/MOVE-CALLSITE)
    require_sema_error(R"(package main
fn consume(move s: str) { println("{}", s) }
fn main() {
    msg := "token"
    consume(msg)
}
)", "cfire/MOVE-CALLSITE", "missing_move_callsite.cf");

    // SAFE-02d: Static SQL & Shell Injection Risk Warnings (cfire/SQL-INJECTION-RISK & cfire/SHELL-INJECTION-RISK)
    {
        auto inj_ast = cfire::frontend::parse_source("injection_warn.cf", R"(package main
import "ext:db/sqlite"
import "core:os"
fn check_warns(db: sqlite.Database, user_input: str) {
    _ = db.exec("DELETE FROM users WHERE name = '" + user_input + "'")
    _ = os.exec("rm -rf " + user_input)
}
fn main() {}
)");
        require(inj_ast.has_value(), "parse injection_warn.cf (SAFE-02d)");
        if (inj_ast) {
            auto res = cfire::frontend::analyze_resilient(std::move(*inj_ast));
            require(res.errors.empty(), "injection warnings do not fail compilation (SAFE-02d)");
            bool saw_sql = false;
            bool saw_shell = false;
            for (const auto& w : res.warnings) {
                if (w.contains("cfire/SQL-INJECTION-RISK")) saw_sql = true;
                if (w.contains("cfire/SHELL-INJECTION-RISK")) saw_shell = true;
            }
            require(saw_sql, "emits cfire/SQL-INJECTION-RISK on concatenated SQL query (SAFE-02d)");
            require(saw_shell, "emits cfire/SHELL-INJECTION-RISK on concatenated os.exec command (SAFE-02d)");
        }
    }

    // TOOL-SAFE-01: Fix-it synthesis for cfire/MOVE-CALLSITE and cfire/NULL-DEREF + CLI inspect diagnostic code
    {
        std::string null_src = "package main\nfn main() {\n    mut p: *mut i32 = none\n    *p = 1\n}\n";
        auto lines = cfire::diag::split_lines(null_src);
        auto sd = cfire::diag::parse_diagnostic_string("null.cf:4:5: error: dereference of potentially null pointer 'p' (cfire/NULL-DEREF)", "null.cf");
        cfire::diag::synthesize_fixits_for_diagnostic(sd, nullptr, lines);
        require(!sd.fixits.empty(), "synthesizes fix-it for cfire/NULL-DEREF (TOOL-SAFE-01)");
        if (!sd.fixits.empty()) {
            require(sd.fixits[0].replacement.contains("assert(p != none)"),
                "cfire/NULL-DEREF fix-it inserts assert(p != none) guard (TOOL-SAFE-01)");
        }

        run_cfire("inspect cfire/USE-AFTER-FREE")
            .assert_ok("cfire inspect cfire/USE-AFTER-FREE exit 0 (TOOL-SAFE-01)")
            .assert_contains("Use of Dangling Pointer or Slice View After Owner Invalidation", "cfire inspect shows diagnostic rule title (TOOL-SAFE-01)");

        run_cfire("inspect cfire/INVALID-STRUCT-TAG")
            .assert_ok("cfire inspect cfire/INVALID-STRUCT-TAG exit 0 (TEST-03a/06)")
            .assert_contains("Duplicate or Malformed Struct Reflection Tag", "cfire inspect shows INVALID-STRUCT-TAG rule title (TEST-03a/06)");
    }

    // TEST-03a: Compile-Time Struct Tag & Default Value Validation (cfire/INVALID-STRUCT-TAG)
    require_sema_error(R"(package main
type BadJson struct {
    A: str `json:"id"`
    B: i64 `json:"id"`
}
fn main() {}
)", "cfire/INVALID-STRUCT-TAG", "dup_json_tag.cf");

    require_sema_error(R"(package main
type BadCli struct {
    Host: str `cli:"--host,-h"`
    Help: str `cli:"--help,-h"`
}
fn main() {}
)", "cfire/INVALID-STRUCT-TAG", "dup_cli_flag.cf");

    require_sema_error(R"(package main
type BadDefault struct {
    Port: i64 `default:"eighty"`
}
fn main() {}
)", "cfire/INVALID-STRUCT-TAG", "bad_default_int.cf");

    require_sema_error(R"(package main
type BadBoolDefault struct {
    Debug: bool `default:"maybe"`
}
fn main() {}
)", "cfire/INVALID-STRUCT-TAG", "bad_default_bool.cf");

    // Lexical scope shadowing restoration & lexer/formatter brace-counting regressions
    require_sema_error(R"(package main
fn main() {
    x := 1
    if true {
        mut x := 2
        x = 3
    }
    x = 4
}
)", "cfire/ASSIGN-IMMUTABLE", "shadow_restore_immutable.cf");

    {
        auto shadow_ok_ast = cfire::frontend::parse_source("shadow_restore_mutable.cf", R"(package main
fn main() {
    mut x := 10
    for x in 0..3 {
        println("{}", x)
    }
    x = 20
}
)");
        require(shadow_ok_ast.has_value(), "parse shadow_restore_mutable.cf");
        if (shadow_ok_ast) {
            auto checked = cfire::frontend::analyze(std::move(*shadow_ok_ast));
            require(checked.has_value(), "outer mutable variable remains mutable after inner loop variable shadow exits");
        }
    }

    {
        auto cpp_brace_ast = cfire::frontend::parse_source("cpp_brace_comments.cf", R"(package main
fn main() {
    cpp {
        // unmatched opening brace in line comment: {
        /* unmatched closing brace in block comment: } */
        const char* s = "braces in string: { }";
        char c = '{';
        (void)s;
        (void)c;
    }
}
)");
        require(cpp_brace_ast.has_value(), "lexer cpp {} block ignores braces inside C++ comments, strings, and char literals");
    }

    {
        std::string unformatted = R"(package main

fn main() {
    s := "escaped quote \"{\" inside string" // inline comment with unmatched {
    cpp {
        char ch = '{'; // C++ char literal brace
        (void)ch;
    }
    if s != "" { // another { in comment
        println("ok")
    }
}
)";
        auto formatted = cfire::fmt::format_source(unformatted, "fmt_comment_brace.cf");
        require(formatted.has_value(), "format_source succeeds with braces in strings, cpp char literals, and inline comments");
        if (formatted) {
            require(formatted->contains("\n    if s != \"\" {"), "formatter preserves 4-space indent after inline comment and cpp char literal '{'");
            require(formatted->contains("\n        println(\"ok\")\n    }\n}"), "formatter does not over-indent closing braces after comment braces");
        }
    }

    // SEC-02: CSPRNG (sha256::random_bytes / random_hex) & RFC 5869 HKDF-SHA256
    {
        auto r1 = cfire::sha256::random_bytes(32);
        auto r2 = cfire::sha256::random_bytes(32);
        require(r1.size() == 32, "sha256::random_bytes(32) returns 32 bytes (SEC-02)");
        require(r1 != r2, "sha256::random_bytes produces distinct CSPRNG output (SEC-02)");
        auto rhex = cfire::sha256::random_hex(16);
        require(rhex.size() == 32, "sha256::random_hex(16) returns 32 hex chars (SEC-02)");

        // RFC 5869 Test Case 1 (Basic test case with SHA-256)
        std::vector<std::byte> ikm(22, std::byte{0x0b});
        std::vector<std::byte> salt(13);
        for (std::size_t i = 0; i < 13; ++i) salt[i] = static_cast<std::byte>(i);
        std::vector<std::byte> info(10);
        for (std::size_t i = 0; i < 10; ++i) info[i] = static_cast<std::byte>(0xf0 + i);
        auto okm = cfire::hmac::hkdf_sha256(ikm, salt, info, 42);
        require(okm.size() == 42, "hkdf_sha256 derives 42-byte OKM (SEC-02)");
        std::string okm_hex;
        for (std::byte b : okm) {
            okm_hex += std::format("{:02x}", static_cast<unsigned char>(b));
        }
        require(okm_hex == "3cb25f25faacd57a90434f64d0362f2a2d2d0a90cf1a5a4c5db02d56ecc4c5bf34007208d5b887185865",
            "hkdf_sha256 matches RFC 5869 Test Case 1 exact vector (SEC-02)");
    }

    // Frontend Sema & Diagnostics for Fallible Results, Stdlib Catalog, String Interpolation & Humanized C++ Fallback
    {
        // 1. os.stdin().read_lines() returns []str! and works with try, .len, [i], and string interpolation
        auto aoc_ast = cfire::frontend::parse_source("day1_01.cf", R"(package main
import "core:os"
fn main() -> void! {
    lines := try os.stdin().read_lines()
    for i in 0..lines.len {
        println("message {lines[i]}")
    }
}
)");
        require(aoc_ast.has_value(), "parse day1_01.cf with os.stdin().read_lines()");
        if (aoc_ast) {
            auto checked = cfire::frontend::analyze(std::move(*aoc_ast));
            require(checked.has_value(), "sema accepts os.stdin().read_lines() with try, .len, and [i]");
        }

        // 1b. CF-089: Sema rejects deprecated I/O APIs with cfire/DEPRECATED-API and synthesizes fix-its
        require_sema_error(R"(package main
fn main() {
    ln := readln("> ") or ""
    println("{}", ln)
}
)", "cfire/DEPRECATED-API", "dep_readln.cf");

        require_sema_error(R"(package main
import "core:fs"
fn main() -> void! {
    _ = try fs.read_all("a.bin")
}
)", "cfire/DEPRECATED-API", "dep_fs_read_all.cf");

        require_sema_error(R"(package main
import "core:fs"
fn main() -> void! {
    try fs.write_all("a.bin", []byte("x"))
}
)", "cfire/DEPRECATED-API", "dep_fs_write_all.cf");

        require_sema_error(R"(package main
import "core:os"
fn main() {
    lines := os.read_stdin_lines()
    println("{}", lines.len)
}
)", "cfire/DEPRECATED-API", "dep_os_read_stdin_lines.cf");

        require_sema_error(R"(package main
import "core:os"
fn main() -> void! {
    _ = try os.socketpair()
}
)", "cfire/DEPRECATED-API", "dep_os_socketpair.cf");

        require_sema_error(R"(package main
import "core:fs"
fn main() -> void! {
    mut f := try fs.open("a.txt", fs.OpenMode::Write)
    _ = try f.write_str("hello")
}
)", "cfire/DEPRECATED-API", "dep_file_write_str.cf");

        require_sema_error(R"(package main
import "core:net"
fn main() -> void! {
    (mut a, mut b) := try net.socketpair()
    _ = try a.write("hi")
    _ = try b.read_str(16)
}
)", "cfire/DEPRECATED-API", "dep_tcp_write_read_str.cf");

        {
            std::string dep_src = "package main\nimport \"core:fs\"\nfn main() -> void! {\n    _ = try fs.read_all(\"a.bin\")\n}\n";
            auto lines = cfire::diag::split_lines(dep_src);
            auto sd = cfire::diag::parse_diagnostic_string(
                "dep.cf:4:14: error: 'fs.read_all' has been renamed to 'fs.read_bytes' (or 'fs.read_text' for UTF-8 strings) (cfire/DEPRECATED-API)",
                "dep.cf");
            cfire::diag::synthesize_fixits_for_diagnostic(sd, nullptr, lines);
            require(!sd.fixits.empty() && sd.fixits[0].replacement == "fs.read_bytes",
                "synthesizes 'fs.read_bytes' replacement fix-it for cfire/DEPRECATED-API on fs.read_all");
        }

        // 2. cfire/UNHANDLED-RESULT on un-unwrapped fallible user & stdlib calls + 'try ' fix-it synthesis
        require_sema_error(R"(package main
import "core:fs"
fn main() {
    content := fs.read_text("input.txt")
    println("{}", content)
}
)", "cfire/UNHANDLED-RESULT", "unhandled_stdlib_result.cf");

        require_sema_error(R"(package main
fn compute() -> []str! {
    return ["a", "b"]
}
fn main() {
    lines := compute()
    println("{}", lines.len)
}
)", "cfire/UNHANDLED-RESULT", "unhandled_user_result.cf");

        {
            std::string unhandled_src = "package main\nimport \"core:fs\"\nfn main() -> void! {\n    s := fs.read_text(\"a.txt\")\n}\n";
            auto lines = cfire::diag::split_lines(unhandled_src);
            auto sd = cfire::diag::parse_diagnostic_string(
                "unhandled.cf:4:10: error: call to 'fs.read_text' returns fallible type 'str!' which must be handled with 'try' or 'or' (cfire/UNHANDLED-RESULT)",
                "unhandled.cf");
            cfire::diag::synthesize_fixits_for_diagnostic(sd, nullptr, lines);
            require(!sd.fixits.empty() && sd.fixits[0].replacement == "try ",
                "synthesizes 'try ' insertion fix-it for cfire/UNHANDLED-RESULT");
        }

        // 3. cfire/MISSING-IMPORT when calling stdlib module without import
        require_sema_error(R"(package main
fn main() {
    args := os.args()
    println("{}", args.len)
}
)", "cfire/MISSING-IMPORT", "missing_os_import.cf");

        // 4. cfire/UNKNOWN-STDLIB-FN with edit-distance suggestion
        {
            auto typo_ast = cfire::frontend::parse_source("typo_stdlib.cf", R"(package main
import "core:os"
fn main() {
    val := os.geten("PATH")
    println("{}", val)
}
)");
            require(typo_ast.has_value(), "parse typo_stdlib.cf");
            if (typo_ast) {
                auto checked = cfire::frontend::analyze(std::move(*typo_ast));
                require(!checked.has_value() && checked.error().contains("cfire/UNKNOWN-STDLIB-FN") &&
                        checked.error().contains("did you mean 'os.getenv'"),
                        "cfire/UNKNOWN-STDLIB-FN suggests closest stdlib function 'os.getenv'");
            }
        }

        // 5. cfire/ARITY-MISMATCH on stdlib calls
        require_sema_error(R"(package main
import "core:os"
fn main() {
    args := os.args("unexpected_arg")
    println("{}", args.len)
}
)", "cfire/ARITY-MISMATCH", "stdlib_arity_mismatch.cf");

        // 6. cfire/UNWRAPPED-OPTIONAL on .len and [i] access on optional T?
        require_sema_error(R"(package main
fn maybe_lines() -> []str? {
    return none
}
fn main() {
    lines := maybe_lines()
    println("{}", lines.len)
}
)", "cfire/UNWRAPPED-OPTIONAL", "opt_len_unwrapped.cf");

        require_sema_error(R"(package main
fn maybe_lines() -> []str? {
    return none
}
fn main() {
    lines := maybe_lines()
    println("{}", lines[0])
}
)", "cfire/UNWRAPPED-OPTIONAL", "opt_index_unwrapped.cf");

        // 7. cfire/INVALID-MEMBER and cfire/INVALID-INDEX on scalar primitives
        require_sema_error(R"(package main
fn main() {
    count := 42
    println("{}", count.len)
}
)", "cfire/INVALID-MEMBER", "scalar_invalid_member.cf");

        require_sema_error(R"(package main
fn main() {
    count := 42
    println("{}", count[0])
}
)", "cfire/INVALID-INDEX", "scalar_invalid_index.cf");

        // 8. cfire/INVALID-INTERPOLATION and cfire/UNDECLARED-IDENT inside string interpolation
        require_sema_error(R"(package main
fn main() {
    println("bad {1 + }")
}
)", "cfire/INVALID-INTERPOLATION", "bad_interp_syntax.cf");

        require_sema_error(R"(package main
fn main() {
    println("val={undeclared_var}")
}
)", "cfire/UNDECLARED-IDENT", "bad_interp_ident.cf");

        // 9. Fallback C++ compiler output humanization (std::expected -> T!, std::vector<std::string> -> []str, .size -> .len)
        {
            std::string raw_clang_err =
                "day1_01.cf:7:23: error: no member named 'size' in 'std::expected<std::vector<std::string>, cfire::Error>'\n"
                "day1_01.cf:8:28: error: type 'std::expected<std::vector<std::string>, cfire::Error>' does not provide a subscript operator\n";
            std::string humanized = cfire::diag::humanize_cpp_compiler_output(raw_clang_err);
            require(humanized.contains("no member '.len' in '[]str!'"),
                "humanize_cpp_compiler_output rewrites std::expected<std::vector<std::string>, cfire::Error> and .size to []str! and .len");
            require(humanized.contains("type '[]str!' does not support indexing '[i]'"),
                "humanize_cpp_compiler_output rewrites subscript operator diagnostic to cfire syntax");
            require(!humanized.contains("std::expected") && !humanized.contains("std::vector"),
                "humanize_cpp_compiler_output strips lowered C++ std:: types");

            std::string raw_gen_err =
                "/tmp/proj/.cfire/gen/day1_01.cpp:39:12: error: no matching function for call to 'cfire::slice' on 'cfire::fs::File'\n";
            std::string humanized_gen = cfire::diag::humanize_cpp_compiler_output(raw_gen_err);
            require(humanized_gen.contains("day1_01.cf:39:12:") &&
                    humanized_gen.contains("slice '[a..b]'") &&
                    humanized_gen.contains("fs.File"),
                "humanize_cpp_compiler_output rewrites .cfire/gen/<stem>.cpp path and cfire:: internals");
        }

        // 10. Misplaced / No-op Token Enforcement (try, or, mut/out, move, break/continue, no-effect stmt)
        // 10a. cfire/REDUNDANT-TRY on non-fallible expression & call + auto-fixer
        require_sema_error(R"(package main
fn main() -> void! {
    x := try (1 + 2)
    println("{}", x)
}
)", "cfire/REDUNDANT-TRY", "redundant_try_expr.cf");

        require_sema_error(R"(package main
fn plain() -> i32 { return 7 }
fn main() -> void! {
    x := try plain()
    println("{}", x)
}
)", "cfire/REDUNDANT-TRY", "redundant_try_call.cf");

        {
            std::string red_try_src = "package main\nfn main() -> void! {\n    x := try (1 + 2)\n}\n";
            auto lines = cfire::diag::split_lines(red_try_src);
            auto sd = cfire::diag::parse_diagnostic_string(
                "red_try.cf:3:10: error: 'try' has no effect on non-fallible expression of type 'i32'; remove 'try' (cfire/REDUNDANT-TRY)",
                "red_try.cf");
            cfire::diag::synthesize_fixits_for_diagnostic(sd, nullptr, lines);
            require(!sd.fixits.empty() && sd.fixits[0].replacement.empty() && sd.fixits[0].end_col - sd.fixits[0].start_col == 4,
                "synthesizes 'try ' removal fix-it for cfire/REDUNDANT-TRY");
        }

        // 10b. cfire/TRY-ON-OPTIONAL on T? expression
        require_sema_error(R"(package main
fn maybe_num() -> i32? { return none }
fn main() -> void! {
    x := try maybe_num()
    println("{}", x)
}
)", "cfire/TRY-ON-OPTIONAL", "try_on_optional.cf");

        // 10c. cfire/TRY-IN-NON-FALLIBLE-FN
        require_sema_error(R"(package main
fn fallible() -> i32! { return 1 }
fn helper() -> i32 {
    return try fallible()
}
fn main() {
    println("{}", helper())
}
)", "cfire/TRY-IN-NON-FALLIBLE-FN", "try_in_non_fallible_fn.cf");

        // 10d. cfire/TRY-IN-DEFER
        require_sema_error(R"(package main
fn fallible() -> i32! { return 1 }
fn main() -> void! {
    defer {
        _ = try fallible()
    }
}
)", "cfire/TRY-IN-DEFER", "try_in_defer.cf");

        // 10e. cfire/INVALID-TRY-CONTEXT on short-circuit RHS and while-loop header
        require_sema_error(R"(package main
fn check() -> bool! { return true }
fn main() -> void! {
    ok := true
    if ok && (try check()) {
        println("yes")
    }
}
)", "cfire/INVALID-TRY-CONTEXT", "try_in_short_circuit_rhs.cf");

        require_sema_error(R"(package main
fn check() -> bool! { return false }
fn main() -> void! {
    for (try check()) {
        break
    }
}
)", "cfire/INVALID-TRY-CONTEXT", "try_in_while_cond.cf");

        // 10f. cfire/REDUNDANT-OR on neither-fallible-nor-optional LHS
        require_sema_error(R"(package main
fn main() {
    x := (1 + 2) or 0
    println("{}", x)
}
)", "cfire/REDUNDANT-OR", "redundant_or_value.cf");

        require_sema_error(R"(package main
fn plain() -> i32 { return 5 }
fn main() {
    x := plain() or { return }
    println("{}", x)
}
)", "cfire/REDUNDANT-OR", "redundant_or_block.cf");

        // 10g. cfire/INVALID-OR-BLOCK-CONTEXT on short-circuit RHS and while-loop header
        require_sema_error(R"(package main
fn check() -> bool! { return true }
fn main() {
    ok := true
    if ok && (check() or { return }) {
        println("yes")
    }
}
)", "cfire/INVALID-OR-BLOCK-CONTEXT", "or_block_in_short_circuit_rhs.cf");

        require_sema_error(R"(package main
fn check() -> bool! { return false }
fn main() {
    for (check() or { break }) {
        break
    }
}
)", "cfire/INVALID-OR-BLOCK-CONTEXT", "or_block_in_while_cond.cf");

        // 10h. cfire/REDUNDANT-CALLSITE-INTENT on 'mut'/'out' passed to 'in' parameter + auto-fixer
        require_sema_error(R"(package main
fn read_only(x: i32) -> i32 { return x + 1 }
fn main() {
    mut v := 10
    println("{}", read_only(mut v))
}
)", "cfire/REDUNDANT-CALLSITE-INTENT", "redundant_callsite_mut.cf");

        require_sema_error(R"(package main
fn main() {
    mut v := 10
    println("{}", mut v)
}
)", "cfire/REDUNDANT-CALLSITE-INTENT", "redundant_builtin_mut.cf");

        {
            std::string red_mut_src = "package main\nfn read_only(x: i32) -> i32 { return x }\nfn main() {\n    mut v := 10\n    _ = read_only(mut v)\n}\n";
            auto lines = cfire::diag::split_lines(red_mut_src);
            auto sd = cfire::diag::parse_diagnostic_string(
                "red_mut.cf:5:19: error: call-site 'mut' annotation has no effect on 'in' parameter 1 of 'read_only'; remove 'mut' (cfire/REDUNDANT-CALLSITE-INTENT)",
                "red_mut.cf");
            cfire::diag::synthesize_fixits_for_diagnostic(sd, nullptr, lines);
            require(!sd.fixits.empty() && sd.fixits[0].replacement.empty() && sd.fixits[0].end_col - sd.fixits[0].start_col == 4,
                "synthesizes 'mut ' removal fix-it for cfire/REDUNDANT-CALLSITE-INTENT");
        }

        // 10i. cfire/INVALID-MOVE on rvalue literals, temporary expressions, and scalar Copy variables
        require_sema_error(R"(package main
fn main() {
    x := move 42
    println("{}", x)
}
)", "cfire/INVALID-MOVE", "move_on_literal.cf");

        require_sema_error(R"(package main
fn main() {
    mut a := 1
    mut b := 2
    x := move (a + b)
    println("{}", x)
}
)", "cfire/INVALID-MOVE", "move_on_rvalue_expr.cf");

        require_sema_error(R"(package main
fn main() {
    mut n := 42
    y := move n
    println("{}", y)
}
)", "cfire/INVALID-MOVE", "move_on_copy_scalar.cf");

        // 10j. cfire/BREAK-OUTSIDE-LOOP at function scope, inside defer, and inside closure inside loop
        require_sema_error(R"(package main
fn main() {
    break
}
)", "cfire/BREAK-OUTSIDE-LOOP", "break_at_fn_scope.cf");

        require_sema_error(R"(package main
fn main() {
    for i in 0..3 {
        defer {
            continue
        }
        println("{}", i)
    }
}
)", "cfire/BREAK-OUTSIDE-LOOP", "continue_inside_defer_in_loop.cf");

        // 10k. cfire/NO-EFFECT-STMT on pure expression statements (including == typo for =)
        {
            auto eq_ast = cfire::frontend::parse_source("eq_stmt.cf", R"(package main
fn main() {
    mut x := 1
    x == 2
    println("{}", x)
}
)");
            require(eq_ast.has_value(), "parse eq_stmt.cf");
            if (eq_ast) {
                auto checked = cfire::frontend::analyze(std::move(*eq_ast));
                require(!checked.has_value() &&
                        checked.error().contains("cfire/NO-EFFECT-STMT") &&
                        checked.error().contains("did you mean '=' assignment?"),
                        "cfire/NO-EFFECT-STMT flags 'x == 2' with assignment hint");
            }
        }

        require_sema_error(R"(package main
fn main() {
    a := 10
    b := 20
    a + b
}
)", "cfire/NO-EFFECT-STMT", "arith_no_effect_stmt.cf");

        // 10l. E2E verification of generalized sub-expression hoisting for 'try' and block-form 'or { ... }'
        run_cfire_source(R"(package main

fn get_num(x: i32) -> i32! {
    if x < 0 {
        return error("neg")
    }
    return x * 10
}

fn check_flag(b: bool) -> bool! {
    return b
}

fn get_items() -> []i32! {
    return [1, 2, 3]
}

fn compute_sum() -> i32! {
    mut total := (try get_num(2)) + (try get_num(3))
    if (try check_flag(true)) {
        for item in (try get_items()) {
            total += item
        }
    }
    return total + (try get_num(1))
}

fn main() -> void! {
    val := try compute_sum()
    mut fallback_acc := 0
    for i in -1..3 {
        v := (get_num(i) or { continue }) + 5
        fallback_acc += v
    }
    println("val={} acc={}", val, fallback_acc)
}
)", "cfire_subexpr_try_or_hoist")
            .assert_ok("sub-expression try and block-form or hoisting compiles and exits 0")
            .assert_contains("val=66 acc=45", "sub-expression try and block-form or produces expected runtime values");
    }

    // CF-099: Missing-return divergence check
    require_sema_error(R"(package main
fn incomplete_ret(x: i32) -> i32 {
    if x > 0 {
        return 1
    }
}
fn main() {}
)", "cfire/MISSING-RETURN", "missing_return_if.cf");

    require_sema_error(R"(package main
fn loop_with_break() -> i32 {
    for {
        break
    }
}
fn main() {}
)", "cfire/MISSING-RETURN", "missing_return_break.cf");

    // CF-097: Escaping closure capture semantics and mutation diagnostic
    require_sema_error(R"(package main
fn test_esc() {
    mut total := 0
    async.scope |scope| {
        scope.spawn || {
            total += 5
        }
    }
}
fn main() {}
)", "cfire/ESCAPING-CAPTURE-MUT", "esc_capture_mut.cf");

    require_sema_ok(R"(package main
fn test_esc_ok() {
    mut total := 0
    ptr := &mut total
    async.scope |scope| {
        scope.spawn || {
            *ptr += 5
        }
    }
}
fn make_adder(base: i32) -> fn(i32) -> i32 {
    return |x: i32| -> i32 { return base + x }
}
fn main() {}
)", "esc_capture_ok.cf");

    require_sema_ok(R"(package main
fn infinite_loop() -> i32 {
    for {
        // diverges
    }
}
fn full_if(x: i32) -> i32 {
    if x > 0 { return 1 } else { return 0 }
}
fn main() {}
)", "valid_return_divergence.cf");

    // CF-100: Expr-node-keyed move tracking and CFG loop move suppression
    run_cfire_source(R"(package main

fn pair_str(a: str, b: str) -> str {
    return a + ":" + b
}

fn test_same_line(move s: str) -> str {
    return pair_str(s, s)
}

fn test_loop_no_move(move s: str) -> str {
    mut res := ""
    for i in 0..3 {
        res += s + "-"
    }
    return res
}

fn main() {
    mut s1 := "hello"
    val1 := test_same_line(move s1)
    println("same_line={}", val1)
    mut s2 := "item"
    val2 := test_loop_no_move(move s2)
    println("loop={}", val2)
}
)", "test_cf100_move")
        .assert_ok("CF-100 expr-node move compiles and exits 0")
        .assert_contains("same_line=hello:hello", "first argument on same line preserved")
        .assert_contains("loop=item-item-item-", "loop iterations never moved automatically");

    // CF-098: Defined Integer Overflow, Wrapping & Saturation Semantics
    run_cfire_source(R"(package main

import "core:math"

fn main() {
    max_i64 := 9223372036854775807
    sat := math.saturating_add(max_i64, 100)
    wrap := math.wrapping_add(max_i64, 1)
    chk1 := math.checked_add(max_i64, 1)
    chk2 := math.checked_add(10, 20)

    has_chk1 := chk1 != none
    println("sat={}", sat)
    println("wrap={}", wrap)
    println("has_chk1={}", has_chk1)
    println("chk2={}", chk2 or 0)
}
)", "test_cf098_math")
        .assert_ok("CF-098 math overflow compiles and exits 0")
        .assert_contains("sat=9223372036854775807", "saturating add clamps to max i64")
        .assert_contains("wrap=-9223372036854775808", "wrapping add wraps to min i64")
        .assert_contains("has_chk1=false", "checked add returns none on overflow")
        .assert_contains("chk2=30", "checked add succeeds within bounds");

    // CF-097: Escaping closure retains captured state by value
    run_cfire_source(R"(package main

fn make_adder(base: i32) -> fn(i32) -> i32 {
    return |x: i32| -> i32 {
        return base + x
    }
}

fn main() {
    add5 := make_adder(5)
    res := add5(10)
    println("add5_res={}", res)
}
)", "test_cf097_closure")
        .assert_ok("CF-097 escaping closure compiles and exits 0")
        .assert_contains("add5_res=15", "returned closure retains captured state by value");

    std::println("--- PASS: test_sema_intent_and_move_diagnostics");
}

void test_slice_pattern_matching(const std::filesystem::path& rt_dir) {
    std::println("=== RUN   test_slice_pattern_matching");
    (void)rt_dir;
    // 1. In-process AST and Codegen verification
    std::string_view cf_src = R"(package main

fn sum_slice(s: &[]i32) -> i32 {
    match s {
        [] => {
            return 0
        }
        [head, ..tail] => {
            return head + sum_slice(tail)
        }
    }
    return 0
}

fn inspect(s: &[]str) -> str {
    match s {
        [] => {
            return "empty"
        }
        [single] => {
            return "single"
        }
        [a, b] => {
            return "pair"
        }
        [first, ..middle, last] => {
            return "sandwich"
        }
    }
    return "unknown"
}

fn main() {
}
)";

    auto ast = cfire::frontend::parse_source("test_slice.cf", cf_src);
    require(ast.has_value(), "parse_source with slice patterns (LD-EXT-01)");
    if (ast) {
        auto checked = cfire::frontend::analyze(std::move(*ast));
        require(checked.has_value(), "analyze slice patterns (LD-EXT-01)");
        if (checked) {
            std::string cpp = cfire::frontend::emit_cpp(*checked);
            require(cpp.contains("cfire::slice(_target, 1, _target.size())"),
                "codegen lowers tail destructuring with cfire::slice (LD-EXT-01)");
            require(cpp.contains("_target.size() == 0"),
                "codegen lowers empty slice pattern condition (LD-EXT-01)");
            require(cpp.contains("_target.size() == 2"),
                "codegen lowers exact length slice pattern condition (LD-EXT-01)");
            require(!cpp.contains("auto middle = cfire::slice"),
                "codegen elides unused slice rest binding 'middle'");
        }
    }

    // 2. End-to-end cfire run invocation
    run_cfire_source(R"(package main

fn sum_slice(s: &[]i32) -> i32 {
    match s {
        [] => {
            return 0
        }
        [head, ..tail] => {
            return head + sum_slice(tail)
        }
    }
    return 0
}

fn classify(s: &[]str) -> str {
    match s {
        [] => {
            return "empty"
        }
        [single] => {
            return "single:" + single
        }
        [a, b] => {
            return "pair:" + a + "+" + b
        }
        [first, ..mid, last] => {
            return "sandwich:" + first + ".." + last
        }
    }
    return "other"
}

fn main() {
    nums: []i32 = [10, 20, 30, 40]
    println("sum={}", sum_slice(nums))

    e: []str = []
    o: []str = ["solo"]
    p: []str = ["cat", "dog"]
    m: []str = ["alpha", "beta", "gamma", "omega"]

    println("c0={}", classify(e))
    println("c1={}", classify(o))
    println("c2={}", classify(p))
    println("c3={}", classify(m))
}
)", "cfire_slice")
        .assert_ok("cfire run slice destructuring exit 0 (LD-EXT-01)")
        .assert_contains("sum=100", "slice recursive sum output (LD-EXT-01)")
        .assert_contains("c0=empty", "empty slice pattern output (LD-EXT-01)")
        .assert_contains("c1=single:solo", "single slice pattern output (LD-EXT-01)")
        .assert_contains("c2=pair:cat+dog", "pair slice pattern output (LD-EXT-01)")
        .assert_contains("c3=sandwich:alpha..omega", "sandwich slice pattern output (LD-EXT-01)");

    std::println("--- PASS: test_slice_pattern_matching");
}

void test_hard_deprecation_enforcement([[maybe_unused]] const std::filesystem::path& rt_dir) {
    std::println("=== RUN   test_hard_deprecation_enforcement (CF-103)");

    // 1. var declarations produce hard error cfire/DEPRECATED-VAR
    require_sema_error(R"(package main
fn main() {
    var x = 10
}
)", "cfire/DEPRECATED-VAR", "dep_var.cf");

    // 2. out parameter intent produces hard error cfire/DEPRECATED-OUT-PARAM
    require_sema_error(R"(package main
fn compute(out total: i32) {
    total = 42
}
fn main() {}
)", "cfire/DEPRECATED-OUT-PARAM", "dep_out_decl.cf");

    // 3. out call-site annotation produces hard error cfire/DEPRECATED-OUT-PARAM
    require_sema_error(R"(package main
fn compute(mut total: i32) {}
fn main() {
    mut res := 0
    compute(out res)
}
)", "cfire/DEPRECATED-OUT-PARAM", "dep_out_call.cf");

    // 4. x: mut T produces hard error cfire/DEPRECATED-INTENT-POS
    require_sema_error(R"(package main
fn update(x: mut i32) {
    x = x + 1
}
fn main() {}
)", "cfire/DEPRECATED-INTENT-POS", "dep_mut_pos.cf");

    // 5. explicit in intent produces hard error cfire/DEPRECATED-IN-PARAM
    require_sema_error(R"(package main
fn inspect(in x: i32) {}
fn main() {}
)", "cfire/DEPRECATED-IN-PARAM", "dep_in_param.cf");

    // 6. make(own[T], v) produces hard error cfire/INVALID-SMART-PTR-CTOR
    {
        auto ast_res = cfire::frontend::parse_source("dep_make_smart.cf", R"(package main
type Box struct {
    val: i32
}
fn main() {
    b := make(own[Box], Box{val: 42})
}
)");
        require(!ast_res.has_value(), "make(own[T], ...) rejected at parse time (CF-103)");
        if (!ast_res) {
            require(ast_res.error().contains("cfire/INVALID-SMART-PTR-CTOR"),
                "expected cfire/INVALID-SMART-PTR-CTOR, got: " + ast_res.error());
        }
    }

    // 7. _ := discard produces hard error cfire/DEPRECATED-DISCARD
    {
        auto ast_res = cfire::frontend::parse_source("dep_discard.cf", R"(package main
fn main() {
    _ := 42
}
)");
        require(!ast_res.has_value(), "_ := rejected at parse time (CF-103)");
        if (!ast_res) {
            require(ast_res.error().contains("cfire/DEPRECATED-DISCARD"),
                "expected cfire/DEPRECATED-DISCARD, got: " + ast_res.error());
        }
    }

    // 8. Type.Variant produces hard error cfire/DEPRECATED-SEPARATOR
    require_sema_error(R"(package main
type Color enum {
    Red,
    Green,
    Blue
}
fn main() {
    c := Color.Red
}
)", "cfire/DEPRECATED-SEPARATOR", "dep_enum_dot.cf");

    // 9. (*p).Field on smart pointers produces hard error cfire/REDUNDANT-DEREF
    require_sema_error(R"(package main
type Box struct {
    val: i32
}
fn main() {
    b := make[own[Box]](Box{val: 42})
    _ = (*b).val
}
)", "cfire/REDUNDANT-DEREF", "dep_smart_deref.cf");

    // 10. Test cfire fix automated repair of deprecations
    {
        ScopedTempDir tmp_fix("cfire_dep_fix");
        auto f = tmp_fix.file("repair.cf", R"(package main

type Box struct {
    val: i32
}

type Status enum {
    Active,
    Idle
}

fn worker(in x: i32) {
    _ = x
}

fn main() {
    _ := 10
    b := make[own[Box]](Box{val: 42})
    v := (*b).val
    st := Status.Active
}
)");
        auto fix_res = run_cfire(std::format("fix '{}'", f.string()));
        fix_res.assert_ok("cfire fix applies deprecation repairs (CF-103)");
        auto chk_res = run_cfire(std::format("check '{}'", f.string()));
        chk_res.assert_ok("repaired file passes cfire check cleanly (CF-103)");
    }

    std::println("--- PASS: test_hard_deprecation_enforcement (CF-103)");
}

void test_duplicate_declaration_pass([[maybe_unused]] const std::filesystem::path& rt_dir) {
    std::println("=== RUN   test_duplicate_declaration_pass (CF-104)");

    // 1. Duplicate free functions in package
    require_sema_error(R"(package main
fn calculate(x: i32) -> i32 { return x * 2; }
fn calculate(y: i32) -> i32 { return y + 1; }
fn main() {}
)", "cfire/DUPLICATE-DECLARATION", "dup_fn.cf");

    // 2. Duplicate methods on the same type
    require_sema_error(R"(package main
type Point struct { x: i32 }
fn (p: Point) length() -> i32 { return p.x; }
fn (p: Point) length() -> i32 { return p.x; }
fn main() {}
)", "cfire/DUPLICATE-DECLARATION", "dup_method.cf");

    // 3. Duplicate structs
    require_sema_error(R"(package main
type Config struct { timeout: i32 }
type Config struct { port: i32 }
fn main() {}
)", "cfire/DUPLICATE-DECLARATION", "dup_struct.cf");

    // 4. Duplicate enums or struct/enum collision
    require_sema_error(R"(package main
type Mode enum { Fast, Slow }
type Mode struct { rate: i32 }
fn main() {}
)", "cfire/DUPLICATE-DECLARATION", "dup_type_collision.cf");

    // 5. Duplicate enum variants in same enum
    require_sema_error(R"(package main
type Level enum { Low, Medium, Low }
fn main() {}
)", "cfire/DUPLICATE-DECLARATION", "dup_variant.cf");

    // 6. Duplicate global variables
    require_sema_error(R"(package main
TIMEOUT := 5000
TIMEOUT := 10000
fn main() {}
)", "cfire/DUPLICATE-DECLARATION", "dup_global.cf");

    // 7. Enum variant collisions across different enums scoped properly without false positives
    run_cfire_source(R"(package main

type TaskState enum {
    Pending,
    Done
}

type JobState enum {
    Pending,
    Running,
    Done
}

fn check_task(t: TaskState) -> str {
    return match t {
        Pending => "task_pending",
        Done => "task_done",
    }
}

fn main() {
    println(check_task(TaskState::Pending))
}
)", "enum_scope_test")
        .assert_ok("cross-enum variant matching succeeds when scoped to target enum (CF-104)")
        .assert_contains("task_pending", "evaluates correct arm");

    // 8. Unknown enum variant diagnostic
    require_sema_error(R"(package main
type Direction enum { North, South }
fn get_heading(d: Direction) -> i32 {
    return match d {
        North => 1,
        South => 2,
        East => 3,
    }
}
fn main() {}
)", "cfire/UNKNOWN-ENUM-VARIANT", "unknown_variant.cf");

    std::println("--- PASS: test_duplicate_declaration_pass (CF-104)");
}

void test_catalog_gated_import_validation([[maybe_unused]] const std::filesystem::path& rt_dir) {
    std::println("=== RUN   test_catalog_gated_import_validation (CF-105)");

    // 1. Unknown standard library import with typo suggests closest module (CF-105)
    require_sema_error(R"(package main
import "core:tyme"
fn main() {}
)", "cfire/UNKNOWN-MODULE", "unknown_mod_tyme.cf");

    // Verify the error text contains the suggestion
    {
        auto ast = cfire::frontend::parse_source("tyme.cf", "package main\nimport \"core:tyme\"\nfn main() {}\n");
        require(ast.has_value(), "parse tyme.cf");
        if (ast) {
            auto chk = cfire::frontend::analyze_resilient(std::move(*ast));
            require(!chk.errors.empty(), "emits error for unknown module core:tyme");
            if (!chk.errors.empty()) {
                require(chk.errors[0].contains("unknown module 'core:tyme'"), "contains 'unknown module 'core:tyme''");
                require(chk.errors[0].contains("did you mean 'core:time'?"), "contains suggestion 'did you mean 'core:time'?'");
                require(chk.errors[0].contains("(cfire/UNKNOWN-MODULE)"), "contains error code cfire/UNKNOWN-MODULE");
            }
        }
    }

    // 2. Unknown ext import with typo suggests closest module
    {
        auto ast = cfire::frontend::parse_source("sqllite.cf", "package main\nimport \"ext:sqllite\"\nfn main() {}\n");
        require(ast.has_value(), "parse sqllite.cf");
        if (ast) {
            auto chk = cfire::frontend::analyze_resilient(std::move(*ast));
            require(!chk.errors.empty(), "emits error for unknown module ext:sqllite");
            if (!chk.errors.empty()) {
                require(chk.errors[0].contains("unknown module 'ext:sqllite'"), "contains 'unknown module 'ext:sqllite''");
                require(chk.errors[0].contains("(cfire/UNKNOWN-MODULE)"), "contains error code cfire/UNKNOWN-MODULE");
            }
        }
    }

    // 3. Valid canonical core and ext imports pass validation without error
    {
        auto ast = cfire::frontend::parse_source("valid.cf", "package main\nimport \"core:time\"\nimport \"core:fs\"\nimport \"ext:db/sqlite\"\nfn main() {}\n");
        require(ast.has_value(), "parse valid.cf");
        if (ast) {
            auto chk = cfire::frontend::analyze(std::move(*ast));
            require(chk.has_value(), "valid imports pass catalog validation");
        }
    }

    std::println("--- PASS: test_catalog_gated_import_validation (CF-105)");
}

void test_plain_enum_usability_cluster([[maybe_unused]] const std::filesystem::path& rt_dir) {
    std::println("=== RUN   test_plain_enum_usability_cluster (CF-106)");

    run_cfire_source(R"CF(package main

import "core:json"

type Color enum {
    Red
    Green
    Blue
}

type Item struct {
    name: str
    color: Color
}

fn main() {
    c1 := Color::Red
    c2 := Color::Green
    c3 := Color::Red

    // 1. Equality and inequality
    assert(c1 == c3, "c1 == c3")
    assert(c1 != c2, "c1 != c2")
    assert(c1 == Color::Red, "c1 == Color::Red")
    assert(c2 == Color::Green, "c2 == Color::Green")

    // 2. String conversion & interpolation
    s := str(c1)
    assert_eq(s, "Red")
    assert_eq(c2.to_string(), "Green")
    println("{c1}")
    println("Color is {c2}")

    // 3. Integer casting & static factories
    i := i32(c2)
    assert_eq(i, 1)
    i_blue := i32(Color::Blue)
    assert_eq(i_blue, 2)

    c_opt := Color.from_i32(0)
    assert(c_opt != none, "c_opt is some")
    assert((c_opt or Color::Blue) == Color::Red, "c_opt is Red")

    c_from_str := Color.from_string("Blue")
    assert(c_from_str != none, "c_from_str is some")
    assert((c_from_str or Color::Red) == Color::Blue, "c_from_str is Blue")

    c_invalid := Color.from_i32(99)
    assert(c_invalid == none, "invalid idx is none")
    c_invalid_str := Color.from_string("Purple")
    assert(c_invalid_str == none, "invalid string is none")

    // 4. JSON serialization & parsing
    raw_json := json.stringify(c2)
    assert_eq(raw_json, "\"Green\"")

    parsed_color := try json.parse[Color]("\"Blue\"")
    assert(parsed_color == Color::Blue, "parsed Blue")

    // 5. In struct reflection
    item := Item{name: "Widget", color: Color::Green}
    item_json := json.stringify(item)
    assert(item_json.contains("\"color\":\"Green\""), "struct json contains color")

    parsed_item := try json.parse[Item](item_json)
    assert_eq(parsed_item.name, "Widget")
    assert(parsed_item.color == Color::Green, "parsed item color is Green")
}
)CF", "enum_cluster_test")
        .assert_ok("plain enum usability cluster runs and passes all assertions (CF-106)")
        .assert_contains("Red", "prints interpolated enum")
        .assert_contains("Color is Green", "prints formatted enum string");

    std::println("--- PASS: test_plain_enum_usability_cluster (CF-106)");
}

void test_uniform_generic_type_application([[maybe_unused]] const std::filesystem::path& rt_dir) {
    std::println("=== RUN   test_uniform_generic_type_application (CF-107)");

    run_cfire_source(R"CF(package main

import "core:collections"

fn make_pair(a: i32, b: str) -> cpp.std::pair[i32, str] {
    return make[cpp.std::pair[i32, str]](a, b)
}

fn inspect_set(s: collections.Set[str]) -> usize {
    return s.size()
}

fn main() {
    // 1. Generic Set
    mut s := collections.Set[str]{}
    s.insert("apple")
    s.insert("banana")
    assert(s.contains("apple"), "set contains apple")
    assert(!s.contains("cherry"), "set does not contain cherry")
    assert(s.size() == usize(2), "set size is 2")
    assert(inspect_set(s) == usize(2), "function takes Set[str] parameter")

    // 2. Generic Deque
    mut dq := collections.Deque[i64]{}
    dq.push_back(100)
    dq.push_front(50)
    assert(dq.size() == usize(2), "deque size is 2")
    v1 := dq.pop_front()
    assert(v1 == 50, "deque pop_front is 50")
    v2 := dq.pop_back()
    assert(v2 == 100, "deque pop_back is 100")
    assert(dq.empty(), "deque is empty")

    // 3. Generic PriorityQueue
    mut pq := collections.PriorityQueue[i32]{}
    pq.push(10)
    pq.push(30)
    pq.push(20)
    assert(pq.size() == usize(3), "pq size is 3")
    assert(pq.top() == 30, "pq top is 30")

    // 4. Multi-argument generic C++ type application
    p := make_pair(42, "galaxy")
    assert(p.first == 42, "pair.first is 42")
    assert(p.second == "galaxy", "pair.second is galaxy")

    p2 := cpp.std::pair[i32, str](100, "stars")
    assert(p2.first == 100, "p2.first is 100")
    assert(p2.second == "stars", "p2.second is stars")

    println("uniform generic application passed")
}
)CF", "generic_type_app_test")
        .assert_ok("uniform generic type application passes all assertions (CF-107)")
        .assert_contains("uniform generic application passed", "prints confirmation output");

    std::println("--- PASS: test_uniform_generic_type_application (CF-107)");
}

void test_cpp_backend_error_leak_prevention([[maybe_unused]] const std::filesystem::path& rt_dir) {
    std::println("=== RUN   test_cpp_backend_error_leak_prevention (CF-108)");

    // 1. Verify 2-argument error("msg", code) compiles, runs, and exposes .message() and .code()
    run_cfire_source(R"CF(package main

fn fail_with_code(code: i32) -> i32! {
    if code != 0 {
        return error("entity not found", 404)
    }
    return 100
}

fn main() {
    res := fail_with_code(1) or err {
        assert(err.message() == "entity not found", "error message matches")
        assert(err.code() == 404, "error code matches")
        println("error with code handled: {} (code {})", err.message(), err.code())
        return
    }
    assert(false, "expected error branch")
}
)CF", "error_code_test")
        .assert_ok("2-argument error(msg, code) compiles and executes correctly (CF-108)")
        .assert_contains("error with code handled: entity not found (code 404)", "prints error details");

    // 2. Sema negative test: error() with 0 arguments or >2 arguments
    require_sema_error(R"(package main
fn bad_err() -> i32! {
    return error()
}
fn main() {}
)", "cfire/WRONG-ARG-COUNT", "error_zero_args.cf");

    require_sema_error(R"(package main
fn bad_err() -> i32! {
    return error("msg", 1, 2)
}
fn main() {}
)", "cfire/WRONG-ARG-COUNT", "error_three_args.cf");

    // 3. Sema negative test: dynamic slice or array of interface ([]Interface)
    require_sema_error(R"(package main
type Shape interface {
    area() -> f64
}
fn main() {
    mut shapes: []Shape = []
}
)", "cfire/DYNAMIC-INTERFACE-UNSUPPORTED", "slice_of_interface_var.cf");

    require_sema_error(R"(package main
type Shape interface {
    area() -> f64
}
type Canvas struct {
    shapes: []Shape
}
fn main() {}
)", "cfire/DYNAMIC-INTERFACE-UNSUPPORTED", "slice_of_interface_struct.cf");

    require_sema_error(R"(package main
type Shape interface {
    area() -> f64
}
fn render(shapes: []Shape) {}
fn main() {}
)", "cfire/DYNAMIC-INTERFACE-UNSUPPORTED", "slice_of_interface_fn_param.cf");

    require_sema_error(R"(package main
type Shape interface {
    area() -> f64
}
fn get_shapes() -> []Shape {
    return []
}
fn main() {}
)", "cfire/DYNAMIC-INTERFACE-UNSUPPORTED", "slice_of_interface_fn_ret.cf");

    // 4. Runtime string passed as format string to println, print, format
    run_cfire_source(R"CF(package main

fn main() {
    fmt_str := "value is {} and label is {}"
    println(fmt_str, 42, "star")
    print(fmt_str, 99, "planet")
    println()
    formatted := format(fmt_str, 123, "moon")
    assert(formatted == "value is 123 and label is moon", "runtime format string matches")
    println("runtime formatting passed")
}
)CF", "runtime_fmt_test")
        .assert_ok("runtime string format in println and format executes cleanly (CF-108)")
        .assert_contains("value is 42 and label is star", "runtime println works")
        .assert_contains("runtime formatting passed", "assert passed");

    // 5. Channel range-for iteration
    run_cfire_source(R"CF(package main
import "core:sync"

fn main() {
    ch := sync.make_channel[i64](5)
    ch.send(10)
    ch.send(20)
    ch.send(30)
    ch.close()

    mut sum := 0
    for val in ch {
        sum += val
    }
    assert(sum == 60, "channel range-for accumulated values")
    println("channel range iteration passed")
}
)CF", "channel_range_test")
        .assert_ok("channel range-for iteration executes cleanly (CF-108)")
        .assert_contains("channel range iteration passed", "channel test output");

    std::println("--- PASS: test_cpp_backend_error_leak_prevention (CF-108)");
}

void test_scoped_name_resolution_cpp_ffi([[maybe_unused]] const std::filesystem::path& rt_dir) {
    std::println("=== RUN   test_scoped_name_resolution_cpp_ffi (CF-109)");

    // 1. Sema Negative: File with import cpp still checks pure cfire variables and catches typos
    require_sema_error(R"(package main
import cpp "<cstdio>"

fn test_typo() {
    val := 42
    result := val + typo_variable
}
fn main() {}
)", "cfire/UNDECLARED-IDENT", "cpp_ffi_typo.cf");

    // 2. Sema Negative: File with import cpp catches redundant try on non-fallible expression
    require_sema_error(R"(package main
import cpp "<cstdio>"

fn compute() -> i32 { return 10 }

fn test_redundant() -> void! {
    x := try compute()
}
fn main() {}
)", "cfire/REDUNDANT-TRY", "cpp_ffi_redundant_try.cf");

    // 3. Sema Negative: File with import cpp catches redundant or on non-fallible expression
    require_sema_error(R"(package main
import cpp "<cstdio>"

fn compute() -> i32 { return 10 }

fn test_redundant_or() {
    x := compute() or 0
}
fn main() {}
)", "cfire/REDUNDANT-OR", "cpp_ffi_redundant_or.cf");

    // 4. Conformance Positive: File with import cpp resolves cpp. symbols and pure cfire variables
    run_cfire_source(R"CF(package main
import cpp "<cstdio>"

fn add_pure(a: i32, b: i32) -> i32 {
    return a + b
}

fn main() {
    pure_res := add_pure(15, 25)
    assert(pure_res == 40, "pure cfire function call succeeds")

    cpp.printf("printf from import cpp: %d\n", pure_res)
    println("scoped resolution with cpp ffi passed")
}
)CF", "cpp_ffi_resolution_test")
        .assert_ok("import cpp file with pure cfire and cpp. calls compiles and runs (CF-109)")
        .assert_contains("printf from import cpp: 40", "cpp.printf output verified")
        .assert_contains("scoped resolution with cpp ffi passed", "completion output verified");

    // 5. Hard Error on cpp:: separator (cfire/DEPRECATED-SEPARATOR)
    require_sema_error(R"(package main
import cpp "<cstdio>"
fn main() {
    cpp::printf("hi\n")
}
)", "cfire/DEPRECATED-SEPARATOR", "cpp_colon_call.cf");

    // 6. Hard Error on :: in type references (var decl, struct field, fn sig)
    require_sema_error(R"(package main
import cpp "<cstdio>"
fn main() {
    mut f: *cpp::FILE = none
}
)", "cfire/DEPRECATED-SEPARATOR", "cpp_colon_var.cf");

    require_sema_error(R"(package main
import cpp "<cstdio>"
type FileHandle struct {
    f: *cpp::FILE
}
fn main() {}
)", "cfire/DEPRECATED-SEPARATOR", "cpp_colon_field.cf");

    require_sema_error(R"(package main
import cpp "<cstdio>"
fn get_file(f: *cpp::FILE) -> *cpp::FILE {
    return f
}
fn main() {}
)", "cfire/DEPRECATED-SEPARATOR", "cpp_colon_fn.cf");

    // 7. Auto-repair fix-it: cfire fix replaces cpp:: with cpp.
    {
        ScopedTempDir tmp_fix("cfire_cpp_sep_fix");
        auto f = tmp_fix.file("cpp_sep.cf", R"(package main
import cpp "<cstdio>"
fn main() {
    cpp::printf("hello world\n")
}
)");
        auto fix_res = run_cfire(std::format("fix '{}'", f.string()));
        fix_res.assert_ok("cfire fix applies cpp:: -> cpp. replacement");
        auto fixed_content = cfire::fs::read_text(f).value_or("");
        require(fixed_content.contains("cpp.printf"), "fixed content contains cpp.printf");
        require(!fixed_content.contains("cpp::printf"), "fixed content does not contain cpp::printf");
        auto chk_res = run_cfire(std::format("check '{}'", f.string()));
        chk_res.assert_ok("fixed file passes check cleanly");
    }

    // 8. Aliased C++ import execution and syntax validation
    run_cfire_source(R"CF(package main
import cpp "<cstdio>" as my_io

fn main() {
    my_io.printf("aliased cpp printf works: %d\n", 42)
    println("aliased cpp test passed")
}
)CF", "cpp_alias_exec_test")
        .assert_ok("import cpp as alias executes cleanly")
        .assert_contains("aliased cpp printf works: 42")
        .assert_contains("aliased cpp test passed");

    require_sema_error(R"(package main
import cpp "<cstdio>" as my_io
fn main() {
    my_io::printf("hi\n")
}
)", "cfire/DEPRECATED-SEPARATOR", "cpp_alias_colon.cf");

    // 9. Exclusive Aliasing: Shadowed default qualifiers rejected with fix-it
    require_sema_error(R"(package main
import cpp "<cstdio>" as my_io
fn main() {
    cpp.printf("hi\n")
}
)", "module 'cpp' is imported as 'my_io'; use 'my_io.printf'", "shadowed_cpp.cf");

    require_sema_error(R"(package main
import "core:str" as s
fn main() {
    parts := str.split("a,b", ",")
}
)", "module 'str' is imported as 's'; use 's.split'", "shadowed_core.cf");

    // 10. Bare unqualified C calls rejected with targeted hint
    require_sema_error(R"(package main
import cpp "<cstdio>"
fn main() {
    printf("hi\n")
}
)", "did you mean 'cpp.printf'?", "bare_c_call.cf");

    require_sema_error(R"(package main
import cpp "<cstdio>" as my_io
fn main() {
    printf("hi\n")
}
)", "did you mean 'my_io.printf'?", "bare_c_call_alias.cf");

    // 11. Strict CF-116 type checks in files with import cpp
    require_sema_error(R"(package main
import cpp "<cstdio>"
fn main() {
    x: i32 = "hello"
}
)", "cfire/TYPE-MISMATCH", "cpp_type_mismatch_var.cf");

    require_sema_error(R"(package main
import cpp "<cstdio>"
fn main() {
    x := 10 + "hello"
}
)", "cfire/TYPE-MISMATCH", "cpp_type_mismatch_bin.cf");

    require_sema_error(R"(package main
import cpp "<cstdio>"
fn get_num() -> i32 {
    return "hello"
}
fn main() {}
)", "cfire/TYPE-MISMATCH", "cpp_type_mismatch_ret.cf");

    require_sema_error(R"(package main
import cpp "<cstdio>"
fn main() {
    mut x: i32 = 0
    x = "hello"
}
)", "cfire/TYPE-MISMATCH", "cpp_type_mismatch_assign.cf");

    std::println("--- PASS: test_scoped_name_resolution_cpp_ffi (CF-109)");
}

void test_match_exhaustiveness_and_reachability([[maybe_unused]] const std::filesystem::path& rt_dir) {
    std::println("=== RUN   test_match_exhaustiveness_and_reachability (CF-110)");

    // 1. Sema Negative: Statement match on string without wildcard fallback triggers cfire/NON-EXHAUSTIVE-MATCH
    require_sema_error(R"(package main
fn classify(s: str) {
    match s {
        "apple" => { println("fruit") }
        "carrot" => { println("vegetable") }
    }
}
fn main() {}
)", "cfire/NON-EXHAUSTIVE-MATCH", "match_stmt_str_no_wildcard.cf");

    // 2. Sema Negative: Statement match on integer without wildcard fallback triggers cfire/NON-EXHAUSTIVE-MATCH
    require_sema_error(R"(package main
fn classify_int(n: i32) {
    match n {
        1 => { println("one") }
        2 => { println("two") }
    }
}
fn main() {}
)", "cfire/NON-EXHAUSTIVE-MATCH", "match_stmt_int_no_wildcard.cf");

    // 3. Sema Negative: Duplicate enum variant triggers cfire/UNREACHABLE-MATCH-ARM
    require_sema_error(R"(package main
type Color enum { Red, Green, Blue }
fn test_dup(c: Color) {
    match c {
        Color::Red => { println("red") }
        Color::Red => { println("duplicate red") }
        _ => {}
    }
}
fn main() {}
)", "cfire/UNREACHABLE-MATCH-ARM", "match_duplicate_variant.cf");

    // 4. Sema Negative: Unknown/misspelled enum variant even with wildcard triggers cfire/UNKNOWN-ENUM-VARIANT
    require_sema_error(R"(package main
type Color enum { Red, Green, Blue }
fn test_typo(c: Color) {
    match c {
        Redd => { println("typo") }
        _ => {}
    }
}
fn main() {}
)", "cfire/UNKNOWN-ENUM-VARIANT", "match_typo_variant_with_wildcard.cf");

    // 5. Conformance Positive: Match statement on string with wildcard executes cleanly
    run_cfire_source(R"CF(package main

fn classify(s: str) -> str {
    mut res := ""
    match s {
        "apple" => { res = "fruit" }
        "carrot" => { res = "vegetable" }
        _ => { res = "unknown" }
    }
    return res
}

fn main() {
    assert(classify("apple") == "fruit", "matches apple")
    assert(classify("carrot") == "vegetable", "matches carrot")
    assert(classify("rock") == "unknown", "matches wildcard")
    println("match statement exhaustiveness passed")
}
)CF", "match_stmt_str_test")
        .assert_ok("statement match on string with wildcard executes cleanly (CF-110)")
        .assert_contains("match statement exhaustiveness passed", "test completion confirmed");

    std::println("--- PASS: test_match_exhaustiveness_and_reachability (CF-110)");
}

void test_or_chaining_and_block_disambiguation([[maybe_unused]] const std::filesystem::path& rt_dir) {
    std::println("=== RUN   test_or_chaining_and_block_disambiguation (CF-111)");

    // 1. Sema Negative: Redundant 'or' on non-fallible expression triggers cfire/REDUNDANT-OR
    require_sema_error(R"(package main
fn main() {
    x := 42
    y := x or 10
}
)", "cfire/REDUNDANT-OR", "redundant_or_non_fallible.cf");

    // 2. Parser Negative: Naked anonymous struct literal in 'if' condition is rejected
    {
        std::string naked_src = "package main\nfn main() {\n    if true && { x: 1 } {\n        println(\"naked struct literal in if head\")\n    }\n}\n";
        auto ast = cfire::frontend::parse_source("naked_struct_lit_in_if.cf", naked_src);
        require(!ast.has_value() && ast.error().contains("unexpected '{' in expression"),
            "naked struct literal in if head rejected with unexpected '{' error");
    }

    // 3. Conformance Positive: Left-associative chained 'or' and struct literal fallback
    run_cfire_source(R"CF(package main

type Point struct {
    x: int
    y: int
}

fn try_parse(s: str) -> int! {
    if s == "bad" {
        return error("parse error")
    }
    if s == "42" {
        return 42
    }
    if s == "100" {
        return 100
    }
    return error("unknown number")
}

fn test_chained_fallback() -> int {
    // Left-associative: (try_parse("bad") or try_parse("bad2")) or 77
    val := try_parse("bad") or try_parse("bad2") or 77
    return val
}

fn test_chained_first_hit() -> int {
    val := try_parse("42") or try_parse("100") or 999
    return val
}

fn test_chained_second_hit() -> int {
    val := try_parse("bad") or try_parse("100") or 999
    return val
}

fn test_chained_block_handler() -> int {
    val := try_parse("bad") or try_parse("bad2") or err {
        return -1
    }
    return val
}

fn get_point(fail: bool) -> Point! {
    if fail {
        return error("no point")
    }
    return Point{ x: 10, y: 20 }
}

fn test_struct_lit_fallback() -> Point {
    // Disambiguates struct literal fallback outside if head
    p := get_point(true) or Point{ x: 99, y: 88 }
    return p
}

fn test_if_or_err_handler(fail: bool) -> int {
    mut result := 0
    mut arg := "42"
    if fail {
        arg = "bad"
    }
    if (try_parse(arg) or err { result = -10; return result }) == 42 {
        result = 1
    }
    return result
}

fn main() {
    assert(test_chained_fallback() == 77, "chains to final non-fallible fallback")
    assert(test_chained_first_hit() == 42, "short-circuits on first hit")
    assert(test_chained_second_hit() == 100, "chains through to second hit")
    assert(test_chained_block_handler() == -1, "executes chained block error handler")

    p := test_struct_lit_fallback()
    assert(p.x == 99 && p.y == 88, "parses struct literal fallback cleanly")

    assert(test_if_or_err_handler(false) == 1, "handles non-error in if condition")
    assert(test_if_or_err_handler(true) == -10, "handles error in if condition")

    println("or chaining and block disambiguation passed")
}
)CF", "or_chaining_disambiguation_test")
        .assert_ok("chained or and struct literal disambiguation execute cleanly (CF-111)")
        .assert_contains("or chaining and block disambiguation passed", "test completion confirmed");

    std::println("--- PASS: test_or_chaining_and_block_disambiguation (CF-111)");
}

void test_lexer_exponents_keywords_and_interpolation([[maybe_unused]] const std::filesystem::path& rt_dir) {
    std::println("=== RUN   test_lexer_exponents_keywords_and_interpolation (CF-112)");

    // 1. Parser Negative: 'break' and 'continue' are reserved keywords and cannot be variable names
    {
        std::string break_var_src = "package main\nfn main() {\n    break := 1\n}\n";
        auto ast = cfire::frontend::parse_source("break_var.cf", break_var_src);
        require(!ast.has_value(), "declaring variable named 'break' is rejected as a syntax error");
    }
    {
        std::string cont_var_src = "package main\nfn main() {\n    continue := 2\n}\n";
        auto ast = cfire::frontend::parse_source("cont_var.cf", cont_var_src);
        require(!ast.has_value(), "declaring variable named 'continue' is rejected as a syntax error");
    }

    // 2. Sema Negative: 'break' outside for loop triggers cfire/BREAK-OUTSIDE-LOOP
    require_sema_error(R"(package main
fn main() {
    break
}
)", "cfire/BREAK-OUTSIDE-LOOP", "break_outside_loop.cf");

    // 3. Sema Negative: 'continue' outside for loop triggers cfire/BREAK-OUTSIDE-LOOP
    require_sema_error(R"(package main
fn main() {
    continue
}
)", "cfire/BREAK-OUTSIDE-LOOP", "continue_outside_loop.cf");

    // 4. Sema Negative: Ambiguous string interpolation starting with a number literal triggers cfire/AMBIGUOUS-INTERPOLATION
    require_sema_error(R"(package main
fn main() {
    msg := "{1+2}"
}
)", "cfire/AMBIGUOUS-INTERPOLATION", "ambiguous_interp.cf");

    // 5. Conformance Positive: Scientific floats, break/continue in loop, parenthesized interpolation, positional formats
    run_cfire_source(R"CF(package main

fn test_scientific_floats() -> bool {
    a := 1e9
    b := 2.5e-3
    c := 1E+6
    assert(a == 1000000000.0, "1e9 == 1e9")
    assert(b > 0.0024 && b < 0.0026, "2.5e-3 in range")
    assert(c == 1000000.0, "1E+6 == 1000000")
    return true
}

fn test_break_and_continue() -> int {
    mut sum := 0
    for i in 0..10 {
        if i == 2 {
            continue
        }
        if i == 6 {
            break
        }
        sum += i
    }
    // Expected sum: 0 + 1 + 3 + 4 + 5 = 13
    return sum
}

fn test_interpolation_and_positional() -> bool {
    // Parenthesized expression interpolation
    calc := "result is {(1 + 2)}"
    assert(calc == "result is 3", "parenthesized interpolation works")

    // Named variable interpolation
    x := 42
    named := "x is {x}"
    assert(named == "x is 42", "named variable interpolation works")

    return true
}

fn main() {
    assert(test_scientific_floats(), "scientific floats pass")
    assert(test_break_and_continue() == 13, "break and continue in loop execute correctly")
    assert(test_interpolation_and_positional(), "interpolations pass")

    println("lexer exponents keywords and interpolation passed")
}
)CF", "cf112_conformance_test")
        .assert_ok("CF-112 features execute cleanly")
        .assert_contains("lexer exponents keywords and interpolation passed", "test completion confirmed");

    std::println("--- PASS: test_lexer_exponents_keywords_and_interpolation (CF-112)");
}

void test_content_hashed_pch_and_cli_flag_validation([[maybe_unused]] const std::filesystem::path& rt_dir) {
    std::println("=== RUN   test_content_hashed_pch_and_cli_flag_validation (CF-113)");

    // 1. Verify argv command execution directly without shell interpretation
    {
        int exit_code = 0;
        auto out = cfire::bridge::run_argv_capture({"echo", "cfire_argv_test_ok"}, exit_code);
        require(exit_code == 0, "run_argv_capture succeeds on valid command");
        require(out.find("cfire_argv_test_ok") != std::string::npos, "run_argv_capture captures output correctly");
    }

    // 2. Verify unknown CLI flags produce actionable typo suggestions via Levenshtein distance
    {
        int ec = 0;
        auto out = cfire::bridge::run_command_capture("./cfire build --relase main.cf 2>&1", ec);
        require(ec != 0, "mistyped flag --relase fails with non-zero exit code");
        require(out.find("did you mean '--release'?") != std::string::npos,
            "mistyped flag suggests '--release'");
    }
    {
        int ec = 0;
        auto out = cfire::bridge::run_command_capture("./cfire build --targt wasm main.cf 2>&1", ec);
        require(ec != 0, "mistyped flag --targt fails with non-zero exit code");
        require(out.find("did you mean '--target'?") != std::string::npos,
            "mistyped flag suggests '--target'");
    }
    {
        int ec = 0;
        auto out = cfire::bridge::run_command_capture("./cfire build --totally-unrecognized-xyz main.cf 2>&1", ec);
        require(ec != 0, "unrecognized flag fails with non-zero exit code");
        require(out.find("unrecognized flag '--totally-unrecognized-xyz'") != std::string::npos,
            "unrecognized flag produces clean error without crash");
    }

    // 3. Verify user-private PCH cache exists and has content hash tag
    {
        std::error_code ec;
#if !defined(_WIN32)
        auto uid = ::getuid();
        auto pch_dir = std::filesystem::temp_directory_path(ec) / std::format("cfire-{}-pch", uid);
        require(std::filesystem::exists(pch_dir, ec), "user-private PCH cache directory exists");
#endif
    }

    std::println("--- PASS: test_content_hashed_pch_and_cli_flag_validation (CF-113)");
}

void test_multiline_lambda_source_mapping_cf114([[maybe_unused]] const std::filesystem::path& rt_dir) {
    std::println("=== RUN   test_multiline_lambda_source_mapping (CF-114)");

    // 1. Verify #line directive re-emission following a multi-line closure
    {
        std::string src = 
            "package main\n"                         // 1
            "\n"                                     // 2
            "fn test_multiline_lambda() -> i32 {\n"  // 3
            "    cl := |x: i32| -> i32 {\n"          // 4
            "        y := x * 2\n"                   // 5
            "        z := y + 3\n"                   // 6
            "        return z\n"                     // 7
            "    }\n"                                // 8
            "    res := cl(10)\n"                    // 9
            "    return res\n"                       // 10
            "}\n";                                   // 11

        auto parse_res = cfire::frontend::parse_source("test_cl.cf", src);
        require(parse_res.has_value(), "parse test_cl.cf succeeds");
        auto check_res = cfire::frontend::analyze(std::move(*parse_res));
        require(check_res.has_value(), "analyze test_cl.cf succeeds");

        std::string cpp = cfire::frontend::emit_cpp(*check_res, false);
        // Verify #line directives inside closure
        require(cpp.find("#line 4 \"test_cl.cf\"") != std::string::npos, "emits #line 4 for closure assignment");
        require(cpp.find("#line 5 \"test_cl.cf\"") != std::string::npos, "emits #line 5 inside closure");
        require(cpp.find("#line 6 \"test_cl.cf\"") != std::string::npos, "emits #line 6 inside closure");
        require(cpp.find("#line 7 \"test_cl.cf\"") != std::string::npos, "emits #line 7 inside closure");
        // Crucial CF-114 check: ensure #line 9 is emitted for the statement following the multi-line closure
        require(cpp.find("#line 9 \"test_cl.cf\"") != std::string::npos, "emits #line 9 following multi-line closure");
        require(cpp.find("#line 10 \"test_cl.cf\"") != std::string::npos, "emits #line 10 for return statement");
    }

    // 2. Verify #line directive re-emission following hoisted try unwrap checks
    {
        std::string src =
            "package main\n"                         // 1
            "\n"                                     // 2
            "fn try_op() -> i32! { return 5 }\n"     // 3
            "\n"                                     // 4
            "fn test_subexpr_try() -> i32! {\n"      // 5
            "    val := (try try_op()) + 10\n"       // 6
            "    final_res := val * 2\n"             // 7
            "    return final_res\n"                 // 8
            "}\n";                                   // 9

        auto parse_res = cfire::frontend::parse_source("test_try.cf", src);
        require(parse_res.has_value(), "parse test_try.cf succeeds");
        auto check_res = cfire::frontend::analyze(std::move(*parse_res));
        require(check_res.has_value(), "analyze test_try.cf succeeds");

        std::string cpp = cfire::frontend::emit_cpp(*check_res, false);
        // The hoisted try check should be followed by re-emitting #line 6 before the assignment
        require(cpp.find("#line 6 \"test_try.cf\"") != std::string::npos, "emits #line 6 for hoisted subexpr try");
        require(cpp.find("#line 7 \"test_try.cf\"") != std::string::npos, "emits #line 7 for statement following hoisted try");
    }

    // 3. Verify synthetic line directive resets attribution to <cfire-synthetic>
    {
        cfire::frontend::CheckedFile dummy_cf;
        cfire::frontend::Emitter emitter(dummy_cf, false);
        emitter.set_source_path("some_file.cf");
        emitter.emit_synthetic_line_directive();
        std::string out = emitter.emit_cpp();
        require(out.find("#line 1 \"<cfire-synthetic>\"") != std::string::npos,
            "emit_synthetic_line_directive produces synthetic line marker");
    }

    // 4. Conformance Positive: Multiline closures and hoisted tries run accurately
    run_cfire_source(R"CF(package main

fn fallible_square(n: i32) -> i32! {
    return n * n
}

fn compute() -> i32! {
    cl := |a: i32| -> i32 {
        step1 := a + 1
        step2 := step1 * 2
        return step2
    }
    val := cl(3)
    res := (try fallible_square(val)) + 10
    return res
}

fn main() {
    ans := compute() or 0
    assert(ans == 74, "multiline lambda and try unwrap line mappings executed accurately")
    println("cf114 line mapping test completed")
}
)CF", "cf114_conformance_test")
        .assert_ok("CF-114 features execute cleanly")
        .assert_contains("cf114 line mapping test completed", "test completion confirmed");

    std::println("--- PASS: test_multiline_lambda_source_mapping (CF-114)");
}

void test_interned_typeid_lattice_and_diagnostics_cf116([[maybe_unused]] const std::filesystem::path& rt_dir) {
    std::println("=== RUN   test_interned_typeid_lattice_and_diagnostics (CF-116)");

    // 1. Interned TypeId Lattice canonicalization and assignability
    {
        cfire::frontend::TypeTable tt;
        auto id_i32 = cfire::frontend::TypeId::Int32();
        auto id_i64 = cfire::frontend::TypeId::Int64();
        auto id_str = cfire::frontend::TypeId::Str();

        require(id_i32.is_valid() && !id_i32.is_unknown(), "i32 is valid non-unknown TypeId");
        require(tt.to_string(id_i32) == "i32", "i32 string representation matches");
        require(tt.to_string(id_str) == "str", "str string representation matches");

        auto slice_1 = tt.make_slice(id_i32);
        auto slice_2 = tt.make_slice(id_i32);
        require(slice_1 == slice_2, "make_slice canonicalizes TypeId");
        require(tt.from_name("[]i32") == slice_1, "from_name('[]i32') resolves to interned slice TypeId");

        auto opt_1 = tt.make_optional(id_i32);
        auto opt_2 = tt.make_optional(id_i32);
        require(opt_1 == opt_2, "make_optional canonicalizes TypeId");
        require(tt.unwrap_optional(opt_1) == id_i32, "unwrap_optional recovers base element TypeId");

        auto fal_1 = tt.make_fallible(id_i32);
        require(tt.unwrap_fallible(fal_1) == id_i32, "unwrap_fallible recovers base element TypeId");

        require(tt.is_assignable(id_i64, id_i32), "integer types are assignable");
        require(!tt.is_assignable(id_str, id_i32), "str and i32 are not assignable");
        require(tt.is_assignable(opt_1, id_i32), "optional is assignable from base element");
    }

    // 2. Negative Verification: Type mismatch on variable assignment triggers cfire/TYPE-MISMATCH
    require_sema_error(R"(package main
fn main() {
    mut x: i32 = 10
    x = "invalid string"
}
)", "cfire/TYPE-MISMATCH", "type_mismatch_assign.cf");

    // 3. Negative Verification: Type mismatch on variable declaration triggers cfire/TYPE-MISMATCH
    require_sema_error(R"(package main
fn main() {
    x: i32 = "invalid string"
}
)", "cfire/TYPE-MISMATCH", "type_mismatch_init.cf");

    // 4. Negative Verification: Type mismatch on return statement triggers cfire/TYPE-MISMATCH
    require_sema_error(R"(package main
fn bad_return() -> i32 {
    return "invalid return"
}
fn main() {}
)", "cfire/TYPE-MISMATCH", "type_mismatch_return.cf");

    // 5. Negative Verification: Binary operator operand mismatch triggers cfire/TYPE-MISMATCH
    require_sema_error(R"(package main
fn main() {
    x := 10 + true
}
)", "cfire/TYPE-MISMATCH", "type_mismatch_binary.cf");

    // 5b. Q3 canonical type identity: one package-qualified type, three internal spellings
    // (annotation joined with '::', stdlib catalog bare return, inferred variable) are the same type.
    require_sema_ok(R"(package main
import "ext:webui"

fn make_app() -> webui.App {
    mut app := webui.new_app("Studio", 640, 480)
    return app
}

fn direct_app() -> webui.App {
    return webui.new_app("Direct", 320, 240)
}

fn main() {
    a: webui.App = make_app()
    mut b := direct_app()
    b = make_app()
}
)", "q3_qualified_type_identity.cf");

    // 5c. Q3: enum variant paths ('::') are untouched by package canonicalization.
    require_sema_ok(R"(package main
import "core:log"

fn pick() -> log.Level {
    return log.Level::Debug
}

fn main() {
    lvl: log.Level = pick()
    _ = lvl
}
)", "q3_enum_variant_path.cf");

    // 5d. Negative: a local type and a package type sharing a leaf name are distinct (no leaf matching),
    // and the diagnostic explains the difference.
    require_sema_error(R"(package main
import "ext:webui"

type App struct {
    title: str
}

fn wrong() -> webui.App {
    return App{title: "local"}
}

fn main() {}
)", "'App' is a local type, distinct from 'webui.App' (did you mean 'webui.App'?)", "q3_local_vs_package_type.cf");

    // 5e. Diagnostics print the Q3 source spelling ('webui.App'), not the parser's internal 'webui::App'.
    require_sema_error(R"(package main
import "ext:webui"

fn wrong() -> webui.App {
    return 42
}

fn main() {}
)", "expects 'webui.App', got 'i64'", "q3_diag_spelling.cf");

    // 6. Conformance Positive: Type-safe execution across complex types and fallible returns
    run_cfire_source(R"CF(package main

type Item struct {
    id: i32
    label: str
}

fn fetch_item(n: i32) -> Item! {
    return Item{id: n, label: "widget"}
}

fn process_slice(items: []Item) -> i32 {
    mut sum: i32 = 0
    for it in items {
        sum += it.id
    }
    return sum
}

fn main() {
    it1 := try fetch_item(40)
    it2 := try fetch_item(2)
    mut list: []Item = [it1, it2]
    total := process_slice(list)
    assert(total == 42, "type-checked struct slice operations executed accurately")
    println("cf116 interned type system test completed")
}
)CF", "cf116_conformance_test")
        .assert_ok("CF-116 features execute cleanly")
        .assert_contains("cf116 interned type system test completed", "test completion confirmed");

    std::println("--- PASS: test_interned_typeid_lattice_and_diagnostics (CF-116)");
}

void test_unified_stdlib_metadata_table_cf117(const std::filesystem::path& rt_dir) {
    (void)rt_dir;
    std::println("=== RUN   test_unified_stdlib_metadata_table (CF-117)");

    const auto& catalog = cfire::doc::stdlib_catalog();
    require(catalog.size() >= 39, "stdlib_catalog contains at least 39 modules");

    // Verify all module paths are unique and sorted
    for (size_t i = 1; i < catalog.size(); ++i) {
        require(catalog[i - 1].import_path < catalog[i].import_path,
                std::format("catalog modules strictly sorted: {} < {}", catalog[i - 1].import_path, catalog[i].import_path));
    }

    // Verify module_registry queries match catalog exactly
    const auto known_paths = cfire::modules::known_module_paths();
    require(known_paths.size() >= catalog.size(), "known_module_paths contains all catalog modules and aliases");
    for (size_t i = 0; i < catalog.size(); ++i) {
        require(cfire::modules::is_known_module(catalog[i].import_path), "is_known_module matches catalog");
    }

    // Check core and vendor namespaces
    require(cfire::modules::is_known_core_namespace("os"), "os is known core namespace");
    require(cfire::modules::is_known_core_namespace("build"), "build is known core namespace (bug CF-117 fixed)");
    require(cfire::modules::is_known_core_namespace("time"), "time is known core namespace");
    require(cfire::modules::is_known_core_namespace("aes"), "aes is known core namespace");
    require(cfire::modules::is_known_core_namespace("webui"), "webui is known core namespace");

    // Check resolve_import
    auto resolved_aes = cfire::modules::resolve_import("core:crypto/aes");
    require(resolved_aes.is_core, "core:crypto/aes is core");
    require(!resolved_aes.core_keys.empty() && resolved_aes.core_keys[0] == "crypto", "core:crypto/aes core key is crypto");

    auto resolved_build = cfire::modules::resolve_import("core:build");
    require(resolved_build.is_core, "core:build is core");
    require(!resolved_build.core_keys.empty() && resolved_build.core_keys[0] == "buildsys", "core:build core key is buildsys");

    // Check StdlibCatalogIndex
    cfire::frontend::StdlibCatalogIndex cat_idx;
    require(cat_idx.has_namespace("os"), "cat_idx knows os");
    require(cat_idx.has_namespace("aes"), "cat_idx knows aes");
    require(cat_idx.has_namespace("build"), "cat_idx knows build");

    require(cat_idx.ns_to_import.at("aes") == "core:crypto/aes", "cat_idx resolves aes to core:crypto/aes");
    require(cat_idx.ns_to_import.at("build") == "core:build", "cat_idx resolves build to core:build");

    require(cat_idx.is_known_ns_member("http", "Response"), "cat_idx knows http.Response");
    require(cat_idx.is_known_ns_member("time", "Duration"), "cat_idx knows time.Duration");
    require(cat_idx.is_known_ns_member("json", "JsonValue"), "cat_idx knows json.JsonValue");
    require(cat_idx.is_known_ns_member("sync", "Mutex"), "cat_idx knows sync.Mutex");

    auto fn_resp_text = cat_idx.find_ns_fn("http", "Response.text");
    require(fn_resp_text != nullptr, "cat_idx has http.Response.text");
    if (fn_resp_text) {
        require(fn_resp_text->return_type == "Response", "http.Response.text returns Response");
    }

    auto fn_getenv = cat_idx.find_ns_fn("os", "getenv");
    require(fn_getenv != nullptr, "cat_idx has os.getenv");
    if (fn_getenv) {
        require(fn_getenv->return_type == "str", "os.getenv returns str");
    }

    // Verify end-to-end execution with multiple stdlib modules derived from catalog
    run_cfire_source(R"CF(package main

import "core:os"
import "core:time"
import "core:math"
import "core:str"

fn main() {
    start := time.now()
    _ = start
    sq := math.sqrt(16.0)
    assert(sq == 4.0, "math.sqrt returned 4.0")
    rep := str.replace_all("banana", "a", "o")
    assert(rep == "bonono", "str.replace_all executed correctly")
    home := os.getenv("NONEXISTENT_VAR_12345", "fallback_val")
    assert(home == "fallback_val", "os.getenv fallback")
    println("cf117 metadata catalog unification test verified")
}
)CF", "cf117_stdlib_catalog_test")
        .assert_ok("CF-117 test executes cleanly")
        .assert_contains("cf117 metadata catalog unification test verified", "CF-117 verified output");

    std::println("--- PASS: test_unified_stdlib_metadata_table (CF-117)");
}

void test_unified_diagnostics_pipeline_cf118(const std::filesystem::path& rt_dir) {
    std::println("=== RUN   test_unified_diagnostics_pipeline (CF-118)");

    cfire::analysis::CheckOptions opts;
    opts.runtime_dir = rt_dir;

    // 1. Resilient syntax error checking via check_source
    const std::string bad_syntax = "package main\nfn main() { x := ;\n}\n";
    auto diags1 = cfire::analysis::check_source("bad_syntax.cf", bad_syntax, opts);
    require(!diags1.empty(), "check_source detects syntax error");
    if (!diags1.empty()) {
        require(diags1[0].code == "cfire/SYNTAX", "syntax error code is cfire/SYNTAX");
        require(diags1[0].line >= 1, "syntax error line >= 1");
    }

    // 2. Resilient sema error checking with structured diagnostics and fixits
    const std::string bad_sema = "package main\nfn bump(mut x: i32) { x += 1 }\nfn main() { mut val: i32 = 10; bump(val) }\n";
    auto diags2 = cfire::analysis::check_source("bad_sema.cf", bad_sema, opts);
    require(!diags2.empty(), "check_source detects sema error");
    bool found_mut_callsite = false;
    for (const auto& d : diags2) {
        if (d.code == "cfire/MUT-CALLSITE") {
            found_mut_callsite = true;
            require(d.line == 3, "MUT-CALLSITE at line 3");
            require(!d.fixits.empty(), "MUT-CALLSITE has attached fixit");
            if (!d.fixits.empty()) {
                require(d.fixits[0].replacement == "mut ", "fixit suggests 'mut '");
            }
        }
    }
    require(found_mut_callsite, "found MUT-CALLSITE diagnostic");

    // 3. Serialization to JSON
    std::string json_out = cfire::diag::serialize_diagnostics_json(diags2);
    require(json_out.find("\"code\": \"cfire/MUT-CALLSITE\"") != std::string::npos, "JSON contains diagnostic code");
    require(json_out.find("\"fixits\"") != std::string::npos, "JSON contains fixits");

    // 4. Clean source check returns 0 diagnostics
    const std::string clean_src = "package main\nfn main() { println(\"clean\") }\n";
    auto diags_clean = cfire::analysis::check_source("clean.cf", clean_src, opts);
    require(diags_clean.empty(), "clean source yields zero diagnostics");

    // 5. check_path on files and directory
    ScopedTempDir tmp("cf118_diag_test");
    auto clean_file = tmp.path / "clean_mod.cf";
    {
        std::ofstream out(clean_file);
        out << "package main\nfn main() { println(\"ok\") }\n";
    }
    auto diags_p_clean = cfire::analysis::check_path(clean_file, opts);
    require(diags_p_clean.empty(), "check_path on clean file returns zero diagnostics");

    auto bad_file = tmp.path / "bad_mod.cf";
    {
        std::ofstream out(bad_file);
        out << "package main\nfn main() { unknown_ident_call() }\n";
    }
    auto diags_p_bad = cfire::analysis::check_path(bad_file, opts);
    require(!diags_p_bad.empty(), "check_path on bad file returns diagnostics");

    // 6. Directory check (checks all .cf files)
    auto diags_dir = cfire::analysis::check_path(tmp.path, opts);
    require(!diags_dir.empty(), "check_path on directory detects errors in directory files");

    std::println("--- PASS: test_unified_diagnostics_pipeline (CF-118)");
}

void test_phase3_advanced_language_and_safety([[maybe_unused]] const std::filesystem::path& rt_dir) {
    std::println("=== RUN   test_phase3_advanced_language_and_safety (CF-121 - CF-124)");

    // 1. CF-121: Generic struct and enum declaration AST parsing and codegen
    {
        const std::string gen_src = R"CF(package main
type Box[T] struct {
    Val: T
}
type Outcome[T, E] enum {
    Success(T),
    Failure(E),
}
fn main() {}
)CF";
        auto ast = cfire::frontend::parse_source("generic_decl.cf", gen_src);
        require(ast.has_value(), "parse generic struct and enum succeeds (CF-121)");
        require(!ast->structs.empty() && ast->structs[0].type_params.size() == 1, "struct Box has 1 type param T");
        require(ast->structs[0].type_params[0].name == "T", "type param name is T");
        require(!ast->enums.empty() && ast->enums[0].type_params.size() == 2, "enum Outcome has 2 type params T, E");
        require(ast->enums[0].type_params[0].name == "T" && ast->enums[0].type_params[1].name == "E", "type params are T and E");
    }

    // 2. CF-122: Or-patterns in match execution and exhaustiveness
    run_cfire_source(R"CF(package main

fn check_code(code: int) -> str {
    return match code {
        200 | 201 | 204 => "ok",
        400 | 401 | 404 => "client_err",
        500 | 502 | 503 => "server_err",
        _ => "other",
    }
}

fn check_cmd(cmd: str) -> str {
    return match cmd {
        "start" | "run" | "launch" => "exec",
        "stop" | "halt" | "kill" => "term",
        _ => "unknown",
    }
}

fn main() {
    assert(check_code(200) == "ok", "status 200 matches or-pattern")
    assert(check_code(201) == "ok", "status 201 matches or-pattern")
    assert(check_code(404) == "client_err", "status 404 matches or-pattern")
    assert(check_code(502) == "server_err", "status 502 matches or-pattern")
    assert(check_code(999) == "other", "wildcard arm matches")

    assert(check_cmd("run") == "exec", "run matches exec")
    assert(check_cmd("halt") == "term", "halt matches term")
    assert(check_cmd("help") == "unknown", "help matches wildcard")

    println("phase3 or-pattern match verified")
}
)CF", "phase3_match_or_test")
        .assert_ok("or-patterns in match execute cleanly (CF-122)")
        .assert_contains("phase3 or-pattern match verified", "confirmation received");

    // CF-122 Sema Negative: Duplicate literal in or-arm triggers cfire/UNREACHABLE-MATCH-ARM
    require_sema_error(R"(package main
fn main() {
    x := 10
    match x {
        1 | 1 => { println("dup") }
        _ => {}
    }
}
)", "cfire/UNREACHABLE-MATCH-ARM");

    // 3. CF-123: Lint clusters of 3+ integer constants with identical prefix (cfire/PREFER-ENUM) + FixIt
    {
        const std::string const_src = R"CF(package main
STATE_INIT := 0
STATE_RUNNING := 1
STATE_STOPPED := 2

fn main() {}
)CF";
        cfire::analysis::CheckOptions opts;
        opts.runtime_dir = rt_dir;
        auto diags = cfire::analysis::check_source("state_consts.cf", const_src, opts);
        bool found_prefer_enum = false;
        for (const auto& d : diags) {
            if (d.code == "cfire/PREFER-ENUM") {
                found_prefer_enum = true;
                require(!d.fixits.empty(), "cfire/PREFER-ENUM has attached fixit");
                if (!d.fixits.empty()) {
                    require(d.fixits[0].replacement.find("type State enum") != std::string::npos, "fixit suggests type State enum");
                }
            }
        }
        require(found_prefer_enum, "emitted cfire/PREFER-ENUM for STATE_* cluster (CF-123)");
    }

    // 4. CF-124: cpp {} blocks and foreign C++ calls invalidate loans and null facts (F9)
    // 4a. cpp {} invalidates active loan
    require_sema_error(R"(package main
fn main() {
    mut x := 10
    p := &x
    cpp {
        // foreign modification
    }
    y := *p
}
)", "cfire/USE-AFTER-FREE");

    // 4b. cpp {} invalidates NonNull fact, demoting to MaybeNull (requiring null check before dereference)
    require_sema_error(R"(package main
fn test_cpp_null(ptr: *int) {
    if ptr != none {
        cpp {
            // foreign code can mutate ptr
        }
        val := *ptr
    }
}
fn main() {}
)", "cfire/NULL-DEREF");

    // 4c. foreign C++ call invalidates NonNull fact on argument
    require_sema_error(R"(package main
import cpp "<cstdio>"
fn test_foreign_null(ptr: *int) {
    if ptr != none {
        cpp.printf("%p\n", ptr)
        val := *ptr
    }
}
fn main() {}
)", "cfire/NULL-DEREF");

    std::println("--- PASS: test_phase3_advanced_language_and_safety (CF-121 - CF-124)");
}

void test_phase2_concurrency_match_and_flow_control([[maybe_unused]] const std::filesystem::path& rt_dir) {
    std::println("=== RUN   test_phase2_concurrency_match_and_flow_control");

    // 1. Escaping Capture Mutation Diagnostics (cfire/ESCAPING-CAPTURE-MUT)
    // 1a. Struct field mutation
    require_sema_error(R"(package main
type S struct { count: i32 }
fn main() {
    mut s := S{count: 0}
    async.scope |s_ctx| {
        s_ctx.spawn || {
            s.count += 1
        }
    }
}
)", "cfire/ESCAPING-CAPTURE-MUT");

    // 1b. Slice / array index element mutation
    require_sema_error(R"(package main
fn main() {
    mut arr: []i32 = [1, 2, 3]
    async.scope |s_ctx| {
        s_ctx.spawn || {
            arr[0] = 99
        }
    }
}
)", "cfire/ESCAPING-CAPTURE-MUT");

    // 1c. Container mutating method call (.push)
    require_sema_error(R"(package main
fn main() {
    mut arr: []i32 = [1, 2]
    async.scope |s_ctx| {
        s_ctx.spawn || {
            arr.push(3)
        }
    }
}
)", "cfire/ESCAPING-CAPTURE-MUT");

    // 2. Data-bearing sync.Mutex[T] with with_lock and lock() RAII guard
    run_cfire_source(R"(package main
import "core:sync"
fn main() {
    mu := sync.make_mutex(0)
    async.scope |s| {
        for i in 0..10 {
            s.spawn || {
                mu.with_lock(|mut v| {
                    v += 1
                })
            }
        }
    }
    g := mu.lock()
    println("TOTAL: {}", *g)
}
)", "phase2_mutex")
        .assert_ok("concurrency mutex with_lock and lock() succeeds")
        .assert_contains("TOTAL: 10", "mutex protected 10 concurrent increments");

    // 3. sync.Atomic[T] and async.Task[T] join
    run_cfire_source(R"(package main
import "core:sync"
fn main() {
    atm := sync.make_atomic(100)
    async.scope |s| {
        t := s.spawn || -> i32 {
            atm.add(23)
            return 77
        }
        res := t.join()
        println("TASK: {}, ATOM: {}", res, atm.load())
    }
}
)", "phase2_atomic_task")
        .assert_ok("atomic and task join succeed")
        .assert_contains("TASK: 77, ATOM: 123", "task result and atomic updated correctly");

    // 4. Pattern guards and Range patterns in match
    run_cfire_source(R"(package main
fn test_guards(x: i32) -> str {
    match x {
        _ if x < 0 => { return "negative" }
        0 => { return "zero" }
        1..=5 => { return "small" }
        6..10 => { return "mid" }
        _ => { return "large" }
    }
}
fn main() {
    println("{},{},{},{},{}", test_guards(-5), test_guards(0), test_guards(3), test_guards(7), test_guards(100))
}
)", "phase2_guards_ranges")
        .assert_ok("pattern guards and range patterns compile and run")
        .assert_contains("negative,zero,small,mid,large", "guards and ranges matched expected arms");

    // 5. Enum match with return, break, continue control flow
    run_cfire_source(R"(package main
type Step enum { Skip, Stop, Done(i32) }
fn run_steps() -> i32 {
    mut sum := 0
    steps: []Step = [Step::Skip, Step::Done(10), Step::Skip, Step::Done(20), Step::Stop, Step::Done(30)]
    for s in steps {
        match s {
            Skip => { continue }
            Stop => { break }
            Done(v) => { sum += v }
        }
    }
    return sum
}
fn main() {
    println("SUM: {}", run_steps())
}
)", "phase2_enum_control_flow")
        .assert_ok("enum match with continue and break succeeds")
        .assert_contains("SUM: 30", "correctly skipped and broke out of loop in match arms");

    // 6. Optional match (Some/none) and Result match (Ok/Err)
    run_cfire_source(R"(package main
fn check_opt(o: i32?) -> str {
    match o {
        Some(val) => { return "val={val}" }
        none => { return "empty" }
    }
}
fn check_res(r: i32!) -> str {
    match r {
        Ok(v) => { return "ok={v}" }
        Err(e) => { return "err={e}" }
    }
}
fn main() {
    o1: i32? = 42
    o2: i32? = none
    r1: i32! = 100
    r2: i32! = error("failed")
    println("{}, {}, {}, {}", check_opt(o1), check_opt(o2), check_res(r1), check_res(r2))
}
)", "phase2_opt_res_match")
        .assert_ok("optional and result match blocks compile and run")
        .assert_contains("val=42, empty, ok=100, err=failed", "Some/none and Ok/Err unpacked accurately");

    // 7. Conditional unwrapping (if val := expr [; guard] and for val := expr)
    run_cfire_source(R"(package main
type Iterator struct {
    curr: i32
    max: i32
}
fn (mut it: Iterator) next() -> i32? {
    if it.curr < it.max {
        val := it.curr
        it.curr += 1
        return val
    }
    return none
}
fn main() {
    opt: i32? = 50
    if v := opt; v > 20 {
        println("UNWRAPPED: {}", v)
    } else {
        println("NOPE")
    }
    mut it := Iterator{curr: 0, max: 3}
    mut total := 0
    for v := it.next() {
        total += v
    }
    println("TOTAL_IT: {}", total)
}
)", "phase2_unwrapping")
        .assert_ok("if and for unwrapping compile and run")
        .assert_contains("UNWRAPPED: 50", "conditional if-unwrap evaluated correctly")
        .assert_contains("TOTAL_IT: 3", "for-unwrap loop terminated cleanly at none");

    // 8. Two-variable map iteration & collection .len()
    run_cfire_source(R"(package main
import "core:collections"
fn main() {
    mut m: map[str]i32 = {}
    m["apple"] = 10
    mut sum := 0
    for k, v in m {
        if k == "apple" {
            sum += v
        }
    }
    mut set := collections.Set[i32]{}
    set.insert(1)
    set.insert(2)
    println("MAP: {}, SET_LEN: {}", sum, set.len())
}
)", "phase2_map_and_len")
        .assert_ok("two-variable map iteration and Set .len() compile and run")
        .assert_contains("MAP: 10, SET_LEN: 2", "map iteration and set.len() produced expected results");

    std::println("--- PASS: test_phase2_concurrency_match_and_flow_control");
}









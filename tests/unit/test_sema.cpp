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
    s := Shape.Circle(1.0)
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
)", "cfire/OUT-CALLSITE", "out_callsite.cf");

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
    t := Token.Eof
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
    _ := try fs.read_all("a.bin")
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
    _ := try os.socketpair()
}
)", "cfire/DEPRECATED-API", "dep_os_socketpair.cf");

        require_sema_error(R"(package main
import "core:fs"
fn main() -> void! {
    mut f := try fs.open("a.txt", fs.OpenMode::Write)
    _ := try f.write_str("hello")
}
)", "cfire/DEPRECATED-API", "dep_file_write_str.cf");

        require_sema_error(R"(package main
import "core:net"
fn main() -> void! {
    (mut a, mut b) := try net.socketpair()
    _ := try a.write("hi")
    _ := try b.read_str(16)
}
)", "cfire/DEPRECATED-API", "dep_tcp_write_read_str.cf");

        {
            std::string dep_src = "package main\nimport \"core:fs\"\nfn main() -> void! {\n    _ := try fs.read_all(\"a.bin\")\n}\n";
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
        _ := try fallible()
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
            std::string red_mut_src = "package main\nfn read_only(x: i32) -> i32 { return x }\nfn main() {\n    mut v := 10\n    _ := read_only(mut v)\n}\n";
            auto lines = cfire::diag::split_lines(red_mut_src);
            auto sd = cfire::diag::parse_diagnostic_string(
                "red_mut.cf:5:20: error: call-site 'mut' annotation has no effect on 'in' parameter 1 of 'read_only'; remove 'mut' (cfire/REDUNDANT-CALLSITE-INTENT)",
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



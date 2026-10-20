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



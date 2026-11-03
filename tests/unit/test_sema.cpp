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



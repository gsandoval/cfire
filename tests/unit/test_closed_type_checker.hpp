#pragma once
// tests/unit/test_closed_type_checker.hpp — Closed pure-.cf type checker, Typed AST & Q90 backend ICE boundary unit tests.

#include "../test_harness.hpp"

void test_closed_type_checker() {
    std::println("=== RUN   test_closed_type_checker");

    // 1. TypeTable: TypeParam, GenericInst, Function (with default params), Error, Foreign, substitute & unify
    {
        cfire::frontend::TypeTable tt;
        auto t_ord = tt.make_type_param("T", {"Ordered"});
        auto t_num = tt.make_type_param("T", {"Numeric"});
        require(t_ord != t_num, "same-named TypeParams with distinct constraints get distinct TypeIds");
        require(tt.has_constraint(t_ord, "Ordered"), "TypeParam tracks Ordered constraint");
        require(tt.is_numeric_or_constrained(t_num), "Numeric TypeParam satisfies is_numeric_or_constrained");
        require(!tt.is_numeric_or_constrained(t_ord), "Ordered TypeParam does not satisfy Numeric");

        auto box_i32 = tt.make_generic_inst("Box", {cfire::frontend::TypeId::Int32()});
        const auto& bdesc = tt.get(box_i32);
        require(bdesc.kind == cfire::frontend::TypeKind::GenericInst && bdesc.base_name == "Box" &&
                bdesc.type_args.size() == 1 && bdesc.type_args[0] == cfire::frontend::TypeId::Int32(),
            "make_generic_inst records GenericInst base_name and type_args");
        require(tt.from_name("Box[i32]") == box_i32, "from_name parses GenericInst matching make_generic_inst");

        auto fn_def = tt.from_name("fn(reason: str = \"\", code: i32 = 0) -> bool");
        const auto& fdesc = tt.get(fn_def);
        require(fdesc.kind == cfire::frontend::TypeKind::Function &&
                fdesc.param_types.size() == 2 &&
                fdesc.param_types[0] == cfire::frontend::TypeId::Str() &&
                fdesc.param_types[1] == cfire::frontend::TypeId::Int32() &&
                fdesc.fn_min_params == 0 &&
                fdesc.ret_type == cfire::frontend::TypeId::Bool(),
            "from_name strips fn param names and tracks min_params for defaults");

        require(cfire::frontend::TypeId::Error().is_error(), "TypeId::Error is_error");
        require(cfire::frontend::TypeId::Foreign().is_foreign(), "TypeId::Foreign is_foreign");
        require(tt.is_assignable(cfire::frontend::TypeId::Int32(), cfire::frontend::TypeId::Error()),
            "Error poison is assignable to avoid cascades");
        require(tt.is_assignable(cfire::frontend::TypeId::Int32(), cfire::frontend::TypeId::Foreign()),
            "Foreign boundary type is assignable across interop boundary");

        auto slice_t = tt.make_slice(t_ord);
        auto slice_i32 = tt.make_slice(cfire::frontend::TypeId::Int32());
        std::unordered_map<std::string, cfire::frontend::TypeId> bindings;
        require(tt.unify(slice_t, slice_i32, bindings), "unify matches []T against []i32");
        require(bindings["T"] == cfire::frontend::TypeId::Int32(), "unify binds T -> i32");
        require(tt.substitute(slice_t, bindings) == slice_i32, "substitute rewrites []T to []i32");
        require(tt.contains_type_param(slice_t) && !tt.contains_type_param(slice_i32),
            "contains_type_param detects generic vs instantiated types");
    }

    // 2. Typed AST: resolved_type_id populated across Expr, VarDeclStmt, IfStmt, ForStmt, OrExpr, MatchExpr, ClosureExpr, ArrayLitExpr
    {
        const std::string src = R"CF(package main
fn maybe() -> i32? { return 7 }
fn main() {
    arr := [1, 2, 3]
    for item in arr {
        _ = item
    }
    if v := maybe() {
        _ = v
    }
    y := maybe() or 0
    n := 1
    m := match n {
        1 => 1
        _ => 2
    }
    cl := |a: i32| -> i32 {
        return a + 1
    }
    _ = y
    _ = m
    _ = cl
}
)CF";
        auto parsed = cfire::frontend::parse_source("typed_ast_test.cf", src);
        require(parsed.has_value(), "typed_ast_test.cf parses cleanly");
        auto checked = cfire::frontend::analyze(std::move(*parsed));
        require(checked.has_value(), "typed_ast_test.cf passes Sema cleanly");
        if (checked) {
            const cfire::frontend::FnDecl* main_fn = nullptr;
            for (const auto& fn : checked->ast.functions) {
                if (fn.name == "main") main_fn = &fn;
            }
            require(main_fn != nullptr && main_fn->body.size() >= 7, "found main with expected statements");
            if (main_fn && main_fn->body.size() >= 7) {
                const auto* vd_arr = std::get_if<cfire::frontend::VarDeclStmt>(&main_fn->body[0]->node);
                require(vd_arr != nullptr && vd_arr->resolved_type_id != 0, "VarDeclStmt arr has resolved_type_id");
                if (vd_arr && vd_arr->init) {
                    const auto* arr_lit = std::get_if<cfire::frontend::ArrayLitExpr>(&vd_arr->init->node);
                    require(arr_lit != nullptr && arr_lit->resolved_elem_type_id == cfire::frontend::TypeId::Int64().index,
                        "ArrayLitExpr populates resolved_elem_type_id");
                    require(vd_arr->init->resolved_type_id != 0, "Expr populates resolved_type_id");
                }

                const auto* for_st = std::get_if<cfire::frontend::ForStmt>(&main_fn->body[1]->node);
                require(for_st != nullptr && for_st->resolved_item_type_id == cfire::frontend::TypeId::Int64().index,
                    "ForStmt populates resolved_item_type_id");

                const auto* if_st = std::get_if<cfire::frontend::IfStmt>(&main_fn->body[2]->node);
                require(if_st != nullptr && if_st->resolved_unwrap_type_id == cfire::frontend::TypeId::Int32().index,
                    "IfStmt populates resolved_unwrap_type_id");

                const auto* vd_or = std::get_if<cfire::frontend::VarDeclStmt>(&main_fn->body[3]->node);
                require(vd_or != nullptr && vd_or->init != nullptr, "found OrExpr VarDeclStmt");
                if (vd_or && vd_or->init) {
                    const auto* or_ex = std::get_if<cfire::frontend::OrExpr>(&vd_or->init->node);
                    require(or_ex != nullptr && or_ex->resolved_type_id == cfire::frontend::TypeId::Int32().index,
                        "OrExpr populates resolved_type_id");
                }

                const auto* vd_match = std::get_if<cfire::frontend::VarDeclStmt>(&main_fn->body[5]->node);
                require(vd_match != nullptr && vd_match->init != nullptr, "found MatchExpr VarDeclStmt");
                if (vd_match && vd_match->init) {
                    const auto* me = std::get_if<cfire::frontend::MatchExpr>(&vd_match->init->node);
                    require(me != nullptr && me->resolved_type_id == cfire::frontend::TypeId::Int64().index,
                        "MatchExpr populates resolved_type_id");
                }

                const auto* vd_cl = std::get_if<cfire::frontend::VarDeclStmt>(&main_fn->body[6]->node);
                require(vd_cl != nullptr && vd_cl->init != nullptr, "found ClosureExpr VarDeclStmt");
                if (vd_cl && vd_cl->init) {
                    const auto* cl = std::get_if<cfire::frontend::ClosureExpr>(&vd_cl->init->node);
                    require(cl != nullptr && cl->resolved_return_type_id == cfire::frontend::TypeId::Int32().index,
                        "ClosureExpr populates resolved_return_type_id");
                }
            }
        }
    }

    // 3. Q90 format_backend_failure: pure-.cf ICE boundary & real .cf source snippets without .cpp columns/snippets
    {
        ScopedTempDir tmp("cfire_q90_diag");
        std::string cf_content =
            "package main\n"
            "fn main() {\n"
            "    start := 1\n"
            "    end := 10\n"
            "    for i in start..end {\n"
            "        println(\"{}\", i)\n"
            "    }\n"
            "}\n";
        auto cf_path = tmp.file("day2_01.cf", cf_content);
        std::string raw_clang = std::format(
            "{}:5:198: error: cannot increment value of type 'std::common_type_t<decltype(start), decltype(end)>'\n"
            "    5 | for (std::common_type_t<decltype(start), decltype(end)> it = 0; ; ++it) {{}}\n"
            "      |                                                                 ^~\n",
            cf_path.string());

        std::string pure_msg = cfire::diag::format_backend_failure(raw_clang, /*has_cpp_interop=*/false, /*verbose=*/false);
        require(pure_msg.contains("cfire/INTERNAL-BACKEND-ERROR"), "pure-.cf backend failure emits cfire/INTERNAL-BACKEND-ERROR");
        require(pure_msg.contains(std::format("{}:5: error:", cf_path.string())), "pure-.cf ICE strips impossible .cpp column 198");
        require(pure_msg.contains("5 |     for i in start..end {"), "pure-.cf ICE displays real .cf source line");
        require(!pure_msg.contains("common_type_t") && !pure_msg.contains("decltype(") && !pure_msg.contains(":198"),
            std::format("pure-.cf non-verbose ICE hides raw C++ internals, got:\n{}", pure_msg));

        std::string verbose_msg = cfire::diag::format_backend_failure(raw_clang, /*has_cpp_interop=*/false, /*verbose=*/true);
        require(verbose_msg.contains("--- raw C++ backend output ---") && verbose_msg.contains("common_type_t"),
            "pure-.cf verbose ICE preserves raw C++ output behind flag");

        std::string interop_msg = cfire::diag::format_backend_failure(raw_clang, /*has_cpp_interop=*/true, /*verbose=*/false);
        require(!interop_msg.contains("cfire/INTERNAL-BACKEND-ERROR"), "C++ interop failure does not mask as pure-.cf ICE");
        require(interop_msg.contains(std::format("{}:5: error:", cf_path.string())) &&
                !interop_msg.contains(":198") &&
                interop_msg.contains("5 |     for i in start..end {") &&
                !interop_msg.contains("it = 0; ; ++it"),
            std::format("C++ interop failure drops generated .cpp snippet and impossible col while showing real .cf line, got:\n{}", interop_msg));
    }

    // 4. Wave 2 (CF-258): Fully Typed C++23 Codegen & selective auto retention
    {
        const std::string src = R"CF(package main
import "core:sync"
type Pair struct {
    a: i32
    b: str
}
GREETING := "hello"
mut counter := 10

fn identity[T](x: T) -> T {
    tmp := x
    return tmp
}

fn sum_generic[T: Integer](n: T) -> T {
    mut acc: T = 0
    for i in 0..n {
        acc += i
    }
    return acc
}

fn maybe_num(flag: bool) -> i32? {
    if flag { return 42 }
    return none
}

fn apply_cb(x: i64, cb: fn(i64) -> i64) -> i64 {
    return cb(x)
}

fn run_workers() -> []i32 {
    results := async.scope(2) |sc| {
        sc.spawn || { return i32(10) }
        sc.spawn || { return i32(20) }
    }
    return results
}

fn main() {
    n := 7
    s := "world"
    d := 3.5
    ok := true
    p := Pair{a: 1, b: "ok"}
    xs := [1, 2, 3]
    mut m := make(map[str]i32)
    m["k"] = 5
    opt := maybe_num(true)

    for i in 0..n {
        _ = i
    }
    u: usize = 3
    for j in 0..u {
        _ = j
    }
    a32: i32 = 1
    b32: i32 = 4
    for k in a32..b32 {
        _ = k
    }
    for x in xs {
        _ = x
    }
    for mk, mv in m {
        _ = mk
        _ = mv
    }
    if uval := opt {
        _ = uval
    }
    for wval := opt {
        _ = wval
        break
    }
    mres := match 1 {
        1 => "one"
        _ => "other"
    }
    ires := if ok { i32(1) } else { i32(2) }
    cl := |x: i32| -> i32 {
        return x + 1
    }
    cb_out := apply_cb(10, |v| {
        return v + 2
    })
    defer {
        _ = n
    }
    g := identity(99)
    workers := run_workers()
    _ = GREETING
    _ = counter
    _ = s
    _ = d
    _ = p
    _ = cl
    _ = cb_out
    _ = g
    _ = workers
    _ = mres
    _ = ires
    _ = sum_generic(i32(4))
}
)CF";
        auto parsed = cfire::frontend::parse_source("wave2_typed_codegen_test.cf", src);
        require(parsed.has_value(), std::format("wave2_typed_codegen_test.cf parses cleanly: {}", parsed ? "" : parsed.error()));
        if (parsed) {
            auto checked = cfire::frontend::analyze(std::move(*parsed));
            require(checked.has_value(), std::format("wave2_typed_codegen_test.cf analyzes cleanly: {}", checked ? "" : checked.error()));
            if (checked) {
                require(checked->errors.empty(),
                    std::format("wave2_typed_codegen_test.cf has 0 errors, got: {}",
                        checked->errors.empty() ? "" : checked->errors[0]));
                std::string cpp = cfire::frontend::emit_cpp(*checked, false);

                // Zero legacy decltype / common_type_t / decltype(auto) in emitted code
                require(!cpp.contains("common_type_t"), "Wave 2 emits zero std::common_type_t in range loops");
                require(!cpp.contains("decltype(auto)"), "Wave 2 emits zero decltype(auto)");
                auto main_pos = cpp.find("auto main(");
                require(main_pos != std::string::npos, "emitted C++ contains main");
                std::string body_cpp = (main_pos != std::string::npos) ? cpp.substr(main_pos) : cpp;
                require(!body_cpp.contains("decltype("), "Wave 2 emits zero decltype(...) in pure-.cf function bodies");

                // Globals & locals
                require(cpp.contains("inline const std::string GREETING = \"hello\";"), "typed const global");
                require(cpp.contains("inline std::int64_t counter = 10;"), "typed mut global");
                require(cpp.contains("std::int64_t n = 7;"), "typed i64 local");
                require(cpp.contains("std::string s = \"world\";"), "typed str local");
                require(cpp.contains("double d = 3.5;"), "typed f64 local");
                require(cpp.contains("bool ok = true;"), "typed bool local");
                require(cpp.contains("Pair p = Pair{"), "typed struct local");
                require(cpp.contains("std::vector<std::int64_t> xs = {1, 2, 3};"), "typed slice local");
                require(cpp.contains("cfire::Map<std::string, std::int32_t> m = "), "typed map local");
                require(cpp.contains("std::optional<std::int32_t> opt = maybe_num(true);"), "typed optional local");
                require(cpp.contains("std::int64_t g = identity(99);"), "typed generic call result local");
                require(cpp.contains("std::vector<std::int32_t> workers = run_workers();"), "typed slice result local");
                require(cpp.contains("std::int64_t cb_out = apply_cb("), "typed callback call result local");

                // Range loops, for-in, unwrap bindings
                require(cpp.contains("for (std::int64_t i = "), "typed i64 range index");
                require(cpp.contains("for (std::size_t j = "), "typed usize range index");
                require(cpp.contains("for (std::int32_t k = "), "typed i32 range index");
                require(cpp.contains("for (T i = "), "typed generic Integer T range index");
                require(cpp.contains("for (const std::int64_t& x : cfire::detail::iter_items(xs))"), "typed for-in item binding");
                require(cpp.contains("if (std::optional<std::int32_t> _opt = opt; _opt.has_value())"), "typed if unwrap opt");
                require(cpp.contains("const std::int32_t& uval = *_opt;"), "typed if unwrap binding");
                require(cpp.contains("std::optional<std::int32_t> _opt = opt;"), "typed for unwrap opt");
                require(cpp.contains("const std::int32_t& wval = *_opt;"), "typed for unwrap binding");

                // Match / if expression lambdas & unannotated closures
                require(cpp.contains("std::string mres =") && cpp.contains("([&]() -> std::string {"), "typed match expression lambda & var");
                require(cpp.contains("std::int32_t ires =") && cpp.contains("([&]() -> std::int32_t {"), "typed if expression lambda & var");
                require(cpp.contains("(std::int64_t v) -> std::int64_t"), "typed unannotated closure param & return");

                // Value async.scope result vector
                require(cpp.contains("std::vector<std::int32_t> results;"), "typed async.scope result vector");

                // Retained `auto` ONLY where required (closure local, defer guard, template-dependent local, map structured binding)
                require(cpp.contains("auto cl = "), "closure local retains auto");
                require(cpp.contains("auto _defer_"), "defer guard retains auto");
                require(cpp.contains("auto tmp = x;"), "template-dependent generic local retains auto");
                require(cpp.contains("for (auto&& [mk, mv] : cfire::detail::iter_pairs(_col_mk))"), "structured binding retains auto&&");
            }
        }
    }

    std::println("--- PASS: test_closed_type_checker");
}

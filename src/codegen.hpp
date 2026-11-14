#pragma once
// src/codegen.hpp — C++23 Emitter for cfire (.cf)

#include "sema/types.hpp"
#include <string>
#include <vector>

namespace cfire::frontend {

struct CppUnit {
    std::string id;
    std::string pkg_name;
    std::string filename;
    std::string cpp;
};

struct ShardedEmission {
    std::string decls_hpp;
    std::vector<CppUnit> units;
    bool has_cpp_impl_define = false;
};

} // namespace cfire::frontend

#if defined(CFIRE_SPLIT_TUS) && !defined(CFIRE_TU_CODEGEN_DECL_IMPL) && !defined(CFIRE_TU_CODEGEN_BODY_IMPL)

namespace cfire::frontend {

[[nodiscard]] auto emit_cpp(const CheckedFile& cf, bool test_mode = false) -> std::string;
[[nodiscard]] auto emit_sharded_cpp(const CheckedFile& cf, bool test_mode = false) -> ShardedEmission;

} // namespace cfire::frontend

#else

#ifdef CFIRE_SPLIT_TUS
#define CFIRE_CG_INLINE
#else
#define CFIRE_CG_INLINE inline
#endif

#include "module_registry.hpp"
#include "sema/types.hpp"
#include "sema/stdlib_index.hpp"
#include "sema/builtin_methods.hpp"
#include <algorithm>
#include <format>
#include <regex>
#include <sstream>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace cfire::frontend {

class Emitter {

public:
    explicit Emitter(const CheckedFile& cf, bool test_mode = false)
        : cf_(cf), test_mode_(test_mode), source_path_(cf.ast.path) {
        init_imports();
    }

    [[nodiscard]] auto emit_cpp() -> std::string;
    [[nodiscard]] auto emit_sharded() -> ShardedEmission;

    // Q10: `i32`, `f64`, `bool`, ... used as a value prefix (`i32.MAX`, `f64.parse(s)`); returns the C++ type.
    [[nodiscard]] auto primitive_prefix_cpp(const ExprPtr& obj) const -> std::string {
        if (!obj) return {};
        const auto* id = std::get_if<IdentExpr>(&obj->node);
        if (!id || local_vars_.contains(id->name)) return {};
        static const std::unordered_map<std::string_view, std::string_view> kPrims = {
            {"i8", "std::int8_t"}, {"i16", "std::int16_t"}, {"i32", "std::int32_t"}, {"i64", "std::int64_t"},
            {"u8", "std::uint8_t"}, {"u16", "std::uint16_t"}, {"u32", "std::uint32_t"}, {"u64", "std::uint64_t"},
            {"isize", "std::ptrdiff_t"}, {"usize", "std::size_t"}, {"f32", "float"}, {"f64", "double"}, {"bool", "bool"}};
        if (auto it = kPrims.find(id->name); it != kPrims.end()) return std::string(it->second);
        return {};
    }

    // Q25: the C++ for every `make(T, args...)` form. Sema has already checked arity and argument types.
    [[nodiscard]] auto user_ctor_name(const TypeRef& t) const -> std::string {
        std::string leaf = t.name;
        for (auto p = leaf.find("::"); p != std::string::npos; p = leaf.find("::")) leaf.replace(p, 2, ".");
        std::string pkg;
        if (auto dot = leaf.rfind('.'); dot != std::string::npos) {
            pkg = leaf.substr(0, dot);
            leaf = leaf.substr(dot + 1);
        }
        const std::string fn = "cfire_ctor_" + leaf;
        if (!pkg.empty() && cf_.fn_sigs.contains(pkg + "." + fn)) {
            return colliding_fn_names_.contains(fn) ? std::format("cfire_pkg_{}__{}", pkg, fn) : fn;
        }
        if (cf_.fn_sigs.contains(fn)) return fn;
        for (const auto& [key, sig] : cf_.fn_sigs) {
            if (key.ends_with("." + fn)) return colliding_fn_names_.contains(fn) ? std::format("cfire_pkg_{}__{}", sig.package_name, fn) : fn;
        }
        return {};
    }

    [[nodiscard]] auto emit_make(const CallExpr& call) -> std::string;

public:
    const CheckedFile& cf_;
    bool test_mode_ = false;
    bool in_main_ = false;
    bool in_test_ = false;
    bool in_fallible_void_ = false;
    bool in_ptr_return_ = false;
    bool sharded_non_inline_ = false;
    std::string cur_fn_raw_return_type_{};
    std::string source_path_{};
    int last_source_line_ = -1;
    std::ostringstream buf_;
    int indent_ = 0;
    std::string cur_fn_{};
    std::string cur_pkg_{};
    std::unordered_set<std::string> colliding_fn_names_{};
    std::unordered_set<std::string> colliding_gv_names_{};
    int defer_count_ = 0;
    int try_count_ = 0;
    std::unordered_set<std::string> used_core_{};
    std::unordered_map<std::string, std::string> used_vendor_{};
    std::unordered_map<std::string, std::string> core_aliases_{};
    std::vector<std::string> active_async_scopes_{};
    // Q51 (CF-189): scope variable -> (its `cfire::async::Results<T>` collector, the lowered `T`), for the value-producing
    // scopes being emitted; their `scope.spawn` calls add the task to the collector.
    std::unordered_map<std::string, std::pair<std::string, std::string>> value_scope_results_{};

    // A `.cf` type written as text (`i32`, `[]str`, `http.Request`) in C++, for types Sema records as strings.
    [[nodiscard]] auto lower_type_text(const std::string& cf_type) const -> std::string {
        const std::string probe = std::format("package cf_probe\nfn cf_probe(x: {}) {{\n}}\n", cf_type);
        if (auto ast = cfire::frontend::parse_source("<type>", probe); ast && !ast->functions.empty() && !ast->functions[0].params.empty()) {
            return lower_owning_type(ast->functions[0].params[0].type);
        }
        return "auto";
    }
    std::unordered_set<std::string> local_vars_{};
    std::unordered_set<std::string> ptr_vars_{};
    std::unordered_set<std::string> ptr_field_names_{};
    std::unordered_set<std::string> smart_ptr_vars_{};
    std::unordered_set<std::string> smart_ptr_field_names_{};
    std::unordered_set<std::string> optional_vars_{};
    std::unordered_set<std::string> map_vars_{};
    std::unordered_map<std::string, std::size_t> fixed_array_vars_{};
    std::unordered_map<std::string, std::string> loop_index_to_col_{};
    std::unordered_map<std::string, std::string> var_types_{};
    // Q36 / Q53: read-only `str` and `[]T` params (lowered to `cfire::str_ref` / `cfire::slice_ref<T>`), with the
    // owned C++ type an escaping closure copies them into.
    std::vector<std::pair<std::string, std::string>> view_params_{};
    std::unordered_map<const Expr*, std::string> hoisted_tries_{};

    // CF-196: an integer constant expression (literals, unary `-`, arithmetic and bitwise operators); Sema types it `i64`.
    [[nodiscard]] static auto is_int_const_expr(const ExprPtr& e) -> bool {
        if (!e) return false;
        if (const auto* lit = std::get_if<LiteralExpr>(&e->node)) return lit->kind == TokenKind::Int;
        if (const auto* un = std::get_if<UnaryExpr>(&e->node)) return un->op == "-" && is_int_const_expr(un->operand);
        if (const auto* b = std::get_if<BinaryExpr>(&e->node)) {
            static const std::unordered_set<std::string_view> ops = {"+", "-", "*", "/", "%", "<<", ">>", "&", "|", "^"};
            return ops.contains(b->op) && is_int_const_expr(b->left) && is_int_const_expr(b->right);
        }
        return false;
    }

    [[nodiscard]] static auto sanitize_cpp_ident(std::string_view id) -> std::string {
        static const std::unordered_set<std::string_view> kCppReserved = {
            "short", "long", "signed", "unsigned", "class", "template", "operator",
            "export", "delete", "new", "private", "protected", "public", "virtual",
            "override", "final", "typename", "namespace", "using", "this", "throw",
            "catch", "union", "goto", "register", "volatile", "friend", "explicit",
            "mutable", "inline", "constexpr", "consteval", "constinit", "co_await",
            "co_return", "co_yield", "concept", "requires", "stdout", "stderr", "stdin"
        };
        if (kCppReserved.contains(id)) {
            return std::string(id) + "_cf_";
        }
        return std::string(id);
    }

    [[nodiscard]] static auto escape_line_path(std::string_view p) -> std::string {
        std::string out;
        for (char c : p) {
            if (c == '\\') out += "\\\\";
            else if (c == '"') out += "\\\"";
            else out += c;
        }
        return out;
    }

    void set_source_path(std::string_view p) {
        if (!p.empty() && p != source_path_) {
            source_path_ = std::string(p);
            last_source_line_ = -1;
        }
    }

    void emit_line_directive(int line) {
        if (line > 0 && line != last_source_line_ && !source_path_.empty()) {
            last_source_line_ = line;
            writeln(std::format("#line {} \"{}\"", line, escape_line_path(source_path_)));
        }
    }

    void reset_line_directive() {
        last_source_line_ = -1;
    }

    void emit_synthetic_line_directive() {
        if (!source_path_.empty()) {
            writeln("#line 1 \"<cfire-synthetic>\"");
            last_source_line_ = -1;
        }
    }

    [[nodiscard]] auto make_sub_emitter(bool inherit_return_context = true) const -> Emitter {
        Emitter sub(cf_, test_mode_);
        sub.indent_ = indent_;
        sub.cur_fn_ = cur_fn_;
        sub.cur_pkg_ = cur_pkg_;
        sub.source_path_ = source_path_;
        sub.last_source_line_ = last_source_line_;
        sub.colliding_fn_names_ = colliding_fn_names_;
        sub.colliding_gv_names_ = colliding_gv_names_;
        sub.used_core_ = used_core_;
        sub.used_vendor_ = used_vendor_;
        sub.core_aliases_ = core_aliases_;
        sub.local_vars_ = local_vars_;
        sub.ptr_vars_ = ptr_vars_;
        sub.ptr_field_names_ = ptr_field_names_;
        sub.smart_ptr_vars_ = smart_ptr_vars_;
        sub.smart_ptr_field_names_ = smart_ptr_field_names_;
        sub.optional_vars_ = optional_vars_;
        sub.map_vars_ = map_vars_;
        sub.fixed_array_vars_ = fixed_array_vars_;
        sub.loop_index_to_col_ = loop_index_to_col_;
        sub.var_types_ = var_types_;
        sub.view_params_ = view_params_;
        sub.generic_params_ = generic_params_;
        sub.sharded_non_inline_ = sharded_non_inline_;
        if (inherit_return_context) {
            sub.in_main_ = in_main_;
            sub.in_test_ = in_test_;
            sub.in_fallible_void_ = in_fallible_void_;
            sub.in_ptr_return_ = in_ptr_return_;
            sub.cur_fn_raw_return_type_ = cur_fn_raw_return_type_;
        }
        return sub;
    }

    void init_imports();

    void writeln(std::string_view line = "") {
        if (line.empty()) {
            buf_ << "\n";
            return;
        }
        for (int i = 0; i < indent_; ++i) buf_ << "    ";
        buf_ << line << "\n";
    }

    void emit_file();

    void emit_test_runner(const FileAST& ast, bool has_user_main);
    void emit_test_decl(const TestDecl& td, std::size_t idx);

    void emit_interface(const InterfaceDecl& iface);

    void emit_enum(const EnumDecl& en);

    void emit_struct(const StructDecl& st);

    void emit_struct_reflection(const StructDecl& st);
    void emit_enum_reflection(const EnumDecl& en);

    [[nodiscard]] auto emitted_fn_symbol(const FnDecl& fn) const -> std::string {
        if (fn.receiver) return fn.receiver->type.name + "__" + fn.name;
        if (!fn.package_name.empty() && colliding_fn_names_.contains(fn.name)) {
            return std::format("cfire_pkg_{}__{}", fn.package_name, sanitize_cpp_ident(fn.name));
        }
        return sanitize_cpp_ident(fn.name);
    }

    // The type parameters in scope for the function being emitted (its own and a generic receiver's), so a
    // `[]T` parameter of a type parameter keeps a deducible `const std::vector<T>&` (Q53).
    std::unordered_set<std::string> generic_params_{};
    void set_generic_params(const FnDecl& fn) {
        generic_params_.clear();
        for (const auto& tp : fn.type_params) generic_params_.insert(tp.name);
        if (fn.receiver) {
            for (const auto& ta : fn.receiver->type.type_args) generic_params_.insert(ta.name);
        }
    }
    [[nodiscard]] auto mentions_generic_param(const TypeRef& tr) const -> bool {
        if (generic_params_.contains(tr.name)) return true;
        if (tr.elem_type && mentions_generic_param(*tr.elem_type)) return true;
        if (tr.key_type && mentions_generic_param(*tr.key_type)) return true;
        for (const auto& ta : tr.type_args) {
            if (mentions_generic_param(ta)) return true;
        }
        for (const auto& tt : tr.tuple_types) {
            if (mentions_generic_param(tt)) return true;
        }
        return false;
    }

    void emit_function_forward_decl(const FnDecl& fn);

    void emit_function(const FnDecl& fn);

    [[nodiscard]] static auto expr_references_ident(const ExprPtr& e, std::string_view ident) -> bool {
        if (!e || ident.empty()) return false;
        return std::visit([&](const auto& ex) -> bool {
            using T = std::decay_t<decltype(ex)>;
            if constexpr (std::is_same_v<T, IdentExpr>) {
                return ex.name == ident;
            } else if constexpr (std::is_same_v<T, LiteralExpr>) {
                if (ex.kind == TokenKind::String && ex.value.contains('{')) {
                    return ex.value.find(ident) != std::string::npos;
                }
                return false;
            } else if constexpr (std::is_same_v<T, UnaryExpr>) {
                return expr_references_ident(ex.operand, ident);
            } else if constexpr (std::is_same_v<T, BinaryExpr>) {
                return expr_references_ident(ex.left, ident) || expr_references_ident(ex.right, ident);
            } else if constexpr (std::is_same_v<T, MemberExpr>) {
                return ex.field == ident || expr_references_ident(ex.object, ident);
            } else if constexpr (std::is_same_v<T, IndexExpr>) {
                return expr_references_ident(ex.object, ident) || expr_references_ident(ex.index, ident);
            } else if constexpr (std::is_same_v<T, SliceExpr>) {
                return expr_references_ident(ex.object, ident) ||
                       expr_references_ident(ex.start, ident) ||
                       expr_references_ident(ex.end, ident);
            } else if constexpr (std::is_same_v<T, StructLitExpr>) {
                for (const auto& f : ex.fields) {
                    if (expr_references_ident(f.value, ident)) return true;
                }
                return false;
            } else if constexpr (std::is_same_v<T, ArrayLitExpr> || std::is_same_v<T, TupleExpr>) {
                for (const auto& el : ex.elements) {
                    if (expr_references_ident(el, ident)) return true;
                }
                return false;
            } else if constexpr (std::is_same_v<T, ClosureExpr>) {
                return stmts_reference_ident(ex.body, ident);
            } else if constexpr (std::is_same_v<T, OrExpr>) {
                return expr_references_ident(ex.inner, ident) ||
                       expr_references_ident(ex.fallback_val, ident) ||
                       stmts_reference_ident(ex.handler_body, ident);
            } else if constexpr (std::is_same_v<T, TryExpr>) {
                return expr_references_ident(ex.inner, ident);
            } else if constexpr (std::is_same_v<T, CallExpr>) {
                if (expr_references_ident(ex.callee, ident)) return true;
                for (const auto& a : ex.args) {
                    if (expr_references_ident(a.value, ident)) return true;
                }
                if (ex.block_arg && stmts_reference_ident(ex.block_arg->body, ident)) return true;
                return false;
            } else if constexpr (std::is_same_v<T, MatchExpr>) {
                if (expr_references_ident(ex.target, ident)) return true;
                for (const auto& arm : ex.arms) {
                    if (stmts_reference_ident(arm.body, ident)) return true;
                }
                return false;
            }
            return false;
        }, e->node);
    }

    // Q36: an escaping closure owns copies of the view parameters it uses, so it cannot outlive their text.
    [[nodiscard]] auto view_param_captures(const std::vector<StmtPtr>& body) const -> std::string {
        std::string caps;
        for (const auto& [name, owned] : view_params_) {
            if (stmts_reference_ident(body, name)) caps += std::format(", {0} = {1}({0})", name, owned);
        }
        return caps;
    }

    [[nodiscard]] static auto stmts_reference_ident(const std::vector<StmtPtr>& stmts, std::string_view ident) -> bool {
        if (ident.empty()) return false;
        for (const auto& s : stmts) {
            if (!s) continue;
            bool hit = std::visit([&](const auto& st) -> bool {
                using T = std::decay_t<decltype(st)>;
                if constexpr (std::is_same_v<T, VarDeclStmt>) {
                    return expr_references_ident(st.init, ident);
                } else if constexpr (std::is_same_v<T, DestructureVarDeclStmt>) {
                    return expr_references_ident(st.init, ident);
                } else if constexpr (std::is_same_v<T, AssignStmt>) {
                    return expr_references_ident(st.target, ident) || expr_references_ident(st.value, ident);
                } else if constexpr (std::is_same_v<T, DestructureAssignStmt>) {
                    for (const auto& t : st.targets) {
                        if (expr_references_ident(t, ident)) return true;
                    }
                    return expr_references_ident(st.value, ident);
                } else if constexpr (std::is_same_v<T, DeferStmt>) {
                    return expr_references_ident(st.call, ident) ||
                           stmts_reference_ident(st.body, ident);
                } else if constexpr (std::is_same_v<T, ReturnStmt>) {
                    return expr_references_ident(st.value, ident);
                } else if constexpr (std::is_same_v<T, IfStmt>) {
                    return expr_references_ident(st.cond, ident) ||
                           stmts_reference_ident(st.then_body, ident) ||
                           stmts_reference_ident(st.else_body, ident);
                } else if constexpr (std::is_same_v<T, ForStmt>) {
                    return expr_references_ident(st.iter_expr, ident) ||
                           expr_references_ident(st.range_end, ident) ||
                           stmts_reference_ident(st.body, ident);
                } else if constexpr (std::is_same_v<T, MatchStmt>) {
                    if (expr_references_ident(st.target, ident)) return true;
                    for (const auto& arm : st.arms) {
                        if (stmts_reference_ident(arm.body, ident)) return true;
                    }
                    return false;
                } else if constexpr (std::is_same_v<T, AsyncScopeStmt>) {
                    return expr_references_ident(st.workers, ident) ||
                           stmts_reference_ident(st.body, ident);
                } else if constexpr (std::is_same_v<T, CppBlockStmt>) {
                    return st.raw_code.find(ident) != std::string::npos;
                } else if constexpr (std::is_same_v<T, ExprStmt>) {
                    return expr_references_ident(st.expr, ident);
                }
                return false;
            }, s->node);
            if (hit) return true;
        }
        return false;
    }

    void emit_try_unwrap_check(std::string_view tmp, int stmt_line, bool in_main) {
        writeln(std::format("if (!{}.has_value()) {{", tmp));
        ++indent_;
        if (in_main) {
            writeln(std::format("std::println(stderr, \"Error: {{}}\", {}.error().message());", tmp));
            writeln("return 1;");
        } else if (in_test_) {
            writeln(std::format("cfire::assert_true_at(false, {}.error().message(), \"{}\", {}, \"try\");",
                tmp, escape_line_path(source_path_), stmt_line));
            writeln("return;");
        } else {
            writeln(std::format("return std::unexpected(std::move({}).error());", tmp));
        }
        --indent_;
        writeln("}");
        last_source_line_ = -1;
    }

    void hoist_subexpr_tries(const ExprPtr& e, bool in_main, int stmt_line, bool is_top_handled = false);

    [[nodiscard]] auto is_core_ns_ident(std::string_view name) const -> bool {
        return cfire::modules::is_known_core_namespace(name) ||
               used_core_.contains(std::string(name)) ||
               core_aliases_.contains(std::string(name));
    }

    [[nodiscard]] auto expr_returns_optional(const ExprPtr& e) const -> bool {
        if (!e) return false;
        if (const auto* tr = std::get_if<TryExpr>(&e->node)) {
            return expr_returns_optional(tr->inner);
        }
        if (const auto* call = std::get_if<CallExpr>(&e->node)) {
            if (const auto* cid = std::get_if<IdentExpr>(&call->callee->node)) {
                if (auto it = cf_.fn_sigs.find(cid->name); it != cf_.fn_sigs.end()) {
                    return it->second.return_type.is_optional;
                }
            } else if (const auto* mem = std::get_if<MemberExpr>(&call->callee->node)) {
                if (const auto* recv_id = std::get_if<IdentExpr>(&mem->object->node)) {
                    if (const auto* sfn = cfire::frontend::StdlibCatalogIndex::instance().find_ns_fn(recv_id->name, mem->field)) {
                        if (sfn->is_optional) return true;
                    }
                    if (auto it = cf_.fn_sigs.find(recv_id->name + "." + mem->field); it != cf_.fn_sigs.end()) {
                        return it->second.return_type.is_optional;
                    }
                } else if (const auto* sub_mem = std::get_if<MemberExpr>(&mem->object->node)) {
                    if (const auto* sub_id = std::get_if<IdentExpr>(&sub_mem->object->node)) {
                        if (const auto* sfn = cfire::frontend::StdlibCatalogIndex::instance().find_ns_fn(sub_id->name, sub_mem->field + "." + mem->field)) {
                            if (sfn->is_optional) return true;
                        }
                    }
                }
                if (auto oit = cf_.method_owner.find(mem->field); oit != cf_.method_owner.end()) {
                    if (auto mit = cf_.methods.find(oit->second + "::" + mem->field); mit != cf_.methods.end()) {
                        return mit->second.return_type.is_optional;
                    }
                }
                if (auto uit = cfire::frontend::StdlibCatalogIndex::instance().unambiguous_methods.find(mem->field);
                    uit != cfire::frontend::StdlibCatalogIndex::instance().unambiguous_methods.end()) {
                    if (uit->second.is_optional) return true;
                }
                if (mem->field == "parse_i64" || mem->field == "parse_i32" || mem->field == "parse_u64" ||
                    mem->field == "parse_f64" || mem->field == "parse_bool" ||
                    mem->field == "try_recv" || mem->field == "recv_timeout" ||
                    mem->field == "getenv_opt" || mem->field == "read_line") {
                    return true;
                }
            }
        }
        return false;
    }

    void emit_stmt(const StmtPtr& s, bool in_main);
    void emit_or_handler_body(const OrExpr& or_ex, std::string_view tmp, bool in_main);
    void emit_match_common(const ExprPtr& target, bool is_move, const std::vector<MatchArm>& arms, bool is_expr, bool in_main, std::ostringstream& sb);
    void emit_match_stmt(const MatchStmt& ms, bool in_main);
    void emit_match_arm_body(std::ostringstream& sb, const std::vector<StmtPtr>& body, bool is_expr);
    [[nodiscard]] auto emit_match_expr(const MatchExpr& me) -> std::string;
    [[nodiscard]] auto match_lambda_header() const -> std::string;
    std::string match_expr_result_type_{};

    struct InterpolationResult {
        std::string fmt_str{};
        std::vector<std::string> args{};
    };

    [[nodiscard]] auto is_local_type(const std::string& name) const -> bool {
        if (cf_.struct_tags.contains(name) || cf_.enums.contains(name)) return true;
        for (const auto& st : cf_.ast.structs) {
            if (st.name == name) return true;
        }
        return false;
    }

    [[nodiscard]] auto is_package_ident(const std::string& ident) const -> bool {
        if (ident.empty()) return false;
        std::string prefix = ident + ".";
        for (const auto& [k, _] : cf_.fn_sigs) {
            if (k.starts_with(prefix)) return true;
        }
        for (const auto& gv : cf_.ast.global_vars) {
            if (gv.package_name == ident) return true;
        }
        for (const auto& st : cf_.ast.structs) {
            if (st.package_name == ident) return true;
        }
        for (const auto& en : cf_.ast.enums) {
            if (en.package_name == ident) return true;
        }
        return false;
    }

    [[nodiscard]] auto is_ident_in_scope(const std::string& ident) const -> bool {
        if (local_vars_.contains(ident)) return true;
        if (cf_.fn_sigs.contains(ident)) return true;
        if (is_core_ns_ident(ident)) return true;
        if (is_vendor_alias(ident)) return true;
        if (is_local_type(ident)) return true;
        if (ident == "i8" || ident == "i16" || ident == "i32" || ident == "i64" ||
            ident == "u8" || ident == "u16" || ident == "u32" || ident == "u64" ||
            ident == "usize" || ident == "isize" || ident == "f32" || ident == "f64" ||
            ident == "bool" || ident == "byte" || ident == "str") {
            return true;
        }
        for (const auto& gv : cf_.ast.global_vars) {
            if (gv.name == ident) return true;
        }
        return false;
    }

    [[nodiscard]] static auto escape_unescaped_quotes(std::string_view sv) -> std::string {
        std::string out;
        out.reserve(sv.size());
        for (std::size_t i = 0; i < sv.size(); ++i) {
            if (sv[i] == '"' && (i == 0 || sv[i - 1] != '\\')) {
                out += "\\\"";
            } else {
                out.push_back(sv[i]);
            }
        }
        return out;
    }

    [[nodiscard]] static auto expr_root_ident(const ExprPtr& e) -> std::string {
        if (!e) return "";
        if (const auto* id = std::get_if<IdentExpr>(&e->node)) return id->name;
        if (const auto* mem = std::get_if<MemberExpr>(&e->node)) return expr_root_ident(mem->object);
        if (const auto* idx = std::get_if<IndexExpr>(&e->node)) return expr_root_ident(idx->object);
        if (const auto* sl = std::get_if<SliceExpr>(&e->node)) return expr_root_ident(sl->object);
        if (const auto* call = std::get_if<CallExpr>(&e->node)) return expr_root_ident(call->callee);
        if (const auto* un = std::get_if<UnaryExpr>(&e->node)) return expr_root_ident(un->operand);
        if (const auto* tr = std::get_if<TryExpr>(&e->node)) return expr_root_ident(tr->inner);
        if (const auto* or_ex = std::get_if<OrExpr>(&e->node)) return expr_root_ident(or_ex->inner);
        if (const auto* bin = std::get_if<BinaryExpr>(&e->node)) {
            auto l = expr_root_ident(bin->left);
            return !l.empty() ? l : expr_root_ident(bin->right);
        }
        return "";
    }

    [[nodiscard]] auto extract_interpolation(const LiteralExpr& lit, int line) -> std::optional<InterpolationResult>;
    [[nodiscard]] auto emit_expr(const ExprPtr& e) -> std::string;
    [[nodiscard]] auto emit_call(const CallExpr& call) -> std::string;
    [[nodiscard]] auto infer_closure_expr_cpp_type(
        const ExprPtr& e,
        const std::unordered_map<std::string, std::string>& local_types
    ) const -> std::string;
    auto analyze_closure_returns(
        const std::vector<StmtPtr>& stmts,
        std::unordered_map<std::string, std::string>& local_types,
        bool& has_fallible,
        bool& has_val_ret,
        std::string& val_ret_cpp
    ) const -> void;
    [[nodiscard]] auto emit_closure(const ClosureExpr& cl, int expr_line = -1) -> std::string;
    [[nodiscard]] auto emit_call_arg(const CallArg& a) -> std::string;

    [[nodiscard]] auto lower_raw_owning_type(const TypeRef& tr) const -> std::string;

    [[nodiscard]] auto is_vendor_alias(std::string_view mod) const -> bool {
        if (cf_.vendor_aliases.contains(std::string(mod))) return true;
        for (const auto& [_, alias] : used_vendor_) {
            if (alias == mod) return true;
        }
        return false;
    }

    [[nodiscard]] auto is_cpp_qualifier(std::string_view mod) const -> bool {
        return mod == "cpp" || cf_.cpp_qualifiers.contains(std::string(mod));
    }

    [[nodiscard]] auto lower_owning_type(const TypeRef& tr) const -> std::string {
        std::string t = lower_raw_owning_type(tr);
        if (tr.is_optional) t = std::format("std::optional<{}>", t);
        if (tr.is_fallible) t = std::format("std::expected<{}, cfire::Error>", t);
        return t;
    }

    [[nodiscard]] auto lower_param_type(std::string_view intent, const TypeRef& tr) const -> std::string {
        if (intent == "mut" || intent == "out") return lower_owning_type(tr) + "&";
        if (intent == "move") return lower_owning_type(tr) + "&&";
        if (tr.is_ptr || tr.is_mut_ptr) return lower_owning_type(tr);
        if (tr.is_mut_borrow) {
            if (tr.is_slice && tr.elem_type) {
                return std::format("std::span<{}>", lower_owning_type(*tr.elem_type));
            }
            if (tr.name == "str") return "std::string&";
            return lower_owning_type(tr);
        }
        if (tr.is_borrow) {
            if (tr.is_slice && tr.elem_type) {
                return std::format("std::span<const {}>", lower_owning_type(*tr.elem_type));
            }
            if (tr.name == "str") return "cfire::str_ref";
            return lower_owning_type(tr);
        }
        // Q36 (CF-182): read-only `str` is a view, so slices and literals pass without a copy.
        if (tr.name == "str" && !tr.is_optional && !tr.is_fallible && tr.smart_wrap.empty()) return "cfire::str_ref";
        if (tr.is_slice && tr.elem_type) {
            // Q53 (CF-189): so is a read-only `[]T`, which takes sub-slices too (`[]bool` has no contiguous storage).
            const std::string elem = lower_owning_type(*tr.elem_type);
            if (elem != "bool" && !tr.is_optional && !tr.is_fallible && tr.smart_wrap.empty() && !mentions_generic_param(*tr.elem_type)) {
                return std::format("cfire::slice_ref<{}>", elem);
            }
            return std::format("const std::vector<{}>&", elem);
        }
        if (tr.name == "path") return "const std::filesystem::path&";
        if (tr.name == "i8" || tr.name == "i16" || tr.name == "i32" || tr.name == "i64" ||
            tr.name == "u8" || tr.name == "byte" || tr.name == "u16" || tr.name == "u32" ||
            tr.name == "u64" || tr.name == "usize" || tr.name == "isize" ||
            tr.name == "f32" || tr.name == "f64" || tr.name == "bool") {
            return lower_owning_type(tr);
        }
        return "const " + lower_owning_type(tr) + "&";
    }

    [[nodiscard]] auto lower_return_type(const TypeRef& tr) const -> std::string {
        return lower_owning_type(tr);
    }

    [[nodiscard]] auto is_non_value_field_type(const TypeRef& tr, int depth = 0) const -> bool {
        if (depth > 8) return true;
        if (tr.is_ptr || tr.is_mut_ptr || tr.is_borrow ||
            tr.is_mut_borrow || tr.is_fn || !tr.smart_wrap.empty()) {
            return true;
        }
        if (tr.is_slice && tr.elem_type) return is_non_value_field_type(*tr.elem_type, depth + 1);
        std::string n = tr.name;
        if (n.starts_with("cpp::") || n.starts_with("webui.") || n.starts_with("webui::") ||
            n.starts_with("sync.") || n.starts_with("sync::") ||
            n.starts_with("sqlite.") || n.starts_with("sqlite::") ||
            n.starts_with("ssh.") || n.starts_with("ssh::") ||
            n.starts_with("fs.") || n.starts_with("fs::") ||
            n.starts_with("net.") || n.starts_with("net::") ||
            n.starts_with("http.") || n.starts_with("http::") ||
            n == "App" || n == "Mutex" || n == "WaitGroup" || n == "Channel" ||
            n == "Conn" || n == "Session" || n == "File" || n == "TcpConn" || n == "TcpListener") {
            return true;
        }
        for (const auto& sub_st : cf_.ast.structs) {
            if (sub_st.name == n) {
                for (const auto& sf : sub_st.fields) {
                    if (is_non_value_field_type(sf.type, depth + 1)) return true;
                }
                break;
            }
        }
        return false;
    }

    [[nodiscard]] auto can_emit_json_field_type(const TypeRef& tr, int depth = 0) const -> bool {
        if (depth > 8) return false;
        if (is_non_value_field_type(tr, depth)) return false;
        if (tr.is_map || tr.is_tuple || tr.is_array) return false;
        if (tr.is_slice) return tr.elem_type && can_emit_json_field_type(*tr.elem_type, depth + 1);
        if (tr.is_optional) {
            TypeRef inner = tr;
            inner.is_optional = false;
            return can_emit_json_field_type(inner, depth + 1);
        }
        const std::string& n = tr.name;
        if (n == "i8" || n == "i16" || n == "i32" || n == "i64" || n == "isize" ||
            n == "u8" || n == "u16" || n == "u32" || n == "u64" || n == "usize" ||
            n == "f32" || n == "f64" || n == "bool" || n == "str") {
            return true;
        }
        if (cf_.enums.contains(n)) return true;
        for (const auto& sub_st : cf_.ast.structs) {
            if (sub_st.name == n) {
                for (const auto& sf : sub_st.fields) {
                    if (!can_emit_json_field_type(sf.type, depth + 1)) return false;
                }
                return true;
            }
        }
        return false;
    }
};

[[nodiscard]] CFIRE_CG_INLINE auto emit_cpp(const CheckedFile& cf, bool test_mode = false) -> std::string;
[[nodiscard]] CFIRE_CG_INLINE auto emit_sharded_cpp(const CheckedFile& cf, bool test_mode = false) -> ShardedEmission;

} // namespace cfire::frontend

#if !defined(CFIRE_SPLIT_TUS) || defined(CFIRE_TU_CODEGEN_DECL_IMPL)
#include "codegen/decl.inc"
#include "codegen/reflect.hpp"
#include "codegen/test_runner.hpp"

namespace cfire::frontend {
[[nodiscard]] CFIRE_CG_INLINE auto emit_cpp(const CheckedFile& cf, bool test_mode) -> std::string {
    Emitter em(cf, test_mode);
    return em.emit_cpp();
}

[[nodiscard]] CFIRE_CG_INLINE auto emit_sharded_cpp(const CheckedFile& cf, bool test_mode) -> ShardedEmission {
    Emitter em(cf, test_mode);
    return em.emit_sharded();
}
} // namespace cfire::frontend
#endif

#if !defined(CFIRE_SPLIT_TUS) || defined(CFIRE_TU_CODEGEN_BODY_IMPL)
#include "codegen/stmt.hpp"
#include "codegen/expr.hpp"

namespace cfire::frontend {
CFIRE_CG_INLINE auto Emitter::emit_make(const CallExpr& call) -> std::string {
    const TypeRef& ta = call.type_args[0];
    std::string args;
    for (std::size_t i = 0; i < call.args.size(); ++i) {
        if (i > 0) args += ", ";
        args += emit_call_arg(call.args[i]);
    }
    if (!ta.smart_wrap.empty() && ta.elem_type) {
        const char* helper = ta.smart_wrap == "own" ? "cfire::make_own" : "cfire::make_shared_ptr";
        std::string inner = lower_owning_type(*ta.elem_type);
        if (std::string ctor = user_ctor_name(*ta.elem_type); !ctor.empty() && !call.make_from_value) {
            return std::format("{}<{}>({}({}))", helper, inner, ctor, args);
        }
        return std::format("{}<{}>({})", helper, inner, args);
    }
    if (ta.is_slice && ta.elem_type) {
        return std::format("cfire::make_slice<{}>({})", lower_owning_type(*ta.elem_type), args);
    }
    if (ta.is_map || ta.is_array) return std::format("{}{{}}", lower_owning_type(ta));
    if (std::string ctor = user_ctor_name(ta); !ctor.empty()) return std::format("{}({})", ctor, args);
    std::string spelled = ta.name;
    for (auto p = spelled.find("::"); p != std::string::npos; p = spelled.find("::")) spelled.replace(p, 2, ".");
    if (auto dot = spelled.find('.'); dot != std::string::npos) {
        std::string ns = spelled.substr(0, dot);
        if (auto it = core_aliases_.find(ns); it != core_aliases_.end()) ns = it->second;
        if (const auto* sc = find_stdlib_ctor_for(ns, std::string_view(spelled).substr(dot + 1))) {
            used_core_.insert(std::string(sc->ns));
            if (sc->generic && !ta.type_args.empty()) {
                if (sc->type == "Mutex" && ta.type_args[0].name == "void") return "cfire::sync::MutexT<void>{}";
                return std::format("{}<{}>({})", sc->cpp, lower_owning_type(ta.type_args[0]), args);
            }
            // Labeled arguments (`tls: cfg`) go to their parameter; skipped ones get their declared default.
            if (const auto* sig = StdlibCatalogIndex::instance().find_ns_fn(sc->ns, std::string(sc->type) + ".make");
                sig && std::any_of(call.args.begin(), call.args.end(), [](const CallArg& a) { return !a.label.empty(); })) {
                std::vector<std::string> vals(sig->params.size());
                std::size_t next_pos = 0;
                std::size_t last = 0;
                for (const auto& a : call.args) {
                    std::size_t slot = next_pos;
                    if (!a.label.empty()) {
                        for (std::size_t k = 0; k < sig->params.size(); ++k) {
                            if (sig->params[k].name == a.label) { slot = k; break; }
                        }
                    } else {
                        ++next_pos;
                    }
                    if (slot < vals.size()) {
                        vals[slot] = emit_call_arg(a);
                        last = std::max(last, slot + 1);
                    }
                }
                std::string placed;
                for (std::size_t k = 0; k < last; ++k) {
                    std::string v = vals[k];
                    if (v.empty()) v = sig->params[k].default_text.empty() ? std::string("{}") : sig->params[k].default_text;
                    placed += (k > 0 ? ", " : "") + v;
                }
                return std::format("{}({})", sc->cpp, placed);
            }
            return std::format("{}({})", sc->cpp, args);
        }
    }
    return std::format("{}{{{}}}", lower_owning_type(ta), args);
}

} // namespace cfire::frontend

namespace cfire::frontend {
} // namespace cfire::frontend
#endif

#undef CFIRE_CG_INLINE
#endif

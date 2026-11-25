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

#if defined(CFIRE_SPLIT_TUS) && !defined(CFIRE_CODEGEN_DECL_IMPL) && !defined(CFIRE_CODEGEN_BODY_IMPL)

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

    // `i32`, `f64`, `bool`, ... used as a value prefix (`i32.MAX`, `f64.parse(s)`); returns the C++ type.
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

    // the C++ for every `make(T, args...)` form. Sema has already checked arity and argument types.
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
    TypeRef cur_fn_return_typeref_{};
    bool has_cur_fn_return_typeref_ = false;
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
    std::unordered_set<std::string> error_vars_{};
    std::vector<std::string> active_async_scopes_{};
    // scope variable -> (its `cfire::async::Results<T>` collector, the lowered `T`), for the value-producing
    // scopes being emitted; their `scope.spawn` calls add the task to the collector.
    std::unordered_map<std::string, std::pair<std::string, std::string>> value_scope_results_{};

    enum class LowerContext : uint8_t {
        Value,
        ParamIn,
        ParamMut,
        ParamMove,
        Return,
        RangeIndex
    };
    [[nodiscard]] auto lower_named_cpp_type(std::string_view raw_name, bool has_type_args) const -> std::string;
    [[nodiscard]] auto lower_type_id(TypeId tid, LowerContext ctx = LowerContext::Value) const -> std::optional<std::string>;
    [[nodiscard]] auto lower_var_decl_cpp_type(const VarDeclStmt& st) const -> std::string;
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
    // / read-only `str` and `[]T` params (lowered to `cfire::str_ref` / `cfire::slice_ref<T>`), with the
    // owned C++ type an escaping closure copies them into.
    std::vector<std::pair<std::string, std::string>> view_params_{};
    std::unordered_map<const Expr*, std::string> hoisted_tries_{};

    // an integer constant expression (literals, unary `-`, arithmetic and bitwise operators); Sema types it `i64`.
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
        return cfire::frontend::sanitize_cpp_ident(id);
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
        sub.error_vars_ = error_vars_;
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
            sub.cur_fn_return_typeref_ = cur_fn_return_typeref_;
            sub.has_cur_fn_return_typeref_ = has_cur_fn_return_typeref_;
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

    // Effective template parameter list for emitting a function or method. Function-level type parameters
    // come first so explicit `x.method[U](...)` binds `U` while receiver type args (`fn (b: Box[T])`) are deduced.
    [[nodiscard]] auto effective_type_params(const FnDecl& fn) const -> std::vector<TypeParam> {
        std::vector<TypeParam> out = fn.type_params;
        if (!fn.receiver || fn.receiver->type.type_args.empty()) return out;
        static const std::unordered_set<std::string_view> kBuiltinNames = {
            "i8", "i16", "i32", "i64", "isize", "u8", "u16", "u32", "u64", "usize",
            "f32", "f64", "bool", "byte", "str", "any", "void", "Error"
        };
        std::unordered_set<std::string_view> seen;
        for (const auto& tp : out) seen.insert(tp.name);
        const StructDecl* recv_st = nullptr;
        for (const auto& st : cf_.ast.structs) {
            if (st.name == fn.receiver->type.name) { recv_st = &st; break; }
        }
        const EnumDecl* recv_en = nullptr;
        if (!recv_st) {
            for (const auto& en : cf_.ast.enums) {
                if (en.name == fn.receiver->type.name) { recv_en = &en; break; }
            }
        }
        for (std::size_t idx = 0; idx < fn.receiver->type.type_args.size(); ++idx) {
            const auto& ta = fn.receiver->type.type_args[idx];
            if (ta.name.empty() || ta.is_ptr || ta.is_mut_ptr || ta.is_slice || ta.is_array ||
                ta.is_map || ta.is_optional || ta.is_tuple || ta.is_fn || !ta.type_args.empty()) {
                continue;
            }
            if (kBuiltinNames.contains(ta.name) || cf_.declared_type_names.contains(ta.name)) continue;
            if (!seen.insert(ta.name).second) continue;
            std::vector<std::string> constraints;
            if (recv_st && idx < recv_st->type_params.size()) constraints = recv_st->type_params[idx].constraints;
            else if (recv_en && idx < recv_en->type_params.size()) constraints = recv_en->type_params[idx].constraints;
            out.push_back(TypeParam{ta.name, std::move(constraints)});
        }
        return out;
    }

    // The type parameters in scope for the function being emitted (its own and a generic receiver's), so a
    // `[]T` parameter of a type parameter keeps a deducible `const std::vector<T>&`.
    std::unordered_set<std::string> generic_params_{};
    void set_generic_params(const FnDecl& fn) {
        generic_params_.clear();
        for (const auto& tp : effective_type_params(fn)) generic_params_.insert(tp.name);
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
                    if (expr_references_ident(arm.guard, ident)) return true;
                    if (stmts_reference_ident(arm.body, ident)) return true;
                }
                return false;
            }
            return false;
        }, e->node);
    }

    // an escaping closure owns copies of the view parameters it uses, so it cannot outlive their text.
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
                        if (expr_references_ident(arm.guard, ident)) return true;
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
                if (mem->field == "try_recv" || mem->field == "recv_timeout" ||
                    mem->field == "read_line" || mem->field == "ignore_error" ||
                    (mem->field == "getenv" && call->args.size() <= 1)) {
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
    TypeId match_expr_result_tid_{TypeId::Unknown()};

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

    [[nodiscard]] auto resolve_concept_name(std::string_view bound) const -> std::string {
        if (auto it = cf_.interface_defs.find(std::string(bound)); it != cf_.interface_defs.end()) {
            return it->second.name;
        }
        if (bound == "Comparable" || bound == "Ordered" || bound == "Numeric" || bound == "Integer") {
            return "cfire::constraints::" + std::string(bound);
        }
        if (auto dot = bound.rfind('.'); dot != std::string_view::npos) {
            return std::string(bound.substr(dot + 1));
        }
        return std::string(bound);
    }

    [[nodiscard]] auto resolve_enum_cpp_name(std::string_view prefix) const -> std::string {
        if (prefix.empty()) return "";
        if (cf_.enums.contains(std::string(prefix))) return std::string(prefix);
        if (auto dot = prefix.find('.'); dot != std::string_view::npos) {
            std::string_view pkg = prefix.substr(0, dot);
            std::string_view ename = prefix.substr(dot + 1);
            std::string mangled = std::format("cfire_pkg_{}__{}", pkg, ename);
            if (cf_.enums.contains(mangled)) return mangled;
            for (const auto& [k, en] : cf_.enums) {
                (void)k;
                std::string orig = en.name;
                if (orig.starts_with("cfire_pkg_")) {
                    if (auto sep = orig.find("__"); sep != std::string::npos) orig = orig.substr(sep + 2);
                }
                if (en.package_name == pkg && orig == ename) return en.name;
            }
        }
        return std::string(prefix);
    }

    [[nodiscard]] auto format_template_clause(const std::vector<TypeParam>& tps) const -> std::string {
        if (tps.empty()) return "";
        const bool has_multi = std::any_of(tps.begin(), tps.end(), [](const TypeParam& tp) {
            return tp.constraints.size() > 1;
        });
        std::string tparams;
        std::string reqs;
        for (std::size_t i = 0; i < tps.size(); ++i) {
            if (i > 0) tparams += ", ";
            const auto& tp = tps[i];
            if (!has_multi && tp.constraints.size() == 1) {
                tparams += std::format("{} {}", resolve_concept_name(tp.constraints[0]), tp.name);
            } else {
                tparams += std::format("typename {}", tp.name);
                for (const auto& b : tp.constraints) {
                    if (!reqs.empty()) reqs += " && ";
                    reqs += std::format("{}<{}>", resolve_concept_name(b), tp.name);
                }
            }
        }
        if (!reqs.empty()) {
            return std::format("template <{}> requires ({})", tparams, reqs);
        }
        return std::format("template <{}>", tparams);
    }

    [[nodiscard]] static auto lower_cpp_int_literal(std::string_view lit) -> std::string {
        if (lit.starts_with("-0o") || lit.starts_with("-0O")) {
            std::string_view rest = lit.substr(3);
            const bool all_zero = !rest.empty() && std::all_of(rest.begin(), rest.end(), [](char c) { return c == '0'; });
            return all_zero ? "0" : ("-0" + std::string(rest));
        }
        if (lit.starts_with("0o") || lit.starts_with("0O")) {
            std::string_view rest = lit.substr(2);
            const bool all_zero = !rest.empty() && std::all_of(rest.begin(), rest.end(), [](char c) { return c == '0'; });
            return all_zero ? "0" : ("0" + std::string(rest));
        }
        return std::string(lit);
    }

    [[nodiscard]] static auto lower_default_expr_text(std::string_view d) -> std::string {
        if (d.empty() || d == "[]") return "{}";
        if (d.starts_with("0o") || d.starts_with("0O") || d.starts_with("-0o") || d.starts_with("-0O")) {
            return lower_cpp_int_literal(d);
        }
        return std::string(d);
    }

    static void append_utf8_codepoint(std::string& out, std::uint32_t cp) {
        if (cp <= 0x7Fu) {
            out.push_back(static_cast<char>(cp));
        } else if (cp <= 0x7FFu) {
            out.push_back(static_cast<char>(0xC0u | (cp >> 6)));
            out.push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
        } else if (cp <= 0xFFFFu) {
            out.push_back(static_cast<char>(0xE0u | (cp >> 12)));
            out.push_back(static_cast<char>(0x80u | ((cp >> 6) & 0x3Fu)));
            out.push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
        } else if (cp <= 0x10FFFFu) {
            out.push_back(static_cast<char>(0xF0u | (cp >> 18)));
            out.push_back(static_cast<char>(0x80u | ((cp >> 12) & 0x3Fu)));
            out.push_back(static_cast<char>(0x80u | ((cp >> 6) & 0x3Fu)));
            out.push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
        }
    }

    [[nodiscard]] static auto encode_cpp_octal_bytes(std::string_view bytes) -> std::string {
        std::string out;
        out.reserve(bytes.size() * 2);
        for (unsigned char uc : bytes) {
            switch (uc) {
                case '\\': out += "\\\\"; break;
                case '"': out += "\\\""; break;
                case '\n': out += "\\n"; break;
                case '\r': out += "\\r"; break;
                case '\t': out += "\\t"; break;
                case '\0': out += "\\000"; break;
                default:
                    if (uc >= 0x20 && uc <= 0x7E) {
                        out.push_back(static_cast<char>(uc));
                    } else {
                        out += std::format("\\{:03o}", static_cast<unsigned int>(uc));
                    }
                    break;
            }
        }
        return out;
    }

    // Decodes a raw cfire string literal snippet when it contains embedded NUL, `\u{...}`, or `\xHH` escapes
    // that could truncate at `\0` or greedily consume trailing hex digits in C++. Returns `{bytes, has_nul, reencode}`.
    [[nodiscard]] static auto decode_cfire_string_escapes(std::string_view raw, bool for_format = false) -> std::tuple<std::string, bool, bool> {
        std::string decoded;
        decoded.reserve(raw.size());
        bool has_nul = false;
        bool needs_reencode = false;
        for (std::size_t i = 0; i < raw.size();) {
            if (raw[i] != '\\' || i + 1 >= raw.size()) {
                decoded.push_back(raw[i]);
                ++i;
                continue;
            }
            char esc = raw[i + 1];
            if (esc == '0') {
                decoded.push_back('\0');
                has_nul = true;
                needs_reencode = true;
                i += 2;
            } else if (esc == 'n') { decoded.push_back('\n'); i += 2; }
            else if (esc == 't') { decoded.push_back('\t'); i += 2; }
            else if (esc == 'r') { decoded.push_back('\r'); i += 2; }
            else if (esc == 'b') { decoded.push_back('\b'); i += 2; }
            else if (esc == 'f') { decoded.push_back('\f'); i += 2; }
            else if (esc == 'v') { decoded.push_back('\v'); i += 2; }
            else if (esc == 'a') { decoded.push_back('\a'); i += 2; }
            else if (esc == '\\') { decoded.push_back('\\'); i += 2; }
            else if (esc == '"') { decoded.push_back('"'); i += 2; }
            else if (esc == '\'') { decoded.push_back('\''); i += 2; }
            else if (esc == '{' || esc == '}') {
                decoded.push_back(esc);
                if (for_format) decoded.push_back(esc);
                i += 2;
            }
            else if (esc == 'x' && i + 3 < raw.size() &&
                     std::isxdigit(static_cast<unsigned char>(raw[i + 2])) &&
                     std::isxdigit(static_cast<unsigned char>(raw[i + 3]))) {
                std::uint32_t b = 0;
                std::from_chars(raw.data() + i + 2, raw.data() + i + 4, b, 16);
                decoded.push_back(static_cast<char>(static_cast<unsigned char>(b)));
                if (b == 0) has_nul = true;
                needs_reencode = true;
                i += 4;
            } else if (esc == 'u' && i + 2 < raw.size() && raw[i + 2] == '{') {
                std::size_t j = i + 3;
                while (j < raw.size() && raw[j] != '}') ++j;
                if (j < raw.size() && j > i + 3) {
                    std::uint32_t cp = 0;
                    auto [ptr, ec] = std::from_chars(raw.data() + i + 3, raw.data() + j, cp, 16);
                    if (ec == std::errc{} && ptr == raw.data() + j) {
                        append_utf8_codepoint(decoded, cp);
                        if (cp == 0) has_nul = true;
                        needs_reencode = true;
                        i = j + 1;
                        continue;
                    }
                }
                decoded.push_back(raw[i]);
                decoded.push_back(esc);
                i += 2;
            } else if (esc == 'u' && i + 5 < raw.size()) {
                std::uint32_t cp = 0;
                auto [ptr, ec] = std::from_chars(raw.data() + i + 2, raw.data() + i + 6, cp, 16);
                if (ec == std::errc{} && ptr == raw.data() + i + 6) {
                    append_utf8_codepoint(decoded, cp);
                    if (cp == 0) has_nul = true;
                    needs_reencode = true;
                    i += 6;
                } else {
                    decoded.push_back(raw[i]);
                    decoded.push_back(esc);
                    i += 2;
                }
            } else {
                decoded.push_back(raw[i]);
                decoded.push_back(esc);
                i += 2;
            }
        }
        return {std::move(decoded), has_nul, needs_reencode};
    }

    [[nodiscard]] static auto rewrite_brace_escapes_preserving_escapes(std::string_view raw, bool for_format) -> std::string {
        std::string out;
        out.reserve(raw.size());
        for (std::size_t i = 0; i < raw.size();) {
            if (raw[i] == '\\' && i + 1 < raw.size()) {
                char esc = raw[i + 1];
                if (esc == '{' || esc == '}') {
                    out.push_back(esc);
                    if (for_format) out.push_back(esc);
                    i += 2;
                    continue;
                }
                out.push_back(raw[i]);
                out.push_back(esc);
                if (esc == 'u' && i + 2 < raw.size() && raw[i + 2] == '{') {
                    std::size_t k = i + 2;
                    while (k < raw.size() && raw[k] != '}') {
                        out.push_back(raw[k++]);
                    }
                    if (k < raw.size()) out.push_back(raw[k++]);
                    i = k;
                } else {
                    i += 2;
                }
                continue;
            }
            out.push_back(raw[i++]);
        }
        return out;
    }

    [[nodiscard]] static auto lower_string_literal_cpp(std::string_view raw) -> std::string {
        auto [decoded, has_nul, needs_reencode] = decode_cfire_string_escapes(raw, false);
        if (has_nul) {
            return std::format("cfire::str_ref(\"{}\", {})", encode_cpp_octal_bytes(decoded), decoded.size());
        }
        if (needs_reencode) {
            return std::format("\"{}\"", encode_cpp_octal_bytes(decoded));
        }
        return std::format("\"{}\"", escape_unescaped_quotes(rewrite_brace_escapes_preserving_escapes(raw, false)));
    }

    [[nodiscard]] static auto escape_format_literal_segment(std::string_view raw) -> std::string {
        auto [decoded, has_nul, needs_reencode] = decode_cfire_string_escapes(raw, true);
        if (needs_reencode) {
            return encode_cpp_octal_bytes(decoded);
        }
        return escape_unescaped_quotes(rewrite_brace_escapes_preserving_escapes(raw, true));
    }

    [[nodiscard]] auto infer_elem_cpp_type(const ExprPtr& e) const -> std::string {
        if (!e) return "";
        if (const auto* lit = std::get_if<LiteralExpr>(&e->node)) {
            if (lit->kind == TokenKind::String || lit->kind == TokenKind::RawString) return "std::string";
            if (lit->kind == TokenKind::Int) return "std::int64_t";
            if (lit->kind == TokenKind::Float) return "double";
            if (lit->kind == TokenKind::Char) return "std::uint8_t";
            if (lit->kind == TokenKind::KwTrue || lit->kind == TokenKind::KwFalse) return "bool";
        }
        if (const auto* un = std::get_if<UnaryExpr>(&e->node); un && (un->op == "-" || un->op == "+") && un->operand) {
            return infer_elem_cpp_type(un->operand);
        }
        if (const auto* nested_arr = std::get_if<ArrayLitExpr>(&e->node)) {
            return infer_array_lit_cpp_type(*nested_arr);
        }
        if (const auto* tup = std::get_if<TupleExpr>(&e->node); tup && !tup->elements.empty()) {
            std::string inner;
            for (std::size_t i = 0; i < tup->elements.size(); ++i) {
                std::string et = infer_elem_cpp_type(tup->elements[i]);
                if (et.empty()) return "";
                if (i > 0) inner += ", ";
                inner += et;
            }
            return std::format("std::tuple<{}>", inner);
        }
        if (const auto* slit = std::get_if<StructLitExpr>(&e->node); slit && !slit->type_name.empty()) {
            TypeRef tr;
            tr.name = slit->type_name;
            tr.type_args = slit->type_args;
            return lower_owning_type(tr);
        }
        if (const auto* id = std::get_if<IdentExpr>(&e->node)) {
            if (auto it = var_types_.find(id->name); it != var_types_.end() && !it->second.empty() && it->second != "str") {
                TypeRef tr;
                tr.name = it->second;
                return lower_owning_type(tr);
            }
        }
        return "";
    }

    [[nodiscard]] auto infer_array_lit_cpp_type(const ArrayLitExpr& arr) const -> std::string {
        if (arr.elements.empty()) return "";
        std::string et = infer_elem_cpp_type(arr.elements[0]);
        if (et.empty()) return "";
        return std::format("std::vector<{}>", et);
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
    [[nodiscard]] auto emit_typed_tuple(const TupleExpr& tup, const TypeRef& tr) -> std::string;
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
        // read-only `str` is a view, so slices and literals pass without a copy.
        if (tr.name == "str" && !tr.is_optional && !tr.is_fallible && tr.smart_wrap.empty()) return "cfire::str_ref";
        if (tr.is_slice && tr.elem_type) {
            // so is a read-only `[]T`, which takes sub-slices too (`[]bool` has no contiguous storage).
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
        if (n == "fs.FileInfo" || n == "fs::FileInfo" || n == "FileInfo" ||
            n == "ipc.PeerCred" || n == "ipc::PeerCred" || n == "PeerCred") {
            return false;
        }
        if (n.starts_with("cpp::") || n.starts_with("webui.") || n.starts_with("webui::") ||
            n.starts_with("sync.") || n.starts_with("sync::") ||
            n.starts_with("sqlite.") || n.starts_with("sqlite::") ||
            n.starts_with("ssh.") || n.starts_with("ssh::") ||
            n.starts_with("fs.") || n.starts_with("fs::") ||
            n.starts_with("net.") || n.starts_with("net::") ||
            n.starts_with("http.") || n.starts_with("http::") ||
            n.starts_with("ipc.") || n.starts_with("ipc::") ||
            n.starts_with("websocket.") || n.starts_with("websocket::") ||
            n == "App" || n == "Mutex" || n == "WaitGroup" || n == "Channel" ||
            n == "Conn" || n == "Session" || n == "File" || n == "TcpConn" || n == "TcpListener" ||
            n == "Listener" || n == "HttpServer" || n == "Server") {
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

#if !defined(CFIRE_SPLIT_TUS) || defined(CFIRE_CODEGEN_DECL_IMPL)
#include "codegen/declarations.hpp"
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

#if !defined(CFIRE_SPLIT_TUS) || defined(CFIRE_CODEGEN_BODY_IMPL)
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
                    std::size_t slot = vals.size();
                    if (!a.label.empty()) {
                        for (std::size_t k = 0; k < sig->params.size(); ++k) {
                            if (sig->params[k].name == a.label) { slot = k; break; }
                        }
                    } else {
                        while (next_pos < vals.size() && !vals[next_pos].empty()) ++next_pos;
                        slot = next_pos++;
                    }
                    if (slot < vals.size()) {
                        vals[slot] = emit_call_arg(a);
                        last = std::max(last, slot + 1);
                    }
                }
                std::string placed;
                for (std::size_t k = 0; k < last; ++k) {
                    std::string v = vals[k];
                    if (v.empty()) v = lower_default_expr_text(sig->params[k].default_text);
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

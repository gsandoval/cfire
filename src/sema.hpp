#pragma once
// src/sema.hpp — C++23 Semantic Analyzer, Intent Verifier, Exhaustive Match & Struct Tag Extractor

#include "ast.hpp"
#include <expected>
#include <format>
#include <regex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace cfire::frontend {

struct StructTagInfo {
    std::string field_name{};
    TypeRef field_type{};
    std::string cli_long{};
    std::string cli_short{};
    std::string cli_help{};
    std::string json_key{};
    std::string db_column{};
    std::unordered_map<std::string, std::string> custom_tags{};
};

struct CheckedFile {
    FileAST ast{};
    std::unordered_map<std::string, FnDecl> fn_sigs{};
    std::unordered_map<std::string, FnDecl> methods{};     // "Type::method" -> FnDecl
    std::unordered_map<std::string, std::string> method_owner{}; // "method" -> "Type"
    std::unordered_map<std::string, EnumDecl> enums{};     // "EnumName" -> EnumDecl
    std::unordered_map<std::string, std::string> variant_to_enum{}; // "VariantName" -> "EnumName"
    std::unordered_map<std::string, std::vector<StructTagInfo>> struct_tags{};
    std::unordered_map<std::string, int> last_use_move{}; // "fnName:varName" -> line
    std::unordered_map<std::string, std::vector<std::string>> cpp_fn_params{}; // C/C++ fn -> param QualTypes
    std::vector<std::string> warnings{};
};

[[nodiscard]] inline auto trim_str(std::string_view sv) -> std::string {
    while (!sv.empty() && std::isspace(static_cast<unsigned char>(sv.front()))) sv.remove_prefix(1);
    while (!sv.empty() && std::isspace(static_cast<unsigned char>(sv.back()))) sv.remove_suffix(1);
    return std::string(sv);
}

inline auto sema_to_str(int v) -> std::string { return std::to_string(v); }

// --- AST Node Kind Inspection ---
[[nodiscard]] inline auto get_expr_kind(const ExprPtr& e) -> int {
    if (!e) return 0;
    return std::visit([](const auto& n) -> int {
        using T = std::decay_t<decltype(n)>;
        if constexpr (std::is_same_v<T, IdentExpr>) return 1;
        else if constexpr (std::is_same_v<T, LiteralExpr>) return 2;
        else if constexpr (std::is_same_v<T, UnaryExpr>) return 3;
        else if constexpr (std::is_same_v<T, BinaryExpr>) return 4;
        else if constexpr (std::is_same_v<T, MemberExpr>) return 5;
        else if constexpr (std::is_same_v<T, IndexExpr>) return 6;
        else if constexpr (std::is_same_v<T, SliceExpr>) return 7;
        else if constexpr (std::is_same_v<T, TryExpr>) return 8;
        else if constexpr (std::is_same_v<T, OrExpr>) return 9;
        else if constexpr (std::is_same_v<T, StructLitExpr>) return 10;
        else if constexpr (std::is_same_v<T, ArrayLitExpr>) return 11;
        else if constexpr (std::is_same_v<T, TupleExpr>) return 12;
        else if constexpr (std::is_same_v<T, ClosureExpr>) return 13;
        else if constexpr (std::is_same_v<T, CallExpr>) return 14;
        return 0;
    }, e->node);
}

[[nodiscard]] inline auto get_stmt_kind(const StmtPtr& s) -> int {
    if (!s) return 0;
    return std::visit([](const auto& n) -> int {
        using T = std::decay_t<decltype(n)>;
        if constexpr (std::is_same_v<T, VarDeclStmt>) return 1;
        else if constexpr (std::is_same_v<T, DestructureVarDeclStmt>) return 2;
        else if constexpr (std::is_same_v<T, AssignStmt>) return 3;
        else if constexpr (std::is_same_v<T, DestructureAssignStmt>) return 4;
        else if constexpr (std::is_same_v<T, DeferStmt>) return 5;
        else if constexpr (std::is_same_v<T, ReturnStmt>) return 6;
        else if constexpr (std::is_same_v<T, IfStmt>) return 7;
        else if constexpr (std::is_same_v<T, ForStmt>) return 8;
        else if constexpr (std::is_same_v<T, MatchStmt>) return 9;
        else if constexpr (std::is_same_v<T, AsyncScopeStmt>) return 10;
        else if constexpr (std::is_same_v<T, ExprStmt>) return 11;
        else if constexpr (std::is_same_v<T, CppBlockStmt>) return 12;
        return 0;
    }, s->node);
}

[[nodiscard]] inline auto get_expr_line(const ExprPtr& e) -> int { return e ? e->line : 0; }
[[nodiscard]] inline auto get_expr_col(const ExprPtr& e) -> int { return e ? e->col : 0; }
[[nodiscard]] inline auto get_stmt_line(const StmtPtr& s) -> int { return s ? s->line : 0; }

// --- Expression Extractors ---
[[nodiscard]] inline auto expr_get_ident(const ExprPtr& e) -> std::string {
    if (e) {
        if (const auto* id = std::get_if<IdentExpr>(&e->node)) return id->name;
    }
    return {};
}

[[nodiscard]] inline auto expr_get_literal_value(const ExprPtr& e) -> std::string {
    if (e) {
        if (const auto* lit = std::get_if<LiteralExpr>(&e->node)) return lit->value;
    }
    return {};
}

[[nodiscard]] inline auto expr_get_literal_is_string(const ExprPtr& e) -> bool {
    if (e) {
        if (const auto* lit = std::get_if<LiteralExpr>(&e->node)) return lit->kind == TokenKind::String;
    }
    return false;
}

[[nodiscard]] inline auto extract_interpolated_vars(const std::string& val) -> std::vector<std::string> {
    if (!val.contains('{')) return {};
    static const std::regex interp_var_re(R"(\{([a-zA-Z_][a-zA-Z0-9_]*)\})");
    std::vector<std::string> vars;
    for (auto it = std::sregex_iterator(val.begin(), val.end(), interp_var_re); it != std::sregex_iterator(); ++it) {
        vars.push_back((*it)[1].str());
    }
    return vars;
}

[[nodiscard]] inline auto expr_get_unary_op(const ExprPtr& e) -> std::string {
    if (e) {
        if (const auto* un = std::get_if<UnaryExpr>(&e->node)) return un->op;
    }
    return {};
}

[[nodiscard]] inline auto expr_get_unary_operand(const ExprPtr& e) -> ExprPtr {
    if (e) {
        if (const auto* un = std::get_if<UnaryExpr>(&e->node)) return un->operand;
    }
    return nullptr;
}

[[nodiscard]] inline auto expr_get_binary_left(const ExprPtr& e) -> ExprPtr {
    if (e) {
        if (const auto* bin = std::get_if<BinaryExpr>(&e->node)) return bin->left;
    }
    return nullptr;
}

[[nodiscard]] inline auto expr_get_binary_right(const ExprPtr& e) -> ExprPtr {
    if (e) {
        if (const auto* bin = std::get_if<BinaryExpr>(&e->node)) return bin->right;
    }
    return nullptr;
}

[[nodiscard]] inline auto expr_get_member_object(const ExprPtr& e) -> ExprPtr {
    if (e) {
        if (const auto* mem = std::get_if<MemberExpr>(&e->node)) return mem->object;
    }
    return nullptr;
}

[[nodiscard]] inline auto expr_get_member_field(const ExprPtr& e) -> std::string {
    if (e) {
        if (const auto* mem = std::get_if<MemberExpr>(&e->node)) return mem->field;
    }
    return {};
}

[[nodiscard]] inline auto expr_get_member_is_namespace(const ExprPtr& e) -> bool {
    if (e) {
        if (const auto* mem = std::get_if<MemberExpr>(&e->node)) return mem->is_namespace;
    }
    return false;
}

[[nodiscard]] inline auto expr_get_index_object(const ExprPtr& e) -> ExprPtr {
    if (e) {
        if (const auto* idx = std::get_if<IndexExpr>(&e->node)) return idx->object;
    }
    return nullptr;
}

[[nodiscard]] inline auto expr_get_index_expr(const ExprPtr& e) -> ExprPtr {
    if (e) {
        if (const auto* idx = std::get_if<IndexExpr>(&e->node)) return idx->index;
    }
    return nullptr;
}

[[nodiscard]] inline auto expr_get_slice_object(const ExprPtr& e) -> ExprPtr {
    if (e) {
        if (const auto* sl = std::get_if<SliceExpr>(&e->node)) return sl->object;
    }
    return nullptr;
}

[[nodiscard]] inline auto expr_get_slice_start(const ExprPtr& e) -> ExprPtr {
    if (e) {
        if (const auto* sl = std::get_if<SliceExpr>(&e->node)) return sl->start;
    }
    return nullptr;
}

[[nodiscard]] inline auto expr_get_slice_end(const ExprPtr& e) -> ExprPtr {
    if (e) {
        if (const auto* sl = std::get_if<SliceExpr>(&e->node)) return sl->end;
    }
    return nullptr;
}

[[nodiscard]] inline auto expr_get_try_inner(const ExprPtr& e) -> ExprPtr {
    if (e) {
        if (const auto* tr = std::get_if<TryExpr>(&e->node)) return tr->inner;
    }
    return nullptr;
}

[[nodiscard]] inline auto expr_get_or_inner(const ExprPtr& e) -> ExprPtr {
    if (e) {
        if (const auto* or_ex = std::get_if<OrExpr>(&e->node)) return or_ex->inner;
    }
    return nullptr;
}

[[nodiscard]] inline auto expr_get_or_err_var(const ExprPtr& e) -> std::string {
    if (e) {
        if (const auto* or_ex = std::get_if<OrExpr>(&e->node)) return or_ex->err_var;
    }
    return {};
}

[[nodiscard]] inline auto expr_get_or_fallback(const ExprPtr& e) -> ExprPtr {
    if (e) {
        if (const auto* or_ex = std::get_if<OrExpr>(&e->node)) return or_ex->fallback_val;
    }
    return nullptr;
}

[[nodiscard]] inline auto expr_get_or_handler_body(const ExprPtr& e) -> std::vector<StmtPtr> {
    if (e) {
        if (const auto* or_ex = std::get_if<OrExpr>(&e->node)) return or_ex->handler_body;
    }
    return {};
}

[[nodiscard]] inline auto expr_get_struct_lit_field_count(const ExprPtr& e) -> std::size_t {
    if (e) {
        if (const auto* st = std::get_if<StructLitExpr>(&e->node)) return st->fields.size();
    }
    return 0;
}

[[nodiscard]] inline auto expr_get_struct_lit_field_name(const ExprPtr& e, std::size_t i) -> std::string {
    if (e) {
        if (const auto* st = std::get_if<StructLitExpr>(&e->node)) {
            if (i < st->fields.size()) return st->fields[i].name;
        }
    }
    return {};
}

[[nodiscard]] inline auto expr_get_struct_lit_field_value(const ExprPtr& e, std::size_t i) -> ExprPtr {
    if (e) {
        if (const auto* st = std::get_if<StructLitExpr>(&e->node)) {
            if (i < st->fields.size()) return st->fields[i].value;
        }
    }
    return nullptr;
}

[[nodiscard]] inline auto expr_get_array_lit_elements(const ExprPtr& e) -> std::vector<ExprPtr> {
    if (e) {
        if (const auto* arr = std::get_if<ArrayLitExpr>(&e->node)) return arr->elements;
    }
    return {};
}

[[nodiscard]] inline auto expr_get_tuple_elements(const ExprPtr& e) -> std::vector<ExprPtr> {
    if (e) {
        if (const auto* tup = std::get_if<TupleExpr>(&e->node)) return tup->elements;
    }
    return {};
}

[[nodiscard]] inline auto expr_get_closure_body(const ExprPtr& e) -> std::vector<StmtPtr> {
    if (e) {
        if (const auto* cl = std::get_if<ClosureExpr>(&e->node)) return cl->body;
    }
    return {};
}

[[nodiscard]] inline auto expr_get_call_callee(const ExprPtr& e) -> ExprPtr {
    if (e) {
        if (const auto* call = std::get_if<CallExpr>(&e->node)) return call->callee;
    }
    return nullptr;
}

[[nodiscard]] inline auto expr_get_call_arg_count(const ExprPtr& e) -> std::size_t {
    if (e) {
        if (const auto* call = std::get_if<CallExpr>(&e->node)) return call->args.size();
    }
    return 0;
}

[[nodiscard]] inline auto expr_get_call_arg_value(const ExprPtr& e, std::size_t i) -> ExprPtr {
    if (e) {
        if (const auto* call = std::get_if<CallExpr>(&e->node)) {
            if (i < call->args.size()) return call->args[i].value;
        }
    }
    return nullptr;
}

[[nodiscard]] inline auto expr_get_call_arg_intent(const ExprPtr& e, std::size_t i) -> std::string {
    if (e) {
        if (const auto* call = std::get_if<CallExpr>(&e->node)) {
            if (i < call->args.size()) return call->args[i].intent;
        }
    }
    return {};
}

[[nodiscard]] inline auto expr_get_call_arg_line(const ExprPtr& e, std::size_t i) -> int {
    if (e) {
        if (const auto* call = std::get_if<CallExpr>(&e->node)) {
            if (i < call->args.size()) return call->args[i].line;
        }
    }
    return 0;
}

[[nodiscard]] inline auto expr_get_call_arg_col(const ExprPtr& e, std::size_t i) -> int {
    if (e) {
        if (const auto* call = std::get_if<CallExpr>(&e->node)) {
            if (i < call->args.size()) return call->args[i].col;
        }
    }
    return 0;
}

[[nodiscard]] inline auto expr_has_call_block_arg(const ExprPtr& e) -> bool {
    if (e) {
        if (const auto* call = std::get_if<CallExpr>(&e->node)) return call->block_arg.has_value();
    }
    return false;
}

[[nodiscard]] inline auto expr_get_call_block_arg_body(const ExprPtr& e) -> std::vector<StmtPtr> {
    if (e) {
        if (const auto* call = std::get_if<CallExpr>(&e->node)) {
            if (call->block_arg) return call->block_arg->body;
        }
    }
    return {};
}

// --- Statement Extractors ---
[[nodiscard]] inline auto stmt_get_var_name(const StmtPtr& s) -> std::string {
    if (s) {
        if (const auto* vd = std::get_if<VarDeclStmt>(&s->node)) return vd->name;
    }
    return {};
}

[[nodiscard]] inline auto stmt_get_var_is_mut(const StmtPtr& s) -> bool {
    if (s) {
        if (const auto* vd = std::get_if<VarDeclStmt>(&s->node)) return vd->is_mut;
    }
    return false;
}

[[nodiscard]] inline auto stmt_get_var_was_var(const StmtPtr& s) -> bool {
    if (s) {
        if (const auto* vd = std::get_if<VarDeclStmt>(&s->node)) return vd->was_var;
    }
    return false;
}

[[nodiscard]] inline auto stmt_get_var_col(const StmtPtr& s) -> int {
    if (s) {
        if (const auto* vd = std::get_if<VarDeclStmt>(&s->node)) return vd->col;
    }
    return 0;
}

[[nodiscard]] inline auto stmt_get_var_init(const StmtPtr& s) -> ExprPtr {
    if (s) {
        if (const auto* vd = std::get_if<VarDeclStmt>(&s->node)) return vd->init;
    }
    return nullptr;
}

[[nodiscard]] inline auto stmt_get_destruct_var_bindings(const StmtPtr& s) -> std::vector<std::pair<std::string, bool>> {
    if (s) {
        if (const auto* dvs = std::get_if<DestructureVarDeclStmt>(&s->node)) return dvs->bindings;
    }
    return {};
}

[[nodiscard]] inline auto stmt_get_destruct_var_init(const StmtPtr& s) -> ExprPtr {
    if (s) {
        if (const auto* dvs = std::get_if<DestructureVarDeclStmt>(&s->node)) return dvs->init;
    }
    return nullptr;
}

[[nodiscard]] inline auto stmt_get_assign_target(const StmtPtr& s) -> ExprPtr {
    if (s) {
        if (const auto* as = std::get_if<AssignStmt>(&s->node)) return as->target;
    }
    return nullptr;
}

[[nodiscard]] inline auto stmt_get_assign_value(const StmtPtr& s) -> ExprPtr {
    if (s) {
        if (const auto* as = std::get_if<AssignStmt>(&s->node)) return as->value;
    }
    return nullptr;
}

[[nodiscard]] inline auto stmt_get_destruct_assign_targets(const StmtPtr& s) -> std::vector<ExprPtr> {
    if (s) {
        if (const auto* das = std::get_if<DestructureAssignStmt>(&s->node)) return das->targets;
    }
    return {};
}

[[nodiscard]] inline auto stmt_get_destruct_assign_value(const StmtPtr& s) -> ExprPtr {
    if (s) {
        if (const auto* das = std::get_if<DestructureAssignStmt>(&s->node)) return das->value;
    }
    return nullptr;
}

[[nodiscard]] inline auto stmt_get_defer_call(const StmtPtr& s) -> ExprPtr {
    if (s) {
        if (const auto* df = std::get_if<DeferStmt>(&s->node)) return df->call;
    }
    return nullptr;
}

[[nodiscard]] inline auto stmt_get_return_value(const StmtPtr& s) -> ExprPtr {
    if (s) {
        if (const auto* rt = std::get_if<ReturnStmt>(&s->node)) return rt->value;
    }
    return nullptr;
}

[[nodiscard]] inline auto stmt_get_if_cond(const StmtPtr& s) -> ExprPtr {
    if (s) {
        if (const auto* ifs = std::get_if<IfStmt>(&s->node)) return ifs->cond;
    }
    return nullptr;
}

[[nodiscard]] inline auto stmt_get_if_then_body(const StmtPtr& s) -> std::vector<StmtPtr> {
    if (s) {
        if (const auto* ifs = std::get_if<IfStmt>(&s->node)) return ifs->then_body;
    }
    return {};
}

[[nodiscard]] inline auto stmt_get_if_else_body(const StmtPtr& s) -> std::vector<StmtPtr> {
    if (s) {
        if (const auto* ifs = std::get_if<IfStmt>(&s->node)) return ifs->else_body;
    }
    return {};
}

[[nodiscard]] inline auto stmt_get_for_iter(const StmtPtr& s) -> ExprPtr {
    if (s) {
        if (const auto* fs = std::get_if<ForStmt>(&s->node)) return fs->iter_expr;
    }
    return nullptr;
}

[[nodiscard]] inline auto stmt_get_for_body(const StmtPtr& s) -> std::vector<StmtPtr> {
    if (s) {
        if (const auto* fs = std::get_if<ForStmt>(&s->node)) return fs->body;
    }
    return {};
}

[[nodiscard]] inline auto stmt_get_match_target(const StmtPtr& s) -> ExprPtr {
    if (s) {
        if (const auto* ms = std::get_if<MatchStmt>(&s->node)) return ms->target;
    }
    return nullptr;
}

[[nodiscard]] inline auto stmt_get_match_is_move(const StmtPtr& s) -> bool {
    if (s) {
        if (const auto* ms = std::get_if<MatchStmt>(&s->node)) return ms->is_move;
    }
    return false;
}

[[nodiscard]] inline auto stmt_get_match_col(const StmtPtr& s) -> int {
    if (s) {
        if (const auto* ms = std::get_if<MatchStmt>(&s->node)) return ms->col;
    }
    return 0;
}

[[nodiscard]] inline auto stmt_get_match_arm_count(const StmtPtr& s) -> std::size_t {
    if (s) {
        if (const auto* ms = std::get_if<MatchStmt>(&s->node)) return ms->arms.size();
    }
    return 0;
}

[[nodiscard]] inline auto stmt_get_match_arm_variant(const StmtPtr& s, std::size_t i) -> std::string {
    if (s) {
        if (const auto* ms = std::get_if<MatchStmt>(&s->node)) {
            if (i < ms->arms.size()) return ms->arms[i].variant_name;
        }
    }
    return {};
}

[[nodiscard]] inline auto stmt_get_match_arm_enum_prefix(const StmtPtr& s, std::size_t i) -> std::string {
    if (s) {
        if (const auto* ms = std::get_if<MatchStmt>(&s->node)) {
            if (i < ms->arms.size()) return ms->arms[i].enum_prefix;
        }
    }
    return {};
}

[[nodiscard]] inline auto stmt_get_match_arm_is_slice(const StmtPtr& s, std::size_t i) -> bool {
    if (s) {
        if (const auto* ms = std::get_if<MatchStmt>(&s->node)) {
            if (i < ms->arms.size()) return ms->arms[i].is_slice_pattern;
        }
    }
    return false;
}

[[nodiscard]] inline auto stmt_get_match_arm_body(const StmtPtr& s, std::size_t i) -> std::vector<StmtPtr> {
    if (s) {
        if (const auto* ms = std::get_if<MatchStmt>(&s->node)) {
            if (i < ms->arms.size()) return ms->arms[i].body;
        }
    }
    return {};
}

[[nodiscard]] inline auto stmt_get_async_workers(const StmtPtr& s) -> ExprPtr {
    if (s) {
        if (const auto* sc = std::get_if<AsyncScopeStmt>(&s->node)) return sc->workers;
    }
    return nullptr;
}

[[nodiscard]] inline auto stmt_get_async_body(const StmtPtr& s) -> std::vector<StmtPtr> {
    if (s) {
        if (const auto* sc = std::get_if<AsyncScopeStmt>(&s->node)) return sc->body;
    }
    return {};
}

[[nodiscard]] inline auto stmt_get_expr(const StmtPtr& s) -> ExprPtr {
    if (s) {
        if (const auto* es = std::get_if<ExprStmt>(&s->node)) return es->expr;
    }
    return nullptr;
}

// --- Zero-Copy Function & Test AST Accessors ---
[[nodiscard]] inline auto ast_fn_has_receiver(const FileAST& ast, std::size_t fvi) -> bool {
    return ast.functions[fvi].receiver.has_value();
}
[[nodiscard]] inline auto ast_fn_get_receiver_name(const FileAST& ast, std::size_t fvi) -> std::string {
    return ast.functions[fvi].receiver ? ast.functions[fvi].receiver->name : "";
}
[[nodiscard]] inline auto ast_fn_get_receiver_intent(const FileAST& ast, std::size_t fvi) -> std::string {
    return ast.functions[fvi].receiver ? ast.functions[fvi].receiver->intent : "";
}
[[nodiscard]] inline auto ast_fn_get_param_count(const FileAST& ast, std::size_t fvi) -> std::size_t {
    return ast.functions[fvi].params.size();
}
[[nodiscard]] inline auto ast_fn_get_param_name(const FileAST& ast, std::size_t fvi, std::size_t pi) -> std::string {
    return ast.functions[fvi].params[pi].name;
}
[[nodiscard]] inline auto ast_fn_get_param_intent(const FileAST& ast, std::size_t fvi, std::size_t pi) -> std::string {
    return ast.functions[fvi].params[pi].intent;
}
[[nodiscard]] inline auto ast_fn_get_param_line(const FileAST& ast, std::size_t fvi, std::size_t pi) -> int {
    return ast.functions[fvi].params[pi].line;
}
[[nodiscard]] inline auto ast_fn_get_param_col(const FileAST& ast, std::size_t fvi, std::size_t pi) -> int {
    return ast.functions[fvi].params[pi].col;
}
[[nodiscard]] inline auto ast_fn_get_body_count(const FileAST& ast, std::size_t fvi) -> std::size_t {
    return ast.functions[fvi].body.size();
}
[[nodiscard]] inline auto ast_fn_get_body_stmt(const FileAST& ast, std::size_t fvi, std::size_t sbi) -> StmtPtr {
    return ast.functions[fvi].body[sbi];
}
[[nodiscard]] inline auto ast_fn_get_key(const FileAST& ast, std::size_t fvi) -> std::string {
    const auto& fn = ast.functions[fvi];
    return fn.receiver ? (fn.receiver->type.name + "__" + fn.name) : fn.name;
}

[[nodiscard]] inline auto ast_test_get_body_count(const FileAST& ast, std::size_t ti) -> std::size_t {
    return ast.tests[ti].body.size();
}
[[nodiscard]] inline auto ast_test_get_body_stmt(const FileAST& ast, std::size_t ti, std::size_t sbi) -> StmtPtr {
    return ast.tests[ti].body[sbi];
}

// --- Signature, Method & Receiver Lookup ---
[[nodiscard]] inline auto fn_has_receiver(const FnDecl& fn) -> bool {
    return fn.receiver.has_value();
}

[[nodiscard]] inline auto fn_get_receiver(const FnDecl& fn) -> ReceiverDecl {
    return fn.receiver ? *fn.receiver : ReceiverDecl{};
}

[[nodiscard]] inline auto lookup_fn_sig(const CheckedFile& cf, const std::string& name) -> std::pair<FnDecl, bool> {
    auto it = cf.fn_sigs.find(name);
    if (it != cf.fn_sigs.end()) return {it->second, true};
    return {{}, false};
}

[[nodiscard]] inline auto lookup_method_by_field(const CheckedFile& cf, const std::string& field) -> std::pair<FnDecl, bool> {
    auto owner_it = cf.method_owner.find(field);
    if (owner_it != cf.method_owner.end()) {
        std::string mkey = owner_it->second + "::" + field;
        auto mit = cf.methods.find(mkey);
        if (mit != cf.methods.end()) return {mit->second, true};
    }
    return {{}, false};
}

// --- Tag Extraction Helper ---
[[nodiscard]] inline auto extract_struct_field_tags(const std::string& field_name, const TypeRef& field_type, const std::string& raw_tag)
    -> std::vector<StructTagInfo>
{
    if (raw_tag.empty()) return {};
    static const std::regex tag_re(R"re((\w+):"([^"]*)")re");
    StructTagInfo info;
    info.field_name = field_name;
    info.field_type = field_type;
    for (auto it = std::sregex_iterator(raw_tag.begin(), raw_tag.end(), tag_re); it != std::sregex_iterator(); ++it) {
        std::string key = (*it)[1].str();
        std::string val = (*it)[2].str();
        if (key == "cli") {
            auto comma = val.find(',');
            if (comma != std::string::npos) {
                info.cli_long = trim_str(std::string_view(val).substr(0, comma));
                info.cli_short = trim_str(std::string_view(val).substr(comma + 1));
            } else {
                info.cli_long = trim_str(val);
            }
        } else if (key == "help") {
            info.cli_help = val;
        } else if (key == "json") {
            auto comma = val.find(',');
            info.json_key = trim_str(comma != std::string::npos ? std::string_view(val).substr(0, comma) : std::string_view(val));
        } else if (key == "db") {
            info.db_column = trim_str(val);
        }
        info.custom_tags[key] = val;
    }
    return {std::move(info)};
}

// --- Map Operation Helpers ---
inline auto clone_str_int_map(const cfire::Map<std::string, int>& m) -> cfire::Map<std::string, int> {
    return m;
}

inline auto merge_post_branch_moves(
    const cfire::Map<std::string, int>& initial,
    const cfire::Map<std::string, int>& then_m,
    const cfire::Map<std::string, int>& else_m)
    -> cfire::Map<std::string, int>
{
    auto post = initial;
    for (const auto& [var, line] : then_m) {
        if (else_m.contains(var)) {
            auto it = else_m.get(var);
            post[var] = std::max(line, it ? *it : 0);
        }
    }
    return post;
}

inline auto find_unreassigned_loop_moves(
    const cfire::Map<std::string, int>& pre_loop,
    const cfire::Map<std::string, int>& post_loop)
    -> std::pair<std::string, int>
{
    for (const auto& [var, line] : post_loop) {
        if (!pre_loop.contains(var)) {
            return {var, line};
        }
    }
    return {"", 0};
}

inline void record_last_use_moves(
    CheckedFile& cf,
    const std::string& fn_key,
    const cfire::Map<std::string, int>& last_read)
{
    for (const auto& [p_name, ln] : last_read) {
        cf.last_use_move[fn_key + ":" + p_name] = ln;
    }
}

// --- Enum Lookup Helpers ---
[[nodiscard]] inline auto lookup_variant_to_enum(const CheckedFile& cf, const std::string& vname) -> std::string {
    auto it = cf.variant_to_enum.find(vname);
    if (it != cf.variant_to_enum.end()) return it->second;
    return {};
}

[[nodiscard]] inline auto lookup_enum(const CheckedFile& cf, const std::string& ename) -> std::pair<EnumDecl, bool> {
    auto it = cf.enums.find(ename);
    if (it != cf.enums.end()) return {it->second, true};
    return {{}, false};
}

[[nodiscard]] inline auto enum_contains_variant(const EnumDecl& en, const std::string& vname) -> bool {
    for (const auto& v : en.variants) {
        if (v.name == vname) return true;
    }
    return false;
}

inline void index_file_declarations(CheckedFile& cf, const FileAST& ast) {
    for (const auto& en : ast.enums) {
        cf.enums[en.name] = en;
        for (const auto& v : en.variants) {
            cf.variant_to_enum[v.name] = en.name;
        }
    }
    for (const auto& st : ast.structs) {
        std::vector<StructTagInfo> tags;
        for (const auto& fld : st.fields) {
            if (fld.raw_tag.empty()) continue;
            auto field_tags = extract_struct_field_tags(fld.name, fld.type, fld.raw_tag);
            tags.insert(tags.end(), field_tags.begin(), field_tags.end());
        }
        if (!tags.empty()) {
            cf.struct_tags[st.name] = std::move(tags);
        }
    }
    for (const auto& fn : ast.functions) {
        if (fn.receiver) {
            std::string key = fn.receiver->type.name + "::" + fn.name;
            cf.methods[key] = fn;
            cf.method_owner[fn.name] = fn.receiver->type.name;
        } else {
            cf.fn_sigs[fn.name] = fn;
        }
    }
}

inline void set_checked_file_ast(CheckedFile& cf, FileAST& ast) {
    cf.ast = std::move(ast);
}

class SemaFnChecker {
public:
    SemaFnChecker(std::string_view path, const FnDecl& fn, const CheckedFile& cf, std::vector<std::string>& warnings)
        : path_(path), cf_(cf), warnings_(warnings)
    {
        if (fn.receiver) {
            if (fn.receiver->intent == "mut") {
                mut_vars_.insert(fn.receiver->name);
            } else {
                imm_vars_.insert(fn.receiver->name);
            }
        }
        for (const auto& p : fn.params) {
            if (p.intent == "move") {
                move_params_.insert(p.name);
            }
            if (p.intent == "out") {
                warnings_.push_back(std::format(
                    "{}:{}:{}: warning: 'out' parameter is deprecated; use multi-return tuple '(T1, T2)' instead (cfire/DEPRECATED-OUT-PARAM)",
                    path_, p.line, p.col));
            }
            if (p.intent == "mut" || p.intent == "out") {
                mut_vars_.insert(p.name);
            } else {
                imm_vars_.insert(p.name);
            }
        }
    }

    std::unordered_map<std::string, int> last_read_line{};

    [[nodiscard]] auto record_var_read(const std::string& name, int line, int col)
        -> std::expected<void, std::string>
    {
        if (auto it = moved_at_line_.find(name); it != moved_at_line_.end()) {
            return std::unexpected(std::format(
                "{}:{}:{}: use of moved variable '{}' (previously moved at line {}) (cfire/USE-AFTER-MOVE)",
                path_, line, col, name, it->second));
        }
        if (move_params_.contains(name)) {
            last_read_line[name] = line;
        }
        return {};
    }

    struct PathInfo {
        std::string root{};
        std::string path{};
    };

    [[nodiscard]] static auto extract_path_info(const ExprPtr& e) -> std::optional<PathInfo> {
        if (!e) return std::nullopt;
        if (const auto* id = std::get_if<IdentExpr>(&e->node)) {
            return PathInfo{id->name, id->name};
        }
        if (const auto* mem = std::get_if<MemberExpr>(&e->node)) {
            if (mem->is_namespace) return std::nullopt;
            auto inner = extract_path_info(mem->object);
            if (!inner) return std::nullopt;
            return PathInfo{inner->root, inner->path + "." + mem->field};
        }
        if (const auto* idx = std::get_if<IndexExpr>(&e->node)) {
            auto inner = extract_path_info(idx->object);
            if (!inner) return std::nullopt;
            return PathInfo{inner->root, inner->path + "[*]"};
        }
        if (const auto* un = std::get_if<UnaryExpr>(&e->node)) {
            if (un->op == "&" || un->op == "*") {
                return extract_path_info(un->operand);
            }
        }
        return std::nullopt;
    }

    [[nodiscard]] static auto paths_conflict(std::string_view a, std::string_view b) -> bool {
        if (a == b) return true;
        if (a.starts_with(b)) {
            char next = a[b.size()];
            if (next == '.' || next == '[') return true;
        }
        if (b.starts_with(a)) {
            char next = b[a.size()];
            if (next == '.' || next == '[') return true;
        }
        return false;
    }

    [[nodiscard]] auto check_expr(const ExprPtr& e) -> std::expected<void, std::string> {
        if (!e) return {};
        static const std::regex interp_var_re(R"(\{([a-zA-Z_][a-zA-Z0-9_]*)\})");

        if (const auto* id = std::get_if<IdentExpr>(&e->node)) {
            return record_var_read(id->name, e->line, e->col);
        }
        if (const auto* lit = std::get_if<LiteralExpr>(&e->node)) {
            if (lit->kind == TokenKind::String && lit->value.contains('{')) {
                auto begin = std::sregex_iterator(lit->value.begin(), lit->value.end(), interp_var_re);
                for (auto it = begin; it != std::sregex_iterator(); ++it) {
                    if (auto r = record_var_read((*it)[1].str(), e->line, e->col); !r) return r;
                }
            }
            return {};
        }
        if (const auto* un = std::get_if<UnaryExpr>(&e->node)) {
            if (auto r = check_expr(un->operand); !r) return r;
            if (un->op == "move") {
                if (auto pi = extract_path_info(un->operand)) {
                    moved_at_line_[pi->root] = e->line;
                }
            }
            return {};
        }
        if (const auto* bin = std::get_if<BinaryExpr>(&e->node)) {
            if (auto r = check_expr(bin->left); !r) return r;
            return check_expr(bin->right);
        }
        if (const auto* mem = std::get_if<MemberExpr>(&e->node)) {
            return check_expr(mem->object);
        }
        if (const auto* idx = std::get_if<IndexExpr>(&e->node)) {
            if (auto r = check_expr(idx->object); !r) return r;
            return check_expr(idx->index);
        }
        if (const auto* sl = std::get_if<SliceExpr>(&e->node)) {
            if (auto r = check_expr(sl->object); !r) return r;
            if (sl->start) if (auto r = check_expr(sl->start); !r) return r;
            if (sl->end) if (auto r = check_expr(sl->end); !r) return r;
            return {};
        }
        if (const auto* tr = std::get_if<TryExpr>(&e->node)) {
            return check_expr(tr->inner);
        }
        if (const auto* or_ex = std::get_if<OrExpr>(&e->node)) {
            if (auto r = check_expr(or_ex->inner); !r) return r;
            if (or_ex->fallback_val) {
                if (auto r = check_expr(or_ex->fallback_val); !r) return r;
            }
            for (const auto& s : or_ex->handler_body) {
                if (auto r = check_stmt(s); !r) return r;
            }
            return {};
        }
        if (const auto* st = std::get_if<StructLitExpr>(&e->node)) {
            for (const auto& f : st->fields) {
                if (auto r = check_expr(f.value); !r) return r;
            }
            return {};
        }
        if (const auto* arr = std::get_if<ArrayLitExpr>(&e->node)) {
            for (const auto& el : arr->elements) {
                if (auto r = check_expr(el); !r) return r;
            }
            return {};
        }
        if (const auto* tup = std::get_if<TupleExpr>(&e->node)) {
            for (const auto& el : tup->elements) {
                if (auto r = check_expr(el); !r) return r;
            }
            return {};
        }
        if (const auto* cl = std::get_if<ClosureExpr>(&e->node)) {
            for (const auto& s : cl->body) {
                if (auto r = check_stmt(s); !r) return r;
            }
            return {};
        }
        if (const auto* call = std::get_if<CallExpr>(&e->node)) {
            if (auto r = check_expr(call->callee); !r) return r;
            const FnDecl* target_sig = nullptr;
            if (const auto* id = std::get_if<IdentExpr>(&call->callee->node)) {
                if (auto it = cf_.fn_sigs.find(id->name); it != cf_.fn_sigs.end()) {
                    target_sig = &it->second;
                }
            } else if (const auto* mem = std::get_if<MemberExpr>(&call->callee->node)) {
                if (auto owner_it = cf_.method_owner.find(mem->field); owner_it != cf_.method_owner.end()) {
                    std::string mkey = owner_it->second + "::" + mem->field;
                    if (auto mit = cf_.methods.find(mkey); mit != cf_.methods.end()) {
                        target_sig = &mit->second;
                        // Verify receiver mutability if method requires `mut` receiver!
                        if (target_sig->receiver && target_sig->receiver->intent == "mut") {
                            if (const auto* obj_id = std::get_if<IdentExpr>(&mem->object->node)) {
                                if (imm_vars_.contains(obj_id->name)) {
                                    return std::unexpected(std::format(
                                        "{}:{}:{}: cannot call mutating method '{}' on immutable variable '{}' (cfire/MUT-RECEIVER)",
                                        path_, e->line, e->col, mem->field, obj_id->name));
                                }
                            }
                        }
                    }
                }
            }
            struct SeenArgPath {
                PathInfo info;
                bool is_mut_or_out;
            };
            std::vector<SeenArgPath> seen_paths;

            if (const auto* mem = std::get_if<MemberExpr>(&call->callee->node)) {
                if (auto p = extract_path_info(mem->object)) {
                    bool is_mut = (target_sig && target_sig->receiver && target_sig->receiver->intent == "mut");
                    seen_paths.push_back({*p, is_mut});
                }
            }

            for (std::size_t i = 0; i < call->args.size(); ++i) {
                const auto& arg = call->args[i];
                if (arg.intent == "out") {
                    warnings_.push_back(std::format(
                        "{}:{}:{}: warning: 'out' call-site annotation is deprecated; use multi-return tuple destructuring instead (cfire/DEPRECATED-OUT-PARAM)",
                        path_, arg.line, arg.col));
                }
                if (auto r = check_expr(arg.value); !r) return r;
                if (target_sig && i < target_sig->params.size()) {
                    const auto& expected = target_sig->params[i].intent;
                    if ((expected == "mut" || expected == "out" || expected == "move") && arg.intent != expected) {
                        std::string_view rule = (expected == "out") ? "cfire/OUT-CALLSITE" : "cfire/MUT-CALLSITE";
                        return std::unexpected(std::format(
                            "{}:{}:{}: parameter '{}' of {} requires explicit '{}' annotation at call-site ({})",
                            path_, arg.line, arg.col, target_sig->params[i].name, target_sig->name, expected, rule));
                    }
                }
                if (auto pi = extract_path_info(arg.value)) {
                    bool is_mut_or_out = (arg.intent == "mut" || arg.intent == "out");
                    for (const auto& prev : seen_paths) {
                        if (prev.info.root == pi->root && (prev.is_mut_or_out || is_mut_or_out)) {
                            if (paths_conflict(prev.info.path, pi->path)) {
                                std::string entity = (pi->path == pi->root && prev.info.path == prev.info.root)
                                    ? std::format("variable '{}'", pi->root)
                                    : std::format("projection '{}'", pi->path);
                                return std::unexpected(std::format(
                                    "{}:{}:{}: cannot alias {} with a mut/out parameter in the same call (cfire/MUT-ALIAS)",
                                    path_, arg.line, arg.col, entity));
                            }
                        }
                    }
                    seen_paths.push_back({*pi, is_mut_or_out});

                    if (is_mut_or_out) {
                        if (imm_vars_.contains(pi->root)) {
                            std::string entity = (pi->path == pi->root) ? pi->root : pi->path;
                            return std::unexpected(std::format(
                                "{}:{}:{}: cannot pass immutable variable '{}' as '{}' argument (cfire/IMMUTABLE-MUT-ARG)",
                                path_, arg.line, arg.col, entity, arg.intent));
                        }
                    }
                    if (arg.intent == "move") {
                        moved_at_line_[pi->root] = arg.line;
                    }
                }
            }
            if (call->block_arg) {
                for (const auto& s : call->block_arg->body) {
                    if (auto r = check_stmt(s); !r) return r;
                }
            }
            return {};
        }
        return {};
    }

    [[nodiscard]] static auto stmt_diverges(const StmtPtr& s) -> bool {
        if (!s) return false;
        if (std::holds_alternative<ReturnStmt>(s->node)) return true;
        if (const auto* es = std::get_if<ExprStmt>(&s->node)) {
            if (const auto* call = std::get_if<CallExpr>(&es->expr->node)) {
                if (const auto* id = std::get_if<IdentExpr>(&call->callee->node)) {
                    if (id->name == "panic" || id->name == "exit") return true;
                }
            }
        }
        if (const auto* ifs = std::get_if<IfStmt>(&s->node)) {
            if (!ifs->else_body.empty() && block_diverges(ifs->then_body) && block_diverges(ifs->else_body)) {
                return true;
            }
        }
        return false;
    }

    [[nodiscard]] static auto block_diverges(const std::vector<StmtPtr>& body) -> bool {
        for (const auto& s : body) {
            if (stmt_diverges(s)) return true;
        }
        return false;
    }

    [[nodiscard]] auto check_stmt(const StmtPtr& s) -> std::expected<void, std::string> {
        if (!s) return {};
        if (const auto* vd = std::get_if<VarDeclStmt>(&s->node)) {
            if (vd->was_var) {
                warnings_.push_back(std::format(
                    "{}:{}:{}: warning: 'var' is deprecated; use 'x := val' or 'mut x: T = val' instead (cfire/DEPRECATED-VAR)",
                    path_, vd->line, vd->col));
            }
            if (vd->is_mut) {
                imm_vars_.erase(vd->name);
                mut_vars_.insert(vd->name);
            } else {
                mut_vars_.erase(vd->name);
                imm_vars_.insert(vd->name);
            }
            if (vd->init) {
                if (const auto* or_ex = std::get_if<OrExpr>(&vd->init->node); or_ex && !or_ex->handler_body.empty()) {
                    if (!block_diverges(or_ex->handler_body)) {
                        return std::unexpected(std::format(
                            "{}:{}:{}: block 'or {} {{ ... }}' must definitely diverge with return or panic, otherwise unwrapping empty value causes undefined behavior (cfire/NON-DIVERGING-OR-BLOCK)",
                            path_, vd->line, vd->col, or_ex->err_var.empty() ? "err" : or_ex->err_var));
                    }
                }
            }
            return check_expr(vd->init);
        }
        if (const auto* dvs = std::get_if<DestructureVarDeclStmt>(&s->node)) {
            for (const auto& [name, is_mut] : dvs->bindings) {
                if (is_mut) {
                    mut_vars_.insert(name);
                } else {
                    imm_vars_.insert(name);
                }
            }
            return check_expr(dvs->init);
        }
        if (const auto* as = std::get_if<AssignStmt>(&s->node)) {
            if (as->value) {
                if (const auto* or_ex = std::get_if<OrExpr>(&as->value->node); or_ex && !or_ex->handler_body.empty()) {
                    if (!block_diverges(or_ex->handler_body)) {
                        return std::unexpected(std::format(
                            "{}:{}:{}: assignment from 'or {} {{ ... }}' must definitely diverge with return or panic (cfire/NON-DIVERGING-OR-BLOCK)",
                            path_, as->line, 1, or_ex->err_var.empty() ? "err" : or_ex->err_var));
                    }
                }
            }
            if (auto r = check_expr(as->value); !r) return r;
            if (const auto* id = std::get_if<IdentExpr>(&as->target->node)) {
                if (imm_vars_.contains(id->name)) {
                    return std::unexpected(std::format(
                        "{}:{}:{}: cannot assign to immutable variable '{}' (cfire/IMMUTABLE-ASSIGN)",
                        path_, as->line, 1, id->name));
                }
                moved_at_line_.erase(id->name);
            } else {
                if (auto r = check_expr(as->target); !r) return r;
            }
            return {};
        }
        if (const auto* das = std::get_if<DestructureAssignStmt>(&s->node)) {
            if (auto r = check_expr(das->value); !r) return r;
            for (const auto& targ : das->targets) {
                if (const auto* id = std::get_if<IdentExpr>(&targ->node)) {
                    if (imm_vars_.contains(id->name)) {
                        return std::unexpected(std::format(
                            "{}:{}:{}: cannot assign to immutable variable '{}' (cfire/IMMUTABLE-ASSIGN)",
                            path_, das->line, 1, id->name));
                    }
                    moved_at_line_.erase(id->name);
                } else {
                    if (auto r = check_expr(targ); !r) return r;
                }
            }
            return {};
        }
        if (const auto* df = std::get_if<DeferStmt>(&s->node)) {
            return check_expr(df->call);
        }
        if (const auto* rt = std::get_if<ReturnStmt>(&s->node)) {
            return check_expr(rt->value);
        }
        if (const auto* ifs = std::get_if<IfStmt>(&s->node)) {
            if (auto r = check_expr(ifs->cond); !r) return r;
            auto initial_moves = moved_at_line_;

            for (const auto& b : ifs->then_body) if (auto r = check_stmt(b); !r) return r;
            auto then_moves = moved_at_line_;

            moved_at_line_ = initial_moves;
            for (const auto& b : ifs->else_body) if (auto r = check_stmt(b); !r) return r;
            auto else_moves = moved_at_line_;

            if (ifs->else_body.empty()) {
                moved_at_line_ = initial_moves;
            } else {
                if (block_diverges(ifs->then_body)) {
                    moved_at_line_ = else_moves;
                } else if (block_diverges(ifs->else_body)) {
                    moved_at_line_ = then_moves;
                } else {
                    std::unordered_map<std::string, int> post_moves = initial_moves;
                    for (const auto& [var, line] : then_moves) {
                        if (else_moves.contains(var)) {
                            post_moves[var] = std::max(line, else_moves[var]);
                        }
                    }
                    moved_at_line_ = std::move(post_moves);
                }
            }
            return {};
        }
        if (const auto* fs = std::get_if<ForStmt>(&s->node)) {
            if (auto r = check_expr(fs->iter_expr); !r) return r;
            auto pre_loop_moves = moved_at_line_;
            for (const auto& b : fs->body) if (auto r = check_stmt(b); !r) return r;
            for (const auto& [var, line] : moved_at_line_) {
                if (!pre_loop_moves.contains(var)) {
                    return std::unexpected(std::format(
                        "{}:{}:{}: variable '{}' moved inside loop body without reassignment before next iteration (cfire/USE-AFTER-MOVE)",
                        path_, line, 1, var));
                }
            }
            return {};
        }
        if (const auto* ms = std::get_if<MatchStmt>(&s->node)) {
            if (auto r = check_expr(ms->target); !r) return r;
            if (ms->is_move && ms->target) {
                if (auto pi = extract_path_info(ms->target)) {
                    moved_at_line_[pi->root] = ms->line;
                }
            }
            bool is_slice_match = false;
            for (const auto& a : ms->arms) {
                if (a.is_slice_pattern) {
                    is_slice_match = true;
                    break;
                }
            }
            if (is_slice_match) {
                for (const auto& arm : ms->arms) {
                    for (const auto& b : arm.body) {
                        if (auto r = check_stmt(b); !r) return r;
                    }
                }
                return {};
            }

            std::string enum_name;
            std::unordered_set<std::string> covered;
            bool has_wildcard = false;

            for (const auto& arm : ms->arms) {
                if (arm.variant_name == "_") {
                    has_wildcard = true;
                } else {
                    covered.insert(arm.variant_name);
                    if (!arm.enum_prefix.empty()) {
                        enum_name = arm.enum_prefix;
                    } else if (auto vit = cf_.variant_to_enum.find(arm.variant_name); vit != cf_.variant_to_enum.end()) {
                        enum_name = vit->second;
                    }
                }
                for (const auto& b : arm.body) {
                    if (auto r = check_stmt(b); !r) return r;
                }
            }

            // Exhaustiveness check against known EnumDecl
            if (!has_wildcard && !enum_name.empty()) {
                if (auto eit = cf_.enums.find(enum_name); eit != cf_.enums.end()) {
                    for (const auto& v : eit->second.variants) {
                        if (!covered.contains(v.name)) {
                            return std::unexpected(std::format(
                                "{}:{}:{}: non-exhaustive match on enum '{}': missing variant '{}' (cfire/NON-EXHAUSTIVE-MATCH)",
                                path_, ms->line, ms->col, enum_name, v.name));
                        }
                    }
                }
            }
            return {};
        }
        if (const auto* sc = std::get_if<AsyncScopeStmt>(&s->node)) {
            if (auto r = check_expr(sc->workers); !r) return r;
            for (const auto& b : sc->body) if (auto r = check_stmt(b); !r) return r;
            return {};
        }
        if (const auto* es = std::get_if<ExprStmt>(&s->node)) {
            return check_expr(es->expr);
        }
        return {};
    }

private:
    std::string_view path_;
    const CheckedFile& cf_;
    std::vector<std::string>& warnings_;
    std::unordered_map<std::string, int> moved_at_line_{};
    std::unordered_set<std::string> move_params_{};
    std::unordered_set<std::string> mut_vars_{};
    std::unordered_set<std::string> imm_vars_{};
};

[[nodiscard]] inline auto sema_extract_path_info(const ExprPtr& e) -> std::pair<std::string, std::string> {
    auto res = SemaFnChecker::extract_path_info(e);
    if (res) return {res->root, res->path};
    return {"", ""};
}

[[nodiscard]] inline auto sema_block_diverges(const std::vector<StmtPtr>& body) -> bool {
    return SemaFnChecker::block_diverges(body);
}

[[nodiscard]] inline auto analyze_cxx(FileAST ast) -> std::expected<CheckedFile, std::string> {
    CheckedFile cf;
    static const std::regex tag_re(R"re((\w+):"([^"]*)")re");

    // 0. Check Global / Package Variables
    for (const auto& gv : ast.global_vars) {
        if (gv.was_var) {
            cf.warnings.push_back(std::format(
                "{}:{}:1: warning: 'var' is deprecated; use 'x := val' or 'mut x: T = val' instead (cfire/DEPRECATED-VAR)",
                ast.path, gv.line));
        }
    }

    // 1. Index Enums and their Variants
    for (const auto& en : ast.enums) {
        cf.enums[en.name] = en;
        for (const auto& v : en.variants) {
            cf.variant_to_enum[v.name] = en.name;
        }
    }

    // 2. Index Struct Field Tags
    for (const auto& st : ast.structs) {
        std::vector<StructTagInfo> tags;
        for (const auto& fld : st.fields) {
            if (fld.raw_tag.empty()) continue;
            StructTagInfo info;
            info.field_name = fld.name;
            info.field_type = fld.type;

            auto begin = std::sregex_iterator(fld.raw_tag.begin(), fld.raw_tag.end(), tag_re);
            for (auto it = begin; it != std::sregex_iterator(); ++it) {
                std::string key = (*it)[1].str();
                std::string val = (*it)[2].str();
                if (key == "cli") {
                    auto comma = val.find(',');
                    if (comma != std::string::npos) {
                        info.cli_long = trim_str(std::string_view(val).substr(0, comma));
                        info.cli_short = trim_str(std::string_view(val).substr(comma + 1));
                    } else {
                        info.cli_long = trim_str(val);
                    }
                } else if (key == "help") {
                    info.cli_help = val;
                } else if (key == "json") {
                    auto comma = val.find(',');
                    info.json_key = trim_str(comma != std::string::npos ? std::string_view(val).substr(0, comma) : std::string_view(val));
                } else if (key == "db") {
                    info.db_column = trim_str(val);
                }
                info.custom_tags[key] = val;
            }
            tags.push_back(std::move(info));
        }
        if (!tags.empty()) {
            cf.struct_tags[st.name] = std::move(tags);
        }
    }

    // 3. Index Functions & Methods
    for (const auto& fn : ast.functions) {
        if (fn.receiver) {
            std::string key = fn.receiver->type.name + "::" + fn.name;
            cf.methods[key] = fn;
            cf.method_owner[fn.name] = fn.receiver->type.name;
        } else {
            cf.fn_sigs[fn.name] = fn;
        }
    }

    // 4. Validate Function & Method Bodies
    for (const auto& fn : ast.functions) {
        SemaFnChecker checker(ast.path, fn, cf, cf.warnings);
        for (const auto& st : fn.body) {
            if (auto r = checker.check_stmt(st); !r) return std::unexpected(r.error());
        }
        std::string fn_key = fn.receiver ? (fn.receiver->type.name + "__" + fn.name) : fn.name;
        for (const auto& [p_name, ln] : checker.last_read_line) {
            cf.last_use_move[fn_key + ":" + p_name] = ln;
        }
    }

    // 5. Validate Test Declarations
    for (std::size_t idx = 0; idx < ast.tests.size(); ++idx) {
        FnDecl dummy_fn;
        dummy_fn.name = std::format("__cfire_test_{}", idx);
        SemaFnChecker checker(ast.path, dummy_fn, cf, cf.warnings);
        for (const auto& st : ast.tests[idx].body) {
            if (auto r = checker.check_stmt(st); !r) return std::unexpected(r.error());
        }
    }

    cf.ast = std::move(ast);
    return cf;
}

} // namespace cfire::frontend

#include "bootstrap/sema.generated.hpp"

namespace cfire::frontend {

[[nodiscard]] inline auto selfhost_analyze(FileAST ast)
    -> std::expected<CheckedFile, std::string>
{
    auto res = ::selfhost_analyze_file(ast);
    if (res.has_error) {
        return std::unexpected(res.error);
    }
    return std::move(res.cf);
}

[[nodiscard]] inline auto analyze(FileAST ast)
    -> std::expected<CheckedFile, std::string>
{
    return selfhost_analyze(std::move(ast));
}

} // namespace cfire::frontend

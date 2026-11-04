#pragma once
// src/sema.hpp — C++23 Semantic Analyzer, Intent Verifier, Exhaustive Match & Struct Tag Extractor

#include "ast.hpp"
#include "diagnostics.hpp"
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
    std::unordered_map<std::string, std::string> method_owner{}; // "method" -> "Type" (primary owner for single-owner methods)
    std::unordered_map<std::string, std::unordered_set<std::string>> method_owners{}; // "method" -> set of "Type"s
    std::unordered_map<std::string, EnumDecl> enums{};     // "EnumName" -> EnumDecl
    std::unordered_map<std::string, std::string> variant_to_enum{}; // "VariantName" -> "EnumName"
    std::unordered_map<std::string, std::vector<StructTagInfo>> struct_tags{};
    std::unordered_map<std::string, int> last_use_move{}; // "fnName:varName" -> line
    std::unordered_map<std::string, std::vector<std::string>> cpp_fn_params{}; // C/C++ fn -> param QualTypes
    std::vector<std::string> warnings{};
    std::vector<std::string> errors{};
};

[[nodiscard]] inline auto trim_str(std::string_view sv) -> std::string {
    while (!sv.empty() && std::isspace(static_cast<unsigned char>(sv.front()))) sv.remove_prefix(1);
    while (!sv.empty() && std::isspace(static_cast<unsigned char>(sv.back()))) sv.remove_suffix(1);
    return std::string(sv);
}


class SemaFnChecker {
public:
    enum class NullState { NonNull, MaybeNull, Null };

    struct InvalidatedLoan {
        std::string reason{};
        int line = 1;
    };

    SemaFnChecker(std::string_view path, const FnDecl& fn, const CheckedFile& cf,
                  std::vector<std::string>& warnings, std::vector<std::string>& errors, bool resilient = false)
        : path_(path), cf_(cf), warnings_(warnings), errors_(errors), resilient_(resilient)
    {
        if (fn.receiver) {
            var_scope_depth_[fn.receiver->name] = 0;
            if (fn.receiver->intent == "mut") {
                mut_vars_.insert(fn.receiver->name);
            } else {
                imm_vars_.insert(fn.receiver->name);
            }
            if (fn.receiver->type.is_map) {
                map_vars_.insert(fn.receiver->name);
            }
            if (fn.receiver->type.is_array && !fn.receiver->type.array_len.empty()) {
                if (auto n = cfire::detail::parse_int<std::size_t>(fn.receiver->type.array_len)) {
                    fixed_array_lens_[fn.receiver->name] = *n;
                }
            }
        }
        for (const auto& p : fn.params) {
            var_scope_depth_[p.name] = 0;
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
            if (p.type.is_map) {
                map_vars_.insert(p.name);
            }
            if (p.type.is_array && !p.type.array_len.empty()) {
                if (auto n = cfire::detail::parse_int<std::size_t>(p.type.array_len)) {
                    fixed_array_lens_[p.name] = *n;
                }
            }
            if (p.type.is_ptr || p.type.is_mut_ptr || !p.type.smart_wrap.empty()) {
                ptr_or_smart_vars_.insert(p.name);
                ptr_null_state_[p.name] = p.type.is_optional ? NullState::MaybeNull : NullState::NonNull;
            }
        }
    }

    SemaFnChecker(std::string_view path, const FnDecl& fn, const CheckedFile& cf, std::vector<std::string>& warnings)
        : SemaFnChecker(path, fn, cf, warnings, fallback_errors_, false) {}

    std::unordered_map<std::string, int> last_read_line{};

    [[nodiscard]] auto emit_error(std::string msg) -> std::expected<void, std::string> {
        if (resilient_) {
            errors_.push_back(std::move(msg));
            return {};
        }
        return std::unexpected(std::move(msg));
    }

    void invalidate_loans_for_owner(const std::string& owner, std::string_view reason, int line) {
        for (const auto& [borrower, orig] : borrow_origin_) {
            if (orig == owner) {
                invalidated_loans_[borrower] = InvalidatedLoan{std::string(reason), line};
            }
        }
    }

    [[nodiscard]] auto check_not_iterated_collection(const std::string& root, int line, int col)
        -> std::expected<void, std::string>
    {
        if (active_loop_collections_.contains(root)) {
            return emit_error(std::format(
                "{}:{}:{}: cannot structurally mutate collection '{}' while iterating over it (cfire/ITERATOR-INVALIDATION)",
                path_, line, col, root));
        }
        return {};
    }

    [[nodiscard]] auto record_var_read(const std::string& name, int line, int col)
        -> std::expected<void, std::string>
    {
        if (auto it = moved_at_line_.find(name); it != moved_at_line_.end()) {
            return emit_error(std::format(
                "{}:{}:{}: use of moved variable '{}' (previously moved at line {}) (cfire/USE-AFTER-MOVE)",
                path_, line, col, name, it->second));
        }
        if (auto it = invalidated_loans_.find(name); it != invalidated_loans_.end()) {
            return emit_error(std::format(
                "{}:{}:{}: use of dangling pointer/view '{}' ({}) (cfire/USE-AFTER-FREE)",
                path_, line, col, name, it->second.reason));
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
            if (un->op == "&" || un->op == "&mut" || un->op == "*") {
                return extract_path_info(un->operand);
            }
        }
        return std::nullopt;
    }

    [[nodiscard]] auto resolve_root_owner(const std::string& var) const -> std::string {
        if (auto it = borrow_origin_.find(var); it != borrow_origin_.end()) {
            return it->second;
        }
        return var;
    }

    [[nodiscard]] auto extract_borrow_source(const ExprPtr& e, bool* is_direct_expr = nullptr) const -> std::optional<std::string> {
        if (is_direct_expr) *is_direct_expr = false;
        if (!e) return std::nullopt;
        if (const auto* un = std::get_if<UnaryExpr>(&e->node)) {
            if (un->op == "&" || un->op == "&mut") {
                if (auto pi = extract_path_info(un->operand)) {
                    if (is_direct_expr) *is_direct_expr = true;
                    return resolve_root_owner(pi->root);
                }
            }
            return std::nullopt;
        }
        if (const auto* sl = std::get_if<SliceExpr>(&e->node)) {
            if (auto pi = extract_path_info(sl->object)) {
                if (is_direct_expr) *is_direct_expr = true;
                return resolve_root_owner(pi->root);
            }
            return std::nullopt;
        }
        if (const auto* call = std::get_if<CallExpr>(&e->node)) {
            if (const auto* mem = std::get_if<MemberExpr>(&call->callee->node)) {
                if ((mem->field == "data" || mem->field == "data_ptr" || mem->field == "byte_ptr") && call->args.empty()) {
                    if (auto pi = extract_path_info(mem->object)) {
                        if (is_direct_expr) *is_direct_expr = true;
                        return resolve_root_owner(pi->root);
                    }
                }
                if (mem->field == "byte_ptr_at" && call->args.size() == 1) {
                    if (auto pi = extract_path_info(mem->object)) {
                        if (is_direct_expr) *is_direct_expr = true;
                        return resolve_root_owner(pi->root);
                    }
                }
                if (const auto* mod_id = std::get_if<IdentExpr>(&mem->object->node);
                    mod_id && mod_id->name == "str" && !call->args.empty() &&
                    (mem->field == "byte_ptr" || mem->field == "byte_ptr_at")) {
                    if (auto pi = extract_path_info(call->args[0].value)) {
                        if (is_direct_expr) *is_direct_expr = true;
                        return resolve_root_owner(pi->root);
                    }
                }
            }
            return std::nullopt;
        }
        if (const auto* id = std::get_if<IdentExpr>(&e->node)) {
            if (auto it = borrow_origin_.find(id->name); it != borrow_origin_.end()) {
                return it->second;
            }
        }
        return std::nullopt;
    }

    [[nodiscard]] auto is_ptr_or_smart_init(const ExprPtr& e) const -> bool {
        if (!e) return false;
        if (const auto* un = std::get_if<UnaryExpr>(&e->node)) {
            return un->op == "&" || un->op == "&mut";
        }
        if (const auto* call = std::get_if<CallExpr>(&e->node)) {
            if (const auto* id = std::get_if<IdentExpr>(&call->callee->node)) {
                if (id->name == "make" && call->type_args.size() == 1 && !call->type_args[0].smart_wrap.empty()) {
                    return true;
                }
            }
            if (const auto* mem = std::get_if<MemberExpr>(&call->callee->node)) {
                if (mem->field == "data" || mem->field == "data_ptr" ||
                    mem->field == "byte_ptr" || mem->field == "byte_ptr_at") {
                    return true;
                }
            }
        }
        if (const auto* id = std::get_if<IdentExpr>(&e->node)) {
            return ptr_or_smart_vars_.contains(id->name);
        }
        return false;
    }

    [[nodiscard]] static auto is_const_zero_expr(const ExprPtr& e) -> bool {
        if (!e) return false;
        if (const auto* un = std::get_if<UnaryExpr>(&e->node)) {
            if (un->op == "-" || un->op == "+") {
                return is_const_zero_expr(un->operand);
            }
            return false;
        }
        if (const auto* lit = std::get_if<LiteralExpr>(&e->node)) {
            if (lit->kind == TokenKind::Int) {
                if (auto v = cfire::detail::parse_int<std::int64_t>(lit->value)) {
                    return *v == 0;
                }
            } else if (lit->kind == TokenKind::Float) {
                bool has_zero = false;
                for (char c : lit->value) {
                    if (c == '0') has_zero = true;
                    else if (c != '.' && c != '_' && c != 'f' && c != 'F') return false;
                }
                return has_zero;
            }
        }
        return false;
    }

    [[nodiscard]] static auto is_dynamic_string_expr(const ExprPtr& e) -> bool {
        if (!e) return false;
        if (const auto* lit = std::get_if<LiteralExpr>(&e->node)) {
            if (lit->kind == TokenKind::String && lit->value.contains('{') && lit->value.contains('}')) {
                return true;
            }
            return false;
        }
        if (const auto* bin = std::get_if<BinaryExpr>(&e->node)) {
            if (bin->op == "+") {
                bool left_static = bin->left && std::holds_alternative<LiteralExpr>(bin->left->node) && !is_dynamic_string_expr(bin->left);
                bool right_static = bin->right && std::holds_alternative<LiteralExpr>(bin->right->node) && !is_dynamic_string_expr(bin->right);
                return !(left_static && right_static);
            }
        }
        return false;
    }

    [[nodiscard]] auto infer_expr_null_state(const ExprPtr& e) const -> std::optional<NullState> {
        if (!e) return std::nullopt;
        if (const auto* lit = std::get_if<LiteralExpr>(&e->node)) {
            if (lit->kind == TokenKind::KwNone) {
                return NullState::Null;
            }
            return std::nullopt;
        }
        if (const auto* un = std::get_if<UnaryExpr>(&e->node)) {
            if (un->op == "&" || un->op == "&mut") {
                return NullState::NonNull;
            }
            return std::nullopt;
        }
        if (const auto* call = std::get_if<CallExpr>(&e->node)) {
            if (const auto* id = std::get_if<IdentExpr>(&call->callee->node)) {
                if (id->name == "make" && call->type_args.size() == 1 && !call->type_args[0].smart_wrap.empty()) {
                    return NullState::NonNull;
                }
            }
            if (const auto* mem = std::get_if<MemberExpr>(&call->callee->node)) {
                if (mem->field == "data" || mem->field == "data_ptr" ||
                    mem->field == "byte_ptr" || mem->field == "byte_ptr_at") {
                    return NullState::NonNull;
                }
            }
            return std::nullopt;
        }
        if (const auto* id = std::get_if<IdentExpr>(&e->node)) {
            if (auto it = ptr_null_state_.find(id->name); it != ptr_null_state_.end()) {
                return it->second;
            }
        }
        return std::nullopt;
    }

    [[nodiscard]] static auto extract_null_check(const ExprPtr& cond) -> std::optional<std::pair<std::string, bool>> {
        if (!cond) return std::nullopt;
        if (const auto* bin = std::get_if<BinaryExpr>(&cond->node)) {
            if (bin->op == "!=" || bin->op == "==") {
                bool is_ne = (bin->op == "!=");
                if (bin->left && bin->right) {
                    const auto* l_id = std::get_if<IdentExpr>(&bin->left->node);
                    const auto* r_lit = std::get_if<LiteralExpr>(&bin->right->node);
                    if (l_id && r_lit && r_lit->kind == TokenKind::KwNone) {
                        return std::make_pair(l_id->name, is_ne);
                    }
                    const auto* r_id = std::get_if<IdentExpr>(&bin->right->node);
                    const auto* l_lit = std::get_if<LiteralExpr>(&bin->left->node);
                    if (r_id && l_lit && l_lit->kind == TokenKind::KwNone) {
                        return std::make_pair(r_id->name, is_ne);
                    }
                }
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

    [[nodiscard]] auto is_ident_in_scope_sema(const std::string& ident) const -> bool {
        if (local_defined_vars_.contains(ident) || mut_vars_.contains(ident) ||
            imm_vars_.contains(ident) || move_params_.contains(ident)) {
            return true;
        }
        if (cf_.fn_sigs.contains(ident)) return true;
        for (const auto& gv : cf_.ast.global_vars) {
            if (gv.name == ident) return true;
        }
        return false;
    }

    [[nodiscard]] static auto extract_root_ident_sema(const ExprPtr& e) -> std::string {
        if (!e) return "";
        if (const auto* id = std::get_if<IdentExpr>(&e->node)) return id->name;
        if (const auto* mem = std::get_if<MemberExpr>(&e->node)) return extract_root_ident_sema(mem->object);
        if (const auto* idx = std::get_if<IndexExpr>(&e->node)) return extract_root_ident_sema(idx->object);
        if (const auto* call = std::get_if<CallExpr>(&e->node)) return extract_root_ident_sema(call->callee);
        if (const auto* un = std::get_if<UnaryExpr>(&e->node)) return extract_root_ident_sema(un->operand);
        if (const auto* bin = std::get_if<BinaryExpr>(&e->node)) {
            auto l = extract_root_ident_sema(bin->left);
            return !l.empty() ? l : extract_root_ident_sema(bin->right);
        }
        return "";
    }

    [[nodiscard]] auto check_expr(const ExprPtr& e) -> std::expected<void, std::string> {
        if (!e) return {};

        if (const auto* id = std::get_if<IdentExpr>(&e->node)) {
            return record_var_read(id->name, e->line, e->col);
        }
        if (const auto* lit = std::get_if<LiteralExpr>(&e->node)) {
            if (lit->kind == TokenKind::String && lit->value.contains('{')) {
                auto placeholders = scan_interpolation_placeholders(lit->value);
                for (const auto& ph : placeholders) {
                    if (auto parsed = parse_expr_snippet(path_, ph.expr_text, e->line)) {
                        std::string root = extract_root_ident_sema(parsed->expr);
                        if (!root.empty() && is_ident_in_scope_sema(root)) {
                            if (auto r = check_expr(parsed->expr); !r) return r;
                        }
                    }
                }
            }
            return {};
        }
        if (const auto* un = std::get_if<UnaryExpr>(&e->node)) {
            if (auto r = check_expr(un->operand); !r) return r;
            if (un->op == "move") {
                if (auto pi = extract_path_info(un->operand)) {
                    if (auto r = check_not_iterated_collection(pi->root, e->line, e->col); !r) return r;
                    moved_at_line_[pi->root] = e->line;
                    invalidate_loans_for_owner(
                        pi->root,
                        std::format("underlying owner '{}' was moved at line {}", pi->root, e->line),
                        e->line);
                }
            } else if (un->op == "*" && un->operand) {
                if (const auto* id = std::get_if<IdentExpr>(&un->operand->node)) {
                    if (auto it = ptr_null_state_.find(id->name); it != ptr_null_state_.end()) {
                        if (it->second == NullState::Null) {
                            if (auto r = emit_error(std::format(
                                "{}:{}:{}: dereference of null pointer '{}' (cfire/NULL-DEREF)",
                                path_, e->line, e->col, id->name)); !r) return r;
                        } else if (it->second == NullState::MaybeNull) {
                            if (auto r = emit_error(std::format(
                                "{}:{}:{}: dereference of potentially null pointer '{}' without '!= none' guard (cfire/NULL-DEREF)",
                                path_, e->line, e->col, id->name)); !r) return r;
                        }
                    }
                }
            }
            return {};
        }
        if (const auto* bin = std::get_if<BinaryExpr>(&e->node)) {
            if (auto r = check_expr(bin->left); !r) return r;
            if (auto r = check_expr(bin->right); !r) return r;
            if ((bin->op == "/" || bin->op == "%") && is_const_zero_expr(bin->right)) {
                return emit_error(std::format(
                    "{}:{}:{}: division or modulo by constant zero (cfire/DIVIDE-BY-ZERO)",
                    path_, e->line, e->col));
            }
            return {};
        }
        if (const auto* mem = std::get_if<MemberExpr>(&e->node)) {
            if (auto r = check_expr(mem->object); !r) return r;
            if (!mem->is_namespace && mem->object) {
                if (const auto* id = std::get_if<IdentExpr>(&mem->object->node)) {
                    if (ptr_or_smart_vars_.contains(id->name) &&
                        mem->field != "reset" && mem->field != "get" &&
                        mem->field != "has_value" && mem->field != "value_or") {
                        if (auto it = ptr_null_state_.find(id->name); it != ptr_null_state_.end()) {
                            if (it->second == NullState::Null) {
                                return emit_error(std::format(
                                    "{}:{}:{}: member access on null pointer '{}' (cfire/NULL-DEREF)",
                                    path_, e->line, e->col, id->name));
                            }
                            if (it->second == NullState::MaybeNull) {
                                return emit_error(std::format(
                                    "{}:{}:{}: member access on potentially null pointer '{}' without '!= none' guard (cfire/NULL-DEREF)",
                                    path_, e->line, e->col, id->name));
                            }
                        }
                    }
                }
            }
            return {};
        }
        if (const auto* idx = std::get_if<IndexExpr>(&e->node)) {
            if (auto r = check_expr(idx->object); !r) return r;
            if (auto r = check_expr(idx->index); !r) return r;

            bool is_map_obj = false;
            const IdentExpr* obj_id = idx->object ? std::get_if<IdentExpr>(&idx->object->node) : nullptr;
            if (obj_id && map_vars_.contains(obj_id->name)) {
                is_map_obj = true;
            }
            if (!is_map_obj && idx->index) {
                if (const auto* un_idx = std::get_if<UnaryExpr>(&idx->index->node)) {
                    if (un_idx->op == "-" && un_idx->operand) {
                        if (const auto* neg_lit = std::get_if<LiteralExpr>(&un_idx->operand->node);
                            neg_lit && neg_lit->kind == TokenKind::Int && neg_lit->value != "0") {
                            if (auto r = emit_error(std::format(
                                "{}:{}:{}: negative index -{} is out of bounds (cfire/INDEX-OUT-OF-BOUNDS)",
                                path_, e->line, e->col, neg_lit->value)); !r) return r;
                        }
                    }
                } else if (const auto* idx_lit = std::get_if<LiteralExpr>(&idx->index->node)) {
                    if (idx_lit->kind == TokenKind::Int) {
                        if (idx_lit->value.starts_with('-') && idx_lit->value != "-0") {
                            if (auto r = emit_error(std::format(
                                "{}:{}:{}: negative index {} is out of bounds (cfire/INDEX-OUT-OF-BOUNDS)",
                                path_, e->line, e->col, idx_lit->value)); !r) return r;
                        } else if (auto k = cfire::detail::parse_int<std::size_t>(idx_lit->value)) {
                            if (obj_id) {
                                if (auto fit = fixed_array_lens_.find(obj_id->name); fit != fixed_array_lens_.end()) {
                                    if (*k >= fit->second) {
                                        if (auto r = emit_error(std::format(
                                            "{}:{}:{}: index {} is out of bounds for array '{}' of length {} (cfire/INDEX-OUT-OF-BOUNDS)",
                                            path_, e->line, e->col, *k, obj_id->name, fit->second)); !r) return r;
                                    }
                                }
                            } else if (idx->object) {
                                if (const auto* arr_lit = std::get_if<ArrayLitExpr>(&idx->object->node)) {
                                    if (*k >= arr_lit->elements.size()) {
                                        if (auto r = emit_error(std::format(
                                            "{}:{}:{}: index {} is out of bounds for array literal of length {} (cfire/INDEX-OUT-OF-BOUNDS)",
                                            path_, e->line, e->col, *k, arr_lit->elements.size())); !r) return r;
                                    }
                                }
                            }
                        }
                    }
                }
            }
            return {};
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
                                    if (auto r = emit_error(std::format(
                                        "{}:{}:{}: cannot call mutating method '{}' on immutable variable '{}' (cfire/MUT-RECEIVER)",
                                        path_, e->line, e->col, mem->field, obj_id->name)); !r) return r;
                                }
                            }
                        }
                    }
                }
                if (mem->field == "push" || mem->field == "pop" || mem->field == "clear" ||
                    mem->field == "erase" || mem->field == "remove") {
                    if (auto pi = extract_path_info(mem->object)) {
                        if (auto r = check_not_iterated_collection(pi->root, e->line, e->col); !r) return r;
                        invalidate_loans_for_owner(
                            pi->root,
                            std::format("underlying container '{}' was mutated at line {}", pi->root, e->line),
                            e->line);
                    }
                }
                if (!call->args.empty() && is_dynamic_string_expr(call->args[0].value)) {
                    const auto* recv_id = mem->object ? std::get_if<IdentExpr>(&mem->object->node) : nullptr;
                    if (recv_id && recv_id->name == "os" && (mem->field == "exec" || mem->field == "exec_output")) {
                        warnings_.push_back(std::format(
                            "{}:{}:{}: warning: dynamic string interpolation/concatenation in 'os.{}' risks shell command injection; prefer 'os.command(binary, args)' (cfire/SHELL-INJECTION-RISK)",
                            path_, call->args[0].line, call->args[0].col, mem->field));
                    } else if ((!recv_id || recv_id->name != "os") && (mem->field == "exec" || mem->field == "query")) {
                        warnings_.push_back(std::format(
                            "{}:{}:{}: warning: dynamic string interpolation/concatenation in SQL '{}' risks SQL injection; use parameterized '?1' bindings instead (cfire/SQL-INJECTION-RISK)",
                            path_, call->args[0].line, call->args[0].col, mem->field));
                    }
                }
            }

            if (target_sig) {
                std::size_t actual_args = call->args.size() + (call->block_arg ? 1 : 0);
                if (actual_args != target_sig->params.size()) {
                    if (auto r = emit_error(std::format(
                        "{}:{}:{}: call to '{}' expects {} argument(s), got {} (cfire/ARITY-MISMATCH)",
                        path_, e->line, e->col, target_sig->name, target_sig->params.size(), actual_args)); !r) return r;
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
                        std::string_view rule = (expected == "out")
                            ? "cfire/OUT-CALLSITE"
                            : (expected == "move" ? "cfire/MOVE-CALLSITE" : "cfire/MUT-CALLSITE");
                        if (auto r = emit_error(std::format(
                            "{}:{}:{}: parameter '{}' of {} requires explicit '{}' annotation at call-site ({})",
                            path_, arg.line, arg.col, target_sig->params[i].name, target_sig->name, expected, rule)); !r) return r;
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
                                if (auto r = emit_error(std::format(
                                    "{}:{}:{}: cannot alias {} with a mut/out parameter in the same call (cfire/MUT-ALIAS)",
                                    path_, arg.line, arg.col, entity)); !r) return r;
                            }
                        }
                    }
                    seen_paths.push_back({*pi, is_mut_or_out});

                    if (is_mut_or_out) {
                        if (imm_vars_.contains(pi->root)) {
                            std::string entity = (pi->path == pi->root) ? pi->root : pi->path;
                            if (auto r = emit_error(std::format(
                                "{}:{}:{}: cannot pass immutable variable '{}' as '{}' argument (cfire/IMMUTABLE-MUT-ARG)",
                                path_, arg.line, arg.col, entity, arg.intent)); !r) return r;
                        }
                        if (pi->path == pi->root) {
                            if (auto r = check_not_iterated_collection(pi->root, arg.line, arg.col); !r) return r;
                        }
                    }
                    if (arg.intent == "move") {
                        if (auto r = check_not_iterated_collection(pi->root, arg.line, arg.col); !r) return r;
                        moved_at_line_[pi->root] = arg.line;
                        invalidate_loans_for_owner(
                            pi->root,
                            std::format("underlying owner '{}' was moved at line {}", pi->root, arg.line),
                            arg.line);
                    }
                }
            }
            if (const auto* id = std::get_if<IdentExpr>(&call->callee->node)) {
                if (id->name == "assert" && !call->args.empty()) {
                    if (auto nc = extract_null_check(call->args[0].value); nc && nc->second) {
                        ptr_null_state_[nc->first] = NullState::NonNull;
                    }
                }
            }
            if (call->block_arg) {
                ++scope_depth_;
                for (const auto& s : call->block_arg->body) {
                    if (auto r = check_stmt(s); !r) {
                        --scope_depth_;
                        return r;
                    }
                }
                --scope_depth_;
            }
            return {};
        }
        if (const auto* me = std::get_if<MatchExpr>(&e->node)) {
            if (auto r = check_expr(me->target); !r) return r;
            if (me->is_move && me->target) {
                if (auto pi = extract_path_info(me->target)) {
                    if (auto r = check_not_iterated_collection(pi->root, me->line, me->col); !r) return r;
                    moved_at_line_[pi->root] = me->line;
                    invalidate_loans_for_owner(
                        pi->root,
                        std::format("underlying owner '{}' was moved at line {}", pi->root, me->line),
                        me->line);
                }
            }
            if (auto r = check_match_arms_reachability(me->arms, me->line, me->col); !r) return r;
            bool is_slice_match = false;
            bool is_primitive_match = false;
            for (const auto& a : me->arms) {
                if (a.is_slice_pattern) {
                    is_slice_match = true;
                }
                if (!a.literal_val.empty()) {
                    is_primitive_match = true;
                }
            }

            std::string enum_name;
            std::unordered_set<std::string> covered;
            bool has_wildcard = false;

            for (const auto& arm : me->arms) {
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
                ++scope_depth_;
                for (const auto& b : arm.body) {
                    if (auto r = check_stmt(b); !r) {
                        --scope_depth_;
                        return r;
                    }
                }
                --scope_depth_;
            }

            if (!has_wildcard && !enum_name.empty()) {
                if (auto eit = cf_.enums.find(enum_name); eit != cf_.enums.end()) {
                    for (const auto& v : eit->second.variants) {
                        if (!covered.contains(v.name)) {
                            if (auto r = emit_error(std::format(
                                "{}:{}:{}: non-exhaustive match on enum '{}': missing variant '{}' (cfire/NON-EXHAUSTIVE-MATCH)",
                                path_, me->line, me->col, enum_name, v.name)); !r) return r;
                        }
                    }
                }
            } else if ((is_slice_match || is_primitive_match || enum_name.empty()) && !has_wildcard) {
                if (!is_slice_match || !is_slice_exhaustively_covered(me->arms)) {
                    if (auto r = emit_error(std::format(
                        "{}:{}:{}: match expression must have a wildcard '_' fallback arm (cfire/NON-EXHAUSTIVE-MATCH)",
                        path_, me->line, me->col)); !r) return r;
                }
            }
            return {};
        }
        return {};
    }

    [[nodiscard]] static auto is_slice_exhaustively_covered(const std::vector<MatchArm>& arms) -> bool {
        std::unordered_set<std::size_t> exact_covered;
        std::optional<std::size_t> min_variadic_covered;
        for (const auto& arm : arms) {
            if (arm.variant_name == "_" && !arm.is_slice_pattern && arm.literal_val.empty()) {
                return true;
            }
            if (arm.is_slice_pattern) {
                std::size_t min_sz = arm.slice_head.size() + arm.slice_tail.size();
                if (!arm.slice_has_rest) {
                    exact_covered.insert(min_sz);
                } else {
                    if (!min_variadic_covered || min_sz < *min_variadic_covered) {
                        min_variadic_covered = min_sz;
                    }
                }
            }
        }
        if (!min_variadic_covered) return false;
        for (std::size_t k = 0; k < *min_variadic_covered; ++k) {
            if (!exact_covered.contains(k)) return false;
        }
        return true;
    }

    [[nodiscard]] auto check_match_arms_reachability(
        const std::vector<MatchArm>& arms,
        int match_line,
        int match_col
    ) -> std::expected<void, std::string> {
        bool seen_wildcard = false;
        std::unordered_set<std::string> seen_variants;
        std::unordered_set<std::string> seen_literals;
        std::unordered_set<std::size_t> exact_slice_sizes;
        std::optional<std::size_t> min_variadic_slice;

        auto is_size_covered = [&](std::size_t n) -> bool {
            if (exact_slice_sizes.contains(n)) return true;
            if (min_variadic_slice && n >= *min_variadic_slice) return true;
            return false;
        };

        auto all_sizes_from_covered = [&](std::size_t m) -> bool {
            if (!min_variadic_slice) return false;
            for (std::size_t k = m; k < *min_variadic_slice; ++k) {
                if (!exact_slice_sizes.contains(k)) return false;
            }
            return true;
        };

        for (std::size_t idx = 0; idx < arms.size(); ++idx) {
            const auto& arm = arms[idx];
            if (seen_wildcard) {
                return emit_error(std::format(
                    "{}:{}:{}: unreachable match arm #{}: previous wildcard '_' arm already matches all remaining cases (cfire/UNREACHABLE-MATCH-ARM)",
                    path_, match_line, match_col, idx + 1));
            }
            if (arm.is_slice_pattern) {
                std::size_t min_sz = arm.slice_head.size() + arm.slice_tail.size();
                if (!arm.slice_has_rest) {
                    if (is_size_covered(min_sz)) {
                        return emit_error(std::format(
                            "{}:{}:{}: unreachable slice match arm #{} (exact length {} is already matched by an earlier arm) (cfire/UNREACHABLE-MATCH-ARM)",
                            path_, match_line, match_col, idx + 1, min_sz));
                    }
                    exact_slice_sizes.insert(min_sz);
                } else {
                    if (all_sizes_from_covered(min_sz)) {
                        return emit_error(std::format(
                            "{}:{}:{}: unreachable slice match arm #{} (slices of length >= {} are already matched by earlier arms) (cfire/UNREACHABLE-MATCH-ARM)",
                            path_, match_line, match_col, idx + 1, min_sz));
                    }
                    if (!min_variadic_slice || min_sz < *min_variadic_slice) {
                        min_variadic_slice = min_sz;
                    }
                }
                continue;
            }
            if (arm.variant_name == "_" && arm.literal_val.empty()) {
                if (min_variadic_slice && all_sizes_from_covered(0)) {
                    return emit_error(std::format(
                        "{}:{}:{}: unreachable wildcard '_' match arm #{}: all slice lengths are already covered by earlier arms (cfire/UNREACHABLE-MATCH-ARM)",
                        path_, match_line, match_col, idx + 1));
                }
                seen_wildcard = true;
                continue;
            }
            if (!arm.literal_val.empty()) {
                if (seen_literals.contains(arm.literal_val)) {
                    return emit_error(std::format(
                        "{}:{}:{}: unreachable match arm #{}: duplicate literal pattern '{}' (cfire/UNREACHABLE-MATCH-ARM)",
                        path_, match_line, match_col, idx + 1, arm.literal_val));
                }
                seen_literals.insert(arm.literal_val);
                continue;
            }
            if (!arm.variant_name.empty()) {
                if (seen_variants.contains(arm.variant_name)) {
                    return emit_error(std::format(
                        "{}:{}:{}: unreachable match arm #{}: duplicate variant pattern '{}' (cfire/UNREACHABLE-MATCH-ARM)",
                        path_, match_line, match_col, idx + 1, arm.variant_name));
                }
                seen_variants.insert(arm.variant_name);
            }
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
            var_scope_depth_[vd->name] = scope_depth_;
            local_defined_vars_.insert(vd->name);
            moved_at_line_.erase(vd->name);
            invalidated_loans_.erase(vd->name);
            borrow_origin_.erase(vd->name);

            if (vd->is_mut) {
                imm_vars_.erase(vd->name);
                mut_vars_.insert(vd->name);
            } else {
                mut_vars_.erase(vd->name);
                imm_vars_.insert(vd->name);
            }
            if (vd->type) {
                if (vd->type->is_map) {
                    map_vars_.insert(vd->name);
                }
                if (vd->type->is_array && !vd->type->array_len.empty()) {
                    if (auto n = cfire::detail::parse_int<std::size_t>(vd->type->array_len)) {
                        fixed_array_lens_[vd->name] = *n;
                    }
                }
            } else if (!vd->is_mut && vd->init) {
                if (const auto* arr_lit = std::get_if<ArrayLitExpr>(&vd->init->node)) {
                    fixed_array_lens_[vd->name] = arr_lit->elements.size();
                }
            }
            if (vd->init) {
                if (const auto* call_ex = std::get_if<CallExpr>(&vd->init->node)) {
                    if (const auto* cid = std::get_if<IdentExpr>(&call_ex->callee->node);
                        cid && cid->name == "make" && call_ex->type_args.size() == 1 && call_ex->type_args[0].is_map) {
                        map_vars_.insert(vd->name);
                    }
                }
                if (const auto* or_ex = std::get_if<OrExpr>(&vd->init->node); or_ex && !or_ex->handler_body.empty()) {
                    if (!block_diverges(or_ex->handler_body)) {
                        if (auto r = emit_error(std::format(
                            "{}:{}:{}: block 'or {} {{ ... }}' must definitely diverge with return or panic, otherwise unwrapping empty value causes undefined behavior (cfire/NON-DIVERGING-OR-BLOCK)",
                            path_, vd->line, vd->col, or_ex->err_var.empty() ? "err" : or_ex->err_var)); !r) return r;
                    }
                }
            }
            if (auto r = check_expr(vd->init); !r) return r;

            if (auto src = extract_borrow_source(vd->init)) {
                borrow_origin_[vd->name] = *src;
            }
            bool is_ptr_or_smart = (vd->type && (vd->type->is_ptr || vd->type->is_mut_ptr || !vd->type->smart_wrap.empty())) ||
                                   is_ptr_or_smart_init(vd->init);
            if (is_ptr_or_smart) {
                ptr_or_smart_vars_.insert(vd->name);
            } else {
                ptr_or_smart_vars_.erase(vd->name);
            }
            if (auto ns = infer_expr_null_state(vd->init)) {
                ptr_null_state_[vd->name] = *ns;
            } else if (!vd->init && is_ptr_or_smart) {
                ptr_null_state_[vd->name] = NullState::Null;
            } else if (vd->type && (vd->type->is_ptr || vd->type->is_mut_ptr || !vd->type->smart_wrap.empty())) {
                ptr_null_state_[vd->name] = vd->type->is_optional ? NullState::MaybeNull : NullState::NonNull;
            } else {
                ptr_null_state_.erase(vd->name);
            }
            return {};
        }
        if (const auto* dvs = std::get_if<DestructureVarDeclStmt>(&s->node)) {
            for (const auto& [name, is_mut] : dvs->bindings) {
                var_scope_depth_[name] = scope_depth_;
                local_defined_vars_.insert(name);
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
                        if (auto r = emit_error(std::format(
                            "{}:{}:{}: assignment from 'or {} {{ ... }}' must definitely diverge with return or panic (cfire/NON-DIVERGING-OR-BLOCK)",
                            path_, as->line, 1, or_ex->err_var.empty() ? "err" : or_ex->err_var)); !r) return r;
                    }
                }
            }
            if (auto r = check_expr(as->value); !r) return r;
            if ((as->op == "/=" || as->op == "%=") && is_const_zero_expr(as->value)) {
                if (auto r = emit_error(std::format(
                    "{}:{}:1: division or modulo assignment by constant zero (cfire/DIVIDE-BY-ZERO)",
                    path_, as->line)); !r) return r;
            }
            if (as->op != "=") {
                if (auto r = check_expr(as->target); !r) return r;
            }
            if (const auto* id = std::get_if<IdentExpr>(&as->target->node)) {
                if (imm_vars_.contains(id->name)) {
                    if (auto r = emit_error(std::format(
                        "{}:{}:{}: cannot assign to immutable variable '{}' (cfire/ASSIGN-IMMUTABLE)",
                        path_, as->line, 1, id->name)); !r) return r;
                }
                if (auto r = check_not_iterated_collection(id->name, as->line, 1); !r) return r;
                moved_at_line_.erase(id->name);
                if (as->op == "=") {
                    invalidate_loans_for_owner(
                        id->name,
                        std::format("underlying owner '{}' was reassigned at line {}", id->name, as->line),
                        as->line);
                    if (auto src = extract_borrow_source(as->value)) {
                        auto d_target = var_scope_depth_.find(id->name);
                        auto d_src = var_scope_depth_.find(*src);
                        if (d_target != var_scope_depth_.end() && d_src != var_scope_depth_.end()) {
                            if (d_src->second > d_target->second) {
                                if (auto r = emit_error(std::format(
                                    "{}:{}:1: pointer/view '{}' cannot borrow '{}' from an inner scope (cfire/DANGLING-POINTER)",
                                    path_, as->line, id->name, *src)); !r) return r;
                            }
                        }
                        borrow_origin_[id->name] = *src;
                        invalidated_loans_.erase(id->name);
                    } else {
                        borrow_origin_.erase(id->name);
                        invalidated_loans_.erase(id->name);
                    }
                    if (is_ptr_or_smart_init(as->value)) {
                        ptr_or_smart_vars_.insert(id->name);
                    }
                    if (auto ns = infer_expr_null_state(as->value)) {
                        ptr_null_state_[id->name] = *ns;
                    } else if (ptr_null_state_.contains(id->name)) {
                        ptr_null_state_[id->name] = NullState::NonNull;
                    }
                }
            } else {
                if (as->op == "=") {
                    if (auto r = check_expr(as->target); !r) return r;
                }
            }
            return {};
        }
        if (const auto* das = std::get_if<DestructureAssignStmt>(&s->node)) {
            if (auto r = check_expr(das->value); !r) return r;
            for (const auto& targ : das->targets) {
                if (const auto* id = std::get_if<IdentExpr>(&targ->node)) {
                    if (imm_vars_.contains(id->name)) {
                        if (auto r = emit_error(std::format(
                            "{}:{}:{}: cannot assign to immutable variable '{}' (cfire/ASSIGN-IMMUTABLE)",
                            path_, das->line, 1, id->name)); !r) return r;
                    }
                    moved_at_line_.erase(id->name);
                } else {
                    if (auto r = check_expr(targ); !r) return r;
                }
            }
            return {};
        }
        if (const auto* df = std::get_if<DeferStmt>(&s->node)) {
            if (df->call) {
                return check_expr(df->call);
            }
            ++in_defer_depth_;
            ++scope_depth_;
            for (const auto& b : df->body) {
                if (auto r = check_stmt(b); !r) {
                    --scope_depth_;
                    --in_defer_depth_;
                    return r;
                }
            }
            --scope_depth_;
            --in_defer_depth_;
            return {};
        }
        if (const auto* rt = std::get_if<ReturnStmt>(&s->node)) {
            if (in_defer_depth_ > 0) {
                if (auto r = emit_error(std::format(
                    "{}:{}:1: cannot use 'return' inside a 'defer {{ ... }}' block (cfire/RETURN-IN-DEFER)",
                    path_, rt->line)); !r) return r;
            }
            if (auto r = check_expr(rt->value); !r) return r;
            if (rt->value) {
                bool is_direct = false;
                if (auto src = extract_borrow_source(rt->value, &is_direct)) {
                    if (local_defined_vars_.contains(*src) || move_params_.contains(*src)) {
                        if (auto r = emit_error(std::format(
                            "{}:{}:1: cannot return pointer/view borrowing local variable '{}' (cfire/DANGLING-RETURN)",
                            path_, rt->line, *src)); !r) return r;
                    }
                }
            }
            return {};
        }
        if (const auto* ifs = std::get_if<IfStmt>(&s->node)) {
            if (auto r = check_expr(ifs->cond); !r) return r;
            auto initial_moves = moved_at_line_;
            auto initial_loans = invalidated_loans_;
            auto initial_null = ptr_null_state_;
            auto nc = extract_null_check(ifs->cond);

            if (nc) {
                ptr_null_state_[nc->first] = nc->second ? NullState::NonNull : NullState::Null;
            }
            ++scope_depth_;
            for (const auto& b : ifs->then_body) {
                if (auto r = check_stmt(b); !r) {
                    --scope_depth_;
                    return r;
                }
            }
            --scope_depth_;
            auto then_moves = moved_at_line_;
            auto then_loans = invalidated_loans_;
            auto then_null = ptr_null_state_;

            moved_at_line_ = initial_moves;
            invalidated_loans_ = initial_loans;
            ptr_null_state_ = initial_null;
            if (nc) {
                ptr_null_state_[nc->first] = nc->second ? NullState::Null : NullState::NonNull;
            }
            ++scope_depth_;
            for (const auto& b : ifs->else_body) {
                if (auto r = check_stmt(b); !r) {
                    --scope_depth_;
                    return r;
                }
            }
            --scope_depth_;
            auto else_moves = moved_at_line_;
            auto else_loans = invalidated_loans_;
            auto else_null = ptr_null_state_;

            bool then_div = block_diverges(ifs->then_body);
            bool else_div = !ifs->else_body.empty() && block_diverges(ifs->else_body);

            if (ifs->else_body.empty()) {
                moved_at_line_ = initial_moves;
                if (then_div) {
                    invalidated_loans_ = else_loans;
                    ptr_null_state_ = else_null;
                } else {
                    invalidated_loans_ = then_loans;
                    for (const auto& [k, v] : initial_loans) {
                        invalidated_loans_.try_emplace(k, v);
                    }
                    std::unordered_map<std::string, NullState> merged_null = initial_null;
                    for (const auto& [var, st] : then_null) {
                        if (nc && var == nc->first) {
                            // Do not leak cond-narrowing out of a non-diverging if-block unless reassigned
                            continue;
                        }
                        auto it = initial_null.find(var);
                        if (it == initial_null.end() || it->second == st) {
                            merged_null[var] = st;
                        } else {
                            merged_null[var] = NullState::MaybeNull;
                        }
                    }
                    ptr_null_state_ = std::move(merged_null);
                }
            } else {
                if (then_div) {
                    moved_at_line_ = else_moves;
                    invalidated_loans_ = else_loans;
                    ptr_null_state_ = else_null;
                } else if (else_div) {
                    moved_at_line_ = then_moves;
                    invalidated_loans_ = then_loans;
                    ptr_null_state_ = then_null;
                } else {
                    std::unordered_map<std::string, int> post_moves = initial_moves;
                    for (const auto& [var, line] : then_moves) {
                        if (else_moves.contains(var)) {
                            post_moves[var] = std::max(line, else_moves[var]);
                        }
                    }
                    moved_at_line_ = std::move(post_moves);

                    std::unordered_map<std::string, InvalidatedLoan> post_loans = then_loans;
                    for (const auto& [var, loan] : else_loans) {
                        post_loans.try_emplace(var, loan);
                    }
                    invalidated_loans_ = std::move(post_loans);

                    std::unordered_map<std::string, NullState> merged_null = initial_null;
                    for (const auto& [var, st_then] : then_null) {
                        auto it_else = else_null.find(var);
                        if (it_else != else_null.end() && it_else->second == st_then) {
                            if (nc && var == nc->first) {
                                // Only preserve if both branches ended with same state independently of complementary cond
                                merged_null[var] = st_then;
                            } else {
                                merged_null[var] = st_then;
                            }
                        } else {
                            merged_null[var] = NullState::MaybeNull;
                        }
                    }
                    ptr_null_state_ = std::move(merged_null);
                }
            }
            return {};
        }
        if (const auto* fs = std::get_if<ForStmt>(&s->node)) {
            if (auto r = check_expr(fs->iter_expr); !r) return r;
            if (fs->range_end) {
                if (auto r = check_expr(fs->range_end); !r) return r;
            }
            auto pre_loop_moves = moved_at_line_;
            auto pre_loop_loans = invalidated_loans_;
            std::string locked_col;
            if (!fs->item_var.empty() && !fs->range_end) {
                if (auto pi = extract_path_info(fs->iter_expr); pi && pi->path == pi->root) {
                    if (!active_loop_collections_.contains(pi->root)) {
                        locked_col = pi->root;
                        active_loop_collections_.insert(locked_col);
                    }
                }
            }
            ++scope_depth_;
            if (!fs->index_var.empty()) {
                var_scope_depth_[fs->index_var] = scope_depth_;
                local_defined_vars_.insert(fs->index_var);
                imm_vars_.insert(fs->index_var);
            }
            if (!fs->item_var.empty()) {
                var_scope_depth_[fs->item_var] = scope_depth_;
                local_defined_vars_.insert(fs->item_var);
                imm_vars_.insert(fs->item_var);
            }
            for (const auto& b : fs->body) {
                if (auto r = check_stmt(b); !r) {
                    --scope_depth_;
                    if (!locked_col.empty()) active_loop_collections_.erase(locked_col);
                    return r;
                }
            }
            --scope_depth_;
            if (!locked_col.empty()) active_loop_collections_.erase(locked_col);

            for (const auto& [var, line] : moved_at_line_) {
                if (!pre_loop_moves.contains(var)) {
                    if (auto r = emit_error(std::format(
                        "{}:{}:{}: variable '{}' moved inside loop body without reassignment before next iteration (cfire/USE-AFTER-MOVE)",
                        path_, line, 1, var)); !r) return r;
                }
            }
            for (const auto& [var, loan] : invalidated_loans_) {
                if (!pre_loop_loans.contains(var) && var_scope_depth_.contains(var) && var_scope_depth_[var] <= scope_depth_) {
                    // Loan on outer pointer invalidated inside loop; remains invalidated after loop
                }
            }
            return {};
        }
        if (const auto* ms = std::get_if<MatchStmt>(&s->node)) {
            if (auto r = check_expr(ms->target); !r) return r;
            if (ms->is_move && ms->target) {
                if (auto pi = extract_path_info(ms->target)) {
                    if (auto r = check_not_iterated_collection(pi->root, ms->line, ms->col); !r) return r;
                    moved_at_line_[pi->root] = ms->line;
                    invalidate_loans_for_owner(
                        pi->root,
                        std::format("underlying owner '{}' was moved at line {}", pi->root, ms->line),
                        ms->line);
                }
            }
            if (auto r = check_match_arms_reachability(ms->arms, ms->line, ms->col); !r) return r;
            bool is_slice_match = false;
            for (const auto& a : ms->arms) {
                if (a.is_slice_pattern) {
                    is_slice_match = true;
                    break;
                }
            }
            if (is_slice_match) {
                for (const auto& arm : ms->arms) {
                    ++scope_depth_;
                    for (const auto& b : arm.body) {
                        if (auto r = check_stmt(b); !r) {
                            --scope_depth_;
                            return r;
                        }
                    }
                    --scope_depth_;
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
                ++scope_depth_;
                for (const auto& b : arm.body) {
                    if (auto r = check_stmt(b); !r) {
                        --scope_depth_;
                        return r;
                    }
                }
                --scope_depth_;
            }

            // Exhaustiveness check against known EnumDecl
            if (!has_wildcard && !enum_name.empty()) {
                if (auto eit = cf_.enums.find(enum_name); eit != cf_.enums.end()) {
                    for (const auto& v : eit->second.variants) {
                        if (!covered.contains(v.name)) {
                            if (auto r = emit_error(std::format(
                                "{}:{}:{}: non-exhaustive match on enum '{}': missing variant '{}' (cfire/NON-EXHAUSTIVE-MATCH)",
                                path_, ms->line, ms->col, enum_name, v.name)); !r) return r;
                        }
                    }
                }
            }
            return {};
        }
        if (const auto* sc = std::get_if<AsyncScopeStmt>(&s->node)) {
            if (auto r = check_expr(sc->workers); !r) return r;
            ++scope_depth_;
            for (const auto& b : sc->body) {
                if (auto r = check_stmt(b); !r) {
                    --scope_depth_;
                    return r;
                }
            }
            --scope_depth_;
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
    std::vector<std::string>& errors_;
    bool resilient_{false};
    std::vector<std::string> fallback_errors_{};
    int scope_depth_{1};
    int in_defer_depth_{0};
    std::unordered_map<std::string, int> var_scope_depth_{};
    std::unordered_set<std::string> local_defined_vars_{};
    std::unordered_map<std::string, std::string> borrow_origin_{};
    std::unordered_map<std::string, InvalidatedLoan> invalidated_loans_{};
    std::unordered_set<std::string> active_loop_collections_{};
    std::unordered_set<std::string> ptr_or_smart_vars_{};
    std::unordered_map<std::string, NullState> ptr_null_state_{};
    std::unordered_map<std::string, std::size_t> fixed_array_lens_{};
    std::unordered_set<std::string> map_vars_{};
    std::unordered_map<std::string, int> moved_at_line_{};
    std::unordered_set<std::string> move_params_{};
    std::unordered_set<std::string> mut_vars_{};
    std::unordered_set<std::string> imm_vars_{};
};


[[nodiscard]] inline auto analyze_resilient(FileAST ast) -> CheckedFile {
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
                } else if (key == "db" || key == "sql") {
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
            cf.method_owners[fn.name].insert(fn.receiver->type.name);
        } else {
            cf.fn_sigs[fn.name] = fn;
        }
    }

    // 4. Validate Function & Method Bodies
    for (const auto& fn : ast.functions) {
        SemaFnChecker checker(ast.path, fn, cf, cf.warnings, cf.errors, true);
        for (const auto& st : fn.body) {
            (void)checker.check_stmt(st);
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
        SemaFnChecker checker(ast.path, dummy_fn, cf, cf.warnings, cf.errors, true);
        for (const auto& st : ast.tests[idx].body) {
            (void)checker.check_stmt(st);
        }
    }

    cf.ast = std::move(ast);
    return cf;
}

[[nodiscard]] inline auto analyze(FileAST ast) -> std::expected<CheckedFile, std::string> {
    auto cf = analyze_resilient(std::move(ast));
    if (!cf.errors.empty()) {
        return std::unexpected(cf.errors.front());
    }
    return cf;
}

} // namespace cfire::frontend

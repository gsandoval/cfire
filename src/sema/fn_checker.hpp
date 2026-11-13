#pragma once
// src/sema/fn_checker.hpp — Single-Pass Function Body & Effect Checker

#include "types.hpp"
#include "stdlib_index.hpp"
#include "builtin_methods.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <format>
#include <optional>
#include <ranges>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace cfire::frontend {

class SemaFnChecker {
public:
    enum class NullState { NonNull, MaybeNull, Null };

    struct InvalidatedLoan {
        std::string reason{};
        int line = 1;
    };

    [[nodiscard]] static auto is_scalar_primitive_type_name(std::string_view t) -> bool {
        return t == "i8" || t == "i16" || t == "i32" || t == "i64" || t == "isize" || t == "int" ||
               t == "u8" || t == "u16" || t == "u32" || t == "u64" || t == "usize" || t == "uint" ||
               t == "byte" || t == "f32" || t == "f64" || t == "bool";
    }

    [[nodiscard]] static auto is_scalar_primitive_typeref(const TypeRef& t) -> bool {
        return !t.is_slice && !t.is_array && !t.is_map && !t.is_ptr && !t.is_mut_ptr &&
               t.smart_wrap.empty() && !t.is_tuple && !t.is_fn && !t.is_optional && !t.is_fallible &&
               is_scalar_primitive_type_name(t.name);
    }

    SemaFnChecker(std::string_view path, const FnDecl& fn, const CheckedFile& cf,
                  std::vector<std::string>& warnings, std::vector<std::string>& errors,
                  std::vector<cfire::diag::StructuredDiagnostic>& diagnostics, bool resilient = false)
        : path_(path), pkg_name_(fn.package_name), fn_name_(fn.name), fn_ret_str_(format_type_ref_sema(fn.return_type)),
          cf_(cf), warnings_(warnings), errors_(errors), diags_(diagnostics), resilient_(resilient),
          fn_is_fallible_(fn.return_type.is_fallible || fn.name == "main" || fn.name.starts_with("__cfire_test_"))
    {
        for (const auto& imp : cf_.ast.imports) {
            if (imp.is_cpp) {
                has_cpp_imports_ = true;
                break;
            }
        }
        if (fn.receiver) {
            (void)check_type_ref_separators(fn.receiver->type, fn.receiver->line, fn.receiver->col);  // recorded by emit_error
            param_names_.insert(fn.receiver->name);
            var_scope_depth_[fn.receiver->name] = 0;
            if (!fn.receiver->type.name.empty()) {
                var_types_[fn.receiver->name] = format_type_ref_sema(fn.receiver->type);
            }
            if (fn.receiver->has_explicit_in) {
                (void)emit_error(diag_at(fn.receiver->line, fn.receiver->col, "cfire/DEPRECATED-IN-PARAM", std::format("explicit 'in' receiver intent is deprecated; omit 'in'")));
            }
            if (fn.receiver->has_deprecated_mut_pos) {
                (void)emit_error(diag_at(fn.receiver->line, fn.receiver->col, "cfire/DEPRECATED-INTENT-POS", std::format("'(s: mut T)' receiver placement is deprecated; use '(mut s: T)' instead")));
            }
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
            (void)check_type_ref_separators(p.type, p.line, p.col);  // recorded by emit_error
            param_names_.insert(p.name);
            var_scope_depth_[p.name] = 0;
            if (!p.type.smart_wrap.empty() && p.type.elem_type && !p.type.elem_type->name.empty()) {
                var_types_[p.name] = p.type.elem_type->name;
            } else if (!p.type.name.empty()) {
                var_types_[p.name] = format_type_ref_sema(p.type);
            }
            if (p.type.is_optional) {
                optional_vars_.insert(p.name);
            }
            if (p.type.is_fallible) {
                fallible_vars_.insert(p.name);
            }
            if (is_scalar_primitive_typeref(p.type)) {
                scalar_primitive_vars_.insert(p.name);
            }
            if (p.intent == "move") {
                move_params_.insert(p.name);
            }
            if (p.has_explicit_in) {
                (void)emit_error(diag_at(p.line, p.col, "cfire/DEPRECATED-IN-PARAM", std::format("explicit 'in' parameter intent is deprecated; omit 'in' (parameters are immutable by default)")));
            }
            if (p.has_deprecated_mut_pos) {
                (void)emit_error(diag_at(p.line, p.col, "cfire/DEPRECATED-INTENT-POS", std::format("'x: mut T' parameter placement is deprecated; use 'mut x: T' instead")));
            }
            if (p.intent == "mut") {
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
            if (p.type.is_optional) {
                optional_vars_.insert(p.name);
            }
            if (p.type.is_fallible) {
                fallible_vars_.insert(p.name);
            }
            if (p.type.is_fn && p.type.fn_ret) {
                if (p.type.fn_ret->is_fallible) fn_ret_fallible_vars_.insert(p.name);
                if (p.type.fn_ret->is_optional) fn_ret_optional_vars_.insert(p.name);
            }
            if (p.type.is_ptr || p.type.is_mut_ptr || !p.type.smart_wrap.empty()) {
                raw_or_smart_ptr_vars_.insert(p.name);
            }
            if (p.type.is_ptr || p.type.is_mut_ptr || !p.type.smart_wrap.empty() || p.type.is_optional) {
                ptr_or_smart_vars_.insert(p.name);
                ptr_null_state_[p.name] = p.type.is_optional ? NullState::MaybeNull : NullState::NonNull;
            }
            if (!p.type.smart_wrap.empty()) {
                smart_ptr_vars_.insert(p.name);
            }
            if ((p.type.is_slice || p.type.is_array) && p.type.name == "byte") {
                byte_slice_vars_.insert(p.name);
            }
            std::string pt = p.type.raw_name.empty() ? p.type.name : p.type.raw_name;
            auto dot = pt.find('.');
            if (dot != std::string::npos) {
                std::string qual = pt.substr(0, dot);
                if (qual == "cpp" || is_cpp_qualifier_sema(qual) || is_vendor_alias_sema(qual)) {
                    foreign_vars_.insert(p.name);
                }
            }
        }
        if (!fn.return_type.name.empty()) {
            (void)check_type_ref_separators(fn.return_type, fn.line, 0);  // recorded by emit_error
        }
    }

    SemaFnChecker(std::string_view path, const FnDecl& fn, const CheckedFile& cf, std::vector<std::string>& warnings)
        : SemaFnChecker(path, fn, cf, warnings, fallback_errors_, fallback_diags_, false) {}

    std::unordered_map<std::string, uint64_t> last_read_expr_id{};
    std::unordered_set<std::string> read_in_loop_{};

    // Q24 (CF-149): diagnostics are recorded structurally where they are detected. In non-resilient mode the
    // rendered text is also returned so callers can stop early.
    [[nodiscard]] auto emit_error(cfire::diag::StructuredDiagnostic d) -> std::expected<void, std::string> {
        d.severity = "error";
        std::string text = cfire::diag::render_diagnostic(d);
        errors_.push_back(text);
        diags_.push_back(std::move(d));
        if (resilient_) return {};
        return std::unexpected(std::move(text));
    }
    void emit_warning(cfire::diag::StructuredDiagnostic d) {
        d.severity = "warning";
        warnings_.push_back(cfire::diag::render_diagnostic(d));
        diags_.push_back(std::move(d));
    }
    [[nodiscard]] auto diag_at(int line, int col, std::string_view code, std::string msg,
                               std::unordered_map<std::string, std::string> facts = {}) const -> cfire::diag::StructuredDiagnostic {
        return cfire::diag::diag(path_, line, col, code, std::move(msg), std::move(facts));
    }

    void invalidate_loans_for_owner(const std::string& owner, std::string_view reason, int line) {
        for (const auto& [borrower, orig] : borrow_origin_) {
            if (orig == owner) {
                invalidated_loans_[borrower] = InvalidatedLoan{std::string(reason), line};
            }
        }
        // CF-144: a borrower can depend on several owners (a call returning a view of either argument,
        // or a struct literal holding pointers into different containers).
        for (const auto& [borrower, owners] : extra_borrow_origins_) {
            if (std::find(owners.begin(), owners.end(), owner) != owners.end()) {
                invalidated_loans_[borrower] = InvalidatedLoan{std::string(reason), line};
            }
        }
    }

    // Records every owner `e` borrows from onto `name` (the first stays in borrow_origin_ for existing checks).
    void record_borrow_origins(const std::string& name, const ExprPtr& e) {
        auto owners = extract_borrow_sources(e);
        if (owners.size() > 1) {
            extra_borrow_origins_[name] = std::vector<std::string>(owners.begin() + 1, owners.end());
        } else {
            extra_borrow_origins_.erase(name);
        }
    }

    // CF-144 (A3): does `call` produce a temporary owned value (as opposed to a view/pointer into storage
    // that outlives the expression)? Unknown callees are treated as owning: cfire values are owned unless
    // a signature says `&T` / `*T`.
    [[nodiscard]] auto call_returns_owned_temporary(const CallExpr& call) const -> bool {
        if (const auto* cid = std::get_if<IdentExpr>(&call.callee->node)) {
            const FnDecl* sig = nullptr;
            if (!pkg_name_.empty()) {
                if (auto pit = cf_.fn_sigs.find(pkg_name_ + "." + cid->name); pit != cf_.fn_sigs.end()) sig = &pit->second;
            }
            if (!sig) {
                if (auto it = cf_.fn_sigs.find(cid->name); it != cf_.fn_sigs.end()) sig = &it->second;
            }
            if (sig) return !returns_view_type(sig->return_type);
            return cid->name != "cpp";
        }
        if (const auto* mem = std::get_if<MemberExpr>(&call.callee->node)) {
            if (const auto* sig = resolve_member_call_sig(*mem)) return !returns_view_type(sig->return_type);
            const auto* obj_id = mem->object ? std::get_if<IdentExpr>(&mem->object->node) : nullptr;
            const auto& idx = StdlibCatalogIndex::instance();
            if (obj_id && idx.has_namespace(obj_id->name) && !is_ident_in_scope_sema(obj_id->name)) {
                if (const auto* sfn = idx.find_ns_fn(obj_id->name, mem->field)) {
                    return !(sfn->return_type.starts_with("&") || sfn->return_type.starts_with("*"));
                }
                return false;  // unknown stdlib member: don't guess
            }
            // Builtin view-returning members borrow from their receiver, not from a temporary.
            if (mem->field == "data" || mem->field == "data_ptr" || mem->field == "byte_ptr" || mem->field == "byte_ptr_at") return false;
            return false;
        }
        return false;
    }

    // If `e` creates a view or pointer (`x[a..b]`, `&x`, `x.data()`) whose base object is a temporary owned
    // value, the view dangles at the end of the full expression once it is stored (CF-144 A3).
    [[nodiscard]] auto view_of_temporary(const ExprPtr& e) const -> bool {
        if (!e) return false;
        ExprPtr base = nullptr;
        if (const auto* sl = std::get_if<SliceExpr>(&e->node)) {
            base = sl->object;
        } else if (const auto* un = std::get_if<UnaryExpr>(&e->node); un && (un->op == "&" || un->op == "&mut")) {
            base = un->operand;
        } else if (const auto* call = std::get_if<CallExpr>(&e->node)) {
            if (const auto* mem = std::get_if<MemberExpr>(&call->callee->node);
                mem && (mem->field == "data" || mem->field == "data_ptr" || mem->field == "byte_ptr" || mem->field == "byte_ptr_at")) {
                base = mem->object;
            }
        }
        while (base) {
            if (const auto* m = std::get_if<MemberExpr>(&base->node); m && !m->is_namespace) { base = m->object; continue; }
            if (const auto* ix = std::get_if<IndexExpr>(&base->node)) { base = ix->object; continue; }
            if (const auto* sl = std::get_if<SliceExpr>(&base->node)) { base = sl->object; continue; }
            break;
        }
        if (!base) return false;
        if (const auto* call = std::get_if<CallExpr>(&base->node)) return call_returns_owned_temporary(*call);
        return std::holds_alternative<StructLitExpr>(base->node) || std::holds_alternative<ArrayLitExpr>(base->node);
    }

    [[nodiscard]] auto check_view_of_temporary(const ExprPtr& e, std::string_view target, int line, int col)
        -> std::expected<void, std::string> {
        if (!view_of_temporary(e)) return {};
        return emit_error(diag_at(line, col, "cfire/DANGLING-TEMPORARY", std::format("{} borrows from a temporary value that is destroyed at the end of this statement; bind the owner to a variable first (e.g. 'owner := f()' then 'owner[a..b]')", target)));
    }

    [[nodiscard]] static auto returns_view_type(const TypeRef& t) -> bool {
        return t.is_borrow || t.is_mut_borrow || t.is_ptr || t.is_mut_ptr;
    }

    // CF-144: all owners an expression's value borrows from.
    //  - `&x`, `x[a..b]`, `x.data()`: the root owner of x (existing rule, see extract_borrow_source)
    //  - a call whose declared return type is a view or pointer borrows from its receiver and from every
    //    argument that is (or borrows from) a local path: `h := head(s)` keeps `s` loaned while `h` lives
    //  - a struct literal borrows from every owner its field values borrow from
    [[nodiscard]] auto extract_borrow_sources(const ExprPtr& e) const -> std::vector<std::string> {
        std::vector<std::string> out;
        auto add = [&](const std::string& o) {
            if (!o.empty() && std::find(out.begin(), out.end(), o) == out.end()) out.push_back(o);
        };
        if (!e) return out;
        if (auto direct = extract_borrow_source(e)) {
            add(*direct);
            return out;
        }
        if (const auto* call = std::get_if<CallExpr>(&e->node)) {
            const FnDecl* sig = nullptr;
            ExprPtr receiver = nullptr;
            if (const auto* cid = std::get_if<IdentExpr>(&call->callee->node)) {
                if (!pkg_name_.empty()) {
                    if (auto pit = cf_.fn_sigs.find(pkg_name_ + "." + cid->name); pit != cf_.fn_sigs.end()) sig = &pit->second;
                }
                if (!sig) {
                    if (auto it = cf_.fn_sigs.find(cid->name); it != cf_.fn_sigs.end()) sig = &it->second;
                }
            } else if (const auto* mem = std::get_if<MemberExpr>(&call->callee->node)) {
                sig = resolve_member_call_sig(*mem);
                receiver = mem->object;
            }
            if (sig && returns_view_type(sig->return_type)) {
                auto from_arg = [&](const ExprPtr& a) {
                    if (auto b = extract_borrow_source(a)) {
                        add(*b);
                    } else if (auto pi = extract_path_info(a)) {
                        add(resolve_root_owner(pi->root));
                    }
                };
                if (receiver && sig->receiver) from_arg(receiver);
                for (const auto& a : call->args) from_arg(a.value);
            }
        } else if (const auto* st = std::get_if<StructLitExpr>(&e->node)) {
            for (const auto& f : st->fields) {
                for (auto& o : extract_borrow_sources(f.value)) add(o);
            }
        }
        return out;
    }

    [[nodiscard]] auto check_not_iterated_collection(const std::string& root, int line, int col)
        -> std::expected<void, std::string>
    {
        if (active_loop_collections_.contains(root)) {
            return emit_error(diag_at(line, col, "cfire/ITERATOR-INVALIDATION", std::format("cannot structurally mutate collection '{}' while iterating over it", root)));
        }
        return {};
    }

    [[nodiscard]] auto record_var_read(const std::string& name, int line, int col, uint64_t expr_id = 0)
        -> std::expected<void, std::string>
    {
        if (auto it = moved_at_line_.find(name); it != moved_at_line_.end()) {
            return emit_error(diag_at(line, col, "cfire/USE-AFTER-MOVE", std::format("use of moved variable '{}' (previously moved at line {})", name, it->second)));
        }
        if (auto it = invalidated_loans_.find(name); it != invalidated_loans_.end()) {
            return emit_error(diag_at(line, col, "cfire/USE-AFTER-FREE", std::format("use of dangling pointer/view '{}' ({})", name, it->second.reason)));
        }
        if (move_params_.contains(name)) {
            if (loop_depth_ == 0) {
                if (!read_in_loop_.contains(name)) {
                    last_read_expr_id[name] = expr_id;
                }
            } else {
                read_in_loop_.insert(name);
                last_read_expr_id.erase(name);
            }
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
                // W4: view-returning builtin methods (`s.trim()`, `s.strip_prefix(p)`) borrow from their receiver.
                if (const BuiltinMethod* bm = lookup_builtin_method(*mem); bm && bm->borrows) {
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
            return ptr_or_smart_vars_.contains(id->name) || smart_ptr_vars_.contains(id->name) || raw_or_smart_ptr_vars_.contains(id->name);
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
            }
            // Float division by zero is IEEE-defined (±inf / NaN), so only integer zero counts (CF-165 corpus).
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

    // SQL statements reach `.exec` / `.query` on SQLite connections, statements and transactions.
    [[nodiscard]] auto is_sql_receiver(const ExprPtr& obj) const -> bool {
        std::string t = type_name_of(obj);
        while (!t.empty() && (t.front() == '&' || t.front() == '*')) t.erase(t.begin());
        if (t.starts_with("mut ")) t.erase(0, 4);
        if (t.starts_with("sqlite.") || t.starts_with("sqlite::") || t.starts_with("cfire::sqlite")) return true;
        if (t.empty() || t == "Conn" || t == "Db" || t == "Tx" || t == "Stmt") return is_stdlib_ns_imported("sqlite");
        return false;
    }

    // CF-167: is `e` a string the program fully controls? Literals without interpolation, concatenations of
    // constants, and locals whose every assignment was constant. Anything else (parameters, call results,
    // interpolated or concatenated runtime data) may carry external input into a shell or SQL statement.
    [[nodiscard]] auto is_constant_string_expr(const ExprPtr& e) const -> bool {
        if (!e) return false;
        if (const auto* lit = std::get_if<LiteralExpr>(&e->node)) {
            return lit->kind == TokenKind::String && !is_dynamic_string_expr(e);
        }
        if (const auto* bin = std::get_if<BinaryExpr>(&e->node); bin && bin->op == "+") {
            return is_constant_string_expr(bin->left) && is_constant_string_expr(bin->right);
        }
        if (const auto* id = std::get_if<IdentExpr>(&e->node)) {
            return const_string_vars_.contains(id->name);
        }
        return false;
    }

    [[nodiscard]] auto is_cpp_qualifier_sema(std::string_view name) const -> bool {
        if (cf_.shadowed_default_qualifiers.contains(std::string(name))) return false;
        if (name == "cpp") return true;
        return cf_.cpp_qualifiers.contains(std::string(name));
    }

    [[nodiscard]] auto is_vendor_alias_sema(std::string_view name) const -> bool {
        if (cf_.vendor_aliases.contains(std::string(name))) return true;
        for (const auto& imp : cf_.ast.imports) {
            if (imp.alias == name && imp.path.starts_with("vendor:")) return true;
        }
        return false;
    }

    [[nodiscard]] auto resolve_stdlib_ns_sema(std::string_view qualifier) const -> std::string {
        if (cf_.shadowed_default_qualifiers.contains(std::string(qualifier))) return "";
        if (auto it = cf_.core_aliases.find(std::string(qualifier)); it != cf_.core_aliases.end()) {
            return it->second;
        }
        if (StdlibCatalogIndex::instance().has_namespace(qualifier)) {
            return std::string(qualifier);
        }
        return "";
    }

    [[nodiscard]] auto is_namespace_ident_sema(std::string_view name) const -> bool {
        if (cf_.shadowed_default_qualifiers.contains(std::string(name))) return false;
        if (name == "cpp") return true;
        if (is_cpp_qualifier_sema(name)) return true;
        if (is_vendor_alias_sema(name)) return true;
        if (cf_.core_aliases.contains(std::string(name))) return true;
        if (StdlibCatalogIndex::instance().has_namespace(name)) return true;
        for (const auto& imp : cf_.ast.imports) {
            if (!imp.alias.empty() && imp.alias == name) return true;
            auto slash = imp.path.rfind('/');
            std::string_view tail = (slash == std::string::npos) ? std::string_view(imp.path) : std::string_view(imp.path).substr(slash + 1);
            if (tail == name) return true;
        }
        for (const auto& fn : cf_.ast.functions) {
            if (!fn.package_name.empty() && fn.package_name == name) return true;
        }
        for (const auto& st : cf_.ast.structs) {
            if (!st.package_name.empty() && st.package_name == name) return true;
        }
        for (const auto& en : cf_.ast.enums) {
            if (!en.package_name.empty() && en.package_name == name) return true;
        }
        for (const auto& gv : cf_.ast.global_vars) {
            if (!gv.package_name.empty() && gv.package_name == name) return true;
        }
        return false;
    }

    [[nodiscard]] static auto is_common_stdlib_method_sema(std::string_view name) -> bool {
        return name == "open" || name == "close" || name == "fd" || name == "is_open" || name == "is_tty" ||
               name == "poll" || name == "read" || name == "read_into" || name == "read_bytes" ||
               name == "read_exact" || name == "read_text" || name == "read_bytes_until" ||
               name == "read_text_until" || name == "read_str" || name == "read_line" ||
               name == "read_lines" || name == "read_all" || name == "write" ||
               name == "write_text" || name == "write_str" || name == "write_bytes" ||
               name == "flush" || name == "seek" ||
               name == "tell" || name == "size" || name == "len" || name == "empty" ||
               name == "clear" || name == "push" || name == "pop" || name == "get" || name == "set" ||
               name == "contains" || name == "keys" || name == "values" || name == "erase" ||
               name == "remove" || name == "send" || name == "recv" || name == "try_recv" ||
               name == "recv_timeout" || name == "bind" || name == "step" || name == "exec" ||
               name == "query" || name == "prepare" || name == "begin" || name == "commit" ||
               name == "rollback" || name == "join" || name == "detach" || name == "lock" ||
               name == "unlock" || name == "reset" || name == "has_value" || name == "value_or" ||
               name == "value" || name == "parse_i64" || name == "parse_i32" || name == "parse_u64" ||
               name == "parse_f64" || name == "parse_bool";
    }

    [[nodiscard]] auto resolve_member_call_sig(const MemberExpr& mem) const -> const FnDecl* {
        const auto* obj_id = mem.object ? std::get_if<IdentExpr>(&mem.object->node) : nullptr;
        if (obj_id && is_namespace_ident_sema(obj_id->name) && !is_ident_in_scope_sema(obj_id->name)) {
            if (auto it = cf_.fn_sigs.find(obj_id->name + "." + mem.field); it != cf_.fn_sigs.end()) {
                return &it->second;
            }
            return nullptr;
        }
        if (obj_id) {
            if (auto vt = var_types_.find(obj_id->name); vt != var_types_.end()) {
                if (auto mit = cf_.methods.find(vt->second + "::" + mem.field); mit != cf_.methods.end()) {
                    return &mit->second;
                }
            }
        }
        if (auto owner_it = cf_.method_owner.find(mem.field); owner_it != cf_.method_owner.end()) {
            auto owners_it = cf_.method_owners.find(mem.field);
            if (owners_it != cf_.method_owners.end() && owners_it->second.size() == 1 &&
                !is_common_stdlib_method_sema(mem.field)) {
                if (auto mit = cf_.methods.find(owner_it->second + "::" + mem.field); mit != cf_.methods.end()) {
                    return &mit->second;
                }
            }
        }
        return nullptr;
    }

    [[nodiscard]] auto expr_returns_optional_sema(const ExprPtr& e) const -> bool {
        if (!e) return false;
        if (const auto* id = std::get_if<IdentExpr>(&e->node)) {
            return optional_vars_.contains(id->name);
        }
        if (const auto* lit = std::get_if<LiteralExpr>(&e->node)) {
            return lit->kind == TokenKind::KwNone;
        }
        if (const auto* tr = std::get_if<TryExpr>(&e->node)) {
            return expr_returns_optional_sema(tr->inner);
        }
        if (const auto* or_ex = std::get_if<OrExpr>(&e->node)) {
            if (or_ex->fallback_val) {
                return expr_returns_optional_sema(or_ex->fallback_val);
            }
            return false;
        }
        if (const auto* mem = std::get_if<MemberExpr>(&e->node)) {
            if (!mem->is_namespace && mem->object) {
                std::string owner_t = type_name_of(mem->object);
                if (!owner_t.empty()) {
                    for (const auto& st : cf_.ast.structs) {
                        if (st.name == owner_t) {
                            for (const auto& fld : st.fields) {
                                if (fld.name == mem->field) {
                                    return fld.type.is_optional && !fld.type.is_fallible;
                                }
                            }
                        }
                    }
                }
            }
        }
        if (const auto* call = std::get_if<CallExpr>(&e->node)) {
            if (const auto* mem = std::get_if<MemberExpr>(&call->callee->node)) {
                if (mem->object && (mem->field == "from_i32" || mem->field == "from_i64" || mem->field == "from_string")) {
                    if (const auto* id = std::get_if<IdentExpr>(&mem->object->node)) {
                        if (cf_.enums.contains(id->name)) return true;
                    } else if (const auto* sub_mem = std::get_if<MemberExpr>(&mem->object->node)) {
                        if (cf_.enums.contains(sub_mem->field)) return true;
                    }
                }
            }
            if (const auto* cid = std::get_if<IdentExpr>(&call->callee->node)) {
                if (cid->name == "some" || cid->name == "readln" || cid->name == "input") {
                    return true;
                }
                if (cid->name.ends_with("::from_i32") || cid->name.ends_with("::from_i64") || cid->name.ends_with("::from_string")) {
                    return true;
                }
                if (fn_ret_optional_vars_.contains(cid->name)) {
                    return true;
                }
                if (!pkg_name_.empty()) {
                    if (auto pit = cf_.fn_sigs.find(pkg_name_ + "." + cid->name); pit != cf_.fn_sigs.end()) {
                        return pit->second.return_type.is_optional && !pit->second.return_type.is_fallible;
                    }
                }
                if (auto it = cf_.fn_sigs.find(cid->name); it != cf_.fn_sigs.end()) {
                    return it->second.return_type.is_optional && !it->second.return_type.is_fallible;
                }
            } else if (const auto* mem = std::get_if<MemberExpr>(&call->callee->node)) {
                if (const BuiltinMethod* bm = lookup_builtin_method(*mem); bm && !bm->ret.empty()) return bm->ret.ends_with('?');  // W4 table
                if (mem->field == "parse" && primitive_type_prefix(mem->object).is_valid()) return true;  // Q10: T.parse(s) -> T?
                if (mem->field == "try_as" && call->type_args.size() == 1) return true;  // Q9: x.try_as[T]() -> T?
                if (mem->field == "checked_shl" || mem->field == "checked_shr") return true;  // Q11
                if (mem->field == "from_i32" || mem->field == "from_i64" || mem->field == "from_string") {
                    if (mem->object) {
                        if (const auto* id = std::get_if<IdentExpr>(&mem->object->node)) {
                            if (cf_.enums.contains(id->name)) return true;
                        } else if (const auto* sub_mem = std::get_if<MemberExpr>(&mem->object->node)) {
                            if (cf_.enums.contains(sub_mem->field)) return true;
                        }
                    }
                }
                const auto& idx = StdlibCatalogIndex::instance();
                if (const auto* recv_id = std::get_if<IdentExpr>(&mem->object->node)) {
                    if ((is_namespace_ident_sema(recv_id->name) || idx.has_namespace(recv_id->name)) &&
                        !is_ident_in_scope_sema(recv_id->name)) {
                        if (const auto* sfn = idx.find_ns_fn(recv_id->name, mem->field)) {
                            if (sfn->is_optional) return true;
                        }
                        if (recv_id->name == "os" && mem->field == "getenv_opt") {
                            return true;
                        }
                        if (recv_id->name == "str" && (
                            mem->field == "find" ||
                            mem->field == "parse_i64" || mem->field == "parse_i32" || mem->field == "parse_u64" ||
                            mem->field == "parse_f64" || mem->field == "parse_bool")) {
                            return true;
                        }
                        if (recv_id->name == "cli" &&
                            (mem->field == "try_parse" || mem->field == "prompt" || mem->field == "prompt_password")) {
                            return true;
                        }
                        if (const auto* sig = resolve_member_call_sig(*mem)) {
                            return sig->return_type.is_optional && !sig->return_type.is_fallible;
                        }
                        return false;
                    }
                }
                if (mem->field == "parse_i64" || mem->field == "parse_i32" || mem->field == "parse_u64" ||
                    mem->field == "parse_f64" || mem->field == "parse_bool" ||
                    mem->field == "try_recv" || mem->field == "recv_timeout" ||
                    mem->field == "getenv_opt" || mem->field == "read_line") {
                    return true;
                }
                if (mem->field == "get" && call->args.size() == 1 && !call->block_arg) {
                    return true;
                }
                if (mem->field == "recv" && call->args.empty()) {
                    return true;
                }
                if (const auto* sig = resolve_member_call_sig(*mem)) {
                    return sig->return_type.is_optional && !sig->return_type.is_fallible;
                }
            }
        }
        return false;
    }

    // Q23 (CF-148): the former string-based inference, now producing TypeIds. Only reached as the fallback
    // of infer_expr_type_id_sema; names for messages come from type_name_of().
    [[nodiscard]] auto infer_named_type_id_(const ExprPtr& e) const -> TypeId {
        if (!e) return TypeId::Unknown();
        if (const auto* lit = std::get_if<LiteralExpr>(&e->node)) {
            if (lit->kind == TokenKind::String) return type_id_of("str");
            if (lit->kind == TokenKind::Int) return type_id_of("i64");
            if (lit->kind == TokenKind::Float) return type_id_of("f64");
            if (lit->kind == TokenKind::Char) return TypeId::Byte();
            if (lit->kind == TokenKind::KwTrue || lit->kind == TokenKind::KwFalse) return type_id_of("bool");
        }
        if (const auto* st = std::get_if<StructLitExpr>(&e->node)) {
            return type_id_of(st->type_name);
        }
        if (const auto* tr = std::get_if<TryExpr>(&e->node)) {
            return type_id_of(type_name_of(tr->inner));
        }
        if (const auto* or_ex = std::get_if<OrExpr>(&e->node)) {
            return type_id_of(type_name_of(or_ex->inner));
        }
        if (const auto* id = std::get_if<IdentExpr>(&e->node)) {
            if (auto it = var_types_.find(id->name); it != var_types_.end()) {
                return type_id_of(it->second);
            }
            if (auto vit = cf_.variant_to_enum.find(id->name); vit != cf_.variant_to_enum.end()) {
                return type_id_of(vit->second);
            }
            if (auto pos = id->name.find("::"); pos != std::string::npos) {
                std::string prefix = id->name.substr(0, pos);
                if (cf_.enums.contains(prefix)) return type_id_of(prefix);
            }
            if (auto pos = id->name.find('.'); pos != std::string::npos) {
                std::string prefix = id->name.substr(0, pos);
                if (cf_.enums.contains(prefix)) return type_id_of(prefix);
            }
        }
        if (const auto* mem = std::get_if<MemberExpr>(&e->node)) {
            if (mem->object) {
                if (const auto* id = std::get_if<IdentExpr>(&mem->object->node)) {
                    if (cf_.enums.contains(id->name)) return type_id_of(id->name);
                } else if (const auto* sub_mem = std::get_if<MemberExpr>(&mem->object->node)) {
                    if (cf_.enums.contains(sub_mem->field)) return type_id_of(sub_mem->field);
                }
            }
            if (!mem->is_namespace && mem->object) {
                std::string owner_t = type_name_of(mem->object);
                if (!owner_t.empty()) {
                    if (auto sit = cf_.struct_tags.find(owner_t); sit != cf_.struct_tags.end()) {
                        for (const auto& f : sit->second) {
                            if (f.field_name == mem->field) {
                                return type_id_of(f.field_type.name);
                            }
                        }
                    }
                }
            }
        }
        if (const auto* call = std::get_if<CallExpr>(&e->node)) {
            if (const auto* cm = std::get_if<MemberExpr>(&call->callee->node);
                cm && call->type_args.size() == 1 &&
                (cm->field == "wrapping_as" || cm->field == "saturating_as" || cm->field == "try_as")) {
                return type_id_of(call->type_args[0].name);  // optional-ness of try_as is tracked by expr_returns_optional_sema
            }
            if (const auto* cid = std::get_if<IdentExpr>(&call->callee->node)) {
                if (auto pos = cid->name.find("::"); pos != std::string::npos) {
                    std::string prefix = cid->name.substr(0, pos);
                    if (cf_.enums.contains(prefix)) return type_id_of(prefix);
                }
                if (auto pos = cid->name.find('.'); pos != std::string::npos) {
                    std::string prefix = cid->name.substr(0, pos);
                    if (cf_.enums.contains(prefix)) return type_id_of(prefix);
                }
                if (auto vit = cf_.variant_to_enum.find(cid->name); vit != cf_.variant_to_enum.end()) {
                    return type_id_of(vit->second);
                }
                if (cid->name == "make" && call->type_args.size() == 1) {
                    // Q25: `make(http.Server, ...)` has the constructor's declared type (`http.HttpServer`).
                    if (const auto* sc = find_stdlib_ctor(call->type_args[0].name); sc && !sc->generic) {
                        if (const auto* sig = StdlibCatalogIndex::instance().find_ns_fn(sc->ns, std::string(sc->type) + ".make")) {
                            if (!sig->unwrapped_return_type.empty()) return type_id_of(qualify_stdlib_type_sema(*sig, std::string(sc->ns)));
                        }
                    }
                    if (!call->type_args[0].smart_wrap.empty() && call->type_args[0].elem_type) {
                        return type_id_of(call->type_args[0].elem_type->name);
                    }
                    if (!call->type_args[0].name.empty()) {
                        return type_id_of(call->type_args[0].name);
                    }
                }
                if (cid->name == "sync.Mutex" || cid->name == "sync.make_mutex") {
                    if (!call->type_args.empty()) return type_id_of("sync.Mutex[" + call->type_args[0].name + "]");
                    if (!call->args.empty() && call->args[0].value) return type_id_of("sync.Mutex[" + type_name_of(call->args[0].value) + "]");
                    return type_id_of("sync.Mutex[void]");
                }
                if (cid->name == "sync.Atomic" || cid->name == "sync.make_atomic") {
                    if (!call->type_args.empty()) return type_id_of("sync.Atomic[" + call->type_args[0].name + "]");
                    if (!call->args.empty() && call->args[0].value) return type_id_of("sync.Atomic[" + type_name_of(call->args[0].value) + "]");
                    return type_id_of("sync.Atomic[i64]");
                }
                if (cid->name == "[]byte" || cid->name == "[]u8" || cid->name == "str" ||
                    cid->name == "i8" || cid->name == "i16" || cid->name == "i32" || cid->name == "i64" ||
                    cid->name == "u8" || cid->name == "byte" || cid->name == "u16" || cid->name == "u32" ||
                    cid->name == "u64" || cid->name == "usize" || cid->name == "isize" ||
                    cid->name == "f32" || cid->name == "f64" || cid->name == "bool") {
                    return type_id_of(cid->name);
                }
                if (!pkg_name_.empty()) {
                    if (auto pit = cf_.fn_sigs.find(pkg_name_ + "." + cid->name); pit != cf_.fn_sigs.end()) {
                        if (!pit->second.return_type.smart_wrap.empty() && pit->second.return_type.elem_type) {
                            return type_id_of(pit->second.return_type.elem_type->name);
                        }
                        return type_id_of(pit->second.return_type.name);
                    }
                }
                if (auto it = cf_.fn_sigs.find(cid->name); it != cf_.fn_sigs.end()) {
                    if (!it->second.return_type.smart_wrap.empty() && it->second.return_type.elem_type) {
                        return type_id_of(it->second.return_type.elem_type->name);
                    }
                    return type_id_of(it->second.return_type.name);
                }
            } else if (const auto* mem = std::get_if<MemberExpr>(&call->callee->node)) {
                if (mem->object) {
                    if (const auto* id = std::get_if<IdentExpr>(&mem->object->node)) {
                        if (cf_.enums.contains(id->name)) return type_id_of(id->name);
                    } else if (const auto* sub_mem = std::get_if<MemberExpr>(&mem->object->node)) {
                        if (cf_.enums.contains(sub_mem->field)) return type_id_of(sub_mem->field);
                    }
                }
                if (const auto* recv_id = std::get_if<IdentExpr>(&mem->object->node)) {
                    if (recv_id->name == "os" && (mem->field == "getenv" || mem->field == "getenv_opt" || mem->field == "cwd" || mem->field == "temp_dir")) {
                        return type_id_of("str");
                    }
                    if (recv_id->name == "fs" && mem->field == "read_text") {
                        return type_id_of("str");
                    }
                    if ((recv_id->name == "fs" && mem->field == "read_bytes") ||
                        (recv_id->name == "hex" && mem->field == "decode") ||
                        (recv_id->name == "base64" && mem->field == "decode") ||
                        (recv_id->name == "rand" && mem->field == "bytes") ||
                        (recv_id->name == "sodium" && (mem->field == "random_bytes" || mem->field == "argon2id" || mem->field == "blake2b" || mem->field == "derive_subkey" || mem->field == "xchacha20poly1305_encrypt" || mem->field == "xchacha20poly1305_decrypt"))) {
                        return type_id_of("[]byte");
                    }
                    if (const auto* std_sig = StdlibCatalogIndex::instance().find_ns_fn(recv_id->name, mem->field)) {
                        if (std_sig->unwrapped_return_type == "[]byte") return type_id_of("[]byte");
                        if (!std_sig->unwrapped_return_type.empty() && std_sig->unwrapped_return_type != "void" &&
                            !std_sig->unwrapped_return_type.starts_with('[') && !std_sig->unwrapped_return_type.starts_with('&') &&
                            !std_sig->unwrapped_return_type.starts_with("map[") && !std_sig->unwrapped_return_type.starts_with("own[") &&
                            !std_sig->unwrapped_return_type.starts_with("shared[") && !std_sig->unwrapped_return_type.starts_with("tuple[") &&
                            !std_sig->unwrapped_return_type.starts_with('(') &&
                            std_sig->unwrapped_return_type != "T") {
                            std::string ret_type = qualify_stdlib_type_sema(*std_sig, recv_id->name);
                            if (!call->type_args.empty()) {
                                std::string targ = call->type_args[0].name;
                                if (auto p = ret_type.find("[T]"); p != std::string::npos && !targ.empty()) {
                                    ret_type.replace(p, 3, "[" + targ + "]");
                                }
                            }
                            return type_id_of(ret_type);
                        }
                    }
                    if (!is_ident_in_scope_sema(recv_id->name)) {
                        if (auto fit = cf_.fn_sigs.find(mem->field); fit != cf_.fn_sigs.end()) {
                            return type_id_of(fit->second.return_type.name);
                        }
                    }
                } else if (mem->object) {
                    if (const auto* sub_mem = std::get_if<MemberExpr>(&mem->object->node)) {
                        if (const auto* sub_id = std::get_if<IdentExpr>(&sub_mem->object->node)) {
                            if (const auto* std_sig = StdlibCatalogIndex::instance().find_ns_fn(sub_id->name, sub_mem->field + "." + mem->field)) {
                                if (std_sig->unwrapped_return_type == "[]byte") return type_id_of("[]byte");
                                if (!std_sig->unwrapped_return_type.empty() && std_sig->unwrapped_return_type != "void") {
                                    return type_id_of(qualify_stdlib_type_sema(*std_sig, sub_id->name));
                                }
                            }
                        }
                    }
                }
                if (mem->field == "bytes" && call->args.empty()) {
                    return type_id_of("[]byte");
                }
                if (mem->field == "ocr" || mem->field == "detect_language") {
                    return type_id_of("str");
                }
                if ((mem->field == "parse" || mem->field == "generate_struct" ||
                     mem->field == "generate_struct_from_file" || mem->field == "run_struct" ||
                     mem->field == "run_struct_validated") && call->type_args.size() == 1 &&
                    !call->type_args[0].name.empty()) {
                    return type_id_of(call->type_args[0].name);
                }
                if (const auto* sig = resolve_member_call_sig(*mem)) {
                    if (sig->return_type.is_slice && sig->return_type.name == "byte") return type_id_of("[]byte");
                    return type_id_of(sig->return_type.name);
                }
                if (mem->object) {
                    std::string recv_t = type_name_of(mem->object);
                    if (!recv_t.empty()) {
                        const auto& idx = StdlibCatalogIndex::instance();
                        std::string base_recv = recv_t;
                        std::string generic_arg;
                        if (auto br = base_recv.find('['); br != std::string::npos && base_recv.ends_with(']')) {
                            generic_arg = base_recv.substr(br + 1, base_recv.size() - br - 2);
                            base_recv = base_recv.substr(0, br);
                        }
                        std::string unqual_recv = base_recv;
                        if (auto dot = unqual_recv.rfind('.'); dot != std::string::npos) {
                            unqual_recv = unqual_recv.substr(dot + 1);
                        }
                        if (auto dcol = unqual_recv.rfind("::"); dcol != std::string::npos) {
                            unqual_recv = unqual_recv.substr(dcol + 2);
                        }

                        auto find_ret = [&](const std::string& key) -> std::string {
                            if (auto mit = idx.receiver_methods.find(key); mit != idx.receiver_methods.end()) {
                                std::string uret = mit->second.unwrapped_return_type;
                                if (!generic_arg.empty() && uret == "T") {
                                    return generic_arg;
                                }
                                return uret;
                            }
                            return "";
                        };

                        if (base_recv == "async.Task" && mem->field == "join") {
                            return type_id_of(generic_arg.empty() ? "void" : generic_arg);
                        }
                        if (base_recv == "sync.Mutex") {
                            if (mem->field == "lock") return type_id_of("sync.MutexGuard[" + generic_arg + "]");
                            if (mem->field == "get") return type_id_of(generic_arg);
                            if (mem->field == "with_lock") {
                                if (call->block_arg && call->block_arg->return_type) return type_id_of(call->block_arg->return_type->name);
                                if (!call->args.empty() && call->args[0].value) {
                                    if (const auto* cl = std::get_if<ClosureExpr>(&call->args[0].value->node); cl && cl->return_type) {
                                        return type_id_of(cl->return_type->name);
                                    }
                                }
                                return type_id_of(generic_arg);
                            }
                        }
                        if (base_recv == "sync.MutexGuard" && mem->field == "get") {
                            return type_id_of(generic_arg);
                        }
                        if (base_recv == "sync.Atomic") {
                            if (mem->field == "load" || mem->field == "add" || mem->field == "sub" ||
                                mem->field == "fetch_add" || mem->field == "fetch_sub") {
                                return type_id_of(generic_arg.empty() ? "i64" : generic_arg);
                            }
                            if (mem->field == "cas") return type_id_of("bool");
                        }
                        if (mem->field == "spawn") {
                            std::string ret_t = "void";
                            if (!call->args.empty() && call->args[0].value) {
                                if (const auto* cl = std::get_if<ClosureExpr>(&call->args[0].value->node); cl && cl->return_type) {
                                    ret_t = cl->return_type->name;
                                }
                            }
                            if (call->block_arg && call->block_arg->return_type) ret_t = call->block_arg->return_type->name;
                            return type_id_of("async.Task[" + ret_t + "]");
                        }
                        if (mem->field == "len") {
                            if (unqual_recv == "Set" || unqual_recv == "Deque" || unqual_recv == "PriorityQueue" ||
                                unqual_recv == "BTreeMap" || unqual_recv == "BitSet") {
                                return type_id_of("i64");
                            }
                        }

                        if (std::string r = find_ret(recv_t + "." + mem->field); !r.empty()) return type_id_of(r);
                        if (std::string r = find_ret(base_recv + "." + mem->field); !r.empty()) return type_id_of(r);
                        if (std::string r = find_ret(unqual_recv + "." + mem->field); !r.empty()) return type_id_of(r);
                    }
                }
            }
        }
        // Receiver of unknown type (e.g. an untyped closure parameter `|req|`): if exactly one stdlib
        // receiver method has this name, its return type is unambiguous (CF-148).
        if (const auto* call = std::get_if<CallExpr>(&e->node)) {
            if (const auto* mem = std::get_if<MemberExpr>(&call->callee->node); mem && mem->object && !mem->is_namespace) {
                if (infer_expr_type_id_sema(mem->object).is_unknown()) {
                    std::string unique_ret;
                    bool ambiguous = false;
                    const std::string suffix = "." + mem->field;
                    for (const auto& [key, sig] : StdlibCatalogIndex::instance().receiver_methods) {
                        if (!key.ends_with(suffix) || key.find('.') != key.size() - suffix.size()) continue;
                        const std::string& r = sig.unwrapped_return_type;
                        if (r.empty() || r == "void" || r == "T" || r.find("T]") != std::string::npos) { ambiguous = true; break; }
                        if (unique_ret.empty()) unique_ret = r;
                        else if (unique_ret != r) { ambiguous = true; break; }
                    }
                    if (!ambiguous && !unique_ret.empty()) return type_id_of(unique_ret);
                }
            }
        }
        return TypeId::Unknown();
    }

    [[nodiscard]] auto infer_expr_type_id_sema(const ExprPtr& e) const -> TypeId {
        if (!e) return TypeId::Unknown();
        if (const auto* lit = std::get_if<LiteralExpr>(&e->node)) {
            if (lit->kind == TokenKind::String || lit->kind == TokenKind::RawString) return TypeId::Str();
            if (lit->kind == TokenKind::Char) return TypeId::Byte();
            if (lit->kind == TokenKind::Int) return TypeId::Int64();
            if (lit->kind == TokenKind::Float) return TypeId::Float64();
            if (lit->kind == TokenKind::KwTrue || lit->kind == TokenKind::KwFalse) return TypeId::Bool();
            return TypeId::Unknown();
        }
        if (const auto* un = std::get_if<UnaryExpr>(&e->node)) {
            TypeId op_t = infer_expr_type_id_sema(un->operand);
            if (un->op == "!" || un->op == "not") return TypeId::Bool();
            if (un->op == "&") return cf_.type_table.make_borrow(op_t);
            if (un->op == "&mut") return cf_.type_table.make_mut_borrow(op_t);
            if (un->op == "*") {
                TypeId ptr_t = op_t;
                if (cf_.type_table.get(ptr_t).kind == TypeKind::Optional) {
                    ptr_t = cf_.type_table.unwrap_optional(ptr_t);
                }
                const auto& d = cf_.type_table.get(ptr_t);
                if (d.elem_type.is_valid()) return d.elem_type;
            }
            return op_t;
        }
        if (const auto* bin = std::get_if<BinaryExpr>(&e->node)) {
            if (bin->op == "==" || bin->op == "!=" || bin->op == "<" || bin->op == "<=" ||
                bin->op == ">" || bin->op == ">=" || bin->op == "&&" || bin->op == "||") {
                return TypeId::Bool();
            }
            TypeId lt = infer_expr_type_id_sema(bin->left);
            TypeId rt = infer_expr_type_id_sema(bin->right);
            if (bin->op == "+" && (lt == TypeId::Str() || rt == TypeId::Str())) {
                return TypeId::Str();
            }
            // String concatenation always yields an owned `str`, even when both operands are `&str` views.
            if (bin->op == "+") {
                const TypeId str_borrow = type_id_of("&str");
                if (lt == str_borrow || rt == str_borrow) return TypeId::Str();
            }
            if (lt.is_valid() && !lt.is_unknown()) return lt;
            if (rt.is_valid() && !rt.is_unknown()) return rt;
            return TypeId::Unknown();
        }
        if (const auto* arr = std::get_if<ArrayLitExpr>(&e->node)) {
            // `[18, 26, 22]` is a slice of its first element's type (CF-148: no UNKNOWN-TYPE for literals).
            if (!arr->elements.empty()) {
                TypeId elem = infer_expr_type_id_sema(arr->elements.front());
                if (!elem.is_unknown()) return cf_.type_table.make_slice(elem);
            }
            return TypeId::Unknown();
        }
        if (const auto* sl = std::get_if<SliceExpr>(&e->node)) {
            TypeId obj_t = infer_expr_type_id_sema(sl->object);
            if (obj_t == TypeId::Str()) return type_id_of("&str");
            const auto& d = cf_.type_table.get(obj_t);
            if (d.kind == TypeKind::Slice || d.kind == TypeKind::Array) {
                return cf_.type_table.make_slice(d.elem_type);
            }
            return TypeId::Unknown();
        }
        if (const auto* idx = std::get_if<IndexExpr>(&e->node)) {
            TypeId obj_t = infer_expr_type_id_sema(idx->object);
            if (obj_t == TypeId::Str()) return TypeId::Byte();
            const auto& d = cf_.type_table.get(obj_t);
            if (d.kind == TypeKind::Slice || d.kind == TypeKind::Array) return d.elem_type;
            if (d.kind == TypeKind::Map) return d.elem_type;
            return TypeId::Unknown();
        }
        if (const auto* or_ex = std::get_if<OrExpr>(&e->node)) {
            TypeId inner_t = infer_expr_type_id_sema(or_ex->inner);
            if (inner_t.is_valid() && !inner_t.is_unknown()) {
                const auto& d = cf_.type_table.get(inner_t);
                if (d.kind == TypeKind::Fallible) return cf_.type_table.unwrap_fallible(inner_t);
                if (d.kind == TypeKind::Optional) return cf_.type_table.unwrap_optional(inner_t);
                return inner_t;
            }
            if (or_ex->fallback_val) {
                TypeId fb_t = infer_expr_type_id_sema(or_ex->fallback_val);
                if (fb_t.is_valid() && !fb_t.is_unknown()) return fb_t;
            }
        }
        if (const auto* tr = std::get_if<TryExpr>(&e->node)) {
            TypeId inner_t = infer_expr_type_id_sema(tr->inner);
            if (inner_t.is_valid() && !inner_t.is_unknown()) {
                const auto& d = cf_.type_table.get(inner_t);
                if (d.kind == TypeKind::Fallible) return cf_.type_table.unwrap_fallible(inner_t);
                // `try` on `T?` is already TRY-ON-OPTIONAL; type the result as `T` so it does not cascade.
                if (d.kind == TypeKind::Optional) return cf_.type_table.unwrap_optional(inner_t);
                return inner_t;
            }
        }
        if (const auto* me = std::get_if<MatchExpr>(&e->node)) {
            for (const auto& arm : me->arms) {
                if (!arm.body.empty()) {
                    if (const auto* expr_st = std::get_if<ExprStmt>(&arm.body.back()->node)) {
                        TypeId t = infer_expr_type_id_sema(expr_st->expr);
                        if (t.is_valid() && !t.is_unknown()) return t;
                    }
                }
            }
        }
        if (const auto* mem = std::get_if<MemberExpr>(&e->node)) {
            if (TypeId prim = primitive_type_prefix(mem->object); prim.is_valid()) {
                if (mem->field == "BITS") return TypeId::Uint32();
                if (find_builtin_assoc_const(primitive_bits(prim), mem->field)) return prim;
            }
            if (const auto* oid = mem->object ? std::get_if<IdentExpr>(&mem->object->node) : nullptr;
                oid && oid->name == "math" && !is_ident_in_scope_sema("math") && is_math_constant(mem->field)) {
                return TypeId::Float64();
            }
        }
        if (const auto* call = std::get_if<CallExpr>(&e->node)) {
            if (const auto* mem = std::get_if<MemberExpr>(&call->callee->node); mem && mem->field == "release" && smart_wrap_of(mem->object) == "own") {
                TypeId ot = infer_expr_type_id_sema(mem->object);
                if (ot.is_valid() && cf_.type_table.get(ot).kind == TypeKind::Own) return cf_.type_table.make_mut_ptr(cf_.type_table.get(ot).elem_type);
                return cf_.type_table.make_mut_ptr(type_id_of(type_name_of(mem->object)));
            }
            if (const auto* mem = std::get_if<MemberExpr>(&call->callee->node); mem && mem->field == "from_raw") {
                if (std::string own_t = own_type_prefix(mem->object); !own_t.empty()) return type_id_of(own_t);
            }
            if (const auto* mem = std::get_if<MemberExpr>(&call->callee->node); mem && mem->field == "parse") {
                if (TypeId prim = primitive_type_prefix(mem->object); prim.is_valid()) return cf_.type_table.make_optional(prim);
            }
            if (const auto* mem = std::get_if<MemberExpr>(&call->callee->node)) {
                if ((mem->field == "wrapping_as" || mem->field == "saturating_as" || mem->field == "try_as") && call->type_args.size() == 1) {
                    return type_id_of(call->type_args[0].name);
                }
                if (const BuiltinMethod* bm = lookup_builtin_method(*mem); bm && !bm->ret.empty()) {
                    TypeId rt = resolve_builtin_type(bm->ret, builtin_self_type(mem->object));
                    if (rt.is_valid()) return rt;
                }
                if (mem->field == "len") return TypeId::Usize();
                if (mem->field == "bytes" && call->args.empty()) return type_id_of("[]byte");
                // Zero-copy pointers (CF-148): `s.byte_ptr()` / `s.byte_ptr_at(i)` -> *c_char, `xs.data_ptr()` -> *T.
                if ((mem->field == "byte_ptr" || mem->field == "byte_ptr_at") && mem->object) return type_id_of("*c_char");
                if ((mem->field == "data_ptr" || mem->field == "data") && call->args.empty() && mem->object) {
                    TypeId recv = infer_expr_type_id_sema(mem->object);
                    if (recv == TypeId::Str()) return type_id_of("*c_char");
                    const auto& rd = cf_.type_table.get(recv);
                    if ((rd.kind == TypeKind::Slice || rd.kind == TypeKind::Array) && !rd.elem_type.is_unknown()) {
                        return cf_.type_table.make_raw_ptr(rd.elem_type);
                    }
                }
                if (const auto* obj_id = mem->object ? std::get_if<IdentExpr>(&mem->object->node) : nullptr) {
                    // Aliased imports (`import "core:tako" as tk`) resolve to their canonical namespace.
                    std::string ns_name = obj_id->name;
                    if (!StdlibCatalogIndex::instance().has_namespace(ns_name) && !is_ident_in_scope_sema(ns_name)) {
                        if (std::string canon = resolve_stdlib_ns_sema(ns_name); !canon.empty()) ns_name = canon;
                    }
                    if (const auto* sfn = StdlibCatalogIndex::instance().find_ns_fn(ns_name, mem->field)) {
                        if (!sfn->unwrapped_return_type.empty() && sfn->unwrapped_return_type != "void") {
                            // Generic stdlib functions (`json.parse[T]`, `db.query[T]`) return their type argument.
                            const std::string& ur = sfn->unwrapped_return_type;
                            if (!call->type_args.empty() && (ur == "T" || ur == "[]T" || ur == "T?")) {
                                const std::string& targ = call->type_args[0].name;
                                if (!targ.empty()) return type_id_of(ur == "[]T" ? "[]" + targ : targ);
                            }
                            if (ur == "T" || ur == "[]T") return infer_named_type_id_(e);
                            return type_id_of(qualify_stdlib_type_sema(*sfn, obj_id->name));
                        }
                    }
                    if (auto it = var_types_.find(obj_id->name); it != var_types_.end()) {
                        std::string recv_t = it->second;
                        const std::string recv_full = canonical_type_name_sema(it->second);
                        std::string base_recv = recv_t;
                        std::string generic_arg;
                        if (auto br = base_recv.find('['); br != std::string::npos && base_recv.ends_with(']')) {
                            generic_arg = base_recv.substr(br + 1, base_recv.size() - br - 2);
                            base_recv = base_recv.substr(0, br);
                        }
                        std::string unqual_recv = base_recv;
                        if (auto dot = unqual_recv.rfind('.'); dot != std::string::npos) unqual_recv = unqual_recv.substr(dot + 1);
                        if (auto dcol = unqual_recv.rfind("::"); dcol != std::string::npos) unqual_recv = unqual_recv.substr(dcol + 2);

                        auto check_mit = [&](const std::string& key) -> TypeId {
                            if (auto mit = StdlibCatalogIndex::instance().receiver_methods.find(key);
                                mit != StdlibCatalogIndex::instance().receiver_methods.end()) {
                                if (!mit->second.unwrapped_return_type.empty() && mit->second.unwrapped_return_type != "void") {
                                    if (!generic_arg.empty() && mit->second.unwrapped_return_type == "T") {
                                        return type_id_of(generic_arg);
                                    }
                                    const auto dotq = recv_full.rfind('.');
                                    return type_id_of(qualify_stdlib_type_sema(mit->second, dotq == std::string::npos
                                        ? local_qualifier_for_module_sema(mit->second.module_import, mit->second.ns_alias)
                                        : recv_full.substr(0, dotq)));
                                }
                            }
                            return TypeId::Unknown();
                        };

                        if (TypeId tid = check_mit(recv_t + "." + mem->field); !tid.is_unknown()) return tid;
                        if (TypeId tid = check_mit(base_recv + "." + mem->field); !tid.is_unknown()) return tid;
                        if (TypeId tid = check_mit(unqual_recv + "." + mem->field); !tid.is_unknown()) return tid;
                    }
                } else if (mem->object) {
                    if (const auto* sub_mem = std::get_if<MemberExpr>(&mem->object->node)) {
                        if (const auto* sub_id = std::get_if<IdentExpr>(&sub_mem->object->node)) {
                            if (const auto* sfn = StdlibCatalogIndex::instance().find_ns_fn(sub_id->name, sub_mem->field + "." + mem->field)) {
                                if (!sfn->unwrapped_return_type.empty() && sfn->unwrapped_return_type != "void") {
                                    return type_id_of(qualify_stdlib_type_sema(*sfn, sub_id->name));
                                }
                            }
                        }
                    }
                }
            }
        }
        return infer_named_type_id_(e);
    }

    // Display / lookup name of an expression's type, derived from its TypeId ("" when unknown).
    [[nodiscard]] auto type_name_of(const ExprPtr& e) const -> std::string {
        TypeId id = infer_expr_type_id_sema(e);
        if (id.is_unknown()) return "";
        return cf_.type_table.to_string(id);
    }

    [[nodiscard]] auto is_non_byte_int_or_u8_expr(const ExprPtr& e) const -> bool {
        if (!e) return false;
        if (const auto* lit = std::get_if<LiteralExpr>(&e->node)) {
            return lit->kind == TokenKind::Int;
        }
        if (const auto* un = std::get_if<UnaryExpr>(&e->node)) {
            if (un->op == "-" || un->op == "+") {
                return is_non_byte_int_or_u8_expr(un->operand);
            }
        }
        std::string t = type_name_of(e);
        return t == "u8" || t == "i8" || t == "i16" || t == "i32" || t == "i64" ||
               t == "u16" || t == "u32" || t == "u64" || t == "usize" || t == "isize" || t == "int";
    }

    [[nodiscard]] auto is_byte_slice_expr(const ExprPtr& e) const -> bool {
        if (!e) return false;
        if (const auto* id = std::get_if<IdentExpr>(&e->node)) {
            return byte_slice_vars_.contains(id->name);
        }
        if (const auto* mem = std::get_if<MemberExpr>(&e->node)) {
            if (!mem->is_namespace && mem->object) {
                std::string owner_t = type_name_of(mem->object);
                if (!owner_t.empty()) {
                    if (auto sit = cf_.struct_tags.find(owner_t); sit != cf_.struct_tags.end()) {
                        for (const auto& f : sit->second) {
                            if (f.field_name == mem->field) {
                                return (f.field_type.is_slice || f.field_type.is_array) && f.field_type.name == "byte";
                            }
                        }
                    }
                }
            }
        }
        return type_name_of(e) == "[]byte";
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
        if (expr_returns_optional_sema(e)) {
            if (const auto* id = std::get_if<IdentExpr>(&e->node)) {
                if (auto it = ptr_null_state_.find(id->name); it != ptr_null_state_.end()) {
                    return it->second;
                }
            }
            return NullState::MaybeNull;
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

    [[nodiscard]] static auto expr_start_loc(const ExprPtr& e) -> std::pair<int, int> {
        if (!e) return {1, 1};
        if (const auto* call = std::get_if<CallExpr>(&e->node)) {
            if (call->callee) return expr_start_loc(call->callee);
        }
        if (const auto* mem = std::get_if<MemberExpr>(&e->node)) {
            if (mem->object) return expr_start_loc(mem->object);
        }
        if (const auto* idx = std::get_if<IndexExpr>(&e->node)) {
            if (idx->object) return expr_start_loc(idx->object);
        }
        if (const auto* sl = std::get_if<SliceExpr>(&e->node)) {
            if (sl->object) return expr_start_loc(sl->object);
        }
        if (const auto* bin = std::get_if<BinaryExpr>(&e->node)) {
            if (bin->left) return expr_start_loc(bin->left);
        }
        if (const auto* or_ex = std::get_if<OrExpr>(&e->node)) {
            if (or_ex->inner) return expr_start_loc(or_ex->inner);
        }
        return {e->line, e->col};
    }

    [[nodiscard]] static auto format_type_ref_sema(const TypeRef& t) -> std::string {
        std::string s;
        if (t.is_borrow) s += "&";
        else if (t.is_mut_borrow) s += "&mut ";
        else if (t.is_ptr) s += "*";
        else if (t.is_mut_ptr) s += "*mut ";
        if (t.is_slice) {
            s += "[]";
            if (t.elem_type) {
                s += format_type_ref_sema(*t.elem_type);
            } else if (t.name.starts_with("[]")) {
                s += t.name.substr(2);
            } else {
                s += t.name.empty() ? "void" : t.name;
            }
        } else if (t.is_array) {
            s += "[" + t.array_len + "]";
            if (t.elem_type) {
                s += format_type_ref_sema(*t.elem_type);
            } else {
                s += t.name.empty() ? "void" : t.name;
            }
        } else if (t.is_tuple) {
            s += "(";
            for (std::size_t i = 0; i < t.tuple_types.size(); ++i) {
                if (i > 0) s += ", ";
                s += format_type_ref_sema(t.tuple_types[i]);
            }
            s += ")";
        } else if (t.is_map) {
            s += "map[" + (t.key_type ? format_type_ref_sema(*t.key_type) : std::string("str")) + "]" +
                 (t.elem_type ? format_type_ref_sema(*t.elem_type) : std::string("void"));
        } else if (!t.smart_wrap.empty() && t.elem_type) {
            s += t.smart_wrap + "[" + format_type_ref_sema(*t.elem_type) + "]";
        } else if (!t.type_args.empty()) {
            s += t.name.empty() ? "void" : t.name;
            s += "[";
            for (std::size_t i = 0; i < t.type_args.size(); ++i) {
                if (i > 0) s += ", ";
                s += format_type_ref_sema(t.type_args[i]);
            }
            s += "]";
        } else if (t.elem_type && (t.is_ptr || t.is_mut_ptr || t.is_borrow || t.is_mut_borrow)) {
            s += format_type_ref_sema(*t.elem_type);
        } else if (t.elem_type) {
            s += t.name.empty() ? "void" : t.name;
            s += "[" + format_type_ref_sema(*t.elem_type) + "]";
        } else {
            s += t.name.empty() ? "void" : t.name;
        }
        if (t.is_optional) s += "?";
        if (t.is_fallible) s += "!";
        return s;
    }

    [[nodiscard]] auto is_stdlib_ns_imported(std::string_view ns) const -> bool {
        if (cf_.shadowed_default_qualifiers.contains(std::string(ns))) return false;
        if (ns == "build" || ns == "testing") return true;
        if (ns == "cpp") return !cf_.cpp_qualifiers.empty();
        if (is_cpp_qualifier_sema(ns)) return true;
        if (is_vendor_alias_sema(ns)) return true;
        if (cf_.core_aliases.contains(std::string(ns))) return true;
        const auto& idx = StdlibCatalogIndex::instance();
        auto mod_it = idx.ns_to_import.find(std::string(ns));
        std::string_view expected_mod = (mod_it != idx.ns_to_import.end()) ? std::string_view(mod_it->second) : std::string_view{};
        for (const auto& imp : cf_.ast.imports) {
            if (!imp.alias.empty() && imp.alias == ns) return true;
            if (!expected_mod.empty() && imp.path == expected_mod) return true;
            auto colon = imp.path.rfind(':');
            auto slash = imp.path.rfind('/');
            std::size_t sep = std::string::npos;
            if (colon != std::string::npos && slash != std::string::npos) sep = std::max(colon, slash);
            else if (colon != std::string::npos) sep = colon;
            else sep = slash;
            std::string_view tail = (sep == std::string::npos) ? std::string_view(imp.path) : std::string_view(imp.path).substr(sep + 1);
            if (tail == ns) return true;
            if (ns == "rl" && tail == "raylib") return true;
            if (ns == "rg" && tail == "raygui") return true;
        }
        return false;
    }

    // CF-148: is `t` a type Sema fully understands (so a mismatch is certain, not a gap in inference)?
    // Primitives, str, &str, slices/arrays of those, and nominal types declared in this program.
    [[nodiscard]] auto is_concrete_checkable_type(TypeId t) const -> bool {
        if (t.is_unknown()) return false;
        if (t.index >= TypeId::Bool().index && t.index <= TypeId::Byte().index) return true;
        const auto& d = cf_.type_table.get(t);
        switch (d.kind) {
            case TypeKind::Borrow:
            case TypeKind::Slice:
            case TypeKind::Array:
                return is_concrete_checkable_type(d.elem_type);
            case TypeKind::NominalStruct:
            case TypeKind::NominalEnum:
                return cf_.struct_tags.contains(d.name) || cf_.enums.contains(d.name);
            default:
                return false;
        }
    }

    // CF-145: a type argument bound to `T: Iface` must implement every method of Iface (name, parameter
    // count and types, return type). Concepts used to be emitted without being checked anywhere.
    [[nodiscard]] auto check_interface_constraints(const CallExpr& call, const FnDecl& sig, int line, int col)
        -> std::expected<void, std::string> {
        for (const auto& tp : sig.type_params) {
            if (tp.constraint.empty()) continue;
            auto iit = std::find_if(cf_.ast.interfaces.begin(), cf_.ast.interfaces.end(),
                [&](const InterfaceDecl& d) { return d.name == tp.constraint; });
            if (iit == cf_.ast.interfaces.end()) continue;
            // Find the argument whose parameter is exactly `T` and take its concrete type.
            std::string concrete;
            int arg_line = line, arg_col = col;
            for (std::size_t i = 0; i < sig.params.size() && i < call.args.size(); ++i) {
                if (format_type_ref_sema(sig.params[i].type) != tp.name) continue;
                concrete = type_name_of(call.args[i].value);
                arg_line = call.args[i].line;
                arg_col = call.args[i].col;
                break;
            }
            if (concrete.empty() && !call.type_args.empty()) concrete = call.type_args.front().name;
            if (concrete.empty() || !cf_.struct_tags.contains(concrete)) continue;  // only nominal types declared here
            std::vector<std::string> problems;
            for (const auto& m : iit->methods) {
                auto mit = cf_.methods.find(concrete + "::" + m.name);
                if (mit == cf_.methods.end()) {
                    problems.push_back(std::format("missing method '{}'", m.name));
                    continue;
                }
                const FnDecl& impl = mit->second;
                if (impl.params.size() != m.param_types.size()) {
                    problems.push_back(std::format("'{}' takes {} parameter(s), interface expects {}", m.name, impl.params.size(), m.param_types.size()));
                    continue;
                }
                for (std::size_t k = 0; k < m.param_types.size(); ++k) {
                    if (format_type_ref_sema(impl.params[k].type) != format_type_ref_sema(m.param_types[k])) {
                        problems.push_back(std::format("'{}' parameter {} is '{}', interface expects '{}'", m.name, k + 1,
                            format_type_ref_sema(impl.params[k].type), format_type_ref_sema(m.param_types[k])));
                    }
                }
                if (format_type_ref_sema(impl.return_type) != format_type_ref_sema(m.return_type)) {
                    problems.push_back(std::format("'{}' returns '{}', interface expects '{}'", m.name,
                        format_type_ref_sema(impl.return_type), format_type_ref_sema(m.return_type)));
                }
            }
            if (!problems.empty()) {
                std::string joined;
                for (const auto& p : problems) joined += (joined.empty() ? "" : "; ") + p;
                if (auto r = emit_error(diag_at(arg_line, arg_col, "cfire/UNSATISFIED-CONSTRAINT", std::format("type '{}' does not satisfy interface '{}' required by '{}[{}: {}]': {}", concrete, tp.constraint, sig.name, tp.name, tp.constraint, joined))); !r) return r;
            }
        }
        return {};
    }

    // CF-148: argument types of calls to functions declared in this program (clang used to report these).
    [[nodiscard]] auto check_call_argument_types(const CallExpr& call, const FnDecl& sig) -> std::expected<void, std::string> {
        std::unordered_set<std::string> type_params;
        for (const auto& tp : sig.type_params) type_params.insert(tp.name);
        auto mentions_type_param = [&](const TypeRef& tr) {
            std::string n = format_type_ref_sema(tr);
            for (const auto& tp : type_params) {
                if (n == tp || n.find("[" + tp + "]") != std::string::npos || n.ends_with(tp)) return true;
            }
            return false;
        };
        for (std::size_t i = 0; i < call.args.size() && i < sig.params.size(); ++i) {
            const auto& arg = call.args[i];
            const ParamDecl* param = &sig.params[i];
            if (!arg.label.empty()) {
                auto it = std::find_if(sig.params.begin(), sig.params.end(), [&](const ParamDecl& pd) { return pd.name == arg.label; });
                if (it == sig.params.end()) continue;
                param = &*it;
            }
            if (mentions_type_param(param->type) || param->type.name.starts_with("fn")) continue;
            TypeId want = cf_.type_table.from_typeref(param->type);
            TypeId got = infer_expr_type_id_sema(arg.value);
            if (!is_concrete_checkable_type(want) || !is_concrete_checkable_type(got)) continue;
            if (expr_returns_optional_sema(arg.value)) continue;  // handled by the optional checks
            if (!cf_.type_table.is_assignable(want, got)) {
                if (auto r = emit_error(diag_at(arg.line, arg.col, "cfire/TYPE-MISMATCH", std::format("argument {} of '{}' expects '{}', got '{}'{}", i + 1, sig.name, cf_.type_table.to_string(want), cf_.type_table.to_string(got), nominal_mismatch_hint_sema(want, got)))); !r) return r;
            }
        }
        return {};
    }

    // Q11 (CF-166): a constant shift count outside [0, bit width of the promoted left operand) is rejected.
    [[nodiscard]] auto check_const_shift_count(const ExprPtr& left, const ExprPtr& count, int line, int col)
        -> std::expected<void, std::string> {
        if (!count) return {};
        bool negative = false;
        const Expr* lit_expr = count;
        if (const auto* un = std::get_if<UnaryExpr>(&count->node); un && un->op == "-" && un->operand) {
            negative = true;
            lit_expr = un->operand;
        }
        const auto* lit = std::get_if<LiteralExpr>(&lit_expr->node);
        if (!lit || lit->kind != TokenKind::Int) return {};
        auto v = cfire::detail::parse_int<std::int64_t>(lit->value);
        if (!v) return {};
        std::string lt = type_name_of(left);
        int bits = 64;  // integer literals and i64/u64/isize/usize
        if (lt == "i8" || lt == "u8" || lt == "byte" || lt == "i16" || lt == "u16" || lt == "i32" || lt == "u32") bits = 32;  // promoted to int
        if (negative || *v >= bits) {
            return emit_error(diag_at(line, col, "cfire/SHIFT-OUT-OF-RANGE", std::format("shift count {}{} is out of range for a {}-bit operand; use 'wrapping_shl'/'checked_shl' for explicit behavior", negative ? "-" : "", lit->value, bits)));
        }
        return {};
    }

    // Q9 (CF-165): an integer constant that does not fit its target type is a compile error, not a silent
    // truncation (`a: u8 = 300`, `u8(300)`, `c: u32 = -1`). Runtime values are range-checked by `T(x)`.
    [[nodiscard]] auto check_int_literal_fits(const ExprPtr& e, std::string_view tname, int line, int col)
        -> std::expected<void, std::string> {
        struct Range { __int128 lo; __int128 hi; };
        static const std::unordered_map<std::string_view, Range> ranges = {
            {"i8", {INT8_MIN, INT8_MAX}}, {"i16", {INT16_MIN, INT16_MAX}}, {"i32", {INT32_MIN, INT32_MAX}},
            {"int", {INT64_MIN, INT64_MAX}}, {"i64", {INT64_MIN, INT64_MAX}}, {"isize", {INT64_MIN, INT64_MAX}},
            {"u8", {0, UINT8_MAX}}, {"byte", {0, UINT8_MAX}}, {"u16", {0, UINT16_MAX}}, {"u32", {0, UINT32_MAX}},
            {"u64", {0, UINT64_MAX}}, {"usize", {0, UINT64_MAX}}, {"uint", {0, UINT64_MAX}}};
        auto rit = ranges.find(tname);
        if (rit == ranges.end() || !e) return {};
        bool negative = false;
        const Expr* lit_expr = e;
        if (const auto* un = std::get_if<UnaryExpr>(&e->node); un && un->op == "-" && un->operand) {
            negative = true;
            lit_expr = un->operand;
        }
        const auto* lit = std::get_if<LiteralExpr>(&lit_expr->node);
        if (!lit || lit->kind != TokenKind::Int) return {};
        std::string digits;
        for (char c : lit->value) if (c != '_') digits.push_back(c);
        int base = 10;
        std::string_view body = digits;
        if (body.size() > 2 && body[0] == '0' && (body[1] == 'x' || body[1] == 'X')) { base = 16; body.remove_prefix(2); }
        else if (body.size() > 2 && body[0] == '0' && (body[1] == 'b' || body[1] == 'B')) { base = 2; body.remove_prefix(2); }
        else if (body.size() > 2 && body[0] == '0' && (body[1] == 'o' || body[1] == 'O')) { base = 8; body.remove_prefix(2); }
        __int128 v = 0;
        bool overflow = false;
        for (char c : body) {
            int d = std::isdigit(static_cast<unsigned char>(c)) ? c - '0'
                  : std::isxdigit(static_cast<unsigned char>(c)) ? (std::tolower(static_cast<unsigned char>(c)) - 'a' + 10) : -1;
            if (d < 0 || d >= base) return {};  // not a plain integer literal; leave it to other checks
            v = v * base + d;
            if (v > static_cast<__int128>(UINT64_MAX)) { overflow = true; break; }
        }
        if (negative) v = -v;
        if (overflow || v < rit->second.lo || v > rit->second.hi) {
            std::string shown = (negative ? "-" : "") + lit->value;
            return emit_error(diag_at(line, col, "cfire/LITERAL-OUT-OF-RANGE", std::format("integer literal {} does not fit in '{}' (range {}..{}); use a value in range or an explicit 'wrapping_as[{}]()'", shown, tname, static_cast<long long>(rit->second.lo), rit->second.hi > static_cast<__int128>(INT64_MAX) ? std::string("18446744073709551615") : std::to_string(static_cast<long long>(rit->second.hi)), tname)));
        }
        return {};
    }

    // Q14 (CF-143): methods on builtin types are a closed set. Anything else would fall through to the raw
    // std::string / std::vector / map member of the same name, which bypasses intents and the loan tracker.
    enum class BuiltinReceiver : std::uint8_t { None, Str, Slice, Array, Map };

    [[nodiscard]] static auto classify_builtin_receiver(std::string_view t) -> BuiltinReceiver {
        while (t.starts_with("&mut ") || t.starts_with("*mut ")) t.remove_prefix(5);
        while (t.starts_with("&") || t.starts_with("*")) t.remove_prefix(1);
        if (t == "str" || t == "string") return BuiltinReceiver::Str;
        if (t.starts_with("[]")) return BuiltinReceiver::Slice;
        if (t.starts_with("[") && t.size() > 1 && std::isdigit(static_cast<unsigned char>(t[1]))) return BuiltinReceiver::Array;
        if (t.starts_with("map[")) return BuiltinReceiver::Map;
        return BuiltinReceiver::None;
    }

    [[nodiscard]] static constexpr auto bm_bits_of(BuiltinReceiver k) -> std::uint8_t {
        switch (k) {
            case BuiltinReceiver::Str: return BmStr;
            case BuiltinReceiver::Slice: return BmSlice;
            case BuiltinReceiver::Array: return BmArray;
            case BuiltinReceiver::Map: return BmMap;
            case BuiltinReceiver::None: return 0;
        }
        return 0;
    }

    // W4: the receiver kind of a builtin method call, from the receiver's TypeId (borrows and pointers
    // auto-deref). Falls back to the receiver's spelled type when inference has no TypeId.
    [[nodiscard]] auto builtin_recv_bits(const ExprPtr& obj) const -> std::uint8_t {
        if (!obj) return 0;
        TypeId t = infer_expr_type_id_sema(obj);
        for (int guard = 0; guard < 4 && t.is_valid(); ++guard) {
            const auto& d = cf_.type_table.get(t);
            if (d.kind == TypeKind::Borrow || d.kind == TypeKind::MutBorrow || d.kind == TypeKind::RawPtr || d.kind == TypeKind::MutPtr) {
                t = d.elem_type;
                continue;
            }
            break;
        }
        if (t.is_valid()) {
            switch (cf_.type_table.get(t).kind) {
                case TypeKind::Int8: case TypeKind::Int16: case TypeKind::Int32: case TypeKind::Int64:
                case TypeKind::Uint8: case TypeKind::Uint16: case TypeKind::Uint32: case TypeKind::Uint64:
                case TypeKind::Isize: case TypeKind::Usize:
                    return BmInt;
                case TypeKind::Float32: case TypeKind::Float64: return BmFloat;
                case TypeKind::Bool: return BmBool;
                case TypeKind::Str: return BmStr;
                case TypeKind::Slice: return BmSlice;
                case TypeKind::Array: return BmArray;
                case TypeKind::Map: return BmMap;
                default: break;
            }
        }
        return bm_bits_of(classify_builtin_receiver(type_name_of(obj)));
    }

    [[nodiscard]] auto builtin_self_type(const ExprPtr& obj) const -> TypeId {
        TypeId t = infer_expr_type_id_sema(obj);
        for (int guard = 0; guard < 4 && t.is_valid(); ++guard) {
            const auto& d = cf_.type_table.get(t);
            if (d.kind != TypeKind::Borrow && d.kind != TypeKind::MutBorrow && d.kind != TypeKind::RawPtr && d.kind != TypeKind::MutPtr) break;
            t = d.elem_type;
        }
        return t;
    }

    // Resolves a builtin-method type spelling (`Self`, `T?`, `[]K`, `&str?`, `usize`) against the receiver.
    [[nodiscard]] auto resolve_builtin_type(std::string_view spec, TypeId self) const -> TypeId {
        while (!spec.empty() && spec.front() == ' ') spec.remove_prefix(1);
        while (!spec.empty() && (spec.back() == ' ' || spec.back() == '=')) spec.remove_suffix(1);
        if (spec.empty() || spec == "fn") return TypeId::Unknown();
        if (spec.ends_with('?')) {
            TypeId inner = resolve_builtin_type(spec.substr(0, spec.size() - 1), self);
            return inner.is_valid() ? cf_.type_table.make_optional(inner) : inner;
        }
        if (spec.starts_with("[]")) {
            TypeId inner = resolve_builtin_type(spec.substr(2), self);
            return inner.is_valid() ? cf_.type_table.make_slice(inner) : inner;
        }
        if (spec == "&str") return TypeId::Str();
        if (spec == "void") return TypeId::Void();
        const auto& sd = cf_.type_table.get(self);
        if (spec == "Self") return self;
        if (spec == "T") {
            if (self == TypeId::Str()) return TypeId::Byte();
            if (sd.kind == TypeKind::Slice || sd.kind == TypeKind::Array) return sd.elem_type;
            return TypeId::Unknown();
        }
        if (spec == "K") return sd.kind == TypeKind::Map ? sd.key_type : TypeId::Unknown();
        if (spec == "V") return sd.kind == TypeKind::Map ? sd.elem_type : TypeId::Unknown();
        if (spec == "*T") {
            if (self == TypeId::Str()) return type_id_of("*c_char");
            if ((sd.kind == TypeKind::Slice || sd.kind == TypeKind::Array) && sd.elem_type.is_valid()) return cf_.type_table.make_raw_ptr(sd.elem_type);
            return TypeId::Unknown();
        }
        return type_id_of(spec);
    }

    [[nodiscard]] auto lookup_builtin_method(const MemberExpr& mem) const -> const BuiltinMethod* {
        if (!mem.object) return nullptr;
        if (const auto* oid = std::get_if<IdentExpr>(&mem.object->node);
            oid && !is_ident_in_scope_sema(oid->name) && (is_namespace_ident_sema(oid->name) || cf_.enums.contains(oid->name))) {
            return nullptr;
        }
        std::uint8_t bits = builtin_recv_bits(mem.object);
        if (bits == 0) return nullptr;
        return find_builtin_method(bits, mem.field);
    }

    [[nodiscard]] static auto builtin_method_allowed(BuiltinReceiver k, std::string_view m) -> bool {
        if (k == BuiltinReceiver::None) return true;
        return find_builtin_method(bm_bits_of(k), m) != nullptr;
    }

    // W4: arity, argument types and `mut` receivers of builtin method calls, all read from the table.
    [[nodiscard]] auto check_builtin_method_call(const CallExpr& call, const MemberExpr& mem, int line, int col) -> std::expected<void, std::string> {
        const BuiltinMethod* bm = lookup_builtin_method(mem);
        if (!bm || !bm->lowered) return {};
        const std::size_t want = builtin_param_count(*bm);
        const std::size_t got = call.args.size() + (call.block_arg ? 1 : 0);
        TypeId self = builtin_self_type(mem.object);
        std::string self_name = self.is_valid() ? cf_.type_table.to_string(self) : std::string("value");
        const std::size_t required = builtin_required_param_count(*bm);
        if (got < required || got > want) {
            std::string expected = required == want ? std::to_string(want) : std::format("{} to {}", required, want);
            return emit_error(diag_at(line, col, "cfire/ARITY-MISMATCH", std::format("method '{}' on '{}' expects {} argument(s), got {}", bm->name, self_name, expected, got)));
        }
        std::vector<std::string_view> params;
        for (std::string_view rest = bm->params; !rest.empty();) {
            auto comma = rest.find(',');
            params.push_back(rest.substr(0, comma));
            if (comma == std::string_view::npos) break;
            rest.remove_prefix(comma + 1);
        }
        for (std::size_t i = 0; i < call.args.size() && i < params.size(); ++i) {
            const auto& arg = call.args[i];
            if (arg.value && std::holds_alternative<ArrayLitExpr>(arg.value->node)) continue;  // literal adapts to `[]T`
            TypeId want_t = resolve_builtin_type(params[i], self);
            TypeId got_t = infer_expr_type_id_sema(arg.value);
            if (!is_concrete_checkable_type(want_t) || !is_concrete_checkable_type(got_t)) continue;
            if (expr_returns_optional_sema(arg.value)) continue;
            // Integer literals adapt to the receiver's width (`x.min(3)` on an `i64`).
            if (cf_.type_table.is_numeric(want_t) && cf_.type_table.is_numeric(got_t) && is_numeric_literal_expr(arg.value)) continue;
            if (!cf_.type_table.is_assignable(want_t, got_t)) {
                return emit_error(diag_at(arg.line, arg.col, "cfire/TYPE-MISMATCH", std::format("argument {} of '{}.{}' expects '{}', got '{}'", i + 1, self_name, bm->name, cf_.type_table.to_string(want_t), cf_.type_table.to_string(got_t))));
            }
        }
        if (bm->mutates) {
            if (const auto* oid = std::get_if<IdentExpr>(&mem.object->node);
                oid && imm_vars_.contains(oid->name) && !smart_ptr_vars_.contains(oid->name)) {
                return emit_error(diag_at(line, col, "cfire/MUT-RECEIVER", std::format("cannot call mutating method '{}' on immutable variable '{}'; declare it with 'mut'", bm->name, oid->name), {{"var", oid->name}}));
            }
        }
        return {};
    }

    // Canonical cfire spelling for raw C++ members, used for the diagnostic hint and `cfire fix`.
    [[nodiscard]] static auto builtin_method_replacement(BuiltinReceiver k, std::string_view m) -> std::string_view {
        if (m == "size" || m == "length") return "len";
        if (k != BuiltinReceiver::Map && (m == "push_back" || m == "emplace_back")) return "push";
        if (k != BuiltinReceiver::Map && m == "pop_back") return "pop";
        if (k == BuiltinReceiver::Map && m == "erase") return "remove";
        if (k == BuiltinReceiver::Map && (m == "count" || m == "find")) return "contains";
        return "";
    }

    [[nodiscard]] static auto is_math_constant(std::string_view f) -> bool {
        return f == "PI" || f == "E" || f == "TAU" || f == "SQRT_2" || f == "LN_2" || f == "LN_10";
    }

    [[nodiscard]] static auto bm_kind_name(std::uint8_t bits) -> std::string_view {
        if (bits & BmStr) return "str";
        if (bits & BmSlice) return "slice";
        if (bits & BmArray) return "fixed array";
        if (bits & BmMap) return "map";
        if (bits & BmInt) return "integer";
        if (bits & BmFloat) return "float";
        if (bits & BmBool) return "bool";
        return "value";
    }

    [[nodiscard]] static auto is_numeric_literal_expr(const ExprPtr& e) -> bool {
        if (!e) return false;
        if (const auto* un = std::get_if<UnaryExpr>(&e->node); un && un->op == "-") return is_numeric_literal_expr(un->operand);
        const auto* lit = std::get_if<LiteralExpr>(&e->node);
        return lit && (lit->kind == TokenKind::Int || lit->kind == TokenKind::Float);
    }

    [[nodiscard]] auto check_builtin_method_surface(const MemberExpr& mem, int line, int col) -> std::expected<void, std::string> {
        if (mem.field == "find" || mem.field == "rfind") return {};  // CF-059 has its own diagnostic
        if (const auto* oid = mem.object ? std::get_if<IdentExpr>(&mem.object->node) : nullptr;
            oid && !is_ident_in_scope_sema(oid->name) && (is_namespace_ident_sema(oid->name) || cf_.enums.contains(oid->name))) {
            return {};
        }
        const std::uint8_t bits = builtin_recv_bits(mem.object);
        if (bits == 0 || find_builtin_method(bits, mem.field)) return {};
        std::string_view type_name = bm_kind_name(bits);
        for (const auto& r : kRetiredBuiltinMethods) {
            if ((r.recv & bits) && r.name == mem.field) {
                return emit_error(diag_at(line, col, "cfire/RETIRED-API", std::format("'.{}()' has been renamed; use '.{}()' instead", mem.field, r.replacement), {{"from", "." + std::string(mem.field) + "("}, {"to", "." + std::string(r.replacement) + "("}}));
            }
        }
        // Q10: parsing is associated with the target type (`i32.parse(s)`), not a `str` method.
        if ((bits & BmStr) && mem.field.starts_with("parse_")) {
            std::string target = mem.field.substr(6);
            return emit_error(diag_at(line, col, "cfire/RETIRED-API", std::format("'.{}()' has been removed; use '{}.parse(s)' (returns '{}?')", mem.field, target, target), {{"rewrite", "method_to_assoc"}, {"method", std::string(mem.field)}, {"to", target + ".parse"}}));
        }
        if ((bits & (BmInt | BmFloat | BmBool)) && mem.field.size() > 0) {
            // Numbers, floats and bool have a closed method set too; anything else is a typo or a C++ member.
            return emit_error(diag_at(line, col, "cfire/UNKNOWN-METHOD", std::format("unknown {} method '{}'", type_name, mem.field), {{"method", std::string(mem.field)}, {"replacement", ""}}));
        }
        auto kind = classify_builtin_receiver(type_name_of(mem.object));
        std::string_view repl = builtin_method_replacement(kind, mem.field);
        std::string hint = repl.empty()
            ? std::string("; raw C++ members are only available inside 'cpp { }' blocks")
            : std::format("; use '.{}()' instead", repl);
        return emit_error(diag_at(line, col, "cfire/UNKNOWN-METHOD", std::format("unknown {} method '{}'{}", type_name, mem.field, hint), {{"method", std::string(mem.field)}, {"replacement", std::string(repl)}}));
    }

    [[nodiscard]] auto check_stdlib_ns_imported(std::string_view ns, int line, int col) -> std::expected<void, std::string> {
        if (cf_.shadowed_default_qualifiers.contains(std::string(ns))) {
            std::string active_alias = lookup_or_empty(cf_.shadowed_default_qualifiers, std::string(ns));
            return emit_error(diag_at(line, col, "cfire/UNDECLARED-IDENT", std::format("module '{}' is imported as '{}'; use '{}.<member>'", ns, active_alias, active_alias)));
        }
        const auto& idx = StdlibCatalogIndex::instance();
        if (!idx.has_namespace(ns) || is_ident_in_scope_sema(std::string(ns))) return {};
        if (is_stdlib_ns_imported(ns)) return {};
        if (reported_missing_imports_.contains(std::string(ns))) return {};
        reported_missing_imports_.insert(std::string(ns));
        std::string mod_path = idx.import_path_for(ns);
        return emit_error(diag_at(line, col, "cfire/MISSING-IMPORT", std::format("module '{}' is not imported; add 'import \"{}\"'", ns, mod_path), {{"module", mod_path}}));
    }

    [[nodiscard]] auto check_type_ref_separators(const TypeRef& tr, int line, int col) -> std::expected<void, std::string> {
        if (!tr.raw_name.empty()) {
            auto dcol = tr.raw_name.find("::");
            auto dot = tr.raw_name.find('.');
            if (dcol != std::string::npos && (dot == std::string::npos || dcol < dot)) {
                std::string qual = tr.raw_name.substr(0, dcol);
                if (!cf_.enums.contains(qual) &&
                    (qual == "cpp" || is_cpp_qualifier_sema(qual) || is_vendor_alias_sema(qual) ||
                     !resolve_stdlib_ns_sema(qual).empty() || is_namespace_ident_sema(qual) ||
                     cf_.shadowed_default_qualifiers.contains(qual))) {
                    std::string rest = tr.raw_name.substr(dcol + 2);
                    return emit_error(diag_at(line, col, "cfire/DEPRECATED-SEPARATOR", std::format("use '{}.{}' instead of '{}::{}'", qual, rest, qual, rest), {{"kind", "qualifier"}}));
                }
            }
            if (dot != std::string::npos) {
                std::string qual = tr.raw_name.substr(0, dot);
                if (cf_.shadowed_default_qualifiers.contains(qual)) {
                    std::string active_alias = lookup_or_empty(cf_.shadowed_default_qualifiers, qual);
                    std::string rest = tr.raw_name.substr(dot + 1);
                    return emit_error(diag_at(line, col, "cfire/UNDECLARED-IDENT", std::format("module '{}' is imported as '{}'; use '{}.{}'", qual, active_alias, active_alias, rest)));
                }
            }
        }
        if (tr.elem_type) {
            if (auto r = check_type_ref_separators(*tr.elem_type, line, col); !r) return r;
        }
        if (tr.key_type) {
            if (auto r = check_type_ref_separators(*tr.key_type, line, col); !r) return r;
        }
        for (const auto& arg : tr.type_args) {
            if (auto r = check_type_ref_separators(arg, line, col); !r) return r;
        }
        for (const auto& tup : tr.tuple_types) {
            if (auto r = check_type_ref_separators(tup, line, col); !r) return r;
        }
        return {};
    }

    [[nodiscard]] auto is_ident_in_scope_sema(const std::string& ident) const -> bool {
        if (local_defined_vars_.contains(ident) || mut_vars_.contains(ident) ||
            imm_vars_.contains(ident) || move_params_.contains(ident)) {
            return true;
        }
        if (cf_.fn_sigs.contains(ident)) return true;
        if (!pkg_name_.empty() && cf_.fn_sigs.contains(pkg_name_ + "." + ident)) return true;
        for (const auto& gv : cf_.ast.global_vars) {
            if (gv.name == ident) return true;
        }
        return false;
    }

    [[nodiscard]] static auto is_builtin_ident_sema(std::string_view name) -> bool {
        return name == "_" || name == "break" || name == "continue" ||
               name == "nullptr" || name == "null" ||
               name == "println" || name == "print" || name == "eprintln" || name == "eprint" ||
               name == "format" || name == "readln" || name == "input" || name == "black_box" ||
               name == "panic" || name == "assert" ||
               name == "assert_eq" || name == "assert_ne" || name == "assert_true" || name == "assert_false" ||
               name == "assert_err" || name == "assert_ok" || name == "error" || name == "make" ||
               name == "len" || name == "cap" || name == "append" || name == "copy" || name == "delete" ||
               name == "close" || name == "exit" || name == "some" || name == "none" || name == "ok" ||
               name == "err" || name == "own" || name == "shared" || name == "async" ||
               name == "i8" || name == "i16" || name == "i32" || name == "i64" || name == "isize" || name == "int" ||
               name == "u8" || name == "u16" || name == "u32" || name == "u64" || name == "usize" || name == "uint" ||
               name == "byte" || name == "f32" || name == "f64" || name == "bool" || name == "str" ||
               name == "char" || name == "rune" || name == "void" || name == "any" ||
               name == "c_int" || name == "c_char" || name == "c_void" || name == "c_long" ||
               name == "c_ulong" || name == "c_float" || name == "c_double" ||
               name.starts_with("[]") || name.starts_with("[") || name.starts_with("map[") ||
               name.starts_with("*") || name.starts_with("&");
    }

    [[nodiscard]] auto is_known_identifier_sema(const std::string& name) const -> bool {
        if (cf_.shadowed_default_qualifiers.contains(name)) return false;
        if (name.empty() || is_builtin_ident_sema(name)) return true;
        if (is_ident_in_scope_sema(name)) return true;
        if (is_namespace_ident_sema(name)) return true;
        if (cf_.struct_tags.contains(name) || cf_.enums.contains(name) || cf_.variant_to_enum.contains(name)) return true;
        for (const auto& st : cf_.ast.structs) {
            if (st.name == name) return true;
        }
        // Check compound names like `Shape.Circle`, `audio.Waveform::Sine`, `Waveform::Sine`, `http.Response`
        auto dot = name.find('.');
        auto dcol = name.find("::");
        std::size_t sep = std::min(dot, dcol);
        if (sep != std::string::npos) {
            std::string prefix = name.substr(0, sep);
            if (is_known_identifier_sema(prefix)) return true;
            if (prefix == "Waveform" || prefix == "SfxPreset" || prefix == "OpenMode" ||
                prefix == "SeekWhence" || prefix == "Level" || prefix == "Format" || prefix == "Provider") {
                return true;
            }
            if (dcol != std::string::npos) {
                std::string suffix = name.substr(dcol + 2);
                if (cf_.variant_to_enum.contains(suffix)) return true;
            }
        }
        return false;
    }

    // ---------------------------------------------------------------------------------
    // Canonical nominal type identity (ADR Q3)
    //
    // Q3: '.' qualifies a package (`webui.App`, `rl.Rectangle`); '::' walks type-level
    // paths (`pkg.Type::Variant`, `Enum::Variant`). The same source type reaches sema in
    // different spellings: the parser joins annotations with '::' (`rl::Rectangle`),
    // struct literals keep the source separator (`rl.Rectangle`), and stdlib catalog
    // signatures are written from inside their module (`App`). Every name -> TypeId
    // conversion goes through `type_id_of`, which restores the package boundary by
    // *resolution* (is the leading segment a package/alias in this file?), never by
    // blind separator rewriting. The canonical key uses the import alias as written.
    // ---------------------------------------------------------------------------------

    [[nodiscard]] auto is_package_qualifier_sema(std::string_view head) const -> bool {
        if (head.empty()) return false;
        return is_cpp_qualifier_sema(head) || is_namespace_ident_sema(head) || is_vendor_alias_sema(head);
    }

    /// Canonicalizes a bare (unwrapped) nominal path: `rl::Rectangle` -> `rl.Rectangle`,
    /// `webui.App` -> `webui.App`, `Enum::Variant` / local `App` unchanged.
    [[nodiscard]] auto canonical_nominal_path_sema(std::string_view name) const -> std::string {
        // Generic application `pkg.Box[T]`: canonicalize the head and each argument.
        if (auto br = name.find('['); br != std::string_view::npos && name.ends_with(']') && br > 0) {
            std::string out = canonical_nominal_path_sema(name.substr(0, br)) + "[";
            std::string_view args = name.substr(br + 1, name.size() - br - 2);
            int depth = 0;
            std::size_t start = 0;
            for (std::size_t i = 0; i <= args.size(); ++i) {
                if (i < args.size() && (args[i] == '[' || args[i] == '(')) ++depth;
                else if (i < args.size() && (args[i] == ']' || args[i] == ')')) --depth;
                else if (i == args.size() || (args[i] == ',' && depth == 0)) {
                    std::string_view a = args.substr(start, i - start);
                    while (!a.empty() && a.front() == ' ') a.remove_prefix(1);
                    if (start > 0) out += ", ";
                    out += canonical_type_name_sema(a);
                    start = i + 1;
                }
            }
            return out + "]";
        }
        auto sep = name.find_first_of(".:");
        if (sep == std::string_view::npos) {
            // Bare name declared in the current (non-main) package is that package's type.
            if (!pkg_name_.empty() && pkg_name_ != "main") {
                for (const auto& st : cf_.ast.structs) {
                    if (st.name == name && st.package_name == pkg_name_) return std::format("{}.{}", pkg_name_, name);
                }
                for (const auto& en : cf_.ast.enums) {
                    if (en.name == name && en.package_name == pkg_name_) return std::format("{}.{}", pkg_name_, name);
                }
            }
            return std::string(name);
        }
        const bool is_dcolon = name[sep] == ':' && sep + 1 < name.size() && name[sep + 1] == ':';
        if (name[sep] == ':' && !is_dcolon) return std::string(name);
        std::string_view head = name.substr(0, sep);
        std::string_view rest = name.substr(sep + (is_dcolon ? 2 : 1));
        if (is_package_qualifier_sema(head)) return std::format("{}.{}", head, rest);
        return std::string(name);
    }

    /// Canonicalizes a full type spelling, recursing through the same wrappers that
    /// `TypeTable::from_name` decomposes.
    [[nodiscard]] auto canonical_type_name_sema(std::string_view name) const -> std::string {
        if (name.empty()) return {};
        if (name.starts_with("[]")) return "[]" + canonical_type_name_sema(name.substr(2));
        if (name.ends_with('?')) return canonical_type_name_sema(name.substr(0, name.size() - 1)) + "?";
        if (name.ends_with('!')) return canonical_type_name_sema(name.substr(0, name.size() - 1)) + "!";
        if (name.starts_with("&mut ")) return "&mut " + canonical_type_name_sema(name.substr(5));
        if (name.starts_with('&')) return "&" + canonical_type_name_sema(name.substr(1));
        if (name.starts_with("*mut ")) return "*mut " + canonical_type_name_sema(name.substr(5));
        if (name.starts_with('*')) return "*" + canonical_type_name_sema(name.substr(1));
        if (name.starts_with("own[") && name.ends_with(']')) return "own[" + canonical_type_name_sema(name.substr(4, name.size() - 5)) + "]";
        if (name.starts_with("shared[") && name.ends_with(']')) return "shared[" + canonical_type_name_sema(name.substr(7, name.size() - 8)) + "]";
        if (name.starts_with('[')) {
            // Fixed array `[N]T`
            if (auto rb = name.find(']'); rb != std::string_view::npos) {
                return std::string(name.substr(0, rb + 1)) + canonical_type_name_sema(name.substr(rb + 1));
            }
        }
        if (name.starts_with("map[")) {
            int depth = 0;
            for (std::size_t i = 3; i < name.size(); ++i) {
                if (name[i] == '[') ++depth;
                else if (name[i] == ']' && --depth == 0) {
                    return "map[" + canonical_type_name_sema(name.substr(4, i - 4)) + "]" + canonical_type_name_sema(name.substr(i + 1));
                }
            }
        }
        if (name.starts_with('(') || name.starts_with("fn")) return std::string(name); // tuples / fn types
        return canonical_nominal_path_sema(name);
    }

    /// Canonicalizes every nominal leaf of a parsed TypeRef (annotations, fields, params).
    [[nodiscard]] auto canonical_typeref_sema(TypeRef tr) const -> TypeRef {
        if (!tr.name.empty() && !tr.is_fn && !tr.is_tuple) tr.name = canonical_type_name_sema(tr.name);
        if (tr.elem_type) tr.elem_type = std::make_shared<TypeRef>(canonical_typeref_sema(*tr.elem_type));
        if (tr.key_type) tr.key_type = std::make_shared<TypeRef>(canonical_typeref_sema(*tr.key_type));
        for (auto& a : tr.type_args) a = canonical_typeref_sema(a);
        for (auto& t : tr.tuple_types) t = canonical_typeref_sema(t);
        return tr;
    }

    /// Single entry point for converting a type spelling into an interned TypeId.
    /// Strict: used for explicit annotations, which must be spelled as Q3 requires.
    [[nodiscard]] auto type_id_of(std::string_view name) const -> TypeId {
        return cf_.type_table.from_name(canonical_type_name_sema(name));
    }

    [[nodiscard]] auto type_id_of(const TypeRef& tr) const -> TypeId {
        return cf_.type_table.from_typeref(canonical_typeref_sema(tr));
    }

    /// Q2 hint: explain local-vs-package mismatches that share a leaf name.
    [[nodiscard]] auto nominal_mismatch_hint_sema(TypeId a, TypeId b) const -> std::string {
        auto is_nominal = [this](TypeId t) {
            if (!t.is_valid() || t.is_unknown()) return false;
            const auto k = cf_.type_table.get(t).kind;
            return k == TypeKind::NominalStruct || k == TypeKind::NominalEnum;
        };
        if (!is_nominal(a) || !is_nominal(b)) return {};
        std::string sa = cf_.type_table.to_string(a);
        std::string sb = cf_.type_table.to_string(b);
        auto leaf_of = [](const std::string& s) -> std::string_view {
            auto p = s.rfind('.');
            return p == std::string::npos ? std::string_view(s) : std::string_view(s).substr(p + 1);
        };
        const bool qa = sa.find('.') != std::string::npos;
        const bool qb = sb.find('.') != std::string::npos;
        if (qa == qb || leaf_of(sa) != leaf_of(sb)) return {};
        const std::string& local = qa ? sb : sa;
        const std::string& pkg = qa ? sa : sb;
        return std::format("; '{}' is a local type, distinct from '{}' (did you mean '{}'?)", local, pkg, pkg);
    }

    /// The qualifier this file uses for a stdlib/vendor module: its import alias if one
    /// was given, otherwise the module's own namespace.
    [[nodiscard]] auto local_qualifier_for_module_sema(std::string_view module_import, std::string_view default_ns) const -> std::string {
        for (const auto& imp : cf_.ast.imports) {
            if (imp.path == module_import && !imp.alias.empty()) return imp.alias;
        }
        return std::string(default_ns);
    }

    /// Stdlib catalog signatures name module-local types bare (`-> App`). Qualify such a
    /// return type with the module's qualifier in this file (`webui.App`), using the
    /// CF-117 metadata table as the source of truth for which names a module declares.
    [[nodiscard]] auto qualify_stdlib_type_sema(const StdFnSigInfo& sig, std::string_view qualifier) const -> std::string {
        const std::string& t = sig.unwrapped_return_type;
        const auto& idx = StdlibCatalogIndex::instance();
        auto declares = [&](std::string_view bare) {
            if (auto it = idx.ns_types_and_consts.find(sig.ns_alias); it != idx.ns_types_and_consts.end()) {
                return it->second.contains(std::string(bare));
            }
            return false;
        };
        // Find the innermost bare nominal (strip wrappers we know) and qualify it in place.
        std::size_t b = 0;
        std::size_t e = t.size();
        for (bool changed = true; changed;) {
            changed = false;
            std::string_view v(t.data() + b, e - b);
            for (std::string_view p : {"[]", "&mut ", "&", "*mut ", "*"}) {
                if (v.starts_with(p)) { b += p.size(); changed = true; break; }
            }
            if (!changed && (v.ends_with('?') || v.ends_with('!'))) { --e; changed = true; }
        }
        std::string_view core(t.data() + b, e - b);
        std::string_view head = core.substr(0, core.find('['));
        if (head.empty() || head.find_first_of(".:") != std::string_view::npos || !declares(head)) return t;
        // Built-in types (`str`, `i32`, `bool`, ...) are never module-qualified, even when a
        // namespace shares their name (e.g. `core:str` lists `str`).
        {
            const auto& d = cf_.type_table.get(cf_.type_table.from_name(head));
            if (d.kind != TypeKind::NominalStruct && d.kind != TypeKind::NominalEnum) return t;
        }
        return t.substr(0, b) + std::string(qualifier) + "." + std::string(core) + t.substr(e);
    }

    [[nodiscard]] auto resolve_ident(const std::string& name) const -> std::optional<Resolution> {
        Resolution res;
        // 1. Function parameter
        if (param_names_.contains(name)) {
            res.kind = SymbolKind::Param;
            res.canonical_name = sanitize_cpp_ident(name);
            res.is_mut = mut_vars_.contains(name);
            res.is_optional = optional_vars_.contains(name);
            res.is_smart_ptr = smart_ptr_vars_.contains(name);
            res.is_raw_ptr = raw_or_smart_ptr_vars_.contains(name) && !smart_ptr_vars_.contains(name);
            if (auto it = var_types_.find(name); it != var_types_.end()) {
                res.type_name = it->second;
            }
            return res;
        }
        // 2. Local variable
        if (local_defined_vars_.contains(name) || mut_vars_.contains(name) || imm_vars_.contains(name)) {
            res.kind = SymbolKind::Local;
            res.canonical_name = sanitize_cpp_ident(name);
            res.is_mut = mut_vars_.contains(name);
            res.is_optional = optional_vars_.contains(name);
            res.is_smart_ptr = smart_ptr_vars_.contains(name);
            res.is_raw_ptr = raw_or_smart_ptr_vars_.contains(name) && !smart_ptr_vars_.contains(name);
            if (auto it = var_types_.find(name); it != var_types_.end()) {
                res.type_name = it->second;
            }
            return res;
        }
        // 3. Enum Variant
        if (auto vit = cf_.variant_to_enum.find(name); vit != cf_.variant_to_enum.end()) {
            res.kind = SymbolKind::EnumVariant;
            res.owner_type = vit->second;
            res.canonical_name = name;
            res.type_name = vit->second;
            return res;
        }
        // 4. Struct
        if (!pkg_name_.empty()) {
            for (const auto& st : cf_.ast.structs) {
                std::string st_pkg = st.package_name.empty() ? cf_.ast.package_name : st.package_name;
                if (st.name == name && st_pkg == pkg_name_) {
                    res.kind = SymbolKind::Struct;
                    res.canonical_name = st.name;
                    res.type_name = st.name;
                    res.package_name = st.package_name;
                    return res;
                }
            }
        }
        for (const auto& st : cf_.ast.structs) {
            if (st.name == name) {
                res.kind = SymbolKind::Struct;
                res.canonical_name = st.name;
                res.type_name = st.name;
                res.package_name = st.package_name;
                return res;
            }
        }
        // 5. Enum
        if (cf_.enums.contains(name)) {
            res.kind = SymbolKind::Struct;
            res.canonical_name = name;
            res.type_name = name;
            return res;
        }
        // 6. Global variable
        if (!pkg_name_.empty()) {
            for (const auto& gv : cf_.ast.global_vars) {
                std::string gv_pkg = gv.package_name.empty() ? cf_.ast.package_name : gv.package_name;
                if (gv.name == name && gv_pkg == pkg_name_) {
                    res.kind = SymbolKind::Global;
                    res.package_name = gv.package_name;
                    res.type_name = gv.type ? gv.type->name : "";
                    if (!gv.package_name.empty() && cf_.colliding_gv_names.contains(gv.name)) {
                        res.canonical_name = std::format("cfire_pkg_{}__{}", gv.package_name, sanitize_cpp_ident(gv.name));
                    } else {
                        res.canonical_name = sanitize_cpp_ident(gv.name);
                    }
                    return res;
                }
            }
        }
        for (const auto& gv : cf_.ast.global_vars) {
            if (gv.name == name) {
                res.kind = SymbolKind::Global;
                res.package_name = gv.package_name;
                res.type_name = gv.type ? gv.type->name : "";
                if (!gv.package_name.empty() && cf_.colliding_gv_names.contains(gv.name)) {
                    res.canonical_name = std::format("cfire_pkg_{}__{}", gv.package_name, sanitize_cpp_ident(gv.name));
                } else {
                    res.canonical_name = sanitize_cpp_ident(gv.name);
                }
                return res;
            }
        }
        // 7. Function
        if (!pkg_name_.empty()) {
            if (auto pit = cf_.fn_sigs.find(pkg_name_ + "." + name); pit != cf_.fn_sigs.end()) {
                res.kind = SymbolKind::Function;
                res.package_name = pit->second.package_name;
                if (!pit->second.package_name.empty() && cf_.colliding_fn_names.contains(pit->second.name)) {
                    res.canonical_name = std::format("cfire_pkg_{}__{}", pit->second.package_name, sanitize_cpp_ident(pit->second.name));
                } else {
                    res.canonical_name = sanitize_cpp_ident(pit->second.name);
                }
                return res;
            }
        }
        if (auto it = cf_.fn_sigs.find(name); it != cf_.fn_sigs.end()) {
            res.kind = SymbolKind::Function;
            res.package_name = it->second.package_name;
            if (!it->second.package_name.empty() && cf_.colliding_fn_names.contains(it->second.name)) {
                res.canonical_name = std::format("cfire_pkg_{}__{}", it->second.package_name, sanitize_cpp_ident(it->second.name));
            } else {
                res.canonical_name = sanitize_cpp_ident(it->second.name);
            }
            return res;
        }
        // 8. CoreNamespace or Vendor
        if (is_namespace_ident_sema(name) || is_vendor_alias_sema(name) || name == "cpp") {
            res.kind = SymbolKind::CoreNamespace;
            res.canonical_name = name;
            return res;
        }
        return std::nullopt;
    }

    [[nodiscard]] auto resolve_member(const MemberExpr& mem) const -> std::optional<Resolution> {
        Resolution res;
        if (!mem.object) return std::nullopt;

        // W4: methods on builtin types lower to the `cfire::bm` overload set.
        if (const BuiltinMethod* bm = lookup_builtin_method(mem); bm && bm->lowered) {
            res.kind = SymbolKind::Method;
            res.owner_type = "builtin";
            res.canonical_name = std::format("cfire::bm::{}", bm->name);
            TypeId rt = resolve_builtin_type(bm->ret, builtin_self_type(mem.object));
            if (rt.is_valid()) res.type_name = cf_.type_table.to_string(rt);
            return res;
        }

        // 1. If object is an IdentExpr
        if (const auto* id = std::get_if<IdentExpr>(&mem.object->node)) {
            // Foreign FFI prefix "cpp" or alias
            if (id->name == "cpp" || is_cpp_qualifier_sema(id->name)) {
                res.kind = SymbolKind::CoreNamespace;
                res.canonical_name = mem.field;
                return res;
            }
            // Vendor alias
            if (is_vendor_alias_sema(id->name)) {
                res.kind = SymbolKind::Function;
                res.canonical_name = mem.field;
                return res;
            }
            // Enum variant: Shape::Circle or Shape.Circle
            if (cf_.enums.contains(id->name)) {
                res.kind = SymbolKind::EnumVariant;
                res.owner_type = id->name;
                res.canonical_name = std::format("{}::{}", id->name, mem.field);
                res.type_name = id->name;
                return res;
            }
            // If id is NOT a local/param variable, check core namespace and packages
            if (!param_names_.contains(id->name) && !local_defined_vars_.contains(id->name) &&
                !mut_vars_.contains(id->name) && !imm_vars_.contains(id->name)) {
                std::string pkg_qualified = id->name + "." + mem.field;
                if (auto it = cf_.fn_sigs.find(pkg_qualified); it != cf_.fn_sigs.end()) {
                    res.kind = SymbolKind::Function;
                    res.package_name = id->name;
                    if (cf_.colliding_fn_names.contains(mem.field)) {
                        res.canonical_name = std::format("cfire_pkg_{}__{}", id->name, sanitize_cpp_ident(mem.field));
                    } else {
                        res.canonical_name = sanitize_cpp_ident(mem.field);
                    }
                    return res;
                }
                for (const auto& gv : cf_.ast.global_vars) {
                    if (gv.name == mem.field && gv.package_name == id->name) {
                        res.kind = SymbolKind::Global;
                        res.package_name = id->name;
                        res.type_name = gv.type ? gv.type->name : "";
                        if (cf_.colliding_gv_names.contains(mem.field)) {
                            res.canonical_name = std::format("cfire_pkg_{}__{}", id->name, sanitize_cpp_ident(mem.field));
                        } else {
                            res.canonical_name = sanitize_cpp_ident(mem.field);
                        }
                        return res;
                    }
                }
                if (is_namespace_ident_sema(id->name)) {
                    res.kind = SymbolKind::CoreNamespace;
                    std::string actual_ns = resolve_stdlib_ns_sema(id->name);
                    if (actual_ns.empty()) actual_ns = id->name;
                    for (const auto& imp : cf_.ast.imports) {
                        if (imp.alias == id->name) {
                            auto colon = imp.path.rfind(':');
                            auto slash = imp.path.rfind('/');
                            std::size_t sep = std::string::npos;
                            if (colon != std::string::npos && slash != std::string::npos) sep = std::max(colon, slash);
                            else if (colon != std::string::npos) sep = colon;
                            else sep = slash;
                            actual_ns = (sep == std::string::npos) ? imp.path : imp.path.substr(sep + 1);
                            break;
                        }
                    }
                    if (actual_ns == "os" && (mem.field == "stdin" || mem.field == "stdout" || mem.field == "stderr")) {
                        res.canonical_name = std::format("cfire::os::{}_file", mem.field);
                    } else {
                        res.canonical_name = std::format("cfire::{}::{}", actual_ns, mem.field);
                    }
                    return res;
                }
            }
        }

        // 2. Struct fields and methods
        std::string owner_type = type_name_of(mem.object);
        if (!owner_type.empty()) {
            if (auto sit = cf_.struct_tags.find(owner_type); sit != cf_.struct_tags.end()) {
                for (const auto& f : sit->second) {
                    if (f.field_name == mem.field) {
                        res.kind = SymbolKind::Field;
                        res.owner_type = owner_type;
                        res.canonical_name = sanitize_cpp_ident(mem.field);
                        res.type_name = f.field_type.name;
                        res.is_optional = f.field_type.is_optional;
                        res.is_smart_ptr = !f.field_type.smart_wrap.empty();
                        res.is_raw_ptr = f.field_type.is_ptr || f.field_type.is_mut_ptr;
                        if (const auto* oid = std::get_if<IdentExpr>(&mem.object->node)) {
                            if (smart_ptr_vars_.contains(oid->name) || raw_or_smart_ptr_vars_.contains(oid->name) || optional_vars_.contains(oid->name)) {
                                res.is_auto_deref = true;
                            }
                        }
                        return res;
                    }
                }
            }
            if (cf_.methods.contains(owner_type + "::" + mem.field) || cf_.methods.contains(owner_type + "." + mem.field)) {
                res.kind = SymbolKind::Method;
                res.owner_type = owner_type;
                res.canonical_name = std::format("cfire_method_{}", mem.field);
                return res;
            }
        }

        // 3. User method across types
        if (cf_.method_owners.contains(mem.field) || cf_.method_owner.contains(mem.field)) {
            res.kind = SymbolKind::Method;
            res.canonical_name = std::format("cfire_method_{}", mem.field);
            return res;
        }

        return std::nullopt;
    }

    [[nodiscard]] auto expr_returns_fallible_sema(const ExprPtr& e, std::string* out_callee = nullptr, std::string* out_ret = nullptr) const -> bool {
        if (!e) return false;
        if (const auto* or_ex = std::get_if<OrExpr>(&e->node)) {
            if (or_ex->fallback_val) {
                return expr_returns_fallible_sema(or_ex->fallback_val, out_callee, out_ret);
            }
            return false;
        }
        if (const auto* call = std::get_if<CallExpr>(&e->node)) {
            if (const auto* cid = std::get_if<IdentExpr>(&call->callee->node);
                cid && cid->name == "make" && call->type_args.size() == 1 && !is_ident_in_scope_sema("make")) {
                // Q25: `make(T, ...)` is fallible exactly when the constructor it calls is.
                const TypeRef& t = call->type_args[0];
                const TypeRef& target = (!t.smart_wrap.empty() && t.elem_type) ? *t.elem_type : t;
                if (const FnDecl* ctor = find_user_ctor(target)) {
                    if (out_callee) *out_callee = "make";
                    if (out_ret) *out_ret = format_type_ref_sema(target);
                    return ctor->return_type.is_fallible;
                }
                if (const auto* sc = find_stdlib_ctor(target.name)) {
                    if (const auto* sig = StdlibCatalogIndex::instance().find_ns_fn(sc->ns, std::string(sc->type) + ".make")) {
                        if (out_callee) *out_callee = "make";
                        if (out_ret) *out_ret = std::format("{}.{}", sc->ns, sc->type);
                        return sig->is_fallible;
                    }
                }
                return false;
            }
            if (const auto* cid = std::get_if<IdentExpr>(&call->callee->node)) {
                const FnDecl* sig = nullptr;
                if (!pkg_name_.empty()) {
                    if (auto pit = cf_.fn_sigs.find(pkg_name_ + "." + cid->name); pit != cf_.fn_sigs.end()) {
                        sig = &pit->second;
                    }
                }
                if (!sig) {
                    if (auto it = cf_.fn_sigs.find(cid->name); it != cf_.fn_sigs.end()) {
                        sig = &it->second;
                    }
                }
                if (sig && sig->return_type.is_fallible) {
                    if (out_callee) *out_callee = cid->name;
                    if (out_ret) *out_ret = format_type_ref_sema(sig->return_type);
                    return true;
                }
                if (fn_ret_fallible_vars_.contains(cid->name)) {
                    if (out_callee) *out_callee = cid->name;
                    if (out_ret) *out_ret = "T!";
                    return true;
                }
            } else if (const auto* mem = std::get_if<MemberExpr>(&call->callee->node)) {
                const auto* obj_id = mem->object ? std::get_if<IdentExpr>(&mem->object->node) : nullptr;
                const auto& idx = StdlibCatalogIndex::instance();
                if (obj_id && idx.has_namespace(obj_id->name) && !is_ident_in_scope_sema(obj_id->name)) {
                    if (const auto* sfn = idx.find_ns_fn(obj_id->name, mem->field)) {
                        if (sfn->is_fallible) {
                            if (out_callee) *out_callee = obj_id->name + "." + mem->field;
                            if (out_ret) *out_ret = sfn->return_type;
                            return true;
                        }
                    }
                }
                if (const auto* sig = resolve_member_call_sig(*mem)) {
                    if (sig->return_type.is_fallible) {
                        if (out_callee) *out_callee = mem->field;
                        if (out_ret) *out_ret = format_type_ref_sema(sig->return_type);
                        return true;
                    }
                }
                if (obj_id) {
                    if (auto vt = var_types_.find(obj_id->name); vt != var_types_.end()) {
                        if (auto mit = idx.receiver_methods.find(vt->second + "." + mem->field); mit != idx.receiver_methods.end()) {
                            if (mit->second.is_fallible) {
                                if (out_callee) *out_callee = obj_id->name + "." + mem->field;
                                if (out_ret) *out_ret = mit->second.return_type;
                                return true;
                            }
                        }
                    }
                }
                if (mem->object) {
                    std::string recv_t = type_name_of(mem->object);
                    if (!recv_t.empty()) {
                        if (auto mit = idx.receiver_methods.find(recv_t + "." + mem->field); mit != idx.receiver_methods.end()) {
                            if (mit->second.is_fallible) {
                                if (out_callee) *out_callee = mem->field;
                                if (out_ret) *out_ret = mit->second.return_type;
                                return true;
                            }
                        }
                    }
                }
            }
        }
        return false;
    }

    [[nodiscard]] auto is_expr_fallible_for_unwrap_sema(const ExprPtr& e) const -> bool {
        if (!e) return false;
        if (const auto* or_ex = std::get_if<OrExpr>(&e->node)) {
            if (or_ex->fallback_val) {
                return is_expr_fallible_for_unwrap_sema(or_ex->fallback_val);
            }
            return false;
        }
        if (std::holds_alternative<TryExpr>(e->node)) {
            return false;
        }
        if (expr_returns_fallible_sema(e)) return true;
        if (const auto* id = std::get_if<IdentExpr>(&e->node)) {
            return fallible_vars_.contains(id->name);
        }
        if (const auto* mem = std::get_if<MemberExpr>(&e->node)) {
            if (!mem->is_namespace && mem->object) {
                std::string owner_t = type_name_of(mem->object);
                if (!owner_t.empty()) {
                    for (const auto& st : cf_.ast.structs) {
                        if (st.name == owner_t) {
                            for (const auto& fld : st.fields) {
                                if (fld.name == mem->field) return fld.type.is_fallible;
                            }
                        }
                    }
                }
            }
            return false;
        }
        if (const auto* call = std::get_if<CallExpr>(&e->node)) {
            if (const auto* cid = std::get_if<IdentExpr>(&call->callee->node)) {
                if (cid->name == "ok" || cid->name == "err" || cid->name == "error" || cid->name == "Err") {
                    return true;
                }
                if (fn_ret_fallible_vars_.contains(cid->name)) {
                    return true;
                }
                if (cid->name.starts_with("cpp::") || cid->name.starts_with("cpp.") ||
                    cid->name.starts_with("rl::") || cid->name.starts_with("rg::") ||
                    cf_.cpp_fn_params.contains(cid->name)) {
                    return true;
                }
                return false;
            }
            if (const auto* mem = std::get_if<MemberExpr>(&call->callee->node)) {
                const auto& idx = StdlibCatalogIndex::instance();
                const auto* obj_id = mem->object ? std::get_if<IdentExpr>(&mem->object->node) : nullptr;
                if (obj_id && !is_ident_in_scope_sema(obj_id->name)) {
                    if (obj_id->name == "cpp" || obj_id->name == "rl" || obj_id->name == "rg") {
                        return true;
                    }
                    if ((obj_id->name == "fs" && (mem->field == "read_all" || mem->field == "write_all")) ||
                        (obj_id->name == "os" && mem->field == "socketpair")) {
                        return true;
                    }
                    if (idx.has_namespace(obj_id->name)) {
                        if (const auto* sfn = idx.find_ns_fn(obj_id->name, mem->field)) {
                            return sfn->is_fallible;
                        }
                        return false;
                    }
                }
                if (const auto* sig = resolve_member_call_sig(*mem)) {
                    return sig->return_type.is_fallible;
                }
                if (mem->field == "read_str" || mem->field == "write_str" ||
                    mem->field == "read_all" || mem->field == "read" || mem->field == "write" ||
                    mem->field == "call_rpc" || mem->field == "run_webview_test" ||
                    mem->field == "generate_struct" || mem->field == "generate_struct_from_file" ||
                    mem->field == "run_struct" || mem->field == "run_struct_validated" ||
                    mem->field == "embed" || mem->field == "embed_image" ||
                    mem->field == "ocr" || mem->field == "detect_language" ||
                    mem->field == "stream" || mem->field == "ask" || mem->field == "tx") {
                    return true;
                }
                if (auto uit = idx.unambiguous_methods.find(mem->field); uit != idx.unambiguous_methods.end()) {
                    if (uit->second.is_fallible) return true;
                }
                std::string dot_suffix = "." + mem->field;
                for (const auto& [k, minfo] : idx.receiver_methods) {
                    if (k.ends_with(dot_suffix) && minfo.is_fallible) return true;
                }
                std::string col_suffix = "::" + mem->field;
                for (const auto& [k, mdecl] : cf_.methods) {
                    if (k.ends_with(col_suffix) && mdecl.return_type.is_fallible) return true;
                }
                const auto* oid = mem->object ? std::get_if<IdentExpr>(&mem->object->node) : nullptr;
                if (oid && (oid->name == "cpp" || is_cpp_qualifier_sema(oid->name) || is_vendor_alias_sema(oid->name))) return true;
            }
        }
        return false;
    }

    [[nodiscard]] auto is_expr_optional_for_unwrap_sema(const ExprPtr& e) const -> bool {
        if (!e) return false;
        if (const auto* or_ex = std::get_if<OrExpr>(&e->node)) {
            if (or_ex->fallback_val) {
                return is_expr_optional_for_unwrap_sema(or_ex->fallback_val);
            }
            return false;
        }
        if (std::holds_alternative<TryExpr>(e->node)) {
            return false;
        }
        if (expr_returns_optional_sema(e)) return true;
        if (const auto* call = std::get_if<CallExpr>(&e->node)) {
            if (const auto* cid = std::get_if<IdentExpr>(&call->callee->node)) {
                if (cid->name.starts_with("cpp::") || cid->name.starts_with("cpp.") ||
                    cid->name.starts_with("rl::") || cid->name.starts_with("rg::") ||
                    cf_.cpp_fn_params.contains(cid->name)) {
                    return true;
                }
                return false;
            }
            if (const auto* mem = std::get_if<MemberExpr>(&call->callee->node)) {
                const auto& idx = StdlibCatalogIndex::instance();
                const auto* obj_id = mem->object ? std::get_if<IdentExpr>(&mem->object->node) : nullptr;
                if (obj_id && !is_ident_in_scope_sema(obj_id->name)) {
                    if (obj_id->name == "cpp" || obj_id->name == "rl" || obj_id->name == "rg") {
                        return true;
                    }
                    if (idx.has_namespace(obj_id->name)) {
                        if (const auto* sfn = idx.find_ns_fn(obj_id->name, mem->field)) {
                            return sfn->is_optional;
                        }
                        return false;
                    }
                }
                if (const auto* sig = resolve_member_call_sig(*mem)) {
                    return sig->return_type.is_optional && !sig->return_type.is_fallible;
                }
                if (auto uit = idx.unambiguous_methods.find(mem->field); uit != idx.unambiguous_methods.end()) {
                    if (uit->second.is_optional) return true;
                }
                std::string dot_suffix = "." + mem->field;
                for (const auto& [k, minfo] : idx.receiver_methods) {
                    if (k.ends_with(dot_suffix) && minfo.is_optional) return true;
                }
                std::string col_suffix = "::" + mem->field;
                for (const auto& [k, mdecl] : cf_.methods) {
                    if (k.ends_with(col_suffix) && mdecl.return_type.is_optional) return true;
                }
                const auto* oid = mem->object ? std::get_if<IdentExpr>(&mem->object->node) : nullptr;
                if (oid && (oid->name == "cpp" || is_cpp_qualifier_sema(oid->name) || is_vendor_alias_sema(oid->name))) return true;
            }
        }
        return false;
    }

    [[nodiscard]] auto validate_move_operand(
        const ExprPtr& operand,
        bool required_by_sig,
        int line,
        int col
    ) -> std::expected<void, std::string> {
        if (!operand) return {};
        if (std::holds_alternative<UnaryExpr>(operand->node)) {
            return emit_error(diag_at(line, col, "cfire/INVALID-MOVE", std::format("cannot use 'move' on temporary or unary expression; 'move' only applies to owned variables")));
        }
        auto pi = extract_path_info(operand);
        if (!pi) {
            return emit_error(diag_at(line, col, "cfire/INVALID-MOVE", std::format("cannot use 'move' on temporary or literal expression; 'move' only applies to owned variables")));
        }
        if (pi->path == pi->root && scalar_primitive_vars_.contains(pi->root) && !required_by_sig) {
            std::string tname = lookup_or(var_types_, pi->root, "primitive");
            return emit_error(diag_at(line, col, "cfire/INVALID-MOVE", std::format("'move' has no effect on Copy primitive variable '{}' of type '{}'", pi->root, tname)));
        }
        return {};
    }

    [[nodiscard]] static auto is_effectful_stmt_expr(const ExprPtr& e) -> bool {
        if (!e) return true;
        if (std::holds_alternative<CallExpr>(e->node) ||
            std::holds_alternative<MatchExpr>(e->node)) {
            return true;
        }
        if (const auto* tr = std::get_if<TryExpr>(&e->node)) {
            return is_effectful_stmt_expr(tr->inner);
        }
        if (const auto* or_ex = std::get_if<OrExpr>(&e->node)) {
            return is_effectful_stmt_expr(or_ex->inner) || !or_ex->handler_body.empty();
        }
        if (const auto* id = std::get_if<IdentExpr>(&e->node)) {
            return id->name == "break" || id->name == "continue";
        }
        return false;
    }

    [[nodiscard]] static auto extract_root_ident_sema(const ExprPtr& e) -> std::string {
        if (!e) return "";
        if (const auto* id = std::get_if<IdentExpr>(&e->node)) return id->name;
        if (const auto* mem = std::get_if<MemberExpr>(&e->node)) return extract_root_ident_sema(mem->object);
        if (const auto* idx = std::get_if<IndexExpr>(&e->node)) return extract_root_ident_sema(idx->object);
        if (const auto* sl = std::get_if<SliceExpr>(&e->node)) return extract_root_ident_sema(sl->object);
        if (const auto* call = std::get_if<CallExpr>(&e->node)) return extract_root_ident_sema(call->callee);
        if (const auto* un = std::get_if<UnaryExpr>(&e->node)) return extract_root_ident_sema(un->operand);
        if (const auto* tr = std::get_if<TryExpr>(&e->node)) return extract_root_ident_sema(tr->inner);
        if (const auto* or_ex = std::get_if<OrExpr>(&e->node)) return extract_root_ident_sema(or_ex->inner);
        if (const auto* bin = std::get_if<BinaryExpr>(&e->node)) {
            auto l = extract_root_ident_sema(bin->left);
            return !l.empty() ? l : extract_root_ident_sema(bin->right);
        }
        return "";
    }

    [[nodiscard]] auto check_expr(const ExprPtr& e) -> std::expected<void, std::string> {
        if (!e) return {};
        if (auto* id = std::get_if<IdentExpr>(&e->node)) {
            id->resolution = resolve_ident(id->name);
            if (id->name == "_") return {};
            if (id->name == "break" || id->name == "continue") {
                if (loop_depth_ == 0) {
                    return emit_error(diag_at(e->line, e->col, "cfire/BREAK-OUTSIDE-LOOP", std::format("'{}' cannot be used outside of a 'for' loop", id->name)));
                }
                return {};
            }
            if (auto r = record_var_read(id->name, e->line, e->col, e->id); !r) return r;
            auto dot = id->name.find('.');
            auto dcol = id->name.find("::");
            std::size_t sep = std::min(dot, dcol);
            if (sep != std::string::npos) {
                std::string prefix = id->name.substr(0, sep);
                if (auto r = check_stdlib_ns_imported(prefix, e->line, e->col); !r) return r;
            }
            if (!is_known_identifier_sema(id->name)) {
                if (cf_.shadowed_default_qualifiers.contains(id->name)) {
                    std::string active_alias = lookup_or_empty(cf_.shadowed_default_qualifiers, id->name);
                    return emit_error(diag_at(e->line, e->col, "cfire/UNDECLARED-IDENT", std::format("module '{}' is imported as '{}'; use '{}.<member>'", id->name, active_alias, active_alias)));
                }
                if (cf_.cpp_fn_params.contains(id->name)) {
                    std::string cpp_q = cf_.cpp_qualifiers.empty() ? "cpp" : *cf_.cpp_qualifiers.begin();
                    return emit_error(diag_at(e->line, e->col, "cfire/UNDECLARED-IDENT", std::format("undeclared identifier '{}'; did you mean '{}.{}'?", id->name, cpp_q, id->name)));
                }
                return emit_error(diag_at(e->line, e->col, "cfire/UNDECLARED-IDENT", std::format("undeclared identifier '{}'", id->name)));
            }
            return {};
        }
        if (const auto* lit = std::get_if<LiteralExpr>(&e->node)) {
            return check_literal_expr(*lit, e->line);
        }
        if (const auto* un = std::get_if<UnaryExpr>(&e->node)) {
            if (auto r = check_unary_expr(*un, e->line, e->col); !r) return r;
            if (un->op == "!" || un->op == "not") return check_bool_condition(un->operand, "operand of '!'");
            return {};
        }
        if (const auto* bin = std::get_if<BinaryExpr>(&e->node)) {
            return check_binary_expr(*bin, e->line, e->col);
        }
        if (auto* mem = std::get_if<MemberExpr>(&e->node)) {
            if (auto r = check_member_expr(*mem, e->line, e->col); !r) return r;
            mem->resolution = resolve_member(*mem);
            return {};
        }
        if (const auto* idx = std::get_if<IndexExpr>(&e->node)) {
            return check_index_expr(*idx, e->line, e->col);
        }
        if (const auto* sl = std::get_if<SliceExpr>(&e->node)) {
            if (auto r = check_expr(sl->object); !r) return r;
            if (sl->start) if (auto r = check_expr(sl->start); !r) return r;
            if (sl->end) if (auto r = check_expr(sl->end); !r) return r;
            return {};
        }
        if (const auto* tr = std::get_if<TryExpr>(&e->node)) {
            if (tr->inner) unwrapped_exprs_.insert(tr->inner);
            return check_try_expr(*tr, e->line, e->col);
        }
        if (const auto* or_ex = std::get_if<OrExpr>(&e->node)) {
            if (or_ex->inner) unwrapped_exprs_.insert(or_ex->inner);
            if (unwrapped_exprs_.contains(e) && or_ex->fallback_val) {
                unwrapped_exprs_.insert(or_ex->fallback_val);
            }
            return check_or_expr(*or_ex, e->line, e->col, e);
        }
        if (const auto* st = std::get_if<StructLitExpr>(&e->node)) {
            auto dot = st->type_name.find('.');
            auto dcol = st->type_name.find("::");
            std::size_t sep = std::min(dot, dcol);
            if (sep != std::string::npos) {
                std::string prefix = st->type_name.substr(0, sep);
                if (auto r = check_stdlib_ns_imported(prefix, e->line, e->col); !r) return r;
            }
            if (dcol != std::string::npos && (dot == std::string::npos || dcol < dot)) {
                std::string qual = st->type_name.substr(0, dcol);
                if (!cf_.enums.contains(qual) &&
                    (qual == "cpp" || is_cpp_qualifier_sema(qual) || is_vendor_alias_sema(qual) ||
                     is_namespace_ident_sema(qual) || cf_.shadowed_default_qualifiers.contains(qual))) {
                    std::string rest = st->type_name.substr(dcol + 2);
                    return emit_error(diag_at(e->line, e->col, "cfire/DEPRECATED-SEPARATOR", std::format("use '{}.{}' instead of '{}::{}'", qual, rest, qual, rest), {{"kind", "qualifier"}}));
                }
            }
            if (dot != std::string::npos) {
                std::string qual = st->type_name.substr(0, dot);
                if (cf_.shadowed_default_qualifiers.contains(qual)) {
                    std::string active_alias = lookup_or_empty(cf_.shadowed_default_qualifiers, qual);
                    std::string rest = st->type_name.substr(dot + 1);
                    return emit_error(diag_at(e->line, e->col, "cfire/UNDECLARED-IDENT", std::format("module '{}' is imported as '{}'; use '{}.{}'", qual, active_alias, active_alias, rest)));
                }
            }
            for (const auto& f : st->fields) {
                if (auto r = check_expr(f.value); !r) return r;
            }
            if (auto r = check_struct_literal_fields(*st, e->line, e->col); !r) return r;
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
            ++in_closure_depth_;
            int saved_loop_depth = loop_depth_;
            int saved_invalid_unwrap = in_invalid_unwrap_context_;
            int saved_escaping_entry_scope = escaping_closure_entry_scope_;
            bool was_escaping = cl->is_escaping;
            if (was_escaping) {
                ++in_escaping_closure_depth_;
                escaping_closure_entry_scope_ = scope_depth_;
            }
            loop_depth_ = 0;
            in_invalid_unwrap_context_ = 0;
            auto cl_guard = cfire::scope_exit([this, saved_loop_depth, saved_invalid_unwrap, was_escaping, saved_escaping_entry_scope] {
                --in_closure_depth_;
                if (was_escaping) {
                    --in_escaping_closure_depth_;
                    escaping_closure_entry_scope_ = saved_escaping_entry_scope;
                }
                loop_depth_ = saved_loop_depth;
                in_invalid_unwrap_context_ = saved_invalid_unwrap;
            });
            ScopeGuard sg(*this);
            auto hint_it = closure_param_hints_.find(cl);
            for (std::size_t i = 0; i < cl->params.size(); ++i) {
                bool is_mut = (i < cl->param_intents.size()) &&
                              (cl->param_intents[i] == "mut" || cl->param_intents[i] == "out");
                bind_scoped_var(cl->params[i], is_mut);
                bool typed = i < cl->param_types.size() && cl->param_types[i].has_value();
                if (typed) {
                    var_types_[cl->params[i]] = format_type_ref_sema(*cl->param_types[i]);
                } else if (hint_it != closure_param_hints_.end() && i < hint_it->second.size()) {
                    var_types_[cl->params[i]] = hint_it->second[i];
                }
            }
            return check_block_stmts(cl->body);
        }
        if (auto* call = std::get_if<CallExpr>(&e->node)) {
            if (auto r = check_call_expr(*call, e->line, e->col, e); !r) return r;
            if (call->callee) {
                if (const auto* cid = std::get_if<IdentExpr>(&call->callee->node)) {
                    call->resolution = cid->resolution;
                } else if (const auto* cmem = std::get_if<MemberExpr>(&call->callee->node)) {
                    call->resolution = cmem->resolution;
                }
            }
            return {};
        }
        if (const auto* me = std::get_if<MatchExpr>(&e->node)) {
            if (auto r = check_match_common(me->target, me->is_move, me->arms, me->line, me->col, true); !r) return r;
            me->result_type = unified_match_result_type(*me);
            return {};
        }
        return {};
    }

    // Primitive type shared by all arms of a match expression, treating bare literal arms as adaptable.
    [[nodiscard]] auto unified_match_result_type(const MatchExpr& me) const -> std::string {
        std::string result;
        for (const auto& arm : me.arms) {
            if (arm.body.empty()) continue;
            const auto* es = std::get_if<ExprStmt>(&arm.body.back()->node);
            if (!es || !es->expr) return "";
            bool is_literal = std::holds_alternative<LiteralExpr>(es->expr->node);
            if (const auto* un = std::get_if<UnaryExpr>(&es->expr->node); un && un->op == "-" && un->operand &&
                std::holds_alternative<LiteralExpr>(un->operand->node)) {
                is_literal = true;
            }
            if (is_literal) continue;
            TypeId t = infer_expr_type_id_sema(es->expr);
            if (t.is_unknown() || t.index < TypeId::Bool().index || t.index > TypeId::Str().index) return "";
            std::string n = cf_.type_table.to_string(t);
            if (!result.empty() && result != n) return "";
            result = n;
        }
        return result;
    }

    [[nodiscard]] auto check_try_expr(const TryExpr& tr, int line, int col) -> std::expected<void, std::string> {
        if (in_defer_depth_ > 0) {
            if (auto r = emit_error(diag_at(line, col, "cfire/TRY-IN-DEFER", std::format("cannot use 'try' inside a 'defer' block; handle errors explicitly with 'or'"))); !r) return r;
        } else if (!fn_is_fallible_ && in_closure_depth_ == 0) {
            std::string ret_desc = fn_ret_str_.empty() ? "void" : fn_ret_str_;
            if (auto r = emit_error(diag_at(line, col, "cfire/TRY-IN-NON-FALLIBLE-FN", std::format("cannot use 'try' in non-fallible function '{}' (returns '{}'); mark return type with '!' or handle error with 'or'", fn_name_.empty() ? "<anonymous>" : fn_name_, ret_desc))); !r) return r;
        }
        if (in_invalid_unwrap_context_ > 0) {
            if (auto r = emit_error(diag_at(line, col, "cfire/INVALID-TRY-CONTEXT", std::format("cannot use 'try' inside short-circuit operand or 'for' loop condition; hoist into a statement or use 'or fallback'"))); !r) return r;
        }
        std::size_t errors_before = errors_.size();
        if (auto r = check_expr(tr.inner); !r) return r;
        if (errors_.size() == errors_before) {
            if (!is_expr_fallible_for_unwrap_sema(tr.inner)) {
                if (is_expr_optional_for_unwrap_sema(tr.inner)) {
                    if (auto r = emit_error(diag_at(line, col, "cfire/TRY-ON-OPTIONAL", std::format("cannot use 'try' on optional 'T?' value; 'try' only propagates fallible 'T!' errors — use 'or fallback' or 'or {{ ... }}' instead"))); !r) return r;
                } else {
                    if (auto r = emit_error(diag_at(line, col, "cfire/REDUNDANT-TRY", std::format("'try' used on non-fallible expression; remove 'try'"))); !r) return r;
                }
            }
        }
        return {};
    }

    [[nodiscard]] static auto is_likely_interpolation_placeholder(std::string_view sv, const InterpPlaceholder& ph) -> bool {
        if (ph.start_pos + 1 >= sv.size()) return false;
        char first_raw = sv[ph.start_pos + 1];
        if (std::isspace(static_cast<unsigned char>(first_raw))) return false;
        if (!(std::isalnum(static_cast<unsigned char>(first_raw)) || first_raw == '_' || first_raw == '(' || first_raw == '*')) {
            return false;
        }
        // Exclude JSON/CSS/code blocks containing ';' or single ':'
        bool in_q = false;
        for (std::size_t i = 0; i < ph.expr_text.size(); ++i) {
            char c = ph.expr_text[i];
            if (c == '"' && (i == 0 || ph.expr_text[i - 1] != '\\')) in_q = !in_q;
            else if (!in_q) {
                if (c == ';') return false;
                if (c == ':') {
                    bool is_dcol = (i + 1 < ph.expr_text.size() && ph.expr_text[i + 1] == ':') ||
                                   (i > 0 && ph.expr_text[i - 1] == ':');
                    if (!is_dcol) return false;
                }
            }
        }
        // Exclude pure integer regex quantifiers like `\d{4}`
        bool all_digits = !ph.expr_text.empty() && std::ranges::all_of(ph.expr_text, [](unsigned char c) {
            return std::isdigit(c) || c == ',';
        });
        if (all_digits) return false;
        return true;
    }

    [[nodiscard]] auto check_literal_expr(const LiteralExpr& lit, int line, int col = 1) -> std::expected<void, std::string> {
        if (lit.kind == TokenKind::String && lit.value.contains('{')) {
            lit.interp_snippets.clear();
            auto placeholders = scan_interpolation_placeholders(lit.value);
            for (const auto& ph : placeholders) {
                if (!is_likely_interpolation_placeholder(lit.value, ph)) continue;
                int expr_col = std::max(1, col) + 1 + static_cast<int>(ph.expr_offset);
                auto parsed = parse_expr_snippet(path_, ph.expr_text, line, expr_col);
                if (!parsed) {
                    if (auto r = emit_error(diag_at(line, expr_col, "cfire/INVALID-INTERPOLATION", std::format("invalid expression '{}' in string interpolation", ph.expr_text))); !r) return r;
                    continue;
                }
                if (!ph.expr_text.empty() && std::isdigit(static_cast<unsigned char>(ph.expr_text.front()))) {
                    if (auto r = emit_error(diag_at(line, expr_col, "cfire/AMBIGUOUS-INTERPOLATION", std::format("ambiguous string interpolation '{{{}}}'; expressions starting with a number literal must be parenthesized as '{{({})}}' to distinguish from positional format arguments", ph.expr_text, ph.expr_text))); !r) return r;
                    continue;
                }
                lit.interp_snippets.push_back(std::move(*parsed));
                if (auto r = check_expr(lit.interp_snippets.back().expr); !r) return r;
            }
        }
        return {};
    }

    [[nodiscard]] auto check_unary_expr(const UnaryExpr& un, int line, int col) -> std::expected<void, std::string> {
        if (auto r = check_expr(un.operand); !r) return r;
        if (un.op == "move") {
            if (auto r = validate_move_operand(un.operand, false, line, col); !r) return r;
            if (auto pi = extract_path_info(un.operand)) {
                if (auto r = check_not_iterated_collection(pi->root, line, col); !r) return r;
                moved_at_line_[pi->root] = line;
                invalidate_loans_for_owner(
                    pi->root,
                    std::format("underlying owner '{}' was moved at line {}", pi->root, line),
                    line);
            }
        } else if (un.op == "*" && un.operand) {
            // CF-182: `*` on a value (e.g. `*line[1..]`) used to reach clang as "indirection requires pointer operand".
            // Only slices and literals: inference reports a pointer field or call result as its pointee type.
            const bool certain = std::holds_alternative<SliceExpr>(un.operand->node) || std::holds_alternative<LiteralExpr>(un.operand->node);
            if (TypeId ot = infer_expr_type_id_sema(un.operand); certain && is_concrete_checkable_type(ot)) {
                const auto& od = cf_.type_table.get(ot);
                const bool str_view = od.kind == TypeKind::Borrow && od.elem_type == TypeId::Str();
                if (str_view || ot == TypeId::Str()) {
                    if (auto r = emit_error(diag_at(line, col, "cfire/TYPE-MISMATCH", "cannot dereference a 'str' value with '*'; remove the '*' (a 'str' slice is copied automatically where owned text is needed)")); !r) return r;
                } else if (cf_.type_table.is_numeric(ot) || ot == TypeId::Bool() || ot == TypeId::Byte() ||
                           od.kind == TypeKind::Slice || od.kind == TypeKind::Array || od.kind == TypeKind::Map) {
                    if (auto r = emit_error(diag_at(line, col, "cfire/TYPE-MISMATCH", std::format("cannot dereference a value of type '{}' with '*'; only pointers, borrows and own/shared handles can be dereferenced", cf_.type_table.to_string(ot)))); !r) return r;
                }
            }
            if (const auto* id = std::get_if<IdentExpr>(&un.operand->node)) {
                if (auto it = ptr_null_state_.find(id->name); it != ptr_null_state_.end()) {
                    if (it->second == NullState::Null) {
                        if (auto r = emit_error(diag_at(line, col, "cfire/NULL-DEREF", std::format("dereference of null pointer '{}'", id->name), {{"ptr", id->name}})); !r) return r;
                    } else if (it->second == NullState::MaybeNull) {
                        if (auto r = emit_error(diag_at(line, col, "cfire/NULL-DEREF", std::format("dereference of potentially null pointer '{}' without '!= none' guard", id->name), {{"ptr", id->name}})); !r) return r;
                    }
                }
            }
        }
        return {};
    }

    [[nodiscard]] auto check_binary_expr(const BinaryExpr& bin, int line, int col) -> std::expected<void, std::string> {
        if (auto r = check_expr(bin.left); !r) return r;
        if (bin.op == "&&" || bin.op == "||") {
            ++in_invalid_unwrap_context_;
            auto guard = cfire::scope_exit([this] { --in_invalid_unwrap_context_; });
            if (auto r = check_expr(bin.right); !r) return r;
            if (auto r = check_bool_condition(bin.left, std::format("left operand of '{}'", bin.op)); !r) return r;
            if (auto r = check_bool_condition(bin.right, std::format("right operand of '{}'", bin.op)); !r) return r;
        } else {
            if (auto r = check_expr(bin.right); !r) return r;
        }
        if (bin.op == "<<" || bin.op == ">>") {
            if (auto r = check_const_shift_count(bin.left, bin.right, line, col); !r) return r;
        }
        // CF-182: `s[0] == "L"` compares a byte with text; it used to reach clang as a pointer/integer comparison.
        if (bin.op == "==" || bin.op == "!=" || bin.op == "<" || bin.op == "<=" || bin.op == ">" || bin.op == ">=") {
            TypeId lt = infer_expr_type_id_sema(bin.left);
            TypeId rt = infer_expr_type_id_sema(bin.right);
            const TypeId str_view = type_id_of("&str");
            auto is_text = [&](TypeId t) { return t == TypeId::Str() || (str_view.is_valid() && t == str_view); };
            if ((lt == TypeId::Byte() && is_text(rt)) || (rt == TypeId::Byte() && is_text(lt))) {
                const ExprPtr& text_side = lt == TypeId::Byte() ? bin.right : bin.left;
                std::string hint = "compare with a byte literal such as 'a', or slice the string (s[i..i + 1])";
                if (const auto* lit = text_side ? std::get_if<LiteralExpr>(&text_side->node) : nullptr;
                    lit && lit->kind == TokenKind::String && lit->value.size() == 1 && static_cast<unsigned char>(lit->value[0]) < 0x80 && lit->value[0] != '\\') {
                    hint = std::format("use the byte literal '{}'", lit->value == "'" ? std::string("\\'") : lit->value);
                }
                return emit_error(diag_at(line, col, "cfire/TYPE-MISMATCH", std::format("cannot compare 'byte' with '{}' using '{}'; {}", cf_.type_table.to_string(lt == TypeId::Byte() ? rt : lt), bin.op, hint)));
            }
        }
        // CF-148: arithmetic / ordering on `T?` needs an explicit unwrap (`x or default`); `==`/`!=` stay legal
        // because `x != none` is the idiom.
        if (bin.op != "==" && bin.op != "!=" && bin.op != "&&" && bin.op != "||") {
            for (const auto* side : {&bin.left, &bin.right}) {
                if (*side && expr_returns_optional_sema(*side)) {
                    return emit_error(diag_at((*side)->line, (*side)->col, "cfire/TYPE-MISMATCH", std::format("operator '{}' cannot be applied to an optional value; unwrap it first with 'or' (e.g. '(x or 0) {} y')", bin.op, bin.op)));
                }
            }
        }
        if ((bin.op == "/" || bin.op == "%") && is_const_zero_expr(bin.right) &&
            infer_expr_type_id_sema(bin.left) != TypeId::Float32() && infer_expr_type_id_sema(bin.left) != TypeId::Float64()) {
            return emit_error(diag_at(line, col, "cfire/DIVIDE-BY-ZERO", std::format("division or modulo by constant zero")));
        }
        // CF-116: Type compatibility check for binary expressions
        {
            TypeId lt = infer_expr_type_id_sema(bin.left);
            TypeId rt = infer_expr_type_id_sema(bin.right);
            // Borrowed string views (e.g. slices `s[a..b]` typed `&str`) behave as `str` for operators.
            auto decay_str_borrow = [this](TypeId t) -> TypeId {
                if (!t.is_valid() || t.is_unknown()) return t;
                const auto& d = cf_.type_table.get(t);
                if ((d.kind == TypeKind::Borrow || d.kind == TypeKind::MutBorrow) && d.elem_type == TypeId::Str()) {
                    return TypeId::Str();
                }
                return t;
            };
            lt = decay_str_borrow(lt);
            rt = decay_str_borrow(rt);
            auto is_generic_placeholder = [this](TypeId t) -> bool {
                if (!t.is_valid() || t.is_unknown()) return false;
                const auto& d = cf_.type_table.get(t);
                return d.name == "T" || d.name == "U" || d.name == "V" || (d.name.size() == 1 && std::isupper(static_cast<unsigned char>(d.name[0])));
            };
            if (lt.is_valid() && rt.is_valid() && !lt.is_unknown() && !rt.is_unknown() &&
                !is_generic_placeholder(lt) && !is_generic_placeholder(rt)) {
                if (bin.op == "+") {
                    if (lt == TypeId::Str() || rt == TypeId::Str()) {
                        if (lt != rt) {
                            return emit_error(diag_at(line, col, "cfire/TYPE-MISMATCH", std::format("cannot concatenate '{}' and '{}' with '+' without explicit conversion", cf_.type_table.to_string(lt), cf_.type_table.to_string(rt))));
                        }
                    } else if (lt == TypeId::Bool() || rt == TypeId::Bool() || !cf_.type_table.is_numeric(lt) || !cf_.type_table.is_numeric(rt)) {
                        return emit_error(diag_at(line, col, "cfire/TYPE-MISMATCH", std::format("binary operator '+' cannot be applied to expressions of type '{}' and '{}'", cf_.type_table.to_string(lt), cf_.type_table.to_string(rt))));
                    }
                } else if (bin.op == "-" || bin.op == "*" || bin.op == "/" || bin.op == "%") {
                    if (lt == TypeId::Str() || rt == TypeId::Str() || lt == TypeId::Bool() || rt == TypeId::Bool()) {
                        return emit_error(diag_at(line, col, "cfire/TYPE-MISMATCH", std::format("binary operator '{}' cannot be applied to expressions of type '{}' and '{}'", bin.op, cf_.type_table.to_string(lt), cf_.type_table.to_string(rt))));
                    }
                } else if (bin.op == "&" || bin.op == "|" || bin.op == "^" || bin.op == "<<" || bin.op == ">>") {
                    if (!cf_.type_table.is_integer(lt) || !cf_.type_table.is_integer(rt)) {
                        return emit_error(diag_at(line, col, "cfire/TYPE-MISMATCH", std::format("bitwise operator '{}' requires integer operands, got '{}' and '{}'", bin.op, cf_.type_table.to_string(lt), cf_.type_table.to_string(rt))));
                    }
                } else if (bin.op == "<" || bin.op == "<=" || bin.op == ">" || bin.op == ">=") {
                    bool l_num = cf_.type_table.is_numeric(lt);
                    bool r_num = cf_.type_table.is_numeric(rt);
                    if ((l_num && !r_num) || (!l_num && r_num) || (lt == TypeId::Str() && rt != TypeId::Str()) || (lt != TypeId::Str() && rt == TypeId::Str())) {
                        return emit_error(diag_at(line, col, "cfire/TYPE-MISMATCH", std::format("comparison operator '{}' cannot compare expressions of type '{}' and '{}'", bin.op, cf_.type_table.to_string(lt), cf_.type_table.to_string(rt))));
                    }
                }
            }
        }
        return {};
    }

    // "own" / "shared" when `obj` is a smart pointer, "" otherwise.
    [[nodiscard]] auto smart_wrap_of(const ExprPtr& obj) const -> std::string {
        if (!obj) return {};
        TypeId t = infer_expr_type_id_sema(obj);
        if (t.is_valid()) {
            const auto k = cf_.type_table.get(t).kind;
            if (k == TypeKind::Own) return "own";
            if (k == TypeKind::Shared) return "shared";
        }
        if (const auto* id = std::get_if<IdentExpr>(&obj->node); id && smart_ptr_vars_.contains(id->name)) {
            if (shared_ptr_var_names_.contains(id->name)) return "shared";
            if (auto it = var_types_.find(id->name); it != var_types_.end()) {
                if (it->second.starts_with("own[")) return "own";
                if (it->second.starts_with("shared[")) return "shared";
            }
            return "own";
        }
        return {};
    }

    // CF-181: `own[T].from_raw(p)` — the object is the type expression `own[T]` (parsed as an index).
    [[nodiscard]] static auto own_type_prefix(const ExprPtr& obj) -> std::string {
        if (!obj) return {};
        const auto* ix = std::get_if<IndexExpr>(&obj->node);
        if (!ix || !ix->object) return {};
        const auto* base = std::get_if<IdentExpr>(&ix->object->node);
        if (!base || (base->name != "own" && base->name != "shared")) return {};
        const auto* targ = ix->index ? std::get_if<IdentExpr>(&ix->index->node) : nullptr;
        if (!targ) return {};
        return base->name + "[" + targ->name + "]";
    }

    // CF-148: `p.zz` on a struct declared in this program must name a field or a method.
    [[nodiscard]] auto check_known_member(const MemberExpr& mem, int line, int col) -> std::expected<void, std::string> {
        if (!mem.object || mem.is_namespace) return {};
        // CF-181: ownership hand-off for C interop.
        if (mem.field == "release") {
            std::string wrap = smart_wrap_of(mem.object);
            if (wrap == "own") return {};
            if (wrap == "shared") {
                return emit_error(diag_at(line, col, "cfire/INVALID-RELEASE", "'release' is only available on 'own[T]'; a 'shared[T]' may have other owners"));
            }
        }
        std::string owner = type_name_of(mem.object);
        auto sit = cf_.struct_tags.find(owner);
        if (sit == cf_.struct_tags.end()) return {};
        for (const auto& f : sit->second) {
            if (f.field_name == mem.field) return {};
        }
        if (cf_.methods.contains(owner + "::" + mem.field) || cf_.methods.contains(owner + "." + mem.field)) return {};
        if (auto oit = cf_.method_owners.find(mem.field); oit != cf_.method_owners.end() && oit->second.contains(owner)) return {};
        std::string known;
        for (const auto& f : sit->second) known += (known.empty() ? "" : ", ") + f.field_name;
        return emit_error(diag_at(line, col > 0 ? col + (mem.is_namespace ? 2 : 1) : col, "cfire/UNKNOWN-FIELD", std::format("struct '{}' has no field or method '{}' (fields: {})", owner, mem.field, known.empty() ? "none" : known)));
    }

    // CF-148: struct literal fields must exist and have compatible types.
    // `http.ServerLimits` / `h::ServerLimits` -> the stdlib struct row, resolving import aliases.
    [[nodiscard]] auto lookup_stdlib_struct(std::string_view spelled_in) const -> const StdlibStruct* {
        std::string spelled(spelled_in);
        for (auto p = spelled.find("::"); p != std::string::npos; p = spelled.find("::")) spelled.replace(p, 2, ".");
        auto dot = spelled.find('.');
        if (dot == std::string::npos) return nullptr;
        std::string ns = spelled.substr(0, dot);
        if (is_ident_in_scope_sema(ns)) return nullptr;
        if (std::string canon = resolve_stdlib_ns_sema(ns); !canon.empty()) ns = canon;
        return find_stdlib_struct(ns, std::string_view(spelled).substr(dot + 1));
    }

    [[nodiscard]] auto check_struct_literal_fields(const StructLitExpr& st, int line, int col) -> std::expected<void, std::string> {
        if (const StdlibStruct* sst = lookup_stdlib_struct(st.type_name)) {
            const auto fields = stdlib_struct_fields(*sst);
            for (const auto& f : st.fields) {
                auto fit = std::find_if(fields.begin(), fields.end(), [&](const StdlibStructField& sf) { return sf.name == f.name; });
                if (fit == fields.end()) {
                    std::string names;
                    for (const auto& sf : fields) names += (names.empty() ? "" : ", ") + std::string(sf.name);
                    if (auto r = emit_error(diag_at(f.value ? f.value->line : line, f.value ? f.value->col : col, "cfire/UNKNOWN-FIELD", std::format("struct '{}.{}' has no field '{}'; fields: {}", sst->ns, sst->name, f.name, names), {{"field", f.name}})); !r) return r;
                    continue;
                }
                if (!f.value) continue;
                TypeId want = type_id_of(fit->type);
                TypeId got = infer_expr_type_id_sema(f.value);
                if (!is_concrete_checkable_type(want) || !is_concrete_checkable_type(got) || expr_returns_optional_sema(f.value)) continue;
                if (cf_.type_table.is_numeric(want) && cf_.type_table.is_numeric(got) && is_numeric_literal_expr(f.value)) continue;
                if (!cf_.type_table.is_assignable(want, got)) {
                    if (auto r = emit_error(diag_at(f.value->line, f.value->col, "cfire/TYPE-MISMATCH", std::format("field '{}.{}.{}' expects '{}', got '{}'", sst->ns, sst->name, f.name, cf_.type_table.to_string(want), cf_.type_table.to_string(got)))); !r) return r;
                }
            }
            return {};
        }
        auto sit = cf_.struct_tags.find(st.type_name);
        if (sit == cf_.struct_tags.end() || !st.type_args.empty()) return {};
        for (const auto& f : st.fields) {
            auto fit = std::find_if(sit->second.begin(), sit->second.end(), [&](const StructTagInfo& t) { return t.field_name == f.name; });
            if (fit == sit->second.end()) {
                if (auto r = emit_error(diag_at(f.value ? f.value->line : line, f.value ? f.value->col : col, "cfire/UNKNOWN-FIELD", std::format("struct '{}' has no field '{}'", st.type_name, f.name))); !r) return r;
                continue;
            }
            if (!f.value || std::holds_alternative<ArrayLitExpr>(f.value->node)) continue;
            TypeId want = cf_.type_table.from_typeref(fit->field_type);
            TypeId got = infer_expr_type_id_sema(f.value);
            if (!is_concrete_checkable_type(want) || !is_concrete_checkable_type(got) || expr_returns_optional_sema(f.value)) continue;
            if (!cf_.type_table.is_assignable(want, got)) {
                if (auto r = emit_error(diag_at(f.value->line, f.value->col, "cfire/TYPE-MISMATCH", std::format("field '{}.{}' expects '{}', got '{}'{}", st.type_name, f.name, cf_.type_table.to_string(want), cf_.type_table.to_string(got), nominal_mismatch_hint_sema(want, got)))); !r) return r;
            }
        }
        return {};
    }

    // Q10: `i32`, `f64`, `bool`, ... used as a value prefix (`i32.MAX`, `f64.parse(s)`).
    [[nodiscard]] auto primitive_type_prefix(const ExprPtr& obj) const -> TypeId {
        if (!obj) return TypeId::Unknown();
        const auto* id = std::get_if<IdentExpr>(&obj->node);
        if (!id || is_ident_in_scope_sema(id->name)) return TypeId::Unknown();
        static constexpr std::array<std::string_view, 13> kPrims = {
            "i8", "i16", "i32", "i64", "u8", "u16", "u32", "u64", "isize", "usize", "f32", "f64", "bool"};
        if (std::find(kPrims.begin(), kPrims.end(), id->name) == kPrims.end()) return TypeId::Unknown();
        return cf_.type_table.from_name(id->name);
    }

    [[nodiscard]] auto primitive_bits(TypeId t) const -> std::uint8_t {
        if (t == TypeId::Bool()) return BmBool;
        if (cf_.type_table.is_float(t)) return BmFloat;
        if (cf_.type_table.is_integer(t)) return BmInt;
        return 0;
    }

    [[nodiscard]] auto check_member_expr(const MemberExpr& mem, int line, int col) -> std::expected<void, std::string> {
        if (std::string own_t = own_type_prefix(mem.object); !own_t.empty()) {
            if (mem.field == "from_raw" && own_t.starts_with("own[")) return {};
            return emit_error(diag_at(line, col, "cfire/UNKNOWN-FIELD", std::format("'{}' has no associated function '{}'; the only one is 'own[T].from_raw(p)'", own_t, mem.field)));
        }
        // Q26: length is a method on every builtin container; the property form `xs.len` is retired.
        if (mem.field == "len" && !callee_members_.contains(&mem) && mem.object && (builtin_recv_bits(mem.object) & BmSized)) {
            return emit_error(diag_at(line, col, "cfire/RETIRED-API", "'.len' is a method; write '.len()'", {{"from", ".len"}, {"to", ".len()"}, {"rewrite", "len_call"}}));
        }
        if (TypeId prim = primitive_type_prefix(mem.object); prim.is_valid()) {
            if (mem.field == "parse" || find_builtin_assoc_const(primitive_bits(prim), mem.field)) return {};
            std::string avail;
            for (const auto& c : kBuiltinAssocConsts) {
                if ((c.recv & primitive_bits(prim)) == 0) continue;
                avail += (avail.empty() ? "" : ", ") + std::string(c.name);
            }
            avail += (avail.empty() ? "" : ", ") + std::string("parse");
            std::string tname = cf_.type_table.to_string(prim);
            return emit_error(diag_at(line, col + 1 + static_cast<int>(tname.size()), "cfire/UNKNOWN-FIELD", std::format("type '{}' has no associated item '{}'; available: {}", tname, mem.field, avail), {{"field", std::string(mem.field)}}));
        }
        if (mem.object) {
            if (auto r = check_known_member(mem, line, col); !r) return r;
            if (const auto* un = std::get_if<UnaryExpr>(&mem.object->node)) {
                if (un->op == "*" && un->operand) {
                    if (const auto* id = std::get_if<IdentExpr>(&un->operand->node)) {
                        if (smart_ptr_vars_.contains(id->name)) {
                            return emit_error(diag_at(line, col, "cfire/REDUNDANT-DEREF", std::format("explicit dereference '(*{}).{}' is redundant on smart pointer '{}'; use '{}.{}' directly", id->name, mem.field, id->name, id->name, mem.field)));
                        }
                    }
                }
            }
            if (const auto* obj_id = std::get_if<IdentExpr>(&mem.object->node)) {
                if (cf_.shadowed_default_qualifiers.contains(obj_id->name)) {
                    std::string active_alias = lookup_or_empty(cf_.shadowed_default_qualifiers, obj_id->name);
                    return emit_error(diag_at(line, col, "cfire/UNDECLARED-IDENT", std::format("module '{}' is imported as '{}'; use '{}.{}'", obj_id->name, active_alias, active_alias, mem.field)));
                }
                if (mem.is_namespace) {
                    if (!cf_.enums.contains(obj_id->name) &&
                        (obj_id->name == "cpp" || is_cpp_qualifier_sema(obj_id->name) ||
                         is_vendor_alias_sema(obj_id->name) || is_namespace_ident_sema(obj_id->name))) {
                        return emit_error(diag_at(line, col, "cfire/DEPRECATED-SEPARATOR", std::format("use '{}.{}' instead of '{}::{}'", obj_id->name, mem.field, obj_id->name, mem.field), {{"kind", "qualifier"}}));
                    }
                }
                if (auto r = check_stdlib_ns_imported(obj_id->name, mem.object->line > 0 ? mem.object->line : line, mem.object->col > 0 ? mem.object->col : col); !r) return r;
                if (obj_id->name == "os" && !is_ident_in_scope_sema("os") &&
                    (mem.field == "STDIN" || mem.field == "STDOUT" || mem.field == "STDERR")) {
                    std::string lower_fn = mem.field;
                    for (char& c : lower_fn) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                    return emit_error(diag_at(line, col, "cfire/DEPRECATED-API", std::format("'os.{}' has been replaced by 'os.{}()' returning an RAII 'fs.File' stream handle", mem.field, lower_fn)));
                }
                if (!is_ident_in_scope_sema(obj_id->name) && !is_namespace_ident_sema(obj_id->name) &&
                    cf_.fn_sigs.contains(mem.field)) {
                    return {};
                }
            }
        }
        if (auto r = check_expr(mem.object); !r) return r;
        if (!mem.is_namespace && mem.object) {
            if (const auto* obj_id = std::get_if<IdentExpr>(&mem.object->node)) {
                if (cf_.enums.contains(obj_id->name)) {
                    if (mem.field != "from_i32" && mem.field != "from_i64" && mem.field != "from_string") {
                        return emit_error(diag_at(line, col, "cfire/DEPRECATED-SEPARATOR", std::format("'.' is not a valid separator for enum variant '{}'; use '::' (e.g. '{}::{}')", mem.field, obj_id->name, mem.field), {{"kind", "enum-variant"}}));
                    }
                }
            } else if (const auto* sub_mem = std::get_if<MemberExpr>(&mem.object->node)) {
                if (cf_.enums.contains(sub_mem->field) ||
                    sub_mem->field == "Level" || sub_mem->field == "OpenMode" || sub_mem->field == "Waveform" ||
                    sub_mem->field == "SfxPreset" || sub_mem->field == "SeekWhence" || sub_mem->field == "Provider" ||
                    sub_mem->field == "Format") {
                    std::string pkg = std::holds_alternative<IdentExpr>(sub_mem->object->node) ? std::get<IdentExpr>(sub_mem->object->node).name : "";
                    return emit_error(diag_at(line, col, "cfire/DEPRECATED-SEPARATOR", std::format("'.' is not a valid separator for enum variant '{}'; use '::' (e.g. '{}.{}::{}')", mem.field, pkg, sub_mem->field, mem.field), {{"kind", "enum-variant"}}));
                }
            }
            if (const auto* id = std::get_if<IdentExpr>(&mem.object->node)) {
                if (optional_vars_.contains(id->name) && !raw_or_smart_ptr_vars_.contains(id->name) &&
                    (mem.field == "len" || mem.field == "empty" || mem.field == "data")) {
                    return emit_error(diag_at(line, col, "cfire/UNWRAPPED-OPTIONAL", std::format("cannot access '.{}' on optional value '{}' without unwrapping; use 'or' to provide a fallback", mem.field, id->name)));
                }
                if (fallible_vars_.contains(id->name) && !reported_unhandled_vars_.contains(id->name) &&
                    mem.field != "has_value" && mem.field != "value" && mem.field != "value_or" && mem.field != "error") {
                    return emit_error(diag_at(line, col, "cfire/UNHANDLED-RESULT", std::format("cannot access '.{}' on fallible result '{}' without unwrapping with 'try' or 'or'", mem.field, id->name)));
                }
                if (scalar_primitive_vars_.contains(id->name) && mem.field == "len") {
                    std::string tname = lookup_or(var_types_, id->name, "primitive");
                    return emit_error(diag_at(line, col, "cfire/INVALID-MEMBER", std::format("primitive type '{}' has no member '.{}'", tname, mem.field)));
                }
                if (ptr_or_smart_vars_.contains(id->name) &&
                    mem.field != "reset" && mem.field != "get" &&
                    mem.field != "has_value" && mem.field != "value_or") {
                    if (auto it = ptr_null_state_.find(id->name); it != ptr_null_state_.end()) {
                        if (it->second == NullState::Null) {
                            return emit_error(diag_at(line, col, "cfire/NULL-DEREF", std::format("member access on null pointer '{}'", id->name), {{"ptr", id->name}}));
                        }
                        if (it->second == NullState::MaybeNull) {
                            return emit_error(diag_at(line, col, "cfire/NULL-DEREF", std::format("member access on potentially null pointer '{}' without '!= none' guard", id->name), {{"ptr", id->name}}));
                        }
                    }
                }
            } else if (expr_returns_optional_sema(mem.object) &&
                       mem.field != "has_value" && mem.field != "value" && mem.field != "value_or") {
                return emit_error(diag_at(line, col, "cfire/UNWRAPPED-OPTIONAL", std::format("cannot access '.{}' on optional value without unwrapping; use 'or' to provide a fallback", mem.field)));
            }
        }
        return {};
    }

    [[nodiscard]] auto check_index_expr(const IndexExpr& idx, int line, int col) -> std::expected<void, std::string> {
        if (auto r = check_expr(idx.object); !r) return r;
        if (auto r = check_expr(idx.index); !r) return r;

        bool is_map_obj = false;
        const IdentExpr* obj_id = idx.object ? std::get_if<IdentExpr>(&idx.object->node) : nullptr;
        if (obj_id) {
            if (optional_vars_.contains(obj_id->name)) {
                return emit_error(diag_at(line, col, "cfire/UNWRAPPED-OPTIONAL", std::format("cannot index into optional value '{}' without unwrapping; use 'or' to provide a fallback", obj_id->name)));
            }
            if (fallible_vars_.contains(obj_id->name) && !reported_unhandled_vars_.contains(obj_id->name)) {
                return emit_error(diag_at(line, col, "cfire/UNHANDLED-RESULT", std::format("cannot index into fallible result '{}' without unwrapping with 'try' or 'or'", obj_id->name)));
            }
            if (scalar_primitive_vars_.contains(obj_id->name)) {
                std::string tname = lookup_or(var_types_, obj_id->name, "primitive");
                return emit_error(diag_at(line, col, "cfire/INVALID-INDEX", std::format("cannot index into non-indexable primitive type '{}'", tname)));
            }
            if (map_vars_.contains(obj_id->name)) {
                is_map_obj = true;
            }
        } else if (idx.object && expr_returns_optional_sema(idx.object)) {
            return emit_error(diag_at(line, col, "cfire/UNWRAPPED-OPTIONAL", std::format("cannot index into optional value without unwrapping; use 'or' to provide a fallback")));
        }
        if (!is_map_obj && idx.index) {
            if (const auto* un_idx = std::get_if<UnaryExpr>(&idx.index->node)) {
                if (un_idx->op == "-" && un_idx->operand) {
                    if (const auto* neg_lit = std::get_if<LiteralExpr>(&un_idx->operand->node);
                        neg_lit && neg_lit->kind == TokenKind::Int && neg_lit->value != "0") {
                        if (auto r = emit_error(diag_at(line, col, "cfire/INDEX-OUT-OF-BOUNDS", std::format("negative index -{} is out of bounds", neg_lit->value))); !r) return r;
                    }
                }
            } else if (const auto* idx_lit = std::get_if<LiteralExpr>(&idx.index->node)) {
                if (idx_lit->kind == TokenKind::Int) {
                    if (idx_lit->value.starts_with('-') && idx_lit->value != "-0") {
                        if (auto r = emit_error(diag_at(line, col, "cfire/INDEX-OUT-OF-BOUNDS", std::format("negative index {} is out of bounds", idx_lit->value))); !r) return r;
                    } else if (auto k = cfire::detail::parse_int<std::size_t>(idx_lit->value)) {
                        if (obj_id) {
                            if (auto fit = fixed_array_lens_.find(obj_id->name); fit != fixed_array_lens_.end()) {
                                if (*k >= fit->second) {
                                    if (auto r = emit_error(diag_at(line, col, "cfire/INDEX-OUT-OF-BOUNDS", std::format("index {} is out of bounds for array '{}' of length {}", *k, obj_id->name, fit->second))); !r) return r;
                                }
                            }
                        } else if (idx.object) {
                            if (const auto* arr_lit = std::get_if<ArrayLitExpr>(&idx.object->node)) {
                                if (*k >= arr_lit->elements.size()) {
                                    if (auto r = emit_error(diag_at(line, col, "cfire/INDEX-OUT-OF-BOUNDS", std::format("index {} is out of bounds for array literal of length {}", *k, arr_lit->elements.size()))); !r) return r;
                                }
                            }
                        }
                    }
                }
            }
        }
        return {};
    }

    [[nodiscard]] auto check_or_expr(const OrExpr& or_ex, int line, int col, const ExprPtr& self_expr = nullptr) -> std::expected<void, std::string> {
        std::size_t errors_before = errors_.size();
        if (self_expr && unwrapped_exprs_.contains(self_expr)) {
            if (or_ex.inner) unwrapped_exprs_.insert(or_ex.inner);
            if (or_ex.fallback_val) unwrapped_exprs_.insert(or_ex.fallback_val);
        }
        if (auto r = check_expr(or_ex.inner); !r) return r;
        bool is_opt = is_expr_optional_for_unwrap_sema(or_ex.inner);
        bool is_fall = is_expr_fallible_for_unwrap_sema(or_ex.inner);
        if (!or_ex.err_var.empty() && is_opt && !is_fall) {
            if (auto r = emit_error(diag_at(line, col, "cfire/OPTIONAL-OR-ERR", std::format("cannot bind error variable '{}' on optional 'T?' in 'or' block; use 'or {{ ... }}' or 'or fallback' instead", or_ex.err_var))); !r) return r;
        } else if (errors_.size() == errors_before && !is_fall && !is_opt) {
            if (auto r = emit_error(diag_at(line, col, "cfire/REDUNDANT-OR", std::format("'or' used on non-fallible, non-optional expression; remove 'or' fallback"))); !r) return r;
        }
        if (or_ex.fallback_val) {
            if (auto r = check_expr(or_ex.fallback_val); !r) return r;
        }
        if (!or_ex.handler_body.empty()) {
            if (in_invalid_unwrap_context_ > 0) {
                if (auto r = emit_error(diag_at(line, col, "cfire/INVALID-OR-BLOCK-CONTEXT", std::format("cannot use block 'or {{ ... }}' inside short-circuit operand or 'for' loop condition; hoist into a statement or use 'or fallback'"))); !r) return r;
            }
            if ((!self_expr || !discarded_or_exprs_.contains(self_expr)) && !block_diverges(or_ex.handler_body)) {
                if (auto r = emit_error(diag_at(line, col, "cfire/NON-DIVERGING-OR-BLOCK", std::format("block 'or {} {{ ... }}' must definitely diverge with return or panic, otherwise unwrapping empty value causes undefined behavior", or_ex.err_var.empty() ? "err" : or_ex.err_var))); !r) return r;
            }
            ScopeGuard sg(*this);
            if (!or_ex.err_var.empty()) {
                bind_scoped_var(or_ex.err_var, false);
            }
            if (auto r = check_block_stmts(or_ex.handler_body); !r) return r;
        }
        return {};
    }

    // CF-148: parameter types for untyped closure arguments, taken from the callee's `fn(A, B) -> R` parameter
    // (`router.get("/x", |req| ...)` binds `req: http.Request`).
    [[nodiscard]] static auto split_fn_type_params(std::string_view fn_type) -> std::vector<std::string> {
        std::vector<std::string> out;
        auto open = fn_type.find('(');
        if (!fn_type.starts_with("fn") || open == std::string_view::npos) return out;
        int depth = 0;
        std::string cur;
        for (std::size_t i = open + 1; i < fn_type.size(); ++i) {
            char c = fn_type[i];
            if ((c == '(' || c == '[') ) ++depth;
            if ((c == ')' || c == ']') && depth-- == 0) { if (!cur.empty()) out.push_back(cur); break; }
            if (c == ',' && depth == 0) { out.push_back(cur); cur.clear(); continue; }
            if (!(cur.empty() && c == ' ')) cur.push_back(c);
        }
        for (auto& t : out) {
            while (!t.empty() && t.back() == ' ') t.pop_back();
            for (std::string_view pre : {"mut ", "move "}) if (t.starts_with(pre)) t = t.substr(pre.size());
        }
        return out;
    }

    void record_closure_param_hints(const CallExpr& call) {
        std::vector<std::string> param_types;  // declared type text of each callee parameter
        std::string qualifier;
        if (const auto* mem = std::get_if<MemberExpr>(&call.callee->node); mem && mem->object) {
            const auto& idx = StdlibCatalogIndex::instance();
            const StdFnSigInfo* sig = nullptr;
            if (const auto* obj_id = std::get_if<IdentExpr>(&mem->object->node);
                obj_id && idx.has_namespace(obj_id->name) && !is_ident_in_scope_sema(obj_id->name)) {
                sig = idx.find_ns_fn(obj_id->name, mem->field);
                qualifier = obj_id->name;
            } else {
                std::string recv = type_name_of(mem->object);
                std::string base = recv.substr(0, recv.find('['));
                std::string unqual = base.substr(base.rfind('.') == std::string::npos ? 0 : base.rfind('.') + 1);
                for (const auto& key : {recv + "." + mem->field, base + "." + mem->field, unqual + "." + mem->field}) {
                    if (auto it = idx.receiver_methods.find(key); it != idx.receiver_methods.end()) { sig = &it->second; break; }
                }
                if (sig) qualifier = local_qualifier_for_module_sema(sig->module_import, sig->ns_alias);
            }
            if (sig) for (const auto& p : sig->params) param_types.push_back(p.type_str);
            if (const FnDecl* user = (sig ? nullptr : resolve_member_call_sig(*mem))) {
                for (const auto& p : user->params) param_types.push_back(format_type_ref_sema(p.type));
            }
        } else if (const auto* cid = std::get_if<IdentExpr>(&call.callee->node)) {
            if (auto it = cf_.fn_sigs.find(cid->name); it != cf_.fn_sigs.end()) {
                for (const auto& p : it->second.params) param_types.push_back(format_type_ref_sema(p.type));
            }
        }
        auto qualify = [&](std::string t) {
            // `app.bind[PresetConfig](...) |p|`: the handler's generic parameter (`fn(T)`, `fn(Args)`) is the call's
            // type argument, so builtin methods on `p.name` resolve (W4).
            if (!call.type_args.empty() && !t.empty() && std::isupper(static_cast<unsigned char>(t[0])) &&
                t.find_first_of(".[") == std::string::npos && (t.size() <= 4 || t == "Args") && !cf_.struct_tags.contains(t)) {
                return format_type_ref_sema(call.type_args[0]);
            }
            if (qualifier.empty() || t.empty() || t.find('.') != std::string::npos) return t;
            static const std::unordered_set<std::string> builtin = {"str", "bool", "i8", "i16", "i32", "i64", "u8", "u16",
                "u32", "u64", "usize", "isize", "f32", "f64", "byte", "void"};
            if (builtin.contains(t) || t.starts_with("[") || t.starts_with("map[")) return t;
            return qualifier + "." + t;
        };
        for (std::size_t i = 0; i < call.args.size() && i < param_types.size(); ++i) {
            const auto* cl = call.args[i].value ? std::get_if<ClosureExpr>(&call.args[i].value->node) : nullptr;
            if (!cl) continue;
            auto hinted = split_fn_type_params(param_types[i]);
            for (auto& h : hinted) h = qualify(h);
            if (!hinted.empty()) closure_param_hints_[cl] = std::move(hinted);
        }
        if (call.block_arg && !param_types.empty()) {
            auto hinted = split_fn_type_params(param_types.back());
            for (auto& h : hinted) h = qualify(h);
            if (!hinted.empty()) closure_param_hints_[&*call.block_arg] = std::move(hinted);
        }
    }

    // Q25: the declared constructor `fn (T) make(...)` of a local type, lowered as `cfire_ctor_T`.
    [[nodiscard]] auto find_user_ctor(const TypeRef& t) const -> const FnDecl* {
        std::string name = t.name;
        for (auto p = name.find("::"); p != std::string::npos; p = name.find("::")) name.replace(p, 2, ".");
        std::string ns;
        if (auto dot = name.rfind('.'); dot != std::string::npos) {
            ns = name.substr(0, dot);
            name = name.substr(dot + 1);
        }
        if (auto br = name.find('['); br != std::string::npos) name = name.substr(0, br);
        const std::string fn_name = "cfire_ctor_" + name;
        if (!ns.empty()) {
            if (auto it = cf_.fn_sigs.find(ns + "." + fn_name); it != cf_.fn_sigs.end()) return &it->second;
        }
        if (!pkg_name_.empty()) {
            if (auto it = cf_.fn_sigs.find(pkg_name_ + "." + fn_name); it != cf_.fn_sigs.end()) return &it->second;
        }
        if (auto it = cf_.fn_sigs.find(fn_name); it != cf_.fn_sigs.end()) return &it->second;
        return nullptr;
    }

    [[nodiscard]] auto find_stdlib_ctor(std::string_view spelled_in) const -> const StdlibCtor* {
        std::string spelled_s(spelled_in);
        for (auto p = spelled_s.find("::"); p != std::string::npos; p = spelled_s.find("::")) spelled_s.replace(p, 2, ".");
        std::string_view spelled = spelled_s;
        auto dot = spelled.find('.');
        if (dot == std::string_view::npos) return nullptr;
        std::string ns(spelled.substr(0, dot));
        if (std::string canon = resolve_stdlib_ns_sema(ns); !canon.empty()) ns = canon;
        return find_stdlib_ctor_for(ns, spelled.substr(dot + 1));
    }

    // Q25: `make(T[, args])`, `make(own[T][, args])`, `make(shared[T][, args])`, `make([]T, len[, cap])`, `make(map[K]V)`.
    [[nodiscard]] auto check_make_call(const CallExpr& call, int line, int col) -> std::expected<void, std::string> {
        if (call.type_args.empty()) {
            return emit_error(diag_at(line, col, "cfire/INVALID-MAKE", "'make' needs a type as its first argument: 'make(T, args...)'"));
        }
        if (!call.make_type_first) {
            return emit_error(diag_at(line, col, "cfire/RETIRED-API", std::format("'make[{0}](...)' has been replaced by 'make({0}, ...)'", format_type_ref_sema(call.type_args[0])), {{"rewrite", "make_bracket"}}));
        }
        const TypeRef& t = call.type_args[0];
        const std::size_t n = call.args.size();
        std::string tname = format_type_ref_sema(t);
        for (auto p = tname.find("::"); p != std::string::npos; p = tname.find("::")) tname.replace(p, 2, ".");
        auto check_ctor_args = [&](const FnDecl& ctor, std::string_view shown) -> std::expected<void, std::string> {
            if (n != ctor.params.size()) {
                return emit_error(diag_at(line, col, "cfire/ARITY-MISMATCH", std::format("constructor of '{}' expects {} argument(s), got {}", shown, ctor.params.size(), n)));
            }
            return check_call_argument_types(call, ctor);
        };
        auto integer_args = [&](std::size_t from) -> std::expected<void, std::string> {
            for (std::size_t i = from; i < n; ++i) {
                TypeId at = infer_expr_type_id_sema(call.args[i].value);
                if (is_concrete_checkable_type(at) && !cf_.type_table.is_integer(at)) {
                    return emit_error(diag_at(call.args[i].line, call.args[i].col, "cfire/TYPE-MISMATCH", std::format("'make({}, ...)' sizes must be integers, got '{}'", tname, cf_.type_table.to_string(at))));
                }
            }
            return {};
        };
        if (!t.smart_wrap.empty() && t.elem_type) {
            if (n == 1) {
                TypeId want = type_id_of(*t.elem_type);
                TypeId got = infer_expr_type_id_sema(call.args[0].value);
                if (want.is_valid() && got.is_valid() && cf_.type_table.is_assignable(want, got)) {
                    call.make_from_value = true;  // `make(own[T], v)` boxes an existing `T`
                    return {};
                }
            }
            if (const FnDecl* ctor = find_user_ctor(*t.elem_type)) return check_ctor_args(*ctor, format_type_ref_sema(*t.elem_type));
            if (n > 1) {
                return emit_error(diag_at(line, col, "cfire/INVALID-MAKE", std::format("'make({}, ...)' takes one '{}' value, or the arguments of a constructor 'fn ({}) make(...)', which '{}' does not declare", tname, format_type_ref_sema(*t.elem_type), t.elem_type->name, t.elem_type->name)));
            }
            return {};
        }
        if (t.is_slice) {
            if (n < 1 || n > 2) {
                return emit_error(diag_at(line, col, "cfire/INVALID-MAKE", std::format("'make({}, len)' or 'make({}, len, cap)' expects 1 or 2 sizes, got {}", tname, tname, n)));
            }
            return integer_args(0);
        }
        if (t.is_map || t.is_array) {
            if (n != 0) {
                return emit_error(diag_at(line, col, "cfire/INVALID-MAKE", std::format("'make({})' takes no further arguments, got {}", tname, n)));
            }
            return {};
        }
        if (const FnDecl* ctor = find_user_ctor(t)) return check_ctor_args(*ctor, tname);
        // Foreign C++ / vendor types (`make(cpp.std.pair[i32, str], 1, "x")`) take their C++ constructor's arguments.
        if (auto sep = tname.find('.'); sep != std::string::npos) {
            std::string q = tname.substr(0, sep);
            if (q == "cpp" || is_cpp_qualifier_sema(q) || is_vendor_alias_sema(q)) return {};
        }
        if (const auto* sc = find_stdlib_ctor(t.name)) {
            if (sc->type == "Mutex" && !t.type_args.empty() && t.type_args[0].name == "void") {
                if (n != 0) return emit_error(diag_at(line, col, "cfire/INVALID-MAKE", std::format("'make({})' is a pure lock and takes no initial value", tname)));
                return {};
            }
            const auto& idx = StdlibCatalogIndex::instance();
            if (const auto* sig = idx.find_ns_fn(sc->ns, std::string(sc->type) + ".make")) {
                if (n < sig->min_args || n > sig->max_args) {
                    std::string expected = sig->min_args == sig->max_args ? std::to_string(sig->min_args) : std::format("{} to {}", sig->min_args, sig->max_args);
                    return emit_error(diag_at(line, col, "cfire/ARITY-MISMATCH", std::format("'make({}, ...)' expects {} argument(s), got {}", tname, expected, n)));
                }
            }
            return {};
        }
        if (n != 0) {
            return emit_error(diag_at(line, col, "cfire/INVALID-MAKE", std::format("type '{}' declares no constructor, so 'make({})' takes no arguments; declare 'fn ({}) make(...) -> {}' or use a struct literal", tname, tname, t.name, t.name)));
        }
        return {};
    }

    [[nodiscard]] auto check_call_expr(const CallExpr& call, int line, int col, const ExprPtr& self_expr = nullptr) -> std::expected<void, std::string> {
        record_closure_param_hints(call);
        if (call.callee) {
            if (const auto* cm = std::get_if<MemberExpr>(&call.callee->node)) callee_members_.insert(cm);
        }
        // CF-182: Go-style free `len(x)` / `cap(x)` are reserved names but not functions; they used to reach clang.
        if (const auto* cid = call.callee ? std::get_if<IdentExpr>(&call.callee->node) : nullptr;
            cid && (cid->name == "len" || cid->name == "cap") && !is_ident_in_scope_sema(cid->name) && !cf_.fn_sigs.contains(cid->name)) {
            std::string recv = "x";
            if (call.args.size() == 1 && call.args[0].value) {
                if (const auto* aid = std::get_if<IdentExpr>(&call.args[0].value->node)) recv = aid->name;
            }
            return emit_error(diag_at(line, col, "cfire/UNDECLARED-IDENT", std::format("'{0}' is a method, not a function; use '{1}.{0}()'", cid->name, recv)));
        }
        if (auto r = check_expr(call.callee); !r) return r;
        const FnDecl* target_sig = nullptr;
        const StdFnSigInfo* std_sig = nullptr;
        const auto& catalog_idx = StdlibCatalogIndex::instance();

        for (const auto& ta : call.type_args) {
            if (auto iface = find_interface_in_slice(ta, cf_.interfaces)) {
                if (auto r = emit_error(diag_at(line, col, "cfire/DYNAMIC-INTERFACE-UNSUPPORTED", std::format("dynamic slice or array of interface '{}' is unsupported; structural interfaces lower to C++ concepts and cannot be stored dynamically", *iface))); !r) return r;
            }
        }

        if (const auto* conv = std::get_if<IdentExpr>(&call.callee->node);
            conv && call.args.size() == 1 && !is_ident_in_scope_sema(conv->name)) {
            if (auto r = check_int_literal_fits(call.args[0].value, conv->name, line, col); !r) return r;
            // `usize(x)` on an optional (`s.index_of(":")` is `usize?`) must unwrap first; clang used to report it.
            if (primitive_type_prefix(call.callee).is_valid() && conv->name != "bool" && expr_returns_optional_sema(call.args[0].value)) {
                if (auto r = emit_error(diag_at(call.args[0].line, call.args[0].col, "cfire/TYPE-MISMATCH", std::format("cannot convert an optional value to '{}'; unwrap it first ('x or default', 'x or {{ ... }}')", conv->name))); !r) return r;
            }
        }
        if (const auto* id = std::get_if<IdentExpr>(&call.callee->node); id && id->name == "make" && !is_ident_in_scope_sema("make")) {
            if (auto r = check_make_call(call, line, col); !r) return r;
        }
        // Q8: an optional has no text form; `println("{}", s.index_of("z"))` must unwrap (`x or default`) first.
        if (const auto* id = std::get_if<IdentExpr>(&call.callee->node);
            id && (id->name == "println" || id->name == "print" || id->name == "eprintln") && !is_ident_in_scope_sema(id->name)) {
            for (std::size_t i = call.args.size() > 1 ? 1 : 0; i < call.args.size(); ++i) {
                const auto& a = call.args[i];
                if (!a.value || std::holds_alternative<LiteralExpr>(a.value->node) || !expr_returns_optional_sema(a.value)) continue;
                if (auto r = emit_error(diag_at(a.line, a.col, "cfire/TYPE-MISMATCH", std::format("'{}' cannot format an optional value; unwrap it first ('x or default', 'x or {{ ... }}', or compare with 'none')", id->name))); !r) return r;
            }
        }
        // `println(a, b)`: with more than one argument the first is the format string (clang used to report this).
        if (const auto* id = std::get_if<IdentExpr>(&call.callee->node);
            id && (id->name == "println" || id->name == "print" || id->name == "eprintln") && !is_ident_in_scope_sema(id->name) && call.args.size() > 1) {
            TypeId ft = infer_expr_type_id_sema(call.args[0].value);
            if (is_concrete_checkable_type(ft) && ft != TypeId::Str() && !expr_returns_optional_sema(call.args[0].value)) {
                if (auto r = emit_error(diag_at(call.args[0].line, call.args[0].col, "cfire/TYPE-MISMATCH", std::format("the first argument of '{}' with several arguments is the format string, got '{}'; write '{}(\"{{}} {{}}\", a, b)'", id->name, cf_.type_table.to_string(ft), id->name))); !r) return r;
            }
        }
        if (const auto* id = std::get_if<IdentExpr>(&call.callee->node)) {
            if ((id->name == "error" || id->name == "Err") && !is_ident_in_scope_sema(id->name)) {
                if (call.args.empty() || call.args.size() > 2) {
                    if (auto r = emit_error(diag_at(line, col, "cfire/WRONG-ARG-COUNT", std::format("'error' requires 1 or 2 arguments (message [, code]); got {}", call.args.size()))); !r) return r;
                }
            }
            if ((id->name == "readln" || id->name == "input") && !is_ident_in_scope_sema(id->name)) {
                if (auto r = emit_error(diag_at(line, col, "cfire/DEPRECATED-API", std::format("builtin '{}' has been removed; use 'cli.prompt(...)' for interactive prompts or 'os.stdin().read_line()' for stdin streams", id->name))); !r) return r;
            }
            if ((id->name == "shared" || id->name == "own") && !is_ident_in_scope_sema(id->name)) {
                if (auto r = emit_error(diag_at(line, col, "cfire/INVALID-SMART-PTR-CTOR", std::format("cannot call '{}' directly as a constructor; use 'make({}[T], ...)' instead", id->name, id->name))); !r) return r;
            }
            if (!pkg_name_.empty()) {
                if (auto pit = cf_.fn_sigs.find(pkg_name_ + "." + id->name); pit != cf_.fn_sigs.end()) {
                    target_sig = &pit->second;
                }
            }
            if (!target_sig) {
                if (auto it = cf_.fn_sigs.find(id->name); it != cf_.fn_sigs.end()) {
                    target_sig = &it->second;
                }
            }
        } else if (const auto* mem = std::get_if<MemberExpr>(&call.callee->node)) {
            const auto* obj_id = mem->object ? std::get_if<IdentExpr>(&mem->object->node) : nullptr;
            if (obj_id && obj_id->name == "ai" && !is_ident_in_scope_sema("ai") &&
                (mem->field == "embed" || mem->field == "embed_image" || mem->field == "ocr" || mem->field == "detect_language")) {
                if (auto r = emit_error(diag_at(line, col, "cfire/DEPRECATED-API", std::format("'ai.{}' is a stateful Client method; construct a client first (e.g. 'client := ai.local()') and call 'client.{}(...)'", mem->field, mem->field))); !r) return r;
            }
            if (obj_id && obj_id->name == "fs" && !is_ident_in_scope_sema("fs")) {
                if (mem->field == "read_all") {
                    if (auto r = emit_error(diag_at(line, col, "cfire/DEPRECATED-API", std::format("'fs.read_all' has been renamed to 'fs.read_bytes' (or 'fs.read_text' for UTF-8 strings)"), {{"from", "fs.read_all"}, {"to", "fs.read_bytes"}})); !r) return r;
                } else if (mem->field == "write_all") {
                    if (auto r = emit_error(diag_at(line, col, "cfire/DEPRECATED-API", std::format("'fs.write_all' has been renamed to 'fs.write_bytes' (or 'fs.write_text' for UTF-8 strings)"), {{"from", "fs.write_all"}, {"to", "fs.write_bytes"}})); !r) return r;
                }
            }
            if (obj_id && obj_id->name == "os" && !is_ident_in_scope_sema("os")) {
                if (mem->field == "read_stdin" || mem->field == "read_stdin_bytes" || mem->field == "read_stdin_lines") {
                    if (auto r = emit_error(diag_at(line, col, "cfire/DEPRECATED-API", std::format("'os.{}' has been replaced by 'os.stdin().read_text()', 'os.stdin().read_bytes()', or 'os.stdin().read_lines()'", mem->field), {{"from", "os." + std::string(mem->field)},
                        {"to", mem->field == "read_stdin_lines" ? "try os.stdin().read_lines"
                             : mem->field == "read_stdin_bytes" ? "try os.stdin().read_bytes" : "try os.stdin().read_text"}})); !r) return r;
                } else if (mem->field == "fd_read" || mem->field == "fd_read_bytes" || mem->field == "fd_write" ||
                           mem->field == "fd_close" || mem->field == "poll_fd") {
                    if (auto r = emit_error(diag_at(line, col, "cfire/DEPRECATED-API", std::format("'os.{}' has been removed; use methods on 'fs.File' ('os.stdin()', 'os.stdout()', 'os.stderr()', 'os.pipe()') or 'net.TcpConn'", mem->field))); !r) return r;
                } else if (mem->field == "socketpair") {
                    if (auto r = emit_error(diag_at(line, col, "cfire/DEPRECATED-API", std::format("'os.socketpair' has been moved to 'net.socketpair() -> (net.TcpConn, net.TcpConn)!'"))); !r) return r;
                } else if (mem->field == "is_tty" && !call.args.empty()) {
                    if (auto r = emit_error(diag_at(line, col, "cfire/DEPRECATED-API", std::format("'os.is_tty(fd)' no longer accepts a raw FD; use 'os.is_tty()' for stdin or call '.is_tty()' on an 'fs.File' handle (e.g. 'os.stdout().is_tty()')"))); !r) return r;
                }
            }
            // CF-181: `o.release()` moves ownership out as a raw pointer; `own[T].from_raw(p)` adopts one.
            if (mem->field == "release" && smart_wrap_of(mem->object) == "own") {
                if (!call.args.empty()) {
                    if (auto r = emit_error(diag_at(line, col, "cfire/ARITY-MISMATCH", "'release' takes no arguments")); !r) return r;
                }
                if (auto pi = extract_path_info(mem->object)) moved_at_line_[pi->root] = line;
                return {};
            }
            if (std::string own_t = own_type_prefix(mem->object); !own_t.empty() && mem->field == "from_raw") {
                if (call.args.size() != 1) {
                    if (auto r = emit_error(diag_at(line, col, "cfire/ARITY-MISMATCH", std::format("'{}.from_raw' expects 1 argument (a raw pointer), got {}", own_t, call.args.size()))); !r) return r;
                } else {
                    if (auto r = check_expr(call.args[0].value); !r) return r;
                    TypeId at = infer_expr_type_id_sema(call.args[0].value);
                    if (is_concrete_checkable_type(at)) {
                        const auto k = cf_.type_table.get(at).kind;
                        if (k != TypeKind::RawPtr && k != TypeKind::MutPtr) {
                            if (auto r = emit_error(diag_at(call.args[0].line, call.args[0].col, "cfire/TYPE-MISMATCH", std::format("'{}.from_raw' expects a raw pointer, got '{}'", own_t, cf_.type_table.to_string(at)))); !r) return r;
                        }
                    }
                }
                return {};
            }
            bool retired_call_reported = false;
            if (TypeId prim = primitive_type_prefix(mem->object); prim.is_valid()) {
                std::string tname = cf_.type_table.to_string(prim);
                if (mem->field != "parse") {
                    if (auto r = emit_error(diag_at(line, col, "cfire/UNKNOWN-FIELD", std::format("type '{}' has no associated function '{}'; use '{}.parse(s)'", tname, mem->field, tname))); !r) return r;
                } else if (call.args.size() != 1) {
                    if (auto r = emit_error(diag_at(line, col, "cfire/ARITY-MISMATCH", std::format("'{}.parse' expects 1 argument, got {}", tname, call.args.size()))); !r) return r;
                } else if (TypeId at = infer_expr_type_id_sema(call.args[0].value); is_concrete_checkable_type(at) && !cf_.type_table.is_assignable(TypeId::Str(), at)) {
                    if (auto r = emit_error(diag_at(call.args[0].line, call.args[0].col, "cfire/TYPE-MISMATCH", std::format("'{}.parse' expects 'str', got '{}'", tname, cf_.type_table.to_string(at)))); !r) return r;
                }
                if (!call.args.empty()) { if (auto r = check_expr(call.args[0].value); !r) return r; }
                return {};
            }
            if (obj_id && !is_ident_in_scope_sema(obj_id->name)) {
                std::string ns = resolve_stdlib_ns_sema(obj_id->name);
                if (ns.empty()) ns = obj_id->name;
                if (const StdlibCtor* sc = find_stdlib_ctor_by_old_fn(ns, mem->field)) {
                    // Q25: library constructors are `make(ns.Type, args...)`. Generic ones need the type argument,
                    // taken from `[T]` when spelled or from the initial value otherwise.
                    std::string to_type = obj_id->name + "." + std::string(sc->type);
                    if (sc->generic) {
                        if (mem->field == "new_mutex") {
                            to_type += "[void]";
                        } else if (call.type_args.empty() && !call.args.empty()) {
                            TypeId at = infer_expr_type_id_sema(call.args[0].value);
                            to_type += "[" + (is_concrete_checkable_type(at) ? cf_.type_table.to_string(at) : std::string("T")) + "]";
                        }
                    }
                    std::string shown = to_type.find('[') == std::string::npos && !call.type_args.empty()
                        ? to_type + "[" + format_type_ref_sema(call.type_args[0]) + "]" : to_type;
                    retired_call_reported = true;
                    if (auto r = emit_error(diag_at(line, col, "cfire/RETIRED-API", std::format("'{}.{}(...)' has been replaced by 'make({}, ...)'", obj_id->name, mem->field, shown), {{"rewrite", "ctor_to_make"}, {"from", obj_id->name + "." + std::string(mem->field)}, {"to_type", to_type}})); !r) return r;
                }
                if (const ApiMigration* mig = find_api_migration(ns, mem->field)) {
                    std::string from = obj_id->name + "." + mem->field;
                    std::string msg;
                    std::unordered_map<std::string, std::string> facts;
                    switch (mig->kind) {
                        case ApiMigrationKind::CallToMethod:
                            msg = std::format("'{}(x, ...)' has been removed; call the method 'x.{}(...)' instead", from, mig->to);
                            facts = {{"rewrite", "call_to_method"}, {"from", from}, {"method", std::string(mig->to)}};
                            break;
                        case ApiMigrationKind::Rename:
                            msg = std::format("'{}' has been removed; use '{}'", from, mig->to);
                            facts = {{"from", from + "("}, {"to", std::string(mig->to) + "("}};
                            break;
                        case ApiMigrationKind::CallToConst:
                            msg = std::format("'{}()' has been removed; use the constant '{}'", from, mig->to);
                            facts = {{"from", from + "()"}, {"to", std::string(mig->to)}};
                            break;
                        case ApiMigrationKind::RenameFn:
                            msg = std::format("'{}' has been renamed to '{}.{}'", from, obj_id->name, mig->to);
                            facts = {{"from", from + "("}, {"to", obj_id->name + "." + std::string(mig->to) + "("}};
                            break;
                        case ApiMigrationKind::SwapToFn:
                            msg = std::format("'{}(host, port)' has been replaced by '{}.{}(port, host)'", from, obj_id->name, mig->to);
                            facts = {{"rewrite", "swap_to_fn"}, {"from", from}, {"to", obj_id->name + "." + std::string(mig->to)}};
                            break;
                        case ApiMigrationKind::TlsServerFiles:
                        case ApiMigrationKind::TlsServerPem: {
                            const bool pem = mig->kind == ApiMigrationKind::TlsServerPem;
                            msg = std::format("'{}' has been replaced by 'make({}.Server, port, tls: {}.TlsConfig{{{}: ..., {}: ...}})'", from, obj_id->name, obj_id->name, pem ? "cert_pem" : "cert_file", pem ? "key_pem" : "key_file");
                            facts = {{"rewrite", "tls_server"}, {"from", from}, {"ns", obj_id->name}, {"pem", pem ? "1" : ""}};
                            break;
                        }
                        case ApiMigrationKind::AppendVerifyOff:
                            msg = std::format("'{}' has been removed; pass 'verify_peer: false' to '{}.{}' instead", from, obj_id->name, mig->to);
                            facts = {{"rewrite", "append_verify_off"}, {"from", from}, {"to", obj_id->name + "." + std::string(mig->to)}};
                            break;
                    }
                    if (auto r = emit_error(diag_at(line, col, "cfire/RETIRED-API", std::move(msg), std::move(facts))); !r) return r;
                    retired_call_reported = true;
                }
            }
            if (mem->field == "find" || mem->field == "rfind") {
                bool is_str_ns = (obj_id && obj_id->name == "str" && !is_ident_in_scope_sema(obj_id->name));
                if (!is_str_ns && mem->object && type_name_of(mem->object) == "str") {
                    std::string_view canonical = (mem->field == "rfind") ? "last_index_of" : "index_of";
                    if (auto r = emit_error(diag_at(line, col, "cfire/UNKNOWN-METHOD", std::format("unknown str method '{}'; use '{}' instead (returns 'usize?')", mem->field, canonical))); !r) return r;
                }
            }
            if (mem->object && !(obj_id && catalog_idx.has_namespace(obj_id->name) && !is_ident_in_scope_sema(obj_id->name))) {
                if (auto r = check_builtin_method_surface(*mem, line, col); !r) return r;
                if (auto r = check_builtin_method_call(call, *mem, line, col); !r) return r;
            }
            target_sig = resolve_member_call_sig(*mem);
            if (!target_sig && (!obj_id || !catalog_idx.has_namespace(obj_id->name) || is_ident_in_scope_sema(obj_id->name))) {
                if (mem->field == "read_str") {
                    if (auto r = emit_error(diag_at(line, col, "cfire/DEPRECATED-API", std::format("'.read_str' has been renamed to '.read_text'"), {{"from", ".read_str"}, {"to", ".read_text"}})); !r) return r;
                } else if (mem->field == "write_str") {
                    if (auto r = emit_error(diag_at(line, col, "cfire/DEPRECATED-API", std::format("'.write_str' has been renamed to '.write_text'"), {{"from", ".write_str"}, {"to", ".write_text"}})); !r) return r;
                } else if (mem->field == "sftp_read_all") {
                    if (auto r = emit_error(diag_at(line, col, "cfire/DEPRECATED-API", std::format("'.sftp_read_all' has been renamed to '.sftp_read_text' or '.sftp_read_bytes'"))); !r) return r;
                } else if (mem->field == "sftp_write_all") {
                    if (auto r = emit_error(diag_at(line, col, "cfire/DEPRECATED-API", std::format("'.sftp_write_all' has been renamed to '.sftp_write_text' or '.sftp_write_bytes'"))); !r) return r;
                }
                std::string recv_t = mem->object ? type_name_of(mem->object) : "";
                bool is_stream_handle = (recv_t == "File" || recv_t == "fs.File" || recv_t == "fs::File" ||
                                         recv_t == "TcpConn" || recv_t == "net.TcpConn" || recv_t == "net::TcpConn" ||
                                         recv_t == "ssh.Channel" || recv_t == "ssh::Channel" ||
                                         (recv_t == "Channel" && is_stdlib_ns_imported("ssh")));
                if (is_stream_handle) {
                    if (mem->field == "read_all") {
                        if (auto r = emit_error(diag_at(line, col, "cfire/DEPRECATED-API", std::format("'.read_all' on File has been replaced by '.read_bytes()' or '.read_text()'"))); !r) return r;
                    } else if (mem->field == "read") {
                        if (auto r = emit_error(diag_at(line, col, "cfire/DEPRECATED-API", std::format("'.read' has been replaced by explicit '.read_bytes', '.read_text', or '.read_into'"))); !r) return r;
                    } else if (mem->field == "write") {
                        if (auto r = emit_error(diag_at(line, col, "cfire/DEPRECATED-API", std::format("'.write' has been replaced by explicit '.write_bytes' or '.write_text'"))); !r) return r;
                    }
                }
            }
            if (!target_sig && obj_id) {
                std::string resolved_ns = resolve_stdlib_ns_sema(obj_id->name);
                if (!resolved_ns.empty() && catalog_idx.has_namespace(resolved_ns) && !is_ident_in_scope_sema(obj_id->name)) {
                    std_sig = catalog_idx.find_ns_fn(resolved_ns, mem->field);
                    if (!std_sig && !retired_call_reported && !is_vendor_alias_sema(obj_id->name) && obj_id->name != "cpp" && !is_cpp_qualifier_sema(obj_id->name) &&
                        !catalog_idx.is_known_ns_member(resolved_ns, mem->field)) {
                        std::string sugg = catalog_idx.suggest_closest_fn(resolved_ns, mem->field);
                        std::string hint = sugg.empty() ? "" : std::format("; did you mean '{}.{}'?", obj_id->name, sugg);
                        if (auto r = emit_error(diag_at(line, col, "cfire/UNKNOWN-STDLIB-FN", std::format("unknown function '{}.{}' in module '{}'{}", obj_id->name, mem->field, catalog_idx.import_path_for(resolved_ns), hint))); !r) return r;
                    }
                } else if (auto dot = obj_id->name.find('.'); dot != std::string::npos) {
                    std::string ns_part = obj_id->name.substr(0, dot);
                    std::string type_part = obj_id->name.substr(dot + 1);
                    if (catalog_idx.has_namespace(ns_part) && !is_ident_in_scope_sema(ns_part)) {
                        std_sig = catalog_idx.find_ns_fn(ns_part, type_part + "." + mem->field);
                    }
                }
            }
            if (!target_sig && !std_sig && mem->object) {
                std::string recv_t = type_name_of(mem->object);
                if (recv_t == "File" || recv_t == "fs.File" || recv_t == "fs::File" ||
                    recv_t == "TcpConn" || recv_t == "net.TcpConn" || recv_t == "net::TcpConn") {
                    std::string norm_t = (recv_t.ends_with("File")) ? "File" : "TcpConn";
                    if (auto mit = catalog_idx.receiver_methods.find(norm_t + "." + mem->field); mit != catalog_idx.receiver_methods.end()) {
                        std_sig = &mit->second;
                    }
                }
            }
            if (target_sig && target_sig->receiver && target_sig->receiver->intent == "mut") {
                if (obj_id && imm_vars_.contains(obj_id->name) && !smart_ptr_vars_.contains(obj_id->name)) {
                    if (auto r = emit_error(diag_at(line, col, "cfire/MUT-RECEIVER", std::format("cannot call mutating method '{}' on immutable variable '{}'", mem->field, obj_id->name), {{"var", obj_id->name}})); !r) return r;
                }
            }
            if (mem->field == "push" && !call.args.empty() && is_byte_slice_expr(mem->object)) {
                if (is_non_byte_int_or_u8_expr(call.args[0].value)) {
                    if (auto r = emit_error(diag_at(call.args[0].line, call.args[0].col, "cfire/TYPE-MISMATCH", std::format("cannot push integer/u8 value into '[]byte' without explicit 'byte(...)' conversion"))); !r) return r;
                }
            }
            const BuiltinMethod* mut_bm = lookup_builtin_method(*mem);
            if ((mut_bm && mut_bm->mutates) || mem->field == "push" || mem->field == "pop" || mem->field == "clear" ||
                mem->field == "erase" || mem->field == "remove") {
                if (auto pi = extract_path_info(mem->object)) {
                    if (auto r = check_not_iterated_collection(pi->root, line, col); !r) return r;
                    invalidate_loans_for_owner(
                        pi->root,
                        std::format("underlying container '{}' was mutated at line {}", pi->root, line),
                        line);
                }
            }
            if (!call.args.empty() && !is_constant_string_expr(call.args[0].value)) {
                // CF-167: an error, not a warning, and it follows data through locals and helper calls.
                const auto* recv_id = mem->object ? std::get_if<IdentExpr>(&mem->object->node) : nullptr;
                if (recv_id && recv_id->name == "os" && !is_ident_in_scope_sema("os") &&
                    (mem->field == "exec" || mem->field == "exec_output")) {
                    if (auto r = emit_error(diag_at(call.args[0].line, call.args[0].col, "cfire/SHELL-INJECTION-RISK", std::format("shell command passed to 'os.{}' is not a constant string, so runtime data can inject shell syntax; use 'os.command(binary, args)' with separate arguments", mem->field))); !r) return r;
                } else if ((!recv_id || recv_id->name != "os") && (mem->field == "exec" || mem->field == "query") &&
                           is_sql_receiver(mem->object)) {
                    if (auto r = emit_error(diag_at(call.args[0].line, call.args[0].col, "cfire/SQL-INJECTION-RISK", std::format("SQL passed to '{}' is not a constant string, so runtime data can inject SQL; keep the statement constant and bind values with '?1', '?2' parameters", mem->field))); !r) return r;
                }
            }
        }

        if (target_sig) {
            std::size_t actual_args = call.args.size() + (call.block_arg ? 1 : 0);
            if (actual_args != target_sig->params.size()) {
                if (auto r = emit_error(diag_at(line, col, "cfire/ARITY-MISMATCH", std::format("call to '{}' expects {} argument(s), got {}", target_sig->name, target_sig->params.size(), actual_args))); !r) return r;
            }
            if (auto r = check_call_argument_types(call, *target_sig); !r) return r;
            if (auto r = check_interface_constraints(call, *target_sig, line, col); !r) return r;
        } else if (std_sig) {
            std::size_t actual_args = call.args.size() + (call.block_arg ? 1 : 0);
            if (actual_args < std_sig->min_args || actual_args > std_sig->max_args) {
                std::string exp_str = (std_sig->min_args == std_sig->max_args)
                    ? std::format("{}", std_sig->min_args)
                    : (std_sig->max_args >= 512
                        ? std::format("at least {}", std_sig->min_args)
                        : std::format("{}..{}", std_sig->min_args, std_sig->max_args));
                if (auto r = emit_error(diag_at(line, col, "cfire/ARITY-MISMATCH", std::format("call to '{}.{}' expects {} argument(s), got {}", std_sig->ns_alias, std_sig->name, exp_str, actual_args))); !r) return r;
            }
            for (const auto& a : call.args) {
                if (a.label.empty()) continue;
                if (std::none_of(std_sig->params.begin(), std_sig->params.end(), [&](const StdParamInfo& p) { return p.name == a.label; })) {
                    std::string names;
                    for (const auto& p : std_sig->params) names += (names.empty() ? "" : ", ") + p.name;
                    if (auto r = emit_error(diag_at(a.line, a.col, "cfire/UNKNOWN-ARG-LABEL", std::format("'{}.{}' has no parameter named '{}'; parameters: {}", std_sig->ns_alias, std_sig->name, a.label, names))); !r) return r;
                }
            }
        }

        if (self_expr && !unwrapped_exprs_.contains(self_expr)) {
            std::string fallible_callee;
            std::string fallible_ret;
            if (expr_returns_fallible_sema(self_expr, &fallible_callee, &fallible_ret)) {
                auto [start_ln, start_col] = expr_start_loc(self_expr);
                if (auto r = emit_error(diag_at(start_ln, start_col, "cfire/UNHANDLED-RESULT", std::format("fallible call to '{}' returns '{}' and must be handled with 'try' or 'or'", fallible_callee, fallible_ret))); !r) return r;
            }
        }

        struct SeenArgPath {
            PathInfo info;
            bool is_mut_or_out;
        };
        std::vector<SeenArgPath> seen_paths;

        if (const auto* mem = std::get_if<MemberExpr>(&call.callee->node)) {
            if (auto p = extract_path_info(mem->object)) {
                bool is_mut = (target_sig && target_sig->receiver && target_sig->receiver->intent == "mut");
                seen_paths.push_back({*p, is_mut});
            }
        }

        for (std::size_t i = 0; i < call.args.size(); ++i) {
            const auto& arg = call.args[i];
            if (auto r = check_expr(arg.value); !r) return r;
            bool expected_is_move = false;
            if (target_sig && i < target_sig->params.size()) {
                const auto& expected = target_sig->params[i].intent;
                expected_is_move = (expected == "move");
                if ((expected == "mut" || expected == "out" || expected == "move") && arg.intent != expected) {
                    std::string_view rule = (expected == "out")
                        ? "cfire/OUT-CALLSITE"
                        : (expected == "move" ? "cfire/MOVE-CALLSITE" : "cfire/MUT-CALLSITE");
                    if (auto r = emit_error(diag_at(arg.line, arg.col, rule, std::format("parameter '{}' of {} requires explicit '{}' annotation at call-site", target_sig->params[i].name, target_sig->name, expected), {{"keyword", expected}})); !r) return r;
                } else if (expected == "in" && (arg.intent == "mut" || arg.intent == "out")) {
                    if (auto r = emit_error(diag_at(arg.line, arg.col, "cfire/REDUNDANT-CALLSITE-INTENT", std::format("parameter '{}' of '{}' has 'in' intent; remove redundant '{}' annotation at call-site", target_sig->params[i].name, target_sig->name, arg.intent), {{"keyword", arg.intent}})); !r) return r;
                }
                const auto& pt = target_sig->params[i].type;
                if ((pt.is_slice || pt.is_array) && pt.name == "byte") {
                    if (const auto* arr_lit = std::get_if<ArrayLitExpr>(&arg.value->node)) {
                        for (const auto& el : arr_lit->elements) {
                            if (is_non_byte_int_or_u8_expr(el)) {
                                if (auto r = emit_error(diag_at(el->line, el->col, "cfire/TYPE-MISMATCH", std::format("cannot use integer/u8 value in '[]byte' literal without explicit 'byte(...)' conversion"))); !r) return r;
                            }
                        }
                    }
                }
            } else if (std_sig && i < std_sig->params.size()) {
                const auto& expected = std_sig->params[i].intent;
                expected_is_move = (expected == "move");
                if ((expected == "mut" || expected == "out" || expected == "move") && arg.intent != expected) {
                    std::string_view rule = (expected == "out")
                        ? "cfire/OUT-CALLSITE"
                        : (expected == "move" ? "cfire/MOVE-CALLSITE" : "cfire/MUT-CALLSITE");
                    if (auto r = emit_error(diag_at(arg.line, arg.col, rule, std::format("parameter '{}' of {}.{} requires explicit '{}' annotation at call-site", std_sig->params[i].name, std_sig->ns_alias, std_sig->name, expected), {{"keyword", expected}})); !r) return r;
                } else if (expected == "in" && (arg.intent == "mut" || arg.intent == "out")) {
                    if (auto r = emit_error(diag_at(arg.line, arg.col, "cfire/REDUNDANT-CALLSITE-INTENT", std::format("parameter '{}' of '{}.{}' has 'in' intent; remove redundant '{}' annotation at call-site", std_sig->params[i].name, std_sig->ns_alias, std_sig->name, arg.intent))); !r) return r;
                }
            } else if ((arg.intent == "mut" || arg.intent == "out") && call.callee) {
                if (const auto* cid = std::get_if<IdentExpr>(&call.callee->node)) {
                    if (!is_ident_in_scope_sema(cid->name) &&
                        (cid->name == "print" || cid->name == "println" || cid->name == "assert" ||
                         cid->name == "len" || cid->name == "cap" || cid->name == "str" ||
                         cid->name == "int" || cid->name == "i8" || cid->name == "i16" ||
                         cid->name == "i32" || cid->name == "i64" || cid->name == "isize" ||
                         cid->name == "u8" || cid->name == "u16" || cid->name == "u32" ||
                         cid->name == "u64" || cid->name == "usize" || cid->name == "byte" ||
                         cid->name == "f32" || cid->name == "f64" || cid->name == "bool" ||
                         cid->name == "make" || cid->name == "panic" || cid->name == "exit")) {
                        if (auto r = emit_error(diag_at(arg.line, arg.col, "cfire/REDUNDANT-CALLSITE-INTENT", std::format("builtin '{}' takes read-only 'in' arguments; remove redundant '{}' annotation at call-site", cid->name, arg.intent), {{"keyword", arg.intent}})); !r) return r;
                    }
                }
            }
            if (arg.intent == "move") {
                if (auto r = validate_move_operand(arg.value, expected_is_move, arg.line, arg.col); !r) return r;
            }
            if (auto pi = extract_path_info(arg.value)) {
                bool is_mut_or_out = (arg.intent == "mut" || arg.intent == "out");
                for (const auto& prev : seen_paths) {
                    if (prev.info.root == pi->root && (prev.is_mut_or_out || is_mut_or_out)) {
                        if (paths_conflict(prev.info.path, pi->path)) {
                            std::string entity = (pi->path == pi->root && prev.info.path == prev.info.root)
                                ? std::format("variable '{}'", pi->root)
                                : std::format("projection '{}'", pi->path);
                            if (auto r = emit_error(diag_at(arg.line, arg.col, "cfire/MUT-ALIAS", std::format("cannot alias {} with a mut/out parameter in the same call", entity))); !r) return r;
                        }
                    }
                }
                seen_paths.push_back({*pi, is_mut_or_out});

                if (is_mut_or_out) {
                    if (imm_vars_.contains(pi->root)) {
                        std::string entity = (pi->path == pi->root) ? pi->root : pi->path;
                        if (auto r = emit_error(diag_at(arg.line, arg.col, "cfire/IMMUTABLE-MUT-ARG", std::format("cannot pass immutable variable '{}' as '{}' argument", entity, arg.intent), {{"var", entity}})); !r) return r;
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
        if (const auto* id = std::get_if<IdentExpr>(&call.callee->node)) {
            if (id->name == "assert" && !call.args.empty()) {
                if (auto nc = extract_null_check(call.args[0].value); nc && nc->second) {
                    ptr_null_state_[nc->first] = NullState::NonNull;
                }
            }
        }
        bool is_foreign_call = false;
        if (const auto* cid = std::get_if<IdentExpr>(&call.callee->node)) {
            if (cid->name.starts_with("cpp.") || is_cpp_qualifier_sema(cid->name) || is_vendor_alias_sema(cid->name) || cf_.cpp_fn_params.contains(cid->name)) {
                is_foreign_call = true;
            }
        } else if (const auto* cmem = std::get_if<MemberExpr>(&call.callee->node)) {
            const auto* obj_id = cmem->object ? std::get_if<IdentExpr>(&cmem->object->node) : nullptr;
            if (obj_id && (obj_id->name == "cpp" || is_cpp_qualifier_sema(obj_id->name) || is_vendor_alias_sema(obj_id->name))) {
                is_foreign_call = true;
            }
        }
        if (is_foreign_call) {
            // CF-124 / F9: foreign C++ calls invalidate loans and null facts of arguments passed
            for (const auto& a : call.args) {
                if (!a.value) continue;
                if (auto pi = extract_path_info(a.value)) {
                    invalidate_loans_for_owner(pi->root, "invalidated by foreign C++ call", line);
                    if (auto it = ptr_null_state_.find(pi->root); it != ptr_null_state_.end()) {
                        it->second = NullState::MaybeNull;
                    }
                    if (borrow_origin_.contains(pi->root)) {
                        invalidated_loans_[pi->root] = InvalidatedLoan{"loan invalidated by foreign C++ call", line};
                    }
                }
            }
        }
        if (in_escaping_closure_depth_ > 0 && call.callee) {
            if (const auto* mem = std::get_if<MemberExpr>(&call.callee->node)) {
                const BuiltinMethod* esc_bm = lookup_builtin_method(*mem);
                if ((esc_bm && esc_bm->mutates) || mem->field == "push" || mem->field == "pop" || mem->field == "clear" ||
                    mem->field == "insert" || mem->field == "erase" || mem->field == "remove") {
                    std::string root = get_root_variable_name(mem->object);
                    if (!root.empty() && root != "_") {
                        auto sit = var_scope_depth_.find(root);
                        if (sit != var_scope_depth_.end() && sit->second <= escaping_closure_entry_scope_) {
                            std::string r_ty;
                            if (auto tit = var_types_.find(root); tit != var_types_.end()) r_ty = tit->second;
                            if (!is_concurrency_safe_type(r_ty)) {
                                return emit_error(diag_at(line, 0, "cfire/ESCAPING-CAPTURE-MUT", std::format("cannot mutate captured outer variable '{}' inside escaping closure; wrap shared mutable state in 'sync.Mutex[T]', 'sync.Atomic[T]', or communicate via 'sync.Channel[T]'", root)));
                            }
                        }
                    }
                }
            }
        }
        bool is_spawn_call = false;
        if (const auto* mem = std::get_if<MemberExpr>(&call.callee->node)) {
            if (mem->field == "spawn") is_spawn_call = true;
        }
        if (is_spawn_call) {
            for (const auto& a : call.args) {
                if (a.value) {
                    if (auto* cl = std::get_if<ClosureExpr>(&a.value->node)) {
                        cl->is_escaping = true;
                    }
                }
            }
        }
        if (call.block_arg) {
            bool was_escaping = call.block_arg->is_escaping || is_spawn_call;
            int saved_escaping_entry_scope = escaping_closure_entry_scope_;
            if (was_escaping) {
                ++in_escaping_closure_depth_;
                escaping_closure_entry_scope_ = scope_depth_;
            }
            int saved_spawn_entry_scope = spawn_entry_scope_;
            if (is_spawn_call) {
                ++in_spawn_closure_depth_;
                spawn_entry_scope_ = scope_depth_;
            }
            auto spawn_guard = cfire::scope_exit([this, is_spawn_call, saved_spawn_entry_scope] {
                if (is_spawn_call) {
                    --in_spawn_closure_depth_;
                    spawn_entry_scope_ = saved_spawn_entry_scope;
                }
            });
            ++in_closure_depth_;
            int saved_loop_depth = loop_depth_;
            int saved_invalid_unwrap = in_invalid_unwrap_context_;
            loop_depth_ = 0;
            in_invalid_unwrap_context_ = 0;
            auto cl_guard = cfire::scope_exit([this, saved_loop_depth, saved_invalid_unwrap, was_escaping, saved_escaping_entry_scope] {
                --in_closure_depth_;
                if (was_escaping) {
                    --in_escaping_closure_depth_;
                    escaping_closure_entry_scope_ = saved_escaping_entry_scope;
                }
                loop_depth_ = saved_loop_depth;
                in_invalid_unwrap_context_ = saved_invalid_unwrap;
            });
            ScopeGuard sg(*this);
            auto hint_it = closure_param_hints_.find(&*call.block_arg);
            for (std::size_t i = 0; i < call.block_arg->params.size(); ++i) {
                bool is_mut = (i < call.block_arg->param_intents.size()) &&
                              (call.block_arg->param_intents[i] == "mut" || call.block_arg->param_intents[i] == "out");
                bind_scoped_var(call.block_arg->params[i], is_mut);
                bool typed = i < call.block_arg->param_types.size() && call.block_arg->param_types[i].has_value();
                if (typed) {
                    var_types_[call.block_arg->params[i]] = format_type_ref_sema(*call.block_arg->param_types[i]);
                } else if (hint_it != closure_param_hints_.end() && i < hint_it->second.size()) {
                    var_types_[call.block_arg->params[i]] = hint_it->second[i];
                }
            }
            if (auto r = check_block_stmts(call.block_arg->body); !r) return r;
        }
        return {};
    }

    [[nodiscard]] auto check_match_common(
        const ExprPtr& target,
        bool is_move,
        const std::vector<MatchArm>& arms,
        int line,
        int col,
        bool is_expr_match
    ) -> std::expected<void, std::string> {
        if (auto r = check_expr(target); !r) return r;
        if (is_move && target) {
            if (auto r = validate_move_operand(target, false, line, col); !r) return r;
            if (auto pi = extract_path_info(target)) {
                if (auto r = check_not_iterated_collection(pi->root, line, col); !r) return r;
                moved_at_line_[pi->root] = line;
                invalidate_loans_for_owner(
                    pi->root,
                    std::format("underlying owner '{}' was moved at line {}", pi->root, line),
                    line);
            }
        }
        if (auto r = check_match_arms_reachability(arms, line, col); !r) return r;
        bool is_slice_match = false;
        bool is_primitive_match = false;
        for (const auto& a : arms) {
            if (a.is_slice_pattern) is_slice_match = true;
            if (!a.literal_val.empty()) is_primitive_match = true;
        }
        if (!is_expr_match && is_slice_match) {
            for (const auto& arm : arms) {
                ScopeGuard sg(*this);
                bind_match_arm_vars(arm);
                if (auto r = check_block_stmts(arm.body); !r) return r;
            }
            return {};
        }

        std::string enum_name;
        std::string target_t;
        if (target) {
            target_t = type_name_of(target);
            if (cf_.enums.contains(target_t)) {
                enum_name = target_t;
            }
        }
        std::unordered_set<std::string> covered;
        bool has_wildcard = false;

        for (const auto& arm : arms) {
            std::vector<MatchPattern> pats = arm.patterns.empty()
                ? std::vector<MatchPattern>{arm.to_pattern()}
                : arm.patterns;
            for (const auto& pat : pats) {
                if (pat.variant_name == "_") {
                    if (!arm.guard) has_wildcard = true;
                } else {
                    if (!arm.guard) covered.insert(pat.variant_name);
                    if (enum_name.empty() && !pat.enum_prefix.empty() && cf_.enums.contains(pat.enum_prefix)) {
                        enum_name = pat.enum_prefix;
                    }
                }
            }
            ScopeGuard sg(*this);
            bind_match_arm_vars(arm, target_t);
            if (arm.guard) {
                if (auto r = check_expr(arm.guard); !r) return r;
            }
            if (auto r = check_block_stmts(arm.body, is_expr_match); !r) return r;
        }

        bool is_opt = target_t.ends_with('?') || target_t.starts_with("std::optional");
        bool is_res = target_t.ends_with('!') || target_t.starts_with("cfire::Result");

        if (is_opt && !has_wildcard) {
            bool has_some = covered.contains("Some");
            bool has_none = covered.contains("none");
            if (!has_some || !has_none) {
                if (auto r = emit_error(diag_at(line, col, "cfire/NON-EXHAUSTIVE-MATCH", std::format("non-exhaustive match on optional '{}': missing '{}'", target_t, !has_some ? "Some" : "none"))); !r) return r;
            }
        } else if (is_res && !has_wildcard) {
            bool has_ok = covered.contains("Ok");
            bool has_err = covered.contains("Err");
            if (!has_ok || !has_err) {
                if (auto r = emit_error(diag_at(line, col, "cfire/NON-EXHAUSTIVE-MATCH", std::format("non-exhaustive match on fallible '{}': missing '{}'", target_t, !has_ok ? "Ok" : "Err"))); !r) return r;
            }
        } else if (!enum_name.empty()) {
            if (auto eit = cf_.enums.find(enum_name); eit != cf_.enums.end()) {
                for (const auto& arm : arms) {
                    std::vector<MatchPattern> pats = arm.patterns.empty()
                        ? std::vector<MatchPattern>{arm.to_pattern()}
                        : arm.patterns;
                    for (const auto& pat : pats) {
                        if (pat.variant_name != "_" && pat.literal_val.empty() && !pat.is_slice_pattern) {
                            if (!pat.enum_prefix.empty() && pat.enum_prefix != enum_name) {
                                if (auto r = emit_error(diag_at(pat.line, 0, "cfire/TYPE-MISMATCH", std::format("variant pattern '{}::{}' does not match target enum '{}'", pat.enum_prefix, pat.variant_name, enum_name))); !r) return r;
                            }
                            bool found_var = false;
                            for (const auto& v : eit->second.variants) {
                                if (v.name == pat.variant_name) {
                                    found_var = true;
                                    break;
                                }
                            }
                            if (!found_var) {
                                if (auto r = emit_error(diag_at(pat.line, 1, "cfire/UNKNOWN-ENUM-VARIANT", std::format("unknown variant '{}' for enum '{}'", pat.variant_name, enum_name))); !r) return r;
                            }
                        }
                    }
                }
                if (!has_wildcard) {
                    for (const auto& v : eit->second.variants) {
                        if (!covered.contains(v.name)) {
                            if (auto r = emit_error(diag_at(line, col, "cfire/NON-EXHAUSTIVE-MATCH", std::format("non-exhaustive match on enum '{}': missing variant '{}'", enum_name, v.name), {{"enum", enum_name}, {"missing", v.name}})); !r) return r;
                        }
                    }
                }
            }
        } else if ((is_primitive_match || is_scalar_primitive_type_name(target_t) || target_t == "str") && !has_wildcard) {
            if (auto r = emit_error(diag_at(line, col, "cfire/NON-EXHAUSTIVE-MATCH", std::format("match on '{}' must have an exhaustive wildcard '_' fallback arm", target_t.empty() ? "primitive scalar" : target_t))); !r) return r;
        } else if (is_expr_match && !has_wildcard) {
            if (!is_slice_match || !is_slice_exhaustively_covered(arms)) {
                if (auto r = emit_error(diag_at(line, col, "cfire/NON-EXHAUSTIVE-MATCH", std::format("match expression must have a wildcard '_' fallback arm"))); !r) return r;
            }
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
                return emit_error(diag_at(arm.line > 1 ? arm.line : match_line, arm.line > 1 ? 0 : match_col, "cfire/UNREACHABLE-MATCH-ARM", std::format("unreachable match arm #{}: previous wildcard '_' arm already matches all remaining cases", idx + 1)));
            }
            if (arm.guard != nullptr) {
                // Guarded arms are conditional; they do not fully cover the pattern and cannot be statically proven unreachable
                continue;
            }
            if (arm.is_slice_pattern) {
                std::size_t min_sz = arm.slice_head.size() + arm.slice_tail.size();
                if (!arm.slice_has_rest) {
                    if (is_size_covered(min_sz)) {
                        return emit_error(diag_at(arm.line > 1 ? arm.line : match_line, arm.line > 1 ? 0 : match_col, "cfire/UNREACHABLE-MATCH-ARM", std::format("unreachable slice match arm #{} (exact length {} is already matched by an earlier arm)", idx + 1, min_sz)));
                    }
                    exact_slice_sizes.insert(min_sz);
                } else {
                    if (all_sizes_from_covered(min_sz)) {
                        return emit_error(diag_at(arm.line > 1 ? arm.line : match_line, arm.line > 1 ? 0 : match_col, "cfire/UNREACHABLE-MATCH-ARM", std::format("unreachable slice match arm #{} (slices of length >= {} are already matched by earlier arms)", idx + 1, min_sz)));
                    }
                    if (!min_variadic_slice || min_sz < *min_variadic_slice) {
                        min_variadic_slice = min_sz;
                    }
                }
                continue;
            }
            if (arm.variant_name == "_" && arm.literal_val.empty()) {
                if (min_variadic_slice && all_sizes_from_covered(0)) {
                    return emit_error(diag_at(arm.line > 1 ? arm.line : match_line, arm.line > 1 ? 0 : match_col, "cfire/UNREACHABLE-MATCH-ARM", std::format("unreachable wildcard '_' match arm #{}: all slice lengths are already covered by earlier arms", idx + 1)));
                }
                seen_wildcard = true;
                continue;
            }
            std::vector<MatchPattern> pats = arm.patterns.empty()
                ? std::vector<MatchPattern>{arm.to_pattern()}
                : arm.patterns;
            for (const auto& pat : pats) {
                if (!pat.literal_val.empty()) {
                    if (seen_literals.contains(pat.literal_val)) {
                        return emit_error(diag_at(arm.line > 1 ? arm.line : match_line, arm.line > 1 ? 0 : match_col, "cfire/UNREACHABLE-MATCH-ARM", std::format("unreachable match arm #{}: duplicate literal pattern '{}'", idx + 1, pat.literal_val)));
                    }
                    seen_literals.insert(pat.literal_val);
                } else if (!pat.variant_name.empty() && pat.variant_name != "_") {
                    if (seen_variants.contains(pat.variant_name)) {
                        return emit_error(diag_at(arm.line > 1 ? arm.line : match_line, arm.line > 1 ? 0 : match_col, "cfire/UNREACHABLE-MATCH-ARM", std::format("unreachable match arm #{}: duplicate variant pattern '{}'", idx + 1, pat.variant_name)));
                    }
                    seen_variants.insert(pat.variant_name);
                }
            }
        }
        return {};
    }

    [[nodiscard]] static auto stmt_has_break(const StmtPtr& s) -> bool {
        if (!s) return false;
        if (std::holds_alternative<BreakStmt>(s->node)) return true;
        if (const auto* es = std::get_if<ExprStmt>(&s->node)) {
            if (es->expr) {
                if (const auto* id = std::get_if<IdentExpr>(&es->expr->node)) {
                    if (id->name == "break") return true;
                }
            }
        }
        if (const auto* ifs = std::get_if<IfStmt>(&s->node)) {
            if (block_has_break(ifs->then_body) || block_has_break(ifs->else_body)) return true;
        }
        if (const auto* ms = std::get_if<MatchStmt>(&s->node)) {
            for (const auto& arm : ms->arms) {
                if (block_has_break(arm.body)) return true;
            }
        }
        return false;
    }

    [[nodiscard]] static auto block_has_break(const std::vector<StmtPtr>& body) -> bool {
        for (const auto& s : body) {
            if (stmt_has_break(s)) return true;
        }
        return false;
    }

    [[nodiscard]] static auto stmt_diverges(const StmtPtr& s, const CheckedFile* cf = nullptr) -> bool {
        if (!s) return false;
        if (std::holds_alternative<ReturnStmt>(s->node)) return true;
        if (std::holds_alternative<BreakStmt>(s->node) || std::holds_alternative<ContinueStmt>(s->node)) return true;
        if (const auto* as = std::get_if<AsyncScopeStmt>(&s->node)) {
            return block_diverges(as->body, cf);
        }
        if (const auto* es = std::get_if<ExprStmt>(&s->node)) {
            if (es->expr) {
                if (const auto* id = std::get_if<IdentExpr>(&es->expr->node)) {
                    if (id->name == "break" || id->name == "continue") return true;
                }
                if (const auto* call = std::get_if<CallExpr>(&es->expr->node)) {
                    if (const auto* id = std::get_if<IdentExpr>(&call->callee->node)) {
                        if (id->name == "panic" || id->name == "exit") return true;
                    } else if (const auto* mem = std::get_if<MemberExpr>(&call->callee->node)) {
                        if (mem->field == "exit" && mem->object) {
                            if (const auto* obj_id = std::get_if<IdentExpr>(&mem->object->node)) {
                                if (obj_id->name == "os") return true;
                            }
                        }
                    }
                }
            }
        }
        if (const auto* ifs = std::get_if<IfStmt>(&s->node)) {
            if (!ifs->else_body.empty() && block_diverges(ifs->then_body, cf) && block_diverges(ifs->else_body, cf)) {
                return true;
            }
        }
        if (const auto* fs = std::get_if<ForStmt>(&s->node)) {
            bool is_infinite = false;
            if (fs->item_var.empty()) {
                if (!fs->iter_expr) {
                    is_infinite = true;
                } else if (const auto* lit = std::get_if<LiteralExpr>(&fs->iter_expr->node)) {
                    if (lit->kind == TokenKind::KwTrue) is_infinite = true;
                }
            }
            if (is_infinite && !block_has_break(fs->body)) {
                return true;
            }
        }
        if (const auto* ms = std::get_if<MatchStmt>(&s->node)) {
            if (!ms->arms.empty()) {
                bool all_diverge = true;
                bool has_wildcard = false;
                std::unordered_set<std::string> arm_variants;
                for (const auto& arm : ms->arms) {
                    if (arm.variant_name == "_") has_wildcard = true;
                    else arm_variants.insert(arm.variant_name);
                    if (!block_diverges(arm.body, cf)) {
                        all_diverge = false;
                    }
                }
                if (all_diverge) {
                    if (has_wildcard) return true;
                    if (arm_variants.contains("Some") && arm_variants.contains("none")) return true;
                    if (arm_variants.contains("Ok") && arm_variants.contains("Err")) return true;
                    if (cf) {
                        for (const auto& [ename, edef] : cf->enums) {
                            if (edef.variants.empty()) continue;
                            bool all_found = true;
                            for (const auto& v : edef.variants) {
                                if (!arm_variants.contains(v.name)) {
                                    all_found = false;
                                    break;
                                }
                            }
                            if (all_found) return true;
                        }
                    }
                }
            }
        }
        return false;
    }

    [[nodiscard]] static auto block_diverges(const std::vector<StmtPtr>& body, const CheckedFile* cf = nullptr) -> bool {
        for (const auto& s : body) {
            if (stmt_diverges(s, cf)) return true;
        }
        return false;
    }

    struct SavedVarDeclState {
        std::optional<int> scope_depth{};
        bool was_local_defined{false};
        bool was_mut{false};
        bool was_imm{false};
        std::optional<std::string> var_type{};
        bool was_optional{false};
        bool was_fallible{false};
        bool was_reported_unhandled{false};
        bool was_scalar_primitive{false};
        bool was_ptr_or_smart{false};
        bool was_raw_or_smart_ptr{false};
        std::optional<NullState> null_state{};
        std::optional<std::size_t> fixed_array_len{};
        bool was_map{false};
        std::optional<std::string> borrow_origin{};
        std::optional<int> moved_at_line{};
        std::optional<InvalidatedLoan> invalidated_loan{};
    };

    struct ScopeGuard {
        SemaFnChecker& checker;
        std::unordered_map<std::string, SavedVarDeclState> saved_vars{};

        explicit ScopeGuard(SemaFnChecker& c) : checker(c) {
            ++checker.scope_depth_;
            checker.scope_stack_.push_back(&saved_vars);
        }
        ~ScopeGuard() {
            for (const auto& [name, st] : saved_vars) {
                if (st.scope_depth) checker.var_scope_depth_[name] = *st.scope_depth;
                else checker.var_scope_depth_.erase(name);

                if (st.was_local_defined) checker.local_defined_vars_.insert(name);
                else checker.local_defined_vars_.erase(name);

                if (st.was_mut) checker.mut_vars_.insert(name);
                else checker.mut_vars_.erase(name);

                if (st.was_imm) checker.imm_vars_.insert(name);
                else checker.imm_vars_.erase(name);

                if (st.var_type) checker.var_types_[name] = *st.var_type;
                else checker.var_types_.erase(name);

                if (st.was_optional) checker.optional_vars_.insert(name);
                else checker.optional_vars_.erase(name);

                if (st.was_fallible) checker.fallible_vars_.insert(name);
                else checker.fallible_vars_.erase(name);

                if (st.was_reported_unhandled) checker.reported_unhandled_vars_.insert(name);
                else checker.reported_unhandled_vars_.erase(name);

                if (st.was_scalar_primitive) checker.scalar_primitive_vars_.insert(name);
                else checker.scalar_primitive_vars_.erase(name);

                if (st.was_ptr_or_smart) checker.ptr_or_smart_vars_.insert(name);
                else checker.ptr_or_smart_vars_.erase(name);

                if (st.was_raw_or_smart_ptr) checker.raw_or_smart_ptr_vars_.insert(name);
                else checker.raw_or_smart_ptr_vars_.erase(name);

                if (st.null_state) checker.ptr_null_state_[name] = *st.null_state;
                else checker.ptr_null_state_.erase(name);

                if (st.fixed_array_len) checker.fixed_array_lens_[name] = *st.fixed_array_len;
                else checker.fixed_array_lens_.erase(name);

                if (st.was_map) checker.map_vars_.insert(name);
                else checker.map_vars_.erase(name);

                if (st.borrow_origin) checker.borrow_origin_[name] = *st.borrow_origin;
                else checker.borrow_origin_.erase(name);

                if (st.moved_at_line) checker.moved_at_line_[name] = *st.moved_at_line;
                else checker.moved_at_line_.erase(name);

                if (st.invalidated_loan) checker.invalidated_loans_[name] = *st.invalidated_loan;
                else checker.invalidated_loans_.erase(name);
            }
            checker.scope_stack_.pop_back();
            --checker.scope_depth_;
        }
        ScopeGuard(const ScopeGuard&) = delete;
        ScopeGuard& operator=(const ScopeGuard&) = delete;
    };

    void save_var_before_redecl(const std::string& name) {
        if (scope_stack_.empty() || name.empty() || name == "_") return;
        auto* top = scope_stack_.back();
        if (top->contains(name)) return;
        SavedVarDeclState st;
        if (auto it = var_scope_depth_.find(name); it != var_scope_depth_.end()) st.scope_depth = it->second;
        st.was_local_defined = local_defined_vars_.contains(name);
        st.was_mut = mut_vars_.contains(name);
        st.was_imm = imm_vars_.contains(name);
        if (auto it = var_types_.find(name); it != var_types_.end()) st.var_type = it->second;
        st.was_optional = optional_vars_.contains(name);
        st.was_fallible = fallible_vars_.contains(name);
        st.was_reported_unhandled = reported_unhandled_vars_.contains(name);
        st.was_scalar_primitive = scalar_primitive_vars_.contains(name);
        st.was_ptr_or_smart = ptr_or_smart_vars_.contains(name);
        st.was_raw_or_smart_ptr = raw_or_smart_ptr_vars_.contains(name);
        if (auto it = ptr_null_state_.find(name); it != ptr_null_state_.end()) st.null_state = it->second;
        if (auto it = fixed_array_lens_.find(name); it != fixed_array_lens_.end()) st.fixed_array_len = it->second;
        st.was_map = map_vars_.contains(name);
        if (auto it = borrow_origin_.find(name); it != borrow_origin_.end()) st.borrow_origin = it->second;
        if (auto it = moved_at_line_.find(name); it != moved_at_line_.end()) st.moved_at_line = it->second;
        if (auto it = invalidated_loans_.find(name); it != invalidated_loans_.end()) st.invalidated_loan = it->second;
        top->emplace(name, std::move(st));
    }

    void bind_scoped_var(const std::string& name, bool is_mut) {
        if (name.empty() || name == "_") return;
        save_var_before_redecl(name);
        var_scope_depth_[name] = scope_depth_;
        local_defined_vars_.insert(name);
        moved_at_line_.erase(name);
        invalidated_loans_.erase(name);
        borrow_origin_.erase(name);
        if (is_mut) {
            imm_vars_.erase(name);
            mut_vars_.insert(name);
        } else {
            mut_vars_.erase(name);
            imm_vars_.insert(name);
        }
    }

    void bind_match_arm_vars(const MatchArm& arm, const std::string& target_t = "") {
        for (const auto& b : arm.bindings) {
            bind_scoped_var(b, false);
        }
        if (!arm.bindings.empty()) {
            if (arm.is_some || arm.variant_name == "Some") {
                if (target_t.ends_with('?')) {
                    var_types_[arm.bindings[0]] = target_t.substr(0, target_t.size() - 1);
                }
            } else if (arm.is_ok || arm.variant_name == "Ok") {
                if (target_t.ends_with('!')) {
                    var_types_[arm.bindings[0]] = target_t.substr(0, target_t.size() - 1);
                }
            } else if (arm.is_err || arm.variant_name == "Err") {
                var_types_[arm.bindings[0]] = "Error";
            }
        }
        if (arm.is_slice_pattern) {
            for (const auto& h : arm.slice_head) bind_scoped_var(h, false);
            if (!arm.slice_rest.empty()) bind_scoped_var(arm.slice_rest, false);
            for (const auto& t : arm.slice_tail) bind_scoped_var(t, false);
        }
    }

    [[nodiscard]] auto check_block_stmts(const std::vector<StmtPtr>& stmts, bool tail_is_value = false) -> std::expected<void, std::string> {
        for (std::size_t i = 0; i < stmts.size(); ++i) {
            if (tail_is_value && i + 1 == stmts.size() && stmts[i]) {
                if (const auto* es = std::get_if<ExprStmt>(&stmts[i]->node)) {
                    if (auto r = check_expr(es->expr); !r) return r;
                    continue;
                }
            }
            if (auto r = check_stmt(stmts[i]); !r) return r;
        }
        return {};
    }

    [[nodiscard]] auto check_stmt(const StmtPtr& s) -> std::expected<void, std::string> {
        if (!s) return {};
        if (std::holds_alternative<CppBlockStmt>(s->node)) {
            seen_cpp_block_ = true;
            // CF-124 / F9: Foreign C++ blocks can arbitrarily mutate pointees and alias memory.
            // Invalidate all active loans and demote NonNull facts to MaybeNull.
            for (const auto& [borrower, orig] : borrow_origin_) {
                invalidated_loans_[borrower] = InvalidatedLoan{"loan invalidated by foreign cpp block", s ? s->line : 1};
            }
            for (auto& [var, state] : ptr_null_state_) {
                if (state == NullState::NonNull) {
                    state = NullState::MaybeNull;
                }
            }
            return {};
        }
        if (const auto* vd = std::get_if<VarDeclStmt>(&s->node)) {
            return check_var_decl_stmt(*vd);
        }
        if (const auto* dvs = std::get_if<DestructureVarDeclStmt>(&s->node)) {
            return check_destructure_var_decl_stmt(*dvs);
        }
        if (const auto* as = std::get_if<AssignStmt>(&s->node)) {
            return check_assign_stmt(*as);
        }
        if (const auto* das = std::get_if<DestructureAssignStmt>(&s->node)) {
            return check_destructure_assign_stmt(*das);
        }
        if (const auto* df = std::get_if<DeferStmt>(&s->node)) {
            return check_defer_stmt(*df);
        }
        if (const auto* rt = std::get_if<ReturnStmt>(&s->node)) {
            return check_return_stmt(*rt);
        }
        if (const auto* bs = std::get_if<BreakStmt>(&s->node)) {
            if (loop_depth_ == 0) {
                return emit_error(diag_at(bs->line, 0, "cfire/BREAK-OUTSIDE-LOOP", std::format("'break' cannot be used outside of a 'for' loop")));
            }
            return {};
        }
        if (const auto* cs = std::get_if<ContinueStmt>(&s->node)) {
            if (loop_depth_ == 0) {
                return emit_error(diag_at(cs->line, 0, "cfire/BREAK-OUTSIDE-LOOP", std::format("'continue' cannot be used outside of a 'for' loop")));
            }
            return {};
        }
        if (const auto* ifs = std::get_if<IfStmt>(&s->node)) {
            return check_if_stmt(*ifs);
        }
        if (const auto* fs = std::get_if<ForStmt>(&s->node)) {
            return check_for_stmt(*fs);
        }
        if (const auto* ms = std::get_if<MatchStmt>(&s->node)) {
            return check_match_common(ms->target, ms->is_move, ms->arms, ms->line, ms->col, false);
        }
        if (const auto* sc = std::get_if<AsyncScopeStmt>(&s->node)) {
            return check_async_scope_stmt(*sc);
        }
        if (const auto* es = std::get_if<ExprStmt>(&s->node)) {
            if (es->expr) {
                if (const auto* or_ex = std::get_if<OrExpr>(&es->expr->node); or_ex && !or_ex->handler_body.empty()) {
                    discarded_or_exprs_.insert(es->expr);
                }
            }
            std::size_t errors_before = errors_.size();
            if (auto r = check_expr(es->expr); !r) return r;
            if (errors_.size() == errors_before && es->expr && !is_effectful_stmt_expr(es->expr)) {
                auto [ln, col] = expr_start_loc(es->expr);
                if (ln <= 0) ln = s->line;
                if (const auto* bin = std::get_if<BinaryExpr>(&es->expr->node); bin && bin->op == "==") {
                    return emit_error(diag_at(ln, col, "cfire/NO-EFFECT-STMT", std::format("equality comparison '==' statement has no effect; did you mean '=' assignment?")));
                }
                return emit_error(diag_at(ln, col, "cfire/NO-EFFECT-STMT", std::format("expression statement has no effect; assign the result or call a function")));
            }
            return {};
        }
        return {};
    }

    [[nodiscard]] auto check_var_decl_stmt(const VarDeclStmt& vd) -> std::expected<void, std::string> {
        if (vd.is_mut && !vd.type && vd.init) {
            TypeId it = infer_expr_type_id_sema(vd.init);
            if (it == TypeId::Str() || it == type_id_of("&str")) cf_.owned_text_decls.insert(&vd);
        }
        if (vd.type) {
            if (auto r = check_type_ref_separators(*vd.type, vd.line, vd.col); !r) return r;
            if (!vd.type->is_slice && !vd.type->is_array && !vd.type->is_optional && !vd.type->is_ptr) {
                if (auto r = check_int_literal_fits(vd.init, vd.type->name, vd.line, vd.col); !r) return r;
            }
            if (auto iface = find_interface_in_slice(*vd.type, cf_.interfaces)) {
                if (auto r = emit_error(diag_at(vd.line, vd.col, "cfire/DYNAMIC-INTERFACE-UNSUPPORTED", std::format("dynamic slice or array of interface '{}' is unsupported; structural interfaces lower to C++ concepts and cannot be stored dynamically", *iface))); !r) return r;
            }
        }
        if (vd.init && (vd.name == "_" || (vd.type && vd.type->is_fallible))) {
            unwrapped_exprs_.insert(vd.init);
        }
        // Check initializer expression before binding the new variable so `x := x + 1` reads outer `x` first
        if (auto r = check_expr(vd.init); !r) return r;

        bool is_byte_slice = (vd.type && (vd.type->is_slice || vd.type->is_array) &&
                              (vd.type->name == "byte" || (vd.type->elem_type && vd.type->elem_type->name == "byte"))) ||
                             (!vd.type && is_byte_slice_expr(vd.init));
        if (is_byte_slice && vd.init) {
            if (const auto* arr_lit = std::get_if<ArrayLitExpr>(&vd.init->node)) {
                for (const auto& el : arr_lit->elements) {
                    if (is_non_byte_int_or_u8_expr(el)) {
                        if (auto r = emit_error(diag_at(el->line, el->col, "cfire/TYPE-MISMATCH", std::format("cannot use integer/u8 value in '[]byte' literal without explicit 'byte(...)' conversion"))); !r) return r;
                    }
                }
            }
        }

        // CF-116: Type compatibility check for declared type vs initializer
        if (vd.type && vd.init) {
            TypeId dest_t = type_id_of(*vd.type);
            TypeId src_t = infer_expr_type_id_sema(vd.init);
            // An array literal adapts to the declared element type, like a bare literal does (`xs: []i32 = [1, 2]`);
            // its elements are checked individually instead of as an inferred `[]i64`.
            if (std::holds_alternative<ArrayLitExpr>(vd.init->node)) src_t = TypeId::Unknown();
            if (dest_t.is_valid() && src_t.is_valid() && !dest_t.is_unknown() && !src_t.is_unknown()) {
                std::string dest_str = cf_.type_table.to_string(dest_t);
                std::string src_str = cf_.type_table.to_string(src_t);
                if (dest_str.find("cpp::") == std::string::npos &&
                    src_str.find("cpp::") == std::string::npos &&
                    dest_str.find("cpp.") == std::string::npos &&
                    src_str.find("cpp.") == std::string::npos &&
                    !cf_.type_table.is_assignable(dest_t, src_t)) {
                    if (!is_byte_slice) {
                        return emit_error(diag_at(vd.line, vd.col, "cfire/TYPE-MISMATCH", std::format("cannot initialize variable '{}' of type '{}' with expression of type '{}'{}", vd.name, dest_str, src_str, nominal_mismatch_hint_sema(dest_t, src_t))));
                    }
                }
            }
        }
        if (!vd.type && vd.name != "_" && vd.init) {
            TypeId src_t = infer_expr_type_id_sema(vd.init);
            if (src_t.is_unknown() && !std::holds_alternative<LiteralExpr>(vd.init->node)) {
                bool is_foreign = false;
                auto check_foreign_expr = [&](auto& self, const ExprPtr& ex) -> bool {
                    if (!ex) return false;
                    if (const auto* call = std::get_if<CallExpr>(&ex->node)) {
                        if (const auto* mem = std::get_if<MemberExpr>(&call->callee->node)) {
                            if (const auto* oid = mem->object ? std::get_if<IdentExpr>(&mem->object->node) : nullptr) {
                                if (oid->name == "cpp" || is_cpp_qualifier_sema(oid->name) || is_vendor_alias_sema(oid->name)) return true;
                            }
                        } else if (const auto* cid = std::get_if<IdentExpr>(&call->callee->node)) {
                            if (cid->name.starts_with("cpp.") || is_vendor_alias_sema(cid->name) || is_cpp_qualifier_sema(cid->name)) return true;
                        }
                    } else if (const auto* slit = std::get_if<StructLitExpr>(&ex->node)) {
                        auto dot = slit->type_name.find('.');
                        if (dot != std::string::npos) {
                            std::string qual = slit->type_name.substr(0, dot);
                            if (qual == "cpp" || is_cpp_qualifier_sema(qual) || is_vendor_alias_sema(qual)) return true;
                        }
                    } else if (const auto* mem = std::get_if<MemberExpr>(&ex->node)) {
                        if (const auto* oid = mem->object ? std::get_if<IdentExpr>(&mem->object->node) : nullptr) {
                            if (oid->name == "cpp" || is_cpp_qualifier_sema(oid->name) || is_vendor_alias_sema(oid->name) || foreign_vars_.contains(oid->name)) return true;
                        }
                    } else if (const auto* id = std::get_if<IdentExpr>(&ex->node)) {
                        if (foreign_vars_.contains(id->name)) return true;
                    } else if (const auto* bin = std::get_if<BinaryExpr>(&ex->node)) {
                        if (self(self, bin->left) || self(self, bin->right)) return true;
                    } else if (const auto* un = std::get_if<UnaryExpr>(&ex->node)) {
                        if (self(self, un->operand)) return true;
                    } else if (const auto* or_ex = std::get_if<OrExpr>(&ex->node)) {
                        if (self(self, or_ex->inner)) return true;
                    }
                    return false;
                };
                is_foreign = check_foreign_expr(check_foreign_expr, vd.init);
                if (is_foreign) {
                    foreign_vars_.insert(vd.name);
                } else {
                    emit_warning(diag_at(vd.line, vd.col, "cfire/UNKNOWN-TYPE", std::format("could not infer concrete type for variable '{}'; expression has unknown type", vd.name)));
                }
            }
        }

        if (auto r = check_view_of_temporary(vd.init, std::format("'{}'", vd.name), vd.line, vd.col); !r) return r;
        if (vd.init && is_constant_string_expr(vd.init)) const_string_vars_.insert(vd.name);
        else const_string_vars_.erase(vd.name);
        disjoint_slot_ptrs_.erase(vd.name);
        if (vd.init) {
            if (const auto* un = std::get_if<UnaryExpr>(&vd.init->node); un && (un->op == "&mut" || un->op == "&")) {
                if (const auto* ix = un->operand ? std::get_if<IndexExpr>(&un->operand->node) : nullptr) {
                    if (const auto* iv = ix->index ? std::get_if<IdentExpr>(&ix->index->node) : nullptr;
                        iv && active_loop_index_vars_.contains(iv->name)) {
                        disjoint_slot_ptrs_.insert(vd.name);
                    }
                }
            }
        }
        std::optional<std::string> src = extract_borrow_source(vd.init);
        if (!src) {
            if (auto owners = extract_borrow_sources(vd.init); !owners.empty()) src = owners.front();
        }
        record_borrow_origins(vd.name, vd.init);
        std::string inferred_t;
        if (vd.type && !vd.type->smart_wrap.empty() && vd.type->elem_type && !vd.type->elem_type->name.empty()) {
            inferred_t = vd.type->elem_type->name;
        } else if (vd.type) {
            inferred_t = format_type_ref_sema(*vd.type);
        } else {
            inferred_t = type_name_of(vd.init);
        }
        auto dot = inferred_t.find('.');
        if (dot != std::string::npos) {
            std::string qual = inferred_t.substr(0, dot);
            if (qual == "cpp" || is_cpp_qualifier_sema(qual) || is_vendor_alias_sema(qual)) {
                foreign_vars_.insert(vd.name);
            }
        }
        bool is_opt_var = (vd.type && vd.type->is_optional) || (!vd.type && expr_returns_optional_sema(vd.init));
        bool is_fallible_var = (vd.type && vd.type->is_fallible);
        bool init_was_unhandled_fallible = (!vd.type && vd.name != "_" && expr_returns_fallible_sema(vd.init));
        bool is_smart = (vd.type && !vd.type->smart_wrap.empty());
        if (!is_smart && vd.init) {
            const Expr* cur_init = vd.init;
            if (const auto* tr = std::get_if<TryExpr>(&cur_init->node)) cur_init = tr->inner;
            else if (const auto* or_ex = std::get_if<OrExpr>(&cur_init->node)) cur_init = or_ex->inner;
            if (cur_init) {
                if (const auto* id = std::get_if<IdentExpr>(&cur_init->node)) {
                    if (smart_ptr_vars_.contains(id->name)) is_smart = true;
                } else if (const auto* call_ex = std::get_if<CallExpr>(&cur_init->node)) {
                    if (const auto* fmem = std::get_if<MemberExpr>(&call_ex->callee->node);
                        fmem && fmem->field == "from_raw" && !own_type_prefix(fmem->object).empty()) {
                        is_smart = true;  // CF-181: `own[T].from_raw(p)`
                    }
                    if (const auto* cid = std::get_if<IdentExpr>(&call_ex->callee->node)) {
                        if (cid->name == "make" && call_ex->type_args.size() == 1 && !call_ex->type_args[0].smart_wrap.empty()) {
                            is_smart = true;
                        } else if (auto fit = cf_.fn_sigs.find(cid->name); fit != cf_.fn_sigs.end() && !fit->second.return_type.smart_wrap.empty()) {
                            is_smart = true;
                        }
                    }
                }
            }
        }
        bool is_raw_or_smart_ptr = (vd.type && (vd.type->is_ptr || vd.type->is_mut_ptr || !vd.type->smart_wrap.empty())) ||
                                   is_ptr_or_smart_init(vd.init);
        bool is_ptr_or_smart = is_raw_or_smart_ptr || is_opt_var;
        bool is_scalar_prim = false;
        if (vd.type) {
            is_scalar_prim = is_scalar_primitive_typeref(*vd.type);
        } else if (!is_opt_var && !is_fallible_var && !init_was_unhandled_fallible && !is_raw_or_smart_ptr && !is_byte_slice && vd.init) {
            if (const auto* lit = std::get_if<LiteralExpr>(&vd.init->node)) {
                is_scalar_prim = (lit->kind == TokenKind::Int || lit->kind == TokenKind::Float || lit->kind == TokenKind::Char ||
                                  lit->kind == TokenKind::KwTrue || lit->kind == TokenKind::KwFalse);
            } else if (std::holds_alternative<BinaryExpr>(vd.init->node) || std::holds_alternative<UnaryExpr>(vd.init->node)) {
                is_scalar_prim = is_scalar_primitive_type_name(inferred_t);
            } else {
                const Expr* cur_init = vd.init;
                if (const auto* tr = std::get_if<TryExpr>(&cur_init->node)) cur_init = tr->inner;
                else if (const auto* or_ex = std::get_if<OrExpr>(&cur_init->node)) cur_init = or_ex->inner;
                if (cur_init) {
                    if (const auto* call_ex = std::get_if<CallExpr>(&cur_init->node)) {
                        if (const auto* cid = std::get_if<IdentExpr>(&call_ex->callee->node)) {
                            if (auto fit = cf_.fn_sigs.find(cid->name); fit != cf_.fn_sigs.end()) {
                                TypeRef rt = fit->second.return_type;
                                rt.is_fallible = false;
                                rt.is_optional = false;
                                is_scalar_prim = is_scalar_primitive_typeref(rt);
                            } else if (is_scalar_primitive_type_name(cid->name)) {
                                is_scalar_prim = true;
                            }
                        } else if (const auto* mem = std::get_if<MemberExpr>(&call_ex->callee->node)) {
                            if (const auto* recv_id = std::get_if<IdentExpr>(&mem->object->node)) {
                                if (const auto* std_sig = StdlibCatalogIndex::instance().find_ns_fn(recv_id->name, mem->field)) {
                                    is_scalar_prim = is_scalar_primitive_type_name(std_sig->unwrapped_return_type);
                                }
                            }
                        }
                    }
                }
            }
        }
        auto init_ns = infer_expr_null_state(vd.init);

        bind_scoped_var(vd.name, vd.is_mut);
        map_vars_.erase(vd.name);
        fixed_array_lens_.erase(vd.name);
        if (is_byte_slice) byte_slice_vars_.insert(vd.name);
        else byte_slice_vars_.erase(vd.name);
        if (is_smart) smart_ptr_vars_.insert(vd.name);
        else smart_ptr_vars_.erase(vd.name);
        {
            // CF-181: remember which smart pointers are `shared[T]` (no `release`).
            bool is_shared = vd.type && vd.type->smart_wrap == "shared";
            if (!is_shared && vd.init) {
                if (const auto* call_ex = std::get_if<CallExpr>(&vd.init->node)) {
                    if (const auto* cid = std::get_if<IdentExpr>(&call_ex->callee->node);
                        cid && cid->name == "make" && call_ex->type_args.size() == 1 && call_ex->type_args[0].smart_wrap == "shared") {
                        is_shared = true;
                    }
                } else if (const auto* id = std::get_if<IdentExpr>(&vd.init->node)) {
                    is_shared = shared_ptr_var_names_.contains(id->name);
                }
            }
            if (is_smart && is_shared) shared_ptr_var_names_.insert(vd.name);
            else shared_ptr_var_names_.erase(vd.name);
        }
        if (is_fallible_var) fallible_vars_.insert(vd.name);
        else fallible_vars_.erase(vd.name);
        if (init_was_unhandled_fallible) reported_unhandled_vars_.insert(vd.name);
        else reported_unhandled_vars_.erase(vd.name);
        if (is_raw_or_smart_ptr) raw_or_smart_ptr_vars_.insert(vd.name);
        else raw_or_smart_ptr_vars_.erase(vd.name);
        if (is_scalar_prim) scalar_primitive_vars_.insert(vd.name);
        else scalar_primitive_vars_.erase(vd.name);
        if (vd.type && vd.type->is_fn && vd.type->fn_ret) {
            if (vd.type->fn_ret->is_fallible) fn_ret_fallible_vars_.insert(vd.name);
            else fn_ret_fallible_vars_.erase(vd.name);
            if (vd.type->fn_ret->is_optional) fn_ret_optional_vars_.insert(vd.name);
            else fn_ret_optional_vars_.erase(vd.name);
        } else {
            fn_ret_fallible_vars_.erase(vd.name);
            fn_ret_optional_vars_.erase(vd.name);
        }

        if (vd.type) {
            if (vd.type->is_map) {
                map_vars_.insert(vd.name);
            }
            if (vd.type->is_array && !vd.type->array_len.empty()) {
                if (auto n = cfire::detail::parse_int<std::size_t>(vd.type->array_len)) {
                    fixed_array_lens_[vd.name] = *n;
                }
            }
        } else if (!vd.is_mut && vd.init) {
            if (const auto* arr_lit = std::get_if<ArrayLitExpr>(&vd.init->node)) {
                fixed_array_lens_[vd.name] = arr_lit->elements.size();
            }
        }
        if (vd.init) {
            if (const auto* call_ex = std::get_if<CallExpr>(&vd.init->node)) {
                if (const auto* cid = std::get_if<IdentExpr>(&call_ex->callee->node);
                    cid && cid->name == "make" && call_ex->type_args.size() == 1 && call_ex->type_args[0].is_map) {
                    map_vars_.insert(vd.name);
                }
            }
        }

        if (src) {
            borrow_origin_[vd.name] = *src;
        }
        if (!inferred_t.empty()) {
            var_types_[vd.name] = std::move(inferred_t);
        } else {
            var_types_.erase(vd.name);
        }
        if (is_opt_var) {
            optional_vars_.insert(vd.name);
        } else {
            optional_vars_.erase(vd.name);
        }
        if (is_ptr_or_smart) {
            ptr_or_smart_vars_.insert(vd.name);
        } else {
            ptr_or_smart_vars_.erase(vd.name);
        }
        if (init_ns) {
            ptr_null_state_[vd.name] = *init_ns;
        } else if (!vd.init && is_ptr_or_smart) {
            ptr_null_state_[vd.name] = NullState::Null;
        } else if (is_opt_var) {
            if (vd.init && (std::holds_alternative<StructLitExpr>(vd.init->node) ||
                            (std::holds_alternative<LiteralExpr>(vd.init->node) &&
                             std::get<LiteralExpr>(vd.init->node).kind != TokenKind::KwNone))) {
                ptr_null_state_[vd.name] = NullState::NonNull;
            } else {
                ptr_null_state_[vd.name] = NullState::MaybeNull;
            }
        } else if (vd.type && (vd.type->is_ptr || vd.type->is_mut_ptr || !vd.type->smart_wrap.empty())) {
            ptr_null_state_[vd.name] = vd.type->is_optional ? NullState::MaybeNull : NullState::NonNull;
        } else {
            ptr_null_state_.erase(vd.name);
        }
        return {};
    }

    [[nodiscard]] auto check_destructure_var_decl_stmt(const DestructureVarDeclStmt& dvs) -> std::expected<void, std::string> {
        if (auto r = check_expr(dvs.init); !r) return r;
        std::string elem_type;
        if (dvs.init) {
            const Expr* inner = dvs.init;
            if (const auto* tr = std::get_if<TryExpr>(&inner->node)) inner = tr->inner;
            else if (const auto* orex = std::get_if<OrExpr>(&inner->node)) inner = orex->inner;
            if (inner) {
                if (const auto* call = std::get_if<CallExpr>(&inner->node)) {
                    if (const auto* mem = std::get_if<MemberExpr>(&call->callee->node)) {
                        if (const auto* obj_id = std::get_if<IdentExpr>(&mem->object->node)) {
                            if (obj_id->name == "os" && mem->field == "pipe") elem_type = "File";
                            else if (obj_id->name == "net" && mem->field == "socketpair") elem_type = "TcpConn";
                            else if (obj_id->name == "websocket" && mem->field == "pair") elem_type = "websocket.Conn";
                        }
                    }
                }
            }
        }
        for (const auto& [name, is_mut] : dvs.bindings) {
            bind_scoped_var(name, is_mut);
            if (!elem_type.empty()) {
                var_types_[name] = elem_type;
            }
        }
        return {};
    }

    [[nodiscard]] auto get_root_variable_name(const ExprPtr& e) const -> std::string {
        if (!e) return "";
        if (const auto* id = std::get_if<IdentExpr>(&e->node)) return id->name;
        if (const auto* mem = std::get_if<MemberExpr>(&e->node)) return get_root_variable_name(mem->object);
        if (const auto* idx = std::get_if<IndexExpr>(&e->node)) return get_root_variable_name(idx->object);
        return "";
    }

    // CF-164 (G1): inside a `scope.spawn` closure, writing through a captured outer pointer (`*p = v`,
    // `p.field = v` via auto-deref) races with every other task holding the same pointer. The one provably
    // disjoint form is allowed: a pointer created inside a `for` body as `&mut xs[i]` from that loop's own
    // index variable, so each task writes a different slot (examples/04-async-workers).
    [[nodiscard]] auto check_spawn_pointer_write(const ExprPtr& target, int line, bool* reported = nullptr) -> std::expected<void, std::string> {
        const Expr* e = target;
        bool through_deref = false;
        while (e) {
            if (const auto* un = std::get_if<UnaryExpr>(&e->node); un && un->op == "*") { through_deref = true; e = un->operand; continue; }
            if (const auto* m = std::get_if<MemberExpr>(&e->node)) { e = m->object; continue; }
            if (const auto* ix = std::get_if<IndexExpr>(&e->node)) { e = ix->object; continue; }
            break;
        }
        const auto* id = e ? std::get_if<IdentExpr>(&e->node) : nullptr;
        if (!id) return {};
        auto sit = var_scope_depth_.find(id->name);
        if (sit == var_scope_depth_.end() || sit->second > spawn_entry_scope_) return {};  // task-local
        std::string ty;
        if (auto tit = var_types_.find(id->name); tit != var_types_.end()) ty = tit->second;
        bool is_pointer = raw_or_smart_ptr_vars_.contains(id->name) || ptr_or_smart_vars_.contains(id->name) ||
                          ty.starts_with("*") || ty.starts_with("&mut") || ty.starts_with("shared[") || ty.starts_with("own[");
        if (!is_pointer || (!through_deref && !ty.starts_with("shared[") && !ty.starts_with("own[") &&
                            !raw_or_smart_ptr_vars_.contains(id->name) && !ptr_or_smart_vars_.contains(id->name))) {
            return {};
        }
        if (disjoint_slot_ptrs_.contains(id->name)) return {};
        if (reported) *reported = true;
        return emit_error(diag_at(line, 0, "cfire/SPAWN-SHARED-MUT", std::format("spawned task writes through captured pointer '{}', which other tasks can also reach (data race); protect the target with 'sync.Mutex[T]'/'sync.Atomic[T]', send results over a 'sync.Channel[T]', or give each task its own slot with 'slot := &mut xs[i]' inside the spawning loop", id->name)));
    }

    [[nodiscard]] auto is_concurrency_safe_type(const std::string& ty) const -> bool {
        if (ty.starts_with("sync.Mutex") || ty.starts_with("cfire::sync::Mutex")) return true;
        if (ty.starts_with("sync.Atomic") || ty.starts_with("cfire::sync::Atomic")) return true;
        if (ty.starts_with("sync.Channel") || ty.starts_with("cfire::sync::Channel")) return true;
        if (ty.starts_with("sync.WaitGroup") || ty.starts_with("sync.RwLock")) return true;
        // shared[T] shares ownership, not access: its pointee is unsynchronized (CF-164).
        return false;
    }

    [[nodiscard]] auto check_assign_stmt(const AssignStmt& as) -> std::expected<void, std::string> {
        if (as.value && as.target) {
            if (const auto* id = std::get_if<IdentExpr>(&as.target->node)) {
                if (id->name == "_" || fallible_vars_.contains(id->name)) {
                    unwrapped_exprs_.insert(as.value);
                }
            }
        }
        if (auto r = check_expr(as.value); !r) return r;
        if (as.op == "<<=" || as.op == ">>=") {
            if (auto r = check_const_shift_count(as.target, as.value, as.line, 1); !r) return r;
        }
        if ((as.op == "/=" || as.op == "%=") && is_const_zero_expr(as.value)) {
            if (auto r = emit_error(diag_at(as.line, 0, "cfire/DIVIDE-BY-ZERO", std::format("division or modulo assignment by constant zero"))); !r) return r;
        }
        if (as.op != "=") {
            if (auto r = check_expr(as.target); !r) return r;
        }
        if (const auto* tid = as.target ? std::get_if<IdentExpr>(&as.target->node) : nullptr) {
            bool stays_constant = as.op == "=" ? is_constant_string_expr(as.value)
                                : (as.op == "+=" && const_string_vars_.contains(tid->name) && is_constant_string_expr(as.value));
            if (stays_constant) const_string_vars_.insert(tid->name);
            else const_string_vars_.erase(tid->name);
        }
        bool spawn_ptr_reported = false;
        if (in_spawn_closure_depth_ > 0 && as.target) {
            if (auto r = check_spawn_pointer_write(as.target, as.line, &spawn_ptr_reported); !r) return r;
        }
        if (in_escaping_closure_depth_ > 0 && as.target && !spawn_ptr_reported) {
            std::string root = get_root_variable_name(as.target);
            if (!root.empty() && root != "_") {
                auto sit = var_scope_depth_.find(root);
                if (sit != var_scope_depth_.end() && sit->second <= escaping_closure_entry_scope_) {
                    std::string r_ty;
                    if (auto tit = var_types_.find(root); tit != var_types_.end()) r_ty = tit->second;
                    if (!is_concurrency_safe_type(r_ty)) {
                        return emit_error(diag_at(as.line, 0, "cfire/ESCAPING-CAPTURE-MUT", std::format("cannot mutate captured outer variable '{}' inside escaping closure; wrap shared mutable state in 'sync.Mutex[T]', 'sync.Atomic[T]', or communicate via 'sync.Channel[T]'", root)));
                    }
                }
            }
        }
        if (const auto* id = std::get_if<IdentExpr>(&as.target->node)) {
            if (id->name == "_") return {};
            if (byte_slice_vars_.contains(id->name) && as.value) {
                if (const auto* arr_lit = std::get_if<ArrayLitExpr>(&as.value->node)) {
                    for (const auto& el : arr_lit->elements) {
                        if (is_non_byte_int_or_u8_expr(el)) {
                            if (auto r = emit_error(diag_at(el->line, el->col, "cfire/TYPE-MISMATCH", std::format("cannot use integer/u8 value in '[]byte' literal without explicit 'byte(...)' conversion"))); !r) return r;
                        }
                    }
                }
            }
            if (imm_vars_.contains(id->name)) {
                if (auto r = emit_error(diag_at(as.line, 1, "cfire/ASSIGN-IMMUTABLE", std::format("cannot assign to immutable variable '{}'", id->name))); !r) return r;
            }
            if (auto r = check_not_iterated_collection(id->name, as.line, 1); !r) return r;
            moved_at_line_.erase(id->name);
            if (as.op == "=") {
                invalidate_loans_for_owner(
                    id->name,
                    std::format("underlying owner '{}' was reassigned at line {}", id->name, as.line),
                    as.line);
                if (auto r = check_view_of_temporary(as.value, std::format("'{}'", id->name), as.line, 1); !r) return r;
                std::optional<std::string> src = extract_borrow_source(as.value);
                if (!src) {
                    if (auto owners = extract_borrow_sources(as.value); !owners.empty()) src = owners.front();
                }
                record_borrow_origins(id->name, as.value);
                if (src) {
                    auto d_target = var_scope_depth_.find(id->name);
                    auto d_src = var_scope_depth_.find(*src);
                    if (d_target != var_scope_depth_.end() && d_src != var_scope_depth_.end()) {
                        if (d_src->second > d_target->second) {
                            if (auto r = emit_error(diag_at(as.line, 0, "cfire/DANGLING-POINTER", std::format("pointer/view '{}' cannot borrow '{}' from an inner scope", id->name, *src))); !r) return r;
                        }
                    }
                    borrow_origin_[id->name] = *src;
                    invalidated_loans_.erase(id->name);
                } else {
                    borrow_origin_.erase(id->name);
                    invalidated_loans_.erase(id->name);
                }
                if (is_ptr_or_smart_init(as.value)) {
                    ptr_or_smart_vars_.insert(id->name);
                }
                if (auto ns = infer_expr_null_state(as.value)) {
                    ptr_null_state_[id->name] = *ns;
                } else if (ptr_null_state_.contains(id->name)) {
                    ptr_null_state_[id->name] = NullState::NonNull;
                }
            }
        } else {
            if (const auto* idx = std::get_if<IndexExpr>(&as.target->node)) {
                if (is_byte_slice_expr(idx->object) && is_non_byte_int_or_u8_expr(as.value)) {
                    if (auto r = emit_error(diag_at(as.line, 0, "cfire/TYPE-MISMATCH", std::format("cannot assign integer/u8 value to '[]byte' element without explicit 'byte(...)' conversion"))); !r) return r;
                }
            }
            if (as.op == "=") {
                if (auto r = check_expr(as.target); !r) return r;
            }
        }
        // CF-116: Type compatibility check for assignment
        if (as.target && as.value && as.op == "=") {
            TypeId dest_t = infer_expr_type_id_sema(as.target);
            TypeId src_t = infer_expr_type_id_sema(as.value);
            if (std::holds_alternative<ArrayLitExpr>(as.value->node)) src_t = TypeId::Unknown();  // literal adapts (CF-148)
            if (dest_t.is_valid() && src_t.is_valid() && !dest_t.is_unknown() && !src_t.is_unknown()) {
                std::string dest_str = cf_.type_table.to_string(dest_t);
                std::string src_str = cf_.type_table.to_string(src_t);
                if (dest_str.find("cpp::") == std::string::npos &&
                    src_str.find("cpp::") == std::string::npos &&
                    dest_str.find("cpp.") == std::string::npos &&
                    src_str.find("cpp.") == std::string::npos &&
                    !cf_.type_table.is_assignable(dest_t, src_t)) {
                    return emit_error(diag_at(as.line, 0, "cfire/TYPE-MISMATCH", std::format("cannot assign expression of type '{}' to target of type '{}'{}", src_str, dest_str, nominal_mismatch_hint_sema(dest_t, src_t))));
                }
            }
        }
        return {};
    }

    [[nodiscard]] auto check_destructure_assign_stmt(const DestructureAssignStmt& das) -> std::expected<void, std::string> {
        if (auto r = check_expr(das.value); !r) return r;
        for (const auto& targ : das.targets) {
            if (const auto* id = std::get_if<IdentExpr>(&targ->node)) {
                if (imm_vars_.contains(id->name)) {
                    if (auto r = emit_error(diag_at(das.line, 1, "cfire/ASSIGN-IMMUTABLE", std::format("cannot assign to immutable variable '{}'", id->name))); !r) return r;
                }
                moved_at_line_.erase(id->name);
            } else {
                if (auto r = check_expr(targ); !r) return r;
            }
        }
        return {};
    }

    [[nodiscard]] auto check_defer_stmt(const DeferStmt& df) -> std::expected<void, std::string> {
        if (df.call) {
            unwrapped_exprs_.insert(df.call);
            return check_expr(df.call);
        }
        ++in_defer_depth_;
        int saved_loop_depth = loop_depth_;
        loop_depth_ = 0;
        auto defer_guard = cfire::scope_exit([this, saved_loop_depth] {
            --in_defer_depth_;
            loop_depth_ = saved_loop_depth;
        });
        ScopeGuard sg(*this);
        return check_block_stmts(df.body);
    }

    [[nodiscard]] auto check_return_stmt(const ReturnStmt& rt) -> std::expected<void, std::string> {
        if (in_defer_depth_ > 0) {
            if (auto r = emit_error(diag_at(rt.line, 0, "cfire/RETURN-IN-DEFER", std::format("cannot use 'return' inside a 'defer {{ ... }}' block"))); !r) return r;
        }
        if (rt.value) {
            if (auto* cl = std::get_if<ClosureExpr>(&rt.value->node)) {
                cl->is_escaping = true;
            }
        }
        if (rt.value && (fn_is_fallible_ || in_closure_depth_ > 0)) {
            unwrapped_exprs_.insert(rt.value);
        }
        if (auto r = check_expr(rt.value); !r) return r;
        if (rt.value) {
            bool is_direct = false;
            if (auto r = check_view_of_temporary(rt.value, "returned value", rt.line, 1); !r) return r;
            if (!extract_borrow_source(rt.value)) {
                // CF-144: a call returning a view of a local (e.g. `return head(local)`) dangles too.
                for (const auto& owner : extract_borrow_sources(rt.value)) {
                    if (local_defined_vars_.contains(owner) || move_params_.contains(owner)) {
                        if (auto r = emit_error(diag_at(rt.line, 0, "cfire/DANGLING-RETURN", std::format("cannot return pointer/view borrowing local variable '{}'", owner))); !r) return r;
                        break;
                    }
                }
            }
            if (auto src = extract_borrow_source(rt.value, &is_direct)) {
                if (local_defined_vars_.contains(*src) || move_params_.contains(*src)) {
                    if (auto r = emit_error(diag_at(rt.line, 0, "cfire/DANGLING-RETURN", std::format("cannot return pointer/view borrowing local variable '{}'", *src))); !r) return r;
                }
            }
        }
        // CF-116: Type compatibility check for return statement
        if (rt.value && !fn_ret_str_.empty() && fn_ret_str_ != "void" && in_closure_depth_ == 0) {
            TypeId ret_t = type_id_of(fn_ret_str_);
            TypeId val_t = infer_expr_type_id_sema(rt.value);
            if (std::holds_alternative<ArrayLitExpr>(rt.value->node)) val_t = TypeId::Unknown();  // literal adapts (CF-148)
            if (ret_t.is_valid() && val_t.is_valid() && !ret_t.is_unknown() && !val_t.is_unknown()) {
                std::string ret_str = cf_.type_table.to_string(ret_t);
                std::string val_str = cf_.type_table.to_string(val_t);
                if (fn_ret_str_.find("cpp::") == std::string::npos &&
                    ret_str.find("cpp::") == std::string::npos &&
                    val_str.find("cpp::") == std::string::npos &&
                    fn_ret_str_.find("cpp.") == std::string::npos &&
                    ret_str.find("cpp.") == std::string::npos &&
                    val_str.find("cpp.") == std::string::npos) {
                    bool ok = cf_.type_table.is_assignable(ret_t, val_t);
                    if (!ok && fn_is_fallible_) {
                        ok = cf_.type_table.is_assignable(cf_.type_table.unwrap_fallible(ret_t), val_t);
                    }
                    if (!ok) {
                        return emit_error(diag_at(rt.line, 0, "cfire/TYPE-MISMATCH", std::format("return type mismatch: function '{}' expects '{}', got '{}'{}", fn_name_, ret_str, val_str, nominal_mismatch_hint_sema(ret_t, val_t))));
                    }
                }
            }
        }
        return {};
    }

    [[nodiscard]] auto check_if_stmt(const IfStmt& ifs) -> std::expected<void, std::string> {
        std::string unwrap_elem_ty;
        if (ifs.unwrap_init) {
            if (auto r = check_expr(ifs.unwrap_init); !r) return r;
            std::string init_ty = type_name_of(ifs.unwrap_init);
            if (init_ty.ends_with('?')) {
                unwrap_elem_ty = init_ty.substr(0, init_ty.size() - 1);
            } else if (init_ty.ends_with('!')) {
                unwrap_elem_ty = init_ty.substr(0, init_ty.size() - 1);
            } else if (init_ty.starts_with("std::optional<") && init_ty.ends_with('>')) {
                unwrap_elem_ty = init_ty.substr(14, init_ty.size() - 15);
            } else if (init_ty.starts_with("cfire::Result<") && init_ty.ends_with('>')) {
                unwrap_elem_ty = init_ty.substr(14, init_ty.size() - 15);
            } else {
                unwrap_elem_ty = "auto";
            }
        }
        if (ifs.cond) {
            ScopeGuard sg_cond(*this);
            if (ifs.unwrap_init && !ifs.unwrap_var.empty()) {
                bind_scoped_var(ifs.unwrap_var, ifs.unwrap_mut);
                var_types_[ifs.unwrap_var] = unwrap_elem_ty;
            }
            if (auto r = check_expr(ifs.cond); !r) return r;
            if (!ifs.unwrap_init) {
                if (auto r = check_bool_condition(ifs.cond, "'if' condition"); !r) return r;
            }
        }
        auto initial_moves = moved_at_line_;
        auto initial_loans = invalidated_loans_;
        auto initial_null = ptr_null_state_;
        auto nc = ifs.cond ? extract_null_check(ifs.cond) : std::nullopt;

        if (nc) {
            ptr_null_state_[nc->first] = nc->second ? NullState::NonNull : NullState::Null;
        }
        {
            ScopeGuard sg(*this);
            if (ifs.unwrap_init && !ifs.unwrap_var.empty()) {
                bind_scoped_var(ifs.unwrap_var, ifs.unwrap_mut);
                var_types_[ifs.unwrap_var] = unwrap_elem_ty;
            }
            if (auto r = check_block_stmts(ifs.then_body); !r) return r;
        }
        auto then_moves = moved_at_line_;
        auto then_loans = invalidated_loans_;
        auto then_null = ptr_null_state_;

        moved_at_line_ = initial_moves;
        invalidated_loans_ = initial_loans;
        ptr_null_state_ = initial_null;
        if (nc) {
            ptr_null_state_[nc->first] = nc->second ? NullState::Null : NullState::NonNull;
        }
        {
            ScopeGuard sg(*this);
            if (auto r = check_block_stmts(ifs.else_body); !r) return r;
        }
        auto else_moves = moved_at_line_;
        auto else_loans = invalidated_loans_;
        auto else_null = ptr_null_state_;

        bool then_div = block_diverges(ifs.then_body);
        bool else_div = !ifs.else_body.empty() && block_diverges(ifs.else_body);

        if (ifs.else_body.empty()) {
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
                        merged_null[var] = st_then;
                    } else {
                        merged_null[var] = NullState::MaybeNull;
                    }
                }
                ptr_null_state_ = std::move(merged_null);
            }
        }
        return {};
    }

    // CF-146: conditions and logical operands must be `bool`; no implicit truthiness for numbers, strings,
    // optionals or pointers. Only reported when the type is known, so inference gaps never produce errors.
    [[nodiscard]] auto check_bool_condition(const ExprPtr& e, std::string_view what) -> std::expected<void, std::string> {
        if (!e) return {};
        std::string fix;
        if (expr_returns_optional_sema(e)) {
            fix = "compare explicitly: 'x != none'";
        } else {
            TypeId t = infer_expr_type_id_sema(e);
            if (t.is_unknown() || t == TypeId::Bool()) return {};
            const auto& d = cf_.type_table.get(t);
            if (cf_.type_table.is_integer(t) || cf_.type_table.is_float(t)) fix = "compare explicitly: 'x != 0'";
            else if (t == TypeId::Str()) fix = "compare explicitly: 's.len() > 0'";
            else if (d.kind == TypeKind::RawPtr || d.kind == TypeKind::MutPtr || d.kind == TypeKind::Optional) fix = "compare explicitly: 'p != none'";
            else if (d.kind == TypeKind::Slice || d.kind == TypeKind::Array || d.kind == TypeKind::Map) fix = "compare explicitly: 'xs.len() > 0'";
            else return {};
        }
        return emit_error(diag_at(e->line, e->col, "cfire/NON-BOOL-CONDITION", std::format("{} must be 'bool', got '{}'; {}", what, type_name_of(e).empty() ? "optional" : type_name_of(e), fix)));
    }

    [[nodiscard]] auto check_for_stmt(const ForStmt& fs) -> std::expected<void, std::string> {
        if (fs.is_unwrap) {
            if (auto r = check_expr(fs.iter_expr); !r) return r;
            std::string init_ty = type_name_of(fs.iter_expr);
            std::string elem_ty;
            if (init_ty.ends_with('?')) elem_ty = init_ty.substr(0, init_ty.size() - 1);
            else if (init_ty.ends_with('!')) elem_ty = init_ty.substr(0, init_ty.size() - 1);
            else if (init_ty.starts_with("sync.Channel[") && init_ty.ends_with(']')) elem_ty = init_ty.substr(13, init_ty.size() - 14);
            else elem_ty = "auto";

            ++loop_depth_;
            auto loop_guard = cfire::scope_exit([this] { --loop_depth_; });
            ScopeGuard sg(*this);
            bind_scoped_var(fs.item_var, fs.unwrap_mut);
            var_types_[fs.item_var] = elem_ty;
            return check_block_stmts(fs.body);
        }
        if (fs.item_var.empty()) {
            ++in_invalid_unwrap_context_;
            auto guard = cfire::scope_exit([this] { --in_invalid_unwrap_context_; });
            if (auto r = check_expr(fs.iter_expr); !r) return r;
            if (auto r = check_bool_condition(fs.iter_expr, "'for' condition"); !r) return r;
        } else {
            if (auto r = check_expr(fs.iter_expr); !r) return r;
            if (fs.range_end) {
                if (auto r = check_expr(fs.range_end); !r) return r;
            }
        }
        auto pre_loop_moves = moved_at_line_;
        auto pre_loop_loans = invalidated_loans_;
        std::string locked_col;
        if (!fs.item_var.empty() && !fs.range_end) {
            if (auto pi = extract_path_info(fs.iter_expr); pi && pi->path == pi->root) {
                if (!active_loop_collections_.contains(pi->root)) {
                    locked_col = pi->root;
                    active_loop_collections_.insert(locked_col);
                }
            }
        }
        auto unlock_guard = cfire::scope_exit([this, &locked_col] {
            if (!locked_col.empty()) active_loop_collections_.erase(locked_col);
        });
        std::string loop_index = fs.range_end ? fs.item_var : fs.index_var;
        if (!loop_index.empty()) ++active_loop_index_vars_[loop_index];
        auto index_guard = cfire::scope_exit([this, loop_index] {
            if (!loop_index.empty() && --active_loop_index_vars_[loop_index] <= 0) active_loop_index_vars_.erase(loop_index);
        });
        {
            ++loop_depth_;
            auto loop_guard = cfire::scope_exit([this] { --loop_depth_; });
            ScopeGuard sg(*this);
            if (fs.range_end) {
                bind_scoped_var(fs.item_var, false);
                var_types_[fs.item_var] = "i64";
            } else if (!fs.item_var.empty()) {
                std::string col_ty = type_name_of(fs.iter_expr);
                std::string idx_ty = "usize";
                std::string item_ty = "auto";
                if (fs.is_unwrap) {
                    if (col_ty.ends_with('?')) {
                        item_ty = col_ty.substr(0, col_ty.size() - 1);
                    } else if (col_ty.ends_with('!')) {
                        item_ty = col_ty.substr(0, col_ty.size() - 1);
                    } else if (col_ty.starts_with("std::optional<") && col_ty.ends_with('>')) {
                        item_ty = col_ty.substr(14, col_ty.size() - 15);
                    } else if (col_ty.starts_with("cfire::Result<") && col_ty.ends_with('>')) {
                        item_ty = col_ty.substr(14, col_ty.size() - 15);
                    }
                } else if (col_ty == "str" || col_ty == "string") {
                    idx_ty = "usize";
                    item_ty = "u8";
                } else if (col_ty.starts_with("map[") && col_ty.find(']') != std::string::npos) {
                    auto close_b = col_ty.find(']');
                    idx_ty = col_ty.substr(4, close_b - 4);
                    item_ty = col_ty.substr(close_b + 1);
                } else if (col_ty.starts_with("collections.BTreeMap[") && col_ty.ends_with(']')) {
                    auto inner = col_ty.substr(21, col_ty.size() - 22);
                    auto comma = inner.find(',');
                    if (comma != std::string::npos) {
                        idx_ty = trim_str(inner.substr(0, comma));
                        item_ty = trim_str(inner.substr(comma + 1));
                    }
                } else if (col_ty.starts_with("[]")) {
                    idx_ty = "usize";
                    item_ty = col_ty.substr(2);
                } else if (col_ty.starts_with("collections.Set[") && col_ty.ends_with(']')) {
                    item_ty = col_ty.substr(16, col_ty.size() - 17);
                } else if (col_ty.starts_with("collections.Deque[") && col_ty.ends_with(']')) {
                    idx_ty = "usize";
                    item_ty = col_ty.substr(18, col_ty.size() - 19);
                } else if (col_ty.starts_with("sync.Channel[") && col_ty.ends_with(']')) {
                    item_ty = col_ty.substr(13, col_ty.size() - 14);
                }
                if (!fs.index_var.empty()) {
                    bind_scoped_var(fs.index_var, false);
                    var_types_[fs.index_var] = idx_ty;
                }
                bind_scoped_var(fs.item_var, false);
                var_types_[fs.item_var] = item_ty;
            }
            if (auto r = check_block_stmts(fs.body); !r) return r;
        }

        for (const auto& [var, line] : moved_at_line_) {
            if (!pre_loop_moves.contains(var)) {
                if (auto r = emit_error(diag_at(line, 1, "cfire/USE-AFTER-MOVE", std::format("variable '{}' moved inside loop body without reassignment before next iteration", var))); !r) return r;
            }
        }
        for (const auto& [var, loan] : invalidated_loans_) {
            if (!pre_loop_loans.contains(var) && var_scope_depth_.contains(var) && var_scope_depth_[var] <= scope_depth_) {
                // Loan on outer pointer invalidated inside loop; remains invalidated after loop
            }
        }
        return {};
    }

    [[nodiscard]] auto check_async_scope_stmt(const AsyncScopeStmt& sc) -> std::expected<void, std::string> {
        if (auto r = check_expr(sc.workers); !r) return r;
        ScopeGuard sg(*this);
        if (!sc.scope_var.empty()) {
            bind_scoped_var(sc.scope_var, false);
        }
        return check_block_stmts(sc.body);
    }

private:
    std::string_view path_;
    std::string pkg_name_{};
    std::string fn_name_{};
    std::string fn_ret_str_{};
    const CheckedFile& cf_;
    std::vector<std::string>& warnings_;
    std::vector<std::string>& errors_;
    std::vector<cfire::diag::StructuredDiagnostic>& diags_;
    bool resilient_{false};
    std::vector<std::string> fallback_errors_{};
    std::vector<cfire::diag::StructuredDiagnostic> fallback_diags_{};
    bool fn_is_fallible_{false};
    bool has_cpp_imports_{false};
    bool seen_cpp_block_{false};
    int scope_depth_{1};
    int in_defer_depth_{0};
    int in_closure_depth_{0};
    int in_escaping_closure_depth_{0};
    int escaping_closure_entry_scope_{0};
    int in_spawn_closure_depth_{0};
    int spawn_entry_scope_{0};
    std::unordered_map<std::string, int> active_loop_index_vars_{};  // name -> nesting count
    std::unordered_set<std::string> disjoint_slot_ptrs_{};
    std::unordered_set<std::string> const_string_vars_{};  // CF-167
    std::unordered_map<const ClosureExpr*, std::vector<std::string>> closure_param_hints_{};  // CF-148
    int loop_depth_{0};
    int in_invalid_unwrap_context_{0};
    std::vector<std::unordered_map<std::string, SavedVarDeclState>*> scope_stack_{};
    std::unordered_set<const Expr*> unwrapped_exprs_{};
    std::unordered_set<const Expr*> discarded_or_exprs_{};
    std::unordered_map<std::string, int> var_scope_depth_{};
    std::unordered_set<std::string> local_defined_vars_{};
    std::unordered_map<std::string, std::string> borrow_origin_{};
    std::unordered_map<std::string, InvalidatedLoan> invalidated_loans_{};
    std::unordered_map<std::string, std::vector<std::string>> extra_borrow_origins_{};
    std::unordered_set<std::string> active_loop_collections_{};
    std::unordered_set<std::string> ptr_or_smart_vars_{};
    std::unordered_set<std::string> raw_or_smart_ptr_vars_{};
    std::unordered_set<std::string> smart_ptr_vars_{};
    std::unordered_set<std::string> byte_slice_vars_{};
    std::unordered_set<std::string> optional_vars_{};
    std::unordered_set<std::string> fallible_vars_{};
    std::unordered_set<std::string> fn_ret_fallible_vars_{};
    std::unordered_set<std::string> fn_ret_optional_vars_{};
    std::unordered_set<std::string> reported_unhandled_vars_{};
    std::unordered_set<std::string> scalar_primitive_vars_{};
    std::unordered_set<std::string> reported_missing_imports_{};
    std::unordered_set<const MemberExpr*> callee_members_{};  // Q26: member expressions that are call targets
    std::unordered_set<std::string> shared_ptr_var_names_{};  // CF-181: smart pointers that are `shared[T]`
    std::unordered_set<std::string> foreign_vars_{};
    std::unordered_map<std::string, std::string> var_types_{};
    std::unordered_map<std::string, NullState> ptr_null_state_{};
    std::unordered_map<std::string, std::size_t> fixed_array_lens_{};
    std::unordered_set<std::string> map_vars_{};
    std::unordered_map<std::string, int> moved_at_line_{};
    std::unordered_set<std::string> move_params_{};
    std::unordered_set<std::string> param_names_{};
    std::unordered_set<std::string> mut_vars_{};
    std::unordered_set<std::string> imm_vars_{};
};


} // namespace cfire::frontend

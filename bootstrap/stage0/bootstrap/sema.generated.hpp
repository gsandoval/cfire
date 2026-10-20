#include "cfire_prelude.hpp"

struct PathInfo;
struct SeenArgPath;
struct SemaResult;
struct SemaFnChecker;

struct PathInfo {
    std::string root{};
    std::string path{};
};

template <>
struct cfire::reflect_struct<PathInfo> {
    static constexpr std::string_view name = "PathInfo";
    static constexpr std::size_t field_count = 2;
    static constexpr std::array<std::string_view, 2> field_names = {
        "root",
        "path",
    };
    static constexpr std::array<std::string_view, 2> field_tags = {
        R"tag()tag",
        R"tag()tag",
    };
};

struct SeenArgPath {
    PathInfo info{};
    bool is_mut_or_out{};
};

template <>
struct cfire::reflect_struct<SeenArgPath> {
    static constexpr std::string_view name = "SeenArgPath";
    static constexpr std::size_t field_count = 2;
    static constexpr std::array<std::string_view, 2> field_names = {
        "info",
        "is_mut_or_out",
    };
    static constexpr std::array<std::string_view, 2> field_tags = {
        R"tag()tag",
        R"tag()tag",
    };
};

struct SemaResult {
    cfire::frontend::CheckedFile cf{};
    std::string error{};
    bool has_error{};
};

template <>
struct cfire::reflect_struct<SemaResult> {
    static constexpr std::string_view name = "SemaResult";
    static constexpr std::size_t field_count = 3;
    static constexpr std::array<std::string_view, 3> field_names = {
        "cf",
        "error",
        "has_error",
    };
    static constexpr std::array<std::string_view, 3> field_tags = {
        R"tag()tag",
        R"tag()tag",
        R"tag()tag",
    };
};

struct SemaFnChecker {
    std::string path{};
    std::vector<std::string> warnings{};
    cfire::Map<std::string, std::int32_t> moved_at_line{};
    cfire::Map<std::string, bool> move_params{};
    cfire::Map<std::string, bool> mut_vars{};
    cfire::Map<std::string, bool> imm_vars{};
    cfire::Map<std::string, std::int32_t> last_read_line{};
};

template <>
struct cfire::reflect_struct<SemaFnChecker> {
    static constexpr std::string_view name = "SemaFnChecker";
    static constexpr std::size_t field_count = 7;
    static constexpr std::array<std::string_view, 7> field_names = {
        "path",
        "warnings",
        "moved_at_line",
        "move_params",
        "mut_vars",
        "imm_vars",
        "last_read_line",
    };
    static constexpr std::array<std::string_view, 7> field_tags = {
        R"tag()tag",
        R"tag()tag",
        R"tag()tag",
        R"tag()tag",
        R"tag()tag",
        R"tag()tag",
        R"tag()tag",
    };
};

[[nodiscard]] inline auto starts_with_str(const std::string& s, const std::string& prefix) -> bool;
[[nodiscard]] inline auto paths_conflict(const std::string& a, const std::string& b) -> bool;
[[nodiscard]] inline auto extract_path_info(const cfire::frontend::ExprPtr& e) -> std::tuple<PathInfo, bool>;
[[nodiscard]] inline auto block_diverges(const std::vector<cfire::frontend::StmtPtr>& body) -> bool;
[[nodiscard]] inline auto SemaFnChecker__record_var_read(SemaFnChecker& c, const std::string& name, std::int32_t line, std::int32_t col) -> std::tuple<std::string, bool>;
[[nodiscard]] inline auto SemaFnChecker__check_expr(SemaFnChecker& c, const cfire::frontend::CheckedFile& cf, const cfire::frontend::ExprPtr& e) -> std::tuple<std::string, bool>;
[[nodiscard]] inline auto SemaFnChecker__check_stmt(SemaFnChecker& c, const cfire::frontend::CheckedFile& cf, const cfire::frontend::StmtPtr& s) -> std::tuple<std::string, bool>;
[[nodiscard]] inline auto selfhost_analyze_file(cfire::frontend::FileAST& ast) -> SemaResult;

[[nodiscard]] inline auto starts_with_str(const std::string& s, const std::string& prefix) -> bool {
    auto n = s.size();
    auto pn = prefix.size();
    if (pn > n) {
        return false;
    }
    return (cfire::slice(s, 0, pn) == prefix);
}

[[nodiscard]] inline auto paths_conflict(const std::string& a, const std::string& b) -> bool {
    if (a == b) {
        return true;
    }
    if (starts_with_str(a, b)) {
        if (a.size() > b.size()) {
            auto next_ch = a[b.size()];
            if ((next_ch == 46) || (next_ch == 91)) {
                return true;
            }
        }
    }
    if (starts_with_str(b, a)) {
        if (b.size() > a.size()) {
            auto next_ch = b[a.size()];
            if ((next_ch == 46) || (next_ch == 91)) {
                return true;
            }
        }
    }
    return false;
}

[[nodiscard]] inline auto extract_path_info(const cfire::frontend::ExprPtr& e) -> std::tuple<PathInfo, bool> {
    auto [root, path] = cfire::frontend::sema_extract_path_info(e);
    if (root != "") {
        return std::make_tuple(PathInfo{.root = root, .path = path}, true);
    }
    return std::make_tuple(PathInfo{.root = "", .path = ""}, false);
}

[[nodiscard]] inline auto block_diverges(const std::vector<cfire::frontend::StmtPtr>& body) -> bool {
    return cfire::frontend::sema_block_diverges(body);
}

[[nodiscard]] inline auto SemaFnChecker__record_var_read(SemaFnChecker& c, const std::string& name, std::int32_t line, std::int32_t col) -> std::tuple<std::string, bool> {
    if (!c.moved_at_line.empty() && c.moved_at_line.contains(name)) {
        auto prev_line = c.moved_at_line[name];
        auto err = (((((((((c.path + ":") + cfire::frontend::sema_to_str(line)) + ":") + cfire::frontend::sema_to_str(col)) + ": use of moved variable '") + name) + "' (previously moved at line ") + cfire::frontend::sema_to_str(prev_line)) + ") (cfire/USE-AFTER-MOVE)");
        return std::make_tuple(err, true);
    }
    if (!c.move_params.empty() && c.move_params.contains(name)) {
        c.last_read_line[name] = line;
    }
    return std::make_tuple("", false);
}

[[nodiscard]] inline auto SemaFnChecker__check_expr(SemaFnChecker& c, const cfire::frontend::CheckedFile& cf, const cfire::frontend::ExprPtr& e) -> std::tuple<std::string, bool> {
    auto k = cfire::frontend::get_expr_kind(e);
    if (k == 0) {
        return std::make_tuple("", false);
    }
    if (k == 1) {
        auto name = cfire::frontend::expr_get_ident(e);
        auto line = cfire::frontend::get_expr_line(e);
        auto col = cfire::frontend::get_expr_col(e);
        return SemaFnChecker__record_var_read(c, name, line, col);
    }
    if (k == 2) {
        if (cfire::frontend::expr_get_literal_is_string(e)) {
            auto val = cfire::frontend::expr_get_literal_value(e);
            auto vars = cfire::frontend::extract_interpolated_vars(val);
            std::size_t i = 0;
            auto n = vars.size();
            auto line = cfire::frontend::get_expr_line(e);
            auto col = cfire::frontend::get_expr_col(e);
            while (i < n) {
                auto [err, is_err] = SemaFnChecker__record_var_read(c, vars[i], line, col);
                if (is_err) {
                    return std::make_tuple(err, true);
                }
                i = (i + 1);
            }
        }
        return std::make_tuple("", false);
    }
    if (k == 3) {
        auto operand = cfire::frontend::expr_get_unary_operand(e);
        auto [err, is_err] = SemaFnChecker__check_expr(c, cf, operand);
        if (is_err) {
            return std::make_tuple(err, true);
        }
        auto op = cfire::frontend::expr_get_unary_op(e);
        if (op == "move") {
            auto [pi, ok_pi] = extract_path_info(operand);
            if (ok_pi) {
                c.moved_at_line[pi.root] = cfire::frontend::get_expr_line(e);
            }
        }
        return std::make_tuple("", false);
    }
    if (k == 4) {
        auto left = cfire::frontend::expr_get_binary_left(e);
        auto [err1, is_err1] = SemaFnChecker__check_expr(c, cf, left);
        if (is_err1) {
            return std::make_tuple(err1, true);
        }
        auto right = cfire::frontend::expr_get_binary_right(e);
        return SemaFnChecker__check_expr(c, cf, right);
    }
    if (k == 5) {
        auto obj = cfire::frontend::expr_get_member_object(e);
        return SemaFnChecker__check_expr(c, cf, obj);
    }
    if (k == 6) {
        auto obj = cfire::frontend::expr_get_index_object(e);
        auto [err1, is_err1] = SemaFnChecker__check_expr(c, cf, obj);
        if (is_err1) {
            return std::make_tuple(err1, true);
        }
        auto idx = cfire::frontend::expr_get_index_expr(e);
        return SemaFnChecker__check_expr(c, cf, idx);
    }
    if (k == 7) {
        auto obj = cfire::frontend::expr_get_slice_object(e);
        auto [err1, is_err1] = SemaFnChecker__check_expr(c, cf, obj);
        if (is_err1) {
            return std::make_tuple(err1, true);
        }
        auto start = cfire::frontend::expr_get_slice_start(e);
        if (start != std::nullopt) {
            auto [err2, is_err2] = SemaFnChecker__check_expr(c, cf, start);
            if (is_err2) {
                return std::make_tuple(err2, true);
            }
        }
        auto end_ex = cfire::frontend::expr_get_slice_end(e);
        if (end_ex != std::nullopt) {
            auto [err3, is_err3] = SemaFnChecker__check_expr(c, cf, end_ex);
            if (is_err3) {
                return std::make_tuple(err3, true);
            }
        }
        return std::make_tuple("", false);
    }
    if (k == 8) {
        auto inner = cfire::frontend::expr_get_try_inner(e);
        return SemaFnChecker__check_expr(c, cf, inner);
    }
    if (k == 9) {
        auto inner = cfire::frontend::expr_get_or_inner(e);
        auto [err1, is_err1] = SemaFnChecker__check_expr(c, cf, inner);
        if (is_err1) {
            return std::make_tuple(err1, true);
        }
        auto fb = cfire::frontend::expr_get_or_fallback(e);
        if (fb != std::nullopt) {
            auto [err2, is_err2] = SemaFnChecker__check_expr(c, cf, fb);
            if (is_err2) {
                return std::make_tuple(err2, true);
            }
        }
        auto h_body = cfire::frontend::expr_get_or_handler_body(e);
        std::size_t i = 0;
        auto n = h_body.size();
        while (i < n) {
            auto [err3, is_err3] = SemaFnChecker__check_stmt(c, cf, h_body[i]);
            if (is_err3) {
                return std::make_tuple(err3, true);
            }
            i = (i + 1);
        }
        return std::make_tuple("", false);
    }
    if (k == 10) {
        auto n = cfire::frontend::expr_get_struct_lit_field_count(e);
        std::size_t i = 0;
        while (i < n) {
            auto fv = cfire::frontend::expr_get_struct_lit_field_value(e, i);
            auto [err, is_err] = SemaFnChecker__check_expr(c, cf, fv);
            if (is_err) {
                return std::make_tuple(err, true);
            }
            i = (i + 1);
        }
        return std::make_tuple("", false);
    }
    if (k == 11) {
        auto els = cfire::frontend::expr_get_array_lit_elements(e);
        std::size_t i = 0;
        auto n = els.size();
        while (i < n) {
            auto [err, is_err] = SemaFnChecker__check_expr(c, cf, els[i]);
            if (is_err) {
                return std::make_tuple(err, true);
            }
            i = (i + 1);
        }
        return std::make_tuple("", false);
    }
    if (k == 12) {
        auto els = cfire::frontend::expr_get_tuple_elements(e);
        std::size_t i = 0;
        auto n = els.size();
        while (i < n) {
            auto [err, is_err] = SemaFnChecker__check_expr(c, cf, els[i]);
            if (is_err) {
                return std::make_tuple(err, true);
            }
            i = (i + 1);
        }
        return std::make_tuple("", false);
    }
    if (k == 13) {
        auto body = cfire::frontend::expr_get_closure_body(e);
        std::size_t i = 0;
        auto n = body.size();
        while (i < n) {
            auto [err, is_err] = SemaFnChecker__check_stmt(c, cf, body[i]);
            if (is_err) {
                return std::make_tuple(err, true);
            }
            i = (i + 1);
        }
        return std::make_tuple("", false);
    }
    if (k == 14) {
        auto callee = cfire::frontend::expr_get_call_callee(e);
        auto [err_c, is_err_c] = SemaFnChecker__check_expr(c, cf, callee);
        if (is_err_c) {
            return std::make_tuple(err_c, true);
        }
        auto callee_k = cfire::frontend::get_expr_kind(callee);
        auto has_target_sig = false;
        auto target_sig = cfire::frontend::FnDecl{};
        if (callee_k == 1) {
            auto fn_name = cfire::frontend::expr_get_ident(callee);
            auto [sig, ok_sig] = cfire::frontend::lookup_fn_sig(cf, fn_name);
            if (ok_sig) {
                target_sig = sig;
                has_target_sig = true;
            }
        } else {
            if (callee_k == 5) {
                auto field = cfire::frontend::expr_get_member_field(callee);
                auto [sig, ok_sig] = cfire::frontend::lookup_method_by_field(cf, field);
                if (ok_sig) {
                    target_sig = sig;
                    has_target_sig = true;
                    if (cfire::frontend::fn_has_receiver(target_sig) && (cfire::frontend::fn_get_receiver(target_sig).intent == "mut")) {
                        auto mem_obj = cfire::frontend::expr_get_member_object(callee);
                        if (cfire::frontend::get_expr_kind(mem_obj) == 1) {
                            auto obj_id = cfire::frontend::expr_get_ident(mem_obj);
                            if (c.imm_vars.contains(obj_id)) {
                                auto err = (((((((((c.path + ":") + cfire::frontend::sema_to_str(cfire::frontend::get_expr_line(e))) + ":") + cfire::frontend::sema_to_str(cfire::frontend::get_expr_col(e))) + ": cannot call mutating method '") + field) + "' on immutable variable '") + obj_id) + "' (cfire/MUT-RECEIVER)");
                                return std::make_tuple(err, true);
                            }
                        }
                    }
                }
            }
        }
        std::vector<SeenArgPath> seen_paths = {};
        if (callee_k == 5) {
            auto mem_obj = cfire::frontend::expr_get_member_object(callee);
            auto [p, ok_p] = extract_path_info(mem_obj);
            if (ok_p) {
                auto is_mut = ((has_target_sig && cfire::frontend::fn_has_receiver(target_sig)) && (cfire::frontend::fn_get_receiver(target_sig).intent == "mut"));
                (void)(seen_paths.push_back(SeenArgPath{.info = p, .is_mut_or_out = is_mut}));
            }
        }
        auto n_args = cfire::frontend::expr_get_call_arg_count(e);
        std::size_t ai = 0;
        while (ai < n_args) {
            auto arg_val = cfire::frontend::expr_get_call_arg_value(e, ai);
            auto arg_intent = cfire::frontend::expr_get_call_arg_intent(e, ai);
            auto arg_line = cfire::frontend::expr_get_call_arg_line(e, ai);
            auto arg_col = cfire::frontend::expr_get_call_arg_col(e, ai);
            if (arg_intent == "out") {
                auto warn = (((((c.path + ":") + cfire::frontend::sema_to_str(arg_line)) + ":") + cfire::frontend::sema_to_str(arg_col)) + ": warning: 'out' call-site annotation is deprecated; use multi-return tuple destructuring instead (cfire/DEPRECATED-OUT-PARAM)");
                (void)(c.warnings.push_back(warn));
            }
            auto [err_a, is_err_a] = SemaFnChecker__check_expr(c, cf, arg_val);
            if (is_err_a) {
                return std::make_tuple(err_a, true);
            }
            if (has_target_sig && (ai < target_sig.params.size())) {
                auto expected = target_sig.params[ai].intent;
                if ((((expected == "mut") || (expected == "out")) || (expected == "move")) && (arg_intent != expected)) {
                    std::string rule = "cfire/MUT-CALLSITE";
                    if (expected == "out") {
                        rule = "cfire/OUT-CALLSITE";
                    }
                    auto err = (((((((((((((c.path + ":") + cfire::frontend::sema_to_str(arg_line)) + ":") + cfire::frontend::sema_to_str(arg_col)) + ": parameter '") + target_sig.params[ai].name) + "' of ") + target_sig.name) + " requires explicit '") + expected) + "' annotation at call-site (") + rule) + ")");
                    return std::make_tuple(err, true);
                }
            }
            auto [pi, ok_pi] = extract_path_info(arg_val);
            if (ok_pi) {
                auto is_mut_or_out = ((arg_intent == "mut") || (arg_intent == "out"));
                std::size_t si = 0;
                auto n_seen = seen_paths.size();
                while (si < n_seen) {
                    auto prev = seen_paths[si];
                    if ((prev.info.root == pi.root) && (prev.is_mut_or_out || is_mut_or_out)) {
                        if (paths_conflict(prev.info.path, pi.path)) {
                            std::string entity = (("projection '" + pi.path) + "'");
                            if ((pi.path == pi.root) && (prev.info.path == prev.info.root)) {
                                entity = (("variable '" + pi.root) + "'");
                            }
                            auto err = (((((((c.path + ":") + cfire::frontend::sema_to_str(arg_line)) + ":") + cfire::frontend::sema_to_str(arg_col)) + ": cannot alias ") + entity) + " with a mut/out parameter in the same call (cfire/MUT-ALIAS)");
                            return std::make_tuple(err, true);
                        }
                    }
                    si = (si + 1);
                }
                (void)(seen_paths.push_back(SeenArgPath{.info = pi, .is_mut_or_out = is_mut_or_out}));
                if (is_mut_or_out) {
                    if (c.imm_vars.contains(pi.root)) {
                        std::string entity = pi.path;
                        if (pi.path == pi.root) {
                            entity = pi.root;
                        }
                        auto err = (((((((((c.path + ":") + cfire::frontend::sema_to_str(arg_line)) + ":") + cfire::frontend::sema_to_str(arg_col)) + ": cannot pass immutable variable '") + entity) + "' as '") + arg_intent) + "' argument (cfire/IMMUTABLE-MUT-ARG)");
                        return std::make_tuple(err, true);
                    }
                }
                if (arg_intent == "move") {
                    c.moved_at_line[pi.root] = arg_line;
                }
            }
            ai = (ai + 1);
        }
        if (cfire::frontend::expr_has_call_block_arg(e)) {
            auto body = cfire::frontend::expr_get_call_block_arg_body(e);
            std::size_t bi = 0;
            auto n_body = body.size();
            while (bi < n_body) {
                auto [err_b, is_err_b] = SemaFnChecker__check_stmt(c, cf, body[bi]);
                if (is_err_b) {
                    return std::make_tuple(err_b, true);
                }
                bi = (bi + 1);
            }
        }
        return std::make_tuple("", false);
    }
    return std::make_tuple("", false);
}

[[nodiscard]] inline auto SemaFnChecker__check_stmt(SemaFnChecker& c, const cfire::frontend::CheckedFile& cf, const cfire::frontend::StmtPtr& s) -> std::tuple<std::string, bool> {
    auto k = cfire::frontend::get_stmt_kind(s);
    if (k == 0) {
        return std::make_tuple("", false);
    }
    if (k == 1) {
        auto name = cfire::frontend::stmt_get_var_name(s);
        auto is_mut = cfire::frontend::stmt_get_var_is_mut(s);
        auto was_var = cfire::frontend::stmt_get_var_was_var(s);
        auto line = cfire::frontend::get_stmt_line(s);
        auto col = cfire::frontend::stmt_get_var_col(s);
        if (was_var) {
            auto warn = (((((c.path + ":") + cfire::frontend::sema_to_str(line)) + ":") + cfire::frontend::sema_to_str(col)) + ": warning: 'var' is deprecated; use 'x := val' or 'mut x: T = val' instead (cfire/DEPRECATED-VAR)");
            (void)(c.warnings.push_back(warn));
        }
        if (is_mut) {
            (void)(c.imm_vars.remove(name));
            c.mut_vars[name] = true;
        } else {
            (void)(c.mut_vars.remove(name));
            c.imm_vars[name] = true;
        }
        auto init = cfire::frontend::stmt_get_var_init(s);
        if (init != std::nullopt) {
            if (cfire::frontend::get_expr_kind(init) == 9) {
                auto h_body = cfire::frontend::expr_get_or_handler_body(init);
                if (h_body.size() > 0) {
                    if (!block_diverges(h_body)) {
                        auto err_v = cfire::frontend::expr_get_or_err_var(init);
                        if (err_v == "") {
                            err_v = "err";
                        }
                        auto err = (((((((c.path + ":") + cfire::frontend::sema_to_str(line)) + ":") + cfire::frontend::sema_to_str(col)) + ": block 'or ") + err_v) + " { ... }' must definitely diverge with return or panic, otherwise unwrapping empty value causes undefined behavior (cfire/NON-DIVERGING-OR-BLOCK)");
                        return std::make_tuple(err, true);
                    }
                }
            }
            return SemaFnChecker__check_expr(c, cf, init);
        }
        return std::make_tuple("", false);
    }
    if (k == 2) {
        auto bindings = cfire::frontend::stmt_get_destruct_var_bindings(s);
        std::size_t i = 0;
        auto n = bindings.size();
        while (i < n) {
            auto b_name = bindings[i].first;
            auto b_is_mut = bindings[i].second;
            if (b_is_mut) {
                c.mut_vars[b_name] = true;
            } else {
                c.imm_vars[b_name] = true;
            }
            i = (i + 1);
        }
        auto init = cfire::frontend::stmt_get_destruct_var_init(s);
        return SemaFnChecker__check_expr(c, cf, init);
    }
    if (k == 3) {
        auto val = cfire::frontend::stmt_get_assign_value(s);
        auto line = cfire::frontend::get_stmt_line(s);
        if (val != std::nullopt) {
            if (cfire::frontend::get_expr_kind(val) == 9) {
                auto h_body = cfire::frontend::expr_get_or_handler_body(val);
                if (h_body.size() > 0) {
                    if (!block_diverges(h_body)) {
                        auto err_v = cfire::frontend::expr_get_or_err_var(val);
                        if (err_v == "") {
                            err_v = "err";
                        }
                        auto err = (((((c.path + ":") + cfire::frontend::sema_to_str(line)) + ":1: assignment from 'or ") + err_v) + " { ... }' must definitely diverge with return or panic (cfire/NON-DIVERGING-OR-BLOCK)");
                        return std::make_tuple(err, true);
                    }
                }
            }
            auto [err_v, is_err_v] = SemaFnChecker__check_expr(c, cf, val);
            if (is_err_v) {
                return std::make_tuple(err_v, true);
            }
        }
        auto target = cfire::frontend::stmt_get_assign_target(s);
        if (cfire::frontend::get_expr_kind(target) == 1) {
            auto id_name = cfire::frontend::expr_get_ident(target);
            if (c.imm_vars.contains(id_name)) {
                auto err = (((((c.path + ":") + cfire::frontend::sema_to_str(line)) + ":1: cannot assign to immutable variable '") + id_name) + "' (cfire/IMMUTABLE-ASSIGN)");
                return std::make_tuple(err, true);
            }
            (void)(c.moved_at_line.remove(id_name));
        } else {
            auto [err_t, is_err_t] = SemaFnChecker__check_expr(c, cf, target);
            if (is_err_t) {
                return std::make_tuple(err_t, true);
            }
        }
        return std::make_tuple("", false);
    }
    if (k == 4) {
        auto val = cfire::frontend::stmt_get_destruct_assign_value(s);
        auto line = cfire::frontend::get_stmt_line(s);
        auto [err_v, is_err_v] = SemaFnChecker__check_expr(c, cf, val);
        if (is_err_v) {
            return std::make_tuple(err_v, true);
        }
        auto targs = cfire::frontend::stmt_get_destruct_assign_targets(s);
        std::size_t i = 0;
        auto n = targs.size();
        while (i < n) {
            auto targ = targs[i];
            if (cfire::frontend::get_expr_kind(targ) == 1) {
                auto id_name = cfire::frontend::expr_get_ident(targ);
                if (c.imm_vars.contains(id_name)) {
                    auto err = (((((c.path + ":") + cfire::frontend::sema_to_str(line)) + ":1: cannot assign to immutable variable '") + id_name) + "' (cfire/IMMUTABLE-ASSIGN)");
                    return std::make_tuple(err, true);
                }
                (void)(c.moved_at_line.remove(id_name));
            } else {
                auto [err_t, is_err_t] = SemaFnChecker__check_expr(c, cf, targ);
                if (is_err_t) {
                    return std::make_tuple(err_t, true);
                }
            }
            i = (i + 1);
        }
        return std::make_tuple("", false);
    }
    if (k == 5) {
        auto call = cfire::frontend::stmt_get_defer_call(s);
        return SemaFnChecker__check_expr(c, cf, call);
    }
    if (k == 6) {
        auto val = cfire::frontend::stmt_get_return_value(s);
        return SemaFnChecker__check_expr(c, cf, val);
    }
    if (k == 7) {
        auto cond = cfire::frontend::stmt_get_if_cond(s);
        auto [err_c, is_err_c] = SemaFnChecker__check_expr(c, cf, cond);
        if (is_err_c) {
            return std::make_tuple(err_c, true);
        }
        auto initial_moves = cfire::frontend::clone_str_int_map(c.moved_at_line);
        auto then_body = cfire::frontend::stmt_get_if_then_body(s);
        std::size_t i = 0;
        auto n_then = then_body.size();
        while (i < n_then) {
            auto [err_t, is_err_t] = SemaFnChecker__check_stmt(c, cf, then_body[i]);
            if (is_err_t) {
                return std::make_tuple(err_t, true);
            }
            i = (i + 1);
        }
        auto then_moves = cfire::frontend::clone_str_int_map(c.moved_at_line);
        c.moved_at_line = cfire::frontend::clone_str_int_map(initial_moves);
        auto else_body = cfire::frontend::stmt_get_if_else_body(s);
        std::size_t ei = 0;
        auto n_else = else_body.size();
        while (ei < n_else) {
            auto [err_e, is_err_e] = SemaFnChecker__check_stmt(c, cf, else_body[ei]);
            if (is_err_e) {
                return std::make_tuple(err_e, true);
            }
            ei = (ei + 1);
        }
        auto else_moves = cfire::frontend::clone_str_int_map(c.moved_at_line);
        if (n_else == 0) {
            c.moved_at_line = initial_moves;
        } else {
            if (block_diverges(then_body)) {
                c.moved_at_line = else_moves;
            } else {
                if (block_diverges(else_body)) {
                    c.moved_at_line = then_moves;
                } else {
                    c.moved_at_line = cfire::frontend::merge_post_branch_moves(initial_moves, then_moves, else_moves);
                }
            }
        }
        return std::make_tuple("", false);
    }
    if (k == 8) {
        auto iter_ex = cfire::frontend::stmt_get_for_iter(s);
        auto [err_it, is_err_it] = SemaFnChecker__check_expr(c, cf, iter_ex);
        if (is_err_it) {
            return std::make_tuple(err_it, true);
        }
        auto pre_loop_moves = cfire::frontend::clone_str_int_map(c.moved_at_line);
        auto body = cfire::frontend::stmt_get_for_body(s);
        std::size_t i = 0;
        auto n = body.size();
        while (i < n) {
            auto [err_b, is_err_b] = SemaFnChecker__check_stmt(c, cf, body[i]);
            if (is_err_b) {
                return std::make_tuple(err_b, true);
            }
            i = (i + 1);
        }
        auto unreassigned = cfire::frontend::find_unreassigned_loop_moves(pre_loop_moves, c.moved_at_line);
        if (unreassigned.first != "") {
            auto err = (((((c.path + ":") + cfire::frontend::sema_to_str(unreassigned.second)) + ":1: variable '") + unreassigned.first) + "' moved inside loop body without reassignment before next iteration (cfire/USE-AFTER-MOVE)");
            return std::make_tuple(err, true);
        }
        return std::make_tuple("", false);
    }
    if (k == 9) {
        auto target = cfire::frontend::stmt_get_match_target(s);
        auto [err_t, is_err_t] = SemaFnChecker__check_expr(c, cf, target);
        if (is_err_t) {
            return std::make_tuple(err_t, true);
        }
        auto line = cfire::frontend::get_stmt_line(s);
        auto col = cfire::frontend::stmt_get_match_col(s);
        if (cfire::frontend::stmt_get_match_is_move(s) && (target != std::nullopt)) {
            auto [pi, ok_pi] = extract_path_info(target);
            if (ok_pi) {
                c.moved_at_line[pi.root] = line;
            }
        }
        auto n_arms = cfire::frontend::stmt_get_match_arm_count(s);
        auto is_slice_match = false;
        std::size_t i = 0;
        while (i < n_arms) {
            if (cfire::frontend::stmt_get_match_arm_is_slice(s, i)) {
                is_slice_match = true;
                break;
            }
            i = (i + 1);
        }
        if (is_slice_match) {
            std::size_t ai = 0;
            while (ai < n_arms) {
                auto arm_body = cfire::frontend::stmt_get_match_arm_body(s, ai);
                std::size_t bi = 0;
                auto n_b = arm_body.size();
                while (bi < n_b) {
                    auto [err_b, is_err_b] = SemaFnChecker__check_stmt(c, cf, arm_body[bi]);
                    if (is_err_b) {
                        return std::make_tuple(err_b, true);
                    }
                    bi = (bi + 1);
                }
                ai = (ai + 1);
            }
            return std::make_tuple("", false);
        }
        std::string enum_name = "";
        cfire::Map<std::string, bool> covered{};
        auto has_wildcard = false;
        std::size_t ai = 0;
        while (ai < n_arms) {
            auto vname = cfire::frontend::stmt_get_match_arm_variant(s, ai);
            auto eprefix = cfire::frontend::stmt_get_match_arm_enum_prefix(s, ai);
            if (vname == "_") {
                has_wildcard = true;
            } else {
                covered[vname] = true;
                if (eprefix != "") {
                    enum_name = eprefix;
                } else {
                    auto candidate = cfire::frontend::lookup_variant_to_enum(cf, vname);
                    if (candidate != "") {
                        enum_name = candidate;
                    }
                }
            }
            auto arm_body = cfire::frontend::stmt_get_match_arm_body(s, ai);
            std::size_t bi = 0;
            auto n_b = arm_body.size();
            while (bi < n_b) {
                auto [err_b, is_err_b] = SemaFnChecker__check_stmt(c, cf, arm_body[bi]);
                if (is_err_b) {
                    return std::make_tuple(err_b, true);
                }
                bi = (bi + 1);
            }
            ai = (ai + 1);
        }
        if (!has_wildcard && (enum_name != "")) {
            auto [en, ok_en] = cfire::frontend::lookup_enum(cf, enum_name);
            if (ok_en) {
                std::size_t vi = 0;
                auto n_v = en.variants.size();
                while (vi < n_v) {
                    auto v = en.variants[vi];
                    if (!covered.contains(v.name)) {
                        auto err = (((((((((c.path + ":") + cfire::frontend::sema_to_str(line)) + ":") + cfire::frontend::sema_to_str(col)) + ": non-exhaustive match on enum '") + enum_name) + "': missing variant '") + v.name) + "' (cfire/NON-EXHAUSTIVE-MATCH)");
                        return std::make_tuple(err, true);
                    }
                    vi = (vi + 1);
                }
            }
        }
        return std::make_tuple("", false);
    }
    if (k == 10) {
        auto workers = cfire::frontend::stmt_get_async_workers(s);
        auto [err_w, is_err_w] = SemaFnChecker__check_expr(c, cf, workers);
        if (is_err_w) {
            return std::make_tuple(err_w, true);
        }
        auto body = cfire::frontend::stmt_get_async_body(s);
        std::size_t i = 0;
        auto n = body.size();
        while (i < n) {
            auto [err_b, is_err_b] = SemaFnChecker__check_stmt(c, cf, body[i]);
            if (is_err_b) {
                return std::make_tuple(err_b, true);
            }
            i = (i + 1);
        }
        return std::make_tuple("", false);
    }
    if (k == 11) {
        auto ex = cfire::frontend::stmt_get_expr(s);
        return SemaFnChecker__check_expr(c, cf, ex);
    }
    return std::make_tuple("", false);
}

[[nodiscard]] inline auto selfhost_analyze_file(cfire::frontend::FileAST& ast) -> SemaResult {
    auto cf = cfire::frontend::CheckedFile{};
    std::size_t gvi = 0;
    auto n_gv = ast.global_vars.size();
    while (gvi < n_gv) {
        auto gv = ast.global_vars[gvi];
        if (gv.was_var) {
            auto warn = (((ast.path + ":") + cfire::frontend::sema_to_str(gv.line)) + ":1: warning: 'var' is deprecated; use 'x := val' or 'mut x: T = val' instead (cfire/DEPRECATED-VAR)");
            (void)(cf.warnings.push_back(warn));
        }
        gvi = (gvi + 1);
    }
    (void)(cfire::frontend::index_file_declarations(cf, ast));
    std::size_t fvi = 0;
    auto n_fn = ast.functions.size();
    while (fvi < n_fn) {
        auto checker = SemaFnChecker{.path = ast.path, .warnings = {}};
        if (cfire::frontend::ast_fn_has_receiver(ast, fvi)) {
            auto r_name = cfire::frontend::ast_fn_get_receiver_name(ast, fvi);
            auto r_intent = cfire::frontend::ast_fn_get_receiver_intent(ast, fvi);
            if (r_intent == "mut") {
                checker.mut_vars[r_name] = true;
            } else {
                checker.imm_vars[r_name] = true;
            }
        }
        std::size_t pi = 0;
        auto n_p = cfire::frontend::ast_fn_get_param_count(ast, fvi);
        while (pi < n_p) {
            auto p_name = cfire::frontend::ast_fn_get_param_name(ast, fvi, pi);
            auto p_intent = cfire::frontend::ast_fn_get_param_intent(ast, fvi, pi);
            auto p_line = cfire::frontend::ast_fn_get_param_line(ast, fvi, pi);
            auto p_col = cfire::frontend::ast_fn_get_param_col(ast, fvi, pi);
            if (p_intent == "move") {
                checker.move_params[p_name] = true;
            }
            if (p_intent == "out") {
                auto warn = (((((ast.path + ":") + cfire::frontend::sema_to_str(p_line)) + ":") + cfire::frontend::sema_to_str(p_col)) + ": warning: 'out' parameter is deprecated; use multi-return tuple '(T1, T2)' instead (cfire/DEPRECATED-OUT-PARAM)");
                (void)(checker.warnings.push_back(warn));
            }
            if ((p_intent == "mut") || (p_intent == "out")) {
                checker.mut_vars[p_name] = true;
            } else {
                checker.imm_vars[p_name] = true;
            }
            pi = (pi + 1);
        }
        std::size_t sbi = 0;
        auto n_body = cfire::frontend::ast_fn_get_body_count(ast, fvi);
        while (sbi < n_body) {
            auto st = cfire::frontend::ast_fn_get_body_stmt(ast, fvi, sbi);
            auto [err, is_err] = SemaFnChecker__check_stmt(checker, cf, st);
            if (is_err) {
                return SemaResult{.cf = cf, .error = err, .has_error = true};
            }
            sbi = (sbi + 1);
        }
        std::size_t wi = 0;
        auto n_w = checker.warnings.size();
        while (wi < n_w) {
            (void)(cf.warnings.push_back(checker.warnings[wi]));
            wi = (wi + 1);
        }
        auto fn_key = cfire::frontend::ast_fn_get_key(ast, fvi);
        (void)(cfire::frontend::record_last_use_moves(cf, fn_key, checker.last_read_line));
        fvi = (fvi + 1);
    }
    std::size_t ti = 0;
    auto n_tests = ast.tests.size();
    while (ti < n_tests) {
        auto checker = SemaFnChecker{.path = ast.path, .warnings = {}};
        std::size_t sbi = 0;
        auto n_tbody = cfire::frontend::ast_test_get_body_count(ast, ti);
        while (sbi < n_tbody) {
            auto st = cfire::frontend::ast_test_get_body_stmt(ast, ti, sbi);
            auto [err, is_err] = SemaFnChecker__check_stmt(checker, cf, st);
            if (is_err) {
                return SemaResult{.cf = cf, .error = err, .has_error = true};
            }
            sbi = (sbi + 1);
        }
        std::size_t wi = 0;
        auto n_w = checker.warnings.size();
        while (wi < n_w) {
            (void)(cf.warnings.push_back(checker.warnings[wi]));
            wi = (wi + 1);
        }
        ti = (ti + 1);
    }
    (void)(cfire::frontend::set_checked_file_ast(cf, ast));
    return SemaResult{.cf = cf, .error = "", .has_error = false};
}

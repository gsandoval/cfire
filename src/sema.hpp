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


[[nodiscard]] inline auto analyze(FileAST ast) -> std::expected<CheckedFile, std::string> {
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

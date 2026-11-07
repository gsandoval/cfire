#pragma once
// src/lsp/inlay_hints.hpp — LSP `textDocument/inlayHint` Provider (Inferred `:=` Types & Parameter Labels)

#include "server.hpp"

namespace cfire::lsp {

inline auto Server::compute_inlay_hints(const std::string& uri) -> std::string {
    std::string text = get_doc_text(uri);
    auto lines = detail::split_lines(text);
    auto [ast_res, ws] = get_or_parse_ast(uri, text);
    if (!ast_res) return "[]";
    const auto& ast = *ast_res;

    struct InlayHintItem {
        int line;
        int character;
        std::string label;
        int kind; // 1 = Type, 2 = Parameter
        bool padding_left;
        bool padding_right;
    };
    std::vector<InlayHintItem> hints;

    // 1. Collect Type Inlay Hints (kind: 1) for `:=` variable bindings
    std::vector<detail::LocalBindingInfo> all_inferred;
    std::unordered_map<std::string, detail::LocalBindingInfo> env;
    for (const auto& fn : ast.functions) {
        env.clear();
        for (const auto& p : fn.params) {
            detail::LocalBindingInfo pi;
            pi.name = p.name;
            pi.cf_type = cfire::fmt::format_type_ref(p.type);
            env[pi.name] = pi;
        }
        detail::collect_stmt_bindings(fn.body, ast, ws, lines, env, all_inferred);
    }
    for (const auto& t : ast.tests) {
        env.clear();
        detail::collect_stmt_bindings(t.body, ast, ws, lines, env, all_inferred);
    }

    for (const auto& b : all_inferred) {
        if (!b.inferred_colon_eq || b.cf_type == "auto" || b.cf_type == "void") continue;
        int ln = std::max(0, b.decl_line - 1);
        hints.push_back(InlayHintItem{
            .line = ln,
            .character = b.name_end_col,
            .label = ": " + b.cf_type,
            .kind = 1,
            .padding_left = false,
            .padding_right = true
        });
    }

    // 2. Collect Parameter Name Inlay Hints (kind: 2) for CallExpr arguments
    auto extract_params_from_sig = [](std::string_view sig) -> std::vector<std::string> {
        std::vector<std::string> plabels;
        auto lp = sig.find('(');
        auto rp = sig.rfind(')');
        if (lp != std::string_view::npos && rp != std::string_view::npos && rp > lp + 1) {
            std::string plist(sig.substr(lp + 1, rp - lp - 1));
            std::istringstream iss(plist);
            std::string item;
            while (std::getline(iss, item, ',')) {
                plabels.push_back(cfire::fmt::detail::trim_ws(item));
            }
        }
        return plabels;
    };

    auto extract_param_name = [](std::string_view p) -> std::string {
        while (!p.empty() && (p.front() == ' ' || p.front() == '\t')) p.remove_prefix(1);
        for (std::string_view prefix : {"in ", "mut ", "out ", "move "}) {
            if (p.starts_with(prefix)) {
                p.remove_prefix(prefix.size());
                break;
            }
        }
        while (!p.empty() && (p.front() == ' ' || p.front() == '\t')) p.remove_prefix(1);
        auto colon = p.find(':');
        if (colon != std::string_view::npos) {
            p = p.substr(0, colon);
        }
        while (!p.empty() && (p.back() == ' ' || p.back() == '\t')) p.remove_suffix(1);
        return std::string(p);
    };

    auto process_calls = [&](const std::vector<cfire::frontend::StmtPtr>& stmts, const std::unordered_map<std::string, detail::LocalBindingInfo>& local_env) {
        detail::walk_stmts_calls(stmts, [&](const cfire::frontend::CallExpr& call) {
            std::vector<std::string> param_names;
            if (const auto* id = std::get_if<cfire::frontend::IdentExpr>(&call.callee->node)) {
                if (id->name != "println" && id->name != "print" && id->name != "panic" && id->name != "make") {
                    for (const auto& fn : ast.functions) {
                        if (fn.name == id->name) {
                            for (const auto& p : fn.params) param_names.push_back(p.name);
                            break;
                        }
                    }
                    if (param_names.empty()) {
                        for (const auto& pkg : ws.local_packages) {
                            for (const auto& fn : pkg.ast.functions) {
                                if (fn.name == id->name) {
                                    for (const auto& p : fn.params) param_names.push_back(p.name);
                                    break;
                                }
                            }
                            if (!param_names.empty()) break;
                        }
                    }
                    if (param_names.empty()) {
                        for (const auto& mod : cfire::doc::stdlib_catalog()) {
                            for (const auto& fn : mod.functions) {
                                if (fn.name == id->name) {
                                    auto pdefs = extract_params_from_sig(fn.cf_sig);
                                    for (const auto& pd : pdefs) {
                                        auto pn = extract_param_name(pd);
                                        if (!pn.empty()) param_names.push_back(pn);
                                    }
                                    break;
                                }
                            }
                            if (!param_names.empty()) break;
                        }
                    }
                }
            } else if (const auto* mem = std::get_if<cfire::frontend::MemberExpr>(&call.callee->node)) {
                if (const auto* obj_id = std::get_if<cfire::frontend::IdentExpr>(&mem->object->node)) {
                    for (const auto& pkg : ws.local_packages) {
                        if (pkg.alias == obj_id->name) {
                            for (const auto& fn : pkg.ast.functions) {
                                if (fn.name == mem->field) {
                                    for (const auto& p : fn.params) param_names.push_back(p.name);
                                    break;
                                }
                            }
                            break;
                        }
                    }
                    if (param_names.empty()) {
                        for (const auto& mod : cfire::doc::stdlib_catalog()) {
                            if (mod.ns_alias == obj_id->name) {
                                for (const auto& fn : mod.functions) {
                                    if (fn.name == mem->field) {
                                        auto pdefs = extract_params_from_sig(fn.cf_sig);
                                        for (const auto& pd : pdefs) {
                                            auto pn = extract_param_name(pd);
                                            if (!pn.empty()) param_names.push_back(pn);
                                        }
                                        break;
                                    }
                                }
                                break;
                            }
                        }
                    }
                }
                if (param_names.empty()) {
                    std::string recv_type = detail::infer_expr_cf_type(mem->object, ast, ws, local_env);
                    while (recv_type.ends_with("!") || recv_type.ends_with("?")) recv_type.pop_back();
                    if (recv_type.starts_with("own[") && recv_type.ends_with("]")) {
                        recv_type = recv_type.substr(4, recv_type.size() - 5);
                    } else if (recv_type.starts_with("shared[") && recv_type.ends_with("]")) {
                        recv_type = recv_type.substr(7, recv_type.size() - 8);
                    }
                    for (const auto& fn : ast.functions) {
                        if (fn.receiver && fn.name == mem->field) {
                            std::string fn_recv = cfire::fmt::format_type_ref(fn.receiver->type);
                            if (fn_recv == recv_type) {
                                for (const auto& p : fn.params) param_names.push_back(p.name);
                                break;
                            }
                        }
                    }
                    if (param_names.empty()) {
                        for (const auto& pkg : ws.local_packages) {
                            for (const auto& fn : pkg.ast.functions) {
                                if (fn.receiver && fn.name == mem->field) {
                                    std::string fn_recv = cfire::fmt::format_type_ref(fn.receiver->type);
                                    if (fn_recv == recv_type || pkg.alias + "." + fn_recv == recv_type) {
                                        for (const auto& p : fn.params) param_names.push_back(p.name);
                                        break;
                                    }
                                }
                            }
                            if (!param_names.empty()) break;
                        }
                    }
                    if (param_names.empty()) {
                        for (const auto& mod : cfire::doc::stdlib_catalog()) {
                            for (const auto& fn : mod.functions) {
                                if (auto dot = fn.name.find('.'); dot != std::string::npos) {
                                    std::string htype = fn.name.substr(0, dot);
                                    std::string mname = fn.name.substr(dot + 1);
                                    if ((recv_type == htype || recv_type == mod.ns_alias + "." + htype) && mname == mem->field) {
                                        auto pdefs = extract_params_from_sig(fn.cf_sig);
                                        for (const auto& pd : pdefs) {
                                            auto pn = extract_param_name(pd);
                                            if (!pn.empty()) param_names.push_back(pn);
                                        }
                                        break;
                                    }
                                }
                            }
                            if (!param_names.empty()) break;
                        }
                    }
                }
            }

            for (std::size_t i = 0; i < call.args.size() && i < param_names.size(); ++i) {
                const auto& arg = call.args[i];
                if (!arg.label.empty()) continue;
                if (arg.line <= 0 || arg.col <= 0) continue;
                if (const auto* arg_id = std::get_if<cfire::frontend::IdentExpr>(&arg.value->node)) {
                    if (arg_id->name == param_names[i]) continue;
                }
                hints.push_back(InlayHintItem{
                    .line = std::max(0, arg.line - 1),
                    .character = std::max(0, arg.col - 1),
                    .label = param_names[i] + ":",
                    .kind = 2,
                    .padding_left = false,
                    .padding_right = true
                });
            }
        });
    };

    for (const auto& fn : ast.functions) {
        std::unordered_map<std::string, detail::LocalBindingInfo> fn_env;
        if (fn.receiver) {
            detail::LocalBindingInfo r;
            r.name = fn.receiver->name;
            r.cf_type = cfire::fmt::format_type_ref(fn.receiver->type);
            fn_env[r.name] = r;
        }
        for (const auto& p : fn.params) {
            detail::LocalBindingInfo pi;
            pi.name = p.name;
            pi.cf_type = cfire::fmt::format_type_ref(p.type);
            fn_env[pi.name] = pi;
        }
        std::vector<detail::LocalBindingInfo> dummy;
        detail::collect_stmt_bindings(fn.body, ast, ws, lines, fn_env, dummy);
        process_calls(fn.body, fn_env);
    }
    for (const auto& t : ast.tests) {
        std::unordered_map<std::string, detail::LocalBindingInfo> t_env;
        std::vector<detail::LocalBindingInfo> dummy;
        detail::collect_stmt_bindings(t.body, ast, ws, lines, t_env, dummy);
        process_calls(t.body, t_env);
    }

    // Sort combined hints in document order: line ASC, character ASC, kind ASC
    std::sort(hints.begin(), hints.end(), [](const auto& a, const auto& b) {
        if (a.line != b.line) return a.line < b.line;
        if (a.character != b.character) return a.character < b.character;
        return a.kind < b.kind;
    });

    std::string out = "[";
    bool first = true;
    for (const auto& h : hints) {
        if (!first) out += ",";
        first = false;
        out += std::format(
            R"({{"position":{{"line":{},"character":{}}},"label":"{}","kind":{},"paddingLeft":{},"paddingRight":{}}})",
            h.line, h.character, detail::json_escape(h.label), h.kind,
            h.padding_left ? "true" : "false", h.padding_right ? "true" : "false");
    }
    out += "]";
    return out;
}

} // namespace cfire::lsp

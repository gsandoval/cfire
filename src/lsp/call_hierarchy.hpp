#pragma once
// src/lsp/call_hierarchy.hpp — LSP `textDocument/prepareCallHierarchy`, `incomingCalls` & `outgoingCalls`

#include "server.hpp"

namespace cfire::lsp {

inline void Server::collect_expr_calls(
    const cfire::frontend::ExprPtr& e,
    std::vector<std::pair<std::string, int>>& calls
) {
    using namespace cfire::frontend;
    detail::walk_exprs(e, [&](const ExprPtr& ex) {
        if (const auto* call = std::get_if<CallExpr>(&ex->node)) {
            if (call->callee) {
                if (const auto* id = std::get_if<IdentExpr>(&call->callee->node)) {
                    calls.emplace_back(id->name, ex->line);
                } else if (const auto* mem = std::get_if<MemberExpr>(&call->callee->node)) {
                    calls.emplace_back(mem->field, ex->line);
                }
            }
        }
    });
}

inline void Server::collect_stmt_calls(
    const std::vector<cfire::frontend::StmtPtr>& stmts,
    std::vector<std::pair<std::string, int>>& calls
) {
    using namespace cfire::frontend;
    detail::walk_stmt_exprs(stmts, [&](const ExprPtr& ex) {
        if (const auto* call = std::get_if<CallExpr>(&ex->node)) {
            if (call->callee) {
                if (const auto* id = std::get_if<IdentExpr>(&call->callee->node)) {
                    calls.emplace_back(id->name, ex->line);
                } else if (const auto* mem = std::get_if<MemberExpr>(&call->callee->node)) {
                    calls.emplace_back(mem->field, ex->line);
                }
            }
        }
    });
}

inline auto Server::compute_prepare_call_hierarchy(const std::string& uri, int line, int character) -> std::string {
    std::string text = get_doc_text(uri);
    auto lines = detail::split_lines(text);
    auto [ast_res, ws] = get_or_parse_ast(uri, text, line, character);
    if (!ast_res) return "[]";

    auto sym = detail::symbol_at_position(lines, line, character);
    std::string target_name = sym.name;
    if (target_name.empty() || target_name == "fn") {
        for (const auto& fn : ast_res->functions) {
            if (fn.line - 1 == line) {
                target_name = fn.name;
                break;
            }
        }
    }
    if (target_name.empty()) return "[]";

    auto format_item = [](std::string_view name, int kind, std::string_view sig, std::string_view doc_uri, int one_based_line) -> std::string {
        int ln = std::max(0, one_based_line - 1);
        return std::format(
            R"({{"name":"{}","kind":{},"detail":"{}","uri":"{}","range":{{"start":{{"line":{},"character":0}},"end":{{"line":{},"character":20}}}},"selectionRange":{{"start":{{"line":{},"character":0}},"end":{{"line":{},"character":12}}}}}})",
            detail::json_escape(name), kind, detail::json_escape(sig), detail::json_escape(doc_uri), ln, ln, ln, ln);
    };

    for (const auto& fn : ast_res->functions) {
        if (fn.name == target_name) {
            int kind = fn.receiver ? 6 : 12;
            return "[" + format_item(fn.name, kind, cfire::fmt::format_fn_signature(fn), uri, fn.line) + "]";
        }
    }
    for (const auto& pkg : ws.local_packages) {
        for (const auto& fn : pkg.ast.functions) {
            if (fn.name == target_name) {
                int kind = fn.receiver ? 6 : 12;
                return "[" + format_item(fn.name, kind, cfire::fmt::format_fn_signature(fn), pkg.uri, fn.line) + "]";
            }
        }
    }
    for (const auto& doc_uri : discover_workspace_cf_files(uri)) {
        if (doc_uri == uri) continue;
        if (auto doc_ast = cfire::frontend::parse_source(detail::uri_to_path(doc_uri), get_doc_text(doc_uri))) {
            for (const auto& fn : doc_ast->functions) {
                if (fn.name == target_name) {
                    int kind = fn.receiver ? 6 : 12;
                    return "[" + format_item(fn.name, kind, cfire::fmt::format_fn_signature(fn), doc_uri, fn.line) + "]";
                }
            }
        }
    }
    return "[]";
}

inline auto Server::compute_incoming_calls(std::string_view msg) -> std::string {
    std::size_t item_idx = detail::find_json_key(msg, "item");
    std::string_view sub = (item_idx != std::string_view::npos) ? msg.substr(item_idx) : msg;
    std::string target_name = detail::extract_json_string(sub, "name").value_or("");
    std::string item_uri = detail::extract_json_string(sub, "uri").value_or("");
    if (target_name.empty()) return "[]";

    auto all_cf_files = discover_workspace_cf_files(item_uri);
    std::string out = "[";
    bool first_caller = true;

    for (const auto& doc_uri : all_cf_files) {
        std::string doc_text = get_doc_text(doc_uri);
        auto doc_ast = cfire::frontend::parse_source(detail::uri_to_path(doc_uri), doc_text);
        if (!doc_ast) continue;

        for (const auto& fn : doc_ast->functions) {
            std::vector<std::pair<std::string, int>> calls;
            collect_stmt_calls(fn.body, calls);
            std::vector<int> call_lines;
            for (const auto& [callee, ln] : calls) {
                if (callee == target_name) {
                    call_lines.push_back(std::max(0, ln - 1));
                }
            }
            if (!call_lines.empty()) {
                if (!first_caller) out += ",";
                first_caller = false;
                int fn_ln = std::max(0, fn.line - 1);
                int kind = fn.receiver ? 6 : 12;
                std::string ranges_json = "[";
                for (std::size_t i = 0; i < call_lines.size(); ++i) {
                    if (i > 0) ranges_json += ",";
                    ranges_json += std::format(
                        R"({{"start":{{"line":{},"character":0}},"end":{{"line":{},"character":12}}}})",
                        call_lines[i], call_lines[i]);
                }
                ranges_json += "]";
                out += std::format(
                    R"({{"from":{{"name":"{}","kind":{},"detail":"{}","uri":"{}","range":{{"start":{{"line":{},"character":0}},"end":{{"line":{},"character":20}}}},"selectionRange":{{"start":{{"line":{},"character":0}},"end":{{"line":{},"character":12}}}}}},"fromRanges":{}}})",
                    detail::json_escape(fn.name), kind, detail::json_escape(cfire::fmt::format_fn_signature(fn)),
                    detail::json_escape(doc_uri), fn_ln, fn_ln, fn_ln, fn_ln, ranges_json);
            }
        }
    }
    out += "]";
    return out;
}

inline auto Server::compute_outgoing_calls(std::string_view msg) -> std::string {
    std::size_t item_idx = detail::find_json_key(msg, "item");
    std::string_view sub = (item_idx != std::string_view::npos) ? msg.substr(item_idx) : msg;
    std::string caller_name = detail::extract_json_string(sub, "name").value_or("");
    std::string caller_uri = detail::extract_json_string(sub, "uri").value_or("");
    if (caller_name.empty() || caller_uri.empty()) return "[]";

    std::string text = get_doc_text(caller_uri);
    auto [ast_res, ws] = get_or_parse_ast(caller_uri, text);
    if (!ast_res) return "[]";

    std::vector<std::pair<std::string, int>> raw_calls;
    for (const auto& fn : ast_res->functions) {
        if (fn.name == caller_name) {
            collect_stmt_calls(fn.body, raw_calls);
            break;
        }
    }
    if (raw_calls.empty()) return "[]";

    struct CalleeGroup {
        std::string name;
        std::vector<int> call_lines;
    };
    std::vector<CalleeGroup> groups;
    std::unordered_map<std::string, std::size_t> group_idx;
    for (const auto& [callee, ln] : raw_calls) {
        int zln = std::max(0, ln - 1);
        if (auto it = group_idx.find(callee); it != group_idx.end()) {
            groups[it->second].call_lines.push_back(zln);
        } else {
            group_idx[callee] = groups.size();
            groups.push_back(CalleeGroup{callee, {zln}});
        }
    }

    auto all_cf_files = discover_workspace_cf_files(caller_uri);
    auto resolve_callee = [&](const std::string& name) -> std::optional<std::tuple<std::string, int, int, std::string>> {
        for (const auto& fn : ast_res->functions) {
            if (fn.name == name) {
                return std::make_tuple(caller_uri, std::max(0, fn.line - 1), fn.receiver ? 6 : 12, cfire::fmt::format_fn_signature(fn));
            }
        }
        for (const auto& pkg : ws.local_packages) {
            for (const auto& fn : pkg.ast.functions) {
                if (fn.name == name) {
                    return std::make_tuple(pkg.uri, std::max(0, fn.line - 1), fn.receiver ? 6 : 12, cfire::fmt::format_fn_signature(fn));
                }
            }
        }
        for (const auto& doc_uri : all_cf_files) {
            if (doc_uri == caller_uri) continue;
            if (auto doc_ast = cfire::frontend::parse_source(detail::uri_to_path(doc_uri), get_doc_text(doc_uri))) {
                for (const auto& fn : doc_ast->functions) {
                    if (fn.name == name) {
                        return std::make_tuple(doc_uri, std::max(0, fn.line - 1), fn.receiver ? 6 : 12, cfire::fmt::format_fn_signature(fn));
                    }
                }
            }
        }
        return std::nullopt;
    };

    std::string out = "[";
    bool first = true;
    for (const auto& g : groups) {
        auto resolved = resolve_callee(g.name);
        if (!resolved) continue;
        const auto& [c_uri, c_ln, c_kind, c_sig] = *resolved;
        if (!first) out += ",";
        first = false;
        std::string ranges_json = "[";
        for (std::size_t i = 0; i < g.call_lines.size(); ++i) {
            if (i > 0) ranges_json += ",";
            ranges_json += std::format(
                R"({{"start":{{"line":{},"character":0}},"end":{{"line":{},"character":12}}}})",
                g.call_lines[i], g.call_lines[i]);
        }
        ranges_json += "]";
        out += std::format(
            R"({{"to":{{"name":"{}","kind":{},"detail":"{}","uri":"{}","range":{{"start":{{"line":{},"character":0}},"end":{{"line":{},"character":20}}}},"selectionRange":{{"start":{{"line":{},"character":0}},"end":{{"line":{},"character":12}}}}}},"fromRanges":{}}})",
            detail::json_escape(g.name), c_kind, detail::json_escape(c_sig),
            detail::json_escape(c_uri), c_ln, c_ln, c_ln, c_ln, ranges_json);
    }
    out += "]";
    return out;
}

} // namespace cfire::lsp

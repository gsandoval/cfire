#include "cfire_prelude.hpp"

[[nodiscard]] auto lsp_json_escape(const std::string& s) -> std::string;
[[nodiscard]] auto lsp_json_unescape(const std::string& s) -> std::string;
[[nodiscard]] auto selfhost_make_rpc_frame(const std::string& body) -> std::string;
[[nodiscard]] auto selfhost_make_rpc_response(const std::string& id, const std::string& result_json) -> std::string;
[[nodiscard]] auto selfhost_make_rpc_notification(const std::string& method, const std::string& params_json) -> std::string;
[[nodiscard]] auto selfhost_make_rpc_error(const std::string& id, std::int32_t code, const std::string& message) -> std::string;
[[nodiscard]] auto selfhost_format_hover_markdown(const std::string& id, const std::string& markdown_content) -> std::string;
[[nodiscard]] auto selfhost_format_null_response(const std::string& id) -> std::string;
[[nodiscard]] auto selfhost_format_completion_list(const std::string& id, const std::string& items_json) -> std::string;
[[nodiscard]] auto selfhost_format_publish_diagnostics(const std::string& uri, const std::string& diagnostics_json) -> std::string;
[[nodiscard]] auto selfhost_format_initialize_response(const std::string& id) -> std::string;

[[nodiscard]] auto lsp_json_escape(const std::string& s) -> std::string {
    std::string res = "";
    std::size_t start = 0;
    std::size_t i = 0;
    auto n = s.size();
    while (i < n) {
        auto c = s[i];
        if (((((c == 34) || (c == 92)) || (c == 10)) || (c == 13)) || (c == 9)) {
            if (i > start) {
                res += cfire::slice(s, start, i);
            }
            if (c == 34) {
                res += "\\\"";
            } else {
                if (c == 92) {
                    res += "\\\\";
                } else {
                    if (c == 10) {
                        res += "\\n";
                    } else {
                        if (c == 13) {
                            res += "\\r";
                        } else {
                            if (c == 9) {
                                res += "\\t";
                            }
                        }
                    }
                }
            }
            start = (i + 1);
        }
        i = (i + 1);
    }
    if (start < n) {
        res += cfire::slice(s, start, n);
    }
    return res;
}

[[nodiscard]] auto lsp_json_unescape(const std::string& s) -> std::string {
    std::string res = "";
    std::size_t start = 0;
    std::size_t i = 0;
    auto n = s.size();
    while (i < n) {
        if ((s[i] == 92) && ((i + 1) < n)) {
            if (i > start) {
                res += cfire::slice(s, start, i);
            }
            auto next = s[(i + 1)];
            if (next == 110) {
                res += "\n";
            } else {
                if (next == 114) {
                    res += "\r";
                } else {
                    if (next == 116) {
                        res += "\t";
                    } else {
                        if (next == 34) {
                            res += "\"";
                        } else {
                            if (next == 92) {
                                res += "\\";
                            } else {
                                if (next == 47) {
                                    res += "/";
                                } else {
                                    res += cfire::slice(s, (i + 1), (i + 2));
                                }
                            }
                        }
                    }
                }
            }
            i = (i + 2);
            start = i;
        } else {
            i = (i + 1);
        }
    }
    if (start < n) {
        res += cfire::slice(s, start, n);
    }
    return res;
}

[[nodiscard]] auto selfhost_make_rpc_frame(const std::string& body) -> std::string {
    return ((("Content-Length: " + cfire::frontend::codegen_usize_to_str(body.size())) + "\r\n\r\n") + body);
}

[[nodiscard]] auto selfhost_make_rpc_response(const std::string& id, const std::string& result_json) -> std::string {
    return (((("{\"jsonrpc\":\"2.0\",\"id\":" + id) + ",\"result\":") + result_json) + "}");
}

[[nodiscard]] auto selfhost_make_rpc_notification(const std::string& method, const std::string& params_json) -> std::string {
    return (((("{\"jsonrpc\":\"2.0\",\"method\":\"" + method) + "\",\"params\":") + params_json) + "}");
}

[[nodiscard]] auto selfhost_make_rpc_error(const std::string& id, std::int32_t code, const std::string& message) -> std::string {
    return (((((("{\"jsonrpc\":\"2.0\",\"id\":" + id) + ",\"error\":{\"code\":") + cfire::frontend::codegen_to_str(code)) + ",\"message\":\"") + lsp_json_escape(message)) + "\"}}");
}

[[nodiscard]] auto selfhost_format_hover_markdown(const std::string& id, const std::string& markdown_content) -> std::string {
    return (((("{\"jsonrpc\":\"2.0\",\"id\":" + id) + ",\"result\":{\"contents\":{\"kind\":\"markdown\",\"value\":\"") + lsp_json_escape(markdown_content)) + "\"}}}");
}

[[nodiscard]] auto selfhost_format_null_response(const std::string& id) -> std::string {
    return (("{\"jsonrpc\":\"2.0\",\"id\":" + id) + ",\"result\":null}");
}

[[nodiscard]] auto selfhost_format_completion_list(const std::string& id, const std::string& items_json) -> std::string {
    return (((("{\"jsonrpc\":\"2.0\",\"id\":" + id) + ",\"result\":{\"isIncomplete\":false,\"items\":") + items_json) + "}}");
}

[[nodiscard]] auto selfhost_format_publish_diagnostics(const std::string& uri, const std::string& diagnostics_json) -> std::string {
    return (((("{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/publishDiagnostics\",\"params\":{\"uri\":\"" + uri) + "\",\"diagnostics\":") + diagnostics_json) + "}}");
}

[[nodiscard]] auto selfhost_format_initialize_response(const std::string& id) -> std::string {
    return (("{\"jsonrpc\":\"2.0\",\"id\":" + id) + ",\"result\":{\"capabilities\":{\"textDocumentSync\":1,\"hoverProvider\":true,\"definitionProvider\":true,\"typeDefinitionProvider\":true,\"referencesProvider\":true,\"renameProvider\":{\"prepareProvider\":true},\"codeActionProvider\":true,\"documentFormattingProvider\":true,\"documentRangeFormattingProvider\":true,\"documentSymbolProvider\":true,\"workspaceSymbolProvider\":true,\"inlayHintProvider\":true,\"completionProvider\":{\"triggerCharacters\":[\".\",\":\",\"@\"]},\"signatureHelpProvider\":{\"triggerCharacters\":[\"(\",\",\"]},\"semanticTokensProvider\":{\"legend\":{\"tokenTypes\":[\"type\",\"struct\",\"enum\",\"interface\",\"parameter\",\"variable\",\"property\",\"enumMember\",\"function\",\"method\",\"keyword\",\"modifier\",\"string\",\"number\",\"operator\",\"namespace\"],\"tokenModifiers\":[\"declaration\",\"definition\",\"readonly\",\"modification\",\"defaultLibrary\",\"async\"]},\"full\":true}}},\"serverInfo\":{\"name\":\"cfire-lsp\",\"version\":\"1.3.0\"}}}");
}

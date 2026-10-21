#pragma once
// src/lsp/server.hpp — Full-Featured Language Server Protocol (LSP 3.17) Server (`cfire::lsp::Server`)

#include "json_rpc.hpp"
#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <format>
#include <fstream>
#include <functional>
#include <iostream>
#include <map>
#include <mutex>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace cfire::lsp {

class Server {
public:
    struct PendingDiagRequest {
        std::string uri;
        std::string text;
        std::chrono::steady_clock::time_point deadline;
    };

    explicit Server(int debounce_ms = 150)
        : debounce_duration_(std::chrono::milliseconds(debounce_ms)) {}

    ~Server() {
        stop_worker();
    }

    void send_rpc(std::ostream& out, std::string_view body) {
        std::lock_guard<std::mutex> lock(out_mutex_);
        detail::write_rpc_message(out, body);
    }

    void send_rpc_async(std::string_view body) {
        std::lock_guard<std::mutex> lock(out_mutex_);
        if (out_stream_) {
            detail::write_rpc_message(*out_stream_, body);
        }
    }

    void start_worker() {
        if (worker_running_.load()) return;
        stop_requested_ = false;
        worker_running_ = true;
        worker_thread_ = std::thread(&Server::worker_loop, this);
    }

    void stop_worker() {
        if (!worker_running_.load()) return;
        {
            std::lock_guard<std::mutex> lock(diag_mutex_);
            stop_requested_ = true;
        }
        diag_cv_.notify_all();
        if (worker_thread_.joinable()) {
            worker_thread_.join();
        }
        worker_running_ = false;
        stop_requested_ = false;
    }

    void schedule_diagnostics(const std::string& uri, const std::string& text, bool immediate) {
        if (!worker_running_.load()) {
            start_worker();
        }
        {
            std::lock_guard<std::mutex> lock(diag_mutex_);
            auto now = std::chrono::steady_clock::now();
            auto deadline = immediate ? now : (now + debounce_duration_);
            pending_diags_[uri] = PendingDiagRequest{uri, text, deadline};
        }
        diag_cv_.notify_one();
    }

    void handle_stream(std::istream& in, std::ostream& out) {
        {
            std::lock_guard<std::mutex> lock(out_mutex_);
            out_stream_ = &out;
        }
        start_worker();

        std::string line;
        while (std::getline(in, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.starts_with("Content-Length:")) {
                std::size_t len = std::stoul(line.substr(15));
                while (std::getline(in, line)) {
                    if (!line.empty() && line.back() == '\r') line.pop_back();
                    if (line.empty()) break;
                }
                std::string payload(len, '\0');
                in.read(payload.data(), static_cast<std::streamsize>(len));
                if (!handle_message(payload, out)) {
                    break;
                }
            }
        }

        stop_worker();
        {
            std::lock_guard<std::mutex> lock(out_mutex_);
            out_stream_ = nullptr;
        }
    }

    bool handle_message(std::string_view msg, std::ostream& out) {
        {
            std::lock_guard<std::mutex> lock(out_mutex_);
            if (!out_stream_) out_stream_ = &out;
        }
        auto method_opt = detail::extract_json_string(msg, "method");
        auto id_opt = detail::extract_json_id(msg);
        std::string method = method_opt.value_or("");

        if (method == "initialize") {
            if (auto root_uri = detail::extract_json_string(msg, "rootUri")) {
                workspace_root_ = detail::uri_to_path(*root_uri);
            } else if (auto root_path = detail::extract_json_string(msg, "rootPath")) {
                workspace_root_ = *root_path;
            }
            if (auto snip = detail::extract_json_bool(msg, "snippetSupport")) {
                client_snippet_support_ = *snip;
            }
            std::string id = id_opt.value_or("1");
            std::string resp = std::format(
                R"({{"jsonrpc":"2.0","id":{},"result":{{"capabilities":{{"textDocumentSync":1,"hoverProvider":true,"definitionProvider":true,"typeDefinitionProvider":true,"referencesProvider":true,"renameProvider":{{"prepareProvider":true}},"codeActionProvider":true,"documentFormattingProvider":true,"documentRangeFormattingProvider":true,"documentSymbolProvider":true,"workspaceSymbolProvider":true,"inlayHintProvider":true,"completionProvider":{{"triggerCharacters":[".",":","@"]}},"signatureHelpProvider":{{"triggerCharacters":["(",","]}},"semanticTokensProvider":{{"legend":{{"tokenTypes":["type","struct","enum","interface","parameter","variable","property","enumMember","function","method","keyword","modifier","string","number","operator","namespace"],"tokenModifiers":["declaration","definition","readonly","modification","defaultLibrary","async"]}},"full":true}}}},"serverInfo":{{"name":"cfire-lsp","version":"1.3.0"}}}}}})",
                id);
            send_rpc(out, resp);
            return true;
        }
        if (method == "initialized") {
            return true;
        }
        if (method == "shutdown") {
            std::string id = id_opt.value_or("1");
            send_rpc(out, std::format(R"({{"jsonrpc":"2.0","id":{},"result":null}})", id));
            return true;
        }
        if (method == "exit") {
            return false;
        }

        if (method == "textDocument/didOpen" || method == "textDocument/didChange" || method == "textDocument/didSave") {
            auto uri = detail::extract_json_string(msg, "uri").value_or("");
            auto text = detail::extract_json_string(msg, "text");
            std::string content;
            {
                std::lock_guard<std::mutex> lock(docs_mutex_);
                if (text) {
                    docs_[uri] = *text;
                    content = *text;
                } else if (docs_.find(uri) == docs_.end()) {
                    std::ifstream ifs(detail::uri_to_path(uri));
                    std::stringstream ss;
                    ss << ifs.rdbuf();
                    docs_[uri] = ss.str();
                    content = docs_[uri];
                } else {
                    content = docs_[uri];
                }
            }
            bool immediate = (method != "textDocument/didChange");
            schedule_diagnostics(uri, content, immediate);
            return true;
        }

        if (method == "textDocument/hover") {
            std::string id = id_opt.value_or("1");
            auto uri = detail::extract_json_string(msg, "uri").value_or("");
            std::size_t pos_idx = detail::find_json_key(msg, "position");
            int line = detail::extract_json_int_after(msg, "line", pos_idx != std::string_view::npos ? pos_idx : 0).value_or(0);
            int character = detail::extract_json_int_after(msg, "character", pos_idx != std::string_view::npos ? pos_idx : 0).value_or(0);
            std::string hover_md = compute_hover(uri, line, character);
            if (hover_md.empty()) {
                send_rpc(out, std::format(R"({{"jsonrpc":"2.0","id":{},"result":null}})", id));
            } else {
                std::string resp = std::format(
                    R"({{"jsonrpc":"2.0","id":{},"result":{{"contents":{{"kind":"markdown","value":"{}"}}}}}})",
                    id, detail::json_escape(hover_md));
                send_rpc(out, resp);
            }
            return true;
        }

        if (method == "textDocument/signatureHelp") {
            std::string id = id_opt.value_or("1");
            auto uri = detail::extract_json_string(msg, "uri").value_or("");
            std::size_t pos_idx = detail::find_json_key(msg, "position");
            int line = detail::extract_json_int_after(msg, "line", pos_idx != std::string_view::npos ? pos_idx : 0).value_or(0);
            int character = detail::extract_json_int_after(msg, "character", pos_idx != std::string_view::npos ? pos_idx : 0).value_or(0);
            std::string sig_json = compute_signature_help(uri, line, character);
            send_rpc(out, std::format(R"({{"jsonrpc":"2.0","id":{},"result":{}}})", id, sig_json));
            return true;
        }

        if (method == "textDocument/inlayHint") {
            std::string id = id_opt.value_or("1");
            auto uri = detail::extract_json_string(msg, "uri").value_or("");
            std::string hints_json = compute_inlay_hints(uri);
            send_rpc(out, std::format(R"({{"jsonrpc":"2.0","id":{},"result":{}}})", id, hints_json));
            return true;
        }

        if (method == "textDocument/completion") {
            std::string id = id_opt.value_or("1");
            auto uri = detail::extract_json_string(msg, "uri").value_or("");
            std::size_t pos_idx = detail::find_json_key(msg, "position");
            int line = detail::extract_json_int_after(msg, "line", pos_idx != std::string_view::npos ? pos_idx : 0).value_or(0);
            int character = detail::extract_json_int_after(msg, "character", pos_idx != std::string_view::npos ? pos_idx : 0).value_or(0);
            std::string items_json = compute_completions(uri, line, character);
            std::string resp = std::format(
                R"({{"jsonrpc":"2.0","id":{},"result":{{"isIncomplete":false,"items":{}}}}})",
                id, items_json);
            send_rpc(out, resp);
            return true;
        }

        if (method == "textDocument/definition") {
            std::string id = id_opt.value_or("1");
            auto uri = detail::extract_json_string(msg, "uri").value_or("");
            std::size_t pos_idx = detail::find_json_key(msg, "position");
            int line = detail::extract_json_int_after(msg, "line", pos_idx != std::string_view::npos ? pos_idx : 0).value_or(0);
            int character = detail::extract_json_int_after(msg, "character", pos_idx != std::string_view::npos ? pos_idx : 0).value_or(0);
            std::string loc_json = compute_definition(uri, line, character);
            send_rpc(out, std::format(R"({{"jsonrpc":"2.0","id":{},"result":{}}})", id, loc_json));
            return true;
        }

        if (method == "textDocument/typeDefinition") {
            std::string id = id_opt.value_or("1");
            auto uri = detail::extract_json_string(msg, "uri").value_or("");
            std::size_t pos_idx = detail::find_json_key(msg, "position");
            int line = detail::extract_json_int_after(msg, "line", pos_idx != std::string_view::npos ? pos_idx : 0).value_or(0);
            int character = detail::extract_json_int_after(msg, "character", pos_idx != std::string_view::npos ? pos_idx : 0).value_or(0);
            std::string loc_json = compute_type_definition(uri, line, character);
            send_rpc(out, std::format(R"({{"jsonrpc":"2.0","id":{},"result":{}}})", id, loc_json));
            return true;
        }

        if (method == "textDocument/references") {
            std::string id = id_opt.value_or("1");
            auto uri = detail::extract_json_string(msg, "uri").value_or("");
            std::size_t pos_idx = detail::find_json_key(msg, "position");
            int line = detail::extract_json_int_after(msg, "line", pos_idx != std::string_view::npos ? pos_idx : 0).value_or(0);
            int character = detail::extract_json_int_after(msg, "character", pos_idx != std::string_view::npos ? pos_idx : 0).value_or(0);
            std::string refs_json = compute_references(uri, line, character);
            send_rpc(out, std::format(R"({{"jsonrpc":"2.0","id":{},"result":{}}})", id, refs_json));
            return true;
        }

        if (method == "textDocument/prepareRename") {
            std::string id = id_opt.value_or("1");
            auto uri = detail::extract_json_string(msg, "uri").value_or("");
            std::size_t pos_idx = detail::find_json_key(msg, "position");
            int line = detail::extract_json_int_after(msg, "line", pos_idx != std::string_view::npos ? pos_idx : 0).value_or(0);
            int character = detail::extract_json_int_after(msg, "character", pos_idx != std::string_view::npos ? pos_idx : 0).value_or(0);
            std::string prep_json = compute_prepare_rename(uri, line, character);
            send_rpc(out, std::format(R"({{"jsonrpc":"2.0","id":{},"result":{}}})", id, prep_json));
            return true;
        }

        if (method == "textDocument/rename") {
            std::string id = id_opt.value_or("1");
            auto uri = detail::extract_json_string(msg, "uri").value_or("");
            std::size_t pos_idx = detail::find_json_key(msg, "position");
            int line = detail::extract_json_int_after(msg, "line", pos_idx != std::string_view::npos ? pos_idx : 0).value_or(0);
            int character = detail::extract_json_int_after(msg, "character", pos_idx != std::string_view::npos ? pos_idx : 0).value_or(0);
            std::string new_name = detail::extract_json_string(msg, "newName").value_or("");
            std::string edit_json = compute_rename(uri, line, character, new_name);
            send_rpc(out, std::format(R"({{"jsonrpc":"2.0","id":{},"result":{}}})", id, edit_json));
            return true;
        }

        if (method == "textDocument/codeAction") {
            std::string id = id_opt.value_or("1");
            auto uri = detail::extract_json_string(msg, "uri").value_or("");
            std::size_t range_idx = detail::find_json_key(msg, "range");
            int start_line = detail::extract_json_int_after(msg, "line", range_idx != std::string_view::npos ? range_idx : 0).value_or(-1);
            std::string actions_json = compute_code_actions(uri, start_line);
            send_rpc(out, std::format(R"({{"jsonrpc":"2.0","id":{},"result":{}}})", id, actions_json));
            return true;
        }

        if (method == "textDocument/documentSymbol") {
            std::string id = id_opt.value_or("1");
            auto uri = detail::extract_json_string(msg, "uri").value_or("");
            std::string syms_json = compute_document_symbols(uri);
            send_rpc(out, std::format(R"({{"jsonrpc":"2.0","id":{},"result":{}}})", id, syms_json));
            return true;
        }

        if (method == "workspace/symbol") {
            std::string id = id_opt.value_or("1");
            std::string query = detail::extract_json_string(msg, "query").value_or("");
            std::string syms_json = compute_workspace_symbols(query);
            send_rpc(out, std::format(R"({{"jsonrpc":"2.0","id":{},"result":{}}})", id, syms_json));
            return true;
        }

        if (method == "textDocument/formatting") {
            std::string id = id_opt.value_or("1");
            auto uri = detail::extract_json_string(msg, "uri").value_or("");
            std::string edits_json = compute_formatting(uri);
            send_rpc(out, std::format(R"({{"jsonrpc":"2.0","id":{},"result":{}}})", id, edits_json));
            return true;
        }

        if (method == "textDocument/rangeFormatting") {
            std::string id = id_opt.value_or("1");
            auto uri = detail::extract_json_string(msg, "uri").value_or("");
            std::size_t range_idx = detail::find_json_key(msg, "range");
            std::size_t start_idx = range_idx != std::string_view::npos ? detail::find_json_key(msg, "start", range_idx) : std::string_view::npos;
            std::size_t end_idx = range_idx != std::string_view::npos ? detail::find_json_key(msg, "end", range_idx) : std::string_view::npos;
            int start_line = detail::extract_json_int_after(msg, "line", start_idx != std::string_view::npos ? start_idx : 0).value_or(0);
            int end_line = detail::extract_json_int_after(msg, "line", end_idx != std::string_view::npos ? end_idx : 0).value_or(start_line);
            std::string edits_json = compute_range_formatting(uri, start_line, end_line);
            send_rpc(out, std::format(R"({{"jsonrpc":"2.0","id":{},"result":{}}})", id, edits_json));
            return true;
        }

        if (method == "textDocument/semanticTokens/full") {
            std::string id = id_opt.value_or("1");
            auto uri = detail::extract_json_string(msg, "uri").value_or("");
            std::string data_json = compute_semantic_tokens(uri);
            send_rpc(out, std::format(R"({{"jsonrpc":"2.0","id":{},"result":{{"data":{}}}}})", id, data_json));
            return true;
        }

        if (id_opt) {
            send_rpc(out, std::format(R"({{"jsonrpc":"2.0","id":{},"result":null}})", *id_opt));
        }
        return true;
    }

private:
    std::chrono::milliseconds debounce_duration_{150};
    std::thread worker_thread_;
    std::mutex diag_mutex_;
    std::condition_variable diag_cv_;
    std::atomic<bool> stop_requested_{false};
    std::atomic<bool> worker_running_{false};
    std::unordered_map<std::string, PendingDiagRequest> pending_diags_;

    std::mutex out_mutex_;
    std::ostream* out_stream_{nullptr};

    std::mutex docs_mutex_;
    std::mutex diag_store_mutex_;
    std::mutex cpp_cache_mutex_;

    std::unordered_map<std::string, std::string> docs_;
    std::unordered_map<std::string, std::vector<detail::DiagnosticEntry>> last_diagnostics_;
    std::unordered_map<std::string, cfire::bridge::HeaderIndex> cpp_header_cache_;
    std::unordered_map<std::string, cfire::frontend::FileAST> last_good_ast_;
    std::unordered_map<std::string, detail::WorkspaceContext> last_good_ws_;
    std::string workspace_root_;
    bool client_snippet_support_{true};

public:
    void set_workspace_root(std::string root) { workspace_root_ = std::move(root); }

    auto get_doc_text(const std::string& uri) -> std::string {
        std::lock_guard<std::mutex> lock(docs_mutex_);
        if (auto it = docs_.find(uri); it != docs_.end()) {
            return it->second;
        }
        std::string path = detail::uri_to_path(uri);
        if (std::filesystem::exists(path)) {
            std::ifstream ifs(path);
            std::stringstream ss;
            ss << ifs.rdbuf();
            docs_[uri] = ss.str();
            return docs_[uri];
        }
        return "";
    }

    // Resolves local `.cf` package imports (`import "./pkg/checksum"`) and `import cpp "..."` headers for `ast`
    auto resolve_workspace(const std::string& uri, const cfire::frontend::FileAST& ast) -> detail::WorkspaceContext {
        detail::WorkspaceContext ws;
        std::filesystem::path file_path = detail::uri_to_path(uri);
        std::filesystem::path base_dir = file_path.parent_path();
        if (base_dir.empty()) base_dir = std::filesystem::current_path();

        auto collect_imports = [&](const cfire::frontend::FileAST& src_ast, const std::filesystem::path& src_dir) {
            for (const auto& imp : src_ast.imports) {
                if (!imp.is_cpp && (imp.path.starts_with("./") || imp.path.starts_with("../"))) {
                    std::filesystem::path target = src_dir / imp.path;
                    std::vector<std::filesystem::path> cf_files;
                    std::error_code ec;
                    if (std::filesystem::is_directory(target, ec)) {
                        for (const auto& entry : std::filesystem::directory_iterator(target, ec)) {
                            if (entry.is_regular_file() && entry.path().extension() == ".cf") {
                                cf_files.push_back(entry.path());
                            }
                        }
                    } else if (std::filesystem::exists(target.string() + ".cf", ec)) {
                        cf_files.push_back(target.string() + ".cf");
                    } else if (std::filesystem::exists(target, ec)) {
                        cf_files.push_back(target);
                    }
                    for (const auto& fpath : cf_files) {
                        std::string sub_uri = detail::path_to_uri(fpath);
                        std::string sub_text = get_doc_text(sub_uri);
                        if (auto sub_ast = cfire::frontend::parse_source(fpath.string(), sub_text)) {
                            detail::ImportedPkgFile pkg;
                            pkg.alias = !imp.alias.empty() ? imp.alias : sub_ast->package_name;
                            pkg.import_path = imp.path;
                            pkg.file_path = fpath.string();
                            pkg.uri = sub_uri;
                            pkg.ast = std::move(*sub_ast);
                            ws.local_packages.push_back(std::move(pkg));
                        }
                    }
                } else if (imp.is_cpp) {
                    std::vector<std::string> cflags = {"-std=c++2b"};
                    // Check sibling or parent `build.cf` for local C/C++ include directories
                    std::filesystem::path search_dir = src_dir;
                    for (int depth = 0; depth < 3 && !search_dir.empty(); ++depth) {
                        auto bcf = search_dir / "build.cf";
                        std::error_code ec;
                        if (std::filesystem::exists(bcf, ec)) {
                            std::string btext = get_doc_text(detail::path_to_uri(bcf));
                            if (auto bast = cfire::frontend::parse_source(bcf.string(), btext)) {
                                for (const auto& gv : bast->global_vars) {
                                    if (!gv.value) continue;
                                    if (const auto* call = std::get_if<cfire::frontend::CallExpr>(&gv.value->node)) {
                                        if (!call->args.empty() && call->args[0].value) {
                                            if (const auto* lit = std::get_if<cfire::frontend::LiteralExpr>(&call->args[0].value->node)) {
                                                auto inc_dir = search_dir / lit->value;
                                                if (std::filesystem::exists(inc_dir, ec)) {
                                                    cflags.push_back("-I" + inc_dir.string());
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                            break;
                        }
                        if (search_dir == search_dir.parent_path()) break;
                        search_dir = search_dir.parent_path();
                    }
                    for (const auto& attr : imp.attrs) {
                        if (attr.name == "pkg" && !attr.args.empty()) {
                            int rc = 0;
                            std::string pc_out = cfire::bridge::run_command_capture("pkg-config --cflags " + attr.args[0], rc);
                            if (rc == 0) {
                                std::istringstream iss(pc_out);
                                std::string tok;
                                while (iss >> tok) cflags.push_back(tok);
                            }
                        }
                    }
                    std::string cache_key = imp.path;
                    for (const auto& f : cflags) cache_key += "|" + f;
                    {
                        std::lock_guard<std::mutex> lock(cpp_cache_mutex_);
                        auto it = cpp_header_cache_.find(cache_key);
                        if (it == cpp_header_cache_.end()) {
                            auto cache_dir = src_dir / ".cfire" / "cache";
                            if (auto idx = cfire::bridge::inspect_header(imp.path, cflags, cache_dir)) {
                                it = cpp_header_cache_.emplace(cache_key, std::move(*idx)).first;
                            }
                        }
                        if (it != cpp_header_cache_.end()) {
                            for (const auto& [sname, sym] : it->second.symbols) {
                                if (sym.kind == "FunctionDecl") {
                                    ws.cpp_symbols[sname] = sym;
                                    ws.cpp_symbol_header[sname] = imp.path;
                                }
                            }
                        }
                    }
                }
            }
        };

        collect_imports(ast, base_dir);
        // Also collect C/C++ imports inside imported local packages!
        std::size_t pkg_count = ws.local_packages.size();
        for (std::size_t i = 0; i < pkg_count; ++i) {
            std::filesystem::path pkg_dir = std::filesystem::path(ws.local_packages[i].file_path).parent_path();
            collect_imports(ws.local_packages[i].ast, pkg_dir);
        }
        return ws;
    }

    // Resilient AST resolution: attempts parse, recovers trailing dot completions via dummy token, falls back to last-good AST
    auto get_or_parse_ast(const std::string& uri, const std::string& text, int line = -1, int col = -1)
        -> std::pair<std::optional<cfire::frontend::FileAST>, detail::WorkspaceContext>
    {
        (void)line;
        (void)col;
        std::string path = detail::uri_to_path(uri);
        auto pres = cfire::frontend::parse_source_resilient(path, text);
        if (pres.ok() || !pres.ast.functions.empty() || !pres.ast.structs.empty() || !pres.ast.enums.empty()) {
            auto ws = resolve_workspace(uri, pres.ast);
            last_good_ast_[uri] = pres.ast;
            last_good_ws_[uri] = ws;
            return {pres.ast, ws};
        }

        // Recovery 1: If file has trailing dot(s) or colons, patch them with placeholders
        {
            auto lines = detail::split_lines(text);
            bool modified = false;
            for (auto& ln : lines) {
                auto end_pos = ln.find_last_not_of(" \t\r\n");
                if (end_pos != std::string::npos) {
                    if (ln[end_pos] == '.') {
                        ln.insert(end_pos + 1, "__cfire_placeholder__");
                        modified = true;
                    } else if (end_pos >= 1 && ln[end_pos] == ':' && ln[end_pos - 1] == ':') {
                        ln.insert(end_pos + 1, "__cfire_placeholder__");
                        modified = true;
                    }
                }
            }
            if (modified) {
                std::string patched_text;
                for (std::size_t i = 0; i < lines.size(); ++i) {
                    if (i > 0) patched_text += "\n";
                    patched_text += lines[i];
                }
                auto patched_ast = cfire::frontend::parse_source(path, patched_text);
                if (patched_ast) {
                    auto ws = resolve_workspace(uri, *patched_ast);
                    return {*patched_ast, ws};
                }
            }
        }

        // Recovery 2: Fall back to last known valid AST
        if (auto it = last_good_ast_.find(uri); it != last_good_ast_.end()) {
            return {it->second, last_good_ws_[uri]};
        }

        return {std::nullopt, {}};
    }

    auto compute_diagnostics_for_doc(const std::string& uri, const std::string& text) -> std::vector<detail::DiagnosticEntry> {
        using namespace cfire::frontend;
        std::vector<detail::DiagnosticEntry> entries;
        std::string file_path = detail::uri_to_path(uri);
        auto pres = parse_source_resilient(file_path, text);

        auto parse_err = [&](const std::string& err_str, std::string_view fallback_code) -> detail::DiagnosticEntry {
            int ln = 0;
            int col = 0;
            std::string code{fallback_code};
            if (auto c1 = err_str.find(':', file_path.size()); c1 != std::string::npos) {
                if (auto c2 = err_str.find(':', c1 + 1); c2 != std::string::npos) {
                    if (auto c3 = err_str.find(':', c2 + 1); c3 != std::string::npos) {
                        try {
                            ln = std::max(0, std::stoi(err_str.substr(c1 + 1, c2 - c1 - 1)) - 1);
                            col = std::max(0, std::stoi(err_str.substr(c2 + 1, c3 - c2 - 1)) - 1);
                        } catch (...) {}
                    }
                }
            }
            if (auto p = err_str.find("cfire/"); p != std::string::npos) {
                std::size_t e = p + 6;
                while (e < err_str.size() && (std::isalnum(static_cast<unsigned char>(err_str[e])) || err_str[e] == '-')) ++e;
                code = err_str.substr(p, e - p);
            }
            return detail::DiagnosticEntry{ln, col, code, err_str};
        };

        if (!pres.ok()) {
            for (const auto& err : pres.errors) {
                entries.push_back(parse_err(err, "cfire/SYNTAX"));
            }
        } else {
            // Merge local package declarations before semantic analysis
            auto ws = resolve_workspace(uri, pres.ast);
            FileAST merged = pres.ast;
            for (const auto& pkg : ws.local_packages) {
                if (pkg.ast.arena) merged.child_arenas.push_back(pkg.ast.arena);
                merged.child_arenas.insert(merged.child_arenas.end(),
                    pkg.ast.child_arenas.begin(), pkg.ast.child_arenas.end());
                for (const auto& st : pkg.ast.structs) merged.structs.push_back(st);
                for (const auto& en : pkg.ast.enums) merged.enums.push_back(en);
                for (const auto& ifc : pkg.ast.interfaces) merged.interfaces.push_back(ifc);
                for (const auto& fn : pkg.ast.functions) merged.functions.insert(merged.functions.begin(), fn);
            }
            auto checked = analyze(std::move(merged));
            if (!checked) {
                entries.push_back(parse_err(checked.error(), "cfire/SEMA"));
            }
        }
        {
            std::lock_guard<std::mutex> lock(diag_store_mutex_);
            last_diagnostics_[uri] = entries;
        }
        return entries;
    }

    void run_and_publish_diagnostics(const std::string& uri, const std::string& text) {
        auto entries = compute_diagnostics_for_doc(uri, text);
        std::string diags_json = "[";
        for (std::size_t i = 0; i < entries.size(); ++i) {
            if (i > 0) diags_json += ",";
            const auto& d = entries[i];
            diags_json += std::format(
                R"({{"range":{{"start":{{"line":{},"character":{}}},"end":{{"line":{},"character":{}}}}},"severity":1,"code":"{}","source":"cfire","message":"{}"}})",
                d.line, d.col, d.line, d.col + 10, detail::json_escape(d.code), detail::json_escape(d.message));
        }
        diags_json += "]";

        std::string notif = std::format(
            R"({{"jsonrpc":"2.0","method":"textDocument/publishDiagnostics","params":{{"uri":"{}","diagnostics":{}}}}})",
            detail::json_escape(uri), diags_json);
        send_rpc_async(notif);
    }

    void publish_diagnostics(const std::string& uri, const std::string& text, std::ostream& out) {
        auto entries = compute_diagnostics_for_doc(uri, text);
        std::string diags_json = "[";
        for (std::size_t i = 0; i < entries.size(); ++i) {
            if (i > 0) diags_json += ",";
            const auto& d = entries[i];
            diags_json += std::format(
                R"({{"range":{{"start":{{"line":{},"character":{}}},"end":{{"line":{},"character":{}}}}},"severity":1,"code":"{}","source":"cfire","message":"{}"}})",
                d.line, d.col, d.line, d.col + 10, detail::json_escape(d.code), detail::json_escape(d.message));
        }
        diags_json += "]";

        std::string notif = std::format(
            R"({{"jsonrpc":"2.0","method":"textDocument/publishDiagnostics","params":{{"uri":"{}","diagnostics":{}}}}})",
            detail::json_escape(uri), diags_json);
        send_rpc(out, notif);
    }

    void worker_loop() {
        while (true) {
            PendingDiagRequest current_req;
            bool has_req = false;
            {
                std::unique_lock<std::mutex> lock(diag_mutex_);
                while (!stop_requested_.load() && pending_diags_.empty()) {
                    diag_cv_.wait(lock);
                }

                if (stop_requested_.load()) {
                    if (!pending_diags_.empty()) {
                        auto it = pending_diags_.begin();
                        current_req = std::move(it->second);
                        pending_diags_.erase(it);
                        has_req = true;
                    } else {
                        break;
                    }
                } else {
                    auto now = std::chrono::steady_clock::now();
                    auto earliest_it = pending_diags_.end();
                    auto min_deadline = std::chrono::steady_clock::time_point::max();
                    for (auto it = pending_diags_.begin(); it != pending_diags_.end(); ++it) {
                        if (it->second.deadline < min_deadline) {
                            min_deadline = it->second.deadline;
                            earliest_it = it;
                        }
                    }

                    if (earliest_it != pending_diags_.end()) {
                        if (now < min_deadline) {
                            diag_cv_.wait_until(lock, min_deadline);
                            continue;
                        }
                        current_req = std::move(earliest_it->second);
                        pending_diags_.erase(earliest_it);
                        has_req = true;
                    }
                }
            }

            if (has_req) {
                run_and_publish_diagnostics(current_req.uri, current_req.text);
            }
        }
    }

    auto compute_hover(const std::string& uri, int line, int character) -> std::string {
        std::string text = get_doc_text(uri);
        auto lines = detail::split_lines(text);
        auto sym = detail::symbol_at_position(lines, line, character);

        // 0A. Check if cursor is on or inside a struct tag (`` `json:"..."` ``, `` `cli:"..."` ``, `` `db:"..."` ``)
        if (line >= 0 && static_cast<std::size_t>(line) < lines.size()) {
            const std::string& ln = lines[static_cast<std::size_t>(line)];
            auto b1 = ln.find('`');
            if (b1 != std::string::npos && character >= static_cast<int>(b1)) {
                auto b2 = ln.find('`', b1 + 1);
                if (b2 != std::string::npos && character <= static_cast<int>(b2) + 1) {
                    std::string tag_content = ln.substr(b1, b2 - b1 + 1);
                    std::string desc;
                    if (tag_content.contains("json:")) {
                        desc += "### Struct Tag `` `json:\"<key>\"` ``\n"
                                "Specifies the JSON object property key for compile-time reflection in `core:json` (`json.parse_struct[T]` / `json.stringify_struct`).\n\n"
                                "**Example:**\n```cf\nService: str `json:\"service\"`\n```\n";
                    }
                    if (tag_content.contains("cli:") || tag_content.contains("help:")) {
                        if (!desc.empty()) desc += "\n---\n\n";
                        desc += "### Struct Tag `` `cli:\"--flag, -f\" help:\"...\"` ``\n"
                                "Declares command-line argument mapping for `core:cli` (`cli.parse[Options]()`).\n"
                                "Binds long/short flags and auto-generates `--help` documentation.\n\n"
                                "**Example:**\n```cf\nInput: str `cli:\"--input, -i\" help:\"Path to input log file\"`\n```\n";
                    }
                    if (tag_content.contains("db:")) {
                        if (!desc.empty()) desc += "\n---\n\n";
                        desc += "### Struct Tag `` `db:\"<column>\"` ``\n"
                                "Maps this struct field to an SQLite database column for `core:db/sqlite` (`sqlite.map_row[T]`).\n\n"
                                "**Example:**\n```cf\nId: i64 `db:\"id\"`\n```\n";
                    }
                    if (!desc.empty()) return desc;
                }
            }
        }

        // 0B. Check if cursor is on `@pkg` attribute
        if (sym.name == "@pkg" || sym.name == "pkg") {
            if (line >= 0 && static_cast<std::size_t>(line) < lines.size() && lines[static_cast<std::size_t>(line)].contains("@pkg")) {
                return "### Attribute `@pkg(\"<name>\")`\n"
                       "Declares an external C/C++ library dependency resolved automatically via `pkg-config`.\n\n"
                       "The build system automatically runs `pkg-config --cflags <name>` and `pkg-config --libs <name>` "
                       "to link the native library and inject headers into the **Clang AST Header Bridge**.\n\n"
                       "**Example:**\n```cf\nimport cpp \"zstd.h\" @pkg(\"libzstd\")\n```";
            }
        }

        // 0C. Check built-in functions & keywords
        if (sym.qualifier.empty()) {
            if (sym.name == "println") {
                return "### Built-in Function `println(format, ...args)`\n```cf\nprintln(format: str, ...args: Any) -> void\n```\n"
                       "Formats and writes text to `stdout` with a trailing newline. Powered by ISO C++23 `std::println`.\n\n"
                       "**Example:**\n```cf\nprintln(\"Hello {}, count: {}\", name, 42)\n```";
            }
            if (sym.name == "print") {
                return "### Built-in Function `print(format, ...args)`\n```cf\nprint(format: str, ...args: Any) -> void\n```\n"
                       "Formats and writes text to `stdout` without a trailing newline. Powered by ISO C++23 `std::print`.\n\n"
                       "**Example:**\n```cf\nprint(\"Processing... \")\n```";
            }
            if (sym.name == "panic") {
                return "### Built-in Function `panic(msg)`\n```cf\npanic(msg: str) -> void\n```\n"
                       "Aborts the current process immediately with a formatted error message to `stderr`.\n\n"
                       "**Example:**\n```cf\npanic(\"unreachable state reached\")\n```";
            }
            if (sym.name == "assert") {
                return "### Built-in Function `assert(condition, message)`\n```cf\nassert(cond: bool, msg: str = \"\") -> void\n```\n"
                       "Evaluates `cond` and aborts execution if false.\n\n"
                       "**Example:**\n```cf\nassert(capacity >= 1, \"capacity must be positive\")\n```";
            }
            if (sym.name == "make") {
                return "### Built-in Allocator `make[T](...args)`\n```cf\nmake[own[T]](args...) -> own[T]\nmake[shared[T]](args...) -> shared[T]\nmake[sync.Channel[T]](capacity: usize) -> sync.Channel[T]\n```\n"
                       "Allocates a managed smart pointer on the heap (`std::make_unique` for `own[T]`, `std::make_shared` for `shared[T]`), or initializes a bounded/unbounded channel.\n\n"
                       "**Example:**\n```cf\nbox := make[own[Node]](Node{Id: 1})\n```";
            }
            if (sym.name == "defer") {
                return "### Keyword `defer`\n"
                       "Schedules an expression or statement to execute when the enclosing function or block exits (LIFO order).\n\n"
                       "Lowers 1-to-1 to `cfire::scope_exit` in C++23.\n\n"
                       "**Example:**\n```cf\ndefer println(\"Cleaned up session\")\n```";
            }
            if (sym.name == "cpp") {
                return "### Block `cpp { ... }`\n"
                       "Directly embeds raw ISO C++23 code into the generated output without syntax translation, providing zero-overhead interoperability.\n\n"
                       "**Example:**\n```cf\ncpp {\n    std::println(\"Direct native C++23 call\");\n}\n```";
            }
        }

        // 1. Check `core:*` stdlib catalog (either `mod.fn` or `mod`)
        if (!sym.name.empty() || !sym.qualifier.empty()) {
            for (const auto& mod : cfire::doc::stdlib_catalog()) {
                if (sym.qualifier == mod.ns_alias) {
                    for (const auto& fn : mod.functions) {
                        if (fn.name == sym.name) {
                            return std::format("### `{}.{}` (`{}`)\n```cf\n{}\n```\n**Lowered C++23:** `{}`\n\n{}\n\n**Example:**\n```cf\n{}\n```",
                                mod.ns_alias, fn.name, mod.import_path, fn.cf_sig, fn.cpp_lowering, fn.summary, fn.example);
                        }
                    }
                }
                if (sym.qualifier.empty() && sym.name == mod.ns_alias) {
                    std::string fns_list;
                    for (const auto& fn : mod.functions) {
                        fns_list += std::format("- `{}` — {}\n", fn.cf_sig, fn.summary);
                    }
                    return std::format("### `import \"{}\"` (`{}`)\n{}\n\n**Auto-link flags:** `{}`\n\n**Functions:**\n{}",
                        mod.import_path, mod.ns_alias, mod.overview, mod.link_flags, fns_list);
                }
            }
        }

        auto [ast_res, ws] = get_or_parse_ast(uri, text, line, character);
        if (!ast_res) return "";
        const auto& ast = *ast_res;
        auto env = detail::build_scope_bindings(ast, ws, lines, line);

        // 2. Check imported local `.cf` packages (`checksum` or `checksum.compute_tag`) and `cpp::` Clang AST Bridge!
        for (const auto& pkg : ws.local_packages) {
            auto pkg_lines = detail::split_lines(get_doc_text(pkg.uri));
            if (sym.qualifier.empty() && sym.name == pkg.alias) {
                std::string exports;
                for (const auto& fn : pkg.ast.functions) {
                    exports += std::format("- `{}` (`{}`)\n", cfire::fmt::format_fn_signature(fn), detail::lower_fn_preview(fn));
                }
                for (const auto& st : pkg.ast.structs) {
                    exports += std::format("- `type {} struct` ({} fields)\n", st.name, st.fields.size());
                }
                return std::format("### Package `{}` (`import \"{}\"`)\n**Source:** `{}`\n\n**Exported Symbols:**\n{}",
                    pkg.alias, pkg.import_path, pkg.file_path, exports);
            }
            if (sym.qualifier == pkg.alias) {
                for (const auto& fn : pkg.ast.functions) {
                    if (fn.name == sym.name) {
                        std::string doc = detail::extract_leading_doc_comments(pkg_lines, fn.line);
                        std::string doc_sec = doc.empty() ? "" : ("\n\n**Documentation:**\n" + doc);
                        return std::format("### `{}.{}` (from `{}`)\n```cf\n{}\n```\n**Lowered ISO C++23:**\n```cpp\n{}\n```\nDefined at `{}:{}`{}",
                            pkg.alias, fn.name, pkg.import_path, cfire::fmt::format_fn_signature(fn), detail::lower_fn_preview(fn), pkg.file_path, fn.line, doc_sec);
                    }
                }
                for (const auto& st : pkg.ast.structs) {
                    if (st.name == sym.name) {
                        std::string doc = detail::extract_leading_doc_comments(pkg_lines, st.line);
                        std::unordered_set<std::string> vis;
                        auto layout = detail::compute_struct_layout(st, pkg.ast, ws, vis);
                        std::string layout_hdr = std::format("\n\n**Memory Layout (64-bit ABI):** `Size: {} bytes | Align: {} bytes`\n", layout.total_size, layout.align);
                        std::string fields_table = "\n| Field | Offset | `.cf` Type | Lowered C++23 | Tags |\n|---|---|---|---|---|\n";
                        for (std::size_t i = 0; i < st.fields.size(); ++i) {
                            const auto& f = st.fields[i];
                            int off = (i < layout.fields.size()) ? layout.fields[i].offset : 0;
                            int fsize = (i < layout.fields.size()) ? layout.fields[i].size : 0;
                            fields_table += std::format("| `{}` | `+{} ({}B)` | `{}` | `{}` | `{}` |\n",
                                f.name, off, fsize, cfire::fmt::format_type_ref(f.type), detail::lower_type_preview(f.type),
                                f.raw_tag.empty() ? "-" : f.raw_tag);
                        }
                        std::string doc_sec = doc.empty() ? "" : ("\n\n**Documentation:**\n" + doc);
                        return std::format("### Struct `{}.{}` (from `{}`)\n```cf\n{}\n```{}{}{}",
                            pkg.alias, st.name, pkg.import_path, cfire::fmt::format_struct_signature(st), layout_hdr, doc_sec, fields_table);
                    }
                }
            }
        }

        if (sym.qualifier == "cpp" || ws.cpp_symbols.contains(sym.name)) {
            if (auto cit = ws.cpp_symbols.find(sym.name); cit != ws.cpp_symbols.end()) {
                const auto& csym = cit->second;
                std::string params_str;
                for (std::size_t i = 0; i < csym.param_types.size(); ++i) {
                    if (i > 0) params_str += ", ";
                    params_str += csym.param_types[i];
                }
                std::string hdr = ws.cpp_symbol_header[sym.name];
                return std::format("### Native C/C++ Function `cpp::{}` (`<{}>`)\n```cpp\n{} {}({})\n```\n**cfire Return Type:** `{}`\n\nIngested via the **Clang AST Header Bridge** (`clang++ -Xclang -ast-dump=json`) with automatic `cfire::c_str` boundary coercion.",
                    csym.name, hdr, csym.return_type, csym.name, params_str, detail::cpp_return_to_cf(csym.return_type));
            }
        }

        // 3. If `qualifier.member` where `qualifier` is a local variable, chained field, or struct/enum:
        if (!sym.qualifier.empty()) {
            if (sym.name == "len") {
                return "### Property `.len`\n```cf\nlen: usize\n```\n**Lowered C++23:** `.size()` (`std::size_t`)";
            }
            std::string recv_type = detail::resolve_qualifier_type(sym.qualifier, ast, ws, env);
            // Check stdlib handle methods (`TcpListener.accept`, `TcpConn.read_str`, `Conn.query`, `Channel.send`)
            for (const auto& mod : cfire::doc::stdlib_catalog()) {
                for (const auto& fn : mod.functions) {
                    if (auto dot = fn.name.find('.'); dot != std::string::npos) {
                        std::string htype = fn.name.substr(0, dot);
                        std::string mname = fn.name.substr(dot + 1);
                        if ((recv_type == htype || recv_type == mod.ns_alias + "." + htype || recv_type.starts_with("sync." + htype)) &&
                            mname == sym.name) {
                            return std::format("### `{}` (`{}`)\n```cf\n{}\n```\n**Lowered C++23:** `{}`\n\n{}\n\n**Example:**\n```cf\n{}\n```",
                                fn.name, mod.import_path, fn.cf_sig, fn.cpp_lowering, fn.summary, fn.example);
                        }
                    }
                }
            }
            // Check user struct fields & methods on `recv_type` (both local and imported packages)
            auto check_structs = [&](const std::vector<cfire::frontend::StructDecl>& structs, const std::vector<std::string>& doc_lines) -> std::string {
                for (const auto& st : structs) {
                    if (st.name == recv_type) {
                        for (const auto& f : st.fields) {
                            if (f.name == sym.name) {
                                std::string f_doc = detail::extract_leading_doc_comments(doc_lines, f.line);
                                if (f.line >= 1 && static_cast<std::size_t>(f.line) <= doc_lines.size()) {
                                    const std::string& fln = doc_lines[static_cast<std::size_t>(f.line - 1)];
                                    if (auto cpos = fln.find("//"); cpos != std::string::npos) {
                                        std::string ic = fln.substr(cpos + 2);
                                        if (!ic.empty() && ic.front() == ' ') ic = ic.substr(1);
                                        if (!ic.empty()) {
                                            if (!f_doc.empty()) f_doc += "\n";
                                            f_doc += ic;
                                        }
                                    }
                                }
                                std::string f_cf = cfire::fmt::format_type_ref(f.type);
                                std::string f_cpp = detail::lower_type_preview(f.type);
                                std::unordered_set<std::string> vis;
                                auto layout = detail::compute_struct_layout(st, ast, ws, vis);
                                std::string layout_info;
                                for (const auto& fl : layout.fields) {
                                    if (fl.name == f.name) {
                                        layout_info = std::format("\n**Field Layout:** `Offset: +{} bytes | Size: {} bytes`", fl.offset, fl.size);
                                        break;
                                    }
                                }
                                std::string tag_info = f.raw_tag.empty() ? "" : std::format("\n\n**Struct Tag:** `` `{}` ``", f.raw_tag);
                                std::string doc_sec = f_doc.empty() ? "" : ("\n\n**Documentation:**\n" + f_doc);
                                return std::format("### Field `{}.{}`\n```cf\n{}: {}\n```\n**Lowered C++23:** `{} {}{{}};`{}{}{}",
                                    st.name, f.name, f.name, f_cf, f_cpp, f.name, layout_info, tag_info, doc_sec);
                            }
                        }
                    }
                }
                return "";
            };
            if (std::string s = check_structs(ast.structs, lines); !s.empty()) return s;
            for (const auto& pkg : ws.local_packages) {
                auto pkg_lines = detail::split_lines(get_doc_text(pkg.uri));
                if (std::string s = check_structs(pkg.ast.structs, pkg_lines); !s.empty()) return s;
            }

            for (const auto& fn : ast.functions) {
                if (fn.receiver && fn.receiver->type.name == recv_type && fn.name == sym.name) {
                    std::string doc = detail::extract_leading_doc_comments(lines, fn.line);
                    std::string doc_sec = doc.empty() ? "" : ("\n\n**Documentation:**\n" + doc);
                    return std::format("### Method `{}.{}`\n```cf\n{}\n```\n**Lowered C++23:**\n```cpp\n{}\n```{}",
                        recv_type, fn.name, cfire::fmt::format_fn_signature(fn), detail::lower_fn_preview(fn), doc_sec);
                }
            }
            for (const auto& pkg : ws.local_packages) {
                for (const auto& fn : pkg.ast.functions) {
                    if (fn.receiver && fn.receiver->type.name == recv_type && fn.name == sym.name) {
                        auto pkg_lines = detail::split_lines(get_doc_text(pkg.uri));
                        std::string doc = detail::extract_leading_doc_comments(pkg_lines, fn.line);
                        std::string doc_sec = doc.empty() ? "" : ("\n\n**Documentation:**\n" + doc);
                        return std::format("### Method `{}.{}`\n```cf\n{}\n```\n**Lowered C++23:**\n```cpp\n{}\n```{}",
                            recv_type, fn.name, cfire::fmt::format_fn_signature(fn), detail::lower_fn_preview(fn), doc_sec);
                    }
                }
            }
            // Check enum variants (`Shape.Circle`)
            for (const auto& en : ast.enums) {
                if (en.name == recv_type) {
                    for (const auto& v : en.variants) {
                        if (v.name == sym.name) {
                            return std::format("### Enum Variant `{}.{}`\n```cf\n{}\n```\nLowers to `{}::{}` inside `std::variant` (line {}).",
                                en.name, v.name, cfire::fmt::format_enum_signature(en), en.name, v.name, v.line);
                        }
                    }
                }
            }
        }

        // 4. Check if hovering over a local variable or function parameter!
        if (sym.qualifier.empty()) {
            if (auto it = env.find(sym.name); it != env.end()) {
                const auto& b = it->second;
                std::string kind_label = b.is_param
                    ? std::format("Parameter (`{}` intent)", b.intent)
                    : (b.is_mut ? "Local Variable (`mut`)" : "Local Binding (`const` by default)");
                std::string extra;
                std::string unwrapped = detail::strip_wrappers(b.cf_type);
                for (const auto& st : ast.structs) {
                    if (st.name == unwrapped) {
                        extra = "\n\n**Struct Fields & Properties:**\n```cf\n" + cfire::fmt::format_struct_signature(st) + "\n```";
                        break;
                    }
                }
                return std::format("### {} `{}`\n```cf\n{}{}: {}\n```\n**Lowered C++23 Type:** `{}`\n\nDeclared at line {}{}",
                    kind_label, b.name, b.is_mut ? "mut " : "", b.name, b.cf_type, b.cpp_type, b.decl_line, extra);
            }
        }

        // 5. Check top-level functions, structs, enums, and enum variants
        for (const auto& fn : ast.functions) {
            if (fn.name == sym.name) {
                std::string doc = detail::extract_leading_doc_comments(lines, fn.line);
                std::string cf_sig = cfire::fmt::format_fn_signature(fn);
                std::string cpp_sig = detail::lower_fn_preview(fn);
                std::string param_docs;
                if (!fn.params.empty()) {
                    param_docs = "\n\n**Parameters:**\n";
                    for (const auto& p : fn.params) {
                        param_docs += std::format("- `{}` (`{}` intent): `{}` $\\rightarrow$ `{}`\n",
                            p.name, p.intent, cfire::fmt::format_type_ref(p.type), detail::lower_type_preview(p.type, true, p.intent));
                    }
                }
                std::string doc_sec = doc.empty() ? "" : ("\n\n**Documentation:**\n" + doc);
                return std::format("### Function `{}`\n```cf\n{}\n```\n**Lowered ISO C++23:**\n```cpp\n{}\n```{}{}",
                    fn.name, cf_sig, cpp_sig, doc_sec, param_docs);
            }
        }
        for (const auto& st : ast.structs) {
            if (st.name == sym.name) {
                std::string doc = detail::extract_leading_doc_comments(lines, st.line);
                std::unordered_set<std::string> vis;
                auto layout = detail::compute_struct_layout(st, ast, ws, vis);
                std::string layout_hdr = std::format("\n\n**Memory Layout (64-bit ABI):** `Size: {} bytes | Align: {} bytes`\n", layout.total_size, layout.align);
                std::string fields_table = "\n| Field | Offset | `.cf` Type | Lowered C++23 | Tags |\n|---|---|---|---|---|\n";
                for (std::size_t i = 0; i < st.fields.size(); ++i) {
                    const auto& f = st.fields[i];
                    int off = (i < layout.fields.size()) ? layout.fields[i].offset : 0;
                    int fsize = (i < layout.fields.size()) ? layout.fields[i].size : 0;
                    fields_table += std::format("| `{}` | `+{} ({}B)` | `{}` | `{}` | `{}` |\n",
                        f.name, off, fsize, cfire::fmt::format_type_ref(f.type), detail::lower_type_preview(f.type),
                        f.raw_tag.empty() ? "-" : f.raw_tag);
                }
                std::string doc_sec = doc.empty() ? "" : ("\n\n**Documentation:**\n" + doc);
                return std::format("### Struct `{}`\n```cf\n{}\n```\nLowers 1-to-1 to C++23 aggregate `struct {}` (line {}).{}{}{}",
                    st.name, cfire::fmt::format_struct_signature(st), st.name, st.line, layout_hdr, doc_sec, fields_table);
            }
        }
        for (const auto& en : ast.enums) {
            if (en.name == sym.name) {
                std::string doc = detail::extract_leading_doc_comments(lines, en.line);
                std::string doc_sec = doc.empty() ? "" : ("\n\n**Documentation:**\n" + doc);
                return std::format("### Algebraic Enum `{}`\n```cf\n{}\n```\nLowers to `std::variant` with exhaustive `match` verification (line {}).{}",
                    en.name, cfire::fmt::format_enum_signature(en), en.line, doc_sec);
            }
            for (const auto& v : en.variants) {
                if (v.name == sym.name) {
                    return std::format("### Enum Variant `{}.{}`\n```cf\n{}\n```\nLowers to `{}::{}` inside `std::variant` (line {}).",
                        en.name, v.name, cfire::fmt::format_enum_signature(en), en.name, v.name, v.line);
                }
            }
        }

        // 6. Sub-expression type hover (operators, binary expressions, call expressions, literals)
        int target_line_1 = line + 1;
        int target_col_1 = character + 1;

        std::vector<cfire::frontend::ExprPtr> all_exprs;
        for (const auto& fn : ast.functions) detail::collect_stmt_exprs(fn.body, all_exprs);
        for (const auto& t : ast.tests) detail::collect_stmt_exprs(t.body, all_exprs);
        for (const auto& gv : ast.global_vars) if (gv.value) detail::collect_all_exprs(gv.value, all_exprs);

        cfire::frontend::ExprPtr best_expr = nullptr;
        int best_span = 999999;

        for (const auto& expr : all_exprs) {
            if (!expr || expr->line != target_line_1) continue;
            std::string text_rep = detail::expr_to_string(expr);
            int est_len = static_cast<int>(text_rep.size());
            if (est_len <= 0) est_len = 1;
            int col_start = expr->col;
            if (const auto* bin = std::get_if<cfire::frontend::BinaryExpr>(&expr->node)) {
                if (bin->left) col_start = std::min(col_start, bin->left->col);
            }
            int col_end = col_start + est_len - 1;
            if (target_col_1 >= col_start && target_col_1 <= col_end) {
                if (est_len < best_span) {
                    best_span = est_len;
                    best_expr = expr;
                }
            }
        }

        if (best_expr) {
            std::string exp_str = detail::expr_to_string(best_expr);
            std::string cf_type = detail::infer_expr_cf_type(best_expr, ast, ws, env);
            if (!exp_str.empty() && cf_type != "auto") {
                std::string cpp_type = detail::cf_type_string_to_cpp(cf_type);
                return std::format("### Expression `{}`\n```cf\n{}: {}\n```\n**Lowered ISO C++23:** `{}`",
                    exp_str, exp_str, cf_type, cpp_type);
            }
        }

        return "";
    }

    auto compute_signature_help(const std::string& uri, int line, int character) -> std::string {
        std::string text = get_doc_text(uri);
        auto lines = detail::split_lines(text);
        auto ctx_opt = detail::find_call_context(lines, line, character);
        if (!ctx_opt) return "null";
        const auto& ctx = *ctx_opt;

        auto format_sig_resp = [&](std::string_view label, std::string_view doc_str, const std::vector<std::string>& param_labels) -> std::string {
            std::string params_json = "[";
            for (std::size_t i = 0; i < param_labels.size(); ++i) {
                if (i > 0) params_json += ",";
                params_json += std::format(R"({{"label":"{}"}})", detail::json_escape(param_labels[i]));
            }
            params_json += "]";
            int active_p = param_labels.empty() ? 0 : std::clamp(ctx.active_param, 0, static_cast<int>(param_labels.size()) - 1);
            return std::format(
                R"({{"signatures":[{{"label":"{}","documentation":{{"kind":"markdown","value":"{}"}},"parameters":{}}}],"activeSignature":0,"activeParameter":{}}})",
                detail::json_escape(label), detail::json_escape(doc_str), params_json, active_p);
        };

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

        // 1. Check `core:*` stdlib catalog
        if (!ctx.qualifier.empty()) {
            for (const auto& mod : cfire::doc::stdlib_catalog()) {
                if (mod.ns_alias == ctx.qualifier) {
                    for (const auto& fn : mod.functions) {
                        if (fn.name == ctx.callee) {
                            return format_sig_resp(
                                fn.cf_sig,
                                std::format("{} (C++23: `{}`)", fn.summary, fn.cpp_lowering),
                                extract_params_from_sig(fn.cf_sig));
                        }
                    }
                }
            }
        }

        // 2. Check user functions, methods, imported `.cf` packages, stdlib handles, and `cpp::` functions
        auto [ast_res, ws] = get_or_parse_ast(uri, text, line, character);
        if (ast_res) {
            auto env = detail::build_scope_bindings(*ast_res, ws, lines, line);

            auto format_fn_decl = [&](const cfire::frontend::FnDecl& fn) -> std::string {
                std::vector<std::string> plabels;
                for (const auto& p : fn.params) {
                    std::string pl = (p.intent != "in" && !p.intent.empty()) ? (p.intent + " ") : "";
                    pl += std::format("{}: {}", p.name, cfire::fmt::format_type_ref(p.type));
                    plabels.push_back(std::move(pl));
                }
                return format_sig_resp(
                    cfire::fmt::format_fn_signature(fn),
                    std::format("Lowered C++23: `{}`", detail::lower_fn_preview(fn)),
                    plabels);
            };

            for (const auto& fn : ast_res->functions) {
                if (fn.name == ctx.callee) return format_fn_decl(fn);
            }
            for (const auto& pkg : ws.local_packages) {
                if (ctx.qualifier.empty() || ctx.qualifier == pkg.alias) {
                    for (const auto& fn : pkg.ast.functions) {
                        if (fn.name == ctx.callee) return format_fn_decl(fn);
                    }
                }
            }
            if (ctx.qualifier == "cpp" || ws.cpp_symbols.contains(ctx.callee)) {
                if (auto cit = ws.cpp_symbols.find(ctx.callee); cit != ws.cpp_symbols.end()) {
                    const auto& csym = cit->second;
                    std::string params_str;
                    for (std::size_t i = 0; i < csym.param_types.size(); ++i) {
                        if (i > 0) params_str += ", ";
                        params_str += csym.param_types[i];
                    }
                    std::string sig = std::format("cpp::{}({}) -> {}", csym.name, params_str, detail::cpp_return_to_cf(csym.return_type));
                    return format_sig_resp(sig, std::format("C/C++ Header `<{}>`", ws.cpp_symbol_header[csym.name]), csym.param_types);
                }
            }
            if (!ctx.qualifier.empty()) {
                std::string recv_type = detail::resolve_qualifier_type(ctx.qualifier, *ast_res, ws, env);
                for (const auto& mod : cfire::doc::stdlib_catalog()) {
                    for (const auto& fn : mod.functions) {
                        if (auto dot = fn.name.find('.'); dot != std::string::npos) {
                            std::string htype = fn.name.substr(0, dot);
                            std::string mname = fn.name.substr(dot + 1);
                            if ((recv_type == htype || recv_type == mod.ns_alias + "." + htype || recv_type.starts_with("sync." + htype)) &&
                                mname == ctx.callee) {
                                return format_sig_resp(fn.cf_sig, fn.summary, extract_params_from_sig(fn.cf_sig));
                            }
                        }
                    }
                }
            }
        }
        return "null";
    }

    auto compute_inlay_hints(const std::string& uri) -> std::string {
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

    auto compute_import_edit(const std::vector<std::string>& lines, std::string_view import_path) -> std::string {
        int last_import_line = -1;
        int package_line = -1;
        for (int i = 0; i < static_cast<int>(lines.size()); ++i) {
            std::string_view l = lines[i];
            while (!l.empty() && (l.front() == ' ' || l.front() == '\t')) l.remove_prefix(1);
            if (l.starts_with("package ")) {
                package_line = i;
            } else if (l.starts_with("import ")) {
                last_import_line = i;
            }
        }

        int insert_line = 0;
        std::string new_text;
        if (last_import_line >= 0) {
            insert_line = last_import_line + 1;
            new_text = std::format("import \"{}\"\n", import_path);
        } else if (package_line >= 0) {
            if (package_line + 1 < static_cast<int>(lines.size()) && lines[package_line + 1].empty()) {
                insert_line = package_line + 2;
                new_text = std::format("import \"{}\"\n\n", import_path);
            } else {
                insert_line = package_line + 1;
                new_text = std::format("\nimport \"{}\"\n", import_path);
            }
        } else {
            insert_line = 0;
            new_text = std::format("import \"{}\"\n\n", import_path);
        }

        return std::format(
            R"([{{"range":{{"start":{{"line":{},"character":0}},"end":{{"line":{},"character":0}}}},"newText":"{}"}}])",
            insert_line, insert_line, detail::json_escape(new_text));
    }

    auto compute_completions(const std::string& uri, int line, int character) -> std::string {
        std::string text = get_doc_text(uri);
        auto lines = detail::split_lines(text);
        auto sym = detail::symbol_at_position(lines, line, character);

        std::string out = "[";
        bool first = true;
        auto append_item = [&](std::string_view label, int kind, std::string_view detail_str, std::string_view doc_str,
                               std::string_view insert_text = "", int insert_text_format = 1,
                               std::string_view filter_text = "", std::string_view additional_edits = "") {
            if (!first) out += ",";
            first = false;
            out += std::format(
                R"({{"label":"{}","kind":{},"detail":"{}","documentation":"{}"}})",
                detail::json_escape(label), kind, detail::json_escape(detail_str), detail::json_escape(doc_str));
            out.pop_back();

            if (!insert_text.empty() && insert_text != label) {
                out += std::format(R"(,"insertText":"{}")", detail::json_escape(insert_text));
            }
            if (insert_text_format > 1 && client_snippet_support_) {
                out += std::format(R"(,"insertTextFormat":{})", insert_text_format);
            }
            if (!filter_text.empty() && filter_text != label) {
                out += std::format(R"(,"filterText":"{}")", detail::json_escape(filter_text));
            }
            if (!additional_edits.empty()) {
                out += std::format(R"(,"additionalTextEdits":{})", additional_edits);
            }
            out += "}";
        };

        auto [ast_res, ws] = get_or_parse_ast(uri, text, line, character);

        auto is_module_imported = [&](std::string_view import_path, std::string_view alias) -> bool {
            if (ast_res) {
                for (const auto& imp : ast_res->imports) {
                    if (imp.path == import_path || imp.path == alias) {
                        return true;
                    }
                }
            }
            for (const auto& l : lines) {
                if (l.find(import_path) != std::string::npos && l.find("import ") != std::string::npos) {
                    return true;
                }
            }
            return false;
        };

        if (!sym.qualifier.empty()) {
            // 1. Check `core:*` module functions (`fs.`, `json.`, `net.`, etc.)
            for (const auto& mod : cfire::doc::stdlib_catalog()) {
                if (mod.ns_alias == sym.qualifier) {
                    bool imported = is_module_imported(mod.import_path, mod.ns_alias);
                    std::string add_edit = imported ? "" : compute_import_edit(lines, mod.import_path);
                    for (const auto& fn : mod.functions) {
                        if (fn.name.find('.') != std::string::npos) continue;
                        std::string fn_detail = imported ? fn.cf_sig : std::format("{} (auto-import)", fn.cf_sig);
                        std::string fn_doc = std::format("{} (C++23: {})", fn.summary, fn.cpp_lowering);
                        if (!imported) {
                            fn_doc += std::format("\n\n*Selecting this will automatically insert `import \"{}\"`.*", mod.import_path);
                        }
                        append_item(fn.name, 3, fn_detail, fn_doc, fn.name, 1, fn.name, add_edit);
                    }
                }
            }
            // 2. Check imported local `.cf` package symbols (`checksum.`)
            for (const auto& pkg : ws.local_packages) {
                if (pkg.alias == sym.qualifier) {
                    auto pkg_lines = detail::split_lines(get_doc_text(pkg.uri));
                    for (const auto& fn : pkg.ast.functions) {
                        std::string doc = detail::extract_leading_doc_comments(pkg_lines, fn.line);
                        if (doc.empty()) doc = detail::lower_fn_preview(fn);
                        append_item(fn.name, 3, cfire::fmt::format_fn_signature(fn), doc);
                    }
                    for (const auto& st : pkg.ast.structs) {
                        std::string doc = detail::extract_leading_doc_comments(pkg_lines, st.line);
                        if (doc.empty()) doc = cfire::fmt::format_struct_signature(st);
                        append_item(st.name, 22, "type " + st.name + " struct", doc);
                    }
                    for (const auto& en : pkg.ast.enums) {
                        std::string doc = detail::extract_leading_doc_comments(pkg_lines, en.line);
                        if (doc.empty()) doc = cfire::fmt::format_enum_signature(en);
                        append_item(en.name, 13, "type " + en.name + " enum", doc);
                    }
                }
            }
            // 3. Check `cpp::` Clang AST Bridge functions
            if (sym.qualifier == "cpp") {
                for (const auto& [cname, csym] : ws.cpp_symbols) {
                    std::string params_str;
                    for (std::size_t i = 0; i < csym.param_types.size(); ++i) {
                        if (i > 0) params_str += ", ";
                        params_str += csym.param_types[i];
                    }
                    append_item(cname, 3, std::format("{}({}) -> {}", cname, params_str, detail::cpp_return_to_cf(csym.return_type)),
                        std::format("Native C/C++ symbol from <{}>", ws.cpp_symbol_header[cname]));
                }
            }
            // 4. Check local variable / chained field type for struct fields, user methods, and stdlib handle methods!
            if (ast_res) {
                auto env = detail::build_scope_bindings(*ast_res, ws, lines, line);
                std::string rtype = detail::resolve_qualifier_type(sym.qualifier, *ast_res, ws, env);
                if (rtype == "str") {
                    append_item("len", 10, "usize", "Length in bytes (lowers to .size())");
                    append_item("empty", 2, "fn empty() -> bool", "Returns true if empty");
                }
                if (rtype.starts_with("[")) {
                    append_item("len", 10, "usize", "Length in elements (lowers to .size())");
                    append_item("empty", 2, "fn empty() -> bool", "Returns true if empty (lowers to .empty())");
                    append_item("push", 2, "fn push(elem: T) -> void", "Appends element to vector (lowers to .push_back())");
                    append_item("pop", 2, "fn pop() -> void", "Removes last element from vector (lowers to .pop_back())");
                    append_item("capacity", 2, "fn capacity() -> usize", "Allocated storage capacity of the vector (lowers to .capacity())");
                    append_item("clear", 2, "fn clear() -> void", "Erases all elements from vector (lowers to .clear())");
                }
                auto add_struct_fields = [&](const std::vector<cfire::frontend::StructDecl>& structs) {
                    for (const auto& st : structs) {
                        if (st.name == rtype) {
                            for (const auto& f : st.fields) {
                                std::string f_sig = cfire::fmt::format_type_ref(f.type);
                                std::string f_doc = std::format("Field of {} (C++23: {}) {}",
                                    st.name, detail::lower_type_preview(f.type), f.raw_tag);
                                append_item(f.name, 5, f_sig, f_doc);
                            }
                        }
                    }
                };
                add_struct_fields(ast_res->structs);
                for (const auto& pkg : ws.local_packages) add_struct_fields(pkg.ast.structs);

                // User methods on `rtype`
                for (const auto& fn : ast_res->functions) {
                    if (fn.receiver && fn.receiver->type.name == rtype) {
                        append_item(fn.name, 2, cfire::fmt::format_fn_signature(fn), detail::lower_fn_preview(fn));
                    }
                }
                for (const auto& pkg : ws.local_packages) {
                    for (const auto& fn : pkg.ast.functions) {
                        if (fn.receiver && fn.receiver->type.name == rtype) {
                            append_item(fn.name, 2, cfire::fmt::format_fn_signature(fn), detail::lower_fn_preview(fn));
                        }
                    }
                }
                // Enum variants (`Shape.Circle`, `Shape.Rect`)
                for (const auto& en : ast_res->enums) {
                    if (en.name == rtype) {
                        for (const auto& v : en.variants) {
                            append_item(v.name, 20, en.name + "::" + v.name, "Algebraic enum variant");
                        }
                    }
                }
                // Stdlib handle methods (`TcpListener.`, `TcpConn.`, `Channel.`, `Conn.`)
                for (const auto& mod : cfire::doc::stdlib_catalog()) {
                    for (const auto& fn : mod.functions) {
                        if (auto dot = fn.name.find('.'); dot != std::string::npos) {
                            std::string htype = fn.name.substr(0, dot);
                            std::string mname = fn.name.substr(dot + 1);
                            if (rtype == htype || rtype == mod.ns_alias + "." + htype || rtype.starts_with("sync." + htype)) {
                                append_item(mname, 2, fn.cf_sig, fn.summary);
                            }
                        }
                    }
                }
            }
            out += "]";
            return out;
        }

        // Unqualified completions: local bindings, `core:*` modules, imported packages, package functions/structs/enums
        if (ast_res) {
            auto env = detail::build_scope_bindings(*ast_res, ws, lines, line);
            for (const auto& [vname, b] : env) {
                append_item(vname, 6, b.cf_type, std::format("C++23: {}", b.cpp_type));
            }
            for (const auto& pkg : ws.local_packages) {
                append_item(pkg.alias, 9, pkg.import_path, "Imported local .cf package");
            }
            if (!ws.cpp_symbols.empty()) {
                append_item("cpp", 9, "Clang AST Header Bridge", "Native C/C++ imported symbols");
            }
            for (const auto& fn : ast_res->functions) {
                std::string doc = detail::extract_leading_doc_comments(lines, fn.line);
                if (doc.empty()) doc = detail::lower_fn_preview(fn);
                append_item(fn.name, 3, cfire::fmt::format_fn_signature(fn), doc);
            }
            for (const auto& st : ast_res->structs) {
                std::string doc = detail::extract_leading_doc_comments(lines, st.line);
                if (doc.empty()) doc = cfire::fmt::format_struct_signature(st);
                append_item(st.name, 22, "type " + st.name + " struct", doc);
            }
            for (const auto& en : ast_res->enums) {
                std::string doc = detail::extract_leading_doc_comments(lines, en.line);
                if (doc.empty()) doc = cfire::fmt::format_enum_signature(en);
                append_item(en.name, 13, "type " + en.name + " enum", doc);
            }
        }
        for (const auto& mod : cfire::doc::stdlib_catalog()) {
            bool imported = is_module_imported(mod.import_path, mod.ns_alias);
            std::string add_edit = imported ? "" : compute_import_edit(lines, mod.import_path);
            std::string mod_detail = imported ? mod.import_path : (mod.import_path + " (auto-import)");
            append_item(mod.ns_alias, 9, mod_detail, mod.title, mod.ns_alias, 1, mod.ns_alias, add_edit);

            for (const auto& fn : mod.functions) {
                if (fn.name.find('.') != std::string::npos) continue;
                std::string call_name = mod.ns_alias + "." + fn.name;
                std::string fn_detail = imported
                    ? std::format("{}: {}", mod.ns_alias, fn.cf_sig)
                    : std::format("{} (auto-import): {}", mod.import_path, fn.cf_sig);
                std::string fn_doc = std::format("{}\n\nSignature: `{}`\nC++23: `{}`\nExample: `{}`",
                    fn.summary, fn.cf_sig, fn.cpp_lowering, fn.example);
                if (!imported) {
                    fn_doc += std::format("\n\n*Selecting this will automatically insert `import \"{}\"`.*", mod.import_path);
                }
                std::string filter = fn.name + " " + call_name;
                append_item(fn.name, 3, fn_detail, fn_doc, call_name, 1, filter, add_edit);
            }
        }

        // Built-in functions and directives
        append_item("println", 3, "println(args...)", "Prints formatted arguments followed by a newline to standard output.");
        append_item("print", 3, "print(args...)", "Prints formatted arguments to standard output without trailing newline.");
        append_item("panic", 3, "panic(message: string)", "Terminates the program immediately with an error message.");
        append_item("assert", 3, "assert(cond: bool, msg: string = \"\")", "Aborts execution if condition evaluates to false.");
        append_item("make", 3, "make([]T, len, cap) / make(map[K]V)", "Allocates and initializes slices or maps.");
        append_item("@pkg", 14, "@pkg(\"libname\")", "Specifies system package / pkg-config dependency for C/C++ header import.");

        // Core language constructs & snippets (LSP-01)
        if (client_snippet_support_) {
            append_item("fn", 15, "fn name(params) -> void { ... }", "Function declaration snippet",
                        "fn ${1:name}(${2:params}) -> ${3:void} {\n\t${0}\n}", 2);
            append_item("type struct", 15, "type Name struct { ... }", "Struct declaration snippet",
                        "type ${1:Name} struct {\n\t${0}\n}", 2);
            append_item("type enum", 15, "type Name enum { ... }", "Algebraic enum declaration snippet",
                        "type ${1:Name} enum {\n\t${0}\n}", 2);
            append_item("match", 15, "match expr { ... }", "Pattern matching expression snippet",
                        "match ${1:expr} {\n\t${2:case} => ${0}\n}", 2);
            append_item("for", 15, "for item in iter { ... }", "For-in iteration loop snippet",
                        "for ${1:item} in ${2:iter} {\n\t${0}\n}", 2);
            append_item("if", 15, "if condition { ... }", "Conditional statement snippet",
                        "if ${1:condition} {\n\t${0}\n}", 2);
            append_item("defer", 15, "defer <expr>", "Schedules statement to execute when enclosing function returns.",
                        "defer ${0}", 2);
            append_item("cpp", 15, "cpp { ... }", "Direct inline C++23 emission block.",
                        "cpp {\n\t${0}\n}", 2);
        } else {
            append_item("fn", 14, "fn keyword", "Function declaration");
            append_item("type struct", 14, "type ... struct", "Struct declaration");
            append_item("type enum", 14, "type ... enum", "Enum declaration");
            append_item("match", 14, "match keyword", "Pattern matching");
            append_item("for", 14, "for keyword", "Loop statement");
            append_item("if", 14, "if keyword", "Conditional statement");
            append_item("defer", 14, "defer <expr>", "Schedules statement to execute when enclosing function returns.");
            append_item("cpp", 14, "cpp { ... }", "Direct inline C++23 emission block.");
        }
        out += "]";
        return out;
    }

    auto compute_definition(const std::string& uri, int line, int character) -> std::string {
        std::string text = get_doc_text(uri);
        auto lines = detail::split_lines(text);
        auto [ast_res, ws] = get_or_parse_ast(uri, text, line, character);
        if (!ast_res) return "null";

        // Check if cursor is on an `import "./..."` declaration line
        for (const auto& imp : ast_res->imports) {
            if (imp.line - 1 == line) {
                for (const auto& pkg : ws.local_packages) {
                    if (pkg.import_path == imp.path) {
                        return std::format(
                            R"({{"uri":"{}","range":{{"start":{{"line":0,"character":0}},"end":{{"line":0,"character":12}}}}}})",
                            detail::json_escape(pkg.uri));
                    }
                }
            }
        }

        auto sym = detail::symbol_at_position(lines, line, character);
        if (sym.name.empty()) return "null";

        std::string target_uri = uri;
        int target_line = -1;
        auto env = detail::build_scope_bindings(*ast_res, ws, lines, line);

        // 1. Check cross-package references (`checksum` or `checksum.compute_tag`)
        for (const auto& pkg : ws.local_packages) {
            if (sym.qualifier.empty() && sym.name == pkg.alias) {
                return std::format(
                    R"({{"uri":"{}","range":{{"start":{{"line":0,"character":0}},"end":{{"line":0,"character":12}}}}}})",
                    detail::json_escape(pkg.uri));
            }
            if (sym.qualifier == pkg.alias) {
                for (const auto& fn : pkg.ast.functions) {
                    if (fn.name == sym.name) {
                        target_uri = pkg.uri;
                        target_line = fn.line - 1;
                        break;
                    }
                }
                for (const auto& st : pkg.ast.structs) {
                    if (st.name == sym.name) {
                        target_uri = pkg.uri;
                        target_line = st.line - 1;
                        break;
                    }
                }
                for (const auto& en : pkg.ast.enums) {
                    if (en.name == sym.name) {
                        target_uri = pkg.uri;
                        target_line = en.line - 1;
                        break;
                    }
                }
            }
        }

        // 2. Check struct fields, methods, or enum variants (`obj.field`, `obj.method`, `Enum.Variant`)
        if (target_line < 0 && !sym.qualifier.empty()) {
            std::string rtype = detail::resolve_qualifier_type(sym.qualifier, *ast_res, ws, env);
            for (const auto& st : ast_res->structs) {
                if (st.name == rtype) {
                    for (const auto& f : st.fields) {
                        if (f.name == sym.name) { target_line = f.line - 1; break; }
                    }
                }
            }
            for (const auto& fn : ast_res->functions) {
                if (fn.receiver && fn.receiver->type.name == rtype && fn.name == sym.name) {
                    target_line = fn.line - 1;
                    break;
                }
            }
            for (const auto& en : ast_res->enums) {
                if (en.name == rtype) {
                    for (const auto& v : en.variants) {
                        if (v.name == sym.name) { target_line = v.line - 1; break; }
                    }
                }
            }
        }
        if (target_line < 0) {
            if (auto it = env.find(sym.name); it != env.end()) {
                target_line = it->second.decl_line - 1;
            }
        }
        if (target_line < 0) {
            for (const auto& fn : ast_res->functions) {
                if (fn.name == sym.name) { target_line = fn.line - 1; break; }
            }
        }
        if (target_line < 0) {
            for (const auto& st : ast_res->structs) {
                if (st.name == sym.name) { target_line = st.line - 1; break; }
            }
        }
        if (target_line < 0) {
            for (const auto& en : ast_res->enums) {
                if (en.name == sym.name) { target_line = en.line - 1; break; }
                for (const auto& v : en.variants) {
                    if (v.name == sym.name) { target_line = v.line - 1; break; }
                }
            }
        }
        if (target_line < 0) {
            for (const auto& iface : ast_res->interfaces) {
                if (iface.name == sym.name) { target_line = iface.line - 1; break; }
            }
        }
        if (target_line < 0) return "null";

        return std::format(
            R"({{"uri":"{}","range":{{"start":{{"line":{},"character":0}},"end":{{"line":{},"character":12}}}}}})",
            detail::json_escape(target_uri), target_line, target_line);
    }

    auto compute_type_definition(const std::string& uri, int line, int character) -> std::string {
        std::string text = get_doc_text(uri);
        auto lines = detail::split_lines(text);
        auto [ast_res, ws] = get_or_parse_ast(uri, text, line, character);
        if (!ast_res) return "null";

        auto sym = detail::symbol_at_position(lines, line, character);
        if (sym.name.empty()) return "null";

        auto env = detail::build_scope_bindings(*ast_res, ws, lines, line);

        std::string raw_type;
        // 1. If symbol is qualified (`obj.field`), get the type of field
        if (!sym.qualifier.empty()) {
            std::string rtype = detail::resolve_qualifier_type(sym.qualifier, *ast_res, ws, env);
            for (const auto& st : ast_res->structs) {
                if (st.name == rtype) {
                    for (const auto& f : st.fields) {
                        if (f.name == sym.name) {
                            raw_type = cfire::fmt::format_type_ref(f.type);
                            break;
                        }
                    }
                }
            }
            for (const auto& pkg : ws.local_packages) {
                for (const auto& st : pkg.ast.structs) {
                    if (st.name == rtype) {
                        for (const auto& f : st.fields) {
                            if (f.name == sym.name) {
                                raw_type = cfire::fmt::format_type_ref(f.type);
                                break;
                            }
                        }
                    }
                }
            }
        } else if (auto it = env.find(sym.name); it != env.end()) {
            // 2. If symbol is a variable in scope
            raw_type = it->second.cf_type;
        } else {
            // 3. Fallback: symbol itself might already be a type identifier (e.g. `AuditLog`)
            raw_type = sym.name;
        }

        // Unwrap type wrappers, collections, pointers, and intents:
        // []T -> T, own[T] -> T, shared[T] -> T, map[K]V -> V, *T -> T, mut T -> T, T? -> T, T! -> T
        auto unwrap_type = [](std::string t) -> std::string {
            bool changed = true;
            while (changed) {
                changed = false;
                while (!t.empty() && (t.front() == ' ' || t.front() == '\t')) t.erase(0, 1);
                while (!t.empty() && (t.back() == ' ' || t.back() == '\t')) t.pop_back();

                if (t.starts_with("mut ")) { t = t.substr(4); changed = true; continue; }
                if (t.starts_with("in ")) { t = t.substr(3); changed = true; continue; }
                if (t.starts_with("move ")) { t = t.substr(5); changed = true; continue; }
                if (t.starts_with("out ")) { t = t.substr(4); changed = true; continue; }
                if (t.starts_with("&") || t.starts_with("*")) { t = t.substr(1); changed = true; continue; }
                if (t.starts_with("[]")) { t = t.substr(2); changed = true; continue; }
                if (t.starts_with("own[") && t.ends_with("]")) {
                    t = t.substr(4, t.size() - 5);
                    changed = true;
                    continue;
                }
                if (t.starts_with("shared[") && t.ends_with("]")) {
                    t = t.substr(7, t.size() - 8);
                    changed = true;
                    continue;
                }
                if (t.starts_with("map[") && t.ends_with("]")) {
                    if (auto close_br = t.find(']'); close_br != std::string::npos && close_br + 1 < t.size()) {
                        t = t.substr(close_br + 1);
                        changed = true;
                        continue;
                    }
                }
                if (t.ends_with("?") || t.ends_with("!")) {
                    t.pop_back();
                    changed = true;
                    continue;
                }
            }
            return t;
        };

        std::string type_name = unwrap_type(raw_type);
        if (type_name.empty()) return "null";

        // Filter out primitive types (per user clarification decision: return null)
        static const std::unordered_set<std::string_view> primitives = {
            "void", "bool", "i8", "i16", "i32", "i64", "u8", "u16", "u32", "u64",
            "f32", "f64", "str", "string", "usize", "isize", "any"
        };
        if (primitives.contains(type_name)) return "null";

        // Find declaration in current file
        for (const auto& st : ast_res->structs) {
            if (st.name == type_name) {
                return std::format(
                    R"({{"uri":"{}","range":{{"start":{{"line":{},"character":0}},"end":{{"line":{},"character":12}}}}}})",
                    detail::json_escape(uri), st.line - 1, st.line - 1);
            }
        }
        for (const auto& en : ast_res->enums) {
            if (en.name == type_name) {
                return std::format(
                    R"({{"uri":"{}","range":{{"start":{{"line":{},"character":0}},"end":{{"line":{},"character":12}}}}}})",
                    detail::json_escape(uri), en.line - 1, en.line - 1);
            }
        }
        for (const auto& iface : ast_res->interfaces) {
            if (iface.name == type_name) {
                return std::format(
                    R"({{"uri":"{}","range":{{"start":{{"line":{},"character":0}},"end":{{"line":{},"character":12}}}}}})",
                    detail::json_escape(uri), iface.line - 1, iface.line - 1);
            }
        }

        // Find declaration in local packages
        for (const auto& pkg : ws.local_packages) {
            std::string prefix = pkg.alias + ".";
            std::string stripped_type = type_name.starts_with(prefix) ? type_name.substr(prefix.size()) : type_name;
            for (const auto& st : pkg.ast.structs) {
                if (st.name == stripped_type) {
                    return std::format(
                        R"({{"uri":"{}","range":{{"start":{{"line":{},"character":0}},"end":{{"line":{},"character":12}}}}}})",
                        detail::json_escape(pkg.uri), st.line - 1, st.line - 1);
                }
            }
            for (const auto& en : pkg.ast.enums) {
                if (en.name == stripped_type) {
                    return std::format(
                        R"({{"uri":"{}","range":{{"start":{{"line":{},"character":0}},"end":{{"line":{},"character":12}}}}}})",
                        detail::json_escape(pkg.uri), en.line - 1, en.line - 1);
                }
            }
        }

        return "null";
    }

    auto compute_semantic_tokens(const std::string& uri) -> std::string {
        std::string text = get_doc_text(uri);
        auto toks = cfire::frontend::tokenize(detail::uri_to_path(uri), text);
        if (!toks || toks->empty()) return "[]";

        auto [ast_res, ws] = get_or_parse_ast(uri, text);

        // 1. Symbol classification maps
        std::unordered_map<std::string, int> type_kinds = {
            {"void", 0}, {"bool", 0}, {"str", 0}, {"string", 0},
            {"i8", 0}, {"i16", 0}, {"i32", 0}, {"i64", 0},
            {"u8", 0}, {"u16", 0}, {"u32", 0}, {"u64", 0},
            {"f32", 0}, {"f64", 0}, {"usize", 0}, {"isize", 0},
            {"any", 0}
        };

        std::unordered_map<std::string, int> func_names;
        for (const auto& mod : cfire::doc::stdlib_catalog()) {
            for (const auto& fn : mod.functions) {
                if (auto dot = fn.name.find('.'); dot != std::string::npos) {
                    func_names[fn.name.substr(dot + 1)] = 9; // method
                } else {
                    func_names[fn.name] = 8; // function
                }
            }
        }

        std::unordered_set<std::string> builtins = {"println", "print", "panic", "assert", "make"};
        std::unordered_set<std::string> namespaces = {"cpp"};
        for (const auto& mod : cfire::doc::stdlib_catalog()) {
            namespaces.insert(mod.ns_alias);
        }

        std::unordered_set<int> func_decl_lines;
        std::map<std::pair<int, int>, int> param_mods;
        std::unordered_map<std::string, int> var_mut_state;
        std::set<std::pair<int, std::string>> var_decl_sites;
        std::set<std::pair<int, std::string>> type_decl_sites;

        if (ast_res) {
            for (const auto& st : ast_res->structs) {
                type_kinds[st.name] = 1; // struct
                type_decl_sites.insert({st.line, st.name});
            }
            for (const auto& en : ast_res->enums) {
                type_kinds[en.name] = 2; // enum
                type_decl_sites.insert({en.line, en.name});
                for (const auto& v : en.variants) {
                    type_kinds[v.name] = 7; // enumMember
                }
            }
            for (const auto& ifc : ast_res->interfaces) {
                type_kinds[ifc.name] = 3; // interface
                type_decl_sites.insert({ifc.line, ifc.name});
            }

            for (const auto& fn : ast_res->functions) {
                func_decl_lines.insert(fn.line);
                func_names[fn.name] = fn.receiver ? 9 : 8;
                if (fn.receiver) {
                    int mod = (fn.receiver->intent == "mut" ? 8 : 4);
                    var_mut_state[fn.receiver->name] = mod;
                }
                for (const auto& p : fn.params) {
                    int mod = (p.intent == "mut" ? 8 : 4);
                    param_mods[{p.line, p.col}] = mod | 1;
                    var_mut_state[p.name] = mod;
                }
            }

            for (const auto& gv : ast_res->global_vars) {
                int mod = (gv.is_mut ? 8 : 4);
                var_mut_state[gv.name] = mod;
                var_decl_sites.insert({gv.line, gv.name});
            }

            auto lines = detail::split_lines(text);
            std::unordered_map<std::string, detail::LocalBindingInfo> env;
            std::vector<detail::LocalBindingInfo> ordered;
            for (const auto& fn : ast_res->functions) {
                detail::collect_stmt_bindings(fn.body, *ast_res, ws, lines, 0, env, ordered);
            }
            for (const auto& [vname, b] : env) {
                int mod = (b.is_mut ? 8 : 4);
                var_mut_state[vname] = mod;
                var_decl_sites.insert({b.decl_line, vname});
            }
        }

        for (const auto& pkg : ws.local_packages) {
            namespaces.insert(pkg.alias);
            for (const auto& st : pkg.ast.structs) {
                type_kinds[st.name] = 1;
                type_kinds[pkg.alias + "." + st.name] = 1;
            }
            for (const auto& en : pkg.ast.enums) {
                type_kinds[en.name] = 2;
                type_kinds[pkg.alias + "." + en.name] = 2;
            }
            for (const auto& fn : pkg.ast.functions) {
                func_names[fn.name] = fn.receiver ? 9 : 8;
            }
        }

        struct RawToken {
            int line_0;
            int col_0;
            int length;
            int type;
            int modifiers;
        };

        std::vector<RawToken> raw_tokens;
        raw_tokens.reserve(toks->size());

        using namespace cfire::frontend;
        for (const auto& t : *toks) {
            if (t.kind == TokenKind::Eof) continue;

            int line_0 = std::max(0, t.line - 1);
            int col_0 = std::max(0, t.col - 1);

            if (t.kind == TokenKind::CppBlock) {
                raw_tokens.push_back(RawToken{line_0, col_0, 9, 10 /* keyword */, 0});
                continue;
            }

            if (t.kind == TokenKind::String) {
                int len = static_cast<int>(t.lexeme.size() + 2);
                raw_tokens.push_back(RawToken{line_0, col_0, len, 12 /* string */, 0});
                continue;
            }

            if (t.kind == TokenKind::RawString) {
                int len = static_cast<int>(t.lexeme.size() + 2);
                int type = (t.lexeme.contains(':') ? 6 /* property */ : 12 /* string */);
                raw_tokens.push_back(RawToken{line_0, col_0, len, type, 0});
                continue;
            }

            if (t.kind == TokenKind::Int || t.kind == TokenKind::Float) {
                raw_tokens.push_back(RawToken{line_0, col_0, static_cast<int>(t.lexeme.size()), 13 /* number */, 0});
                continue;
            }

            // Keyword modifiers: mut, move, in, out, async
            if (t.kind == TokenKind::KwMut || t.kind == TokenKind::KwMove ||
                t.kind == TokenKind::KwIn || t.kind == TokenKind::KwOut ||
                t.kind == TokenKind::KwAsync) {
                raw_tokens.push_back(RawToken{line_0, col_0, static_cast<int>(t.lexeme.size()), 11 /* modifier */, 0});
                continue;
            }

            // General keywords
            if (t.kind == TokenKind::KwPackage || t.kind == TokenKind::KwImport ||
                t.kind == TokenKind::KwType || t.kind == TokenKind::KwStruct ||
                t.kind == TokenKind::KwEnum || t.kind == TokenKind::KwInterface ||
                t.kind == TokenKind::KwFn || t.kind == TokenKind::KwVar ||
                t.kind == TokenKind::KwIf || t.kind == TokenKind::KwElse ||
                t.kind == TokenKind::KwFor || t.kind == TokenKind::KwMatch ||
                t.kind == TokenKind::KwReturn || t.kind == TokenKind::KwDefer ||
                t.kind == TokenKind::KwTry || t.kind == TokenKind::KwOr ||
                t.kind == TokenKind::KwTrue || t.kind == TokenKind::KwFalse ||
                t.kind == TokenKind::KwNone || t.kind == TokenKind::KwMap ||
                t.kind == TokenKind::KwAs) {
                raw_tokens.push_back(RawToken{line_0, col_0, static_cast<int>(t.lexeme.size()), 10 /* keyword */, 0});
                continue;
            }

            // Multi-char & relational operators
            if (t.kind == TokenKind::ColonEq || t.kind == TokenKind::PlusEq ||
                t.kind == TokenKind::MinusEq || t.kind == TokenKind::Assign ||
                t.kind == TokenKind::Eq || t.kind == TokenKind::Neq ||
                t.kind == TokenKind::Lt || t.kind == TokenKind::Gt ||
                t.kind == TokenKind::Le || t.kind == TokenKind::Ge ||
                t.kind == TokenKind::DoublePipe || t.kind == TokenKind::Arrow ||
                t.kind == TokenKind::FatArrow || t.kind == TokenKind::DoubleColon ||
                t.kind == TokenKind::DotDot) {
                raw_tokens.push_back(RawToken{line_0, col_0, static_cast<int>(t.lexeme.size()), 14 /* operator */, 0});
                continue;
            }

            if (t.kind == TokenKind::Ident || t.kind == TokenKind::KwCpp) {
                int len = static_cast<int>(t.lexeme.size());
                if (builtins.contains(t.lexeme)) {
                    raw_tokens.push_back(RawToken{line_0, col_0, len, 8 /* function */, 16 /* defaultLibrary */});
                } else if (namespaces.contains(t.lexeme)) {
                    raw_tokens.push_back(RawToken{line_0, col_0, len, 15 /* namespace */, (t.lexeme == "cpp" ? 16 : 0)});
                } else if (param_mods.contains({t.line, t.col})) {
                    raw_tokens.push_back(RawToken{line_0, col_0, len, 4 /* parameter */, param_mods[{t.line, t.col}]});
                } else if (type_decl_sites.contains({t.line, t.lexeme})) {
                    int tk = type_kinds.contains(t.lexeme) ? type_kinds[t.lexeme] : 1;
                    raw_tokens.push_back(RawToken{line_0, col_0, len, tk, 1 /* declaration */});
                } else if (type_kinds.contains(t.lexeme)) {
                    raw_tokens.push_back(RawToken{line_0, col_0, len, type_kinds[t.lexeme], 0});
                } else if (func_decl_lines.contains(t.line) && func_names.contains(t.lexeme)) {
                    raw_tokens.push_back(RawToken{line_0, col_0, len, func_names[t.lexeme], 1 /* declaration */});
                } else if (func_names.contains(t.lexeme)) {
                    raw_tokens.push_back(RawToken{line_0, col_0, len, func_names[t.lexeme], 0});
                } else if (var_decl_sites.contains({t.line, t.lexeme})) {
                    int mod = var_mut_state.contains(t.lexeme) ? var_mut_state[t.lexeme] : 4;
                    raw_tokens.push_back(RawToken{line_0, col_0, len, 5 /* variable */, mod | 1 /* declaration */});
                } else if (var_mut_state.contains(t.lexeme)) {
                    raw_tokens.push_back(RawToken{line_0, col_0, len, 5 /* variable */, var_mut_state[t.lexeme]});
                } else if (t.lexeme[0] >= 'A' && t.lexeme[0] <= 'Z') {
                    raw_tokens.push_back(RawToken{line_0, col_0, len, 1 /* struct */, 0});
                } else {
                    raw_tokens.push_back(RawToken{line_0, col_0, len, 5 /* variable */, 4 /* readonly */});
                }
                continue;
            }
        }

        // Relative delta encoding per LSP 3.17 specification
        std::string out = "[";
        int prev_line = 0;
        int prev_col = 0;
        bool first = true;
        for (const auto& tok : raw_tokens) {
            int delta_line = tok.line_0 - prev_line;
            int delta_col = (delta_line == 0) ? (tok.col_0 - prev_col) : tok.col_0;

            if (!first) out += ",";
            first = false;
            out += std::format("{},{},{},{},{}", delta_line, delta_col, tok.length, tok.type, tok.modifiers);

            prev_line = tok.line_0;
            prev_col = tok.col_0;
        }
        out += "]";
        return out;
    }

    struct TokenOccurrence {
        std::string uri;
        int line = 0;      // 0-based
        int start_col = 0; // 0-based
        int end_col = 0;   // 0-based
    };

    auto get_workspace_root_for_uri(const std::string& uri) -> std::filesystem::path {
        if (!workspace_root_.empty()) {
            std::error_code ec;
            if (std::filesystem::exists(workspace_root_, ec)) {
                return std::filesystem::path(workspace_root_);
            }
        }
        std::filesystem::path file_path = detail::uri_to_path(uri);
        std::filesystem::path p = file_path.parent_path();
        while (!p.empty() && p != p.parent_path()) {
            std::error_code ec;
            if (std::filesystem::exists(p / "build.cf", ec) ||
                std::filesystem::exists(p / ".git", ec) ||
                std::filesystem::exists(p / ".cfire", ec) ||
                std::filesystem::exists(p / ".vscode", ec) ||
                std::filesystem::exists(p / ".zed", ec)) {
                return p;
            }
            p = p.parent_path();
        }
        return file_path.parent_path();
    }

    auto discover_workspace_cf_files(const std::string& current_uri) -> std::vector<std::string> {
        std::vector<std::string> uris;
        std::unordered_set<std::string> seen;

        std::filesystem::path root = get_workspace_root_for_uri(current_uri);
        std::error_code ec;
        if (std::filesystem::exists(root, ec) && std::filesystem::is_directory(root, ec)) {
            for (std::filesystem::recursive_directory_iterator it(root, std::filesystem::directory_options::skip_permission_denied, ec), end;
                 it != end && !ec; it.increment(ec)) {
                if (it->is_directory(ec)) {
                    auto fname = it->path().filename().string();
                    if (fname.starts_with(".") || fname == "build" || fname == "vendor" || fname == "node_modules" || fname == "c_deps") {
                        it.disable_recursion_pending();
                        continue;
                    }
                } else if (it->is_regular_file(ec) && it->path().extension() == ".cf") {
                    auto p_str = it->path().string();
                    if (p_str.find("/.cfire/") == std::string::npos &&
                        p_str.find("/build/") == std::string::npos &&
                        p_str.find("/.git/") == std::string::npos) {
                        std::string u = detail::path_to_uri(it->path());
                        if (!seen.contains(u)) {
                            uris.push_back(u);
                            seen.insert(u);
                        }
                    }
                }
            }
        }

        for (const auto& [doc_uri, _] : docs_) {
            if (doc_uri.ends_with(".cf") && !seen.contains(doc_uri)) {
                uris.push_back(doc_uri);
                seen.insert(doc_uri);
            }
        }

        if (!seen.contains(current_uri)) {
            uris.push_back(current_uri);
        }
        return uris;
    }

    auto find_symbol_occurrences(const std::string& uri, int line, int character, std::string_view target_name, std::string_view qualifier) -> std::vector<TokenOccurrence> {
        std::vector<TokenOccurrence> refs;
        if (target_name.empty()) return refs;

        std::string text = get_doc_text(uri);
        auto lines = detail::split_lines(text);
        auto [ast_res, ws] = get_or_parse_ast(uri, text, line, character);
        auto env = ast_res ? detail::build_scope_bindings(*ast_res, ws, lines, line) : std::unordered_map<std::string, detail::LocalBindingInfo>{};

        // 1. Check if symbol is a function-local variable, parameter, or receiver
        bool is_local = (qualifier.empty() && env.find(std::string(target_name)) != env.end());
        if (is_local && ast_res) {
            int start_line = 1;
            int end_line = static_cast<int>(lines.size());
            int target_line_1 = line + 1;

            for (const auto& fn : ast_res->functions) {
                if (fn.line <= target_line_1) {
                    start_line = fn.line;
                }
            }
            for (const auto& t : ast_res->tests) {
                if (t.line <= target_line_1) {
                    start_line = t.line;
                }
            }
            for (const auto& fn : ast_res->functions) {
                if (fn.line > start_line && fn.line < end_line) {
                    end_line = fn.line - 1;
                }
            }
            for (const auto& t : ast_res->tests) {
                if (t.line > start_line && t.line < end_line) {
                    end_line = t.line - 1;
                }
            }
            for (const auto& st : ast_res->structs) {
                if (st.line > start_line && st.line < end_line) {
                    end_line = st.line - 1;
                }
            }

            if (auto toks = cfire::frontend::tokenize(detail::uri_to_path(uri), text)) {
                for (std::size_t i = 0; i < toks->size(); ++i) {
                    const auto& t = (*toks)[i];
                    if (t.line < start_line) continue;
                    if (t.line > end_line) break;
                    if (t.kind == cfire::frontend::TokenKind::Ident && t.lexeme == target_name) {
                        bool preceded_by_dot = (i > 0 && (*toks)[i - 1].kind == cfire::frontend::TokenKind::Dot);
                        if (!preceded_by_dot) {
                            int ln = std::max(0, t.line - 1);
                            int sc = std::max(0, t.col - 1);
                            int ec = sc + static_cast<int>(t.lexeme.size());
                            refs.push_back(TokenOccurrence{uri, ln, sc, ec});
                        }
                    }
                }
            }
            return refs;
        }

        // 2. Cross-workspace / top-level symbol resolution
        std::string defining_pkg_name;
        std::string defining_uri = uri;
        bool is_field = false;

        if (!qualifier.empty()) {
            for (const auto& pkg : ws.local_packages) {
                if (pkg.alias == qualifier) {
                    defining_pkg_name = pkg.ast.package_name;
                    defining_uri = pkg.uri;
                    break;
                }
            }
        } else if (ast_res) {
            defining_pkg_name = ast_res->package_name;
            for (const auto& st : ast_res->structs) {
                for (const auto& f : st.fields) {
                    if (f.name == target_name) {
                        is_field = true;
                        break;
                    }
                }
            }
        }

        auto all_cf_files = discover_workspace_cf_files(uri);

        for (const auto& doc_uri : all_cf_files) {
            std::string doc_text = get_doc_text(doc_uri);
            auto doc_ast_res = cfire::frontend::parse_source(detail::uri_to_path(doc_uri), doc_text);
            auto doc_toks = cfire::frontend::tokenize(detail::uri_to_path(doc_uri), doc_text);
            if (!doc_toks) continue;

            bool same_pkg = (doc_uri == defining_uri) || (doc_ast_res && !defining_pkg_name.empty() && doc_ast_res->package_name == defining_pkg_name);
            std::optional<std::string> import_alias;

            if (doc_ast_res) {
                auto doc_ws = resolve_workspace(doc_uri, *doc_ast_res);
                for (const auto& pkg : doc_ws.local_packages) {
                    if ((!defining_pkg_name.empty() && pkg.ast.package_name == defining_pkg_name) || pkg.uri == defining_uri) {
                        import_alias = pkg.alias;
                        break;
                    }
                }
            }

            if (!same_pkg && !import_alias.has_value() && !is_field && !defining_pkg_name.empty()) {
                continue;
            }

            for (std::size_t i = 0; i < doc_toks->size(); ++i) {
                const auto& t = (*doc_toks)[i];
                if (t.kind != cfire::frontend::TokenKind::Ident || t.lexeme != target_name) {
                    continue;
                }

                bool preceded_by_dot = (i > 0 && (*doc_toks)[i - 1].kind == cfire::frontend::TokenKind::Dot);

                if (same_pkg) {
                    if (!preceded_by_dot || is_field) {
                        int ln = std::max(0, t.line - 1);
                        int sc = std::max(0, t.col - 1);
                        int ec = sc + static_cast<int>(t.lexeme.size());
                        refs.push_back(TokenOccurrence{doc_uri, ln, sc, ec});
                    }
                } else if (import_alias.has_value()) {
                    if (i >= 2 && (*doc_toks)[i - 1].kind == cfire::frontend::TokenKind::Dot &&
                        (*doc_toks)[i - 2].lexeme == *import_alias) {
                        int ln = std::max(0, t.line - 1);
                        int sc = std::max(0, t.col - 1);
                        int ec = sc + static_cast<int>(t.lexeme.size());
                        refs.push_back(TokenOccurrence{doc_uri, ln, sc, ec});
                    } else if (!preceded_by_dot) {
                        int ln = std::max(0, t.line - 1);
                        int sc = std::max(0, t.col - 1);
                        int ec = sc + static_cast<int>(t.lexeme.size());
                        refs.push_back(TokenOccurrence{doc_uri, ln, sc, ec});
                    }
                } else if (is_field && preceded_by_dot) {
                    int ln = std::max(0, t.line - 1);
                    int sc = std::max(0, t.col - 1);
                    int ec = sc + static_cast<int>(t.lexeme.size());
                    refs.push_back(TokenOccurrence{doc_uri, ln, sc, ec});
                }
            }
        }

        return refs;
    }

    auto compute_references(const std::string& uri, int line, int character) -> std::string {
        std::string text = get_doc_text(uri);
        auto lines = detail::split_lines(text);
        auto sym = detail::symbol_at_position(lines, line, character);
        if (sym.name.empty()) return "[]";

        auto refs = find_symbol_occurrences(uri, line, character, sym.name, sym.qualifier);
        std::string out = "[";
        for (std::size_t i = 0; i < refs.size(); ++i) {
            if (i > 0) out += ",";
            const auto& r = refs[i];
            out += std::format(
                R"({{"uri":"{}","range":{{"start":{{"line":{},"character":{}}},"end":{{"line":{},"character":{}}}}}}})",
                detail::json_escape(r.uri), r.line, r.start_col, r.line, r.end_col);
        }
        out += "]";
        return out;
    }

    auto compute_prepare_rename(const std::string& uri, int line, int character) -> std::string {
        std::string text = get_doc_text(uri);
        auto lines = detail::split_lines(text);
        auto sym = detail::symbol_at_position(lines, line, character);
        if (sym.name.empty()) return "null";

        static const std::unordered_set<std::string_view> keywords = {
            "package", "import", "cpp", "as", "type", "struct", "enum", "interface",
            "map", "fn", "async", "in", "mut", "out", "move", "var",
            "if", "else", "for", "match", "defer", "return", "try", "or", "test",
            "true", "false", "none"
        };
        if (keywords.contains(sym.name)) return "null";

        static const std::unordered_set<std::string_view> primitives = {
            "i8", "i16", "i32", "i64", "u8", "u16", "u32", "u64", "f32", "f64",
            "bool", "char", "str", "void", "usize", "isize", "rawptr", "any"
        };
        if (primitives.contains(sym.name)) return "null";

        for (const auto& mod : cfire::doc::stdlib_catalog()) {
            if (sym.name == mod.ns_alias) return "null";
        }
        return std::format(
            R"({{"range":{{"start":{{"line":{},"character":{}}},"end":{{"line":{},"character":{}}}}},"placeholder":"{}"}})",
            line, sym.start_col, line, sym.end_col, detail::json_escape(sym.name));
    }

    auto compute_rename(const std::string& uri, int line, int character, std::string_view new_name) -> std::string {
        if (new_name.empty()) return "null";
        std::string text = get_doc_text(uri);
        auto lines = detail::split_lines(text);
        auto sym = detail::symbol_at_position(lines, line, character);
        if (sym.name.empty()) return "null";

        auto refs = find_symbol_occurrences(uri, line, character, sym.name, sym.qualifier);
        if (refs.empty()) return "null";

        std::unordered_map<std::string, std::vector<TokenOccurrence>> by_uri;
        for (const auto& r : refs) {
            by_uri[r.uri].push_back(r);
        }

        std::string changes = "{";
        bool first_uri = true;
        for (const auto& [doc_uri, doc_refs] : by_uri) {
            if (!first_uri) changes += ",";
            first_uri = false;
            changes += std::format("\"{}\":[", detail::json_escape(doc_uri));
            for (std::size_t i = 0; i < doc_refs.size(); ++i) {
                if (i > 0) changes += ",";
                const auto& r = doc_refs[i];
                changes += std::format(
                    R"({{"range":{{"start":{{"line":{},"character":{}}},"end":{{"line":{},"character":{}}}}},"newText":"{}"}})",
                    r.line, r.start_col, r.line, r.end_col, detail::json_escape(new_name));
            }
            changes += "]";
        }
        changes += "}";
        return std::format(R"({{"changes":{}}})", changes);
    }

    auto compute_code_actions(const std::string& uri, int req_line) -> std::string {
        std::string text = get_doc_text(uri);
        auto lines = detail::split_lines(text);
        auto diags = compute_diagnostics_for_doc(uri, text);
        if (diags.empty()) return "[]";

        auto ast_res = cfire::frontend::parse_source(detail::uri_to_path(uri), text);
        std::string out = "[";
        bool first = true;
        auto add_action = [&](std::string_view title, std::string_view diag_code, int edit_line, int edit_col, std::string_view insert_text, bool is_preferred = true) {
            (void)diag_code;
            if (!first) out += ",";
            first = false;
            out += std::format(
                R"({{"title":"{}","kind":"quickfix","isPreferred":{},"edit":{{"changes":{{"{}":[{{"range":{{"start":{{"line":{},"character":{}}},"end":{{"line":{},"character":{}}}}},"newText":"{}"}}]}}}}}})",
                detail::json_escape(title),
                is_preferred ? "true" : "false",
                detail::json_escape(uri),
                edit_line, edit_col, edit_line, edit_col,
                detail::json_escape(insert_text));
        };

        for (const auto& d : diags) {
            if (req_line >= 0 && d.line != req_line) {
                if (d.code != "cfire/NON-EXHAUSTIVE-MATCH") continue;
            }
            if (d.code == "cfire/MUT-CALLSITE") {
                add_action("Insert 'mut ' at call site (cfire/MUT-CALLSITE)", d.code, d.line, d.col, "mut ");
            } else if (d.code == "cfire/OUT-CALLSITE") {
                add_action("Insert 'out ' at call site (cfire/OUT-CALLSITE)", d.code, d.line, d.col, "out ");
            } else if (d.code == "cfire/IMMUTABLE-MUT-ARG" || d.code == "cfire/MUT-RECEIVER") {
                // Extract variable name from `immutable variable '<var>'`
                std::string var_name;
                if (auto p = d.message.find("immutable variable '"); p != std::string::npos) {
                    p += 20;
                    if (auto e = d.message.find('\'', p); e != std::string::npos) {
                        var_name = d.message.substr(p, e - p);
                    }
                }
                if (!var_name.empty() && ast_res) {
                    auto ws = resolve_workspace(uri, *ast_res);
                    auto env = detail::build_scope_bindings(*ast_res, ws, lines, d.line);
                    if (auto it = env.find(var_name); it != env.end()) {
                        int decl_ln = std::max(0, it->second.decl_line - 1);
                        if (static_cast<std::size_t>(decl_ln) < lines.size()) {
                            const std::string& dline = lines[static_cast<std::size_t>(decl_ln)];
                            if (auto vpos = dline.find(var_name); vpos != std::string::npos) {
                                add_action(
                                    std::format("Declare '{}' as mutable ('mut {}')", var_name, var_name),
                                    d.code, decl_ln, static_cast<int>(vpos), "mut ");
                            }
                        }
                    }
                }
            } else if (d.code == "cfire/NON-EXHAUSTIVE-MATCH") {
                std::string enum_name;
                std::string fallback_variant;
                if (auto p = d.message.find("non-exhaustive match on enum '"); p != std::string::npos) {
                    p += 30;
                    if (auto e = d.message.find('\'', p); e != std::string::npos) {
                        enum_name = d.message.substr(p, e - p);
                    }
                }
                if (auto p = d.message.find("missing variant '"); p != std::string::npos) {
                    p += 17;
                    if (auto e = d.message.find('\'', p); e != std::string::npos) {
                        fallback_variant = d.message.substr(p, e - p);
                    }
                }

                auto ws = ast_res ? resolve_workspace(uri, *ast_res) : detail::WorkspaceContext{};
                const cfire::frontend::EnumDecl* target_enum = nullptr;
                if (ast_res && !enum_name.empty()) {
                    for (const auto& en : ast_res->enums) {
                        if (en.name == enum_name) {
                            target_enum = &en;
                            break;
                        }
                    }
                    if (!target_enum) {
                        for (const auto& pkg : ws.local_packages) {
                            for (const auto& en : pkg.ast.enums) {
                                if (en.name == enum_name) {
                                    target_enum = &en;
                                    break;
                                }
                            }
                            if (target_enum) break;
                        }
                    }
                }

                // Determine base indentation of the match statement
                std::string base_indent;
                if (static_cast<std::size_t>(d.line) < lines.size()) {
                    const std::string& match_ln = lines[static_cast<std::size_t>(d.line)];
                    for (char c : match_ln) {
                        if (c == ' ' || c == '\t') base_indent += c;
                        else break;
                    }
                }
                std::string arm_indent = base_indent + "    ";
                std::string body_indent = arm_indent + "    ";

                // Locate the matching closing brace of the match block
                int match_close_line = -1;
                int match_close_col = 0;
                int brace_depth = 0;
                bool found_open_brace = false;

                for (int ln = d.line; ln < static_cast<int>(lines.size()); ++ln) {
                    const std::string& cur_ln = lines[static_cast<std::size_t>(ln)];
                    for (int col = 0; col < static_cast<int>(cur_ln.size()); ++col) {
                        char c = cur_ln[static_cast<std::size_t>(col)];
                        if (c == '{') {
                            found_open_brace = true;
                            ++brace_depth;
                        } else if (c == '}') {
                            if (found_open_brace) {
                                --brace_depth;
                                if (brace_depth == 0) {
                                    match_close_line = ln;
                                    match_close_col = col;
                                    break;
                                }
                            }
                        }
                    }
                    if (match_close_line != -1) break;
                }

                if (match_close_line == -1) {
                    match_close_line = d.line + 1;
                    match_close_col = 0;
                }

                if (req_line >= 0 && (req_line < d.line || req_line > match_close_line)) {
                    continue;
                }

                // Collect covered variant names
                std::unordered_set<std::string> covered;
                if (ast_res) {
                    const cfire::frontend::MatchStmt* target_match = nullptr;
                    auto find_match = [&](auto self, const std::vector<cfire::frontend::StmtPtr>& stmts) -> void {
                        if (target_match) return;
                        for (const auto& s : stmts) {
                            if (!s) continue;
                            if (const auto* ms = std::get_if<cfire::frontend::MatchStmt>(&s->node)) {
                                if (ms->line - 1 == d.line || (ms->line - 1 <= d.line && d.line <= match_close_line)) {
                                    target_match = ms;
                                    return;
                                }
                            }
                            if (const auto* ifs = std::get_if<cfire::frontend::IfStmt>(&s->node)) {
                                self(self, ifs->then_body);
                                self(self, ifs->else_body);
                            } else if (const auto* fs = std::get_if<cfire::frontend::ForStmt>(&s->node)) {
                                self(self, fs->body);
                            } else if (const auto* ms2 = std::get_if<cfire::frontend::MatchStmt>(&s->node)) {
                                for (const auto& arm : ms2->arms) self(self, arm.body);
                            }
                        }
                    };
                    for (const auto& fn : ast_res->functions) find_match(find_match, fn.body);
                    for (const auto& t : ast_res->tests) find_match(find_match, t.body);
                    if (target_match) {
                        for (const auto& arm : target_match->arms) {
                            covered.insert(arm.variant_name);
                        }
                    }
                }

                // Generate missing arms
                std::string arms_text;
                if (target_enum) {
                    for (const auto& v : target_enum->variants) {
                        if (!covered.contains(v.name)) {
                            arms_text += arm_indent + v.name;
                            if (!v.payload_types.empty()) {
                                arms_text += "(";
                                for (std::size_t i = 0; i < v.payload_types.size(); ++i) {
                                    if (i > 0) arms_text += ", ";
                                    arms_text += std::format("v{}", i);
                                }
                                arms_text += ")";
                            }
                            arms_text += " => {\n" + body_indent + "\n" + arm_indent + "}\n";
                        }
                    }
                } else if (!fallback_variant.empty()) {
                    arms_text = arm_indent + fallback_variant + " => {\n" + body_indent + "\n" + arm_indent + "}\n";
                }

                int edit_ln = match_close_line;
                int edit_col = 0;
                if (match_close_col > 0) {
                    const std::string& cl_ln = lines[static_cast<std::size_t>(match_close_line)];
                    std::size_t first_non_ws = cl_ln.find_first_not_of(" \t");
                    if (first_non_ws != std::string::npos && static_cast<int>(first_non_ws) == match_close_col) {
                        edit_col = 0;
                    } else {
                        edit_col = match_close_col;
                        arms_text = "\n" + arms_text + base_indent;
                    }
                }

                if (!arms_text.empty()) {
                    add_action(
                        std::format("Fill missing match arms for '{}' (cfire/NON-EXHAUSTIVE-MATCH)", enum_name.empty() ? "enum" : enum_name),
                        d.code, edit_ln, edit_col, arms_text, true);
                }

                std::string wildcard_text = arm_indent + "_ => {\n" + body_indent + "\n" + arm_indent + "}\n";
                if (edit_col > 0) {
                    wildcard_text = "\n" + wildcard_text + base_indent;
                }
                add_action("Add wildcard pattern '_ => { ... }' (cfire/NON-EXHAUSTIVE-MATCH)",
                           d.code, edit_ln, edit_col, wildcard_text, false);
            }
        }
        out += "]";
        return out;
    }

    auto compute_document_symbols(const std::string& uri) -> std::string {
        std::string text = get_doc_text(uri);
        auto [ast_res, ws] = get_or_parse_ast(uri, text);
        if (!ast_res) return "[]";

        std::string out = "[";
        bool first = true;
        auto make_sym = [](std::string_view name, std::string_view detail_str, int kind, int one_based_line, std::string_view children_json = "[]") -> std::string {
            int ln = std::max(0, one_based_line - 1);
            return std::format(
                R"({{"name":"{}","detail":"{}","kind":{},"range":{{"start":{{"line":{},"character":0}},"end":{{"line":{},"character":20}}}},"selectionRange":{{"start":{{"line":{},"character":0}},"end":{{"line":{},"character":10}}}},"children":{}}})",
                detail::json_escape(name), detail::json_escape(detail_str), kind, ln, ln, ln, ln, children_json);
        };

        for (const auto& st : ast_res->structs) {
            std::string children = "[";
            for (std::size_t i = 0; i < st.fields.size(); ++i) {
                if (i > 0) children += ",";
                const auto& f = st.fields[i];
                children += make_sym(f.name, cfire::fmt::format_type_ref(f.type), 8, f.line);
            }
            children += "]";
            if (!first) out += ",";
            first = false;
            out += make_sym(st.name, "struct", 23, st.line, children);
        }
        for (const auto& en : ast_res->enums) {
            std::string children = "[";
            for (std::size_t i = 0; i < en.variants.size(); ++i) {
                if (i > 0) children += ",";
                const auto& v = en.variants[i];
                std::string ptypes;
                for (std::size_t j = 0; j < v.payload_types.size(); ++j) {
                    if (j > 0) ptypes += ", ";
                    ptypes += cfire::fmt::format_type_ref(v.payload_types[j]);
                }
                children += make_sym(v.name, ptypes, 22, v.line);
            }
            children += "]";
            if (!first) out += ",";
            first = false;
            out += make_sym(en.name, "enum", 10, en.line, children);
        }
        for (const auto& iface : ast_res->interfaces) {
            if (!first) out += ",";
            first = false;
            out += make_sym(iface.name, "interface", 11, iface.line);
        }
        for (const auto& fn : ast_res->functions) {
            if (!first) out += ",";
            first = false;
            int kind = fn.receiver ? 6 : 12; // 6 = Method, 12 = Function
            std::string label = fn.receiver ? std::format("({}).{}", cfire::fmt::format_type_ref(fn.receiver->type), fn.name) : fn.name;
            out += make_sym(label, cfire::fmt::format_fn_signature(fn), kind, fn.line);
        }
        for (const auto& gv : ast_res->global_vars) {
            if (!first) out += ",";
            first = false;
            out += make_sym(gv.name, "var", 13, gv.line);
        }
        for (const auto& t : ast_res->tests) {
            if (!first) out += ",";
            first = false;
            out += make_sym("test: " + t.name, "test", 12, t.line);
        }
        out += "]";
        return out;
    }

    auto compute_workspace_symbols(std::string_view query) -> std::string {
        auto matches_query = [&](std::string_view name) -> bool {
            if (query.empty()) return true;
            std::string ln(name);
            std::string lq(query);
            std::transform(ln.begin(), ln.end(), ln.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            std::transform(lq.begin(), lq.end(), lq.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return ln.find(lq) != std::string::npos;
        };

        std::string out = "[";
        bool first = true;
        auto add_ws_sym = [&](std::string_view name, int kind, std::string_view container, const std::string& uri, int one_based_line) {
            if (!matches_query(name)) return;
            if (!first) out += ",";
            first = false;
            int ln = std::max(0, one_based_line - 1);
            out += std::format(
                R"({{"name":"{}","kind":{},"containerName":"{}","location":{{"uri":"{}","range":{{"start":{{"line":{},"character":0}},"end":{{"line":{},"character":12}}}}}}}})",
                detail::json_escape(name), kind, detail::json_escape(container), detail::json_escape(uri), ln, ln);
        };

        // 1. Scan all open documents
        std::unordered_set<std::string> scanned_uris;
        for (const auto& [doc_uri, doc_text] : docs_) {
            scanned_uris.insert(doc_uri);
            if (auto ast = cfire::frontend::parse_source(detail::uri_to_path(doc_uri), doc_text)) {
                for (const auto& st : ast->structs) add_ws_sym(st.name, 23, ast->package_name, doc_uri, st.line);
                for (const auto& en : ast->enums) add_ws_sym(en.name, 10, ast->package_name, doc_uri, en.line);
                for (const auto& fn : ast->functions) add_ws_sym(fn.name, 12, ast->package_name, doc_uri, fn.line);
            }
        }
        // 2. Scan `core:*` stdlib catalog
        std::string fallback_uri = docs_.empty() ? "file:///stdlib.cf" : docs_.begin()->first;
        for (const auto& mod : cfire::doc::stdlib_catalog()) {
            for (const auto& fn : mod.functions) {
                add_ws_sym(mod.ns_alias + "." + fn.name, 12, mod.import_path, fallback_uri, 1);
            }
        }
        out += "]";
        return out;
    }

    auto compute_formatting(const std::string& uri) -> std::string {
        std::string text = get_doc_text(uri);
        auto formatted = cfire::fmt::format_source(text, detail::uri_to_path(uri));
        if (!formatted || *formatted == text) return "[]";
        auto lines = detail::split_lines(text);
        int end_line = static_cast<int>(lines.size()) + 1;
        return std::format(
            R"([{{"range":{{"start":{{"line":0,"character":0}},"end":{{"line":{},"character":0}}}},"newText":"{}"}}])",
            end_line, detail::json_escape(*formatted));
    }

    auto compute_range_formatting(const std::string& uri, int start_line, int end_line) -> std::string {
        std::string text = get_doc_text(uri);
        auto formatted = cfire::fmt::format_range(text, start_line, end_line, detail::uri_to_path(uri));
        if (!formatted) return "[]";

        auto lines = detail::split_lines(text);
        if (lines.empty()) return "[]";

        start_line = std::clamp(start_line, 0, static_cast<int>(lines.size()) - 1);
        end_line = std::clamp(end_line, start_line, static_cast<int>(lines.size()) - 1);

        std::string orig_selected;
        for (int i = start_line; i <= end_line; ++i) {
            orig_selected += lines[static_cast<std::size_t>(i)];
            if (i < end_line) orig_selected += "\n";
        }

        if (*formatted == orig_selected) return "[]";

        int end_char = static_cast<int>(lines[static_cast<std::size_t>(end_line)].size());
        return std::format(
            R"([{{"range":{{"start":{{"line":{},"character":0}},"end":{{"line":{},"character":{}}}}},"newText":"{}"}}])",
            start_line, end_line, end_char, detail::json_escape(*formatted));
    }
};

} // namespace cfire::lsp

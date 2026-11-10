#pragma once
// src/lsp/server.hpp — Full-Featured Language Server Protocol (LSP 3.17) Server (`cfire::lsp::Server`)

#include "../buildsys.hpp"
#include "../diagnostics.hpp"
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
                std::string_view len_sv = std::string_view(line).substr(15);
                std::size_t len = cfire::detail::parse_int<std::size_t>(len_sv).value_or(0);
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
                R"({{"jsonrpc":"2.0","id":{},"result":{{"capabilities":{{"textDocumentSync":1,"hoverProvider":true,"definitionProvider":true,"typeDefinitionProvider":true,"implementationProvider":true,"callHierarchyProvider":true,"referencesProvider":true,"renameProvider":{{"prepareProvider":true}},"codeActionProvider":true,"documentFormattingProvider":true,"documentRangeFormattingProvider":true,"documentOnTypeFormattingProvider":{{"firstTriggerCharacter":"\n","moreTriggerCharacter":["}}"]}},"documentSymbolProvider":true,"workspaceSymbolProvider":true,"inlayHintProvider":true,"completionProvider":{{"triggerCharacters":[".",":","@"]}},"signatureHelpProvider":{{"triggerCharacters":["(",","]}},"semanticTokensProvider":{{"legend":{{"tokenTypes":["type","struct","enum","interface","parameter","variable","property","enumMember","function","method","keyword","modifier","string","number","operator","namespace"],"tokenModifiers":["declaration","definition","readonly","modification","defaultLibrary","async"]}},"full":true}}}},"serverInfo":{{"name":"cfire-lsp","version":"{}"}}}}}})",
                id, cfire::version::kVersion);
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

        if (method == "textDocument/implementation") {
            std::string id = id_opt.value_or("1");
            auto uri = detail::extract_json_string(msg, "uri").value_or("");
            std::size_t pos_idx = detail::find_json_key(msg, "position");
            int line = detail::extract_json_int_after(msg, "line", pos_idx != std::string_view::npos ? pos_idx : 0).value_or(0);
            int character = detail::extract_json_int_after(msg, "character", pos_idx != std::string_view::npos ? pos_idx : 0).value_or(0);
            std::string impl_json = compute_implementation(uri, line, character);
            send_rpc(out, std::format(R"({{"jsonrpc":"2.0","id":{},"result":{}}})", id, impl_json));
            return true;
        }

        if (method == "textDocument/prepareCallHierarchy") {
            std::string id = id_opt.value_or("1");
            auto uri = detail::extract_json_string(msg, "uri").value_or("");
            std::size_t pos_idx = detail::find_json_key(msg, "position");
            int line = detail::extract_json_int_after(msg, "line", pos_idx != std::string_view::npos ? pos_idx : 0).value_or(0);
            int character = detail::extract_json_int_after(msg, "character", pos_idx != std::string_view::npos ? pos_idx : 0).value_or(0);
            std::string prep_json = compute_prepare_call_hierarchy(uri, line, character);
            send_rpc(out, std::format(R"({{"jsonrpc":"2.0","id":{},"result":{}}})", id, prep_json));
            return true;
        }

        if (method == "callHierarchy/incomingCalls") {
            std::string id = id_opt.value_or("1");
            std::string calls_json = compute_incoming_calls(msg);
            send_rpc(out, std::format(R"({{"jsonrpc":"2.0","id":{},"result":{}}})", id, calls_json));
            return true;
        }

        if (method == "callHierarchy/outgoingCalls") {
            std::string id = id_opt.value_or("1");
            std::string calls_json = compute_outgoing_calls(msg);
            send_rpc(out, std::format(R"({{"jsonrpc":"2.0","id":{},"result":{}}})", id, calls_json));
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

        if (method == "textDocument/onTypeFormatting") {
            std::string id = id_opt.value_or("1");
            auto uri = detail::extract_json_string(msg, "uri").value_or("");
            std::size_t pos_idx = detail::find_json_key(msg, "position");
            int line = detail::extract_json_int_after(msg, "line", pos_idx != std::string_view::npos ? pos_idx : 0).value_or(0);
            int character = detail::extract_json_int_after(msg, "character", pos_idx != std::string_view::npos ? pos_idx : 0).value_or(0);
            std::string ch = detail::extract_json_string(msg, "ch").value_or("\n");
            std::string edits_json = compute_on_type_formatting(uri, line, character, ch);
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

        auto toolchain_pkg_dir = cfire::build::locate_toolchain_layout().pkg_dir;
        auto collect_imports = [&](const cfire::frontend::FileAST& src_ast, const std::filesystem::path& src_dir) {
            for (const auto& imp : src_ast.imports) {
                if (!imp.is_cpp && (imp.path.starts_with("./") || imp.path.starts_with("../") || imp.path.starts_with("pkg:"))) {
                    std::filesystem::path target = imp.path.starts_with("pkg:")
                        ? (toolchain_pkg_dir / imp.path.substr(4))
                        : (src_dir / imp.path);
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
                    for (int depth = 0; depth < 4 && !search_dir.empty(); ++depth) {
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
                                                    if (std::filesystem::is_directory(inc_dir / "include", ec)) {
                                                        cflags.push_back("-I" + (inc_dir / "include").string());
                                                    }
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
                            std::string pc_out = cfire::bridge::run_command_capture(
                                "pkg-config --cflags " + cfire::bridge::shell_quote(attr.args[0]), rc);
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

        // Also automatically discover same-directory sibling `.cf` files sharing `ast.package_name` (ERG-02c)
        if (!ast.package_name.empty()) {
            std::error_code ec;
            auto canon_dir = std::filesystem::weakly_canonical(base_dir, ec);
            auto canon_tmp = std::filesystem::weakly_canonical(std::filesystem::temp_directory_path(ec), ec);
            bool is_shared_tmp = (canon_dir == canon_tmp || base_dir == "/tmp" || base_dir == "/private/tmp");
            if (!is_shared_tmp) {
                auto self_canon = std::filesystem::weakly_canonical(file_path, ec).string();
                for (const auto& entry : std::filesystem::directory_iterator(base_dir, ec)) {
                    if (!entry.is_regular_file() || entry.path().extension() != ".cf") continue;
                    auto fname = entry.path().filename().string();
                    if (fname == "build.cf") continue;
                    auto sib_canon = std::filesystem::weakly_canonical(entry.path(), ec).string();
                    if (sib_canon == self_canon) continue;
                    bool already_added = false;
                    for (const auto& existing : ws.local_packages) {
                        if (std::filesystem::weakly_canonical(existing.file_path, ec).string() == sib_canon) {
                            already_added = true;
                            break;
                        }
                    }
                    if (already_added) continue;
                    std::string sub_uri = detail::path_to_uri(entry.path());
                    std::string sub_text = get_doc_text(sub_uri);
                    if (auto sub_ast = cfire::frontend::parse_source(entry.path().string(), sub_text)) {
                        if (sub_ast->package_name == ast.package_name) {
                            detail::ImportedPkgFile pkg;
                            pkg.alias = sub_ast->package_name;
                            pkg.import_path = "./" + fname;
                            pkg.file_path = entry.path().string();
                            pkg.uri = sub_uri;
                            pkg.ast = std::move(*sub_ast);
                            ws.local_packages.push_back(std::move(pkg));
                        }
                    }
                }
            }
        }

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
        std::string path = detail::uri_to_path(uri);
        auto pres = cfire::frontend::parse_source_resilient(path, text);
        if (pres.ok()) {
            auto ws = resolve_workspace(uri, pres.ast);
            last_good_ast_[uri] = pres.ast;
            last_good_ws_[uri] = ws;
            return {pres.ast, ws};
        }

        // Recovery 1: If file has trailing dot(s) or colons (either at cursor or end-of-line), patch them with placeholders
        {
            auto lines = detail::split_lines(text);
            bool modified = false;
            for (std::size_t li = 0; li < lines.size(); ++li) {
                auto& ln = lines[li];
                auto end_pos = ln.find_last_not_of(" \t\r\n");
                if (end_pos != std::string::npos) {
                    if (ln[end_pos] == '.') {
                        ln.insert(end_pos + 1, "__cfire_placeholder__");
                        modified = true;
                        continue;
                    } else if (end_pos >= 1 && ln[end_pos] == ':' && ln[end_pos - 1] == ':') {
                        ln.insert(end_pos + 1, "__cfire_placeholder__");
                        modified = true;
                        continue;
                    }
                }
                if (line >= 0 && static_cast<std::size_t>(line) == li &&
                    col > 0 && static_cast<std::size_t>(col) <= ln.size()) {
                    char prev = ln[static_cast<std::size_t>(col - 1)];
                    bool next_non_ident = (static_cast<std::size_t>(col) == ln.size()) ||
                        (!std::isalnum(static_cast<unsigned char>(ln[static_cast<std::size_t>(col)])) &&
                         ln[static_cast<std::size_t>(col)] != '_');
                    if (prev == '.' && next_non_ident) {
                        ln.insert(static_cast<std::size_t>(col), "__cfire_placeholder__");
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

        if (!pres.ast.functions.empty() || !pres.ast.structs.empty() || !pres.ast.enums.empty()) {
            auto ws = resolve_workspace(uri, pres.ast);
            last_good_ast_[uri] = pres.ast;
            last_good_ws_[uri] = ws;
            return {pres.ast, ws};
        }

        // Recovery 2: Fall back to last known valid AST
        if (auto it = last_good_ast_.find(uri); it != last_good_ast_.end()) {
            return {it->second, last_good_ws_[uri]};
        }

        return {std::nullopt, {}};
    }

    auto compute_diagnostics_for_doc(const std::string& uri, const std::string& text, bool allow_cpp_fallback = false) -> std::vector<detail::DiagnosticEntry> {
        using namespace cfire::frontend;
        std::vector<detail::DiagnosticEntry> entries;
        std::string file_path = detail::uri_to_path(uri);
        auto pres = parse_source_resilient(file_path, text);

        auto parse_err = [&](const std::string& err_str, std::string_view fallback_code, int severity = 1) -> detail::DiagnosticEntry {
            auto sd = cfire::diag::parse_diagnostic_string(
                err_str, file_path, severity == 2 ? "warning" : "error", fallback_code);
            return detail::DiagnosticEntry{
                std::max(0, sd.line - 1),
                std::max(0, sd.col - 1),
                sd.code,
                err_str,
                severity
            };
        };

        if (!pres.ok()) {
            for (const auto& err : pres.errors) {
                entries.push_back(parse_err(err, "cfire/SYNTAX", 1));
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
                for (const auto& gv : pkg.ast.global_vars) merged.global_vars.push_back(gv);
                for (const auto& fn : pkg.ast.functions) merged.functions.insert(merged.functions.begin(), fn);
            }
            auto checked = analyze_resilient(std::move(merged));
            for (const auto& err : checked.errors) {
                entries.push_back(parse_err(err, "cfire/SEMA", 1));
            }
            for (const auto& warn : checked.warnings) {
                entries.push_back(parse_err(warn, "cfire/WARNING", 2));
            }
            if (allow_cpp_fallback && checked.errors.empty() && !file_path.empty() && debounce_duration_.count() >= 50) {
                auto layout = cfire::build::locate_toolchain_layout(nullptr);
                if (!layout.runtime_dir.empty()) {
                    bool is_test_f = file_path.ends_with("_test.cf");
                    std::string cpp_code = cfire::frontend::emit_cpp(checked, is_test_f);
                    cfire::build::BuildPlan plan;
                    plan.entry_file = file_path;
                    plan.cflags = {"-std=c++2b", "-Wno-c++11-narrowing", "-I" + layout.runtime_dir.string()};
                    std::error_code ec;
                    if (std::filesystem::is_directory(layout.runtime_dir / "vendor", ec)) {
                        plan.cflags.push_back("-I" + (layout.runtime_dir / "vendor").string());
                    }
                    auto syntax_check = cfire::build::verify_cpp_syntax_only(file_path, cpp_code, plan, layout.runtime_dir);
                    if (!syntax_check) {
                        auto clang_lines = cfire::diag::split_lines(syntax_check.error());
                        for (const auto& cl : clang_lines) {
                            if (cl.contains(": error:")) {
                                entries.push_back(parse_err(cl, "cfire/TYPE-ERROR", 1));
                            }
                        }
                    }
                }
            }
        }
        {
            std::lock_guard<std::mutex> lock(diag_store_mutex_);
            last_diagnostics_[uri] = entries;
        }
        return entries;
    }

    void run_and_publish_diagnostics(const std::string& uri, const std::string& text) {
        auto entries = compute_diagnostics_for_doc(uri, text, true);
        std::string diags_json = "[";
        for (std::size_t i = 0; i < entries.size(); ++i) {
            if (i > 0) diags_json += ",";
            const auto& d = entries[i];
            diags_json += std::format(
                R"({{"range":{{"start":{{"line":{},"character":{}}},"end":{{"line":{},"character":{}}}}},"severity":{},"code":"{}","source":"cfire","message":"{}"}})",
                d.line, d.col, d.line, d.col + 10, d.severity, detail::json_escape(d.code), detail::json_escape(d.message));
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
                R"({{"range":{{"start":{{"line":{},"character":{}}},"end":{{"line":{},"character":{}}}}},"severity":{},"code":"{}","source":"cfire","message":"{}"}})",
                d.line, d.col, d.line, d.col + 10, d.severity, detail::json_escape(d.code), detail::json_escape(d.message));
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

    auto compute_hover(const std::string& uri, int line, int character) -> std::string;
    auto compute_signature_help(const std::string& uri, int line, int character) -> std::string;
    auto compute_inlay_hints(const std::string& uri) -> std::string;
    auto compute_import_edit(const std::vector<std::string>& lines, std::string_view import_path) -> std::string;
    auto compute_completions(const std::string& uri, int line, int character) -> std::string;
    auto compute_definition(const std::string& uri, int line, int character) -> std::string;
    auto compute_type_definition(const std::string& uri, int line, int character) -> std::string;
    auto compute_implementation(const std::string& uri, int line, int character) -> std::string;
    void collect_expr_calls(const cfire::frontend::ExprPtr& e, std::vector<std::pair<std::string, int>>& calls);
    void collect_stmt_calls(const std::vector<cfire::frontend::StmtPtr>& stmts, std::vector<std::pair<std::string, int>>& calls);
    auto compute_prepare_call_hierarchy(const std::string& uri, int line, int character) -> std::string;
    auto compute_incoming_calls(std::string_view msg) -> std::string;
    auto compute_outgoing_calls(std::string_view msg) -> std::string;
    auto compute_semantic_tokens(const std::string& uri) -> std::string;

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

    auto find_symbol_occurrences(const std::string& uri, int line, int character, std::string_view target_name, std::string_view qualifier) -> std::vector<TokenOccurrence>;
    auto compute_references(const std::string& uri, int line, int character) -> std::string;
    auto compute_prepare_rename(const std::string& uri, int line, int character) -> std::string;
    auto compute_rename(const std::string& uri, int line, int character, std::string_view new_name) -> std::string;
    auto compute_document_symbols(const std::string& uri) -> std::string;
    auto compute_workspace_symbols(std::string_view query) -> std::string;

    auto compute_code_actions(const std::string& uri, int req_line) -> std::string {
        std::string text = get_doc_text(uri);
        auto lines = detail::split_lines(text);
        auto diags = compute_diagnostics_for_doc(uri, text);
        if (diags.empty()) return "[]";

        auto ast_res = cfire::frontend::parse_source(detail::uri_to_path(uri), text);
        auto ws = ast_res ? resolve_workspace(uri, *ast_res) : detail::WorkspaceContext{};
        std::vector<const cfire::frontend::EnumDecl*> extra_enums;
        for (const auto& pkg : ws.local_packages) {
            for (const auto& en : pkg.ast.enums) {
                extra_enums.push_back(&en);
            }
        }

        std::string out = "[";
        bool first = true;
        std::unordered_set<std::string> emitted_actions;
        auto add_action = [&](std::string_view title, int start_line, int start_col, int end_line, int end_col, std::string_view insert_text, bool is_preferred) {
            std::string dedup_key = std::format("{}:{}:{}:{}:{}:{}", title, start_line, start_col, end_line, end_col, insert_text);
            if (!emitted_actions.insert(dedup_key).second) return;
            if (!first) out += ",";
            first = false;
            out += std::format(
                R"({{"title":"{}","kind":"quickfix","isPreferred":{},"edit":{{"changes":{{"{}":[{{"range":{{"start":{{"line":{},"character":{}}},"end":{{"line":{},"character":{}}}}},"newText":"{}"}}]}}}}}})",
                detail::json_escape(title),
                is_preferred ? "true" : "false",
                detail::json_escape(uri),
                start_line, start_col, end_line, end_col,
                detail::json_escape(insert_text));
        };

        for (const auto& d : diags) {
            if (req_line >= 0 && d.line != req_line && d.code != "cfire/NON-EXHAUSTIVE-MATCH") {
                continue;
            }
            cfire::diag::StructuredDiagnostic sd;
            sd.line = d.line + 1;
            sd.col = d.col + 1;
            sd.code = d.code;
            sd.message = d.message;
            cfire::diag::synthesize_fixits_for_diagnostic(sd, ast_res ? &(*ast_res) : nullptr, lines, extra_enums);

            for (const auto& fix : sd.fixits) {
                int s_ln = std::max(0, fix.start_line - 1);
                int s_col = std::max(0, fix.start_col - 1);
                int e_ln = std::max(0, fix.end_line - 1);
                int e_col = std::max(0, fix.end_col - 1);
                if (d.code == "cfire/NON-EXHAUSTIVE-MATCH" && req_line >= 0) {
                    if (req_line < d.line || req_line > s_ln) continue;
                }
                add_action(fix.title, s_ln, s_col, e_ln, e_col, fix.replacement, fix.is_preferred);
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

    auto compute_on_type_formatting(const std::string& uri, int line, int character, std::string_view ch) -> std::string {
        (void)character;
        if (line < 0) return "[]";
        std::string text = get_doc_text(uri);
        auto lines = detail::split_lines(text);
        while (static_cast<std::size_t>(line) >= lines.size()) {
            lines.emplace_back("");
        }

        int indent = 0;
        bool in_raw_string = false;
        for (int i = 0; i < line; ++i) {
            const std::string& raw = lines[static_cast<std::size_t>(i)];
            if (in_raw_string) {
                auto close_bt = raw.find('`');
                if (close_bt != std::string::npos) {
                    in_raw_string = false;
                    int net_delta = cfire::fmt::detail::scan_line_brace_delta(raw, close_bt + 1, in_raw_string);
                    indent = std::max(0, indent + net_delta);
                }
                continue;
            }
            std::string trimmed = cfire::fmt::detail::trim_ws(raw);
            if (trimmed.empty()) continue;
            int net_delta = cfire::fmt::detail::scan_line_brace_delta(trimmed, 0, in_raw_string);
            indent = std::max(0, indent + net_delta);
        }
        if (in_raw_string) return "[]";

        const std::string& cur_ln = lines[static_cast<std::size_t>(line)];
        int ws_len = 0;
        while (ws_len < static_cast<int>(cur_ln.size()) &&
               (cur_ln[static_cast<std::size_t>(ws_len)] == ' ' || cur_ln[static_cast<std::size_t>(ws_len)] == '\t')) {
            ++ws_len;
        }
        std::string trimmed = cfire::fmt::detail::trim_ws(cur_ln);
        int leading_close = 0;
        for (char c : trimmed) {
            if (c == '}' || c == ')' || c == ']') ++leading_close;
            else break;
        }
        if (ch == "}" && leading_close == 0) {
            return "[]";
        }
        int target_indent = std::max(0, indent - leading_close);
        std::string target_ws(static_cast<std::size_t>(target_indent * 4), ' ');
        if (cur_ln.substr(0, static_cast<std::size_t>(ws_len)) == target_ws) {
            return "[]";
        }
        return std::format(
            R"([{{"range":{{"start":{{"line":{},"character":0}},"end":{{"line":{},"character":{}}}}},"newText":"{}"}}])",
            line, line, ws_len, detail::json_escape(target_ws));
    }
};

} // namespace cfire::lsp

#include "hover.hpp"
#include "inlay_hints.hpp"
#include "completion.hpp"
#include "navigation.hpp"
#include "call_hierarchy.hpp"
#include "semantic_tokens.hpp"

#pragma once
// src/lsp/server.hpp — Full-Featured Language Server Protocol (LSP 3.17) Server (`cfire::lsp::Server`)

#if !defined(CFIRE_SPLIT_TUS) || defined(CFIRE_LSP_SERVER_IMPL)
#include "../analysis.hpp"
#include "../buildsys.hpp"
#include "../sema.hpp"
#endif
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
#include <memory>
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

#ifndef CFIRE_LSP_SERVER_INLINE
#ifdef CFIRE_SPLIT_TUS
#define CFIRE_LSP_SERVER_INLINE
#else
#define CFIRE_LSP_SERVER_INLINE inline
#endif
#endif

namespace cfire::lsp {

class Server {
public:
    struct PendingDiagRequest {
        std::string uri;
        std::string text;
        std::chrono::steady_clock::time_point deadline;
        bool allow_cpp_backend = false;
    };

    explicit Server(int debounce_ms = 150)
        : debounce_duration_(std::chrono::milliseconds(debounce_ms)) {}

    ~Server() {
        stop_worker();
        clear_out_stream();
    }

    void clear_out_stream() {
        std::lock_guard<std::mutex> lock(out_mutex_);
        out_stream_ = nullptr;
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

    void start_worker();

    void stop_worker();

    void schedule_diagnostics(const std::string& uri, const std::string& text, bool immediate, bool allow_cpp_backend = false);

    void handle_stream(std::istream& in, std::ostream& out);

    // Exception barrier: a failure while serving one message must never take the server down.
    bool handle_message(std::string_view msg, std::ostream& out);

    bool handle_message_impl(std::string_view msg, std::ostream& out);

private:
    void report_internal_error(std::string_view msg, std::ostream& out, std::string_view what);

    std::chrono::milliseconds debounce_duration_{150};
    std::thread worker_thread_;
    std::mutex diag_mutex_;
    std::condition_variable diag_cv_;
    std::atomic<bool> stop_requested_{false};
    std::atomic<bool> worker_running_{false};
    std::unordered_map<std::string, PendingDiagRequest> pending_diags_;

    std::mutex cancel_mutex_;
    std::unordered_set<std::string> cancelled_requests_;

    std::mutex out_mutex_;
    std::ostream* out_stream_{nullptr};

public:
    struct CheckedDocResult {
        std::shared_ptr<const cfire::frontend::CheckedFile> checked{};
        detail::WorkspaceContext ws{};
        std::size_t local_fn_count = 0;
        std::size_t local_struct_count = 0;
        std::size_t local_enum_count = 0;
        std::size_t local_iface_count = 0;
        std::size_t local_global_count = 0;
        std::size_t local_test_count = 0;
    };

private:
    struct CachedCheckedDoc {
        std::size_t text_hash = 0;
        std::size_t pkg_hash = 0;
        bool clean = false;
        int recovery_line = -1;
        CheckedDocResult result{};
    };

    std::mutex docs_mutex_;
    std::mutex diag_store_mutex_;
    std::mutex cpp_cache_mutex_;
    std::mutex file_ast_cache_mutex_;
    std::mutex checked_cache_mutex_;

    std::unordered_map<std::string, std::string> docs_;
    std::unordered_map<std::string, std::vector<detail::DiagnosticEntry>> last_diagnostics_;
    std::unordered_map<std::string, cfire::bridge::HeaderIndex> cpp_header_cache_;
    std::unordered_map<std::string, std::pair<std::size_t, cfire::frontend::FileAST>> file_ast_cache_;
    std::unordered_map<std::string, cfire::frontend::FileAST> last_good_ast_;
    std::unordered_map<std::string, detail::WorkspaceContext> last_good_ws_;
    std::unordered_map<std::string, CachedCheckedDoc> checked_doc_cache_;
    std::unordered_map<std::string, CheckedDocResult> last_good_checked_;
    std::string workspace_root_;
    bool client_snippet_support_{true};

public:
    void set_workspace_root(std::string root) { workspace_root_ = std::move(root); }

    auto get_doc_text(const std::string& uri) -> std::string;

    // Resolves local `.cf` package imports (`import "./pkg/checksum"`) and `import cpp "..."` headers for `ast`
    auto resolve_workspace(const std::string& uri, const cfire::frontend::FileAST& ast) -> detail::WorkspaceContext;

    // Resilient AST resolution: attempts parse, recovers trailing dot completions via dummy token, falls back to last-good AST
    auto get_or_parse_ast(const std::string& uri, const std::string& text, int line = -1, int col = -1)
        -> std::pair<std::optional<cfire::frontend::FileAST>, detail::WorkspaceContext>;

    // Cached, resilient Sema Typed-AST resolution over the active file + local workspace declarations
    auto get_or_check_doc(const std::string& uri, const std::string& text, int line = -1, int col = -1)
        -> std::optional<CheckedDocResult>;

    auto compute_diagnostics_for_doc(const std::string& uri, const std::string& text, bool allow_cpp_fallback = false) -> std::vector<detail::DiagnosticEntry>;

    void run_and_publish_diagnostics(const std::string& uri, const std::string& text, bool allow_cpp_backend = true);

    void publish_diagnostics(const std::string& uri, const std::string& text, std::ostream& out);

    void worker_loop();

    auto compute_hover(const std::string& uri, int line, int character) -> std::string;
    auto compute_signature_help(const std::string& uri, int line, int character) -> std::string;
    auto compute_inlay_hints(const std::string& uri) -> std::string;
    auto compute_import_edit(const std::vector<std::string>& lines, std::string_view import_path) -> std::string;
    struct CompletionListResult {
        std::string items_json = "[]";
        bool is_incomplete = false;
    };
    auto compute_completions_list(const std::string& uri, int line, int character) -> CompletionListResult;
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

    // the workspace is the editor's rootUri, else the nearest ancestor holding a project
    // marker. A cache directory (`.cfire`) is not a marker: a stray /tmp/.cfire used to turn all of /tmp
    // into one workspace. `has_marker == false` means "just this file's directory, non-recursively".
    struct WorkspaceRoot {
        std::filesystem::path path;
        bool has_marker = false;
    };

    auto resolve_workspace_root(const std::string& uri) -> WorkspaceRoot;

    auto get_workspace_root_for_uri(const std::string& uri) -> std::filesystem::path {
        return resolve_workspace_root(uri).path;
    }

    static constexpr std::size_t kMaxWorkspaceFiles = 5000;

    auto discover_workspace_cf_files(const std::string& current_uri) -> std::vector<std::string>;

    auto find_symbol_occurrences(const std::string& uri, int line, int character, std::string_view target_name, std::string_view qualifier) -> std::vector<TokenOccurrence>;
    auto find_symbol_occurrences_unscoped(const std::string& uri, int line, int character, std::string_view target_name, std::string_view qualifier) -> std::vector<TokenOccurrence>;
    auto compute_references(const std::string& uri, int line, int character) -> std::string;
    auto compute_prepare_rename(const std::string& uri, int line, int character) -> std::string;
    auto compute_rename(const std::string& uri, int line, int character, std::string_view new_name) -> std::string;
    auto compute_document_symbols(const std::string& uri) -> std::string;
    auto compute_workspace_symbols(std::string_view query) -> std::string;
    auto compute_folding_ranges(const std::string& uri) -> std::string;
    auto compute_selection_range(const std::string& uri, int line, int character) -> std::string;
    auto compute_document_highlight(const std::string& uri, int line, int character) -> std::string;

    auto compute_code_actions(const std::string& uri, int req_line, const std::vector<std::string>& requested_codes = {}) -> std::string;

    auto compute_formatting(const std::string& uri) -> std::string;

    auto compute_range_formatting(const std::string& uri, int start_line, int end_line) -> std::string;

    auto compute_on_type_formatting(const std::string& uri, int line, int character, std::string_view ch) -> std::string;
};


#if !defined(CFIRE_SPLIT_TUS) || defined(CFIRE_LSP_SERVER_IMPL)

CFIRE_LSP_SERVER_INLINE void Server::start_worker() {
    if (worker_running_.load()) return;
    stop_requested_ = false;
    worker_running_ = true;
    worker_thread_ = std::thread(&Server::worker_loop, this);
}

CFIRE_LSP_SERVER_INLINE void Server::stop_worker() {
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
    {
        std::lock_guard<std::mutex> lock(out_mutex_);
        out_stream_ = nullptr;
    }
}

CFIRE_LSP_SERVER_INLINE void Server::schedule_diagnostics(const std::string& uri, const std::string& text, bool immediate, bool allow_cpp_backend) {
    if (!worker_running_.load()) {
        start_worker();
    }
    {
        std::lock_guard<std::mutex> lock(diag_mutex_);
        auto now = std::chrono::steady_clock::now();
        auto deadline = immediate ? now : (now + debounce_duration_);
        pending_diags_[uri] = PendingDiagRequest{uri, text, deadline, allow_cpp_backend};
    }
    diag_cv_.notify_one();
}

CFIRE_LSP_SERVER_INLINE void Server::handle_stream(std::istream& in, std::ostream& out) {
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

CFIRE_LSP_SERVER_INLINE bool Server::handle_message(std::string_view msg, std::ostream& out) {
    try {
        return handle_message_impl(msg, out);
    } catch (const std::exception& e) {
        report_internal_error(msg, out, e.what());
    } catch (...) {
        report_internal_error(msg, out, "unknown exception");
    }
    return true;
}

CFIRE_LSP_SERVER_INLINE bool Server::handle_message_impl(std::string_view msg, std::ostream& out) {
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
            R"({{"jsonrpc":"2.0","id":{},"result":{{"capabilities":{{"positionEncoding":"utf-16","textDocumentSync":2,"hoverProvider":true,"definitionProvider":true,"typeDefinitionProvider":true,"implementationProvider":true,"callHierarchyProvider":true,"referencesProvider":true,"renameProvider":{{"prepareProvider":true}},"codeActionProvider":true,"documentFormattingProvider":true,"documentRangeFormattingProvider":true,"documentSymbolProvider":true,"workspaceSymbolProvider":true,"foldingRangeProvider":true,"selectionRangeProvider":true,"documentHighlightProvider":true,"inlayHintProvider":true,"completionProvider":{{"triggerCharacters":[".",":"]}},"signatureHelpProvider":{{"triggerCharacters":["(",","]}},"semanticTokensProvider":{{"legend":{{"tokenTypes":["type","struct","enum","interface","parameter","variable","property","enumMember","function","method","keyword","modifier","string","number","operator","namespace"],"tokenModifiers":["declaration","definition","readonly","modification","defaultLibrary","async"]}},"full":true}}}},"serverInfo":{{"name":"cfire-lsp","version":"{}"}}}}}})",
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
    if (method == "$/cancelRequest") {
        if (auto cancel_id = detail::extract_json_id(msg)) {
            std::lock_guard<std::mutex> lock(cancel_mutex_);
            cancelled_requests_.insert(*cancel_id);
        }
        return true;
    }

    if (method == "textDocument/didClose") {
        auto uri = detail::extract_json_string(msg, "uri").value_or("");
        {
            std::lock_guard<std::mutex> lock(docs_mutex_);
            docs_.erase(uri);
        }
        {
            std::lock_guard<std::mutex> lock(diag_store_mutex_);
            last_diagnostics_.erase(uri);
        }
        {
            std::lock_guard<std::mutex> lock(file_ast_cache_mutex_);
            file_ast_cache_.erase(uri);
        }
        last_good_ast_.erase(uri);
        last_good_ws_.erase(uri);
        {
            std::lock_guard<std::mutex> lock(checked_cache_mutex_);
            checked_doc_cache_.erase(uri);
            last_good_checked_.erase(uri);
        }
        // Clear published diagnostics in client
        send_rpc(out, std::format(
            R"({{"jsonrpc":"2.0","method":"textDocument/publishDiagnostics","params":{{"uri":"{}","diagnostics":[]}}}})",
            detail::json_escape(uri)));
        return true;
    }

    if (method == "workspace/didChangeWatchedFiles") {
        {
            std::lock_guard<std::mutex> lock(docs_mutex_);
            docs_.clear();
        }
        {
            std::lock_guard<std::mutex> lock(cpp_cache_mutex_);
            cpp_header_cache_.clear();
        }
        {
            std::lock_guard<std::mutex> lock(file_ast_cache_mutex_);
            file_ast_cache_.clear();
        }
        {
            std::lock_guard<std::mutex> lock(checked_cache_mutex_);
            checked_doc_cache_.clear();
            last_good_checked_.clear();
        }
        last_good_ast_.clear();
        last_good_ws_.clear();
        return true;
    }

    if (method == "textDocument/didOpen" || method == "textDocument/didChange" || method == "textDocument/didSave") {
        auto uri = detail::extract_json_string(msg, "uri").value_or("");
        {
            std::lock_guard<std::mutex> lock(checked_cache_mutex_);
            checked_doc_cache_.erase(uri);
        }
        std::string content;
        {
            std::lock_guard<std::mutex> lock(docs_mutex_);
            if (method == "textDocument/didChange") {
                auto changes = detail::extract_content_changes(msg);
                if (!changes.empty()) {
                    for (const auto& ch : changes) {
                        if (ch.has_range) {
                            detail::apply_incremental_edit(docs_[uri], ch.start_line, ch.start_char, ch.end_line, ch.end_char, ch.text);
                        } else {
                            docs_[uri] = ch.text;
                        }
                    }
                    content = docs_[uri];
                } else {
                    content = docs_[uri];
                }
            } else {
                auto text = detail::extract_json_string(msg, "text");
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
        }
        bool immediate = (method != "textDocument/didChange");
        bool allow_cpp_backend = (method != "textDocument/didChange");
        schedule_diagnostics(uri, content, immediate, allow_cpp_backend);
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
        auto comp = compute_completions_list(uri, line, character);
        std::string resp = std::format(
            R"({{"jsonrpc":"2.0","id":{},"result":{{"isIncomplete":{},"items":{}}}}})",
            id, comp.is_incomplete ? "true" : "false", comp.items_json);
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
        std::vector<std::string> codes;
        if (std::size_t ctx_idx = detail::find_json_key(msg, "context"); ctx_idx != std::string_view::npos) {
            std::size_t search_pos = ctx_idx;
            while (true) {
                std::size_t code_pos = detail::find_json_key(msg, "code", search_pos);
                if (code_pos == std::string_view::npos) break;
                if (auto code_str = detail::extract_json_string(msg, "code", search_pos)) {
                    codes.push_back(*code_str);
                }
                search_pos = code_pos + 1;
            }
        }
        std::string actions_json = compute_code_actions(uri, start_line, codes);
        send_rpc(out, std::format(R"({{"jsonrpc":"2.0","id":{},"result":{}}})", id, actions_json));
        return true;
    }

    if (method == "textDocument/foldingRange") {
        std::string id = id_opt.value_or("1");
        auto uri = detail::extract_json_string(msg, "uri").value_or("");
        std::string folds_json = compute_folding_ranges(uri);
        send_rpc(out, std::format(R"({{"jsonrpc":"2.0","id":{},"result":{}}})", id, folds_json));
        return true;
    }

    if (method == "textDocument/selectionRange") {
        std::string id = id_opt.value_or("1");
        auto uri = detail::extract_json_string(msg, "uri").value_or("");
        std::size_t pos_idx = detail::find_json_key(msg, "positions");
        if (pos_idx == std::string_view::npos) pos_idx = detail::find_json_key(msg, "position");
        int line = detail::extract_json_int_after(msg, "line", pos_idx != std::string_view::npos ? pos_idx : 0).value_or(0);
        int character = detail::extract_json_int_after(msg, "character", pos_idx != std::string_view::npos ? pos_idx : 0).value_or(0);
        std::string sel_json = compute_selection_range(uri, line, character);
        send_rpc(out, std::format(R"({{"jsonrpc":"2.0","id":{},"result":{}}})", id, sel_json));
        return true;
    }

    if (method == "textDocument/documentHighlight") {
        std::string id = id_opt.value_or("1");
        auto uri = detail::extract_json_string(msg, "uri").value_or("");
        std::size_t pos_idx = detail::find_json_key(msg, "position");
        int line = detail::extract_json_int_after(msg, "line", pos_idx != std::string_view::npos ? pos_idx : 0).value_or(0);
        int character = detail::extract_json_int_after(msg, "character", pos_idx != std::string_view::npos ? pos_idx : 0).value_or(0);
        std::string hl_json = compute_document_highlight(uri, line, character);
        send_rpc(out, std::format(R"({{"jsonrpc":"2.0","id":{},"result":{}}})", id, hl_json));
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
        send_rpc(out, std::format(
            R"({{"jsonrpc":"2.0","id":{},"error":{{"code":-32601,"message":"Method not found: {}"}}}})",
            *id_opt, detail::json_escape(method)));
    }
    return true;
}

CFIRE_LSP_SERVER_INLINE void Server::report_internal_error(std::string_view msg, std::ostream& out, std::string_view what) {
    auto method = detail::extract_json_string(msg, "method").value_or("");
    std::println(stderr, "cfire lsp: internal error handling '{}': {}", method, what);
    if (auto id_opt = detail::extract_json_id(msg)) {
        send_rpc(out, std::format(
            R"({{"jsonrpc":"2.0","id":{},"error":{{"code":-32603,"message":"cfire internal error: {}"}}}})",
            *id_opt, detail::json_escape(what)));
    }
}

CFIRE_LSP_SERVER_INLINE auto Server::get_doc_text(const std::string& uri) -> std::string {
    std::lock_guard<std::mutex> lock(docs_mutex_);
    if (auto it = docs_.find(uri); it != docs_.end()) {
        return it->second;
    }
    std::string path = detail::uri_to_path(uri);
    if (std::filesystem::exists(path)) {
        std::ifstream ifs(path);
        std::stringstream ss;
        ss << ifs.rdbuf();
        if (docs_.size() > 100) {
            docs_.erase(docs_.begin());
        }
        docs_[uri] = ss.str();
        return docs_[uri];
    }
    return "";
}

CFIRE_LSP_SERVER_INLINE auto Server::resolve_workspace(const std::string& uri, const cfire::frontend::FileAST& ast) -> detail::WorkspaceContext {
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
                    std::size_t sub_hash = std::hash<std::string>{}(sub_text);
                    std::optional<cfire::frontend::FileAST> sub_ast;
                    {
                        std::lock_guard<std::mutex> lock(file_ast_cache_mutex_);
                        if (auto it = file_ast_cache_.find(sub_uri); it != file_ast_cache_.end()) {
                            if (it->second.first == sub_hash) {
                                sub_ast = it->second.second;
                            }
                        }
                    }
                    if (!sub_ast) {
                        if (auto parsed = cfire::frontend::parse_source(fpath.string(), sub_text)) {
                            sub_ast = std::move(*parsed);
                            std::lock_guard<std::mutex> lock(file_ast_cache_mutex_);
                            if (file_ast_cache_.size() > 200) {
                                file_ast_cache_.erase(file_ast_cache_.begin());
                            }
                            file_ast_cache_[sub_uri] = {sub_hash, *sub_ast};
                        }
                    }
                    if (sub_ast) {
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
                cfire::build::BuildPlan lsp_plan;
                lsp_plan.cflags = {"-std=c++2b", "-I" + src_dir.string()};
                // Check sibling or parent `build.cf` for local C/C++ include directories, deps, defines, and PkgConfig
                std::filesystem::path search_dir = src_dir;
                for (int depth = 0; depth < 6 && !search_dir.empty(); ++depth) {
                    auto bcf = search_dir / "build.cf";
                    std::error_code ec;
                    if (std::filesystem::exists(bcf, ec)) {
                        if (auto parsed = cfire::build::parse_build_cf(bcf)) {
                            cfire::build::apply_manifest_pkg_fields(
                                lsp_plan, search_dir, parsed->pkg_config, parsed->libraries, parsed->frameworks, parsed->includes, parsed->defines, parsed->cflags, parsed->ldflags);
                            for (const auto& ex : parsed->executables) {
                                cfire::build::apply_manifest_pkg_fields(
                                    lsp_plan, search_dir, ex.pkg_config, ex.libraries, ex.frameworks, ex.includes, ex.defines, ex.cflags, ex.ldflags);
                            }
                            for (const auto& dep : parsed->external_deps) {
                                if (!dep.source.empty()) {
                                    auto inc_dir = search_dir / dep.source;
                                    if (std::filesystem::exists(inc_dir, ec)) {
                                        lsp_plan.cflags.push_back("-I" + inc_dir.string());
                                        if (std::filesystem::is_directory(inc_dir / "include", ec)) {
                                            lsp_plan.cflags.push_back("-I" + (inc_dir / "include").string());
                                        }
                                    }
                                }
                                for (const auto& dinc : dep.includes) {
                                    auto p = std::filesystem::path(dinc);
                                    if (p.is_relative() && !dep.source.empty()) p = search_dir / dep.source / p;
                                    else if (p.is_relative()) p = search_dir / p;
                                    if (std::filesystem::exists(p, ec)) {
                                        lsp_plan.cflags.push_back("-I" + p.string());
                                    }
                                }
                            }
                        }
                        auto rt_dir = cfire::build::locate_toolchain_layout().runtime_dir;
                        (void)cfire::build::resolve_plan_pkg_configs(lsp_plan, rt_dir);
                        (void)cfire::build::resolve_plan_defines(lsp_plan);
                        break;
                    }
                    if (search_dir == search_dir.parent_path()) break;
                    search_dir = search_dir.parent_path();
                }
                const auto& cflags = lsp_plan.cflags;
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

CFIRE_LSP_SERVER_INLINE auto Server::get_or_parse_ast(const std::string& uri, const std::string& text, int line, int col)
    -> std::pair<std::optional<cfire::frontend::FileAST>, detail::WorkspaceContext> {
    std::string path = detail::uri_to_path(uri);
    auto pres = cfire::frontend::parse_source_resilient(path, text);
    if (pres.ok()) {
        auto ws = resolve_workspace(uri, pres.ast);
        last_good_ast_[uri] = pres.ast;
        last_good_ws_[uri] = ws;
        return {pres.ast, ws};
    }

    auto lines = detail::split_lines(text);
    auto ast_covers_line = [&](const cfire::frontend::FileAST& ast_ref, const std::vector<std::string>& ln_vec) -> bool {
        if (line < 0) return false;
        int one_based = line + 1;
        for (const auto& fn : ast_ref.functions) {
            int fn_end = detail::get_block_end_line(fn.body, ln_vec, fn.line);
            if (one_based >= fn.line && one_based <= fn_end) return true;
        }
        for (const auto& t : ast_ref.tests) {
            int t_end = detail::get_block_end_line(t.body, ln_vec, t.line);
            if (one_based >= t.line && one_based <= t_end) return true;
        }
        return false;
    };
    auto ast_score = [&](const cfire::frontend::FileAST& ast_ref, const std::vector<std::string>& ln_vec) -> std::size_t {
        std::size_t sc = ast_ref.functions.size() * 10 + ast_ref.structs.size() * 5 + ast_ref.enums.size() * 5 + ast_ref.tests.size() * 10;
        if (ast_covers_line(ast_ref, ln_vec)) sc += 1000;
        return sc;
    };

    auto patch_trailing_seps = [&](std::vector<std::string>& ln_vec) -> bool {
        bool mod = false;
        for (std::size_t li = 0; li < ln_vec.size(); ++li) {
            auto& ln = ln_vec[li];
            auto end_pos = ln.find_last_not_of(" \t\r\n");
            if (end_pos != std::string::npos) {
                bool is_single_dot = ln[end_pos] == '.' && (end_pos == 0 || ln[end_pos - 1] != '.');
                if (is_single_dot) {
                    ln.insert(end_pos + 1, "__cfire_placeholder__");
                    mod = true;
                    continue;
                } else if (end_pos >= 1 && ln[end_pos] == ':' && ln[end_pos - 1] == ':') {
                    ln.insert(end_pos + 1, "__cfire_placeholder__");
                    mod = true;
                    continue;
                }
            }
            if (line >= 0 && static_cast<std::size_t>(line) == li &&
                col > 0 && static_cast<std::size_t>(col) <= ln.size()) {
                std::size_t c_idx = static_cast<std::size_t>(col);
                char prev = ln[c_idx - 1];
                bool prev_not_dot = (c_idx < 2 || ln[c_idx - 2] != '.');
                bool next_non_ident = (c_idx == ln.size()) ||
                    (!std::isalnum(static_cast<unsigned char>(ln[c_idx])) && ln[c_idx] != '_' && ln[c_idx] != '.');
                if (prev == '.' && prev_not_dot && next_non_ident) {
                    ln.insert(c_idx, "__cfire_placeholder__");
                    mod = true;
                }
            }
        }
        return mod;
    };

    auto join_lines = [](const std::vector<std::string>& ln_vec) -> std::string {
        std::string out;
        for (std::size_t i = 0; i < ln_vec.size(); ++i) {
            if (i > 0) out += "\n";
            out += ln_vec[i];
        }
        return out;
    };

    cfire::frontend::FileAST best_ast = pres.ast;
    std::size_t best_score = ast_score(best_ast, lines);

    // Recovery 1: Patch trailing `.` / `::` with placeholder identifiers and parse resiliently
    {
        auto patched_lines = lines;
        if (patch_trailing_seps(patched_lines)) {
            auto patched_res = cfire::frontend::parse_source_resilient(path, join_lines(patched_lines));
            std::size_t p_score = ast_score(patched_res.ast, patched_lines);
            if (patched_res.ok() || p_score > best_score) {
                best_ast = std::move(patched_res.ast);
                best_score = p_score;
            }
            if (patched_res.ok()) {
                auto ws = resolve_workspace(uri, best_ast);
                return {best_ast, ws};
            }
        }
    }

    // Recovery 1b: If the function/test containing `line` was dropped by a partial statement (`if xs.`, `println("{}", xs.`, unclosed `{`),
    // isolate the incomplete line and close unbalanced `{` braces so preceding local bindings remain visible.
    if (line >= 0 && static_cast<std::size_t>(line) < lines.size() && !ast_covers_line(best_ast, lines)) {
        auto masked_lines = lines;
        masked_lines[static_cast<std::size_t>(line)] = "";
        patch_trailing_seps(masked_lines);
        int brace_depth = 0;
        bool in_raw = false;
        for (const auto& ml : masked_lines) {
            bool in_str = false;
            for (std::size_t k = 0; k < ml.size(); ++k) {
                char c = ml[k];
                if (in_raw) {
                    if (c == '`') in_raw = false;
                    continue;
                }
                if (in_str) {
                    if (c == '\\' && k + 1 < ml.size()) ++k;
                    else if (c == '"') in_str = false;
                    continue;
                }
                if (c == '/' && k + 1 < ml.size() && ml[k + 1] == '/') break;
                if (c == '"') in_str = true;
                else if (c == '`') in_raw = true;
                else if (c == '{') ++brace_depth;
                else if (c == '}' && brace_depth > 0) --brace_depth;
            }
        }
        for (int b = 0; b < brace_depth; ++b) {
            masked_lines.emplace_back("}");
        }
        auto masked_res = cfire::frontend::parse_source_resilient(path, join_lines(masked_lines));
        std::size_t m_score = ast_score(masked_res.ast, masked_lines);
        if (m_score > best_score) {
            best_ast = std::move(masked_res.ast);
            best_score = m_score;
        }
    }

    if (!best_ast.functions.empty() || !best_ast.structs.empty() || !best_ast.enums.empty() || !best_ast.tests.empty()) {
        auto ws = resolve_workspace(uri, best_ast);
        if (!pres.ast.functions.empty() || !pres.ast.structs.empty() || !pres.ast.enums.empty()) {
            last_good_ast_[uri] = pres.ast;
            last_good_ws_[uri] = ws;
        }
        return {best_ast, ws};
    }

    // Recovery 2: Fall back to last known valid AST
    if (auto it = last_good_ast_.find(uri); it != last_good_ast_.end()) {
        return {it->second, last_good_ws_[uri]};
    }

    return {std::nullopt, {}};
}

CFIRE_LSP_SERVER_INLINE auto Server::get_or_check_doc(const std::string& uri, const std::string& text, int line, int col)
    -> std::optional<CheckedDocResult> {
    const std::size_t text_hash = std::hash<std::string>{}(text);
    auto compute_pkg_hash = [&](const detail::WorkspaceContext& wctx) -> std::size_t {
        std::size_t h = 0;
        for (const auto& pkg : wctx.local_packages) {
            std::size_t h1 = std::hash<std::string>{}(pkg.uri);
            std::size_t h2 = std::hash<std::string>{}(get_doc_text(pkg.uri));
            h ^= h1 + 0x9e3779b9 + (h << 6) + (h >> 2);
            h ^= h2 + 0x9e3779b9 + (h << 6) + (h >> 2);
        }
        return h;
    };

    {
        std::optional<CachedCheckedDoc> cached;
        {
            std::lock_guard<std::mutex> lock(checked_cache_mutex_);
            if (auto it = checked_doc_cache_.find(uri); it != checked_doc_cache_.end()) {
                if (it->second.text_hash == text_hash && (it->second.clean || it->second.recovery_line == line)) {
                    cached = it->second;
                }
            }
        }
        if (cached && compute_pkg_hash(cached->result.ws) == cached->pkg_hash) {
            return cached->result;
        }
    }

    auto [ast_res, ws] = get_or_parse_ast(uri, text, line, col);
    if (!ast_res) return std::nullopt;

    if (ast_res->arena) {
        std::lock_guard<std::mutex> lock(checked_cache_mutex_);
        if (auto it = last_good_checked_.find(uri); it != last_good_checked_.end()) {
            if (it->second.checked && it->second.checked->ast.arena.get() == ast_res->arena.get()) {
                return it->second;
            }
        }
    }

    cfire::frontend::FileAST merged_ast = *ast_res;
    if (merged_ast.path.empty()) {
        merged_ast.path = detail::uri_to_path(uri);
    }
    const std::size_t local_fn_count = merged_ast.functions.size();
    const std::size_t local_struct_count = merged_ast.structs.size();
    const std::size_t local_enum_count = merged_ast.enums.size();
    const std::size_t local_iface_count = merged_ast.interfaces.size();
    const std::size_t local_global_count = merged_ast.global_vars.size();
    const std::size_t local_test_count = merged_ast.tests.size();

    bool has_main = false;
    for (auto& fn : merged_ast.functions) {
        if (fn.source_path.empty()) fn.source_path = merged_ast.path;
        if (!fn.receiver && fn.name == "main") has_main = true;
    }
    for (auto& t : merged_ast.tests) {
        if (t.source_path.empty()) t.source_path = merged_ast.path;
    }

    for (const auto& pkg : ws.local_packages) {
        if (pkg.ast.arena) merged_ast.child_arenas.push_back(pkg.ast.arena);
        merged_ast.child_arenas.insert(merged_ast.child_arenas.end(),
            pkg.ast.child_arenas.begin(), pkg.ast.child_arenas.end());
        std::string sub_pkg_tag;
        if (!pkg.ast.package_name.empty() && pkg.ast.package_name != merged_ast.package_name) {
            sub_pkg_tag = !pkg.alias.empty() ? pkg.alias : pkg.ast.package_name;
        }
        for (const auto& st : pkg.ast.structs) {
            auto copy = st;
            if (copy.package_name.empty() && !sub_pkg_tag.empty()) copy.package_name = sub_pkg_tag;
            if (copy.source_path.empty()) copy.source_path = pkg.file_path;
            merged_ast.structs.push_back(std::move(copy));
        }
        for (const auto& en : pkg.ast.enums) {
            auto copy = en;
            if (copy.package_name.empty() && !sub_pkg_tag.empty()) copy.package_name = sub_pkg_tag;
            if (copy.source_path.empty()) copy.source_path = pkg.file_path;
            merged_ast.enums.push_back(std::move(copy));
        }
        for (const auto& ifc : pkg.ast.interfaces) {
            auto copy = ifc;
            if (copy.package_name.empty() && !sub_pkg_tag.empty()) copy.package_name = sub_pkg_tag;
            if (copy.source_path.empty()) copy.source_path = pkg.file_path;
            merged_ast.interfaces.push_back(std::move(copy));
        }
        for (const auto& gv : pkg.ast.global_vars) {
            auto copy = gv;
            if (copy.package_name.empty() && !sub_pkg_tag.empty()) copy.package_name = sub_pkg_tag;
            if (copy.source_path.empty()) copy.source_path = pkg.file_path;
            if (copy.type.has_value()) copy.value = nullptr;
            merged_ast.global_vars.push_back(std::move(copy));
        }
        for (const auto& fn : pkg.ast.functions) {
            if (!fn.receiver && fn.name == "main" && has_main) continue;
            auto copy = fn;
            if (!copy.receiver && copy.name == "main") has_main = true;
            if (copy.package_name.empty() && !sub_pkg_tag.empty()) copy.package_name = sub_pkg_tag;
            if (copy.source_path.empty()) copy.source_path = pkg.file_path;
            copy.body.clear();
            merged_ast.functions.push_back(std::move(copy));
        }
    }

    std::unordered_map<std::string, std::vector<std::string>> pre_cpp_fn_params;
    std::unordered_map<std::string, std::string> pre_cpp_fn_returns;
    for (const auto& [sname, sym] : ws.cpp_symbols) {
        pre_cpp_fn_params[sname] = sym.param_types;
        if (!sym.return_type.empty()) {
            pre_cpp_fn_returns[sname] = sym.return_type;
        }
    }

    auto checked = std::make_shared<cfire::frontend::CheckedFile>(
        cfire::frontend::analyze_resilient(
            std::move(merged_ast), std::move(pre_cpp_fn_params), std::move(pre_cpp_fn_returns)));

    CheckedDocResult res{
        std::move(checked),
        std::move(ws),
        local_fn_count,
        local_struct_count,
        local_enum_count,
        local_iface_count,
        local_global_count,
        local_test_count
    };

    const bool clean = ast_res->arena != nullptr &&
        last_good_ast_.contains(uri) &&
        last_good_ast_[uri].arena.get() == ast_res->arena.get();
    const std::size_t pkg_hash = compute_pkg_hash(res.ws);

    {
        std::lock_guard<std::mutex> lock(checked_cache_mutex_);
        if (checked_doc_cache_.size() > 64) {
            checked_doc_cache_.erase(checked_doc_cache_.begin());
        }
        checked_doc_cache_[uri] = CachedCheckedDoc{text_hash, pkg_hash, clean, line, res};
        if (clean) {
            last_good_checked_[uri] = res;
        }
    }
    return res;
}

CFIRE_LSP_SERVER_INLINE auto Server::compute_diagnostics_for_doc(const std::string& uri, const std::string& text, bool allow_cpp_fallback) -> std::vector<detail::DiagnosticEntry> {
    std::string file_path = detail::uri_to_path(uri);
    cfire::analysis::CheckOptions opts;
    opts.verify_cpp_backend = allow_cpp_fallback;
    auto layout = cfire::build::locate_toolchain_layout(nullptr);
    opts.runtime_dir = layout.runtime_dir;
    {
        std::lock_guard<std::mutex> lock(docs_mutex_);
        for (const auto& [doc_uri, doc_text] : docs_) {
            std::string doc_path = detail::uri_to_path(doc_uri);
            if (!doc_path.empty()) {
                opts.source_overlays[doc_path] = doc_text;
                std::error_code ec;
                auto canon = std::filesystem::weakly_canonical(doc_path, ec);
                if (!ec && !canon.empty()) {
                    opts.source_overlays[canon.string()] = doc_text;
                }
            }
        }
    }

    auto diags = cfire::analysis::check_source(file_path, text, opts);
    std::vector<detail::DiagnosticEntry> entries;
    entries.reserve(diags.size());
    for (const auto& d : diags) {
        entries.push_back(detail::DiagnosticEntry{
            std::max(0, d.line - 1),
            std::max(0, d.col - 1),
            d.code,
            d.message,
            (d.severity == "warning") ? 2 : 1,
            d.facts
        });
    }
    {
        std::lock_guard<std::mutex> lock(diag_store_mutex_);
        last_diagnostics_[uri] = entries;
    }
    return entries;
}

CFIRE_LSP_SERVER_INLINE void Server::run_and_publish_diagnostics(const std::string& uri, const std::string& text, bool allow_cpp_backend) {
    auto entries = compute_diagnostics_for_doc(uri, text, allow_cpp_backend);
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

CFIRE_LSP_SERVER_INLINE void Server::publish_diagnostics(const std::string& uri, const std::string& text, std::ostream& out) {
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

CFIRE_LSP_SERVER_INLINE void Server::worker_loop() {
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
            try {
                run_and_publish_diagnostics(current_req.uri, current_req.text, current_req.allow_cpp_backend);
            } catch (const std::exception& e) {
                std::println(stderr, "cfire lsp: internal error while analyzing {}: {}", current_req.uri, e.what());
            } catch (...) {
                std::println(stderr, "cfire lsp: internal error while analyzing {}: unknown exception", current_req.uri);
            }
        }
    }
}

CFIRE_LSP_SERVER_INLINE auto Server::resolve_workspace_root(const std::string& uri) -> WorkspaceRoot {
    if (!workspace_root_.empty()) {
        std::error_code ec;
        if (std::filesystem::exists(workspace_root_, ec)) {
            return {std::filesystem::path(workspace_root_), true};
        }
    }
    std::filesystem::path file_path = detail::uri_to_path(uri);
    std::filesystem::path p = file_path.parent_path();
    while (!p.empty() && p != p.parent_path()) {
        std::error_code ec;
        if (std::filesystem::exists(p / "build.cf", ec) ||
            std::filesystem::exists(p / ".git", ec) ||
            std::filesystem::exists(p / ".vscode", ec) ||
            std::filesystem::exists(p / ".zed", ec)) {
            return {p, true};
        }
        p = p.parent_path();
    }
    return {file_path.parent_path(), false};
}

CFIRE_LSP_SERVER_INLINE auto Server::discover_workspace_cf_files(const std::string& current_uri) -> std::vector<std::string> {
    std::vector<std::string> uris;
    std::unordered_set<std::string> seen;

    auto ws = resolve_workspace_root(current_uri);
    std::filesystem::path root = ws.path;
    std::error_code ec;
    if (std::filesystem::exists(root, ec) && std::filesystem::is_directory(root, ec) && !ws.has_marker) {
        // No project marker: only the opened file's own directory, never its whole subtree.
        for (const auto& entry : std::filesystem::directory_iterator(root, std::filesystem::directory_options::skip_permission_denied, ec)) {
            if (uris.size() >= kMaxWorkspaceFiles) break;
            if (entry.is_regular_file(ec) && entry.path().extension() == ".cf") {
                std::string u = detail::path_to_uri(entry.path());
                if (seen.insert(u).second) uris.push_back(u);
            }
        }
    } else if (std::filesystem::exists(root, ec) && std::filesystem::is_directory(root, ec)) {
        for (std::filesystem::recursive_directory_iterator it(root, std::filesystem::directory_options::skip_permission_denied, ec), end;
             it != end && !ec; it.increment(ec)) {
            if (uris.size() >= kMaxWorkspaceFiles) {
                std::println(stderr, "cfire lsp: workspace '{}' has more than {} .cf files; indexing stopped", root.string(), kMaxWorkspaceFiles);
                break;
            }
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

CFIRE_LSP_SERVER_INLINE auto Server::compute_code_actions(const std::string& uri, int req_line, const std::vector<std::string>& requested_codes) -> std::string {
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
        if (!requested_codes.empty()) {
            if (std::find(requested_codes.begin(), requested_codes.end(), d.code) == requested_codes.end()) {
                continue;
            }
        }
        if (req_line >= 0 && d.line != req_line && d.code != "cfire/NON-EXHAUSTIVE-MATCH") {
            continue;
        }
        cfire::diag::StructuredDiagnostic sd;
        sd.line = d.line + 1;
        sd.col = d.col + 1;
        sd.code = d.code;
        sd.message = d.message;
        sd.facts = d.facts;
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

CFIRE_LSP_SERVER_INLINE auto Server::compute_formatting(const std::string& uri) -> std::string {
    std::string text = get_doc_text(uri);
    auto formatted = cfire::fmt::format_source(text, detail::uri_to_path(uri));
    if (!formatted || *formatted == text) return "[]";
    auto lines = detail::split_lines(text);
    int end_line = static_cast<int>(lines.size()) + 1;
    return std::format(
        R"([{{"range":{{"start":{{"line":0,"character":0}},"end":{{"line":{},"character":0}}}},"newText":"{}"}}])",
        end_line, detail::json_escape(*formatted));
}

CFIRE_LSP_SERVER_INLINE auto Server::compute_range_formatting(const std::string& uri, int start_line, int end_line) -> std::string {
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

CFIRE_LSP_SERVER_INLINE auto Server::compute_on_type_formatting(const std::string& uri, int line, int character, std::string_view ch) -> std::string {
    (void)uri;
    (void)line;
    (void)character;
    (void)ch;
    return "[]";
}

#endif // !defined(CFIRE_SPLIT_TUS) || defined(CFIRE_LSP_SERVER_IMPL)

} // namespace cfire::lsp

#undef CFIRE_LSP_SERVER_INLINE

#if !defined(CFIRE_SPLIT_TUS)
#include "hover.hpp"
#include "inlay_hints.hpp"
#include "completion.hpp"
#include "navigation.hpp"
#include "call_hierarchy.hpp"
#include "semantic_tokens.hpp"
#endif

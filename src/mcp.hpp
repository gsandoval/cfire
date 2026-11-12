#pragma once
// src/mcp.hpp — Built-In Model Context Protocol (MCP) Server for the `cfire` Toolchain (`cfire mcp`)

#include "../runtime/core/ai.hpp"
#include "analysis.hpp"
#include "buildsys.hpp"
#include "diagnostics.hpp"
#include "docgen.hpp"
#include "formatter.hpp"
#include "lsp.hpp"
#include "refactor.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <chrono>
#include <atomic>
#if !defined(_WIN32)
#include <unistd.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <signal.h>
#include <poll.h>
#include <fcntl.h>
#endif

namespace cfire::mcp {

struct EmptyInput {};
struct QueryInput {
    std::string query{};
};
struct CodeInput {
    std::string code{};
};
struct CheckInput {
    std::string code{};
    std::string path{};
};
struct PathInput {
    std::string path{};
};
struct TargetInput {
    std::string target{};
};
struct RenameInput {
    std::string target{};
    std::string new_name{};
    bool dry_run{false};
};
struct TestInput {
    std::string path{};
    std::string filter{};
    bool bench{false};
    int fuzz_runs{100};
    std::uint64_t seed{0};
    int bench_time{200};
};

[[nodiscard]] inline auto is_ignored_mcp_dir(std::string_view name) -> bool {
    return name == ".cfire" || name == ".tako" || name == ".git" ||
           name == "dist" || name == "vendor" || name == "node_modules" ||
           name == "ejected_cpp";
}

} // namespace cfire::mcp

namespace cfire::json {

template <>
inline auto schema_struct<cfire::mcp::EmptyInput>() -> std::string {
    return R"({"type":"object","properties":{},"additionalProperties":false})";
}

template <>
inline auto parse_struct<cfire::mcp::EmptyInput>(simdjson::ondemand::object) -> std::expected<cfire::mcp::EmptyInput, cfire::Error> {
    return cfire::mcp::EmptyInput{};
}

template <>
inline auto schema_struct<cfire::mcp::QueryInput>() -> std::string {
    return R"({"type":"object","properties":{"query":{"type":"string","description":"Module import path (e.g. 'core:str', 'ext:ai', 'ext:webui', 'vendor:raylib'), diagnostic code (e.g. 'cfire/USE-AFTER-FREE'), or function/type name"}},"required":["query"],"additionalProperties":false})";
}

template <>
inline auto parse_struct<cfire::mcp::QueryInput>(simdjson::ondemand::object obj) -> std::expected<cfire::mcp::QueryInput, cfire::Error> {
    cfire::mcp::QueryInput out;
    auto q = get_str(obj, "query");
    if (!q) return std::unexpected(q.error());
    out.query = std::move(*q);
    return out;
}

template <>
inline auto schema_struct<cfire::mcp::CodeInput>() -> std::string {
    return R"({"type":"object","properties":{"code":{"type":"string","description":"cfire (.cf) source code or snippet"}},"required":["code"],"additionalProperties":false})";
}

template <>
inline auto parse_struct<cfire::mcp::CodeInput>(simdjson::ondemand::object obj) -> std::expected<cfire::mcp::CodeInput, cfire::Error> {
    cfire::mcp::CodeInput out;
    auto c = get_str(obj, "code");
    if (!c) return std::unexpected(c.error());
    out.code = std::move(*c);
    return out;
}

template <>
inline auto schema_struct<cfire::mcp::CheckInput>() -> std::string {
    return R"JSON({"type":"object","properties":{"code":{"type":"string","description":"Inline cfire (.cf) source code or snippet to check"},"path":{"type":"string","description":"Path to a .cf file or workspace directory to check (resolves same-directory sibling files)"}},"additionalProperties":false})JSON";
}

template <>
inline auto parse_struct<cfire::mcp::CheckInput>(simdjson::ondemand::object obj) -> std::expected<cfire::mcp::CheckInput, cfire::Error> {
    cfire::mcp::CheckInput out;
    if (auto c = get_str(obj, "code")) out.code = std::move(*c);
    if (auto p = get_str(obj, "path")) out.path = std::move(*p);
    return out;
}

template <>
inline auto schema_struct<cfire::mcp::PathInput>() -> std::string {
    return R"JSON({"type":"object","properties":{"path":{"type":"string","description":"Path to a .cf file or directory to outline (defaults to '.')"}},"additionalProperties":false})JSON";
}

template <>
inline auto parse_struct<cfire::mcp::PathInput>(simdjson::ondemand::object obj) -> std::expected<cfire::mcp::PathInput, cfire::Error> {
    cfire::mcp::PathInput out;
    if (auto p = get_str(obj, "path")) out.path = std::move(*p);
    return out;
}

template <>
inline auto schema_struct<cfire::mcp::TargetInput>() -> std::string {
    return R"({"type":"object","properties":{"target":{"type":"string","description":"Inspect target: '<file.cf>:<line>:<col>', 'cpp <header.h>', '<module>.<symbol>', or 'cfire/<DIAG-CODE>'"}},"required":["target"],"additionalProperties":false})";
}

template <>
inline auto parse_struct<cfire::mcp::TargetInput>(simdjson::ondemand::object obj) -> std::expected<cfire::mcp::TargetInput, cfire::Error> {
    cfire::mcp::TargetInput out;
    auto t = get_str(obj, "target");
    if (!t) return std::unexpected(t.error());
    out.target = std::move(*t);
    return out;
}

template <>
inline auto schema_struct<cfire::mcp::RenameInput>() -> std::string {
    return R"JSON({"type":"object","properties":{"target":{"type":"string","description":"Source coordinate '<file.cf>:<line>:<col>' pointing at the symbol to rename (including inside '{...}' interpolated strings)"},"new_name":{"type":"string","description":"New valid cfire identifier name"},"dry_run":{"type":"boolean","description":"Preview the unified diff and occurrence count without modifying files on disk"}},"required":["target","new_name"],"additionalProperties":false})JSON";
}

template <>
inline auto parse_struct<cfire::mcp::RenameInput>(simdjson::ondemand::object obj) -> std::expected<cfire::mcp::RenameInput, cfire::Error> {
    cfire::mcp::RenameInput out;
    auto t = get_str(obj, "target");
    if (!t) return std::unexpected(t.error());
    out.target = std::move(*t);
    auto nn = get_str(obj, "new_name");
    if (!nn) return std::unexpected(nn.error());
    out.new_name = std::move(*nn);
    if (auto dr = get_bool(obj, "dry_run")) out.dry_run = *dr;
    return out;
}

template <>
inline auto schema_struct<cfire::mcp::TestInput>() -> std::string {
    return R"JSON({"type":"object","properties":{"path":{"type":"string","description":"Path to a .cf file or directory to test (defaults to '.')"},"filter":{"type":"string","description":"Substring filter for test, fuzz, or bench names"},"bench":{"type":"boolean","description":"Run multi-iteration statistical benchmarks (bench blocks)"},"fuzz_runs":{"type":"integer","description":"Number of property-based fuzz iterations per fuzz block (default 100)"},"seed":{"type":"integer","description":"Deterministic PRNG seed for fuzzing (0 = auto)"},"bench_time":{"type":"integer","description":"Target benchmark measurement window in ms per benchmark variation (default 200)"}},"additionalProperties":false})JSON";
}

template <>
inline auto parse_struct<cfire::mcp::TestInput>(simdjson::ondemand::object obj) -> std::expected<cfire::mcp::TestInput, cfire::Error> {
    cfire::mcp::TestInput out;
    if (auto p = get_str(obj, "path")) out.path = std::move(*p);
    if (auto f = get_str(obj, "filter")) out.filter = std::move(*f);
    if (auto b = get_bool(obj, "bench")) out.bench = *b;
    if (auto fr = get_i64(obj, "fuzz_runs")) out.fuzz_runs = static_cast<int>(*fr);
    if (auto sd = get_i64(obj, "seed")) out.seed = static_cast<std::uint64_t>(*sd);
    if (auto bt = get_i64(obj, "bench_time")) out.bench_time = static_cast<int>(*bt);
    return out;
}

} // namespace cfire::json

namespace cfire::mcp {

[[nodiscard]] inline auto run_process_argv_timeout(
    const std::vector<std::string>& argv,
    int timeout_seconds,
    int& exit_code,
    bool& timed_out) -> std::string
{
    timed_out = false;
    exit_code = 0;
    if (argv.empty()) return "";

#if defined(_WIN32)
    std::string cmd_line;
    for (std::size_t i = 0; i < argv.size(); ++i) {
        if (i > 0) cmd_line += " ";
        if (argv[i].find(' ') != std::string::npos || argv[i].empty()) {
            cmd_line += "\"" + argv[i] + "\"";
        } else {
            cmd_line += argv[i];
        }
    }
    return cfire::bridge::run_command_capture(cmd_line, exit_code);
#else
    int pipefd[2];
    if (::pipe(pipefd) != 0) {
        exit_code = -1;
        return "failed to create pipe";
    }

    pid_t pid = ::fork();
    if (pid < 0) {
        ::close(pipefd[0]);
        ::close(pipefd[1]);
        exit_code = -1;
        return "failed to fork";
    }

    if (pid == 0) {
        ::dup2(pipefd[1], STDOUT_FILENO);
        ::dup2(pipefd[1], STDERR_FILENO);
        ::close(pipefd[0]);
        ::close(pipefd[1]);
        ::setpgid(0, 0);

        std::vector<char*> c_argv;
        c_argv.reserve(argv.size() + 1);
        for (const auto& a : argv) {
            c_argv.push_back(const_cast<char*>(a.c_str()));
        }
        c_argv.push_back(nullptr);

        ::execvp(c_argv[0], c_argv.data());
        ::_exit(127);
    }

    ::close(pipefd[1]);

    int flags = ::fcntl(pipefd[0], F_GETFL, 0);
    if (flags != -1) {
        ::fcntl(pipefd[0], F_SETFL, flags | O_NONBLOCK);
    }

    std::string output;
    char buffer[4096];
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(timeout_seconds > 0 ? timeout_seconds : 10);

    struct pollfd pfd;
    pfd.fd = pipefd[0];
    pfd.events = POLLIN | POLLHUP | POLLERR;

    bool child_done = false;
    while (!child_done) {
        auto now = std::chrono::steady_clock::now();
        if (now >= deadline) {
            timed_out = true;
            ::kill(-pid, SIGKILL);
            ::kill(pid, SIGKILL);
            int status = 0;
            ::waitpid(pid, &status, 0);
            ::close(pipefd[0]);
            exit_code = 124;
            return output + "\n[error: execution timed out after " + std::to_string(timeout_seconds) + " seconds]";
        }

        int remaining_ms = static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now).count());
        if (remaining_ms < 0) remaining_ms = 0;
        int poll_timeout = std::min(remaining_ms, 200);

        int pr = ::poll(&pfd, 1, poll_timeout);
        if (pr > 0) {
            if (pfd.revents & POLLIN) {
                ssize_t bytes_read = ::read(pipefd[0], buffer, sizeof(buffer));
                if (bytes_read > 0) {
                    output.append(buffer, static_cast<std::size_t>(bytes_read));
                } else if (bytes_read == 0) {
                    child_done = true;
                }
            }
            if (pfd.revents & (POLLHUP | POLLERR)) {
                while (true) {
                    ssize_t bytes_read = ::read(pipefd[0], buffer, sizeof(buffer));
                    if (bytes_read > 0) {
                        output.append(buffer, static_cast<std::size_t>(bytes_read));
                    } else {
                        break;
                    }
                }
                child_done = true;
            }
        } else if (pr == 0) {
            int status = 0;
            pid_t wp = ::waitpid(pid, &status, WNOHANG);
            if (wp == pid) {
                while (true) {
                    ssize_t bytes_read = ::read(pipefd[0], buffer, sizeof(buffer));
                    if (bytes_read > 0) {
                        output.append(buffer, static_cast<std::size_t>(bytes_read));
                    } else {
                        break;
                    }
                }
                exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : (WIFSIGNALED(status) ? 128 + WTERMSIG(status) : 1);
                child_done = true;
            }
        }
    }

    ::close(pipefd[0]);
    int status = 0;
    ::waitpid(pid, &status, 0);
    if (!timed_out) {
        exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : (WIFSIGNALED(status) ? 128 + WTERMSIG(status) : 1);
    }
    return output;
#endif
}

// Q15 (CF-160): every path an MCP client passes is confined to the server's workspace root, so an agent
// (or a prompt-injected instruction it follows) cannot read, test, or rewrite files elsewhere on disk.
struct McpOptions {
    std::filesystem::path root = std::filesystem::current_path();
    bool allow_outside_root = false;
};

[[nodiscard]] inline auto resolve_in_root(const McpOptions& opts, std::string_view raw)
    -> std::expected<std::filesystem::path, cfire::Error> {
    std::filesystem::path p = raw.empty() ? std::filesystem::path(".") : std::filesystem::path(raw);
    if (opts.allow_outside_root) return p;
    std::error_code ec;
    auto root = std::filesystem::weakly_canonical(std::filesystem::absolute(opts.root, ec), ec);
    auto cand = std::filesystem::weakly_canonical(p.is_absolute() ? p : (root / p), ec);  // resolves `..` and symlinks
    auto rel = cand.lexically_relative(root);
    if (ec || rel.empty() || *rel.begin() == "..") {
        return cfire::make_error(std::format(
            "path '{}' is outside the MCP workspace root '{}' (start `cfire mcp --allow-outside-root` to permit this)",
            raw, root.string()));
    }
    return cand;
}

[[nodiscard]] inline auto build_compiler_mcp_server(const std::filesystem::path& runtime_dir, McpOptions mcp_opts = {}) -> cfire::ai::McpServer {
    auto srv = cfire::ai::make_mcp_server("cfire-compiler", std::string(cfire::version::kVersion));

    srv.register_tool<EmptyInput>(
        "cfire_stdlib_list",
        "List all cfire standard library and vendor modules with their import paths and descriptions.",
        [](EmptyInput) -> std::expected<std::string, cfire::Error> {
            std::ostringstream oss;
            for (const auto& mod : cfire::doc::stdlib_catalog()) {
                oss << "- `" << mod.import_path << "` (" << mod.ns_alias << "): " << mod.overview << "\n";
            }
            return oss.str();
        }
    );

    srv.register_tool<QueryInput>(
        "cfire_stdlib_lookup",
        "Look up cfire standard library functions, types, compiler diagnostic codes (e.g. 'cfire/USE-AFTER-FREE'), and examples by module path (e.g. 'core:str', 'ext:ai', 'ext:webui', 'vendor:raylib') or symbol name.",
        [](QueryInput in) -> std::expected<std::string, cfire::Error> {
            std::ostringstream oss;
            bool found = false;
            std::string norm_q = in.query;
            for (const auto& d : cfire::doc::diagnostics_catalog()) {
                if (d.code == norm_q || d.code == ("cfire/" + norm_q) ||
                    d.code.find(norm_q) != std::string_view::npos ||
                    d.title.find(norm_q) != std::string_view::npos) {
                    found = true;
                    oss << "## `" << d.code << "` — " << d.title << "\n"
                        << d.rule << "\n\n"
                        << "**Rejected Example:**\n```cf\n" << d.bad_example << "\n```\n"
                        << "**Accepted Fix:**\n```cf\n" << d.good_example << "\n```\n\n";
                }
            }
            for (const auto& mod : cfire::doc::stdlib_catalog()) {
                bool mod_match = (mod.import_path == norm_q || mod.ns_alias == norm_q ||
                                  mod.import_path.find(norm_q) != std::string_view::npos ||
                                  (norm_q.starts_with("core:") && mod.import_path == ("ext:" + norm_q.substr(5))) ||
                                  (norm_q.starts_with("ext:") && mod.import_path == ("core:" + norm_q.substr(4))));
                if (mod_match) {
                    found = true;
                    oss << "## `" << mod.import_path << "` — " << mod.title << "\n" << mod.overview << "\n\n";
                    for (const auto& fn : mod.functions) {
                        oss << "- `" << fn.cf_sig << "` — " << fn.summary << "\n";
                    }
                    if (mod.ns_alias == "log") {
                        oss << "- `type Level enum { Debug, Info, Warn, Error }` — Minimum severity threshold (`log.Level.Debug` / `log.Level::Debug`)\n";
                        oss << "- `type Format enum { Text, Json }` — Output sink format (`log.Format.Text` / `log.Format.Json`)\n";
                    }
                    oss << "\n";
                    continue;
                }
                for (const auto& fn : mod.functions) {
                    if (fn.name.find(in.query) != std::string_view::npos ||
                        fn.cf_sig.find(in.query) != std::string_view::npos) {
                        found = true;
                        oss << "- [" << mod.import_path << "] `" << fn.cf_sig << "` — " << fn.summary << "\n";
                    }
                }
            }
            if (norm_q == "log.Level" || norm_q == "log::Level" || norm_q == "Level") {
                found = true;
                oss << "## `log.Level` (`core:log`)\n```cf\ntype Level enum {\n    Debug = 0\n    Info = 1\n    Warn = 2\n    Error = 3\n}\n```\n"
                    << "Lowered C++23: `cfire::log::Level` (`uint8_t`). Pass to `log.set_level(log.Level.Debug)`.\n";
            }
            if (norm_q == "log.Format" || norm_q == "log::Format" || norm_q == "Format") {
                found = true;
                oss << "## `log.Format` (`core:log`)\n```cf\ntype Format enum {\n    Text = 0\n    Json = 1\n}\n```\n"
                    << "Lowered C++23: `cfire::log::Format` (`uint8_t`). Pass to `log.set_format(log.Format.Json)`.\n";
            }
            if (!found) {
                return std::format("No stdlib module, symbol, or diagnostic matching '{}'", in.query);
            }
            return oss.str();
        }
    );

    srv.register_tool<CheckInput>(
        "cfire_check",
        "Type-check cfire (.cf) inline code or a workspace file/directory path, verify memory safety & parameter intents, and validate C++23 codegen with fix-it suggestions.",
        [runtime_dir, mcp_opts](CheckInput in) -> std::expected<std::string, cfire::Error> {
            cfire::analysis::CheckOptions opts;
            opts.runtime_dir = runtime_dir;

            std::vector<cfire::analysis::Diagnostic> diags;
            if (!in.code.empty()) {
                std::string src = in.code;
                if (src.find("package ") == std::string::npos) {
                    src = "package main\n\nfn main() {\n" + src + "\n}\n";
                }
                diags = cfire::analysis::check_source("snippet.cf", src, opts);
            } else {
                auto confined = resolve_in_root(mcp_opts, in.path);
                if (!confined) return std::unexpected(confined.error());
                std::filesystem::path target_path = *confined;
                std::error_code ec;
                if (!std::filesystem::exists(target_path, ec)) {
                    return cfire::make_error(std::format("path '{}' not found", target_path.string()));
                }
                diags = cfire::analysis::check_path(target_path, opts);
            }

            if (diags.empty()) {
                return "OK (0 errors)";
            }

            std::string out_msg;
            for (const auto& d : diags) {
                if (!out_msg.empty()) out_msg += "\n";
                out_msg += std::format("{}:{}:{}: {}: {} ({})", d.file, d.line, d.col, d.severity, d.message, d.code);
                for (const auto& fx : d.fixits) {
                    out_msg += std::format("\n  fix-it (line {}:{}): {}", fx.start_line, fx.start_col, fx.title);
                }
            }
            return std::format("Diagnostics:\n{}", out_msg);
        }
    );

    srv.register_tool<CodeInput>(
        "cfire_eval",
        "Compile and execute a cfire (.cf) program or snippet and return its stdout/stderr.",
        [runtime_dir](CodeInput in) -> std::expected<std::string, cfire::Error> {
            // Per-call private directory (mkdtemp, 0700) for the snippet and its binary (CF-153, Q15).
            cfire::detail::SecureTempDir eval_dir("cfire_mcp_eval");
            if (!eval_dir.ok()) return cfire::make_error("cfire_eval: could not create a private temporary directory");
            auto tmp_dir = eval_dir.path();
            std::error_code ec;
            auto cf_file = tmp_dir / "eval.cf";
            auto bin_file = tmp_dir / "eval_bin";
            std::string src = in.code;
            if (src.find("package ") == std::string::npos) {
                std::string imports;
                if (src.contains("json.") || src.contains("`json:")) imports += "import \"core:json\"\n";
                if (src.contains("ai.")) imports += "import \"ext:ai\"\n";
                if (src.contains("webui.") || src.contains("App.")) imports += "import \"ext:webui\"\n";
                if (src.contains("math.")) imports += "import \"core:math\"\n";
                if (src.contains("fs.")) imports += "import \"core:fs\"\n";
                if (src.contains("os.")) imports += "import \"core:os\"\n";
                src = "package main\n" + imports + "\nfn main() {\n" + src + "\n}\n";
            }
            {
                std::ofstream out(cf_file, std::ios::binary | std::ios::trunc);
                out << src;
            }
            auto plan = cfire::build::build_native(cf_file, runtime_dir, bin_file);
            if (!plan) {
                std::filesystem::remove_all(tmp_dir, ec);
                return cfire::make_error(plan.error());
            }
            int rc = 0;
            bool timed_out = false;
            std::string output = run_process_argv_timeout({bin_file.string()}, 10, rc, timed_out);
            std::filesystem::remove_all(tmp_dir, ec);
            if (timed_out) {
                return cfire::make_error("cfire_eval timed out after 10 seconds (terminated)");
            }
            return output;
        }
    );

    srv.register_tool<PathInput>(
        "cfire_outline",
        "Return a token-compressed AST outline of packages, imports, structs, enums, interfaces, function signatures, and test blocks for a .cf file or directory.",
        [mcp_opts](PathInput in) -> std::expected<std::string, cfire::Error> {
            auto confined = resolve_in_root(mcp_opts, in.path);
            if (!confined) return std::unexpected(confined.error());
            std::filesystem::path target_path = *confined;
            std::vector<std::filesystem::path> cf_files;
            std::error_code ec;
            if (std::filesystem::is_directory(target_path, ec)) {
                for (auto it = std::filesystem::recursive_directory_iterator(target_path, ec);
                     it != std::filesystem::recursive_directory_iterator();
                     it.increment(ec)) {
                    if (ec) break;
                    if (it->is_directory(ec)) {
                        if (is_ignored_mcp_dir(it->path().filename().string())) {
                            it.disable_recursion_pending();
                        }
                        continue;
                    }
                    if (it->is_regular_file(ec) && it->path().extension() == ".cf") {
                        cf_files.push_back(it->path());
                    }
                }
            } else if (std::filesystem::exists(target_path, ec)) {
                cf_files.push_back(target_path);
            } else {
                return cfire::make_error(std::format("path '{}' not found", target_path.string()));
            }
            std::sort(cf_files.begin(), cf_files.end());
            std::ostringstream oss;
            for (const auto& f : cf_files) {
                auto content = cfire::build::read_file_text(f);
                if (!content) continue;
                auto ast = cfire::frontend::parse_source(f.string(), *content);
                if (!ast) continue;
                oss << "## " << f.string() << " (package `" << ast->package_name << "`)\n";
                for (const auto& st : ast->structs) {
                    oss << "- struct `" << st.name << "`\n";
                }
                for (const auto& en : ast->enums) {
                    oss << "- enum `" << en.name << "`\n";
                }
                for (const auto& iface : ast->interfaces) {
                    oss << "- interface `" << iface.name << "`\n";
                }
                for (const auto& fn : ast->functions) {
                    oss << "- `" << cfire::fmt::format_fn_signature(fn) << "`\n";
                }
                for (const auto& t : ast->tests) {
                    const char* kind = t.is_bench ? "bench" : (t.is_fuzz ? "fuzz" : "test");
                    oss << "- " << kind << " `\"" << t.name << "\"`\n";
                }
            }
            return oss.str();
        }
    );

    srv.register_tool<TestInput>(
        "cfire_test",
        "Run cfire unit tests (`test`), property-based fuzz tests (`fuzz` with counterexample shrinking), executable doc-tests (`///`), automatic JSON round-trip invariants, and multi-iteration parameterized benchmarks (`bench`).",
        [runtime_dir, mcp_opts](TestInput in) -> std::expected<std::string, cfire::Error> {
            auto cfire_bin = cfire::lsp::resolve_cfire_binary(runtime_dir.parent_path());
            std::vector<std::string> argv = {cfire_bin, in.bench ? "bench" : "test"};
            if (!in.path.empty()) {
                auto confined = resolve_in_root(mcp_opts, in.path);
                if (!confined) return std::unexpected(confined.error());
                argv.push_back(confined->string());
            }
            if (!in.filter.empty()) {
                argv.push_back("--filter");
                argv.push_back(in.filter);
            }
            if (in.fuzz_runs > 0 && !in.bench) {
                argv.push_back("--fuzz-runs");
                argv.push_back(std::to_string(in.fuzz_runs));
            }
            if (in.seed != 0 && !in.bench) {
                argv.push_back("--seed");
                argv.push_back(std::to_string(in.seed));
            }
            if (in.bench_time > 0 && in.bench) {
                argv.push_back("--bench-time");
                argv.push_back(std::to_string(in.bench_time));
            }
            int rc = 0;
            bool timed_out = false;
            std::string output = run_process_argv_timeout(argv, 60, rc, timed_out);
            if (timed_out) {
                return cfire::make_error("cfire_test timed out after 60 seconds");
            }
            return output;
        }
    );

    srv.register_tool<TargetInput>(
        "cfire_inspect",
        "Inspect a source coordinate ('<file.cf>:<line>:<col>'), foreign C/C++ header ('cpp <header.h>'), stdlib symbol, or compiler diagnostic code ('cfire/USE-AFTER-FREE').",
        [runtime_dir, mcp_opts](TargetInput in) -> std::expected<std::string, cfire::Error> {
            std::string target_str = in.target;
            // 1. Check diagnostic catalog
            for (const auto& d : cfire::doc::diagnostics_catalog()) {
                if (d.code == target_str || d.code == ("cfire/" + target_str)) {
                    return std::format("# {} — {}\n\n{}\n\n**Rejected Example:**\n```cf\n{}\n```\n\n**Accepted Fix:**\n```cf\n{}\n```",
                        d.code, d.title, d.rule, d.bad_example, d.good_example);
                }
            }
            // 2. Check C/C++ header ('cpp <header.h>')
            if (target_str.starts_with("cpp ")) {
                std::string header = target_str.substr(4);
                while (!header.empty() && std::isspace(static_cast<unsigned char>(header.front()))) header.erase(header.begin());
                while (!header.empty() && std::isspace(static_cast<unsigned char>(header.back()))) header.pop_back();
                auto cache_dir = runtime_dir.parent_path() / ".cfire" / "cache";
                std::error_code ec;
                std::filesystem::create_directories(cache_dir, ec);
                if (ec) {
                    cache_dir = cfire::detail::user_cache_dir("mcp");  // never a shared temp-dir name (CF-153)
                }
                auto idx = cfire::bridge::inspect_header(header, {}, cache_dir);
                if (!idx) return cfire::make_error(idx.error());
                std::ostringstream oss;
                oss << "Header: <" << header << "> (" << idx->symbols.size() << " symbols)\n";
                for (const auto& [sname, sym] : idx->symbols) {
                    oss << "  cpp::" << sym.name << "(...) -> " << (sym.return_type.empty() ? "void" : sym.return_type) << "\n";
                }
                return oss.str();
            }
            // 3. Check '<file.cf>:<line>:<col>'
            auto c1 = target_str.rfind(':');
            auto c2 = (c1 != std::string::npos && c1 > 0) ? target_str.rfind(':', c1 - 1) : std::string::npos;
            if (c1 != std::string::npos && c2 != std::string::npos && c2 < c1) {
                auto pline = cfire::detail::parse_int<int>(target_str.substr(c2 + 1, c1 - c2 - 1));
                auto pcol = cfire::detail::parse_int<int>(target_str.substr(c1 + 1));
                if (pline && pcol && *pline > 0 && *pcol > 0) {
                    std::string req_file = target_str.substr(0, c2);
                    if (auto confined = resolve_in_root(mcp_opts, req_file); !confined) return std::unexpected(confined.error());
                    cfire::lsp::Server lsp_srv;
                    std::error_code ec;
                    auto abs_p = std::filesystem::absolute(req_file, ec);
                    std::string uri = cfire::lsp::detail::path_to_uri(abs_p);
                    int line_0 = std::max(0, *pline - 1);
                    int col_0 = std::max(0, *pcol - 1);
                    std::string hover_md = lsp_srv.compute_hover(uri, line_0, col_0);
                    std::string def_json = lsp_srv.compute_definition(uri, line_0, col_0);

                    std::ostringstream oss;
                    if (!hover_md.empty()) {
                        oss << hover_md << "\n";
                    }
                    auto raw_txt = cfire::build::read_file_text(abs_p);
                    if (raw_txt) {
                        auto lines = cfire::lsp::detail::split_lines(*raw_txt);
                        if (static_cast<std::size_t>(line_0) < lines.size()) {
                            auto span = cfire::lsp::detail::find_string_interpolation_span_at(lines[static_cast<std::size_t>(line_0)], col_0);
                            if (span.in_interp_expr) {
                                oss << "\n**String Interpolation Context:**\n- Placeholder expression: `{" << span.expr_text
                                    << (span.fmt_spec.empty() ? "" : (":" + span.fmt_spec)) << "}`\n";
                                if (!span.fmt_spec.empty()) {
                                    oss << "- Format specifier: `:" << span.fmt_spec << "` (lowered to `std::format(\"{:" << span.fmt_spec << "}\", ...)`)\n";
                                }
                            }
                        }
                        auto sym = cfire::lsp::detail::symbol_at_position(lines, line_0, col_0);
                        if (!sym.name.empty()) {
                            auto occs = lsp_srv.find_symbol_occurrences(uri, line_0, col_0, sym.name, sym.qualifier);
                            if (!occs.empty()) {
                                oss << "\n**References (" << occs.size() << "):**\n";
                                for (const auto& o : occs) {
                                    oss << "- `" << cfire::lsp::detail::uri_to_path(o.uri) << ":" << (o.line + 1) << ":" << (o.start_col + 1) << "`\n";
                                }
                            }
                        }
                    }
                    if (!def_json.empty() && def_json != "null") {
                        oss << "\n**Definition:** `" << def_json << "`\n";
                    }
                    return oss.str();
                }
            }
            // 4. Fallback to stdlib lookup (including `log.Level` and `log.Format`)
            if (target_str == "log.Level" || target_str == "log::Level" || target_str == "Level") {
                return "### Enum `log.Level` (`core:log`)\n```cf\ntype Level enum {\n    Debug = 0\n    Info = 1\n    Warn = 2\n    Error = 3\n}\n```\n**Lowered C++23:** `cfire::log::Level` (`uint8_t`)\n\nMinimum severity threshold for structured logging (`log.set_level(log.Level.Debug)` or `log.set_level(log.Level::Debug)`).";
            }
            if (target_str == "log.Format" || target_str == "log::Format" || target_str == "Format") {
                return "### Enum `log.Format` (`core:log`)\n```cf\ntype Format enum {\n    Text = 0\n    Json = 1\n}\n```\n**Lowered C++23:** `cfire::log::Format` (`uint8_t`)\n\nOutput sink format for structured logging (`log.set_format(log.Format.Json)` or `log.set_format(log.Format::Text)`).";
            }
            for (const auto& mod : cfire::doc::stdlib_catalog()) {
                for (const auto& fn : mod.functions) {
                    if (fn.name == target_str || (mod.ns_alias + "." + fn.name) == target_str) {
                        return std::format("### {}\n\n{}\n\n**Example:**\n```cf\n{}\n```",
                            fn.cf_sig, fn.summary, fn.example);
                    }
                }
            }
            return cfire::make_error(std::format("unknown inspect target '{}'", target_str));
        }
    );

    srv.register_tool<RenameInput>(
        "cfire_rename",
        "Semantically rename a cfire identifier at '<file.cf>:<line>:<col>' across the workspace, including occurrences inside '{...}' replacement strings.",
        [mcp_opts](RenameInput in) -> std::expected<std::string, cfire::Error> {
            auto c1 = in.target.rfind(':');
            auto c2 = (c1 != std::string::npos && c1 > 0) ? in.target.rfind(':', c1 - 1) : std::string::npos;
            if (c1 == std::string::npos || c2 == std::string::npos || c2 >= c1) {
                return cfire::make_error(std::format("invalid target '{}': expected '<file.cf>:<line>:<col>'", in.target));
            }
            auto pline = cfire::detail::parse_int<int>(in.target.substr(c2 + 1, c1 - c2 - 1));
            auto pcol = cfire::detail::parse_int<int>(in.target.substr(c1 + 1));
            if (!pline || !pcol || *pline <= 0 || *pcol <= 0) {
                return cfire::make_error(std::format("invalid line/col in target '{}'", in.target));
            }
            auto confined = resolve_in_root(mcp_opts, in.target.substr(0, c2));
            if (!confined) return std::unexpected(confined.error());
            std::filesystem::path target_file = *confined;
            auto res = cfire::refactor::plan_rename(target_file, *pline, *pcol, in.new_name);
            if (!res.success) {
                return cfire::make_error(res.error);
            }
            for (const auto& m : res.modifications) {  // a workspace-wide rename must not write outside the root
                if (auto inside = resolve_in_root(mcp_opts, m.file_path); !inside) return std::unexpected(inside.error());
            }
            std::string diff = cfire::refactor::generate_unified_diff(res);
            if (!in.dry_run) {
                if (auto applied = cfire::refactor::apply_rename(res); !applied) {
                    return cfire::make_error(applied.error());
                }
            }
            std::ostringstream oss;
            oss << (in.dry_run ? "[DRY-RUN] " : "") << "Renamed `" << res.old_name << "` -> `" << res.new_name
                << "` (" << res.total_occurrences << " occurrence(s) across " << res.modifications.size() << " file(s)).\n\n"
                << "```diff\n" << diff << "```\n";
            return oss.str();
        }
    );

    srv.register_tool<EmptyInput>(
        "cfire_doc_llm",
        "Return the complete cfire (.cf) LLM language reference sheet, including syntax, parameter intents, memory-safety diagnostics, and stdlib signatures.",
        [](EmptyInput) -> std::expected<std::string, cfire::Error> {
            return cfire::doc::generate_llm_reference();
        }
    );

    return srv;
}

inline auto serve_stdio(const std::filesystem::path& runtime_dir, McpOptions opts = {}) -> int {
    auto srv = build_compiler_mcp_server(runtime_dir, std::move(opts));
    return srv.serve_stdio();
}

} // namespace cfire::mcp

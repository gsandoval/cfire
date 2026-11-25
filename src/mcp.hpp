#pragma once
// src/mcp.hpp — Built-In Model Context Protocol (MCP) Server for the `cfire` Toolchain (`cfire mcp`)

#include "mcp_jsonrpc.hpp"
#include "analysis.hpp"
#include "buildsys.hpp"
#include "diagnostics.hpp"
#include "docgen.hpp"
#include "formatter.hpp"
#include "lsp.hpp"
#include "refactor.hpp"
#include "sema/builtin_methods.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <atomic>
#include <cctype>
#include <chrono>
#include <thread>
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

[[nodiscard]] inline auto format_builtin_receivers(std::uint8_t recv) -> std::string {
    std::vector<std::string_view> names;
    if (recv & cfire::frontend::BmInt) names.push_back("int");
    if (recv & cfire::frontend::BmFloat) names.push_back("float");
    if (recv & cfire::frontend::BmStr) names.push_back("str");
    if (recv & cfire::frontend::BmSlice) names.push_back("[]T");
    if (recv & cfire::frontend::BmArray) names.push_back("[N]T");
    if (recv & cfire::frontend::BmMap) names.push_back("map[K]V");
    if (recv & cfire::frontend::BmBool) names.push_back("bool");
    std::string out;
    for (std::size_t i = 0; i < names.size(); ++i) {
        if (i > 0) out += " | ";
        out += names[i];
    }
    return out;
}

[[nodiscard]] inline auto matches_builtin_method_query(
    const cfire::frontend::BuiltinMethod& bm,
    std::string_view query,
    bool exact_only = false
) -> bool {
    if (query.empty()) return false;
    auto check_tag = [&](std::string_view tag) {
        std::string qualified = std::format("{}.{}", tag, bm.name);
        return exact_only ? (qualified == query) : (qualified.find(query) != std::string_view::npos);
    };
    if (exact_only ? (bm.name == query) : (bm.name.find(query) != std::string_view::npos)) {
        return true;
    }
    if ((bm.recv & cfire::frontend::BmInt) && check_tag("int")) return true;
    if ((bm.recv & cfire::frontend::BmFloat) && check_tag("float")) return true;
    if ((bm.recv & cfire::frontend::BmStr) && check_tag("str")) return true;
    if ((bm.recv & cfire::frontend::BmSlice) && (check_tag("slice") || check_tag("[]T"))) return true;
    if ((bm.recv & cfire::frontend::BmArray) && (check_tag("array") || check_tag("[N]T"))) return true;
    if ((bm.recv & cfire::frontend::BmMap) && (check_tag("map") || check_tag("map[K]V"))) return true;
    if ((bm.recv & cfire::frontend::BmBool) && check_tag("bool")) return true;
    return false;
}

[[nodiscard]] inline auto format_std_type_doc_md(
    const cfire::doc::StdModuleDoc& mod,
    const cfire::doc::StdTypeDoc& td
) -> std::string {
    std::ostringstream oss;
    std::string kind_label = td.kind;
    if (!kind_label.empty()) kind_label[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(kind_label[0])));
    oss << "### " << kind_label << " `" << mod.ns_alias << "." << td.name << "` (`" << mod.import_path << "`)\n\n"
        << td.summary << "\n";
    if (!td.fields.empty()) {
        oss << "\n**Fields:**\n";
        for (const auto& f : td.fields) {
            oss << "- `" << f.name << ": " << f.type << "`";
            if (!f.summary.empty()) oss << " — " << f.summary;
            oss << "\n";
        }
    }
    if (!td.variants.empty()) {
        oss << "\n**Variants:**\n";
        for (const auto& v : td.variants) {
            oss << "- `" << mod.ns_alias << "." << td.bare_name() << "::" << v << "`\n";
        }
    }
    std::string mprefix = std::string(td.bare_name()) + ".";
    bool header_emitted = false;
    for (const auto& fn : mod.functions) {
        if (fn.name.starts_with(mprefix)) {
            if (!header_emitted) {
                oss << "\n**Methods:**\n";
                header_emitted = true;
            }
            oss << "- `" << fn.cf_sig << "` — " << fn.summary << "\n";
        }
    }
    return oss.str();
}

[[nodiscard]] inline auto format_builtin_method_md(const cfire::frontend::BuiltinMethod& bm) -> std::string {
    std::string recvs = format_builtin_receivers(bm.recv);
    std::string sig = std::format("fn ({}{}) {}({}) -> {}",
        bm.mutates ? "mut self: " : "self: ", recvs, bm.name, bm.params, bm.ret);
    return std::format("### `{}`\n\n{}\n\n**Example:**\n```cf\n{}\n```",
        sig, bm.summary, bm.example);
}

[[nodiscard]] inline auto is_ignored_mcp_dir(std::string_view name) -> bool {
    return name == ".cfire" || name == ".tako" || name == ".git" ||
           name == "dist" || name == "vendor" || name == "node_modules" ||
           name == "ejected_cpp";
}

template <>
inline auto schema_for<EmptyInput>() -> std::string {
    return R"({"type":"object","properties":{},"additionalProperties":false})";
}

template <>
inline auto parse_input<EmptyInput>(const JsonVal&) -> std::expected<EmptyInput, cfire::Error> {
    return EmptyInput{};
}

template <>
inline auto schema_for<QueryInput>() -> std::string {
    return R"({"type":"object","properties":{"query":{"type":"string","description":"Module import path (e.g. 'core:fs', 'core:json', 'ext:ai', 'ext:webui'), diagnostic code (e.g. 'cfire/USE-AFTER-FREE'), or function/type name"}},"required":["query"],"additionalProperties":false})";
}

template <>
inline auto parse_input<QueryInput>(const JsonVal& obj) -> std::expected<QueryInput, cfire::Error> {
    QueryInput out;
    auto q = get_req_str(obj, "query");
    if (!q) return std::unexpected(q.error());
    out.query = std::move(*q);
    return out;
}

template <>
inline auto schema_for<CodeInput>() -> std::string {
    return R"({"type":"object","properties":{"code":{"type":"string","description":"cfire (.cf) source code or snippet"}},"required":["code"],"additionalProperties":false})";
}

template <>
inline auto parse_input<CodeInput>(const JsonVal& obj) -> std::expected<CodeInput, cfire::Error> {
    CodeInput out;
    auto c = get_req_str(obj, "code");
    if (!c) return std::unexpected(c.error());
    out.code = std::move(*c);
    return out;
}

template <>
inline auto schema_for<CheckInput>() -> std::string {
    return R"JSON({"type":"object","properties":{"code":{"type":"string","description":"Inline cfire (.cf) source code or snippet to check"},"path":{"type":"string","description":"Path to a .cf file or workspace directory to check (resolves same-directory sibling files)"}},"additionalProperties":false})JSON";
}

template <>
inline auto parse_input<CheckInput>(const JsonVal& obj) -> std::expected<CheckInput, cfire::Error> {
    CheckInput out;
    auto c = get_opt_str(obj, "code");
    if (!c) return std::unexpected(c.error());
    if (*c) out.code = std::move(**c);
    auto p = get_opt_str(obj, "path");
    if (!p) return std::unexpected(p.error());
    if (*p) out.path = std::move(**p);
    return out;
}

template <>
inline auto schema_for<PathInput>() -> std::string {
    return R"JSON({"type":"object","properties":{"path":{"type":"string","description":"Path to a .cf file or directory to outline (defaults to '.')"}},"additionalProperties":false})JSON";
}

template <>
inline auto parse_input<PathInput>(const JsonVal& obj) -> std::expected<PathInput, cfire::Error> {
    PathInput out;
    auto p = get_opt_str(obj, "path");
    if (!p) return std::unexpected(p.error());
    if (*p) out.path = std::move(**p);
    return out;
}

template <>
inline auto schema_for<TargetInput>() -> std::string {
    return R"({"type":"object","properties":{"target":{"type":"string","description":"Inspect target: '<file.cf>:<line>:<col>', 'cpp <header.h>', '<module>.<symbol>', or 'cfire/<DIAG-CODE>'"}},"required":["target"],"additionalProperties":false})";
}

template <>
inline auto parse_input<TargetInput>(const JsonVal& obj) -> std::expected<TargetInput, cfire::Error> {
    TargetInput out;
    auto t = get_req_str(obj, "target");
    if (!t) return std::unexpected(t.error());
    out.target = std::move(*t);
    return out;
}

template <>
inline auto schema_for<RenameInput>() -> std::string {
    return R"JSON({"type":"object","properties":{"target":{"type":"string","description":"Source coordinate '<file.cf>:<line>:<col>' pointing at the symbol to rename (including inside '{...}' interpolated strings)"},"new_name":{"type":"string","description":"New valid cfire identifier name"},"dry_run":{"type":"boolean","description":"Preview the unified diff and occurrence count without modifying files on disk"}},"required":["target","new_name"],"additionalProperties":false})JSON";
}

template <>
inline auto parse_input<RenameInput>(const JsonVal& obj) -> std::expected<RenameInput, cfire::Error> {
    RenameInput out;
    auto t = get_req_str(obj, "target");
    if (!t) return std::unexpected(t.error());
    out.target = std::move(*t);
    auto nn = get_req_str(obj, "new_name");
    if (!nn) return std::unexpected(nn.error());
    out.new_name = std::move(*nn);
    auto dr = get_opt_bool(obj, "dry_run");
    if (!dr) return std::unexpected(dr.error());
    if (*dr) out.dry_run = **dr;
    return out;
}

template <>
inline auto schema_for<TestInput>() -> std::string {
    return R"JSON({"type":"object","properties":{"path":{"type":"string","description":"Path to a .cf file or directory to test (defaults to '.')"},"filter":{"type":"string","description":"Substring filter for test, fuzz, or bench names"},"bench":{"type":"boolean","description":"Run multi-iteration statistical benchmarks (bench blocks)"},"fuzz_runs":{"type":"integer","description":"Number of property-based fuzz iterations per fuzz block (default 100)"},"seed":{"type":"integer","description":"Deterministic PRNG seed for fuzzing (0 = auto)"},"bench_time":{"type":"integer","description":"Target benchmark measurement window in ms per benchmark variation (default 200)"}},"additionalProperties":false})JSON";
}

template <>
inline auto parse_input<TestInput>(const JsonVal& obj) -> std::expected<TestInput, cfire::Error> {
    TestInput out;
    auto p = get_opt_str(obj, "path");
    if (!p) return std::unexpected(p.error());
    if (*p) out.path = std::move(**p);
    auto f = get_opt_str(obj, "filter");
    if (!f) return std::unexpected(f.error());
    if (*f) out.filter = std::move(**f);
    auto b = get_opt_bool(obj, "bench");
    if (!b) return std::unexpected(b.error());
    if (*b) out.bench = **b;
    auto fr = get_opt_i64(obj, "fuzz_runs");
    if (!fr) return std::unexpected(fr.error());
    if (*fr) out.fuzz_runs = static_cast<int>(**fr);
    auto sd = get_opt_i64(obj, "seed");
    if (!sd) return std::unexpected(sd.error());
    if (*sd) out.seed = static_cast<std::uint64_t>(**sd);
    auto bt = get_opt_i64(obj, "bench_time");
    if (!bt) return std::unexpected(bt.error());
    if (*bt) out.bench_time = static_cast<int>(**bt);
    return out;
}

} // namespace cfire::mcp

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
    // honor `timeout_seconds` like the POSIX branch: run the tool in a job object, read its combined
    // output on a thread, and terminate the whole job if it outlives the timeout.
    SECURITY_ATTRIBUTES sa{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
    HANDLE read_end = nullptr;
    HANDLE write_end = nullptr;
    if (!::CreatePipe(&read_end, &write_end, &sa, 0)) {
        exit_code = -1;
        return "failed to create pipe";
    }
    ::SetHandleInformation(read_end, HANDLE_FLAG_INHERIT, 0);
    STARTUPINFOA si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = write_end;
    si.hStdError = write_end;
    si.hStdInput = ::GetStdHandle(STD_INPUT_HANDLE);
    PROCESS_INFORMATION pi{};
    HANDLE job = ::CreateJobObjectA(nullptr, nullptr);
    if (job) {
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
        limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        ::SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof(limits));
    }
    std::vector<char> cmd_buf(cmd_line.begin(), cmd_line.end());
    cmd_buf.push_back('\0');
    const BOOL started = ::CreateProcessA(nullptr, cmd_buf.data(), nullptr, nullptr, TRUE, CREATE_SUSPENDED | CREATE_NO_WINDOW,
                                          nullptr, nullptr, &si, &pi);
    ::CloseHandle(write_end);
    if (!started) {
        ::CloseHandle(read_end);
        if (job) ::CloseHandle(job);
        exit_code = -1;
        return "failed to start process";
    }
    if (job) ::AssignProcessToJobObject(job, pi.hProcess);
    ::ResumeThread(pi.hThread);
    std::string out;
    std::thread reader([&] {
        char buf[4096];
        DWORD n = 0;
        while (::ReadFile(read_end, buf, sizeof(buf), &n, nullptr) && n > 0) out.append(buf, n);
    });
    const DWORD wait_ms = timeout_seconds > 0 ? static_cast<DWORD>(timeout_seconds) * 1000 : INFINITE;
    if (::WaitForSingleObject(pi.hProcess, wait_ms) == WAIT_TIMEOUT) {
        timed_out = true;
        if (job) ::TerminateJobObject(job, 124);
        else ::TerminateProcess(pi.hProcess, 124);
        ::WaitForSingleObject(pi.hProcess, 5000);
    }
    DWORD code = 0;
    ::GetExitCodeProcess(pi.hProcess, &code);
    exit_code = timed_out ? 124 : static_cast<int>(code);
    if (job) ::CloseHandle(job);  // closing the job also ends any grandchildren still holding the pipe open
    reader.join();
    ::CloseHandle(read_end);
    ::CloseHandle(pi.hThread);
    ::CloseHandle(pi.hProcess);
    if (timed_out) out += "\n[error: execution timed out after " + std::to_string(timeout_seconds) + " seconds]";
    return out;
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

// every path an MCP client passes is confined to the server's workspace root, so an agent
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

[[nodiscard]] inline auto build_compiler_mcp_server(const std::filesystem::path& runtime_dir, McpOptions mcp_opts = {}) -> cfire::mcp::McpServer {
    auto srv = cfire::mcp::make_mcp_server("cfire-compiler", std::string(cfire::version::kVersion));

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
        "Look up cfire standard library modules, types, methods, builtin methods, compiler diagnostic codes (e.g. 'cfire/USE-AFTER-FREE'), and examples.",
        [](QueryInput in) -> std::expected<std::string, cfire::Error> {
            std::ostringstream oss;
            bool found = false;
            std::string norm_q = in.query;
            if (auto canon = cfire::modules::canonical_import_for_wrong_prefix(norm_q); !canon.empty()) {
                oss << "> Note: `" << norm_q << "` is non-canonical (`cfire/INVALID-IMPORT`); import as `"
                    << canon << "`.\n\n";
                norm_q = canon;
            }
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
                                  mod.import_path.find(norm_q) != std::string_view::npos);
                if (mod_match) {
                    found = true;
                    oss << "## `" << mod.import_path << "` — " << mod.title << "\n" << mod.overview << "\n\n";
                    if (!mod.type_docs.empty()) {
                        oss << "**Types:**\n";
                        for (const auto& td : mod.type_docs) {
                            oss << "- `" << td.kind << " " << mod.ns_alias << "." << td.name << "` — " << td.summary;
                            if (!td.fields.empty()) {
                                oss << " Fields: ";
                                for (std::size_t fi = 0; fi < td.fields.size(); ++fi) {
                                    if (fi > 0) oss << ", ";
                                    oss << "`" << td.fields[fi].name << ": " << td.fields[fi].type << "`";
                                }
                                oss << ".";
                            }
                            if (!td.variants.empty()) {
                                oss << " Variants: ";
                                for (std::size_t vi = 0; vi < td.variants.size(); ++vi) {
                                    if (vi > 0) oss << ", ";
                                    oss << "`" << mod.ns_alias << "." << td.bare_name() << "::" << td.variants[vi] << "`";
                                }
                                oss << ".";
                            }
                            oss << "\n";
                        }
                        oss << "\n";
                    }
                    oss << "**Functions & Methods:**\n";
                    for (const auto& fn : mod.functions) {
                        oss << "- `" << fn.cf_sig << "` — " << fn.summary << "\n";
                    }
                    oss << "\n";
                    continue;
                }
                for (const auto& td : mod.type_docs) {
                    std::string qname = mod.ns_alias + "." + std::string(td.bare_name());
                    std::string qname_cc = mod.ns_alias + "::" + std::string(td.bare_name());
                    bool type_match = (td.name == norm_q || td.bare_name() == norm_q ||
                                       qname == norm_q || qname_cc == norm_q ||
                                       qname.find(norm_q) != std::string::npos);
                    if (!type_match) {
                        for (const auto& v : td.variants) {
                            if (v == norm_q || (qname + "::" + v) == norm_q) {
                                type_match = true;
                                break;
                            }
                        }
                    }
                    if (type_match) {
                        found = true;
                        oss << format_std_type_doc_md(mod, td) << "\n";
                    }
                }
                for (const auto& fn : mod.functions) {
                    std::string qfn = mod.ns_alias + "." + fn.name;
                    if (fn.name.find(norm_q) != std::string_view::npos ||
                        fn.cf_sig.find(norm_q) != std::string_view::npos ||
                        qfn.find(norm_q) != std::string_view::npos) {
                        found = true;
                        oss << "- [" << mod.import_path << "] `" << fn.cf_sig << "` — " << fn.summary << "\n";
                    }
                }
            }
            for (const auto& bm : cfire::frontend::kBuiltinMethods) {
                if (matches_builtin_method_query(bm, norm_q, false)) {
                    found = true;
                    oss << "- [builtin `" << format_builtin_receivers(bm.recv) << "`] `"
                        << (bm.mutates ? "mut " : "") << bm.name << "(" << bm.params << ") -> " << bm.ret
                        << "` — " << bm.summary << " (`" << bm.example << "`)\n";
                }
            }
            if (!found) {
                return std::format("No stdlib module, type, builtin method, or diagnostic matching '{}'", in.query);
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
            // Per-call private directory (mkdtemp, 0700) for the snippet and its binary.
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
                if (src.contains("time.")) imports += "import \"core:time\"\n";
                if (src.contains("http.")) imports += "import \"core:net/http\"\n";
                if (src.contains("ipc.")) imports += "import \"core:net/ipc\"\n";
                src = "package main\n" + imports + "\nfn main() {\n" + src + "\n}\n";
            }
            {
                std::ofstream out(cf_file, std::ios::binary | std::ios::trunc);
                out << src;
            }
            struct ProfileRestoreGuard {
                cfire::build::BuildProfile prev;
                ~ProfileRestoreGuard() { cfire::build::set_build_profile(prev); }
            } prof_guard{cfire::build::current_build_profile()};
            cfire::build::set_build_profile(cfire::build::BuildProfile::DevDebug);
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
                    oss << "- struct `" << st.name << cfire::fmt::format_type_params_list(st.type_params) << "`\n";
                }
                for (const auto& en : ast->enums) {
                    oss << "- enum `" << en.name << cfire::fmt::format_type_params_list(en.type_params) << "`\n";
                }
                for (const auto& iface : ast->interfaces) {
                    oss << "- interface `" << iface.name << "`\n";
                    for (const auto& m : iface.methods) {
                        oss << "  - `" << cfire::fmt::format_interface_method_sig(m) << "`\n";
                    }
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
                if (header.empty()) {
                    return cfire::make_error("missing header name after 'cpp '");
                }
                std::error_code ec;
                std::filesystem::path ws_root = mcp_opts.root.empty()
                    ? std::filesystem::current_path(ec)
                    : mcp_opts.root;
                cfire::build::BuildPlan insp_plan;
                auto search_d = std::filesystem::weakly_canonical(ws_root, ec);
                if (ec || search_d.empty()) search_d = std::filesystem::absolute(ws_root, ec);
                for (int depth = 0; depth < 8 && !search_d.empty(); ++depth) {
                    auto bcf = search_d / "build.cf";
                    if (std::filesystem::exists(bcf, ec)) {
                        if (auto parsed = cfire::build::parse_build_cf(bcf)) {
                            cfire::build::apply_manifest_pkg_fields(
                                insp_plan, search_d, parsed->pkg_config, parsed->libraries,
                                parsed->frameworks, parsed->includes, parsed->defines, parsed->cflags, parsed->ldflags);
                            for (const auto& ex : parsed->executables) {
                                cfire::build::apply_manifest_pkg_fields(
                                    insp_plan, search_d, ex.pkg_config, ex.libraries,
                                    ex.frameworks, ex.includes, ex.defines, ex.cflags, ex.ldflags);
                            }
                        }
                        break;
                    }
                    if (search_d == search_d.parent_path()) break;
                    search_d = search_d.parent_path();
                }
                (void)cfire::build::resolve_plan_pkg_configs(insp_plan, runtime_dir);
                if (!ws_root.empty()) {
                    insp_plan.cflags.push_back("-I" + ws_root.string());
                }
                if (auto dr = cfire::build::resolve_plan_defines(insp_plan); !dr) {
                    return cfire::make_error(dr.error());
                }
                auto cache_dir = cfire::build::header_inspect_cache_dir(ws_root);
                auto idx = cfire::bridge::inspect_header(
                    header, insp_plan.cflags, cache_dir, cfire::build::resolve_cxx_compiler());
                if (!idx) return cfire::make_error(idx.error());
                std::vector<std::pair<std::string, cfire::bridge::CppSymbol>> sorted_syms(idx->symbols.begin(), idx->symbols.end());
                std::sort(sorted_syms.begin(), sorted_syms.end(), [](const auto& a, const auto& b) {
                    return a.first < b.first;
                });
                std::ostringstream oss;
                oss << "Header: <" << header << "> (" << sorted_syms.size() << " symbols)\n";
                for (const auto& [sname, sym] : sorted_syms) {
                    oss << "  cpp." << sym.name << "(";
                    for (std::size_t p = 0; p < sym.param_types.size(); ++p) {
                        if (p > 0) oss << ", ";
                        oss << sym.param_types[p];
                    }
                    oss << ") -> " << (sym.return_type.empty() ? "void" : sym.return_type) << "\n";
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
            // 4. Fallback to stdlib types, enum variants, functions, and builtin methods
            for (const auto& mod : cfire::doc::stdlib_catalog()) {
                for (const auto& td : mod.type_docs) {
                    std::string qname = mod.ns_alias + "." + std::string(td.bare_name());
                    std::string qname_cc = mod.ns_alias + "::" + std::string(td.bare_name());
                    if (td.name == target_str || td.bare_name() == target_str ||
                        qname == target_str || qname_cc == target_str) {
                        return format_std_type_doc_md(mod, td);
                    }
                    for (const auto& v : td.variants) {
                        if ((qname + "::" + v) == target_str || (td.bare_name() == target_str) ||
                            (std::string(td.bare_name()) + "::" + v) == target_str) {
                            return std::format("### Enum Variant `{}.{}::{}` (`{}`)\n\nVariant of `{}.{}`: {}",
                                mod.ns_alias, td.bare_name(), v, mod.import_path, mod.ns_alias, td.bare_name(), td.summary);
                        }
                    }
                }
                for (const auto& fn : mod.functions) {
                    if (fn.name == target_str || (mod.ns_alias + "." + fn.name) == target_str) {
                        return std::format("### {}\n\n{}\n\n**Example:**\n```cf\n{}\n```",
                            fn.cf_sig, fn.summary, fn.example);
                    }
                }
            }
            {
                std::ostringstream bm_oss;
                bool bm_hit = false;
                for (const auto& bm : cfire::frontend::kBuiltinMethods) {
                    if (matches_builtin_method_query(bm, target_str, true)) {
                        if (bm_hit) bm_oss << "\n\n";
                        bm_oss << format_builtin_method_md(bm);
                        bm_hit = true;
                    }
                }
                if (bm_hit) return bm_oss.str();
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

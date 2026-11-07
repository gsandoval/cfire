#pragma once
// src/mcp.hpp — Built-In Model Context Protocol (MCP) Server for the `cfire` Toolchain (`cfire mcp`)

#include "../runtime/core/ai.hpp"
#include "buildsys.hpp"
#include "diagnostics.hpp"
#include "docgen.hpp"
#include "formatter.hpp"
#include "lsp.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#if !defined(_WIN32)
#include <unistd.h>
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

[[nodiscard]] inline auto build_compiler_mcp_server(const std::filesystem::path& runtime_dir) -> cfire::ai::McpServer {
    auto srv = cfire::ai::make_mcp_server("cfire-compiler", "0.2.0");

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
            if (!found) {
                return std::format("No stdlib module, symbol, or diagnostic matching '{}'", in.query);
            }
            return oss.str();
        }
    );

    srv.register_tool<CheckInput>(
        "cfire_check",
        "Type-check cfire (.cf) inline code or a workspace file/directory path, verify memory safety & parameter intents, and validate C++23 codegen with fix-it suggestions.",
        [runtime_dir](CheckInput in) -> std::expected<std::string, cfire::Error> {
            auto check_single_cf = [&](const std::filesystem::path& cf_file, std::string_view src_override = "") -> std::optional<std::string> {
                std::string fname = cf_file.filename().string();
                bool is_test_f = fname.ends_with("_test.cf") || fname == "test.cf";
                auto res = cfire::build::analyze_and_emit(cf_file, runtime_dir, true, is_test_f);
                if (!res) {
                    std::string src = src_override.empty()
                        ? cfire::build::read_file_text(cf_file).value_or("")
                        : std::string(src_override);
                    auto lines = cfire::diag::split_lines(src);
                    auto sd = cfire::diag::parse_diagnostic_string(res.error(), cf_file.string());
                    cfire::diag::synthesize_fixits_for_diagnostic(sd, nullptr, lines);
                    std::string out_msg = res.error();
                    for (const auto& fx : sd.fixits) {
                        out_msg += std::format("\n  fix-it (line {}:{}): {}", fx.start_line, fx.start_col, fx.title);
                    }
                    return out_msg;
                }
                auto syntax_ok = cfire::build::verify_cpp_syntax_only(cf_file, res->first, res->second, runtime_dir);
                if (!syntax_ok) {
                    return syntax_ok.error();
                }
                return std::nullopt;
            };

            if (!in.code.empty()) {
                auto tmp_dir = std::filesystem::temp_directory_path() / std::format("cfire_mcp_check_{}", ::getpid());
                std::error_code ec;
                std::filesystem::create_directories(tmp_dir, ec);
                auto cf_file = tmp_dir / "snippet.cf";
                std::string src = in.code;
                if (src.find("package ") == std::string::npos) {
                    src = "package main\n\nfn main() {\n" + src + "\n}\n";
                }
                {
                    std::ofstream out(cf_file, std::ios::binary | std::ios::trunc);
                    out << src;
                }
                auto err = check_single_cf(cf_file, src);
                std::filesystem::remove_all(tmp_dir, ec);
                if (err) return std::format("Diagnostics:\n{}", *err);
                return "OK (0 errors)";
            }

            std::filesystem::path target_path = in.path.empty() ? "." : in.path;
            std::error_code ec;
            if (!std::filesystem::exists(target_path, ec)) {
                return cfire::make_error(std::format("path '{}' not found", target_path.string()));
            }
            if (!std::filesystem::is_directory(target_path, ec)) {
                if (auto err = check_single_cf(target_path)) {
                    return std::format("Diagnostics:\n{}", *err);
                }
                return "OK (0 errors)";
            }

            std::vector<std::filesystem::path> targets;
            std::set<std::filesystem::path> seen_dirs;
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
                    if (it->path().filename() == "build.cf") continue;
                    auto parent_dir = std::filesystem::weakly_canonical(it->path().parent_path(), ec);
                    std::string fn = it->path().filename().string();
                    bool is_test_f = fn.ends_with("_test.cf") || fn == "test.cf";
                    if (is_test_f || !seen_dirs.contains(parent_dir)) {
                        if (!is_test_f) seen_dirs.insert(parent_dir);
                        targets.push_back(it->path());
                    }
                }
            }
            std::sort(targets.begin(), targets.end());
            std::string all_diags;
            for (const auto& t : targets) {
                if (auto err = check_single_cf(t)) {
                    if (!all_diags.empty()) all_diags += "\n";
                    all_diags += *err;
                }
            }
            if (!all_diags.empty()) {
                return std::format("Diagnostics:\n{}", all_diags);
            }
            return "OK (0 errors)";
        }
    );

    srv.register_tool<CodeInput>(
        "cfire_eval",
        "Compile and execute a cfire (.cf) program or snippet and return its stdout/stderr.",
        [runtime_dir](CodeInput in) -> std::expected<std::string, cfire::Error> {
            auto tmp_dir = std::filesystem::temp_directory_path() / std::format("cfire_mcp_eval_{}", ::getpid());
            std::error_code ec;
            std::filesystem::create_directories(tmp_dir, ec);
            auto cf_file = tmp_dir / "eval.cf";
            auto bin_file = tmp_dir / "eval_bin";
            std::string src = in.code;
            if (src.find("package ") == std::string::npos) {
                src = "package main\nimport \"ext:ai\"\nimport \"core:json\"\nimport \"ext:webui\"\nimport \"core:str\"\nimport \"core:math\"\n\nfn main() {\n" + src + "\n}\n";
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
            std::string output = cfire::bridge::run_command_capture("\"" + bin_file.string() + "\" 2>&1", rc);
            std::filesystem::remove_all(tmp_dir, ec);
            return output;
        }
    );

    srv.register_tool<PathInput>(
        "cfire_outline",
        "Return a token-compressed AST outline of packages, imports, structs, enums, interfaces, function signatures, and test blocks for a .cf file or directory.",
        [](PathInput in) -> std::expected<std::string, cfire::Error> {
            std::filesystem::path target_path = in.path.empty() ? "." : in.path;
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
        [runtime_dir](TestInput in) -> std::expected<std::string, cfire::Error> {
            auto cfire_bin = cfire::lsp::resolve_cfire_binary(runtime_dir.parent_path());
            std::string cmd = "\"" + cfire_bin + "\" " + (in.bench ? "bench" : "test");
            if (!in.path.empty()) cmd += " \"" + in.path + "\"";
            if (!in.filter.empty()) cmd += " --filter \"" + in.filter + "\"";
            if (in.fuzz_runs > 0 && !in.bench) cmd += std::format(" --fuzz-runs {}", in.fuzz_runs);
            if (in.seed != 0 && !in.bench) cmd += std::format(" --seed {}", in.seed);
            if (in.bench_time > 0 && in.bench) cmd += std::format(" --bench-time {}", in.bench_time);
            cmd += " 2>&1";
            int rc = 0;
            return cfire::bridge::run_command_capture(cmd, rc);
        }
    );

    srv.register_tool<TargetInput>(
        "cfire_inspect",
        "Inspect a source coordinate ('<file.cf>:<line>:<col>'), foreign C/C++ header ('cpp <header.h>'), stdlib symbol, or compiler diagnostic code ('cfire/USE-AFTER-FREE').",
        [runtime_dir](TargetInput in) -> std::expected<std::string, cfire::Error> {
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
                    cache_dir = std::filesystem::temp_directory_path() / "cfire_mcp_cache";
                    std::filesystem::create_directories(cache_dir, ec);
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
                    cfire::lsp::Server lsp_srv;
                    std::error_code ec;
                    auto abs_p = std::filesystem::absolute(req_file, ec);
                    std::string uri = cfire::lsp::detail::path_to_uri(abs_p);
                    return lsp_srv.compute_hover(uri, std::max(0, *pline - 1), std::max(0, *pcol - 1));
                }
            }
            // 4. Fallback to stdlib lookup
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

    srv.register_tool<EmptyInput>(
        "cfire_doc_llm",
        "Return the complete cfire (.cf) LLM language reference sheet, including syntax, parameter intents, memory-safety diagnostics, and stdlib signatures.",
        [](EmptyInput) -> std::expected<std::string, cfire::Error> {
            return cfire::doc::generate_llm_reference();
        }
    );

    return srv;
}

inline auto serve_stdio(const std::filesystem::path& runtime_dir) -> int {
    auto srv = build_compiler_mcp_server(runtime_dir);
    return srv.serve_stdio();
}

} // namespace cfire::mcp

#pragma once
// src/mcp.hpp — Built-In Model Context Protocol (MCP) Server for the `cfire` Toolchain (`cfire mcp`)

#include "../runtime/core/ai.hpp"
#include "buildsys.hpp"
#include "docgen.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

namespace cfire::mcp {

struct EmptyInput {};
struct QueryInput {
    std::string query{};
};
struct CodeInput {
    std::string code{};
};

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
    return R"({"type":"object","properties":{"query":{"type":"string","description":"Module import path (e.g. 'core:ai') or function/type name"}},"required":["query"],"additionalProperties":false})";
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
        "Look up cfire standard library functions, types, and examples by module path (e.g. 'core:ai') or symbol name.",
        [](QueryInput in) -> std::expected<std::string, cfire::Error> {
            std::ostringstream oss;
            bool found = false;
            for (const auto& mod : cfire::doc::stdlib_catalog()) {
                bool mod_match = (mod.import_path == in.query || mod.ns_alias == in.query ||
                                  mod.import_path.find(in.query) != std::string_view::npos);
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
                return std::format("No stdlib module or symbol matching '{}'", in.query);
            }
            return oss.str();
        }
    );

    srv.register_tool<CodeInput>(
        "cfire_check",
        "Type-check cfire (.cf) source code and verify parameter intents and C++23 codegen.",
        [runtime_dir](CodeInput in) -> std::expected<std::string, cfire::Error> {
            auto tmp_dir = std::filesystem::temp_directory_path() / "cfire_mcp_check";
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
            auto res = cfire::build::analyze_and_emit(cf_file, runtime_dir, false);
            if (!res) {
                return std::format("Diagnostics:\n{}", res.error());
            }
            return "OK (0 errors)";
        }
    );

    srv.register_tool<CodeInput>(
        "cfire_eval",
        "Compile and execute a cfire (.cf) program or snippet and return its stdout/stderr.",
        [runtime_dir](CodeInput in) -> std::expected<std::string, cfire::Error> {
            auto tmp_dir = std::filesystem::temp_directory_path() / "cfire_mcp_eval";
            std::error_code ec;
            std::filesystem::create_directories(tmp_dir, ec);
            auto cf_file = tmp_dir / "eval.cf";
            auto bin_file = tmp_dir / "eval_bin";
            std::string src = in.code;
            if (src.find("package ") == std::string::npos) {
                src = "package main\nimport \"core:ai\"\nimport \"core:json\"\nimport \"core:str\"\nimport \"core:math\"\n\nfn main() {\n" + src + "\n}\n";
            }
            {
                std::ofstream out(cf_file, std::ios::binary | std::ios::trunc);
                out << src;
            }
            auto plan = cfire::build::build_native(cf_file, runtime_dir, bin_file);
            if (!plan) {
                return cfire::make_error(plan.error());
            }
            int rc = 0;
            std::string output = cfire::bridge::run_command_capture("\"" + bin_file.string() + "\" 2>&1", rc);
            std::filesystem::remove(bin_file, ec);
            return output;
        }
    );

    return srv;
}

inline auto serve_stdio(const std::filesystem::path& runtime_dir) -> int {
    auto srv = build_compiler_mcp_server(runtime_dir);
    return srv.serve_stdio();
}

} // namespace cfire::mcp

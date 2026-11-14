#pragma once
// src/cli/dispatch.hpp — Split Translation Unit Entrypoints (CF-198)

#include "../build/toolchain_layout.hpp"
#include <filesystem>
#include <string>
#include <string_view>

namespace cfire::cli::tu {

auto run_doctor_tu(std::string_view sub, int argc, char** argv, const cfire::build::ToolchainLayout& layout) -> int;
auto run_lsp_tu(int argc, char** argv) -> int;
auto run_mcp_tu(std::string_view sub, int argc, char** argv, const std::filesystem::path& runtime_dir) -> int;
auto run_docgen_tu(int argc, char** argv, const cfire::build::ToolchainLayout& layout) -> int;
auto run_inspect_fmt_tu(std::string_view sub, int argc, char** argv, const std::filesystem::path& runtime_dir) -> int;
auto run_scaffold_tu(std::string_view sub, int argc, char** argv, const std::filesystem::path& runtime_dir) -> int;
auto run_dev_server_tu(const std::filesystem::path& root, int port, bool open_browser, const std::string& explicit_entry = "") -> int;
auto run_check_fix_tu(std::string_view sub, int argc, char** argv, const std::filesystem::path& runtime_dir) -> int;
auto run_build_tu(std::string_view sub, int argc, char** argv, const std::filesystem::path& runtime_dir) -> int;

} // namespace cfire::cli::tu

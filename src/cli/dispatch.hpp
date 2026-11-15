#pragma once
// src/cli/dispatch.hpp — Split Translation Unit Entrypoints

#include "../build/toolchain_layout.hpp"
#include <filesystem>
#include <string>
#include <string_view>

namespace cfire::cli {

auto run_toolchain_commands(std::string_view sub, int argc, char** argv, const cfire::build::ToolchainLayout& layout) -> int;
auto run_lsp_command(int argc, char** argv) -> int;
auto run_mcp_and_ide_commands(std::string_view sub, int argc, char** argv, const std::filesystem::path& runtime_dir) -> int;
auto run_doc_command(int argc, char** argv, const cfire::build::ToolchainLayout& layout) -> int;
auto run_inspect_and_fmt_commands(std::string_view sub, int argc, char** argv, const std::filesystem::path& runtime_dir) -> int;
auto run_project_commands(std::string_view sub, int argc, char** argv, const std::filesystem::path& runtime_dir) -> int;
auto run_dev_server_command(const std::filesystem::path& root, int port, bool open_browser, const std::string& explicit_entry = "") -> int;
auto run_check_commands(std::string_view sub, int argc, char** argv, const std::filesystem::path& runtime_dir) -> int;
auto run_build_commands(std::string_view sub, int argc, char** argv, const std::filesystem::path& runtime_dir) -> int;

} // namespace cfire::cli

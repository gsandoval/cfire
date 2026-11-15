// src/main.cpp — Native C++23 CLI Driver for cfire (.cf)

#include "version.hpp"
#include "build/toolchain_config.hpp"
#include "build/toolchain_layout.hpp"
#include "cli/dispatch.hpp"
#include "cli/usage.hpp"

#include <filesystem>
#if __has_include(<print>)
#include <print>
#endif
#include <string>
#include <string_view>
#include <vector>

auto main(int argc, char** argv) -> int {
    cfire::build::cli_toolchain_overrides() = {};

    std::vector<char*> eff_argv;
    eff_argv.reserve(static_cast<std::size_t>(argc));
    if (argc > 0) {
        eff_argv.push_back(argv[0]);
    }

    const bool raw_config_cmd = (argc >= 2 && std::string_view(argv[1]) == "config");
    bool stop_override_scan = raw_config_cmd;
    for (int i = 1; i < argc; ++i) {
        std::string_view arg = argv[i];
        if (!stop_override_scan && arg == "--") {
            stop_override_scan = true;
            eff_argv.push_back(argv[i]);
            continue;
        }
        if (!stop_override_scan) {
            auto try_consume_tool = [&](std::string_view flag, std::string& dst) -> int {
                if (arg == flag) {
                    if (i + 1 >= argc) {
                        std::println(stderr, "error: flag '{}' requires a value", flag);
                        return -1;
                    }
                    std::string val = argv[++i];
                    if (val.empty()) {
                        std::println(stderr, "error: flag '{}' requires a non-empty value", flag);
                        return -1;
                    }
                    dst = std::move(val);
                    return 1;
                }
                std::string prefix = std::string(flag) + "=";
                if (arg.starts_with(prefix)) {
                    std::string val(arg.substr(prefix.size()));
                    if (val.empty()) {
                        std::println(stderr, "error: flag '{}' requires a non-empty value", flag);
                        return -1;
                    }
                    dst = std::move(val);
                    return 1;
                }
                return 0;
            };

            auto& ov = cfire::build::cli_toolchain_overrides();
            int consumed = 0;
            if ((consumed = try_consume_tool("--cxx", ov.cxx)) != 0 ||
                (consumed = try_consume_tool("--cc", ov.cc)) != 0 ||
                (consumed = try_consume_tool("--emxx", ov.emxx)) != 0 ||
                (consumed = try_consume_tool("--ar", ov.ar)) != 0 ||
                (consumed = try_consume_tool("--ld", ov.ld)) != 0) {
                if (consumed < 0) return 1;
                continue;
            }
        }
        eff_argv.push_back(argv[i]);
    }

    const int eff_argc = static_cast<int>(eff_argv.size());
    char** eff_ptr = eff_argv.data();

    if (eff_argc < 2) {
        cfire::cli::print_usage();
        return 1;
    }

    std::string sub = eff_ptr[1];
    auto toolchain_layout = cfire::build::locate_toolchain_layout(eff_ptr[0]);
    auto runtime_dir = toolchain_layout.runtime_dir;

    if (sub == "--version" || sub == "-v" || sub == "version") {
        std::println("cfire {} ({}, ISO C++23 systems toolchain)", cfire::version::kVersion, cfire::version::kGitSha);
        std::error_code ec;
        if (!runtime_dir.empty() && std::filesystem::exists(runtime_dir / "cfire_prelude.hpp", ec)) {
            auto prov = cfire::version::read_runtime_provenance(runtime_dir);
            if (!prov.matched) {
                std::println("warning: {} (at {})", prov.mismatch_reason, runtime_dir.string());
            }
        }
        return 0;
    }

    if (sub == "--help" || sub == "-h" || sub == "help") {
        cfire::cli::print_usage();
        return 0;
    }
    for (int i = 2; i < eff_argc; ++i) {
        std::string_view a = eff_ptr[i];
        if (a == "--") break;
        if (a == "--help" || a == "-h") {
            if (!cfire::cli::print_command_usage(sub)) cfire::cli::print_usage();
            return 0;
        }
    }

    if (sub == "doctor" || sub == "setup" || sub == "install" || sub == "config") {
        return cfire::cli::run_toolchain_commands(sub, eff_argc, eff_ptr, toolchain_layout);
    }
    if (sub == "lsp") {
        return cfire::cli::run_lsp_command(eff_argc, eff_ptr);
    }
    if (sub == "mcp" || sub == "ide") {
        return cfire::cli::run_mcp_and_ide_commands(sub, eff_argc, eff_ptr, runtime_dir);
    }
    if (sub == "doc") {
        return cfire::cli::run_doc_command(eff_argc, eff_ptr, toolchain_layout);
    }
    if (sub == "fmt" || sub == "outline" || sub == "inspect") {
        return cfire::cli::run_inspect_and_fmt_commands(sub, eff_argc, eff_ptr, runtime_dir);
    }
    if (sub == "init" || sub == "new" || sub == "eval" || sub == "refactor" || sub == "vendor" ||
        sub == "serve" || sub == "server") {
        return cfire::cli::run_project_commands(sub, eff_argc, eff_ptr, runtime_dir);
    }
    if (sub == "check" || sub == "fix") {
        return cfire::cli::run_check_commands(sub, eff_argc, eff_ptr, runtime_dir);
    }
    if (sub == "clean" || sub == "emit-cpp" || sub == "build" || sub == "run" ||
        sub == "test" || sub == "bench" || sub == "eject") {
        return cfire::cli::run_build_commands(sub, eff_argc, eff_ptr, runtime_dir);
    }

    cfire::cli::print_usage();
    return 1;
}

#if !defined(CFIRE_SPLIT_TUS)
#include "cli/toolchain_commands.cpp"
#include "lsp/server_command.cpp"
#include "cli/mcp_and_ide_commands.cpp"
#include "docgen/command.cpp"
#include "cli/inspect_and_fmt_commands.cpp"
#include "cli/project_commands.cpp"
#include "cli/check_commands.cpp"
#include "cli/build_commands.cpp"
#endif

// src/tu_mcp.cpp — Translation Unit for `cfire mcp` and `cfire ide` (CF-198)

#define CFIRE_TU_LSP_IDE_IMPL 1
#include "cli/dispatch.hpp"
#include "lsp/ide_setup.hpp"
#include "mcp.hpp"

#include <filesystem>
#if __has_include(<print>)
#include <print>
#endif
#include <string>
#include <string_view>

namespace cfire::cli::tu {

auto run_mcp_tu(std::string_view sub, int argc, char** argv, const std::filesystem::path& runtime_dir) -> int {
    if (sub == "mcp") {
        bool setup_mode = false;
        bool print_cfg = false;
        bool global_scope = false;
        std::string target = "auto";
        std::filesystem::path ws_dir = std::filesystem::current_path();
        bool allow_outside_root = false;
        for (int i = 2; i < argc; ++i) {
            std::string_view arg = argv[i];
            if (arg == "--allow-outside-root") {
                allow_outside_root = true;
                continue;
            }
            if (arg == "--setup" || arg == "setup" || arg == "--config" || arg == "config") {
                setup_mode = true;
            } else if (arg == "--print-config" || arg == "print-config") {
                print_cfg = true;
            } else if (arg == "--global" || arg == "-g") {
                global_scope = true;
            } else if (arg == "--dir" && i + 1 < argc) {
                ws_dir = argv[++i];
            } else if (arg == "--all" || arg == "all" || !arg.starts_with("-")) {
                target = std::string(arg);
            }
        }
        if (print_cfg) {
            std::print("{}", cfire::lsp::format_mcp_config_snippet(runtime_dir.parent_path()));
            return 0;
        }
        if (setup_mode) {
            auto res = cfire::lsp::configure_ide(ws_dir, target, global_scope);
            if (!res) {
                std::println(stderr, "{}", res.error());
                return 1;
            }
            std::print("{}", *res);
            return 0;
        }
        // Q15 (CF-160): tools are confined to the workspace (`--dir`, default: cwd) unless explicitly opted out.
        return cfire::mcp::serve_stdio(runtime_dir, cfire::mcp::McpOptions{ws_dir, allow_outside_root});
    }

    // sub == "ide"
    std::string editor = "auto";
    bool global_scope = false;
    std::filesystem::path ws_dir = std::filesystem::current_path();
    for (int i = 2; i < argc; ++i) {
        std::string_view arg = argv[i];
        if (arg == "--global" || arg == "-g") {
            global_scope = true;
        } else if (arg == "--dir" && i + 1 < argc) {
            ws_dir = argv[++i];
        } else {
            editor = std::string(arg);
        }
    }
    auto res = cfire::lsp::configure_ide(ws_dir, editor, global_scope);
    if (!res) {
        std::println(stderr, "{}", res.error());
        return 1;
    }
    std::print("{}", *res);
    return 0;
}

} // namespace cfire::cli::tu

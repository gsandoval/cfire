// src/cli/inspect_and_fmt_commands.cpp — Translation Unit for `fmt`, `outline`, and `inspect`

#define CFIRE_FMT_IMPL 1
#include "common_args.hpp"
#include "inspect_and_outline.hpp"
#include "dispatch.hpp"
#include "usage.hpp"
#include "../formatter.hpp"

#include <filesystem>
#if __has_include(<print>)
#include <print>
#endif
#include <string>
#include <string_view>

namespace cfire::cli {

auto run_inspect_and_fmt_commands(std::string_view sub, int argc, char** argv, const std::filesystem::path& runtime_dir) -> int {
    if (sub == "fmt") {
        bool check_only = false;
        bool to_stdout = false;
        std::filesystem::path target_path = ".";
        for (int i = 2; i < argc; ++i) {
            std::string_view arg = argv[i];
            if (arg == "--check") {
                check_only = true;
            } else if (arg == "--stdout") {
                to_stdout = true;
            } else if (arg.starts_with("-")) {
                // an unknown flag must not fall through to rewriting files in place.
                std::println(stderr, "error: unknown flag '{}' for 'cfire fmt' (expected --check or --stdout)", arg);
                return 2;
            } else {
                target_path = arg;
            }
        }
        auto res = cfire::fmt::format_path(target_path, check_only, to_stdout);
        if (!res) {
            std::println(stderr, "{}", res.error());
            return 1;
        }
        if (check_only && *res > 0) {
            std::println(stderr, "✗ {} file(s) need formatting (run `cfire fmt {}`)", *res, target_path.string());
            return 1;
        }
        if (!to_stdout) {
            std::println("✓ cfire fmt: {} ({} file(s) updated)", target_path.string(), *res);
        }
        return 0;
    }

    if (sub == "outline") {
        return cfire::cli::run_outline_subcommand(argc, argv);
    }

    // sub == "inspect"
    cfire::cli::CommonCliArgs args;
    if (auto err = cfire::cli::parse_common_cli_args(sub, argc, argv, args)) {
        return *err;
    }
    return cfire::cli::run_inspect_subcommand(
        args.target, runtime_dir, args.run_args, args.extra_includes, args.json_mode, args.extra_pkg_configs, args.extra_defines
    );
}

} // namespace cfire::cli

// src/tu_check_fix.cpp — Translation Unit for `check` and `fix` (CF-198)

#define CFIRE_TU_ANALYSIS_IMPL 1
#include "cli/cmd_check_fix.hpp"
#include "cli/cmd_common_args.hpp"
#include "cli/dispatch.hpp"
#include "cli/usage.hpp"

#include <filesystem>
#include <string_view>

namespace cfire::cli::tu {

auto run_check_fix_tu(std::string_view sub, int argc, char** argv, const std::filesystem::path& runtime_dir) -> int {
    cfire::cli::CommonCliArgs args;
    if (auto err = cfire::cli::parse_common_cli_args(sub, argc, argv, args)) {
        return *err;
    }
    if (sub == "fix" || (sub == "check" && args.fix_mode)) {
        int fix_rc = cfire::cli::run_fix_subcommand(sub, args.target, args.json_mode, args.dry_run, args.fix_only_codes);
        if (sub == "fix") return fix_rc;
    }
    return cfire::cli::run_check_subcommand(
        args.target, runtime_dir, args.json_mode, args.target_triple, args.sysroot,
        args.extra_includes, args.web_profile, args.subsystem,
        args.verify_cpp, !args.explicit_release
    );
}

} // namespace cfire::cli::tu

#pragma once
// src/build/syntax_check.hpp — Fast C++23 Clang Syntax Verification

#include "../clang_bridge.hpp"
#include "../diagnostics.hpp"
#include "pch.hpp"
#include "package_loader.hpp"
#include <algorithm>
#include <expected>
#include <filesystem>
#include <format>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

namespace cfire::build {

[[nodiscard]] inline auto verify_cpp_syntax_only(
    const std::filesystem::path& cf_file,
    std::string_view cpp_code,
    const BuildPlan& plan,
    const std::filesystem::path& runtime_dir,
    const std::vector<std::string>& extra_includes = {}
) -> std::expected<void, std::string> {
    auto gen_dir = cf_file.parent_path() / ".cfire" / "gen";
    std::error_code ec;
    std::filesystem::create_directories(gen_dir, ec);
    auto cpp_path = gen_dir / (cf_file.stem().string() + ".cpp");
    if (ec) {
        cpp_path = std::filesystem::temp_directory_path() / (cf_file.stem().string() + "_check.cpp");
    }
    {
        std::ofstream out(cpp_path, std::ios::binary | std::ios::trunc);
        if (!out) {
            return std::unexpected("failed to open generated C++ file for writing: " + cpp_path.string());
        }
        out << cpp_code;
        out.flush();
        if (!out.good()) {
            return std::unexpected("failed to write generated C++ code: " + cpp_path.string());
        }
    }

    bool has_trap = std::ranges::find(plan.cflags, "-fsanitize-trap=all") != plan.cflags.end();
    bool has_pthread = std::ranges::find(plan.cflags, "-pthread") != plan.cflags.end();
#if defined(__linux__)
    has_pthread = true;
#endif
    std::string pch_path;
    if (plan.target_triple.empty() && !plan.uses_objc && !g_debug_mode) {
        pch_path = ensure_prelude_pch(runtime_dir, false, has_trap, has_pthread);
    }
    std::string cmd = resolve_cxx_compiler() + " -O2";
    for (const auto& f : plan.cflags) cmd += " " + f;
    for (const auto& inc : extra_includes) cmd += " -I \"" + inc + "\"";
    if (!pch_path.empty()) {
        cmd += std::format(" -include-pch \"{}\"", pch_path);
    }
    if (plan.uses_objc) {
        cmd += " -x objective-c++";
    }
    cmd += std::format(" -fsyntax-only \"{}\" 2>&1", cpp_path.string());
    int rc = 0;
    auto output = bridge::run_command_capture(cmd, rc);
    if (rc != 0) {
        return std::unexpected(cfire::diag::humanize_cpp_compiler_output(output));
    }
    return {};
}

} // namespace cfire::build

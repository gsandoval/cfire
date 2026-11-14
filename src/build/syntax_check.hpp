#pragma once
// src/build/syntax_check.hpp — Fast C++23 Clang Syntax Verification

#include "../clang_bridge.hpp"
#include "../diagnostics.hpp"
#include "pch.hpp"
#include "package_loader.hpp"
#include <algorithm>
#include <cstdlib>
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
    auto cf_parent = cf_file.parent_path().empty() ? std::filesystem::path(".") : cf_file.parent_path();

    const bool dev_debug = is_dev_debug_profile();
    std::string opt_flags = dev_debug ? "-O0 -g" : "-O2";
    std::vector<std::string> eff_cflags = plan.cflags;
    for (const auto& inc : extra_includes) {
        eff_cflags.push_back("-I \"" + inc + "\"");
    }

    std::string pch_path;
    if (plan.target_triple.empty() && !plan.uses_objc) {
        std::string umbrella = extract_pch_umbrella_header(cpp_code);
        pch_path = ensure_composite_pch(runtime_dir, umbrella, opt_flags, eff_cflags);
    }

    std::string compiler = resolve_cxx_compiler(plan.target_triple);
    if (const char* trace_path = std::getenv("CFIRE_TRACE_CLANG_CHECK"); trace_path && *trace_path != '\0') {
        std::ofstream tf(trace_path, std::ios::app);
        if (tf.good()) {
            tf << compiler << '\n';
        }
    }

    auto build_stdin_cmd = [&](bool with_pch) {
        std::string c = compiler + " " + opt_flags;
        for (const auto& f : eff_cflags) c += " " + f;
        // Passed after ensure_composite_pch so per-directory -iquote does not alter the shared PCH key.
        c += std::format(" -iquote \"{}\"", cf_parent.string());
        if (with_pch && !pch_path.empty()) {
            c += std::format(" -include-pch \"{}\"{}", pch_path, pch_static_flag());
        }
        c += plan.uses_objc ? " -x objective-c++" : " -x c++";
        c += " -fsyntax-only - 2>&1";
        return c;
    };

    int rc = 0;
    auto output = bridge::run_command_capture_with_stdin(build_stdin_cmd(true), cpp_code, rc);
    if (rc != 0 && rc != -1 && !pch_path.empty()) {
        int fb_rc = 0;
        auto fb_out = bridge::run_command_capture_with_stdin(build_stdin_cmd(false), cpp_code, fb_rc);
        if (fb_rc == 0) {
            return {};
        }
        if (fb_rc == -1) {
            rc = -1;
        } else {
            rc = fb_rc;
            output = std::move(fb_out);
        }
    }

    // Fallback when stdin piping is unavailable (e.g. _WIN32 or fork error): use OS SecureTempDir, never <proj>/.cfire/gen.
    if (rc == -1) {
        cfire::detail::SecureTempDir temp_dir("cfire_check");
        auto cpp_path = temp_dir.file(cf_file.stem().string() + (plan.uses_objc ? "_check.mm" : "_check.cpp"));
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
        auto build_file_cmd = [&](bool with_pch) {
            std::string c = compiler + " " + opt_flags;
            for (const auto& f : eff_cflags) c += " " + f;
            c += std::format(" -iquote \"{}\"", cf_parent.string());
            if (with_pch && !pch_path.empty()) {
                c += std::format(" -include-pch \"{}\"{}", pch_path, pch_static_flag());
            }
            if (plan.uses_objc) {
                c += " -x objective-c++";
            }
            c += std::format(" -fsyntax-only \"{}\" 2>&1", cpp_path.string());
            return c;
        };
        rc = 0;
        output = bridge::run_command_capture(build_file_cmd(true), rc);
        if (rc != 0 && !pch_path.empty()) {
            int fb_rc = 0;
            auto fb_out = bridge::run_command_capture(build_file_cmd(false), fb_rc);
            if (fb_rc == 0) {
                return {};
            }
            rc = fb_rc;
            output = std::move(fb_out);
        }
    }

    if (rc != 0) {
        return std::unexpected(cfire::diag::humanize_cpp_compiler_output(output));
    }
    return {};
}

} // namespace cfire::build

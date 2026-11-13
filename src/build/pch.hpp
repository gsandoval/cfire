#pragma once
// src/build/pch.hpp — Content-Hashed Precompiled Header (PCH) Pipeline

#include "../version.hpp"
#include "../clang_bridge.hpp"
#include "package_loader.hpp"
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <fstream>
#include <string>
#include <string_view>
#if !defined(_WIN32)
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace cfire::build {

inline bool g_debug_mode = false;
inline bool g_profile_mode = false;
// `--sanitize` selects ASan+UBSan; `--sanitize=thread` selects TSan (CF-164). Only read when sanitize is on.
inline std::string g_sanitizer_set = "address,undefined";

[[nodiscard]] inline auto sanitizer_opt_flags() -> std::string {
    return "-O1 -g -fno-omit-frame-pointer -fsanitize=" + g_sanitizer_set;
}

// CF-184: on a Windows host every native build links with `-static`, which MinGW clang also turns into the
// predefined `__STATIC__` macro. The PCH is built with it, and every compile that includes the PCH passes it too;
// otherwise clang rejects the PCH and the build silently recompiles without it.
[[nodiscard]] inline auto pch_static_flag() -> std::string_view {
#if defined(_WIN32)
    return " -static";
#else
    return "";
#endif
}

[[nodiscard]] inline auto resolve_cxx_compiler(std::string_view target_triple = "") -> std::string {
    if (const char* env_cxx = std::getenv("CFIRE_CXX"); env_cxx && *env_cxx) {
        return std::string(env_cxx);
    }
    if (!target_triple.empty() && !is_wasm_target(target_triple) && !is_ios_target(target_triple)) {
        std::string prefixed = std::format("{}-clang++", target_triple);
        int rc = 0;
#if defined(_WIN32)
        (void)bridge::run_command_capture(std::format("where {} >NUL 2>&1", prefixed), rc);
#else
        (void)bridge::run_command_capture(std::format("command -v {} >/dev/null 2>&1", prefixed), rc);
#endif
        if (rc == 0) {
            return prefixed;
        }
        if (const char* home = std::getenv("HOME"); home && *home) {
            std::filesystem::path opt_dir = std::filesystem::path(home) / ".local" / "opt";
            std::error_code ec;
            if (std::filesystem::is_directory(opt_dir, ec)) {
                for (const auto& entry : std::filesystem::directory_iterator(opt_dir, ec)) {
                    if (!entry.is_directory(ec)) continue;
                    auto cand = entry.path() / "bin" / prefixed;
                    if (std::filesystem::exists(cand, ec)) {
                        return cand.string();
                    }
                }
            }
        }
    }
    return "clang++";
}

[[nodiscard]] inline auto prelude_pch_path(
    const std::filesystem::path& runtime_dir,
    bool sanitize,
    bool test_mode = false,
    bool with_pthread = false
) -> std::filesystem::path {
    std::error_code ec;
#if defined(_WIN32)
    auto pch_dir = std::filesystem::temp_directory_path(ec) / "cfire_pch_cache";
#else
    auto uid = ::getuid();
    auto tmp_base = std::filesystem::temp_directory_path(ec);
    auto pch_dir = tmp_base / std::format("cfire-{}-pch", uid);
#endif
    std::filesystem::create_directories(pch_dir, ec);
#if !defined(_WIN32)
    ::chmod(pch_dir.c_str(), 0700);
#endif
    auto can_rt = std::filesystem::weakly_canonical(runtime_dir, ec);
    std::string cxx_bin = resolve_cxx_compiler();
    std::string san_tag = g_sanitizer_set == "thread" ? "tsan" : "san";
    std::string tag = sanitize ? (test_mode ? san_tag + "_test" : san_tag) : (test_mode ? "test_trap" : "opt_wrapv");
    if (with_pthread) {
        tag += "_pth";
    }
    if (!pch_static_flag().empty()) tag += "_static";

    std::size_t content_hash = 0;
    auto prelude_hdr = runtime_dir / "cfire_prelude.hpp";
    if (std::filesystem::exists(prelude_hdr, ec)) {
        std::ifstream f(prelude_hdr, std::ios::binary);
        if (f) {
            std::string content((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
            content_hash = std::hash<std::string>{}(content);
        }
    }

    std::string key = std::format("{}|{}|{}|{}|{}|{:016x}",
        (ec ? runtime_dir : can_rt).string(),
        cxx_bin,
        cfire::version::kVersion,
        cfire::version::kGitSha,
        tag,
        content_hash);
    std::size_t rt_hash = std::hash<std::string>{}(key);
    std::string pch_name = std::format("cfire_prelude_{}_{:08x}.pch",
        tag,
        static_cast<std::uint32_t>(rt_hash & 0xFFFFFFFFu));
    return pch_dir / pch_name;
}

[[nodiscard]] inline auto ensure_prelude_pch(
    const std::filesystem::path& runtime_dir,
    bool sanitize,
    bool test_mode = false,
    bool with_pthread = false
) -> std::string {
    auto prelude_hdr = runtime_dir / "cfire_prelude.hpp";
    std::error_code ec;
    if (!std::filesystem::exists(prelude_hdr, ec)) return "";

    auto pch_path = prelude_pch_path(runtime_dir, sanitize, test_mode, with_pthread);
    std::filesystem::create_directories(pch_path.parent_path(), ec);
    std::string cxx_bin = resolve_cxx_compiler();

    bool needs_build = !std::filesystem::exists(pch_path, ec);
    if (!needs_build) {
        auto hdr_time = std::filesystem::last_write_time(prelude_hdr, ec);
        auto pch_time = std::filesystem::last_write_time(pch_path, ec);
        if (!ec && hdr_time > pch_time) {
            needs_build = true;
        }
        auto test_rt_hdr = runtime_dir / "cfire_test_runtime.hpp";
        if (!needs_build && std::filesystem::exists(test_rt_hdr, ec)) {
            auto tr_time = std::filesystem::last_write_time(test_rt_hdr, ec);
            if (!ec && tr_time > pch_time) {
                needs_build = true;
            }
        }
    }

    if (needs_build) {
        std::string opt_flags;
        if (sanitize) {
            opt_flags = sanitizer_opt_flags();
            if (test_mode) {
                opt_flags += " -fsanitize=signed-integer-overflow,shift,integer-divide-by-zero -fsanitize-trap=all";
            }
        } else {
            if (test_mode) {
                opt_flags = "-O2 -fsanitize=signed-integer-overflow,shift,integer-divide-by-zero -fsanitize-trap=all";
            } else {
                opt_flags = "-O2 -fwrapv";
            }
        }
#if defined(__linux__)
        opt_flags += " -Wno-builtin-macro-redefined -D__cpp_concepts=202002L -pthread";
#else
        if (with_pthread) {
            opt_flags += " -pthread";
        }
#endif
        opt_flags += pch_static_flag();
        auto tmp_pch_path = pch_path;
        tmp_pch_path += std::format(".tmp.{}.pch", ::getpid());
        std::string cmd = std::format(
            "{} -std=c++2b -Wno-c++11-narrowing {} -x c++-header \"{}\" -o \"{}\"",
            cxx_bin, opt_flags, prelude_hdr.string(), tmp_pch_path.string());
        int rc = 0;
        (void)bridge::run_command_capture(cmd, rc);
        if (rc != 0) {
            std::filesystem::remove(tmp_pch_path, ec);
            return "";
        }
        std::filesystem::rename(tmp_pch_path, pch_path, ec);
        if (ec) {
            std::filesystem::remove(tmp_pch_path, ec);
        }
    }
    return pch_path.string();
}

} // namespace cfire::build
